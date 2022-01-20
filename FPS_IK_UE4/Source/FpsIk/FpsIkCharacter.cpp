// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FpsIkCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

//////////////////////////////////////////////////////////////////////////
// AFpsIkCharacter

AFpsIkCharacter::AFpsIkCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	m_CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
}

void AFpsIkCharacter::BeginPlay()
{
	Super::BeginPlay();
}


void AFpsIkCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (FMath::IsNearlyZero(GetVelocity().Size()))
	{
		UpdateIK(DeltaTime);
	}
}

void AFpsIkCharacter::UpdateIK(float DeltaTime)
{
	TraceFoot(m_LeftFootSocket, m_LeftFootOffset, m_LeftFootRotation, DeltaTime);
	TraceFoot(m_RightFootSocket, m_RightFootOffset, m_RightFootRotation, DeltaTime);
	UpdateHip(DeltaTime);
	UpdateFootEffector(m_LeftEffectorLocation, m_LeftFootOffset, DeltaTime);
	UpdateFootEffector(m_RightEffectorLocation, m_RightFootOffset, DeltaTime);
}

void AFpsIkCharacter::TraceFoot(FName SocketName, float& OutOffset, FRotator& OutRotation, float DeltaTime)
{
	FVector SocketLocation = GetMesh()->GetSocketLocation(SocketName);
	FVector ActorLocation = GetActorLocation();

	FVector Start = FVector(SocketLocation.X, SocketLocation.Y, ActorLocation.Z);
	FVector End = FVector(SocketLocation.X, SocketLocation.Y, ActorLocation.Z - m_CapsuleHalfHeight - m_TraceDistance);
	
	FHitResult Hit;
	FCollisionQueryParams CollisionParams;
	CollisionParams.bTraceComplex = true;
	CollisionParams.AddIgnoredActor(this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, CollisionParams))
	{
		OutOffset = (Hit.Location - Hit.TraceEnd).Size() - m_TraceDistance + m_AdjustOffset;
		FRotator NewRotation = FRotator(-FMath::RadiansToDegrees(FMath::Atan2(Hit.Normal.X, Hit.Normal.Z)), 0.0f, FMath::RadiansToDegrees(FMath::Atan2(Hit.Normal.Y, Hit.Normal.Z)));
		OutRotation = FMath::RInterpTo(OutRotation, NewRotation, DeltaTime, m_FootInterpSpeed);
	}
	else
	{
		OutOffset = 0;
	}
}

void AFpsIkCharacter::UpdateHip(float DeltaTime)
{
	float NewOffset = FMath::Min(FMath::Min(m_LeftFootOffset, m_RightFootOffset), 0.0f);
	NewOffset = (NewOffset < 0) ? NewOffset : 0;
	m_HipOffset = FMath::FInterpTo(m_HipOffset, NewOffset, DeltaTime, m_HipInterpSpeed);

	float NewCapsuleHalfHeight = m_CapsuleHalfHeight - FMath::Abs(m_HipOffset) / 2.0f;
	GetCapsuleComponent()->SetCapsuleHalfHeight(FMath::FInterpTo(GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), NewCapsuleHalfHeight, DeltaTime, m_HipInterpSpeed));
}

void AFpsIkCharacter::UpdateFootEffector(float& OutEffectorLocation, float FootOffset, float DeltaTime)
{
	OutEffectorLocation = FMath::FInterpTo(OutEffectorLocation, FootOffset - m_HipOffset, DeltaTime, m_FootInterpSpeed);
}

//////////////////////////////////////////////////////////////////////////
// Input

void AFpsIkCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &AFpsIkCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AFpsIkCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AFpsIkCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AFpsIkCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &AFpsIkCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &AFpsIkCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AFpsIkCharacter::OnResetVR);
}




void AFpsIkCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AFpsIkCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void AFpsIkCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void AFpsIkCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AFpsIkCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AFpsIkCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void AFpsIkCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

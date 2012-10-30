/***************************************************************************
 * (c) Beceem Communications Inc.
 * All Rights Reserved
 *
 * file  : CmHost.h
 * author: Rajeev Tirumala
 * date  : September 8 , 2006
 * brief : Definitions for Connection Management Requests structure
 *          which we will use to setup our connection structures.Its high
 *          time we had a header file for CmHost.cpp to isolate the way
 *          f/w sends DSx messages and the way we interpret them in code.
 *          Revision History
 *
 *   Date       Author   Version   Description
 *   08-Sep-06    Rajeev       0.1      Created
 ***************************************************************************/
#ifndef _CM_HOST_H
#define _CM_HOST_H

#pragma once
#pragma pack(push, 4)

#define DSX_MESSAGE_EXCHANGE_BUFFER        0xBF60AC84 /* This contains the pointer */
#define DSX_MESSAGE_EXCHANGE_BUFFER_SIZE   72000      /* 24 K Bytes */

/* brief structure stLocalSFAddRequest */
typedef struct stLocalSFAddRequestAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	struct bcm_connect_mgr_params sfParameterSet;
	/* USE_MEMORY_MANAGER(); */
} stLocalSFAddRequestAlt;

/* brief structure stLocalSFAddIndication */
typedef struct stLocalSFAddIndicationAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
	u8	u8CC;    /* < Confirmation Code */
	u8	u8Padd;  /* < 8-bit Padding */
	u16	u16Padd; /* < 16 bit Padding */
/* USE_MEMORY_MANAGER(); */
} stLocalSFAddIndicationAlt;

/* brief structure stLocalSFAddConfirmation */
typedef struct stLocalSFAddConfirmationAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
} stLocalSFAddConfirmationAlt;

/* brief structure stLocalSFChangeRequest */
typedef struct stLocalSFChangeRequestAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	/*
	 * Pointer location at which following connection manager param Structure can be read
	 * from the target. We only get the address location and we need to read out the
	 * entire connection manager param structure at the given location on target
	 */
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
	u8	u8CC;	 /* < Confirmation Code */
	u8	u8Padd;  /* < 8-bit Padding */
	u16	u16Padd; /* < 16 bit */
} stLocalSFChangeRequestAlt;

/* brief structure stLocalSFChangeConfirmation */
typedef struct stLocalSFChangeConfirmationAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
} stLocalSFChangeConfirmationAlt;

/* brief structure stLocalSFChangeIndication */
typedef struct stLocalSFChangeIndicationAlt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	/* brief 16bitCID */
	u16	u16CID;
	/* brief 16bitVCID */
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
	u8	u8CC;    /* < Confirmation Code */
	u8	u8Padd;  /* < 8-bit Padding */
	u16	u16Padd; /* < 16 bit */
} stLocalSFChangeIndicationAlt;

ULONG StoreCmControlResponseMessage(struct bcm_mini_adapter *Adapter, PVOID pvBuffer, UINT *puBufferLength);
int AllocAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);
int FreeAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);
ULONG SetUpTargetDsxBuffers(struct bcm_mini_adapter *Adapter);
BOOLEAN CmControlResponseMessage(struct bcm_mini_adapter *Adapter, PVOID pvBuffer);

#pragma pack(pop)

#endif

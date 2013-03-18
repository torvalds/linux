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

struct bcm_add_indication_alt {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	u16	u16CID;
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
	u8	u8CC;    /* < Confirmation Code */
	u8	u8Padd;
	u16	u16Padd;
};

struct bcm_change_indication {
	u8	u8Type;
	u8	u8Direction;
	u16	u16TID;
	u16	u16CID;
	u16	u16VCID;
	struct bcm_connect_mgr_params sfAuthorizedSet;
	struct bcm_connect_mgr_params sfAdmittedSet;
	struct bcm_connect_mgr_params sfActiveSet;
	u8	u8CC;    /* < Confirmation Code */
	u8	u8Padd;
	u16	u16Padd;
};

unsigned long StoreCmControlResponseMessage(struct bcm_mini_adapter *Adapter, void *pvBuffer, unsigned int *puBufferLength);
int AllocAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);
int FreeAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);
unsigned long SetUpTargetDsxBuffers(struct bcm_mini_adapter *Adapter);
BOOLEAN CmControlResponseMessage(struct bcm_mini_adapter *Adapter, void *pvBuffer);

#pragma pack(pop)

#endif

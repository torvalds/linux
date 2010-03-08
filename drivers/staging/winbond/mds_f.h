#ifndef __WINBOND_MDS_F_H
#define __WINBOND_MDS_F_H

#include "wbhal_s.h"
#include "core.h"

unsigned char Mds_initial(  struct wbsoft_priv *adapter );
void Mds_Destroy(  struct wbsoft_priv *adapter );
void Mds_Tx(  struct wbsoft_priv *adapter );
void Mds_SendComplete(  struct wbsoft_priv *adapter,  PT02_DESCRIPTOR pT02 );
void Mds_MpduProcess(  struct wbsoft_priv *adapter,  struct wb35_descriptor *pRxDes );
extern void DataDmp(u8 *pdata, u32 len, u32 offset);

// For data frame sending 20060802
u16 MDS_GetPacketSize(  struct wbsoft_priv *adapter );
void MDS_GetNextPacket(  struct wbsoft_priv *adapter,  struct wb35_descriptor *pDes );
void MDS_GetNextPacketComplete(  struct wbsoft_priv *adapter,  struct wb35_descriptor *pDes );
void MDS_SendResult(  struct wbsoft_priv *adapter,  u8 PacketId,  unsigned char SendOK );

#endif

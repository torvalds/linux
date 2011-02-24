#ifndef __WINBOND_MDS_F_H
#define __WINBOND_MDS_F_H

#include "wbhal.h"
#include "core.h"

unsigned char Mds_initial(struct wbsoft_priv *adapter);
void Mds_Tx(struct wbsoft_priv *adapter);
void Mds_SendComplete(struct wbsoft_priv *adapter, struct T02_descriptor *pt02);
void Mds_MpduProcess(struct wbsoft_priv *adapter, struct wb35_descriptor *prxdes);
extern void DataDmp(u8 *pdata, u32 len, u32 offset);

/* For data frame sending */
u16 MDS_GetPacketSize(struct wbsoft_priv *adapter);
void MDS_GetNextPacket(struct wbsoft_priv *adapter, struct wb35_descriptor *pdes);
void MDS_GetNextPacketComplete(struct wbsoft_priv *adapter, struct wb35_descriptor *pdes);
void MDS_SendResult(struct wbsoft_priv *adapter, u8 packetid, unsigned char sendok);

#endif

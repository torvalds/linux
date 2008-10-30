#ifndef __WINBOND_ADAPTER_H
#define __WINBOND_ADAPTER_H

#include <linux/wireless.h>

#include "bssdscpt.h"
#include "mto.h"
#include "wbhal_s.h"

#define WBLINUX_PACKET_ARRAY_SIZE (ETHERNET_TX_DESCRIPTORS*4)

#define WB_MAX_LINK_NAME_LEN 40

struct wb35_adapter {
	u32 adapterIndex;	// 20060703.4 Add for using padapterContext global adapter point

	WB_LOCALDESCRIPT sLocalPara;	// Myself connected parameters
	PWB_BSSDESCRIPTION asBSSDescriptElement;

	MLME_FRAME sMlmeFrame;	// connect to peerSTA parameters

	MTO_PARAMETERS sMtoPara;	// MTO_struct ...
	hw_data_t sHwData;	//For HAL
	MDS Mds;

	spinlock_t SpinLock;
	u32 shutdown;

	atomic_t ThreadCount;

	u32 RxByteCount;
	u32 TxByteCount;

	struct sk_buff *skb_array[WBLINUX_PACKET_ARRAY_SIZE];
	struct sk_buff *packet_return;
	s32 skb_SetIndex;
	s32 skb_GetIndex;
	s32 netif_state_stop;	// 1: stop  0: normal
	struct iw_statistics iw_stats;

	u8 LinkName[WB_MAX_LINK_NAME_LEN];
};

#endif

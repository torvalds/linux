#ifndef __WINBOND_CORE_H
#define __WINBOND_CORE_H

#include <linux/wireless.h>

#include "mlme_s.h"
#include "wbhal_s.h"
#include "mto.h"

#define WBLINUX_PACKET_ARRAY_SIZE (ETHERNET_TX_DESCRIPTORS*4)

#define WB_MAX_LINK_NAME_LEN 40

struct wbsoft_priv {
	u32 adapterIndex;	// 20060703.4 Add for using padapterContext global adapter point

	struct wb_local_para sLocalPara;	// Myself connected parameters

	MLME_FRAME sMlmeFrame;	// connect to peerSTA parameters

	struct wb35_mto_params sMtoPara;	// MTO_struct ...
	struct hw_data sHwData;	//For HAL
	struct wb35_mds Mds;

	spinlock_t SpinLock;

	atomic_t ThreadCount;

	u32 RxByteCount;
	u32 TxByteCount;

	struct sk_buff *packet_return;
	s32 netif_state_stop;	// 1: stop  0: normal
	struct iw_statistics iw_stats;

	u8 LinkName[WB_MAX_LINK_NAME_LEN];

	bool enabled;
};

#endif /* __WINBOND_CORE_H */

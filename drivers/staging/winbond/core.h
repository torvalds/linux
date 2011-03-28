#ifndef __WINBOND_CORE_H
#define __WINBOND_CORE_H

#include <linux/wireless.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "wbhal.h"
#include "mto.h"

#include "mac_structures.h"
#include "mds_s.h"

#define MAX_NUM_TX_MMPDU		2
#define MAX_MMPDU_SIZE			1512
#define MAX_NUM_RX_MMPDU		6

struct mlme_frame {
	s8		*pMMPDU;
	u16		len;
	u8		DataType;
	u8		IsInUsed;

	u8		TxMMPDU[MAX_NUM_TX_MMPDU][MAX_MMPDU_SIZE];
	u8		TxMMPDUInUse[(MAX_NUM_TX_MMPDU + 3) & ~0x03];

	u16		wNumTxMMPDU;
	u16		wNumTxMMPDUDiscarded;

	u8		RxMMPDU[MAX_NUM_RX_MMPDU][MAX_MMPDU_SIZE];
	u8		SaveRxBufSlotInUse[(MAX_NUM_RX_MMPDU + 3) & ~0x03];

	u16		wNumRxMMPDU;
	u16		wNumRxMMPDUDiscarded;

	u16		wNumRxMMPDUInMLME;	/* Number of the Rx MMPDU */
	u16		reserved_1;		/*  in MLME. */
						/*  excluding the discarded */
};

#define WBLINUX_PACKET_ARRAY_SIZE (ETHERNET_TX_DESCRIPTORS*4)

#define WB_MAX_LINK_NAME_LEN 40

struct wbsoft_priv {
	struct wb_local_para sLocalPara;	/* Myself connected
							parameters */

	struct mlme_frame sMlmeFrame;	/* connect to peerSTA parameters */

	struct wb35_mto_params sMtoPara;	/* MTO_struct ... */
	struct hw_data sHwData;	/*For HAL */
	struct wb35_mds Mds;

	atomic_t ThreadCount;

	u32 RxByteCount;
	u32 TxByteCount;

	u8 LinkName[WB_MAX_LINK_NAME_LEN];

	bool enabled;
};

#endif /* __WINBOND_CORE_H */

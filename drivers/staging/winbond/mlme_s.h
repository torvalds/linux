#ifndef __WINBOND_MLME_H
#define __WINBOND_MLME_H

#include <linux/types.h>
#include <linux/spinlock.h>

#include "mac_structures.h"
#include "mds_s.h"

/*
 * ==============================================
 * Global data structures
 * ==============================================
 */
#define MAX_NUM_TX_MMPDU		2
#define MAX_MMPDU_SIZE			1512
#define MAX_NUM_RX_MMPDU		6

struct mlme_frame {
	s8		*pMMPDU;
	u16		len;
	u8		DataType;
	u8		IsInUsed;

	spinlock_t	MLMESpinLock;

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

#endif

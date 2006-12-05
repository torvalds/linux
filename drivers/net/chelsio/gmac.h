/*****************************************************************************
 *                                                                           *
 * File: gmac.h                                                              *
 * $Revision: 1.6 $                                                          *
 * $Date: 2005/06/21 18:29:47 $                                              *
 * Description:                                                              *
 *  Generic MAC functionality.                                               *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_GMAC_H_
#define _CXGB_GMAC_H_

#include "common.h"

enum { MAC_STATS_UPDATE_FAST, MAC_STATS_UPDATE_FULL };
enum { MAC_DIRECTION_RX = 1, MAC_DIRECTION_TX = 2 };

struct cmac_statistics {
	/* Transmit */
	u64 TxOctetsOK;
	u64 TxOctetsBad;
	u64 TxUnicastFramesOK;
	u64 TxMulticastFramesOK;
	u64 TxBroadcastFramesOK;
	u64 TxPauseFrames;
	u64 TxFramesWithDeferredXmissions;
	u64 TxLateCollisions;
	u64 TxTotalCollisions;
	u64 TxFramesAbortedDueToXSCollisions;
	u64 TxUnderrun;
	u64 TxLengthErrors;
	u64 TxInternalMACXmitError;
	u64 TxFramesWithExcessiveDeferral;
	u64 TxFCSErrors;
	u64 TxJumboFramesOK;
	u64 TxJumboOctetsOK;

	/* Receive */
	u64 RxOctetsOK;
	u64 RxOctetsBad;
	u64 RxUnicastFramesOK;
	u64 RxMulticastFramesOK;
	u64 RxBroadcastFramesOK;
	u64 RxPauseFrames;
	u64 RxFCSErrors;
	u64 RxAlignErrors;
	u64 RxSymbolErrors;
	u64 RxDataErrors;
	u64 RxSequenceErrors;
	u64 RxRuntErrors;
	u64 RxJabberErrors;
	u64 RxInternalMACRcvError;
	u64 RxInRangeLengthErrors;
	u64 RxOutOfRangeLengthField;
	u64 RxFrameTooLongErrors;
	u64 RxJumboFramesOK;
	u64 RxJumboOctetsOK;
};

struct cmac_ops {
	void (*destroy)(struct cmac *);
	int (*reset)(struct cmac *);
	int (*interrupt_enable)(struct cmac *);
	int (*interrupt_disable)(struct cmac *);
	int (*interrupt_clear)(struct cmac *);
	int (*interrupt_handler)(struct cmac *);

	int (*enable)(struct cmac *, int);
	int (*disable)(struct cmac *, int);

	int (*loopback_enable)(struct cmac *);
	int (*loopback_disable)(struct cmac *);

	int (*set_mtu)(struct cmac *, int mtu);
	int (*set_rx_mode)(struct cmac *, struct t1_rx_mode *rm);

	int (*set_speed_duplex_fc)(struct cmac *, int speed, int duplex, int fc);
	int (*get_speed_duplex_fc)(struct cmac *, int *speed, int *duplex,
				   int *fc);

	const struct cmac_statistics *(*statistics_update)(struct cmac *, int);

	int (*macaddress_get)(struct cmac *, u8 mac_addr[6]);
	int (*macaddress_set)(struct cmac *, u8 mac_addr[6]);
};

typedef struct _cmac_instance cmac_instance;

struct cmac {
	struct cmac_statistics stats;
	adapter_t *adapter;
	struct cmac_ops *ops;
	cmac_instance *instance;
};

struct gmac {
	unsigned int stats_update_period;
	struct cmac *(*create)(adapter_t *adapter, int index);
	int (*reset)(adapter_t *);
};

extern struct gmac t1_pm3393_ops;
extern struct gmac t1_chelsio_mac_ops;
extern struct gmac t1_vsc7321_ops;
extern struct gmac t1_vsc7326_ops;
extern struct gmac t1_ixf1010_ops;
extern struct gmac t1_dummy_mac_ops;

#endif /* _CXGB_GMAC_H_ */

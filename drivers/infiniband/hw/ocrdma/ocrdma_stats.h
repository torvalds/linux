/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2014 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

#ifndef __OCRDMA_STATS_H__
#define __OCRDMA_STATS_H__

#include <linux/debugfs.h>
#include "ocrdma.h"
#include "ocrdma_hw.h"

#define OCRDMA_MAX_DBGFS_MEM 4096

enum OCRDMA_STATS_TYPE {
	OCRDMA_RSRC_STATS,
	OCRDMA_RXSTATS,
	OCRDMA_WQESTATS,
	OCRDMA_TXSTATS,
	OCRDMA_DB_ERRSTATS,
	OCRDMA_RXQP_ERRSTATS,
	OCRDMA_TXQP_ERRSTATS,
	OCRDMA_TX_DBG_STATS,
	OCRDMA_RX_DBG_STATS,
	OCRDMA_DRV_STATS,
	OCRDMA_RESET_STATS
};

void ocrdma_rem_debugfs(void);
void ocrdma_init_debugfs(void);
void ocrdma_rem_port_stats(struct ocrdma_dev *dev);
void ocrdma_add_port_stats(struct ocrdma_dev *dev);
int ocrdma_pma_counters(struct ocrdma_dev *dev,
			struct ib_mad *out_mad);

#endif	/* __OCRDMA_STATS_H__ */

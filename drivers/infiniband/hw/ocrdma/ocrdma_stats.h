/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

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

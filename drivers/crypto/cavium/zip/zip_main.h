/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#ifndef __ZIP_MAIN_H__
#define __ZIP_MAIN_H__

#include "zip_device.h"
#include "zip_regs.h"

/* PCI device IDs */
#define PCI_DEVICE_ID_THUNDERX_ZIP   0xA01A

/* ZIP device BARs */
#define PCI_CFG_ZIP_PF_BAR0   0  /* Base addr for normal regs */

/* Maximum available zip queues */
#define ZIP_MAX_NUM_QUEUES    8

#define ZIP_128B_ALIGN        7

/* Command queue buffer size */
#define ZIP_CMD_QBUF_SIZE     (8064 + 8)

struct zip_registers {
	char  *reg_name;
	u64   reg_offset;
};

/* ZIP Compression - Decompression stats */
struct zip_stats {
	atomic64_t    comp_req_submit;
	atomic64_t    comp_req_complete;
	atomic64_t    decomp_req_submit;
	atomic64_t    decomp_req_complete;
	atomic64_t    comp_in_bytes;
	atomic64_t    comp_out_bytes;
	atomic64_t    decomp_in_bytes;
	atomic64_t    decomp_out_bytes;
	atomic64_t    decomp_bad_reqs;
};

/* ZIP Instruction Queue */
struct zip_iq {
	u64        *sw_head;
	u64        *sw_tail;
	u64        *hw_tail;
	u64        done_cnt;
	u64        pend_cnt;
	u64        free_flag;

	/* ZIP IQ lock */
	spinlock_t  lock;
};

/* ZIP Device */
struct zip_device {
	u32               index;
	void __iomem      *reg_base;
	struct pci_dev    *pdev;

	/* Different ZIP Constants */
	u64               depth;
	u64               onfsize;
	u64               ctxsize;

	struct zip_iq     iq[ZIP_MAX_NUM_QUEUES];
	struct zip_stats  stats;
};

/* Prototypes */
struct zip_device *zip_get_device(int node_id);
int zip_get_node_id(void);
void zip_reg_write(u64 val, u64 __iomem *addr);
u64 zip_reg_read(u64 __iomem *addr);
void zip_update_cmd_bufs(struct zip_device *zip_dev, u32 queue);
u32 zip_load_instr(union zip_inst_s *instr, struct zip_device *zip_dev);

#endif /* ZIP_MAIN_H */

/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
/*! \file  cn23xx_device.h
 * \brief Host Driver: Routines that perform CN23XX specific operations.
 */

#ifndef __CN23XX_PF_DEVICE_H__
#define __CN23XX_PF_DEVICE_H__

#include "cn23xx_pf_regs.h"

/* Register address and configuration for a CN23XX devices.
 * If device specific changes need to be made then add a struct to include
 * device specific fields as shown in the commented section
 */
struct octeon_cn23xx_pf {
	/** PCI interrupt summary register */
	u8 __iomem *intr_sum_reg64;

	/** PCI interrupt enable register */
	u8 __iomem *intr_enb_reg64;

	/** The PCI interrupt mask used by interrupt handler */
	u64 intr_mask64;

	struct octeon_config *conf;
};

#define CN23XX_SLI_DEF_BP			0x40

struct oct_vf_stats {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 broadcast;
	u64 multicast;
};

int setup_cn23xx_octeon_pf_device(struct octeon_device *oct);

u32 cn23xx_pf_get_oq_ticks(struct octeon_device *oct, u32 time_intr_in_us);

int cn23xx_sriov_config(struct octeon_device *oct);

int cn23xx_fw_loaded(struct octeon_device *oct);

void cn23xx_tell_vf_its_macaddr_changed(struct octeon_device *oct, int vfidx,
					u8 *mac);

int cn23xx_get_vf_stats(struct octeon_device *oct, int ifidx,
			struct oct_vf_stats *stats);
#endif

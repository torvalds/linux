/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_RESETFRAMEWORK_H
#define PVR_ROGUE_FWIF_RESETFRAMEWORK_H

#include <linux/bits.h>
#include <linux/types.h>

#include "pvr_rogue_fwif_shared.h"

struct rogue_fwif_rf_registers {
	union {
		u64 cdmreg_cdm_cb_base;
		u64 cdmreg_cdm_ctrl_stream_base;
	};
	u64 cdmreg_cdm_cb_queue;
	u64 cdmreg_cdm_cb;
};

struct rogue_fwif_rf_cmd {
	/* THIS MUST BE THE LAST MEMBER OF THE CONTAINING STRUCTURE */
	struct rogue_fwif_rf_registers fw_registers __aligned(8);
};

#define ROGUE_FWIF_RF_CMD_SIZE sizeof(struct rogue_fwif_rf_cmd)

#endif /* PVR_ROGUE_FWIF_RESETFRAMEWORK_H */

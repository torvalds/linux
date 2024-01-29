/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _ICE_FWLOG_H_
#define _ICE_FWLOG_H_
#include "ice_adminq_cmd.h"

struct ice_hw;

/* Only a single log level should be set and all log levels under the set value
 * are enabled, e.g. if log level is set to ICE_FW_LOG_LEVEL_VERBOSE, then all
 * other log levels are included (except ICE_FW_LOG_LEVEL_NONE)
 */
enum ice_fwlog_level {
	ICE_FWLOG_LEVEL_NONE = 0,
	ICE_FWLOG_LEVEL_ERROR = 1,
	ICE_FWLOG_LEVEL_WARNING = 2,
	ICE_FWLOG_LEVEL_NORMAL = 3,
	ICE_FWLOG_LEVEL_VERBOSE = 4,
	ICE_FWLOG_LEVEL_INVALID, /* all values >= this entry are invalid */
};

struct ice_fwlog_module_entry {
	/* module ID for the corresponding firmware logging event */
	u16 module_id;
	/* verbosity level for the module_id */
	u8 log_level;
};

struct ice_fwlog_cfg {
	/* list of modules for configuring log level */
	struct ice_fwlog_module_entry module_entries[ICE_AQC_FW_LOG_ID_MAX];
	/* options used to configure firmware logging */
	u16 options;
#define ICE_FWLOG_OPTION_ARQ_ENA		BIT(0)
#define ICE_FWLOG_OPTION_UART_ENA		BIT(1)
	/* set before calling ice_fwlog_init() so the PF registers for firmware
	 * logging on initialization
	 */
#define ICE_FWLOG_OPTION_REGISTER_ON_INIT	BIT(2)
	/* set in the ice_fwlog_get() response if the PF is registered for FW
	 * logging events over ARQ
	 */
#define ICE_FWLOG_OPTION_IS_REGISTERED		BIT(3)

	/* minimum number of log events sent per Admin Receive Queue event */
	u16 log_resolution;
};

struct ice_fwlog_data {
	u16 data_size;
	u8 *data;
};

struct ice_fwlog_ring {
	struct ice_fwlog_data *rings;
	u16 index;
	u16 size;
	u16 head;
	u16 tail;
};

#define ICE_FWLOG_RING_SIZE_INDEX_DFLT 3
#define ICE_FWLOG_RING_SIZE_DFLT 256
#define ICE_FWLOG_RING_SIZE_MAX 512

bool ice_fwlog_ring_full(struct ice_fwlog_ring *rings);
bool ice_fwlog_ring_empty(struct ice_fwlog_ring *rings);
void ice_fwlog_ring_increment(u16 *item, u16 size);
void ice_fwlog_set_supported(struct ice_hw *hw);
bool ice_fwlog_supported(struct ice_hw *hw);
int ice_fwlog_init(struct ice_hw *hw);
void ice_fwlog_deinit(struct ice_hw *hw);
int ice_fwlog_set(struct ice_hw *hw, struct ice_fwlog_cfg *cfg);
int ice_fwlog_get(struct ice_hw *hw, struct ice_fwlog_cfg *cfg);
int ice_fwlog_register(struct ice_hw *hw);
int ice_fwlog_unregister(struct ice_hw *hw);
void ice_fwlog_realloc_rings(struct ice_hw *hw, int index);
#endif /* _ICE_FWLOG_H_ */

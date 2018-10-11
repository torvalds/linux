/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_HW_H_
#define _IGC_HW_H_

#include <linux/types.h>
#include <linux/if_ether.h>
#include "igc_regs.h"
#include "igc_defines.h"
#include "igc_mac.h"
#include "igc_i225.h"

#define IGC_DEV_ID_I225_LM			0x15F2
#define IGC_DEV_ID_I225_V			0x15F3

/* Function pointers for the MAC. */
struct igc_mac_operations {
};

enum igc_mac_type {
	igc_undefined = 0,
	igc_i225,
	igc_num_macs  /* List is 1-based, so subtract 1 for true count. */
};

enum igc_phy_type {
	igc_phy_unknown = 0,
	igc_phy_none,
	igc_phy_i225,
};

struct igc_mac_info {
	struct igc_mac_operations ops;

	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];

	enum igc_mac_type type;

	u32 collision_delta;
	u32 ledctl_default;
	u32 ledctl_mode1;
	u32 ledctl_mode2;
	u32 mc_filter_type;
	u32 tx_packet_delta;
	u32 txcw;

	u16 mta_reg_count;
	u16 uta_reg_count;

	u16 rar_entry_count;

	u8 forced_speed_duplex;

	bool adaptive_ifs;
	bool has_fwsm;
	bool arc_subsystem_valid;

	bool autoneg;
	bool autoneg_failed;
};

struct igc_bus_info {
	u16 func;
	u16 pci_cmd_word;
};

struct igc_hw {
	void *back;

	u8 __iomem *hw_addr;
	unsigned long io_base;

	struct igc_mac_info  mac;

	struct igc_bus_info bus;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8 revision_id;
};

s32  igc_read_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value);
s32  igc_write_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value);
void igc_read_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value);
void igc_write_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value);

#endif /* _IGC_HW_H_ */

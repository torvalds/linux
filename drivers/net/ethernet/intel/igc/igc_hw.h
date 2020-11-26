/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_HW_H_
#define _IGC_HW_H_

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>

#include "igc_regs.h"
#include "igc_defines.h"
#include "igc_mac.h"
#include "igc_phy.h"
#include "igc_nvm.h"
#include "igc_i225.h"
#include "igc_base.h"

#define IGC_DEV_ID_I225_LM			0x15F2
#define IGC_DEV_ID_I225_V			0x15F3
#define IGC_DEV_ID_I225_I			0x15F8
#define IGC_DEV_ID_I220_V			0x15F7
#define IGC_DEV_ID_I225_K			0x3100
#define IGC_DEV_ID_I225_K2			0x3101
#define IGC_DEV_ID_I225_LMVP			0x5502
#define IGC_DEV_ID_I225_IT			0x0D9F
#define IGC_DEV_ID_I226_LM			0x125B
#define IGC_DEV_ID_I226_V			0x125C
#define IGC_DEV_ID_I226_IT			0x125D
#define IGC_DEV_ID_I221_V			0x125E
#define IGC_DEV_ID_I226_BLANK_NVM		0x125F
#define IGC_DEV_ID_I225_BLANK_NVM		0x15FD

/* Function pointers for the MAC. */
struct igc_mac_operations {
	s32 (*check_for_link)(struct igc_hw *hw);
	s32 (*reset_hw)(struct igc_hw *hw);
	s32 (*init_hw)(struct igc_hw *hw);
	s32 (*setup_physical_interface)(struct igc_hw *hw);
	void (*rar_set)(struct igc_hw *hw, u8 *address, u32 index);
	s32 (*read_mac_addr)(struct igc_hw *hw);
	s32 (*get_speed_and_duplex)(struct igc_hw *hw, u16 *speed,
				    u16 *duplex);
	s32 (*acquire_swfw_sync)(struct igc_hw *hw, u16 mask);
	void (*release_swfw_sync)(struct igc_hw *hw, u16 mask);
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

enum igc_media_type {
	igc_media_type_unknown = 0,
	igc_media_type_copper = 1,
	igc_num_media_types
};

enum igc_nvm_type {
	igc_nvm_unknown = 0,
	igc_nvm_eeprom_spi,
	igc_nvm_flash_hw,
	igc_nvm_invm,
};

struct igc_info {
	s32 (*get_invariants)(struct igc_hw *hw);
	struct igc_mac_operations *mac_ops;
	const struct igc_phy_operations *phy_ops;
	struct igc_nvm_operations *nvm_ops;
};

extern const struct igc_info igc_base_info;

struct igc_mac_info {
	struct igc_mac_operations ops;

	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];

	enum igc_mac_type type;

	u32 mc_filter_type;

	u16 mta_reg_count;
	u16 uta_reg_count;

	u32 mta_shadow[MAX_MTA_REG];
	u16 rar_entry_count;

	u8 forced_speed_duplex;

	bool asf_firmware_present;
	bool arc_subsystem_valid;

	bool autoneg;
	bool autoneg_failed;
	bool get_link_status;
};

struct igc_nvm_operations {
	s32 (*acquire)(struct igc_hw *hw);
	s32 (*read)(struct igc_hw *hw, u16 offset, u16 i, u16 *data);
	void (*release)(struct igc_hw *hw);
	s32 (*write)(struct igc_hw *hw, u16 offset, u16 i, u16 *data);
	s32 (*update)(struct igc_hw *hw);
	s32 (*validate)(struct igc_hw *hw);
	s32 (*valid_led_default)(struct igc_hw *hw, u16 *data);
};

struct igc_phy_operations {
	s32 (*acquire)(struct igc_hw *hw);
	s32 (*check_reset_block)(struct igc_hw *hw);
	s32 (*force_speed_duplex)(struct igc_hw *hw);
	s32 (*get_phy_info)(struct igc_hw *hw);
	s32 (*read_reg)(struct igc_hw *hw, u32 address, u16 *data);
	void (*release)(struct igc_hw *hw);
	s32 (*reset)(struct igc_hw *hw);
	s32 (*write_reg)(struct igc_hw *hw, u32 address, u16 data);
};

struct igc_nvm_info {
	struct igc_nvm_operations ops;
	enum igc_nvm_type type;

	u16 word_size;
	u16 delay_usec;
	u16 address_bits;
	u16 opcode_bits;
	u16 page_size;
};

struct igc_phy_info {
	struct igc_phy_operations ops;

	enum igc_phy_type type;

	u32 addr;
	u32 id;
	u32 reset_delay_us; /* in usec */
	u32 revision;

	enum igc_media_type media_type;

	u16 autoneg_advertised;
	u16 autoneg_mask;

	u8 mdix;

	bool is_mdix;
	bool speed_downgraded;
	bool autoneg_wait_to_complete;
};

struct igc_bus_info {
	u16 func;
	u16 pci_cmd_word;
};

enum igc_fc_mode {
	igc_fc_none = 0,
	igc_fc_rx_pause,
	igc_fc_tx_pause,
	igc_fc_full,
	igc_fc_default = 0xFF
};

struct igc_fc_info {
	u32 high_water;     /* Flow control high-water mark */
	u32 low_water;      /* Flow control low-water mark */
	u16 pause_time;     /* Flow control pause timer */
	bool send_xon;      /* Flow control send XON */
	bool strict_ieee;   /* Strict IEEE mode */
	enum igc_fc_mode current_mode; /* Type of flow control */
	enum igc_fc_mode requested_mode;
};

struct igc_dev_spec_base {
	bool clear_semaphore_once;
	bool eee_enable;
};

struct igc_hw {
	void *back;

	u8 __iomem *hw_addr;
	unsigned long io_base;

	struct igc_mac_info  mac;
	struct igc_fc_info   fc;
	struct igc_nvm_info  nvm;
	struct igc_phy_info  phy;

	struct igc_bus_info bus;

	union {
		struct igc_dev_spec_base	_base;
	} dev_spec;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8 revision_id;
};

/* Statistics counters collected by the MAC */
struct igc_hw_stats {
	u64 crcerrs;
	u64 algnerrc;
	u64 symerrs;
	u64 rxerrc;
	u64 mpc;
	u64 scc;
	u64 ecol;
	u64 mcc;
	u64 latecol;
	u64 colc;
	u64 dc;
	u64 tncrs;
	u64 sec;
	u64 cexterr;
	u64 rlec;
	u64 xonrxc;
	u64 xontxc;
	u64 xoffrxc;
	u64 xofftxc;
	u64 fcruc;
	u64 prc64;
	u64 prc127;
	u64 prc255;
	u64 prc511;
	u64 prc1023;
	u64 prc1522;
	u64 tlpic;
	u64 rlpic;
	u64 gprc;
	u64 bprc;
	u64 mprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 rnbc;
	u64 ruc;
	u64 rfc;
	u64 roc;
	u64 rjc;
	u64 mgprc;
	u64 mgpdc;
	u64 mgptc;
	u64 tor;
	u64 tot;
	u64 tpr;
	u64 tpt;
	u64 ptc64;
	u64 ptc127;
	u64 ptc255;
	u64 ptc511;
	u64 ptc1023;
	u64 ptc1522;
	u64 mptc;
	u64 bptc;
	u64 tsctc;
	u64 tsctfc;
	u64 iac;
	u64 htdpmc;
	u64 rpthc;
	u64 hgptc;
	u64 hgorc;
	u64 hgotc;
	u64 lenerrs;
	u64 scvpc;
	u64 hrmpc;
	u64 doosync;
	u64 o2bgptc;
	u64 o2bspc;
	u64 b2ospc;
	u64 b2ogprc;
};

struct net_device *igc_get_hw_dev(struct igc_hw *hw);
#define hw_dbg(format, arg...) \
	netdev_dbg(igc_get_hw_dev(hw), format, ##arg)

s32  igc_read_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value);
s32  igc_write_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value);
void igc_read_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value);
void igc_write_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value);

#endif /* _IGC_HW_H_ */

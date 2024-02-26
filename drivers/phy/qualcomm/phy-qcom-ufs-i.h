/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2015, 2019-2021, Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef UFS_QCOM_PHY_I_H_
#define UFS_QCOM_PHY_I_H_

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/phy/phy-qcom-ufs.h>

#define UFS_QCOM_PHY_CAL_ENTRY(reg, val)	\
	{				\
		.reg_offset = reg,	\
		.cfg_value = val,	\
	}

#define UFS_QCOM_PHY_NAME_LEN	30

enum {
	MASK_SERDES_START       = 0x1,
	MASK_PCS_READY          = 0x1,
};

enum {
	OFFSET_SERDES_START     = 0x0,
};

enum ufs_qcom_phy_submode {
	UFS_QCOM_PHY_SUBMODE_NON_G4,
	UFS_QCOM_PHY_SUBMODE_G4,
	UFS_QCOM_PHY_SUBMODE_G5,
};

struct ufs_qcom_phy_stored_attributes {
	u32 att;
	u32 value;
};


struct ufs_qcom_phy_calibration {
	u32 reg_offset;
	u32 cfg_value;
};

struct ufs_qcom_phy_vreg {
	const char *name;
	struct regulator *reg;
	int max_uA;
	int min_uV;
	int max_uV;
	bool enabled;
};

struct ufs_qcom_phy {
	struct list_head list;
	struct device *dev;
	void __iomem *mmio;
	void __iomem *dev_ref_clk_ctrl_mmio;
	struct clk *tx_iface_clk;
	struct clk *rx_iface_clk;
	bool is_iface_clk_enabled;
	struct clk *ref_clk_src;
	struct clk *ref_clk_parent;
	struct clk *ref_clk_pad_en;
	struct clk *ref_clk;
	struct clk *ref_aux_clk;
	struct clk *qref_clk;
	struct clk *rx_sym0_mux_clk;
	struct clk *rx_sym1_mux_clk;
	struct clk *tx_sym0_mux_clk;
	struct clk *rx_sym0_phy_clk;
	struct clk *rx_sym1_phy_clk;
	struct clk *tx_sym0_phy_clk;
	bool is_ref_clk_enabled;
	bool is_dev_ref_clk_enabled;
	struct ufs_qcom_phy_vreg vdda_pll;
	struct ufs_qcom_phy_vreg vdda_phy;
	struct ufs_qcom_phy_vreg vddp_ref_clk;
	struct ufs_qcom_phy_vreg vdd_phy_gdsc;
	struct ufs_qcom_phy_vreg vdda_qref;

	/* Number of lanes available (1 or 2) for Rx/Tx */
	u32 lanes_per_direction;

	unsigned int quirks;

	/**
	 * If UFS link is put into Hibern8 and if UFS PHY analog hardware is
	 * power collapsed (by clearing UFS_PHY_POWER_DOWN_CONTROL), Hibern8
	 * exit might fail even after powering on UFS PHY analog hardware.
	 * Enabling this quirk will help to solve above issue by doing
	 * custom PHY settings just before PHY analog power collapse.
	 */
	#define UFS_QCOM_PHY_QUIRK_HIBERN8_EXIT_AFTER_PHY_PWR_COLLAPSE	BIT(0)

	u8 host_ctrl_rev_major;
	u16 host_ctrl_rev_minor;
	u16 host_ctrl_rev_step;

	char name[UFS_QCOM_PHY_NAME_LEN];
	struct ufs_qcom_phy_calibration *cached_regs;
	int cached_regs_table_size;
	struct ufs_qcom_phy_specific_ops *phy_spec_ops;

	enum phy_mode mode;
	int submode;
	struct reset_control *ufs_reset;
	struct list_head regs_list_head;
};

/**
 * struct ufs_qcom_phy_specific_ops - set of pointers to functions which have a
 * specific implementation per phy. Each UFS phy, should implement
 * those functions according to its spec and requirements
 * @start_serdes: pointer to a function that starts the serdes
 * @is_physical_coding_sublayer_ready: pointer to a function that
 * checks pcs readiness. returns 0 for success and non-zero for error.
 * @set_tx_lane_enable: pointer to a function that enable tx lanes
 * @power_control: pointer to a function that controls analog rail of phy
 * and writes to QSERDES_RX_SIGDET_CNTRL attribute
 * @ctrl_rx_linecfg: pointer to a function that controls the enable/disable of
 * Rx line config
 * @get_tx_hs_equalizer: pointer to a function retrieving the tx hs equalizer setting
 * @dbg_register_dump: pointer to a function that dumps phy registers for debug.
 * @dbg_register_save: pointer to a function that save phy registers to memory.
 */
struct ufs_qcom_phy_specific_ops {
	int (*calibrate)(struct ufs_qcom_phy *ufs_qcom_phy, bool is_rate_B,
			 bool is_g4);
	void (*start_serdes)(struct ufs_qcom_phy *phy);
	int (*is_physical_coding_sublayer_ready)(struct ufs_qcom_phy *phy);
	void (*set_tx_lane_enable)(struct ufs_qcom_phy *phy, u32 val);
	void (*power_control)(struct ufs_qcom_phy *phy, bool val);
	void (*ctrl_rx_linecfg)(struct ufs_qcom_phy *phy, bool ctrl);
	u32 (*get_tx_hs_equalizer)(struct ufs_qcom_phy *phy, u32 gear);
	void (*dbg_register_dump)(struct ufs_qcom_phy *phy);
	void (*dbg_register_save)(struct ufs_qcom_phy *phy);
};

struct ufs_qcom_phy *get_ufs_qcom_phy(struct phy *generic_phy);
int ufs_qcom_phy_power_on(struct phy *generic_phy);
int ufs_qcom_phy_power_off(struct phy *generic_phy);
int ufs_qcom_phy_init_clks(struct ufs_qcom_phy *phy_common);
int ufs_qcom_phy_init_vregulators(struct ufs_qcom_phy *phy_common);
int ufs_qcom_phy_remove(struct phy *generic_phy,
		       struct ufs_qcom_phy *ufs_qcom_phy);
struct phy *ufs_qcom_phy_generic_probe(struct platform_device *pdev,
			struct ufs_qcom_phy *common_cfg,
			const struct phy_ops *ufs_qcom_phy_gen_ops,
			struct ufs_qcom_phy_specific_ops *phy_spec_ops);
int ufs_qcom_phy_get_reset(struct ufs_qcom_phy *phy_common);
int ufs_qcom_phy_calibrate(struct ufs_qcom_phy *ufs_qcom_phy,
			struct ufs_qcom_phy_calibration *tbl_A, int tbl_size_A,
			struct ufs_qcom_phy_calibration *tbl_B, int tbl_size_B,
			bool is_rate_B);
void ufs_qcom_phy_write_tbl(struct ufs_qcom_phy *ufs_qcom_phy,
			struct ufs_qcom_phy_calibration *tbl,
			int tbl_size);
int ufs_qcom_phy_dump_regs(struct ufs_qcom_phy *phy,
			    int offset, int len, char *prefix);

int ufs_qcom_phy_save_regs(struct ufs_qcom_phy *phy,
			    int offset, int len, char *prefix);
#endif

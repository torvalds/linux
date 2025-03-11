/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Code Construct
 *
 * Author: Jeremy Kerr <jk@codeconstruct.com.au>
 */

#include <linux/clk.h>
#include <linux/i3c/master.h>
#include <linux/reset.h>
#include <linux/types.h>

#define DW_I3C_MAX_DEVS 32

struct dw_i3c_master_caps {
	u8 cmdfifodepth;
	u8 datafifodepth;
};

struct dw_i3c_dat_entry {
	u8 addr;
	bool is_i2c_addr;
	struct i3c_dev_desc *ibi_dev;
};

struct dw_i3c_master {
	struct i3c_master_controller base;
	struct device *dev;
	u16 maxdevs;
	u16 datstartaddr;
	u32 free_pos;
	struct {
		struct list_head list;
		struct dw_i3c_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	struct dw_i3c_master_caps caps;
	void __iomem *regs;
	struct reset_control *core_rst;
	struct clk *core_clk;
	struct clk *pclk;
	char version[5];
	char type[5];
	u32 sir_rej_mask;
	bool i2c_slv_prsnt;
	u32 dev_addr;
	u32 i3c_pp_timing;
	u32 i3c_od_timing;
	u32 ext_lcnt_timing;
	u32 bus_free_timing;
	u32 i2c_fm_timing;
	u32 i2c_fmp_timing;
	u32 quirks;
	/*
	 * Per-device hardware data, used to manage the device address table
	 * (DAT)
	 *
	 * Locking: the devs array may be referenced in IRQ context while
	 * processing an IBI. However, IBIs (for a specific device, which
	 * implies a specific DAT entry) can only happen while interrupts are
	 * requested for that device, which is serialised against other
	 * insertions/removals from the array by the global i3c infrastructure.
	 * So, devs_lock protects against concurrent updates to devs->ibi_dev
	 * between request_ibi/free_ibi and the IBI irq event.
	 */
	struct dw_i3c_dat_entry devs[DW_I3C_MAX_DEVS];
	spinlock_t devs_lock;

	/* platform-specific data */
	const struct dw_i3c_platform_ops *platform_ops;

	struct work_struct hj_work;
};

struct dw_i3c_platform_ops {
	/*
	 * Called on early bus init: the i3c has been set up, but before any
	 * transactions have taken place. Platform implementations may use to
	 * perform actual device enabling with the i3c core ready.
	 */
	int (*init)(struct dw_i3c_master *i3c);

	/*
	 * Initialise a DAT entry to enable/disable IBIs. Allows the platform
	 * to perform any device workarounds on the DAT entry before
	 * inserting into the hardware table.
	 *
	 * Called with the DAT lock held; must not sleep.
	 */
	void (*set_dat_ibi)(struct dw_i3c_master *i3c,
			    struct i3c_dev_desc *dev, bool enable, u32 *reg);
};

extern int dw_i3c_common_probe(struct dw_i3c_master *master,
			       struct platform_device *pdev);
extern void dw_i3c_common_remove(struct dw_i3c_master *master);


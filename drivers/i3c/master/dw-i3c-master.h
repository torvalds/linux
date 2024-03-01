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
	struct i3c_dev_desc *ibi_dev;
};

struct dw_i3c_i2c_dev_data {
	u8 index;
	struct i3c_generic_ibi_pool *ibi_pool;
};

struct dw_i3c_master {
	struct i3c_master_controller base;
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
	char version[5];
	char type[5];
	bool ibi_capable;

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

	/* target mode data */
	struct {
		struct completion comp;
		struct completion rdata_comp;

		/* Used for handling private write */
		struct {
			void *buf;
			u16 max_len;
		} rx;
	} target;

	struct {
		unsigned long core_rate;
		unsigned long core_period;
		u32 i3c_od_scl_low;
		u32 i3c_od_scl_high;
		u32 i3c_pp_scl_low;
		u32 i3c_pp_scl_high;
	} timing;
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
	void (*set_sir_enabled)(struct dw_i3c_master *i3c,
				struct i3c_dev_desc *dev, u8 idx, bool enable);

	/* Enter the software force mode by isolating the SCL and SDA pins */
	void (*enter_sw_mode)(struct dw_i3c_master *i3c);

	/* Exit the software force mode */
	void (*exit_sw_mode)(struct dw_i3c_master *i3c);
	void (*toggle_scl_in)(struct dw_i3c_master *i3c, int count);
	void (*gen_internal_stop)(struct dw_i3c_master *i3c);
	void (*gen_target_reset_pattern)(struct dw_i3c_master *i3c);

	/* For target mode, pending read notification */
	void (*set_ibi_mdb)(struct dw_i3c_master *i3c, u8 mdb);

	/* DAT handling */
	int (*reattach_i3c_dev)(struct i3c_dev_desc *dev, u8 old_dyn_addr);
	int (*attach_i3c_dev)(struct i3c_dev_desc *dev);
	void (*detach_i3c_dev)(struct i3c_dev_desc *dev);
	int (*attach_i2c_dev)(struct i2c_dev_desc *dev);
	void (*detach_i2c_dev)(struct i2c_dev_desc *dev);
	int (*get_addr_pos)(struct dw_i3c_master *i3c, u8 addr);
	int (*flush_dat)(struct dw_i3c_master *i3c, u8 addr);
	void (*set_ibi_dev)(struct dw_i3c_master *i3c,
			    struct i3c_dev_desc *dev);
	struct i3c_dev_desc *(*get_ibi_dev)(struct dw_i3c_master *i3c, u8 addr);
};

extern int dw_i3c_common_probe(struct dw_i3c_master *master,
			       struct platform_device *pdev);
extern void dw_i3c_common_remove(struct dw_i3c_master *master);


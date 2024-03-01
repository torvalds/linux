// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Code Construct
 *
 * Author: Jeremy Kerr <jk@codeconstruct.com.au>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <dt-bindings/i3c/i3c.h>

#include "dw-i3c-master.h"

/* AST2600-specific global register set */
#define AST2600_I3CG_REG0(idx)	(((idx) * 4 * 4) + 0x10)
#define AST2600_I3CG_REG1(idx)	(((idx) * 4 * 4) + 0x14)

#define AST2600_I3CG_REG0_SDA_PULLUP_EN_MASK	GENMASK(29, 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_2K	(0x0 << 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_750	(0x2 << 28)
#define AST2600_I3CG_REG0_SDA_PULLUP_EN_545	(0x3 << 28)

#define AST2600_I3CG_REG1_I2C_MODE		BIT(0)
#define AST2600_I3CG_REG1_TEST_MODE		BIT(1)
#define AST2600_I3CG_REG1_ACT_MODE_MASK		GENMASK(3, 2)
#define AST2600_I3CG_REG1_ACT_MODE(x)		(((x) << 2) & AST2600_I3CG_REG1_ACT_MODE_MASK)
#define AST2600_I3CG_REG1_PENDING_INT_MASK	GENMASK(7, 4)
#define AST2600_I3CG_REG1_PENDING_INT(x)	(((x) << 4) & AST2600_I3CG_REG1_PENDING_INT_MASK)
#define AST2600_I3CG_REG1_SA_MASK		GENMASK(14, 8)
#define AST2600_I3CG_REG1_SA(x)			(((x) << 8) & AST2600_I3CG_REG1_SA_MASK)
#define AST2600_I3CG_REG1_SA_EN			BIT(15)
#define AST2600_I3CG_REG1_INST_ID_MASK		GENMASK(19, 16)
#define AST2600_I3CG_REG1_INST_ID(x)		(((x) << 16) & AST2600_I3CG_REG1_INST_ID_MASK)
#define SCL_SW_MODE_OE				BIT(20)
#define SCL_OUT_SW_MODE_VAL			BIT(21)
#define SCL_IN_SW_MODE_VAL			BIT(23)
#define SDA_SW_MODE_OE				BIT(24)
#define SDA_OUT_SW_MODE_VAL			BIT(25)
#define SDA_IN_SW_MODE_VAL			BIT(27)
#define SCL_IN_SW_MODE_EN			BIT(28)
#define SDA_IN_SW_MODE_EN			BIT(29)
#define SCL_OUT_SW_MODE_EN			BIT(30)
#define SDA_OUT_SW_MODE_EN			BIT(31)

#define AST2600_DEFAULT_SDA_PULLUP_OHMS		2000

#define DEV_ADDR_TABLE_LEGACY_I2C_DEV		BIT(31)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR		GENMASK(23, 16)
#define DEV_ADDR_TABLE_IBI_ADDR_MASK		GENMASK(25, 24)
#define   IBI_ADDR_MASK_OFF			0b00
#define   IBI_ADDR_MASK_LAST_3BITS		0b01
#define   IBI_ADDR_MASK_LAST_4BITS		0b10
#define DEV_ADDR_TABLE_DA_PARITY		BIT(23)
#define DEV_ADDR_TABLE_MR_REJECT		BIT(14)
#define DEV_ADDR_TABLE_SIR_REJECT		BIT(13)
#define DEV_ADDR_TABLE_IBI_MDB			BIT(12)
#define DEV_ADDR_TABLE_IBI_PEC			BIT(11)
#define DEV_ADDR_TABLE_STATIC_ADDR		GENMASK(6, 0)

#define DEV_ADDR_TABLE_LOC(start, idx)		((start) + ((idx) << 2))

#define DEVICE_CTRL				0x0
#define DEV_CTRL_SLAVE_MDB			GENMASK(23, 16)
#define DEV_CTRL_HOT_JOIN_NACK			BIT(8)

#define NUM_OF_SWDATS_IN_GROUP			8
#define ALL_DATS_IN_GROUP_ARE_FREE		((1 << NUM_OF_SWDATS_IN_GROUP) - 1)
#define NUM_OF_SWDAT_GROUP			16

#define ADDR_GRP_MASK				GENMASK(6, 3)
#define ADDR_GRP(x)				(((x) & ADDR_GRP_MASK) >> 3)
#define ADDR_HID_MASK				GENMASK(2, 0)
#define ADDR_HID(x)				((x) & ADDR_HID_MASK)

#define IBI_SIR_REQ_REJECT			0x30
#define INTR_STATUS_EN				0x40
#define INTR_SIGNAL_EN				0x44
#define   INTR_IBI_THLD_STAT			BIT(2)

struct ast2600_i3c_swdat_group {
	u32 dat[NUM_OF_SWDATS_IN_GROUP];
	u32 free_pos;
	int hw_index;
	struct {
		u32 set;
		u32 clr;
	} mask;
};

struct ast2600_i3c {
	struct dw_i3c_master dw;
	struct regmap *global_regs;
	unsigned int global_idx;
	unsigned int sda_pullup;

	struct ast2600_i3c_swdat_group dat_group[NUM_OF_SWDAT_GROUP];
};

static u8 even_parity(u8 p)
{
	p ^= p >> 4;
	p &= 0xf;

	return (0x9669 >> p) & 1;
}

static inline struct dw_i3c_master *
to_dw_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct dw_i3c_master, base);
}

static struct ast2600_i3c *to_ast2600_i3c(struct dw_i3c_master *dw)
{
	return container_of(dw, struct ast2600_i3c, dw);
}

static int ast2600_i3c_pullup_to_reg(unsigned int ohms, u32 *regp)
{
	u32 reg;

	switch (ohms) {
	case 2000:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_2K;
		break;
	case 750:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_750;
		break;
	case 545:
		reg = AST2600_I3CG_REG0_SDA_PULLUP_EN_545;
		break;
	default:
		return -EINVAL;
	}

	if (regp)
		*regp = reg;

	return 0;
}

static int ast2600_i3c_init(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	u32 reg = 0;
	int rc;

	/* reg0: set SDA pullup values */
	rc = ast2600_i3c_pullup_to_reg(i3c->sda_pullup, &reg);
	if (rc)
		return rc;

	rc = regmap_write(i3c->global_regs,
			  AST2600_I3CG_REG0(i3c->global_idx), reg);
	if (rc)
		return rc;

	/* reg1: set up the instance id, but leave everything else disabled,
	 * as it's all for client mode
	 */
	reg = AST2600_I3CG_REG1_INST_ID(i3c->global_idx);
	rc = regmap_write(i3c->global_regs,
			  AST2600_I3CG_REG1(i3c->global_idx), reg);

	return rc;
}

static void ast2600_i3c_set_dat_ibi(struct dw_i3c_master *i3c,
				    struct i3c_dev_desc *dev,
				    bool enable, u32 *dat)
{
	/*
	 * The ast2600 i3c controller will lock up on receiving 4n+1-byte IBIs
	 * if the PEC is disabled. We have no way to restrict the length of
	 * IBIs sent to the controller, so we need to unconditionally enable
	 * PEC checking, which means we drop a byte of payload data
	 */
	if (enable && dev->info.bcr & I3C_BCR_IBI_PAYLOAD) {
		dev_warn_once(&i3c->base.dev,
		      "Enabling PEC workaround. IBI payloads will be truncated\n");
		*dat |= DEV_ADDR_TABLE_IBI_PEC;
	}
}

static void ast2600_i3c_enter_sw_mode(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);

	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_IN_SW_MODE_VAL | SDA_IN_SW_MODE_VAL,
			  SCL_IN_SW_MODE_VAL | SDA_IN_SW_MODE_VAL);

	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN,
			  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN);
}

static void ast2600_i3c_exit_sw_mode(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);

	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN, 0);
}

static void ast2600_i3c_toggle_scl_in(struct dw_i3c_master *dw, int count)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);

	for (; count; count--) {
		regmap_write_bits(i3c->global_regs,
				  AST2600_I3CG_REG1(i3c->global_idx),
				  SCL_IN_SW_MODE_VAL, 0);
		regmap_write_bits(i3c->global_regs,
				  AST2600_I3CG_REG1(i3c->global_idx),
				  SCL_IN_SW_MODE_VAL, SCL_IN_SW_MODE_VAL);
	}
}

static void ast2600_i3c_gen_internal_stop(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);

	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_IN_SW_MODE_VAL, 0);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_IN_SW_MODE_VAL, 0);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_IN_SW_MODE_VAL, SCL_IN_SW_MODE_VAL);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_IN_SW_MODE_VAL, SDA_IN_SW_MODE_VAL);
}

static void ast2600_i3c_gen_target_reset_pattern(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	int i;

	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_OUT_SW_MODE_VAL | SCL_OUT_SW_MODE_VAL,
			  SDA_OUT_SW_MODE_VAL | SCL_OUT_SW_MODE_VAL);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_SW_MODE_OE | SCL_SW_MODE_OE,
			  SDA_SW_MODE_OE | SCL_SW_MODE_OE);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN,
			  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_IN_SW_MODE_VAL | SCL_IN_SW_MODE_VAL,
			  SDA_IN_SW_MODE_VAL | SCL_IN_SW_MODE_VAL);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN,
			  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_OUT_SW_MODE_VAL, 0);
	for (i = 0; i < 7; i++) {
		regmap_write_bits(i3c->global_regs,
				  AST2600_I3CG_REG1(i3c->global_idx),
				  SDA_OUT_SW_MODE_VAL, 0);
		regmap_write_bits(i3c->global_regs,
				  AST2600_I3CG_REG1(i3c->global_idx),
				  SDA_OUT_SW_MODE_VAL, SDA_OUT_SW_MODE_VAL);
	}
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SCL_OUT_SW_MODE_VAL, SCL_OUT_SW_MODE_VAL);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_OUT_SW_MODE_VAL, 0);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_OUT_SW_MODE_VAL, SDA_OUT_SW_MODE_VAL);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN, 0);
	regmap_write_bits(i3c->global_regs, AST2600_I3CG_REG1(i3c->global_idx),
			  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN, 0);
}

static void ast2600_i3c_set_ibi_mdb(struct dw_i3c_master *dw, u8 mdb)
{
	u32 reg;

	reg = readl(dw->regs + DEVICE_CTRL);
	reg &= ~DEV_CTRL_SLAVE_MDB;
	reg |= FIELD_PREP(DEV_CTRL_SLAVE_MDB, mdb);
	writel(reg, dw->regs + DEVICE_CTRL);
}

static int ast2600_i3c_get_free_hw_pos(struct dw_i3c_master *dw)
{
	if (!(dw->free_pos & GENMASK(dw->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(dw->free_pos) - 1;
}

static void ast2600_i3c_init_swdat(struct dw_i3c_master *dw)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp;
	int i, j;
	u32 def_set, def_clr;

	def_clr = DEV_ADDR_TABLE_IBI_ADDR_MASK;
	def_set = DEV_ADDR_TABLE_MR_REJECT | DEV_ADDR_TABLE_SIR_REJECT;

	for (i = 0; i < NUM_OF_SWDAT_GROUP; i++) {
		gp = &i3c->dat_group[i];
		gp->hw_index = -1;
		gp->free_pos = ALL_DATS_IN_GROUP_ARE_FREE;
		gp->mask.clr = def_clr;
		gp->mask.set = def_set;

		for (j = 0; j < NUM_OF_SWDATS_IN_GROUP; j++)
			gp->dat[j] = 0;
	}

	for (i = 0; i < dw->maxdevs; i++)
		writel(def_set,
		       dw->regs + DEV_ADDR_TABLE_LOC(dw->datstartaddr, i));
}

static int ast2600_i3c_set_swdat(struct dw_i3c_master *dw, u8 addr, u32 val)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp = &i3c->dat_group[ADDR_GRP(addr)];
	int pos = ADDR_HID(addr);

	if (!(val & DEV_ADDR_TABLE_LEGACY_I2C_DEV)) {
		/* Calculate DA parity for I3C devices */
		val &= ~DEV_ADDR_TABLE_DA_PARITY;
		val |= FIELD_PREP(DEV_ADDR_TABLE_DA_PARITY, even_parity(addr));
	}
	gp->dat[pos] = val;

	if (val) {
		gp->free_pos &= ~BIT(pos);

		/*
		 * reserve the hw dat resource for the first member of the
		 * group. all the members in the group share the same hw dat.
		 */
		if (gp->hw_index == -1) {
			gp->hw_index = ast2600_i3c_get_free_hw_pos(dw);
			if (gp->hw_index < 0)
				goto out;

			dw->free_pos &= ~BIT(gp->hw_index);
			val &= ~gp->mask.clr;
			val |= gp->mask.set;
			writel(val,
			       dw->regs + DEV_ADDR_TABLE_LOC(dw->datstartaddr,
							     gp->hw_index));
		}
	} else {
		gp->free_pos |= BIT(pos);

		/*
		 * release the hw dat resource if all the members in the group
		 * are free.
		 */
		if (gp->free_pos == ALL_DATS_IN_GROUP_ARE_FREE) {
			writel(gp->mask.set,
			       dw->regs + DEV_ADDR_TABLE_LOC(dw->datstartaddr,
							     gp->hw_index));
			dw->free_pos |= BIT(gp->hw_index);
			gp->hw_index = -1;
		}
	}
out:
	return gp->hw_index;
}

static u32 ast2600_i3c_get_swdat(struct dw_i3c_master *dw, u8 addr)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp = &i3c->dat_group[ADDR_GRP(addr)];

	return gp->dat[ADDR_HID(addr)];
}

static int ast2600_i3c_flush_swdat(struct dw_i3c_master *dw, u8 addr)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp = &i3c->dat_group[ADDR_GRP(addr)];
	u32 dat = gp->dat[ADDR_HID(addr)];
	int hw_index = gp->hw_index;

	if (!dat || hw_index < 0)
		return -1;

	dat &= ~gp->mask.clr;
	dat |= gp->mask.set;
	writel(dat, dw->regs + DEV_ADDR_TABLE_LOC(dw->datstartaddr, hw_index));

	return 0;
}

static int ast2600_i3c_get_swdat_hw_pos(struct dw_i3c_master *dw, u8 addr)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp = &i3c->dat_group[ADDR_GRP(addr)];

	return gp->hw_index;
}

static int ast2600_i3c_reattach_i3c_dev(struct i3c_dev_desc *dev,
					u8 old_dyn_addr)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	u32 dat = FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, dev->info.dyn_addr);

	if (old_dyn_addr != dev->info.dyn_addr)
		ast2600_i3c_set_swdat(master, old_dyn_addr, 0);

	ast2600_i3c_set_swdat(master, dev->info.dyn_addr, dat);
	data->index = ast2600_i3c_get_swdat_hw_pos(master, dev->info.dyn_addr);
	master->devs[dev->info.dyn_addr].addr = dev->info.dyn_addr;

	return 0;
}

static int ast2600_i3c_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int pos;
	u8 addr = dev->info.dyn_addr ?: dev->info.static_addr;

	pos = ast2600_i3c_set_swdat(master, addr,
				    FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, addr));
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = ast2600_i3c_get_swdat_hw_pos(master, addr);
	master->devs[addr].addr = addr;
	i3c_dev_set_master_data(dev, data);

	if (master->base.bus.context == I3C_BUS_CONTEXT_JESD403) {
		dev->info.max_write_ds = 0;
		dev->info.max_read_ds = 0;
	}

	return 0;
}

static void ast2600_i3c_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	u8 addr = dev->info.dyn_addr ?: dev->info.static_addr;

	ast2600_i3c_set_swdat(master, addr, 0);

	i3c_dev_set_master_data(dev, NULL);
	master->devs[addr].addr = 0;
	kfree(data);
}

static int ast2600_i3c_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int pos;

	pos = ast2600_i3c_set_swdat(master, dev->addr,
				    DEV_ADDR_TABLE_LEGACY_I2C_DEV |
				    FIELD_PREP(DEV_ADDR_TABLE_STATIC_ADDR, dev->addr));
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = ast2600_i3c_get_swdat_hw_pos(master, dev->addr);
	master->devs[dev->addr].addr = dev->addr;
	i2c_dev_set_master_data(dev, data);

	return 0;
}

static void ast2600_i3c_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	ast2600_i3c_set_swdat(master, dev->addr, 0);

	i2c_dev_set_master_data(dev, NULL);
	master->devs[dev->addr].addr = 0;
	kfree(data);
}

static void ast2600_i3c_set_sir_enabled(struct dw_i3c_master *dw,
					struct i3c_dev_desc *dev, u8 idx,
					bool enable)
{
	struct ast2600_i3c *i3c = to_ast2600_i3c(dw);
	struct ast2600_i3c_swdat_group *gp =
		&i3c->dat_group[ADDR_GRP(dev->info.dyn_addr)];
	unsigned long flags;
	u32 reg;
	bool global;

	spin_lock_irqsave(&dw->devs_lock, flags);
	if (enable) {
		gp->mask.clr |= DEV_ADDR_TABLE_SIR_REJECT |
				DEV_ADDR_TABLE_IBI_ADDR_MASK;

		gp->mask.set &= ~DEV_ADDR_TABLE_SIR_REJECT;
		gp->mask.set |= FIELD_PREP(DEV_ADDR_TABLE_IBI_ADDR_MASK,
					   IBI_ADDR_MASK_LAST_3BITS);
		/*
		 * The ast2600 i3c controller will lock up on receiving 4n+1-byte IBIs
		 * if the PEC is disabled. We have no way to restrict the length of
		 * IBIs sent to the controller, so we need to unconditionally enable
		 * PEC checking, which means we drop a byte of payload data
		 */
		gp->mask.set |= DEV_ADDR_TABLE_IBI_PEC;
		if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD)
			gp->mask.set |= DEV_ADDR_TABLE_IBI_MDB;
	} else {
		reg = ast2600_i3c_get_swdat(dw, dev->info.dyn_addr);
		reg |= DEV_ADDR_TABLE_SIR_REJECT;
		ast2600_i3c_set_swdat(dw, dev->info.dyn_addr, reg);
	}

	reg = readl(dw->regs + IBI_SIR_REQ_REJECT);
	if (enable) {
		global = reg == 0xffffffff;
		reg &= ~BIT(gp->hw_index);
	} else {
		int i;
		bool hj_rejected = !!(readl(dw->regs + DEVICE_CTRL) &
				      DEV_CTRL_HOT_JOIN_NACK);
		bool ibi_enable = false;

		for (i = 0; i < NUM_OF_SWDATS_IN_GROUP; i++) {
			if (!(gp->dat[i] & DEV_ADDR_TABLE_SIR_REJECT)) {
				ibi_enable = true;
				break;
			}
		}

		if (!ibi_enable) {
			reg |= BIT(gp->hw_index);
			global = (reg == 0xffffffff) && hj_rejected;

			gp->mask.set = DEV_ADDR_TABLE_SIR_REJECT;
		}
	}
	writel(reg, dw->regs + IBI_SIR_REQ_REJECT);

	if (global) {
		reg = readl(dw->regs + INTR_STATUS_EN);
		reg &= ~INTR_IBI_THLD_STAT;
		if (enable)
			reg |= INTR_IBI_THLD_STAT;
		writel(reg, dw->regs + INTR_STATUS_EN);

		reg = readl(dw->regs + INTR_SIGNAL_EN);
		reg &= ~INTR_IBI_THLD_STAT;
		if (enable)
			reg |= INTR_IBI_THLD_STAT;
		writel(reg, dw->regs + INTR_SIGNAL_EN);
	}

	ast2600_i3c_flush_swdat(dw, dev->info.dyn_addr);

	spin_unlock_irqrestore(&dw->devs_lock, flags);
}

static void ast2600_i3c_set_ibi_dev(struct dw_i3c_master *dw,
				    struct i3c_dev_desc *dev)
{
	dw->devs[dev->info.dyn_addr].ibi_dev = dev;
}

static struct i3c_dev_desc *ast2600_i3c_get_ibi_dev(struct dw_i3c_master *dw,
						    u8 addr)
{
	return dw->devs[addr].ibi_dev;
}

static const struct dw_i3c_platform_ops ast2600_i3c_ops = {
	.init = ast2600_i3c_init,
	.set_dat_ibi = ast2600_i3c_set_dat_ibi,
	.enter_sw_mode = ast2600_i3c_enter_sw_mode,
	.exit_sw_mode = ast2600_i3c_exit_sw_mode,
	.toggle_scl_in = ast2600_i3c_toggle_scl_in,
	.gen_internal_stop = ast2600_i3c_gen_internal_stop,
	.gen_target_reset_pattern = ast2600_i3c_gen_target_reset_pattern,
	.set_ibi_mdb = ast2600_i3c_set_ibi_mdb,
	.reattach_i3c_dev = ast2600_i3c_reattach_i3c_dev,
	.attach_i3c_dev = ast2600_i3c_attach_i3c_dev,
	.detach_i3c_dev = ast2600_i3c_detach_i3c_dev,
	.attach_i2c_dev = ast2600_i3c_attach_i2c_dev,
	.detach_i2c_dev = ast2600_i3c_detach_i2c_dev,
	.get_addr_pos = ast2600_i3c_get_swdat_hw_pos,
	.flush_dat = ast2600_i3c_flush_swdat,
	.set_sir_enabled = ast2600_i3c_set_sir_enabled,
	.set_ibi_dev = ast2600_i3c_set_ibi_dev,
	.get_ibi_dev = ast2600_i3c_get_ibi_dev,
};

static int ast2600_i3c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args gspec;
	struct ast2600_i3c *i3c;
	int rc;

	i3c = devm_kzalloc(&pdev->dev, sizeof(*i3c), GFP_KERNEL);
	if (!i3c)
		return -ENOMEM;

	rc = of_parse_phandle_with_fixed_args(np, "aspeed,global-regs", 1, 0,
					      &gspec);
	if (rc)
		return -ENODEV;

	i3c->global_regs = syscon_node_to_regmap(gspec.np);
	of_node_put(gspec.np);

	if (IS_ERR(i3c->global_regs))
		return PTR_ERR(i3c->global_regs);

	i3c->global_idx = gspec.args[0];

	rc = of_property_read_u32(np, "sda-pullup-ohms", &i3c->sda_pullup);
	if (rc)
		i3c->sda_pullup = AST2600_DEFAULT_SDA_PULLUP_OHMS;

	rc = ast2600_i3c_pullup_to_reg(i3c->sda_pullup, NULL);
	if (rc)
		dev_err(&pdev->dev, "invalid sda-pullup value %d\n",
			i3c->sda_pullup);

	i3c->dw.platform_ops = &ast2600_i3c_ops;
	i3c->dw.ibi_capable = true;
	i3c->dw.base.pec_supported = true;

	ast2600_i3c_init_swdat(&i3c->dw);

	return dw_i3c_common_probe(&i3c->dw, pdev);
}

static void ast2600_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *dw_i3c = platform_get_drvdata(pdev);

	dw_i3c_common_remove(dw_i3c);
}

static const struct of_device_id ast2600_i3c_master_of_match[] = {
	{ .compatible = "aspeed,ast2600-i3c", },
	{},
};
MODULE_DEVICE_TABLE(of, ast2600_i3c_master_of_match);

static struct platform_driver ast2600_i3c_driver = {
	.probe = ast2600_i3c_probe,
	.remove_new = ast2600_i3c_remove,
	.driver = {
		.name = "ast2600-i3c-master",
		.of_match_table = ast2600_i3c_master_of_match,
	},
};
module_platform_driver(ast2600_i3c_driver);

MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");
MODULE_DESCRIPTION("ASPEED AST2600 I3C driver");
MODULE_LICENSE("GPL");

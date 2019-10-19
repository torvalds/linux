// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Device State Control Registers driver
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 */

/*
 * The Device State Control Registers (DSCR) provide SoC level control over
 * a number of peripherals. Details vary considerably among the various SoC
 * parts. In general, the DSCR block will provide one or more configuration
 * registers often protected by a lock register. One or more key values must
 * be written to a lock register in order to unlock the configuration register.
 * The configuration register may be used to enable (and disable in some
 * cases) SoC pin drivers, peripheral clock sources (internal or pin), etc.
 * In some cases, a configuration register is write once or the individual
 * bits are write once. That is, you may be able to enable a device, but
 * will not be able to disable it.
 *
 * In addition to device configuration, the DSCR block may provide registers
 * which are used to reset SoC peripherals, provide device ID information,
 * provide MAC addresses, and other miscellaneous functions.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/soc.h>
#include <asm/dscr.h>

#define MAX_DEVSTATE_IDS   32
#define MAX_DEVCTL_REGS     8
#define MAX_DEVSTAT_REGS    8
#define MAX_LOCKED_REGS     4
#define MAX_SOC_EMACS       2

struct rmii_reset_reg {
	u32 reg;
	u32 mask;
};

/*
 * Some registerd may be locked. In order to write to these
 * registers, the key value must first be written to the lockreg.
 */
struct locked_reg {
	u32 reg;	/* offset from base */
	u32 lockreg;	/* offset from base */
	u32 key;	/* unlock key */
};

/*
 * This describes a contiguous area of like control bits used to enable/disable
 * SoC devices. Each controllable device is given an ID which is used by the
 * individual device drivers to control the device state. These IDs start at
 * zero and are assigned sequentially to the control bitfield ranges described
 * by this structure.
 */
struct devstate_ctl_reg {
	u32 reg;		/* register holding the control bits */
	u8  start_id;		/* start id of this range */
	u8  num_ids;		/* number of devices in this range */
	u8  enable_only;	/* bits are write-once to enable only */
	u8  enable;		/* value used to enable device */
	u8  disable;		/* value used to disable device */
	u8  shift;		/* starting (rightmost) bit in range */
	u8  nbits;		/* number of bits per device */
};


/*
 * This describes a region of status bits indicating the state of
 * various devices. This is used internally to wait for status
 * change completion when enabling/disabling a device. Status is
 * optional and not all device controls will have a corresponding
 * status.
 */
struct devstate_stat_reg {
	u32 reg;		/* register holding the status bits */
	u8  start_id;		/* start id of this range */
	u8  num_ids;		/* number of devices in this range */
	u8  enable;		/* value indicating enabled state */
	u8  disable;		/* value indicating disabled state */
	u8  shift;		/* starting (rightmost) bit in range */
	u8  nbits;		/* number of bits per device */
};

struct devstate_info {
	struct devstate_ctl_reg *ctl;
	struct devstate_stat_reg *stat;
};

/* These are callbacks to SOC-specific code. */
struct dscr_ops {
	void (*init)(struct device_node *node);
};

struct dscr_regs {
	spinlock_t		lock;
	void __iomem		*base;
	u32			kick_reg[2];
	u32			kick_key[2];
	struct locked_reg	locked[MAX_LOCKED_REGS];
	struct devstate_info	devstate_info[MAX_DEVSTATE_IDS];
	struct rmii_reset_reg   rmii_resets[MAX_SOC_EMACS];
	struct devstate_ctl_reg devctl[MAX_DEVCTL_REGS];
	struct devstate_stat_reg devstat[MAX_DEVSTAT_REGS];
};

static struct dscr_regs	dscr;

static struct locked_reg *find_locked_reg(u32 reg)
{
	int i;

	for (i = 0; i < MAX_LOCKED_REGS; i++)
		if (dscr.locked[i].key && reg == dscr.locked[i].reg)
			return &dscr.locked[i];
	return NULL;
}

/*
 * Write to a register with one lock
 */
static void dscr_write_locked1(u32 reg, u32 val,
			       u32 lock, u32 key)
{
	void __iomem *reg_addr = dscr.base + reg;
	void __iomem *lock_addr = dscr.base + lock;

	/*
	 * For some registers, the lock is relocked after a short number
	 * of cycles. We have to put the lock write and register write in
	 * the same fetch packet to meet this timing. The .align ensures
	 * the two stw instructions are in the same fetch packet.
	 */
	asm volatile ("b	.s2	0f\n"
		      "nop	5\n"
		      "    .align 5\n"
		      "0:\n"
		      "stw	.D1T2	%3,*%2\n"
		      "stw	.D1T2	%1,*%0\n"
		      :
		      : "a"(reg_addr), "b"(val), "a"(lock_addr), "b"(key)
		);

	/* in case the hw doesn't reset the lock */
	soc_writel(0, lock_addr);
}

/*
 * Write to a register protected by two lock registers
 */
static void dscr_write_locked2(u32 reg, u32 val,
			       u32 lock0, u32 key0,
			       u32 lock1, u32 key1)
{
	soc_writel(key0, dscr.base + lock0);
	soc_writel(key1, dscr.base + lock1);
	soc_writel(val, dscr.base + reg);
	soc_writel(0, dscr.base + lock0);
	soc_writel(0, dscr.base + lock1);
}

static void dscr_write(u32 reg, u32 val)
{
	struct locked_reg *lock;

	lock = find_locked_reg(reg);
	if (lock)
		dscr_write_locked1(reg, val, lock->lockreg, lock->key);
	else if (dscr.kick_key[0])
		dscr_write_locked2(reg, val, dscr.kick_reg[0], dscr.kick_key[0],
				   dscr.kick_reg[1], dscr.kick_key[1]);
	else
		soc_writel(val, dscr.base + reg);
}


/*
 * Drivers can use this interface to enable/disable SoC IP blocks.
 */
void dscr_set_devstate(int id, enum dscr_devstate_t state)
{
	struct devstate_ctl_reg *ctl;
	struct devstate_stat_reg *stat;
	struct devstate_info *info;
	u32 ctl_val, val;
	int ctl_shift, ctl_mask;
	unsigned long flags;

	if (!dscr.base)
		return;

	if (id < 0 || id >= MAX_DEVSTATE_IDS)
		return;

	info = &dscr.devstate_info[id];
	ctl = info->ctl;
	stat = info->stat;

	if (ctl == NULL)
		return;

	ctl_shift = ctl->shift + ctl->nbits * (id - ctl->start_id);
	ctl_mask = ((1 << ctl->nbits) - 1) << ctl_shift;

	switch (state) {
	case DSCR_DEVSTATE_ENABLED:
		ctl_val = ctl->enable << ctl_shift;
		break;
	case DSCR_DEVSTATE_DISABLED:
		if (ctl->enable_only)
			return;
		ctl_val = ctl->disable << ctl_shift;
		break;
	default:
		return;
	}

	spin_lock_irqsave(&dscr.lock, flags);

	val = soc_readl(dscr.base + ctl->reg);
	val &= ~ctl_mask;
	val |= ctl_val;

	dscr_write(ctl->reg, val);

	spin_unlock_irqrestore(&dscr.lock, flags);

	if (!stat)
		return;

	ctl_shift = stat->shift + stat->nbits * (id - stat->start_id);

	if (state == DSCR_DEVSTATE_ENABLED)
		ctl_val = stat->enable;
	else
		ctl_val = stat->disable;

	do {
		val = soc_readl(dscr.base + stat->reg);
		val >>= ctl_shift;
		val &= ((1 << stat->nbits) - 1);
	} while (val != ctl_val);
}
EXPORT_SYMBOL(dscr_set_devstate);

/*
 * Drivers can use this to reset RMII module.
 */
void dscr_rmii_reset(int id, int assert)
{
	struct rmii_reset_reg *r;
	unsigned long flags;
	u32 val;

	if (id < 0 || id >= MAX_SOC_EMACS)
		return;

	r = &dscr.rmii_resets[id];
	if (r->mask == 0)
		return;

	spin_lock_irqsave(&dscr.lock, flags);

	val = soc_readl(dscr.base + r->reg);
	if (assert)
		dscr_write(r->reg, val | r->mask);
	else
		dscr_write(r->reg, val & ~(r->mask));

	spin_unlock_irqrestore(&dscr.lock, flags);
}
EXPORT_SYMBOL(dscr_rmii_reset);

static void __init dscr_parse_devstat(struct device_node *node,
				      void __iomem *base)
{
	u32 val;
	int err;

	err = of_property_read_u32_array(node, "ti,dscr-devstat", &val, 1);
	if (!err)
		c6x_devstat = soc_readl(base + val);
	printk(KERN_INFO "DEVSTAT: %08x\n", c6x_devstat);
}

static void __init dscr_parse_silicon_rev(struct device_node *node,
					 void __iomem *base)
{
	u32 vals[3];
	int err;

	err = of_property_read_u32_array(node, "ti,dscr-silicon-rev", vals, 3);
	if (!err) {
		c6x_silicon_rev = soc_readl(base + vals[0]);
		c6x_silicon_rev >>= vals[1];
		c6x_silicon_rev &= vals[2];
	}
}

/*
 * Some SoCs will have a pair of fuse registers which hold
 * an ethernet MAC address. The "ti,dscr-mac-fuse-regs"
 * property is a mapping from fuse register bytes to MAC
 * address bytes. The expected format is:
 *
 *	ti,dscr-mac-fuse-regs = <reg0 b3 b2 b1 b0
 *				 reg1 b3 b2 b1 b0>
 *
 * reg0 and reg1 are the offsets of the two fuse registers.
 * b3-b0 positionally represent bytes within the fuse register.
 * b3 is the most significant byte and b0 is the least.
 * Allowable values for b3-b0 are:
 *
 *	  0 = fuse register byte not used in MAC address
 *      1-6 = index+1 into c6x_fuse_mac[]
 */
static void __init dscr_parse_mac_fuse(struct device_node *node,
				       void __iomem *base)
{
	u32 vals[10], fuse;
	int f, i, j, err;

	err = of_property_read_u32_array(node, "ti,dscr-mac-fuse-regs",
					 vals, 10);
	if (err)
		return;

	for (f = 0; f < 2; f++) {
		fuse = soc_readl(base + vals[f * 5]);
		for (j = (f * 5) + 1, i = 24; i >= 0; i -= 8, j++)
			if (vals[j] && vals[j] <= 6)
				c6x_fuse_mac[vals[j] - 1] = fuse >> i;
	}
}

static void __init dscr_parse_rmii_resets(struct device_node *node,
					  void __iomem *base)
{
	const __be32 *p;
	int i, size;

	/* look for RMII reset registers */
	p = of_get_property(node, "ti,dscr-rmii-resets", &size);
	if (p) {
		/* parse all the reg/mask pairs we can handle */
		size /= (sizeof(*p) * 2);
		if (size > MAX_SOC_EMACS)
			size = MAX_SOC_EMACS;

		for (i = 0; i < size; i++) {
			dscr.rmii_resets[i].reg = be32_to_cpup(p++);
			dscr.rmii_resets[i].mask = be32_to_cpup(p++);
		}
	}
}


static void __init dscr_parse_privperm(struct device_node *node,
				       void __iomem *base)
{
	u32 vals[2];
	int err;

	err = of_property_read_u32_array(node, "ti,dscr-privperm", vals, 2);
	if (err)
		return;
	dscr_write(vals[0], vals[1]);
}

/*
 * SoCs may have "locked" DSCR registers which can only be written
 * to only after writing a key value to a lock registers. These
 * regisers can be described with the "ti,dscr-locked-regs" property.
 * This property provides a list of register descriptions with each
 * description consisting of three values.
 *
 *	ti,dscr-locked-regs = <reg0 lockreg0 key0
 *                               ...
 *                             regN lockregN keyN>;
 *
 * reg is the offset of the locked register
 * lockreg is the offset of the lock register
 * key is the unlock key written to lockreg
 *
 */
static void __init dscr_parse_locked_regs(struct device_node *node,
					  void __iomem *base)
{
	struct locked_reg *r;
	const __be32 *p;
	int i, size;

	p = of_get_property(node, "ti,dscr-locked-regs", &size);
	if (p) {
		/* parse all the register descriptions we can handle */
		size /= (sizeof(*p) * 3);
		if (size > MAX_LOCKED_REGS)
			size = MAX_LOCKED_REGS;

		for (i = 0; i < size; i++) {
			r = &dscr.locked[i];

			r->reg = be32_to_cpup(p++);
			r->lockreg = be32_to_cpup(p++);
			r->key = be32_to_cpup(p++);
		}
	}
}

/*
 * SoCs may have DSCR registers which are only write enabled after
 * writing specific key values to two registers. The two key registers
 * and the key values can be parsed from a "ti,dscr-kick-regs"
 * propety with the following layout:
 *
 *	ti,dscr-kick-regs = <kickreg0 key0 kickreg1 key1>
 *
 * kickreg is the offset of the "kick" register
 * key is the value which unlocks writing for protected regs
 */
static void __init dscr_parse_kick_regs(struct device_node *node,
					void __iomem *base)
{
	u32 vals[4];
	int err;

	err = of_property_read_u32_array(node, "ti,dscr-kick-regs", vals, 4);
	if (!err) {
		dscr.kick_reg[0] = vals[0];
		dscr.kick_key[0] = vals[1];
		dscr.kick_reg[1] = vals[2];
		dscr.kick_key[1] = vals[3];
	}
}


/*
 * SoCs may provide controls to enable/disable individual IP blocks. These
 * controls in the DSCR usually control pin drivers but also may control
 * clocking and or resets. The device tree is used to describe the bitfields
 * in registers used to control device state. The number of bits and their
 * values may vary even within the same register.
 *
 * The layout of these bitfields is described by the ti,dscr-devstate-ctl-regs
 * property. This property is a list where each element describes a contiguous
 * range of control fields with like properties. Each element of the list
 * consists of 7 cells with the following values:
 *
 *   start_id num_ids reg enable disable start_bit nbits
 *
 * start_id is device id for the first device control in the range
 * num_ids is the number of device controls in the range
 * reg is the offset of the register holding the control bits
 * enable is the value to enable a device
 * disable is the value to disable a device (0xffffffff if cannot disable)
 * start_bit is the bit number of the first bit in the range
 * nbits is the number of bits per device control
 */
static void __init dscr_parse_devstate_ctl_regs(struct device_node *node,
						void __iomem *base)
{
	struct devstate_ctl_reg *r;
	const __be32 *p;
	int i, j, size;

	p = of_get_property(node, "ti,dscr-devstate-ctl-regs", &size);
	if (p) {
		/* parse all the ranges we can handle */
		size /= (sizeof(*p) * 7);
		if (size > MAX_DEVCTL_REGS)
			size = MAX_DEVCTL_REGS;

		for (i = 0; i < size; i++) {
			r = &dscr.devctl[i];

			r->start_id = be32_to_cpup(p++);
			r->num_ids = be32_to_cpup(p++);
			r->reg = be32_to_cpup(p++);
			r->enable = be32_to_cpup(p++);
			r->disable = be32_to_cpup(p++);
			if (r->disable == 0xffffffff)
				r->enable_only = 1;
			r->shift = be32_to_cpup(p++);
			r->nbits = be32_to_cpup(p++);

			for (j = r->start_id;
			     j < (r->start_id + r->num_ids);
			     j++)
				dscr.devstate_info[j].ctl = r;
		}
	}
}

/*
 * SoCs may provide status registers indicating the state (enabled/disabled) of
 * devices on the SoC. The device tree is used to describe the bitfields in
 * registers used to provide device status. The number of bits and their
 * values used to provide status may vary even within the same register.
 *
 * The layout of these bitfields is described by the ti,dscr-devstate-stat-regs
 * property. This property is a list where each element describes a contiguous
 * range of status fields with like properties. Each element of the list
 * consists of 7 cells with the following values:
 *
 *   start_id num_ids reg enable disable start_bit nbits
 *
 * start_id is device id for the first device status in the range
 * num_ids is the number of devices covered by the range
 * reg is the offset of the register holding the status bits
 * enable is the value indicating device is enabled
 * disable is the value indicating device is disabled
 * start_bit is the bit number of the first bit in the range
 * nbits is the number of bits per device status
 */
static void __init dscr_parse_devstate_stat_regs(struct device_node *node,
						 void __iomem *base)
{
	struct devstate_stat_reg *r;
	const __be32 *p;
	int i, j, size;

	p = of_get_property(node, "ti,dscr-devstate-stat-regs", &size);
	if (p) {
		/* parse all the ranges we can handle */
		size /= (sizeof(*p) * 7);
		if (size > MAX_DEVSTAT_REGS)
			size = MAX_DEVSTAT_REGS;

		for (i = 0; i < size; i++) {
			r = &dscr.devstat[i];

			r->start_id = be32_to_cpup(p++);
			r->num_ids = be32_to_cpup(p++);
			r->reg = be32_to_cpup(p++);
			r->enable = be32_to_cpup(p++);
			r->disable = be32_to_cpup(p++);
			r->shift = be32_to_cpup(p++);
			r->nbits = be32_to_cpup(p++);

			for (j = r->start_id;
			     j < (r->start_id + r->num_ids);
			     j++)
				dscr.devstate_info[j].stat = r;
		}
	}
}

static struct of_device_id dscr_ids[] __initdata = {
	{ .compatible = "ti,c64x+dscr" },
	{}
};

/*
 * Probe for DSCR area.
 *
 * This has to be done early on in case timer or interrupt controller
 * needs something. e.g. On C6455 SoC, timer must be enabled through
 * DSCR before it is functional.
 */
void __init dscr_probe(void)
{
	struct device_node *node;
	void __iomem *base;

	spin_lock_init(&dscr.lock);

	node = of_find_matching_node(NULL, dscr_ids);
	if (!node)
		return;

	base = of_iomap(node, 0);
	if (!base) {
		of_node_put(node);
		return;
	}

	dscr.base = base;

	dscr_parse_devstat(node, base);
	dscr_parse_silicon_rev(node, base);
	dscr_parse_mac_fuse(node, base);
	dscr_parse_rmii_resets(node, base);
	dscr_parse_locked_regs(node, base);
	dscr_parse_kick_regs(node, base);
	dscr_parse_devstate_ctl_regs(node, base);
	dscr_parse_devstate_stat_regs(node, base);
	dscr_parse_privperm(node, base);
}

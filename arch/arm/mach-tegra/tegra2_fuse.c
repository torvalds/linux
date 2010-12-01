/*
 * arch/arm/mach-tegra/tegra2_fuse.c
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * Fuses are one time programmable bits on the chip which are used by
 * the chip manufacturer and device manufacturers to store chip/device
 * configurations. The fuse bits are encapsulated in a 32 x 64 array.
 * If a fuse bit is programmed to 1, it cannot be reverted to 0. Either
 * another fuse bit has to be used for the same purpose or a new chip
 * needs to be used.
 *
 * Each and every fuse word has its own shadow word which resides adjacent to
 * a particular fuse word. e.g. Fuse words 0-1 form a fuse-shadow pair.
 * So in theory we have only 32 fuse words to work with.
 * The shadow fuse word is a mirror of the actual fuse word at all times
 * and this is maintained while programming a particular fuse.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <mach/tegra2_fuse.h>

#include "fuse.h"

#define NFUSES	64
#define STATE_IDLE	(0x4 << 16)

/* since fuse burning is irreversible, use this for testing */
#define ENABLE_FUSE_BURNING 1

/* fuse registers */
#define FUSE_CTRL		0x000
#define FUSE_REG_ADDR		0x004
#define FUSE_REG_READ		0x008
#define FUSE_REG_WRITE		0x00C
#define FUSE_TIME_PGM		0x01C
#define FUSE_PRIV2INTFC		0x020
#define FUSE_DIS_PGM		0x02C
#define FUSE_PWR_GOOD_SW	0x034

static u32 fuse_pgm_data[NFUSES / 2];
static u32 fuse_pgm_mask[NFUSES / 2];
static u32 tmp_fuse_pgm_data[NFUSES / 2];
static u32 master_enable;

DEFINE_MUTEX(fuse_lock);

static struct fuse_data fuse_info;

struct param_info {
	u32 *addr;
	int sz;
	u32 start_off;
	int start_bit;
	int nbits;
	int data_offset;
};

static struct param_info fuse_info_tbl[] = {
	[DEVKEY] = {
		.addr = &fuse_info.devkey,
		.sz = sizeof(fuse_info.devkey),
		.start_off = 0x12,
		.start_bit = 8,
		.nbits = 32,
		.data_offset = 0,
	},
	[JTAG_DIS] = {
		.addr = &fuse_info.jtag_dis,
		.sz = sizeof(fuse_info.jtag_dis),
		.start_off = 0x0,
		.start_bit = 24,
		.nbits = 1,
		.data_offset = 1,
	},
	[ODM_PROD_MODE] = {
		.addr = &fuse_info.odm_prod_mode,
		.sz = sizeof(fuse_info.odm_prod_mode),
		.start_off = 0x0,
		.start_bit = 23,
		.nbits = 1,
		.data_offset = 2,
	},
	[SEC_BOOT_DEV_CFG] = {
		.addr = &fuse_info.bootdev_cfg,
		.sz = sizeof(fuse_info.bootdev_cfg),
		.start_off = 0x14,
		.start_bit = 8,
		.nbits = 16,
		.data_offset = 3,
	},
	[SEC_BOOT_DEV_SEL] = {
		.addr = &fuse_info.bootdev_sel,
		.sz = sizeof(fuse_info.bootdev_sel),
		.start_off = 0x14,
		.start_bit = 24,
		.nbits = 3,
		.data_offset = 4,
	},
	[SBK] = {
		.addr = fuse_info.sbk,
		.sz = sizeof(fuse_info.sbk),
		.start_off = 0x0A,
		.start_bit = 8,
		.nbits = 128,
		.data_offset = 5,
	},
	[SW_RSVD] = {
		.addr = &fuse_info.sw_rsvd,
		.sz = sizeof(fuse_info.sw_rsvd),
		.start_off = 0x14,
		.start_bit = 28,
		.nbits = 4,
		.data_offset = 9,
	},
	[IGNORE_DEV_SEL_STRAPS] = {
		.addr = &fuse_info.ignore_devsel_straps,
		.sz = sizeof(fuse_info.ignore_devsel_straps),
		.start_off = 0x14,
		.start_bit = 27,
		.nbits = 1,
		.data_offset = 10,
	},
	[ODM_RSVD] = {
		.addr = fuse_info.odm_rsvd,
		.sz = sizeof(fuse_info.odm_rsvd),
		.start_off = 0x16,
		.start_bit = 4,
		.nbits = 256,
		.data_offset = 11,
	},
	[SBK_DEVKEY_STATUS] = {
		.sz = SBK_DEVKEY_STATUS_SZ,
	},
	[MASTER_ENB] = {
		.addr = &master_enable,
		.sz = sizeof(u8),
		.start_off = 0x0,
		.start_bit = 0,
		.nbits = 1,
	},
};

static void wait_for_idle(void)
{
	u32 reg;

	do {
		reg = tegra_fuse_readl(FUSE_CTRL);
	} while ((reg & (0xF << 16)) != STATE_IDLE);
}

#define FUSE_READ	0x1
#define FUSE_WRITE	0x2
#define FUSE_SENSE	0x3
#define FUSE_CMD_MASK	0x3

static u32 fuse_cmd_read(u32 addr)
{
	u32 reg;

	tegra_fuse_writel(addr, FUSE_REG_ADDR);
	reg = tegra_fuse_readl(FUSE_CTRL);
	reg &= ~FUSE_CMD_MASK;
	reg |= FUSE_READ;
	tegra_fuse_writel(reg, FUSE_CTRL);
	wait_for_idle();

	reg = tegra_fuse_readl(FUSE_REG_READ);
	return reg;
}

static void fuse_cmd_write(u32 value, u32 addr)
{
	u32 reg;

	tegra_fuse_writel(addr, FUSE_REG_ADDR);
	tegra_fuse_writel(value, FUSE_REG_WRITE);

	reg = tegra_fuse_readl(FUSE_CTRL);
	reg &= ~FUSE_CMD_MASK;
	reg |= FUSE_WRITE;
	tegra_fuse_writel(reg, FUSE_CTRL);
	wait_for_idle();
}

static void fuse_cmd_sense(void)
{
	u32 reg;

	reg = tegra_fuse_readl(FUSE_CTRL);
	reg &= ~FUSE_CMD_MASK;
	reg |= FUSE_SENSE;
	tegra_fuse_writel(reg, FUSE_CTRL);
	wait_for_idle();
}

static void fuse_reg_hide(void)
{
	u32 reg = tegra_fuse_readl(0x48);
	reg &= ~(1 << 28);
	tegra_fuse_writel(reg, 0x48);
}

static void fuse_reg_unhide(void)
{
	u32 reg = tegra_fuse_readl(0x48);
	reg |= (1 << 28);
	tegra_fuse_writel(reg, 0x48);
}

static void get_fuse(enum fuse_io_param io_param, u32 *out)
{
	int start_bit = fuse_info_tbl[io_param].start_bit;
	int nbits = fuse_info_tbl[io_param].nbits;
	int offset = fuse_info_tbl[io_param].start_off;
	u32 *dst = fuse_info_tbl[io_param].addr;
	int dst_bit = 0;
	int i;
	u32 val;
	int loops;

	if (out)
		dst = out;

	do {
		val = fuse_cmd_read(offset);
		loops = min(nbits, 32 - start_bit);
		for (i = 0; i < loops; i++) {
			if (val & (BIT(start_bit + i)))
				*dst |= BIT(dst_bit);
			else
				*dst &= ~BIT(dst_bit);
			dst_bit++;
			if (dst_bit == 32) {
				dst++;
				dst_bit = 0;
			}
		}
		nbits -= loops;
		offset += 2;
		start_bit = 0;
	} while (nbits > 0);
}

int tegra_fuse_read(enum fuse_io_param io_param, u32 *data, int size)
{
	int ret = 0, nbits;
	u32 sbk[4], devkey = 0;

	if (!data)
		return -EINVAL;

	if (size != fuse_info_tbl[io_param].sz) {
		pr_err("%s: size mismatch(%d), %d vs %d\n", __func__,
			(int)io_param, size, fuse_info_tbl[io_param].sz);
		return -EINVAL;
	}

	mutex_lock(&fuse_lock);
	fuse_reg_unhide();
	fuse_cmd_sense();

	if (io_param == SBK_DEVKEY_STATUS) {
		*data = 0;

		get_fuse(SBK, sbk);
		get_fuse(DEVKEY, &devkey);
		nbits = sizeof(sbk) * BITS_PER_BYTE;
		if (find_first_bit((unsigned long *)sbk, nbits) != nbits)
			*data = 1;
		else if (devkey)
			*data = 1;
	} else {
		get_fuse(io_param, data);
	}

	fuse_reg_hide();
	mutex_unlock(&fuse_lock);
	return ret;
}

static bool fuse_odm_prod_mode(void)
{
	u32 odm_prod_mode = 0;

	get_fuse(ODM_PROD_MODE, &odm_prod_mode);
	return (odm_prod_mode ? true : false);
}

static void set_fuse(enum fuse_io_param io_param, u32 *data)
{
	int i, start_bit = fuse_info_tbl[io_param].start_bit;
	int nbits = fuse_info_tbl[io_param].nbits, loops;
	int offset = fuse_info_tbl[io_param].start_off >> 1;
	int src_bit = 0;
	u32 val;

	do {
		val = *data;
		loops = min(nbits, 32 - start_bit);
		for (i = 0; i < loops; i++) {
			fuse_pgm_mask[offset] |= BIT(start_bit + i);
			if (val & BIT(src_bit))
				fuse_pgm_data[offset] |= BIT(start_bit + i);
			else
				fuse_pgm_data[offset] &= ~BIT(start_bit + i);
			src_bit++;
			if (src_bit == 32) {
				data++;
				val = *data;
				src_bit = 0;
			}
		}
		nbits -= loops;
		offset++;
		start_bit = 0;
	} while (nbits > 0);
}

static void populate_fuse_arrs(struct fuse_data *info, u32 flags)
{
	u32 data = 0;
	u32 *src = (u32 *)info;
	int i;

	memset(fuse_pgm_data, 0, sizeof(fuse_pgm_data));
	memset(fuse_pgm_mask, 0, sizeof(fuse_pgm_mask));

	/* enable program bit */
	data = 1;
	set_fuse(MASTER_ENB, &data);

	if ((flags & FLAGS_ODMRSVD)) {
		set_fuse(ODM_RSVD, info->odm_rsvd);
		flags &= ~FLAGS_ODMRSVD;
	}

	/* do not burn any more if secure mode is set */
	if (fuse_odm_prod_mode())
		goto out;

	for_each_set_bit(i, (unsigned long *)&flags, MAX_PARAMS)
		set_fuse(i, src + fuse_info_tbl[i].data_offset);

out:
	pr_debug("ready to program");
}

static void fuse_power_enable(void)
{
#if ENABLE_FUSE_BURNING
	tegra_fuse_writel(0x1, FUSE_PWR_GOOD_SW);
	udelay(1);
#endif
}

static void fuse_power_disable(void)
{
#if ENABLE_FUSE_BURNING
	tegra_fuse_writel(0, FUSE_PWR_GOOD_SW);
	udelay(1);
#endif
}

static void fuse_program_array(int pgm_cycles)
{
	u32 reg, fuse_val[2];
	u32 *data = tmp_fuse_pgm_data, addr = 0, *mask = fuse_pgm_mask;
	int i = 0;

	fuse_reg_unhide();
	fuse_cmd_sense();

	/* get the first 2 fuse bytes */
	fuse_val[0] = fuse_cmd_read(0);
	fuse_val[1] = fuse_cmd_read(1);

	fuse_power_enable();

	/*
	 * The fuse macro is a high density macro. Fuses are
	 * burned using an addressing mechanism, so no need to prepare
	 * the full list, but more write to control registers are needed.
	 * The only bit that can be written at first is bit 0, a special write
	 * protection bit by assumptions all other bits are at 0
	 *
	 * The programming pulse must have a precise width of
	 * [9000, 11000] ns.
	 */
	if (pgm_cycles > 0) {
		reg = pgm_cycles;
		tegra_fuse_writel(reg, FUSE_TIME_PGM);
	}
	fuse_val[0] = (0x1 & ~fuse_val[0]);
	fuse_val[1] = (0x1 & ~fuse_val[1]);
	fuse_cmd_write(fuse_val[0], 0);
	fuse_cmd_write(fuse_val[1], 1);

	fuse_power_disable();

	/*
	 * this will allow programming of other fuses
	 * and the reading of the existing fuse values
	 */
	fuse_cmd_sense();

	/* Clear out all bits that have already been burned or masked out */
	memcpy(data, fuse_pgm_data, sizeof(fuse_pgm_data));

	for (addr = 0; addr < NFUSES; addr += 2, data++, mask++) {
		reg = fuse_cmd_read(addr);
		pr_debug("%d: 0x%x 0x%x 0x%x\n", addr, (u32)(*data),
			~reg, (u32)(*mask));
		*data = (*data & ~reg) & *mask;
	}

	fuse_power_enable();

	/*
	 * Finally loop on all fuses, program the non zero ones.
	 * Words 0 and 1 are written last and they contain control fuses. We
	 * need to invalidate after writing to a control word (with the exception
	 * of the master enable). This is also the reason we write them last.
	 */
	for (i = ARRAY_SIZE(fuse_pgm_data) - 1; i >= 0; i--) {
		if (tmp_fuse_pgm_data[i]) {
			fuse_cmd_write(tmp_fuse_pgm_data[i], i * 2);
			fuse_cmd_write(tmp_fuse_pgm_data[i], (i * 2) + 1);
		}

		if (i < 2) {
			fuse_power_disable();
			fuse_cmd_sense();
			fuse_power_enable();
		}
	}

	/* Read all data into the chip options */
	tegra_fuse_writel(0x1, FUSE_PRIV2INTFC);
	udelay(1);
	tegra_fuse_writel(0, FUSE_PRIV2INTFC);

	while (!(tegra_fuse_readl(FUSE_CTRL) & (1 << 30)));

	fuse_reg_hide();
	fuse_power_disable();
}

static int fuse_set(enum fuse_io_param io_param, u32 *param, int size)
{
	int i, nwords = size / sizeof(u32);
	u32 *data;

	if (io_param > MAX_PARAMS)
		return -EINVAL;

	data = (u32*)kzalloc(size, GFP_KERNEL);
	if (!data) {
		pr_err("failed to alloc %d bytes\n", nwords);
		return -ENOMEM;
	}

	get_fuse(io_param, data);

	for (i = 0; i < nwords; i++) {
		if ((data[i] | param[i]) != param[i]) {
			pr_info("hw_val: 0x%x, sw_val: 0x%x, final: 0x%x\n",
				data[i], param[i], (data[i] | param[i]));
			param[i] = (data[i] | param[i]);
		}
	}
	kfree(data);
	return 0;
}

int tegra_fuse_program(struct fuse_data *pgm_data, u32 flags)
{
	u32 reg;
	int i = 0;

	mutex_lock(&fuse_lock);
	reg = tegra_fuse_readl(FUSE_DIS_PGM);
	mutex_unlock(&fuse_lock);
	if (reg) {
		pr_err("fuse programming disabled");
		return -EACCES;
	}

	if (fuse_odm_prod_mode() && (flags != FLAGS_ODMRSVD)) {
		pr_err("reserved odm fuses are allowed in secure mode");
		return -EPERM;
	}

	if ((flags & FLAGS_ODM_PROD_MODE) &&
		(flags & (FLAGS_SBK | FLAGS_DEVKEY))) {
		pr_err("odm production mode and sbk/devkey not allowed");
		return -EPERM;
	}

	mutex_lock(&fuse_lock);
	memcpy(&fuse_info, pgm_data, sizeof(fuse_info));
	for_each_set_bit(i, (unsigned long *)&flags, MAX_PARAMS) {
		fuse_set((u32)i, fuse_info_tbl[i].addr,
			fuse_info_tbl[i].sz);
	}

	populate_fuse_arrs(&fuse_info, flags);
	fuse_program_array(0);

	/* disable program bit */
	reg = 0;
	set_fuse(MASTER_ENB, &reg);

	memset(&fuse_info, 0, sizeof(fuse_info));
	mutex_unlock(&fuse_lock);

	return 0;
}

void tegra_fuse_program_disable(void)
{
	mutex_lock(&fuse_lock);
	tegra_fuse_writel(0x1, FUSE_DIS_PGM);
	mutex_unlock(&fuse_lock);
}

static int __init tegra_fuse_program_init(void)
{
	mutex_init(&fuse_lock);
	return 0;
}

module_init(tegra_fuse_program_init);

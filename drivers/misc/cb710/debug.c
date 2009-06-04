/*
 *  cb710/debug.c
 *
 *  Copyright by Michał Mirosław, 2008-2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/cb710.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define CB710_REG_COUNT		0x80

static const u16 allow[CB710_REG_COUNT/16] = {
	0xFFF0, 0xFFFF, 0xFFFF, 0xFFFF,
	0xFFF0, 0xFFFF, 0xFFFF, 0xFFFF,
};
static const char *const prefix[ARRAY_SIZE(allow)] = {
	"MMC", "MMC", "MMC", "MMC",
	"MS?", "MS?", "SM?", "SM?"
};

static inline int allow_reg_read(unsigned block, unsigned offset, unsigned bits)
{
	unsigned mask = (1 << bits/8) - 1;
	offset *= bits/8;
	return ((allow[block] >> offset) & mask) == mask;
}

#define CB710_READ_REGS_TEMPLATE(t)					\
static void cb710_read_regs_##t(void __iomem *iobase,			\
	u##t *reg, unsigned select)					\
{									\
	unsigned i, j;							\
									\
	for (i = 0; i < ARRAY_SIZE(allow); ++i, reg += 16/(t/8)) {	\
		if (!(select & (1 << i)))					\
			continue;					\
									\
		for (j = 0; j < 0x10/(t/8); ++j) {			\
			if (!allow_reg_read(i, j, t))			\
				continue;				\
			reg[j] = ioread##t(iobase			\
				+ (i << 4) + (j * (t/8)));		\
		}							\
	}								\
}

static const char cb710_regf_8[] = "%02X";
static const char cb710_regf_16[] = "%04X";
static const char cb710_regf_32[] = "%08X";
static const char cb710_xes[] = "xxxxxxxx";

#define CB710_DUMP_REGS_TEMPLATE(t)					\
static void cb710_dump_regs_##t(struct device *dev,			\
	const u##t *reg, unsigned select)				\
{									\
	const char *const xp = &cb710_xes[8 - t/4];			\
	const char *const format = cb710_regf_##t;			\
									\
	char msg[100], *p;						\
	unsigned i, j;							\
									\
	for (i = 0; i < ARRAY_SIZE(allow); ++i, reg += 16/(t/8)) {	\
		if (!(select & (1 << i)))				\
			continue;					\
		p = msg;						\
		for (j = 0; j < 0x10/(t/8); ++j) {			\
			*p++ = ' ';					\
			if (j == 8/(t/8))				\
				*p++ = ' ';				\
			if (allow_reg_read(i, j, t))			\
				p += sprintf(p, format, reg[j]);	\
			else						\
				p += sprintf(p, "%s", xp);		\
		}							\
		dev_dbg(dev, "%s 0x%02X %s\n", prefix[i], i << 4, msg);	\
	}								\
}

#define CB710_READ_AND_DUMP_REGS_TEMPLATE(t)				\
static void cb710_read_and_dump_regs_##t(struct cb710_chip *chip,	\
	unsigned select)						\
{									\
	u##t regs[CB710_REG_COUNT/sizeof(u##t)];			\
									\
	memset(&regs, 0, sizeof(regs));					\
	cb710_read_regs_##t(chip->iobase, regs, select);		\
	cb710_dump_regs_##t(cb710_chip_dev(chip), regs, select);	\
}

#define CB710_REG_ACCESS_TEMPLATES(t)		\
  CB710_READ_REGS_TEMPLATE(t)			\
  CB710_DUMP_REGS_TEMPLATE(t)			\
  CB710_READ_AND_DUMP_REGS_TEMPLATE(t)

CB710_REG_ACCESS_TEMPLATES(8)
CB710_REG_ACCESS_TEMPLATES(16)
CB710_REG_ACCESS_TEMPLATES(32)

void cb710_dump_regs(struct cb710_chip *chip, unsigned select)
{
	if (!(select & CB710_DUMP_REGS_MASK))
		select = CB710_DUMP_REGS_ALL;
	if (!(select & CB710_DUMP_ACCESS_MASK))
		select |= CB710_DUMP_ACCESS_8;

	if (select & CB710_DUMP_ACCESS_32)
		cb710_read_and_dump_regs_32(chip, select);
	if (select & CB710_DUMP_ACCESS_16)
		cb710_read_and_dump_regs_16(chip, select);
	if (select & CB710_DUMP_ACCESS_8)
		cb710_read_and_dump_regs_8(chip, select);
}
EXPORT_SYMBOL_GPL(cb710_dump_regs);


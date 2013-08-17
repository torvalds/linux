/*
 * ST Microelectronics MFD: stmpe's driver
 *
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#include <linux/gpio.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include "stmpe.h"

static int __stmpe_enable(struct stmpe *stmpe, unsigned int blocks)
{
	return stmpe->variant->enable(stmpe, blocks, true);
}

static int __stmpe_disable(struct stmpe *stmpe, unsigned int blocks)
{
	return stmpe->variant->enable(stmpe, blocks, false);
}

static int __stmpe_reg_read(struct stmpe *stmpe, u8 reg)
{
	int ret;

	ret = stmpe->ci->read_byte(stmpe, reg);
	if (ret < 0)
		dev_err(stmpe->dev, "failed to read reg %#x: %d\n", reg, ret);

	dev_vdbg(stmpe->dev, "rd: reg %#x => data %#x\n", reg, ret);

	return ret;
}

static int __stmpe_reg_write(struct stmpe *stmpe, u8 reg, u8 val)
{
	int ret;

	dev_vdbg(stmpe->dev, "wr: reg %#x <= %#x\n", reg, val);

	ret = stmpe->ci->write_byte(stmpe, reg, val);
	if (ret < 0)
		dev_err(stmpe->dev, "failed to write reg %#x: %d\n", reg, ret);

	return ret;
}

static int __stmpe_set_bits(struct stmpe *stmpe, u8 reg, u8 mask, u8 val)
{
	int ret;

	ret = __stmpe_reg_read(stmpe, reg);
	if (ret < 0)
		return ret;

	ret &= ~mask;
	ret |= val;

	return __stmpe_reg_write(stmpe, reg, ret);
}

static int __stmpe_block_read(struct stmpe *stmpe, u8 reg, u8 length,
			      u8 *values)
{
	int ret;

	ret = stmpe->ci->read_block(stmpe, reg, length, values);
	if (ret < 0)
		dev_err(stmpe->dev, "failed to read regs %#x: %d\n", reg, ret);

	dev_vdbg(stmpe->dev, "rd: reg %#x (%d) => ret %#x\n", reg, length, ret);
	stmpe_dump_bytes("stmpe rd: ", values, length);

	return ret;
}

static int __stmpe_block_write(struct stmpe *stmpe, u8 reg, u8 length,
			const u8 *values)
{
	int ret;

	dev_vdbg(stmpe->dev, "wr: regs %#x (%d)\n", reg, length);
	stmpe_dump_bytes("stmpe wr: ", values, length);

	ret = stmpe->ci->write_block(stmpe, reg, length, values);
	if (ret < 0)
		dev_err(stmpe->dev, "failed to write regs %#x: %d\n", reg, ret);

	return ret;
}

/**
 * stmpe_enable - enable blocks on an STMPE device
 * @stmpe:	Device to work on
 * @blocks:	Mask of blocks (enum stmpe_block values) to enable
 */
int stmpe_enable(struct stmpe *stmpe, unsigned int blocks)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_enable(stmpe, blocks);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_enable);

/**
 * stmpe_disable - disable blocks on an STMPE device
 * @stmpe:	Device to work on
 * @blocks:	Mask of blocks (enum stmpe_block values) to enable
 */
int stmpe_disable(struct stmpe *stmpe, unsigned int blocks)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_disable(stmpe, blocks);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_disable);

/**
 * stmpe_reg_read() - read a single STMPE register
 * @stmpe:	Device to read from
 * @reg:	Register to read
 */
int stmpe_reg_read(struct stmpe *stmpe, u8 reg)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_reg_read(stmpe, reg);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_reg_read);

/**
 * stmpe_reg_write() - write a single STMPE register
 * @stmpe:	Device to write to
 * @reg:	Register to write
 * @val:	Value to write
 */
int stmpe_reg_write(struct stmpe *stmpe, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_reg_write(stmpe, reg, val);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_reg_write);

/**
 * stmpe_set_bits() - set the value of a bitfield in a STMPE register
 * @stmpe:	Device to write to
 * @reg:	Register to write
 * @mask:	Mask of bits to set
 * @val:	Value to set
 */
int stmpe_set_bits(struct stmpe *stmpe, u8 reg, u8 mask, u8 val)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_set_bits(stmpe, reg, mask, val);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_set_bits);

/**
 * stmpe_block_read() - read multiple STMPE registers
 * @stmpe:	Device to read from
 * @reg:	First register
 * @length:	Number of registers
 * @values:	Buffer to write to
 */
int stmpe_block_read(struct stmpe *stmpe, u8 reg, u8 length, u8 *values)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_block_read(stmpe, reg, length, values);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_block_read);

/**
 * stmpe_block_write() - write multiple STMPE registers
 * @stmpe:	Device to write to
 * @reg:	First register
 * @length:	Number of registers
 * @values:	Values to write
 */
int stmpe_block_write(struct stmpe *stmpe, u8 reg, u8 length,
		      const u8 *values)
{
	int ret;

	mutex_lock(&stmpe->lock);
	ret = __stmpe_block_write(stmpe, reg, length, values);
	mutex_unlock(&stmpe->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_block_write);

/**
 * stmpe_set_altfunc()- set the alternate function for STMPE pins
 * @stmpe:	Device to configure
 * @pins:	Bitmask of pins to affect
 * @block:	block to enable alternate functions for
 *
 * @pins is assumed to have a bit set for each of the bits whose alternate
 * function is to be changed, numbered according to the GPIOXY numbers.
 *
 * If the GPIO module is not enabled, this function automatically enables it in
 * order to perform the change.
 */
int stmpe_set_altfunc(struct stmpe *stmpe, u32 pins, enum stmpe_block block)
{
	struct stmpe_variant_info *variant = stmpe->variant;
	u8 regaddr = stmpe->regs[STMPE_IDX_GPAFR_U_MSB];
	int af_bits = variant->af_bits;
	int numregs = DIV_ROUND_UP(stmpe->num_gpios * af_bits, 8);
	int mask = (1 << af_bits) - 1;
	u8 regs[numregs];
	int af, afperreg, ret;

	if (!variant->get_altfunc)
		return 0;

	afperreg = 8 / af_bits;
	mutex_lock(&stmpe->lock);

	ret = __stmpe_enable(stmpe, STMPE_BLOCK_GPIO);
	if (ret < 0)
		goto out;

	ret = __stmpe_block_read(stmpe, regaddr, numregs, regs);
	if (ret < 0)
		goto out;

	af = variant->get_altfunc(stmpe, block);

	while (pins) {
		int pin = __ffs(pins);
		int regoffset = numregs - (pin / afperreg) - 1;
		int pos = (pin % afperreg) * (8 / afperreg);

		regs[regoffset] &= ~(mask << pos);
		regs[regoffset] |= af << pos;

		pins &= ~(1 << pin);
	}

	ret = __stmpe_block_write(stmpe, regaddr, numregs, regs);

out:
	mutex_unlock(&stmpe->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(stmpe_set_altfunc);

/*
 * GPIO (all variants)
 */

static struct resource stmpe_gpio_resources[] = {
	/* Start and end filled dynamically */
	{
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell stmpe_gpio_cell = {
	.name		= "stmpe-gpio",
	.resources	= stmpe_gpio_resources,
	.num_resources	= ARRAY_SIZE(stmpe_gpio_resources),
};

static struct mfd_cell stmpe_gpio_cell_noirq = {
	.name		= "stmpe-gpio",
	/* gpio cell resources consist of an irq only so no resources here */
};

/*
 * Keypad (1601, 2401, 2403)
 */

static struct resource stmpe_keypad_resources[] = {
	{
		.name	= "KEYPAD",
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "KEYPAD_OVER",
		.start	= 1,
		.end	= 1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell stmpe_keypad_cell = {
	.name		= "stmpe-keypad",
	.resources	= stmpe_keypad_resources,
	.num_resources	= ARRAY_SIZE(stmpe_keypad_resources),
};

/*
 * STMPE801
 */
static const u8 stmpe801_regs[] = {
	[STMPE_IDX_CHIP_ID]	= STMPE801_REG_CHIP_ID,
	[STMPE_IDX_ICR_LSB]	= STMPE801_REG_SYS_CTRL,
	[STMPE_IDX_GPMR_LSB]	= STMPE801_REG_GPIO_MP_STA,
	[STMPE_IDX_GPSR_LSB]	= STMPE801_REG_GPIO_SET_PIN,
	[STMPE_IDX_GPCR_LSB]	= STMPE801_REG_GPIO_SET_PIN,
	[STMPE_IDX_GPDR_LSB]	= STMPE801_REG_GPIO_DIR,
	[STMPE_IDX_IEGPIOR_LSB] = STMPE801_REG_GPIO_INT_EN,
	[STMPE_IDX_ISGPIOR_MSB] = STMPE801_REG_GPIO_INT_STA,

};

static struct stmpe_variant_block stmpe801_blocks[] = {
	{
		.cell	= &stmpe_gpio_cell,
		.irq	= 0,
		.block	= STMPE_BLOCK_GPIO,
	},
};

static struct stmpe_variant_block stmpe801_blocks_noirq[] = {
	{
		.cell	= &stmpe_gpio_cell_noirq,
		.block	= STMPE_BLOCK_GPIO,
	},
};

static int stmpe801_enable(struct stmpe *stmpe, unsigned int blocks,
			   bool enable)
{
	if (blocks & STMPE_BLOCK_GPIO)
		return 0;
	else
		return -EINVAL;
}

static struct stmpe_variant_info stmpe801 = {
	.name		= "stmpe801",
	.id_val		= STMPE801_ID,
	.id_mask	= 0xffff,
	.num_gpios	= 8,
	.regs		= stmpe801_regs,
	.blocks		= stmpe801_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe801_blocks),
	.num_irqs	= STMPE801_NR_INTERNAL_IRQS,
	.enable		= stmpe801_enable,
};

static struct stmpe_variant_info stmpe801_noirq = {
	.name		= "stmpe801",
	.id_val		= STMPE801_ID,
	.id_mask	= 0xffff,
	.num_gpios	= 8,
	.regs		= stmpe801_regs,
	.blocks		= stmpe801_blocks_noirq,
	.num_blocks	= ARRAY_SIZE(stmpe801_blocks_noirq),
	.enable		= stmpe801_enable,
};

/*
 * Touchscreen (STMPE811 or STMPE610)
 */

static struct resource stmpe_ts_resources[] = {
	{
		.name	= "TOUCH_DET",
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "FIFO_TH",
		.start	= 1,
		.end	= 1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell stmpe_ts_cell = {
	.name		= "stmpe-ts",
	.resources	= stmpe_ts_resources,
	.num_resources	= ARRAY_SIZE(stmpe_ts_resources),
};

/*
 * STMPE811 or STMPE610
 */

static const u8 stmpe811_regs[] = {
	[STMPE_IDX_CHIP_ID]	= STMPE811_REG_CHIP_ID,
	[STMPE_IDX_ICR_LSB]	= STMPE811_REG_INT_CTRL,
	[STMPE_IDX_IER_LSB]	= STMPE811_REG_INT_EN,
	[STMPE_IDX_ISR_MSB]	= STMPE811_REG_INT_STA,
	[STMPE_IDX_GPMR_LSB]	= STMPE811_REG_GPIO_MP_STA,
	[STMPE_IDX_GPSR_LSB]	= STMPE811_REG_GPIO_SET_PIN,
	[STMPE_IDX_GPCR_LSB]	= STMPE811_REG_GPIO_CLR_PIN,
	[STMPE_IDX_GPDR_LSB]	= STMPE811_REG_GPIO_DIR,
	[STMPE_IDX_GPRER_LSB]	= STMPE811_REG_GPIO_RE,
	[STMPE_IDX_GPFER_LSB]	= STMPE811_REG_GPIO_FE,
	[STMPE_IDX_GPAFR_U_MSB]	= STMPE811_REG_GPIO_AF,
	[STMPE_IDX_IEGPIOR_LSB]	= STMPE811_REG_GPIO_INT_EN,
	[STMPE_IDX_ISGPIOR_MSB]	= STMPE811_REG_GPIO_INT_STA,
	[STMPE_IDX_GPEDR_MSB]	= STMPE811_REG_GPIO_ED,
};

static struct stmpe_variant_block stmpe811_blocks[] = {
	{
		.cell	= &stmpe_gpio_cell,
		.irq	= STMPE811_IRQ_GPIOC,
		.block	= STMPE_BLOCK_GPIO,
	},
	{
		.cell	= &stmpe_ts_cell,
		.irq	= STMPE811_IRQ_TOUCH_DET,
		.block	= STMPE_BLOCK_TOUCHSCREEN,
	},
};

static int stmpe811_enable(struct stmpe *stmpe, unsigned int blocks,
			   bool enable)
{
	unsigned int mask = 0;

	if (blocks & STMPE_BLOCK_GPIO)
		mask |= STMPE811_SYS_CTRL2_GPIO_OFF;

	if (blocks & STMPE_BLOCK_ADC)
		mask |= STMPE811_SYS_CTRL2_ADC_OFF;

	if (blocks & STMPE_BLOCK_TOUCHSCREEN)
		mask |= STMPE811_SYS_CTRL2_TSC_OFF;

	return __stmpe_set_bits(stmpe, STMPE811_REG_SYS_CTRL2, mask,
				enable ? 0 : mask);
}

static int stmpe811_get_altfunc(struct stmpe *stmpe, enum stmpe_block block)
{
	/* 0 for touchscreen, 1 for GPIO */
	return block != STMPE_BLOCK_TOUCHSCREEN;
}

static struct stmpe_variant_info stmpe811 = {
	.name		= "stmpe811",
	.id_val		= 0x0811,
	.id_mask	= 0xffff,
	.num_gpios	= 8,
	.af_bits	= 1,
	.regs		= stmpe811_regs,
	.blocks		= stmpe811_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe811_blocks),
	.num_irqs	= STMPE811_NR_INTERNAL_IRQS,
	.enable		= stmpe811_enable,
	.get_altfunc	= stmpe811_get_altfunc,
};

/* Similar to 811, except number of gpios */
static struct stmpe_variant_info stmpe610 = {
	.name		= "stmpe610",
	.id_val		= 0x0811,
	.id_mask	= 0xffff,
	.num_gpios	= 6,
	.af_bits	= 1,
	.regs		= stmpe811_regs,
	.blocks		= stmpe811_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe811_blocks),
	.num_irqs	= STMPE811_NR_INTERNAL_IRQS,
	.enable		= stmpe811_enable,
	.get_altfunc	= stmpe811_get_altfunc,
};

/*
 * STMPE1601
 */

static const u8 stmpe1601_regs[] = {
	[STMPE_IDX_CHIP_ID]	= STMPE1601_REG_CHIP_ID,
	[STMPE_IDX_ICR_LSB]	= STMPE1601_REG_ICR_LSB,
	[STMPE_IDX_IER_LSB]	= STMPE1601_REG_IER_LSB,
	[STMPE_IDX_ISR_MSB]	= STMPE1601_REG_ISR_MSB,
	[STMPE_IDX_GPMR_LSB]	= STMPE1601_REG_GPIO_MP_LSB,
	[STMPE_IDX_GPSR_LSB]	= STMPE1601_REG_GPIO_SET_LSB,
	[STMPE_IDX_GPCR_LSB]	= STMPE1601_REG_GPIO_CLR_LSB,
	[STMPE_IDX_GPDR_LSB]	= STMPE1601_REG_GPIO_SET_DIR_LSB,
	[STMPE_IDX_GPRER_LSB]	= STMPE1601_REG_GPIO_RE_LSB,
	[STMPE_IDX_GPFER_LSB]	= STMPE1601_REG_GPIO_FE_LSB,
	[STMPE_IDX_GPAFR_U_MSB]	= STMPE1601_REG_GPIO_AF_U_MSB,
	[STMPE_IDX_IEGPIOR_LSB]	= STMPE1601_REG_INT_EN_GPIO_MASK_LSB,
	[STMPE_IDX_ISGPIOR_MSB]	= STMPE1601_REG_INT_STA_GPIO_MSB,
	[STMPE_IDX_GPEDR_MSB]	= STMPE1601_REG_GPIO_ED_MSB,
};

static struct stmpe_variant_block stmpe1601_blocks[] = {
	{
		.cell	= &stmpe_gpio_cell,
		.irq	= STMPE24XX_IRQ_GPIOC,
		.block	= STMPE_BLOCK_GPIO,
	},
	{
		.cell	= &stmpe_keypad_cell,
		.irq	= STMPE24XX_IRQ_KEYPAD,
		.block	= STMPE_BLOCK_KEYPAD,
	},
};

/* supported autosleep timeout delay (in msecs) */
static const int stmpe_autosleep_delay[] = {
	4, 16, 32, 64, 128, 256, 512, 1024,
};

static int stmpe_round_timeout(int timeout)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(stmpe_autosleep_delay); i++) {
		if (stmpe_autosleep_delay[i] >= timeout)
			return i;
	}

	/*
	 * requests for delays longer than supported should not return the
	 * longest supported delay
	 */
	return -EINVAL;
}

static int stmpe_autosleep(struct stmpe *stmpe, int autosleep_timeout)
{
	int ret;

	if (!stmpe->variant->enable_autosleep)
		return -ENOSYS;

	mutex_lock(&stmpe->lock);
	ret = stmpe->variant->enable_autosleep(stmpe, autosleep_timeout);
	mutex_unlock(&stmpe->lock);

	return ret;
}

/*
 * Both stmpe 1601/2403 support same layout for autosleep
 */
static int stmpe1601_autosleep(struct stmpe *stmpe,
		int autosleep_timeout)
{
	int ret, timeout;

	/* choose the best available timeout */
	timeout = stmpe_round_timeout(autosleep_timeout);
	if (timeout < 0) {
		dev_err(stmpe->dev, "invalid timeout\n");
		return timeout;
	}

	ret = __stmpe_set_bits(stmpe, STMPE1601_REG_SYS_CTRL2,
			STMPE1601_AUTOSLEEP_TIMEOUT_MASK,
			timeout);
	if (ret < 0)
		return ret;

	return __stmpe_set_bits(stmpe, STMPE1601_REG_SYS_CTRL2,
			STPME1601_AUTOSLEEP_ENABLE,
			STPME1601_AUTOSLEEP_ENABLE);
}

static int stmpe1601_enable(struct stmpe *stmpe, unsigned int blocks,
			    bool enable)
{
	unsigned int mask = 0;

	if (blocks & STMPE_BLOCK_GPIO)
		mask |= STMPE1601_SYS_CTRL_ENABLE_GPIO;

	if (blocks & STMPE_BLOCK_KEYPAD)
		mask |= STMPE1601_SYS_CTRL_ENABLE_KPC;

	return __stmpe_set_bits(stmpe, STMPE1601_REG_SYS_CTRL, mask,
				enable ? mask : 0);
}

static int stmpe1601_get_altfunc(struct stmpe *stmpe, enum stmpe_block block)
{
	switch (block) {
	case STMPE_BLOCK_PWM:
		return 2;

	case STMPE_BLOCK_KEYPAD:
		return 1;

	case STMPE_BLOCK_GPIO:
	default:
		return 0;
	}
}

static struct stmpe_variant_info stmpe1601 = {
	.name		= "stmpe1601",
	.id_val		= 0x0210,
	.id_mask	= 0xfff0,	/* at least 0x0210 and 0x0212 */
	.num_gpios	= 16,
	.af_bits	= 2,
	.regs		= stmpe1601_regs,
	.blocks		= stmpe1601_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe1601_blocks),
	.num_irqs	= STMPE1601_NR_INTERNAL_IRQS,
	.enable		= stmpe1601_enable,
	.get_altfunc	= stmpe1601_get_altfunc,
	.enable_autosleep	= stmpe1601_autosleep,
};

/*
 * STMPE24XX
 */

static const u8 stmpe24xx_regs[] = {
	[STMPE_IDX_CHIP_ID]	= STMPE24XX_REG_CHIP_ID,
	[STMPE_IDX_ICR_LSB]	= STMPE24XX_REG_ICR_LSB,
	[STMPE_IDX_IER_LSB]	= STMPE24XX_REG_IER_LSB,
	[STMPE_IDX_ISR_MSB]	= STMPE24XX_REG_ISR_MSB,
	[STMPE_IDX_GPMR_LSB]	= STMPE24XX_REG_GPMR_LSB,
	[STMPE_IDX_GPSR_LSB]	= STMPE24XX_REG_GPSR_LSB,
	[STMPE_IDX_GPCR_LSB]	= STMPE24XX_REG_GPCR_LSB,
	[STMPE_IDX_GPDR_LSB]	= STMPE24XX_REG_GPDR_LSB,
	[STMPE_IDX_GPRER_LSB]	= STMPE24XX_REG_GPRER_LSB,
	[STMPE_IDX_GPFER_LSB]	= STMPE24XX_REG_GPFER_LSB,
	[STMPE_IDX_GPAFR_U_MSB]	= STMPE24XX_REG_GPAFR_U_MSB,
	[STMPE_IDX_IEGPIOR_LSB]	= STMPE24XX_REG_IEGPIOR_LSB,
	[STMPE_IDX_ISGPIOR_MSB]	= STMPE24XX_REG_ISGPIOR_MSB,
	[STMPE_IDX_GPEDR_MSB]	= STMPE24XX_REG_GPEDR_MSB,
};

static struct stmpe_variant_block stmpe24xx_blocks[] = {
	{
		.cell	= &stmpe_gpio_cell,
		.irq	= STMPE24XX_IRQ_GPIOC,
		.block	= STMPE_BLOCK_GPIO,
	},
	{
		.cell	= &stmpe_keypad_cell,
		.irq	= STMPE24XX_IRQ_KEYPAD,
		.block	= STMPE_BLOCK_KEYPAD,
	},
};

static int stmpe24xx_enable(struct stmpe *stmpe, unsigned int blocks,
			    bool enable)
{
	unsigned int mask = 0;

	if (blocks & STMPE_BLOCK_GPIO)
		mask |= STMPE24XX_SYS_CTRL_ENABLE_GPIO;

	if (blocks & STMPE_BLOCK_KEYPAD)
		mask |= STMPE24XX_SYS_CTRL_ENABLE_KPC;

	return __stmpe_set_bits(stmpe, STMPE24XX_REG_SYS_CTRL, mask,
				enable ? mask : 0);
}

static int stmpe24xx_get_altfunc(struct stmpe *stmpe, enum stmpe_block block)
{
	switch (block) {
	case STMPE_BLOCK_ROTATOR:
		return 2;

	case STMPE_BLOCK_KEYPAD:
		return 1;

	case STMPE_BLOCK_GPIO:
	default:
		return 0;
	}
}

static struct stmpe_variant_info stmpe2401 = {
	.name		= "stmpe2401",
	.id_val		= 0x0101,
	.id_mask	= 0xffff,
	.num_gpios	= 24,
	.af_bits	= 2,
	.regs		= stmpe24xx_regs,
	.blocks		= stmpe24xx_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe24xx_blocks),
	.num_irqs	= STMPE24XX_NR_INTERNAL_IRQS,
	.enable		= stmpe24xx_enable,
	.get_altfunc	= stmpe24xx_get_altfunc,
};

static struct stmpe_variant_info stmpe2403 = {
	.name		= "stmpe2403",
	.id_val		= 0x0120,
	.id_mask	= 0xffff,
	.num_gpios	= 24,
	.af_bits	= 2,
	.regs		= stmpe24xx_regs,
	.blocks		= stmpe24xx_blocks,
	.num_blocks	= ARRAY_SIZE(stmpe24xx_blocks),
	.num_irqs	= STMPE24XX_NR_INTERNAL_IRQS,
	.enable		= stmpe24xx_enable,
	.get_altfunc	= stmpe24xx_get_altfunc,
	.enable_autosleep	= stmpe1601_autosleep, /* same as stmpe1601 */
};

static struct stmpe_variant_info *stmpe_variant_info[STMPE_NBR_PARTS] = {
	[STMPE610]	= &stmpe610,
	[STMPE801]	= &stmpe801,
	[STMPE811]	= &stmpe811,
	[STMPE1601]	= &stmpe1601,
	[STMPE2401]	= &stmpe2401,
	[STMPE2403]	= &stmpe2403,
};

/*
 * These devices can be connected in a 'no-irq' configuration - the irq pin
 * is not used and the device cannot interrupt the CPU. Here we only list
 * devices which support this configuration - the driver will fail probing
 * for any devices not listed here which are configured in this way.
 */
static struct stmpe_variant_info *stmpe_noirq_variant_info[STMPE_NBR_PARTS] = {
	[STMPE801]	= &stmpe801_noirq,
};

static irqreturn_t stmpe_irq(int irq, void *data)
{
	struct stmpe *stmpe = data;
	struct stmpe_variant_info *variant = stmpe->variant;
	int num = DIV_ROUND_UP(variant->num_irqs, 8);
	u8 israddr = stmpe->regs[STMPE_IDX_ISR_MSB];
	u8 isr[num];
	int ret;
	int i;

	if (variant->id_val == STMPE801_ID) {
		handle_nested_irq(stmpe->irq_base);
		return IRQ_HANDLED;
	}

	ret = stmpe_block_read(stmpe, israddr, num, isr);
	if (ret < 0)
		return IRQ_NONE;

	for (i = 0; i < num; i++) {
		int bank = num - i - 1;
		u8 status = isr[i];
		u8 clear;

		status &= stmpe->ier[bank];
		if (!status)
			continue;

		clear = status;
		while (status) {
			int bit = __ffs(status);
			int line = bank * 8 + bit;

			handle_nested_irq(stmpe->irq_base + line);
			status &= ~(1 << bit);
		}

		stmpe_reg_write(stmpe, israddr + i, clear);
	}

	return IRQ_HANDLED;
}

static void stmpe_irq_lock(struct irq_data *data)
{
	struct stmpe *stmpe = irq_data_get_irq_chip_data(data);

	mutex_lock(&stmpe->irq_lock);
}

static void stmpe_irq_sync_unlock(struct irq_data *data)
{
	struct stmpe *stmpe = irq_data_get_irq_chip_data(data);
	struct stmpe_variant_info *variant = stmpe->variant;
	int num = DIV_ROUND_UP(variant->num_irqs, 8);
	int i;

	for (i = 0; i < num; i++) {
		u8 new = stmpe->ier[i];
		u8 old = stmpe->oldier[i];

		if (new == old)
			continue;

		stmpe->oldier[i] = new;
		stmpe_reg_write(stmpe, stmpe->regs[STMPE_IDX_IER_LSB] - i, new);
	}

	mutex_unlock(&stmpe->irq_lock);
}

static void stmpe_irq_mask(struct irq_data *data)
{
	struct stmpe *stmpe = irq_data_get_irq_chip_data(data);
	int offset = data->irq - stmpe->irq_base;
	int regoffset = offset / 8;
	int mask = 1 << (offset % 8);

	stmpe->ier[regoffset] &= ~mask;
}

static void stmpe_irq_unmask(struct irq_data *data)
{
	struct stmpe *stmpe = irq_data_get_irq_chip_data(data);
	int offset = data->irq - stmpe->irq_base;
	int regoffset = offset / 8;
	int mask = 1 << (offset % 8);

	stmpe->ier[regoffset] |= mask;
}

static struct irq_chip stmpe_irq_chip = {
	.name			= "stmpe",
	.irq_bus_lock		= stmpe_irq_lock,
	.irq_bus_sync_unlock	= stmpe_irq_sync_unlock,
	.irq_mask		= stmpe_irq_mask,
	.irq_unmask		= stmpe_irq_unmask,
};

static int __devinit stmpe_irq_init(struct stmpe *stmpe)
{
	struct irq_chip *chip = NULL;
	int num_irqs = stmpe->variant->num_irqs;
	int base = stmpe->irq_base;
	int irq;

	if (stmpe->variant->id_val != STMPE801_ID)
		chip = &stmpe_irq_chip;

	for (irq = base; irq < base + num_irqs; irq++) {
		irq_set_chip_data(irq, stmpe);
		irq_set_chip_and_handler(irq, chip, handle_edge_irq);
		irq_set_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	return 0;
}

static void stmpe_irq_remove(struct stmpe *stmpe)
{
	int num_irqs = stmpe->variant->num_irqs;
	int base = stmpe->irq_base;
	int irq;

	for (irq = base; irq < base + num_irqs; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

static int __devinit stmpe_chip_init(struct stmpe *stmpe)
{
	unsigned int irq_trigger = stmpe->pdata->irq_trigger;
	int autosleep_timeout = stmpe->pdata->autosleep_timeout;
	struct stmpe_variant_info *variant = stmpe->variant;
	u8 icr = 0;
	unsigned int id;
	u8 data[2];
	int ret;

	ret = stmpe_block_read(stmpe, stmpe->regs[STMPE_IDX_CHIP_ID],
			       ARRAY_SIZE(data), data);
	if (ret < 0)
		return ret;

	id = (data[0] << 8) | data[1];
	if ((id & variant->id_mask) != variant->id_val) {
		dev_err(stmpe->dev, "unknown chip id: %#x\n", id);
		return -EINVAL;
	}

	dev_info(stmpe->dev, "%s detected, chip id: %#x\n", variant->name, id);

	/* Disable all modules -- subdrivers should enable what they need. */
	ret = stmpe_disable(stmpe, ~0);
	if (ret)
		return ret;

	if (stmpe->irq >= 0) {
		if (id == STMPE801_ID)
			icr = STMPE801_REG_SYS_CTRL_INT_EN;
		else
			icr = STMPE_ICR_LSB_GIM;

		/* STMPE801 doesn't support Edge interrupts */
		if (id != STMPE801_ID) {
			if (irq_trigger == IRQF_TRIGGER_FALLING ||
					irq_trigger == IRQF_TRIGGER_RISING)
				icr |= STMPE_ICR_LSB_EDGE;
		}

		if (irq_trigger == IRQF_TRIGGER_RISING ||
				irq_trigger == IRQF_TRIGGER_HIGH) {
			if (id == STMPE801_ID)
				icr |= STMPE801_REG_SYS_CTRL_INT_HI;
			else
				icr |= STMPE_ICR_LSB_HIGH;
		}

		if (stmpe->pdata->irq_invert_polarity) {
			if (id == STMPE801_ID)
				icr ^= STMPE801_REG_SYS_CTRL_INT_HI;
			else
				icr ^= STMPE_ICR_LSB_HIGH;
		}
	}

	if (stmpe->pdata->autosleep) {
		ret = stmpe_autosleep(stmpe, autosleep_timeout);
		if (ret)
			return ret;
	}

	return stmpe_reg_write(stmpe, stmpe->regs[STMPE_IDX_ICR_LSB], icr);
}

static int __devinit stmpe_add_device(struct stmpe *stmpe,
				      struct mfd_cell *cell, int irq)
{
	return mfd_add_devices(stmpe->dev, stmpe->pdata->id, cell, 1,
			       NULL, stmpe->irq_base + irq);
}

static int __devinit stmpe_devices_init(struct stmpe *stmpe)
{
	struct stmpe_variant_info *variant = stmpe->variant;
	unsigned int platform_blocks = stmpe->pdata->blocks;
	int ret = -EINVAL;
	int i;

	for (i = 0; i < variant->num_blocks; i++) {
		struct stmpe_variant_block *block = &variant->blocks[i];

		if (!(platform_blocks & block->block))
			continue;

		platform_blocks &= ~block->block;
		ret = stmpe_add_device(stmpe, block->cell, block->irq);
		if (ret)
			return ret;
	}

	if (platform_blocks)
		dev_warn(stmpe->dev,
			 "platform wants blocks (%#x) not present on variant",
			 platform_blocks);

	return ret;
}

/* Called from client specific probe routines */
int __devinit stmpe_probe(struct stmpe_client_info *ci, int partnum)
{
	struct stmpe_platform_data *pdata = dev_get_platdata(ci->dev);
	struct stmpe *stmpe;
	int ret;

	if (!pdata)
		return -EINVAL;

	stmpe = kzalloc(sizeof(struct stmpe), GFP_KERNEL);
	if (!stmpe)
		return -ENOMEM;

	mutex_init(&stmpe->irq_lock);
	mutex_init(&stmpe->lock);

	stmpe->dev = ci->dev;
	stmpe->client = ci->client;
	stmpe->pdata = pdata;
	stmpe->irq_base = pdata->irq_base;
	stmpe->ci = ci;
	stmpe->partnum = partnum;
	stmpe->variant = stmpe_variant_info[partnum];
	stmpe->regs = stmpe->variant->regs;
	stmpe->num_gpios = stmpe->variant->num_gpios;
	dev_set_drvdata(stmpe->dev, stmpe);

	if (ci->init)
		ci->init(stmpe);

	if (pdata->irq_over_gpio) {
		ret = gpio_request_one(pdata->irq_gpio, GPIOF_DIR_IN, "stmpe");
		if (ret) {
			dev_err(stmpe->dev, "failed to request IRQ GPIO: %d\n",
					ret);
			goto out_free;
		}

		stmpe->irq = gpio_to_irq(pdata->irq_gpio);
	} else {
		stmpe->irq = ci->irq;
	}

	if (stmpe->irq < 0) {
		/* use alternate variant info for no-irq mode, if supported */
		dev_info(stmpe->dev,
			"%s configured in no-irq mode by platform data\n",
			stmpe->variant->name);
		if (!stmpe_noirq_variant_info[stmpe->partnum]) {
			dev_err(stmpe->dev,
				"%s does not support no-irq mode!\n",
				stmpe->variant->name);
			ret = -ENODEV;
			goto free_gpio;
		}
		stmpe->variant = stmpe_noirq_variant_info[stmpe->partnum];
	}

	ret = stmpe_chip_init(stmpe);
	if (ret)
		goto free_gpio;

	if (stmpe->irq >= 0) {
		ret = stmpe_irq_init(stmpe);
		if (ret)
			goto free_gpio;

		ret = request_threaded_irq(stmpe->irq, NULL, stmpe_irq,
				pdata->irq_trigger | IRQF_ONESHOT,
				"stmpe", stmpe);
		if (ret) {
			dev_err(stmpe->dev, "failed to request IRQ: %d\n",
					ret);
			goto out_removeirq;
		}
	}

	ret = stmpe_devices_init(stmpe);
	if (ret) {
		dev_err(stmpe->dev, "failed to add children\n");
		goto out_removedevs;
	}

	return 0;

out_removedevs:
	mfd_remove_devices(stmpe->dev);
	if (stmpe->irq >= 0)
		free_irq(stmpe->irq, stmpe);
out_removeirq:
	if (stmpe->irq >= 0)
		stmpe_irq_remove(stmpe);
free_gpio:
	if (pdata->irq_over_gpio)
		gpio_free(pdata->irq_gpio);
out_free:
	kfree(stmpe);
	return ret;
}

int stmpe_remove(struct stmpe *stmpe)
{
	mfd_remove_devices(stmpe->dev);

	if (stmpe->irq >= 0) {
		free_irq(stmpe->irq, stmpe);
		stmpe_irq_remove(stmpe);
	}

	if (stmpe->pdata->irq_over_gpio)
		gpio_free(stmpe->pdata->irq_gpio);

	kfree(stmpe);

	return 0;
}

#ifdef CONFIG_PM
static int stmpe_suspend(struct device *dev)
{
	struct stmpe *stmpe = dev_get_drvdata(dev);

	if (stmpe->irq >= 0 && device_may_wakeup(dev))
		enable_irq_wake(stmpe->irq);

	return 0;
}

static int stmpe_resume(struct device *dev)
{
	struct stmpe *stmpe = dev_get_drvdata(dev);

	if (stmpe->irq >= 0 && device_may_wakeup(dev))
		disable_irq_wake(stmpe->irq);

	return 0;
}

const struct dev_pm_ops stmpe_dev_pm_ops = {
	.suspend	= stmpe_suspend,
	.resume		= stmpe_resume,
};
#endif

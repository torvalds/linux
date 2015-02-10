/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#ifndef __STMPE_H
#define __STMPE_H

#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/stmpe.h>
#include <linux/printk.h>
#include <linux/types.h>

extern const struct dev_pm_ops stmpe_dev_pm_ops;

#ifdef STMPE_DUMP_BYTES
static inline void stmpe_dump_bytes(const char *str, const void *buf,
				    size_t len)
{
	print_hex_dump_bytes(str, DUMP_PREFIX_OFFSET, buf, len);
}
#else
static inline void stmpe_dump_bytes(const char *str, const void *buf,
				    size_t len)
{
}
#endif

/**
 * struct stmpe_variant_block - information about block
 * @cell:	base mfd cell
 * @irq:	interrupt number to be added to each IORESOURCE_IRQ
 *		in the cell
 * @block:	block id; used for identification with platform data and for
 *		enable and altfunc callbacks
 */
struct stmpe_variant_block {
	const struct mfd_cell	*cell;
	int			irq;
	enum stmpe_block	block;
};

/**
 * struct stmpe_variant_info - variant-specific information
 * @name:	part name
 * @id_val:	content of CHIPID register
 * @id_mask:	bits valid in CHIPID register for comparison with id_val
 * @num_gpios:	number of GPIOS
 * @af_bits:	number of bits used to specify the alternate function
 * @regs: variant specific registers.
 * @blocks:	list of blocks present on this device
 * @num_blocks:	number of blocks present on this device
 * @num_irqs:	number of internal IRQs available on this device
 * @enable:	callback to enable the specified blocks.
 *		Called with the I/O lock held.
 * @get_altfunc: callback to get the alternate function number for the
 *		 specific block
 * @enable_autosleep: callback to configure autosleep with specified timeout
 */
struct stmpe_variant_info {
	const char *name;
	u16 id_val;
	u16 id_mask;
	int num_gpios;
	int af_bits;
	const u8 *regs;
	struct stmpe_variant_block *blocks;
	int num_blocks;
	int num_irqs;
	int (*enable)(struct stmpe *stmpe, unsigned int blocks, bool enable);
	int (*get_altfunc)(struct stmpe *stmpe, enum stmpe_block block);
	int (*enable_autosleep)(struct stmpe *stmpe, int autosleep_timeout);
};

/**
 * struct stmpe_client_info - i2c or spi specific routines/info
 * @data: client specific data
 * @read_byte: read single byte
 * @write_byte: write single byte
 * @read_block: read block or multiple bytes
 * @write_block: write block or multiple bytes
 * @init: client init routine, called during probe
 */
struct stmpe_client_info {
	void *data;
	int irq;
	void *client;
	struct device *dev;
	int (*read_byte)(struct stmpe *stmpe, u8 reg);
	int (*write_byte)(struct stmpe *stmpe, u8 reg, u8 val);
	int (*read_block)(struct stmpe *stmpe, u8 reg, u8 len, u8 *values);
	int (*write_block)(struct stmpe *stmpe, u8 reg, u8 len,
			const u8 *values);
	void (*init)(struct stmpe *stmpe);
};

int stmpe_probe(struct stmpe_client_info *ci, enum stmpe_partnum partnum);
int stmpe_remove(struct stmpe *stmpe);

#define STMPE_ICR_LSB_HIGH	(1 << 2)
#define STMPE_ICR_LSB_EDGE	(1 << 1)
#define STMPE_ICR_LSB_GIM	(1 << 0)

/*
 * STMPE801
 */
#define STMPE801_ID			0x0108
#define STMPE801_NR_INTERNAL_IRQS	1

#define STMPE801_REG_CHIP_ID		0x00
#define STMPE801_REG_VERSION_ID		0x02
#define STMPE801_REG_SYS_CTRL		0x04
#define STMPE801_REG_GPIO_INT_EN	0x08
#define STMPE801_REG_GPIO_INT_STA	0x09
#define STMPE801_REG_GPIO_MP_STA	0x10
#define STMPE801_REG_GPIO_SET_PIN	0x11
#define STMPE801_REG_GPIO_DIR		0x12

#define STMPE801_REG_SYS_CTRL_RESET	(1 << 7)
#define STMPE801_REG_SYS_CTRL_INT_EN	(1 << 2)
#define STMPE801_REG_SYS_CTRL_INT_HI	(1 << 0)

/*
 * STMPE811
 */

#define STMPE811_IRQ_TOUCH_DET		0
#define STMPE811_IRQ_FIFO_TH		1
#define STMPE811_IRQ_FIFO_OFLOW		2
#define STMPE811_IRQ_FIFO_FULL		3
#define STMPE811_IRQ_FIFO_EMPTY		4
#define STMPE811_IRQ_TEMP_SENS		5
#define STMPE811_IRQ_ADC		6
#define STMPE811_IRQ_GPIOC		7
#define STMPE811_NR_INTERNAL_IRQS	8

#define STMPE811_REG_CHIP_ID		0x00
#define STMPE811_REG_SYS_CTRL2		0x04
#define STMPE811_REG_SPI_CFG		0x08
#define STMPE811_REG_INT_CTRL		0x09
#define STMPE811_REG_INT_EN		0x0A
#define STMPE811_REG_INT_STA		0x0B
#define STMPE811_REG_GPIO_INT_EN	0x0C
#define STMPE811_REG_GPIO_INT_STA	0x0D
#define STMPE811_REG_GPIO_SET_PIN	0x10
#define STMPE811_REG_GPIO_CLR_PIN	0x11
#define STMPE811_REG_GPIO_MP_STA	0x12
#define STMPE811_REG_GPIO_DIR		0x13
#define STMPE811_REG_GPIO_ED		0x14
#define STMPE811_REG_GPIO_RE		0x15
#define STMPE811_REG_GPIO_FE		0x16
#define STMPE811_REG_GPIO_AF		0x17

#define STMPE811_SYS_CTRL2_ADC_OFF	(1 << 0)
#define STMPE811_SYS_CTRL2_TSC_OFF	(1 << 1)
#define STMPE811_SYS_CTRL2_GPIO_OFF	(1 << 2)
#define STMPE811_SYS_CTRL2_TS_OFF	(1 << 3)

/*
 * STMPE1601
 */

#define STMPE1601_IRQ_GPIOC		8
#define STMPE1601_IRQ_PWM3		7
#define STMPE1601_IRQ_PWM2		6
#define STMPE1601_IRQ_PWM1		5
#define STMPE1601_IRQ_PWM0		4
#define STMPE1601_IRQ_KEYPAD_OVER	2
#define STMPE1601_IRQ_KEYPAD		1
#define STMPE1601_IRQ_WAKEUP		0
#define STMPE1601_NR_INTERNAL_IRQS	9

#define STMPE1601_REG_SYS_CTRL			0x02
#define STMPE1601_REG_SYS_CTRL2			0x03
#define STMPE1601_REG_ICR_LSB			0x11
#define STMPE1601_REG_IER_LSB			0x13
#define STMPE1601_REG_ISR_MSB			0x14
#define STMPE1601_REG_CHIP_ID			0x80
#define STMPE1601_REG_INT_EN_GPIO_MASK_LSB	0x17
#define STMPE1601_REG_INT_STA_GPIO_MSB		0x18
#define STMPE1601_REG_GPIO_MP_LSB		0x87
#define STMPE1601_REG_GPIO_SET_LSB		0x83
#define STMPE1601_REG_GPIO_CLR_LSB		0x85
#define STMPE1601_REG_GPIO_SET_DIR_LSB		0x89
#define STMPE1601_REG_GPIO_ED_MSB		0x8A
#define STMPE1601_REG_GPIO_RE_LSB		0x8D
#define STMPE1601_REG_GPIO_FE_LSB		0x8F
#define STMPE1601_REG_GPIO_PU_LSB		0x91
#define STMPE1601_REG_GPIO_AF_U_MSB		0x92

#define STMPE1601_SYS_CTRL_ENABLE_GPIO		(1 << 3)
#define STMPE1601_SYS_CTRL_ENABLE_KPC		(1 << 1)
#define STMPE1601_SYS_CTRL_ENABLE_SPWM		(1 << 0)

/* The 1601/2403 share the same masks */
#define STMPE1601_AUTOSLEEP_TIMEOUT_MASK	(0x7)
#define STPME1601_AUTOSLEEP_ENABLE		(1 << 3)

/*
 * STMPE1801
 */
#define STMPE1801_ID			0xc110
#define STMPE1801_NR_INTERNAL_IRQS	5
#define STMPE1801_IRQ_KEYPAD_COMBI	4
#define STMPE1801_IRQ_GPIOC		3
#define STMPE1801_IRQ_KEYPAD_OVER	2
#define STMPE1801_IRQ_KEYPAD		1
#define STMPE1801_IRQ_WAKEUP		0

#define STMPE1801_REG_CHIP_ID			0x00
#define STMPE1801_REG_SYS_CTRL			0x02
#define STMPE1801_REG_INT_CTRL_LOW		0x04
#define STMPE1801_REG_INT_EN_MASK_LOW		0x06
#define STMPE1801_REG_INT_STA_LOW		0x08
#define STMPE1801_REG_INT_EN_GPIO_MASK_LOW	0x0A
#define STMPE1801_REG_INT_EN_GPIO_MASK_MID	0x0B
#define STMPE1801_REG_INT_EN_GPIO_MASK_HIGH	0x0C
#define STMPE1801_REG_INT_STA_GPIO_LOW		0x0D
#define STMPE1801_REG_INT_STA_GPIO_MID		0x0E
#define STMPE1801_REG_INT_STA_GPIO_HIGH		0x0F
#define STMPE1801_REG_GPIO_SET_LOW		0x10
#define STMPE1801_REG_GPIO_SET_MID		0x11
#define STMPE1801_REG_GPIO_SET_HIGH		0x12
#define STMPE1801_REG_GPIO_CLR_LOW		0x13
#define STMPE1801_REG_GPIO_CLR_MID		0x14
#define STMPE1801_REG_GPIO_CLR_HIGH		0x15
#define STMPE1801_REG_GPIO_MP_LOW		0x16
#define STMPE1801_REG_GPIO_MP_MID		0x17
#define STMPE1801_REG_GPIO_MP_HIGH		0x18
#define STMPE1801_REG_GPIO_SET_DIR_LOW		0x19
#define STMPE1801_REG_GPIO_SET_DIR_MID		0x1A
#define STMPE1801_REG_GPIO_SET_DIR_HIGH		0x1B
#define STMPE1801_REG_GPIO_RE_LOW		0x1C
#define STMPE1801_REG_GPIO_RE_MID		0x1D
#define STMPE1801_REG_GPIO_RE_HIGH		0x1E
#define STMPE1801_REG_GPIO_FE_LOW		0x1F
#define STMPE1801_REG_GPIO_FE_MID		0x20
#define STMPE1801_REG_GPIO_FE_HIGH		0x21
#define STMPE1801_REG_GPIO_PULL_UP_LOW		0x22
#define STMPE1801_REG_GPIO_PULL_UP_MID		0x23
#define STMPE1801_REG_GPIO_PULL_UP_HIGH		0x24

#define STMPE1801_MSK_SYS_CTRL_RESET		(1 << 7)

#define STMPE1801_MSK_INT_EN_KPC		(1 << 1)
#define STMPE1801_MSK_INT_EN_GPIO		(1 << 3)

/*
 * STMPE24xx
 */

#define STMPE24XX_IRQ_GPIOC		8
#define STMPE24XX_IRQ_PWM2		7
#define STMPE24XX_IRQ_PWM1		6
#define STMPE24XX_IRQ_PWM0		5
#define STMPE24XX_IRQ_ROT_OVER		4
#define STMPE24XX_IRQ_ROT		3
#define STMPE24XX_IRQ_KEYPAD_OVER	2
#define STMPE24XX_IRQ_KEYPAD		1
#define STMPE24XX_IRQ_WAKEUP		0
#define STMPE24XX_NR_INTERNAL_IRQS	9

#define STMPE24XX_REG_SYS_CTRL		0x02
#define STMPE24XX_REG_ICR_LSB		0x11
#define STMPE24XX_REG_IER_LSB		0x13
#define STMPE24XX_REG_ISR_MSB		0x14
#define STMPE24XX_REG_CHIP_ID		0x80
#define STMPE24XX_REG_IEGPIOR_LSB	0x18
#define STMPE24XX_REG_ISGPIOR_MSB	0x19
#define STMPE24XX_REG_GPMR_LSB		0xA4
#define STMPE24XX_REG_GPSR_LSB		0x85
#define STMPE24XX_REG_GPCR_LSB		0x88
#define STMPE24XX_REG_GPDR_LSB		0x8B
#define STMPE24XX_REG_GPEDR_MSB		0x8C
#define STMPE24XX_REG_GPRER_LSB		0x91
#define STMPE24XX_REG_GPFER_LSB		0x94
#define STMPE24XX_REG_GPPUR_LSB		0x97
#define STMPE24XX_REG_GPPDR_LSB		0x9a
#define STMPE24XX_REG_GPAFR_U_MSB	0x9B

#define STMPE24XX_SYS_CTRL_ENABLE_GPIO		(1 << 3)
#define STMPE24XX_SYSCON_ENABLE_PWM		(1 << 2)
#define STMPE24XX_SYS_CTRL_ENABLE_KPC		(1 << 1)
#define STMPE24XX_SYSCON_ENABLE_ROT		(1 << 0)

#endif

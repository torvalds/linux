/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ImgTec IR Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 */

#ifndef _IMG_IR_H_
#define _IMG_IR_H_

#include <linux/io.h>
#include <linux/spinlock.h>

#include "img-ir-raw.h"
#include "img-ir-hw.h"

/* registers */

/* relative to the start of the IR block of registers */
#define IMG_IR_CONTROL		0x00
#define IMG_IR_STATUS		0x04
#define IMG_IR_DATA_LW		0x08
#define IMG_IR_DATA_UP		0x0c
#define IMG_IR_LEAD_SYMB_TIMING	0x10
#define IMG_IR_S00_SYMB_TIMING	0x14
#define IMG_IR_S01_SYMB_TIMING	0x18
#define IMG_IR_S10_SYMB_TIMING	0x1c
#define IMG_IR_S11_SYMB_TIMING	0x20
#define IMG_IR_FREE_SYMB_TIMING	0x24
#define IMG_IR_POW_MOD_PARAMS	0x28
#define IMG_IR_POW_MOD_ENABLE	0x2c
#define IMG_IR_IRQ_MSG_DATA_LW	0x30
#define IMG_IR_IRQ_MSG_DATA_UP	0x34
#define IMG_IR_IRQ_MSG_MASK_LW	0x38
#define IMG_IR_IRQ_MSG_MASK_UP	0x3c
#define IMG_IR_IRQ_ENABLE	0x40
#define IMG_IR_IRQ_STATUS	0x44
#define IMG_IR_IRQ_CLEAR	0x48
#define IMG_IR_IRCORE_ID	0xf0
#define IMG_IR_CORE_REV		0xf4
#define IMG_IR_CORE_DES1	0xf8
#define IMG_IR_CORE_DES2	0xfc


/* field masks */

/* IMG_IR_CONTROL */
#define IMG_IR_DECODEN		0x40000000
#define IMG_IR_CODETYPE		0x30000000
#define IMG_IR_CODETYPE_SHIFT		28
#define IMG_IR_HDRTOG		0x08000000
#define IMG_IR_LDRDEC		0x04000000
#define IMG_IR_DECODINPOL	0x02000000	/* active high */
#define IMG_IR_BITORIEN		0x01000000	/* MSB first */
#define IMG_IR_D1VALIDSEL	0x00008000
#define IMG_IR_BITINV		0x00000040	/* don't invert */
#define IMG_IR_DECODEND2	0x00000010
#define IMG_IR_BITORIEND2	0x00000002	/* MSB first */
#define IMG_IR_BITINVD2		0x00000001	/* don't invert */

/* IMG_IR_STATUS */
#define IMG_IR_RXDVALD2		0x00001000
#define IMG_IR_IRRXD		0x00000400
#define IMG_IR_TOGSTATE		0x00000200
#define IMG_IR_RXDVAL		0x00000040
#define IMG_IR_RXDLEN		0x0000003f
#define IMG_IR_RXDLEN_SHIFT		0

/* IMG_IR_LEAD_SYMB_TIMING, IMG_IR_Sxx_SYMB_TIMING */
#define IMG_IR_PD_MAX		0xff000000
#define IMG_IR_PD_MAX_SHIFT		24
#define IMG_IR_PD_MIN		0x00ff0000
#define IMG_IR_PD_MIN_SHIFT		16
#define IMG_IR_W_MAX		0x0000ff00
#define IMG_IR_W_MAX_SHIFT		8
#define IMG_IR_W_MIN		0x000000ff
#define IMG_IR_W_MIN_SHIFT		0

/* IMG_IR_FREE_SYMB_TIMING */
#define IMG_IR_MAXLEN		0x0007e000
#define IMG_IR_MAXLEN_SHIFT		13
#define IMG_IR_MINLEN		0x00001f00
#define IMG_IR_MINLEN_SHIFT		8
#define IMG_IR_FT_MIN		0x000000ff
#define IMG_IR_FT_MIN_SHIFT		0

/* IMG_IR_POW_MOD_PARAMS */
#define IMG_IR_PERIOD_LEN	0x3f000000
#define IMG_IR_PERIOD_LEN_SHIFT		24
#define IMG_IR_PERIOD_DUTY	0x003f0000
#define IMG_IR_PERIOD_DUTY_SHIFT	16
#define IMG_IR_STABLE_STOP	0x00003f00
#define IMG_IR_STABLE_STOP_SHIFT	8
#define IMG_IR_STABLE_START	0x0000003f
#define IMG_IR_STABLE_START_SHIFT	0

/* IMG_IR_POW_MOD_ENABLE */
#define IMG_IR_POWER_OUT_EN	0x00000002
#define IMG_IR_POWER_MOD_EN	0x00000001

/* IMG_IR_IRQ_ENABLE, IMG_IR_IRQ_STATUS, IMG_IR_IRQ_CLEAR */
#define IMG_IR_IRQ_DEC2_ERR	0x00000080
#define IMG_IR_IRQ_DEC_ERR	0x00000040
#define IMG_IR_IRQ_ACT_LEVEL	0x00000020
#define IMG_IR_IRQ_FALL_EDGE	0x00000010
#define IMG_IR_IRQ_RISE_EDGE	0x00000008
#define IMG_IR_IRQ_DATA_MATCH	0x00000004
#define IMG_IR_IRQ_DATA2_VALID	0x00000002
#define IMG_IR_IRQ_DATA_VALID	0x00000001
#define IMG_IR_IRQ_ALL		0x000000ff
#define IMG_IR_IRQ_EDGE		(IMG_IR_IRQ_FALL_EDGE | IMG_IR_IRQ_RISE_EDGE)

/* IMG_IR_CORE_ID */
#define IMG_IR_CORE_ID		0x00ff0000
#define IMG_IR_CORE_ID_SHIFT		16
#define IMG_IR_CORE_CONFIG	0x0000ffff
#define IMG_IR_CORE_CONFIG_SHIFT	0

/* IMG_IR_CORE_REV */
#define IMG_IR_DESIGNER		0xff000000
#define IMG_IR_DESIGNER_SHIFT		24
#define IMG_IR_MAJOR_REV	0x00ff0000
#define IMG_IR_MAJOR_REV_SHIFT		16
#define IMG_IR_MINOR_REV	0x0000ff00
#define IMG_IR_MINOR_REV_SHIFT		8
#define IMG_IR_MAINT_REV	0x000000ff
#define IMG_IR_MAINT_REV_SHIFT		0

struct device;
struct clk;

/**
 * struct img_ir_priv - Private driver data.
 * @dev:		Platform device.
 * @irq:		IRQ number.
 * @clk:		Input clock.
 * @sys_clk:		System clock.
 * @reg_base:		Iomem base address of IR register block.
 * @lock:		Protects IR registers and variables in this struct.
 * @raw:		Driver data for raw decoder.
 * @hw:			Driver data for hardware decoder.
 */
struct img_ir_priv {
	struct device		*dev;
	int			irq;
	struct clk		*clk;
	struct clk		*sys_clk;
	void __iomem		*reg_base;
	spinlock_t		lock;

	struct img_ir_priv_raw	raw;
	struct img_ir_priv_hw	hw;
};

/* Hardware access */

static inline void img_ir_write(struct img_ir_priv *priv,
				unsigned int reg_offs, unsigned int data)
{
	iowrite32(data, priv->reg_base + reg_offs);
}

static inline unsigned int img_ir_read(struct img_ir_priv *priv,
				       unsigned int reg_offs)
{
	return ioread32(priv->reg_base + reg_offs);
}

#endif /* _IMG_IR_H_ */

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __I2C_VIAI2C_COMMON_H_
#define __I2C_VIAI2C_COMMON_H_

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

/* REG_CR Bit fields */
#define VIAI2C_REG_CR		0x00
#define VIAI2C_CR_ENABLE		BIT(0)
#define VIAI2C_CR_RX_END		BIT(1)
#define VIAI2C_CR_TX_END		BIT(2)
#define VIAI2C_CR_CPU_RDY		BIT(3)
#define VIAI2C_CR_END_MASK		GENMASK(2, 1)

/* REG_TCR Bit fields */
#define VIAI2C_REG_TCR		0x02
#define VIAI2C_TCR_HS_MODE		BIT(13)
#define VIAI2C_TCR_READ			BIT(14)
#define VIAI2C_TCR_FAST			BIT(15)
#define VIAI2C_TCR_ADDR_MASK		GENMASK(6, 0)

/* REG_CSR Bit fields */
#define VIAI2C_REG_CSR		0x04
#define VIAI2C_CSR_RCV_NOT_ACK		BIT(0)
#define VIAI2C_CSR_RCV_ACK_MASK		BIT(0)
#define VIAI2C_CSR_READY_MASK		BIT(1)

/* REG_ISR Bit fields */
#define VIAI2C_REG_ISR		0x06
#define VIAI2C_ISR_NACK_ADDR		BIT(0)
#define VIAI2C_ISR_BYTE_END		BIT(1)
#define VIAI2C_ISR_SCL_TIMEOUT		BIT(2)
#define VIAI2C_ISR_MASK_ALL		GENMASK(2, 0)

/* REG_IMR Bit fields */
#define VIAI2C_REG_IMR		0x08
#define VIAI2C_IMR_BYTE			BIT(1)
#define VIAI2C_IMR_ENABLE_ALL		GENMASK(2, 0)

#define VIAI2C_REG_CDR		0x0A
#define VIAI2C_REG_TR		0x0C
#define VIAI2C_REG_MCR		0x0E

#define VIAI2C_TIMEOUT		(msecs_to_jiffies(1000))

enum {
	VIAI2C_PLAT_WMT,
	VIAI2C_PLAT_ZHAOXIN
};

enum {
	VIAI2C_BYTE_MODE,
	VIAI2C_FIFO_MODE
};

struct viai2c {
	struct i2c_adapter	adapter;
	struct completion	complete;
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	u16			tcr;
	int			irq;
	u16			xfered_len;
	struct i2c_msg		*msg;
	int			ret;
	bool			last;
	unsigned int		mode;
	unsigned int		platform;
	void			*pltfm_priv;
};

int viai2c_wait_bus_not_busy(struct viai2c *i2c);
int viai2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num);
int viai2c_init(struct platform_device *pdev, struct viai2c **pi2c, int plat);
int viai2c_fifo_irq_xfer(struct viai2c *i2c, bool irq);

#endif

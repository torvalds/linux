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

#define REG_CR		0x00
#define REG_TCR		0x02
#define REG_CSR		0x04
#define REG_ISR		0x06
#define REG_IMR		0x08
#define REG_CDR		0x0A
#define REG_TR		0x0C
#define REG_MCR		0x0E

/* REG_CR Bit fields */
#define CR_TX_NEXT_ACK		0x0000
#define CR_ENABLE		0x0001
#define CR_TX_NEXT_NO_ACK	0x0002
#define CR_TX_END		0x0004
#define CR_CPU_RDY		0x0008
#define SLAV_MODE_SEL		0x8000

/* REG_TCR Bit fields */
#define TCR_STANDARD_MODE	0x0000
#define TCR_MASTER_WRITE	0x0000
#define TCR_HS_MODE		0x2000
#define TCR_MASTER_READ		0x4000
#define TCR_FAST_MODE		0x8000
#define TCR_SLAVE_ADDR_MASK	0x007F

/* REG_ISR Bit fields */
#define ISR_NACK_ADDR		0x0001
#define ISR_BYTE_END		0x0002
#define ISR_SCL_TIMEOUT		0x0004
#define ISR_WRITE_ALL		0x0007

/* REG_IMR Bit fields */
#define IMR_ENABLE_ALL		0x0007

/* REG_CSR Bit fields */
#define CSR_RCV_NOT_ACK		0x0001
#define CSR_RCV_ACK_MASK	0x0001
#define CSR_READY_MASK		0x0002

#define WMT_I2C_TIMEOUT		(msecs_to_jiffies(1000))

struct wmt_i2c_dev {
	struct i2c_adapter	adapter;
	struct completion	complete;
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	u16			tcr;
	int			irq;
	u16			cmd_status;
};

int wmt_i2c_wait_bus_not_busy(struct wmt_i2c_dev *i2c_dev);
int wmt_check_status(struct wmt_i2c_dev *i2c_dev);
int wmt_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num);
int wmt_i2c_init(struct platform_device *pdev, struct wmt_i2c_dev **pi2c_dev);

#endif

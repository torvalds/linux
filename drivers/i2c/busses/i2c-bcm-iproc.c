/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define IDM_CTRL_DIRECT_OFFSET       0x00
#define CFG_OFFSET                   0x00
#define CFG_RESET_SHIFT              31
#define CFG_EN_SHIFT                 30
#define CFG_SLAVE_ADDR_0_SHIFT       28
#define CFG_M_RETRY_CNT_SHIFT        16
#define CFG_M_RETRY_CNT_MASK         0x0f

#define TIM_CFG_OFFSET               0x04
#define TIM_CFG_MODE_400_SHIFT       31
#define TIM_RAND_SLAVE_STRETCH_SHIFT      24
#define TIM_RAND_SLAVE_STRETCH_MASK       0x7f
#define TIM_PERIODIC_SLAVE_STRETCH_SHIFT  16
#define TIM_PERIODIC_SLAVE_STRETCH_MASK   0x7f

#define S_CFG_SMBUS_ADDR_OFFSET           0x08
#define S_CFG_EN_NIC_SMB_ADDR3_SHIFT      31
#define S_CFG_NIC_SMB_ADDR3_SHIFT         24
#define S_CFG_NIC_SMB_ADDR3_MASK          0x7f
#define S_CFG_EN_NIC_SMB_ADDR2_SHIFT      23
#define S_CFG_NIC_SMB_ADDR2_SHIFT         16
#define S_CFG_NIC_SMB_ADDR2_MASK          0x7f
#define S_CFG_EN_NIC_SMB_ADDR1_SHIFT      15
#define S_CFG_NIC_SMB_ADDR1_SHIFT         8
#define S_CFG_NIC_SMB_ADDR1_MASK          0x7f
#define S_CFG_EN_NIC_SMB_ADDR0_SHIFT      7
#define S_CFG_NIC_SMB_ADDR0_SHIFT         0
#define S_CFG_NIC_SMB_ADDR0_MASK          0x7f

#define M_FIFO_CTRL_OFFSET           0x0c
#define M_FIFO_RX_FLUSH_SHIFT        31
#define M_FIFO_TX_FLUSH_SHIFT        30
#define M_FIFO_RX_CNT_SHIFT          16
#define M_FIFO_RX_CNT_MASK           0x7f
#define M_FIFO_RX_THLD_SHIFT         8
#define M_FIFO_RX_THLD_MASK          0x3f

#define S_FIFO_CTRL_OFFSET           0x10
#define S_FIFO_RX_FLUSH_SHIFT        31
#define S_FIFO_TX_FLUSH_SHIFT        30
#define S_FIFO_RX_CNT_SHIFT          16
#define S_FIFO_RX_CNT_MASK           0x7f
#define S_FIFO_RX_THLD_SHIFT         8
#define S_FIFO_RX_THLD_MASK          0x3f

#define M_CMD_OFFSET                 0x30
#define M_CMD_START_BUSY_SHIFT       31
#define M_CMD_STATUS_SHIFT           25
#define M_CMD_STATUS_MASK            0x07
#define M_CMD_STATUS_SUCCESS         0x0
#define M_CMD_STATUS_LOST_ARB        0x1
#define M_CMD_STATUS_NACK_ADDR       0x2
#define M_CMD_STATUS_NACK_DATA       0x3
#define M_CMD_STATUS_TIMEOUT         0x4
#define M_CMD_STATUS_FIFO_UNDERRUN   0x5
#define M_CMD_STATUS_RX_FIFO_FULL    0x6
#define M_CMD_PROTOCOL_SHIFT         9
#define M_CMD_PROTOCOL_MASK          0xf
#define M_CMD_PROTOCOL_BLK_WR        0x7
#define M_CMD_PROTOCOL_BLK_RD        0x8
#define M_CMD_PEC_SHIFT              8
#define M_CMD_RD_CNT_SHIFT           0
#define M_CMD_RD_CNT_MASK            0xff

#define S_CMD_OFFSET                 0x34
#define S_CMD_START_BUSY_SHIFT       31
#define S_CMD_STATUS_SHIFT           23
#define S_CMD_STATUS_MASK            0x07
#define S_CMD_STATUS_SUCCESS         0x0
#define S_CMD_STATUS_TIMEOUT         0x5

#define IE_OFFSET                    0x38
#define IE_M_RX_FIFO_FULL_SHIFT      31
#define IE_M_RX_THLD_SHIFT           30
#define IE_M_START_BUSY_SHIFT        28
#define IE_M_TX_UNDERRUN_SHIFT       27
#define IE_S_RX_FIFO_FULL_SHIFT      26
#define IE_S_RX_THLD_SHIFT           25
#define IE_S_RX_EVENT_SHIFT          24
#define IE_S_START_BUSY_SHIFT        23
#define IE_S_TX_UNDERRUN_SHIFT       22
#define IE_S_RD_EVENT_SHIFT          21

#define IS_OFFSET                    0x3c
#define IS_M_RX_FIFO_FULL_SHIFT      31
#define IS_M_RX_THLD_SHIFT           30
#define IS_M_START_BUSY_SHIFT        28
#define IS_M_TX_UNDERRUN_SHIFT       27
#define IS_S_RX_FIFO_FULL_SHIFT      26
#define IS_S_RX_THLD_SHIFT           25
#define IS_S_RX_EVENT_SHIFT          24
#define IS_S_START_BUSY_SHIFT        23
#define IS_S_TX_UNDERRUN_SHIFT       22
#define IS_S_RD_EVENT_SHIFT          21

#define M_TX_OFFSET                  0x40
#define M_TX_WR_STATUS_SHIFT         31
#define M_TX_DATA_SHIFT              0
#define M_TX_DATA_MASK               0xff

#define M_RX_OFFSET                  0x44
#define M_RX_STATUS_SHIFT            30
#define M_RX_STATUS_MASK             0x03
#define M_RX_PEC_ERR_SHIFT           29
#define M_RX_DATA_SHIFT              0
#define M_RX_DATA_MASK               0xff

#define S_TX_OFFSET                  0x48
#define S_TX_WR_STATUS_SHIFT         31
#define S_TX_DATA_SHIFT              0
#define S_TX_DATA_MASK               0xff

#define S_RX_OFFSET                  0x4c
#define S_RX_STATUS_SHIFT            30
#define S_RX_STATUS_MASK             0x03
#define S_RX_PEC_ERR_SHIFT           29
#define S_RX_DATA_SHIFT              0
#define S_RX_DATA_MASK               0xff

#define I2C_TIMEOUT_MSEC             50000
#define M_TX_RX_FIFO_SIZE            64
#define M_RX_FIFO_MAX_THLD_VALUE     (M_TX_RX_FIFO_SIZE - 1)

#define M_RX_MAX_READ_LEN            255
#define M_RX_FIFO_THLD_VALUE         50

#define IE_M_ALL_INTERRUPT_SHIFT     27
#define IE_M_ALL_INTERRUPT_MASK      0x1e

#define SLAVE_READ_WRITE_BIT_MASK    0x1
#define SLAVE_READ_WRITE_BIT_SHIFT   0x1
#define SLAVE_MAX_SIZE_TRANSACTION   64
#define SLAVE_CLOCK_STRETCH_TIME     25

#define IE_S_ALL_INTERRUPT_SHIFT     21
#define IE_S_ALL_INTERRUPT_MASK      0x3f

enum i2c_slave_read_status {
	I2C_SLAVE_RX_FIFO_EMPTY = 0,
	I2C_SLAVE_RX_START,
	I2C_SLAVE_RX_DATA,
	I2C_SLAVE_RX_END,
};

enum bus_speed_index {
	I2C_SPD_100K = 0,
	I2C_SPD_400K,
};

enum bcm_iproc_i2c_type {
	IPROC_I2C,
	IPROC_I2C_NIC
};

struct bcm_iproc_i2c_dev {
	struct device *device;
	enum bcm_iproc_i2c_type type;
	int irq;

	void __iomem *base;
	void __iomem *idm_base;

	u32 ape_addr_mask;

	/* lock for indirect access through IDM */
	spinlock_t idm_lock;

	struct i2c_adapter adapter;
	unsigned int bus_speed;

	struct completion done;
	int xfer_is_done;

	struct i2c_msg *msg;

	struct i2c_client *slave;

	/* bytes that have been transferred */
	unsigned int tx_bytes;
	/* bytes that have been read */
	unsigned int rx_bytes;
	unsigned int thld_bytes;
};

/*
 * Can be expanded in the future if more interrupt status bits are utilized
 */
#define ISR_MASK (BIT(IS_M_START_BUSY_SHIFT) | BIT(IS_M_TX_UNDERRUN_SHIFT)\
		| BIT(IS_M_RX_THLD_SHIFT))

#define ISR_MASK_SLAVE (BIT(IS_S_START_BUSY_SHIFT)\
		| BIT(IS_S_RX_EVENT_SHIFT) | BIT(IS_S_RD_EVENT_SHIFT)\
		| BIT(IS_S_TX_UNDERRUN_SHIFT))

static int bcm_iproc_i2c_reg_slave(struct i2c_client *slave);
static int bcm_iproc_i2c_unreg_slave(struct i2c_client *slave);
static void bcm_iproc_i2c_enable_disable(struct bcm_iproc_i2c_dev *iproc_i2c,
					 bool enable);

static inline u32 iproc_i2c_rd_reg(struct bcm_iproc_i2c_dev *iproc_i2c,
				   u32 offset)
{
	u32 val;

	if (iproc_i2c->idm_base) {
		spin_lock(&iproc_i2c->idm_lock);
		writel(iproc_i2c->ape_addr_mask,
		       iproc_i2c->idm_base + IDM_CTRL_DIRECT_OFFSET);
		val = readl(iproc_i2c->base + offset);
		spin_unlock(&iproc_i2c->idm_lock);
	} else {
		val = readl(iproc_i2c->base + offset);
	}

	return val;
}

static inline void iproc_i2c_wr_reg(struct bcm_iproc_i2c_dev *iproc_i2c,
				    u32 offset, u32 val)
{
	if (iproc_i2c->idm_base) {
		spin_lock(&iproc_i2c->idm_lock);
		writel(iproc_i2c->ape_addr_mask,
		       iproc_i2c->idm_base + IDM_CTRL_DIRECT_OFFSET);
		writel(val, iproc_i2c->base + offset);
		spin_unlock(&iproc_i2c->idm_lock);
	} else {
		writel(val, iproc_i2c->base + offset);
	}
}

static void bcm_iproc_i2c_slave_init(
	struct bcm_iproc_i2c_dev *iproc_i2c, bool need_reset)
{
	u32 val;

	if (need_reset) {
		/* put controller in reset */
		val = iproc_i2c_rd_reg(iproc_i2c, CFG_OFFSET);
		val |= BIT(CFG_RESET_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, CFG_OFFSET, val);

		/* wait 100 usec per spec */
		udelay(100);

		/* bring controller out of reset */
		val &= ~(BIT(CFG_RESET_SHIFT));
		iproc_i2c_wr_reg(iproc_i2c, CFG_OFFSET, val);
	}

	/* flush TX/RX FIFOs */
	val = (BIT(S_FIFO_RX_FLUSH_SHIFT) | BIT(S_FIFO_TX_FLUSH_SHIFT));
	iproc_i2c_wr_reg(iproc_i2c, S_FIFO_CTRL_OFFSET, val);

	/* Maximum slave stretch time */
	val = iproc_i2c_rd_reg(iproc_i2c, TIM_CFG_OFFSET);
	val &= ~(TIM_RAND_SLAVE_STRETCH_MASK << TIM_RAND_SLAVE_STRETCH_SHIFT);
	val |= (SLAVE_CLOCK_STRETCH_TIME << TIM_RAND_SLAVE_STRETCH_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, TIM_CFG_OFFSET, val);

	/* Configure the slave address */
	val = iproc_i2c_rd_reg(iproc_i2c, S_CFG_SMBUS_ADDR_OFFSET);
	val |= BIT(S_CFG_EN_NIC_SMB_ADDR3_SHIFT);
	val &= ~(S_CFG_NIC_SMB_ADDR3_MASK << S_CFG_NIC_SMB_ADDR3_SHIFT);
	val |= (iproc_i2c->slave->addr << S_CFG_NIC_SMB_ADDR3_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, S_CFG_SMBUS_ADDR_OFFSET, val);

	/* clear all pending slave interrupts */
	iproc_i2c_wr_reg(iproc_i2c, IS_OFFSET, ISR_MASK_SLAVE);

	/* Enable interrupt register to indicate a valid byte in receive fifo */
	val = BIT(IE_S_RX_EVENT_SHIFT);
	/* Enable interrupt register for the Slave BUSY command */
	val |= BIT(IE_S_START_BUSY_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val);
}

static void bcm_iproc_i2c_check_slave_status(
	struct bcm_iproc_i2c_dev *iproc_i2c)
{
	u32 val;

	val = iproc_i2c_rd_reg(iproc_i2c, S_CMD_OFFSET);
	/* status is valid only when START_BUSY is cleared after it was set */
	if (val & BIT(S_CMD_START_BUSY_SHIFT))
		return;

	val = (val >> S_CMD_STATUS_SHIFT) & S_CMD_STATUS_MASK;
	if (val == S_CMD_STATUS_TIMEOUT) {
		dev_err(iproc_i2c->device, "slave random stretch time timeout\n");

		/* re-initialize i2c for recovery */
		bcm_iproc_i2c_enable_disable(iproc_i2c, false);
		bcm_iproc_i2c_slave_init(iproc_i2c, true);
		bcm_iproc_i2c_enable_disable(iproc_i2c, true);
	}
}

static bool bcm_iproc_i2c_slave_isr(struct bcm_iproc_i2c_dev *iproc_i2c,
				    u32 status)
{
	u32 val;
	u8 value, rx_status;

	/* Slave RX byte receive */
	if (status & BIT(IS_S_RX_EVENT_SHIFT)) {
		val = iproc_i2c_rd_reg(iproc_i2c, S_RX_OFFSET);
		rx_status = (val >> S_RX_STATUS_SHIFT) & S_RX_STATUS_MASK;
		if (rx_status == I2C_SLAVE_RX_START) {
			/* Start of SMBUS for Master write */
			i2c_slave_event(iproc_i2c->slave,
					I2C_SLAVE_WRITE_REQUESTED, &value);

			val = iproc_i2c_rd_reg(iproc_i2c, S_RX_OFFSET);
			value = (u8)((val >> S_RX_DATA_SHIFT) & S_RX_DATA_MASK);
			i2c_slave_event(iproc_i2c->slave,
					I2C_SLAVE_WRITE_RECEIVED, &value);
		} else if (status & BIT(IS_S_RD_EVENT_SHIFT)) {
			/* Start of SMBUS for Master Read */
			i2c_slave_event(iproc_i2c->slave,
					I2C_SLAVE_READ_REQUESTED, &value);
			iproc_i2c_wr_reg(iproc_i2c, S_TX_OFFSET, value);

			val = BIT(S_CMD_START_BUSY_SHIFT);
			iproc_i2c_wr_reg(iproc_i2c, S_CMD_OFFSET, val);

			/*
			 * Enable interrupt for TX FIFO becomes empty and
			 * less than PKT_LENGTH bytes were output on the SMBUS
			 */
			val = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
			val |= BIT(IE_S_TX_UNDERRUN_SHIFT);
			iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val);
		} else {
			/* Master write other than start */
			value = (u8)((val >> S_RX_DATA_SHIFT) & S_RX_DATA_MASK);
			i2c_slave_event(iproc_i2c->slave,
					I2C_SLAVE_WRITE_RECEIVED, &value);
		}
	} else if (status & BIT(IS_S_TX_UNDERRUN_SHIFT)) {
		/* Master read other than start */
		i2c_slave_event(iproc_i2c->slave,
				I2C_SLAVE_READ_PROCESSED, &value);

		iproc_i2c_wr_reg(iproc_i2c, S_TX_OFFSET, value);
		val = BIT(S_CMD_START_BUSY_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, S_CMD_OFFSET, val);
	}

	/* Stop */
	if (status & BIT(IS_S_START_BUSY_SHIFT)) {
		i2c_slave_event(iproc_i2c->slave, I2C_SLAVE_STOP, &value);
		/*
		 * Enable interrupt for TX FIFO becomes empty and
		 * less than PKT_LENGTH bytes were output on the SMBUS
		 */
		val = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
		val &= ~BIT(IE_S_TX_UNDERRUN_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val);
	}

	/* clear interrupt status */
	iproc_i2c_wr_reg(iproc_i2c, IS_OFFSET, status);

	bcm_iproc_i2c_check_slave_status(iproc_i2c);
	return true;
}

static void bcm_iproc_i2c_read_valid_bytes(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	struct i2c_msg *msg = iproc_i2c->msg;
	uint32_t val;

	/* Read valid data from RX FIFO */
	while (iproc_i2c->rx_bytes < msg->len) {
		val = iproc_i2c_rd_reg(iproc_i2c, M_RX_OFFSET);

		/* rx fifo empty */
		if (!((val >> M_RX_STATUS_SHIFT) & M_RX_STATUS_MASK))
			break;

		msg->buf[iproc_i2c->rx_bytes] =
			(val >> M_RX_DATA_SHIFT) & M_RX_DATA_MASK;
		iproc_i2c->rx_bytes++;
	}
}

static void bcm_iproc_i2c_send(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	struct i2c_msg *msg = iproc_i2c->msg;
	unsigned int tx_bytes = msg->len - iproc_i2c->tx_bytes;
	unsigned int i;
	u32 val;

	/* can only fill up to the FIFO size */
	tx_bytes = min_t(unsigned int, tx_bytes, M_TX_RX_FIFO_SIZE);
	for (i = 0; i < tx_bytes; i++) {
		/* start from where we left over */
		unsigned int idx = iproc_i2c->tx_bytes + i;

		val = msg->buf[idx];

		/* mark the last byte */
		if (idx == msg->len - 1) {
			val |= BIT(M_TX_WR_STATUS_SHIFT);

			if (iproc_i2c->irq) {
				u32 tmp;

				/*
				 * Since this is the last byte, we should now
				 * disable TX FIFO underrun interrupt
				 */
				tmp = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
				tmp &= ~BIT(IE_M_TX_UNDERRUN_SHIFT);
				iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET,
						 tmp);
			}
		}

		/* load data into TX FIFO */
		iproc_i2c_wr_reg(iproc_i2c, M_TX_OFFSET, val);
	}

	/* update number of transferred bytes */
	iproc_i2c->tx_bytes += tx_bytes;
}

static void bcm_iproc_i2c_read(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	struct i2c_msg *msg = iproc_i2c->msg;
	u32 bytes_left, val;

	bcm_iproc_i2c_read_valid_bytes(iproc_i2c);
	bytes_left = msg->len - iproc_i2c->rx_bytes;
	if (bytes_left == 0) {
		if (iproc_i2c->irq) {
			/* finished reading all data, disable rx thld event */
			val = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
			val &= ~BIT(IS_M_RX_THLD_SHIFT);
			iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val);
		}
	} else if (bytes_left < iproc_i2c->thld_bytes) {
		/* set bytes left as threshold */
		val = iproc_i2c_rd_reg(iproc_i2c, M_FIFO_CTRL_OFFSET);
		val &= ~(M_FIFO_RX_THLD_MASK << M_FIFO_RX_THLD_SHIFT);
		val |= (bytes_left << M_FIFO_RX_THLD_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, M_FIFO_CTRL_OFFSET, val);
		iproc_i2c->thld_bytes = bytes_left;
	}
	/*
	 * bytes_left >= iproc_i2c->thld_bytes,
	 * hence no need to change the THRESHOLD SET.
	 * It will remain as iproc_i2c->thld_bytes itself
	 */
}

static void bcm_iproc_i2c_process_m_event(struct bcm_iproc_i2c_dev *iproc_i2c,
					  u32 status)
{
	/* TX FIFO is empty and we have more data to send */
	if (status & BIT(IS_M_TX_UNDERRUN_SHIFT))
		bcm_iproc_i2c_send(iproc_i2c);

	/* RX FIFO threshold is reached and data needs to be read out */
	if (status & BIT(IS_M_RX_THLD_SHIFT))
		bcm_iproc_i2c_read(iproc_i2c);

	/* transfer is done */
	if (status & BIT(IS_M_START_BUSY_SHIFT)) {
		iproc_i2c->xfer_is_done = 1;
		if (iproc_i2c->irq)
			complete(&iproc_i2c->done);
	}
}

static irqreturn_t bcm_iproc_i2c_isr(int irq, void *data)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = data;
	u32 status = iproc_i2c_rd_reg(iproc_i2c, IS_OFFSET);
	bool ret;
	u32 sl_status = status & ISR_MASK_SLAVE;

	if (sl_status) {
		ret = bcm_iproc_i2c_slave_isr(iproc_i2c, sl_status);
		if (ret)
			return IRQ_HANDLED;
		else
			return IRQ_NONE;
	}

	status &= ISR_MASK;
	if (!status)
		return IRQ_NONE;

	/* process all master based events */
	bcm_iproc_i2c_process_m_event(iproc_i2c, status);
	iproc_i2c_wr_reg(iproc_i2c, IS_OFFSET, status);

	return IRQ_HANDLED;
}

static int bcm_iproc_i2c_init(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	u32 val;

	/* put controller in reset */
	val = iproc_i2c_rd_reg(iproc_i2c, CFG_OFFSET);
	val |= BIT(CFG_RESET_SHIFT);
	val &= ~(BIT(CFG_EN_SHIFT));
	iproc_i2c_wr_reg(iproc_i2c, CFG_OFFSET, val);

	/* wait 100 usec per spec */
	udelay(100);

	/* bring controller out of reset */
	val &= ~(BIT(CFG_RESET_SHIFT));
	iproc_i2c_wr_reg(iproc_i2c, CFG_OFFSET, val);

	/* flush TX/RX FIFOs and set RX FIFO threshold to zero */
	val = (BIT(M_FIFO_RX_FLUSH_SHIFT) | BIT(M_FIFO_TX_FLUSH_SHIFT));
	iproc_i2c_wr_reg(iproc_i2c, M_FIFO_CTRL_OFFSET, val);
	/* disable all interrupts */
	val = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
	val &= ~(IE_M_ALL_INTERRUPT_MASK <<
			IE_M_ALL_INTERRUPT_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val);

	/* clear all pending interrupts */
	iproc_i2c_wr_reg(iproc_i2c, IS_OFFSET, 0xffffffff);

	return 0;
}

static void bcm_iproc_i2c_enable_disable(struct bcm_iproc_i2c_dev *iproc_i2c,
					 bool enable)
{
	u32 val;

	val = iproc_i2c_rd_reg(iproc_i2c, CFG_OFFSET);
	if (enable)
		val |= BIT(CFG_EN_SHIFT);
	else
		val &= ~BIT(CFG_EN_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, CFG_OFFSET, val);
}

static int bcm_iproc_i2c_check_status(struct bcm_iproc_i2c_dev *iproc_i2c,
				      struct i2c_msg *msg)
{
	u32 val;

	val = iproc_i2c_rd_reg(iproc_i2c, M_CMD_OFFSET);
	val = (val >> M_CMD_STATUS_SHIFT) & M_CMD_STATUS_MASK;

	switch (val) {
	case M_CMD_STATUS_SUCCESS:
		return 0;

	case M_CMD_STATUS_LOST_ARB:
		dev_dbg(iproc_i2c->device, "lost bus arbitration\n");
		return -EAGAIN;

	case M_CMD_STATUS_NACK_ADDR:
		dev_dbg(iproc_i2c->device, "NAK addr:0x%02x\n", msg->addr);
		return -ENXIO;

	case M_CMD_STATUS_NACK_DATA:
		dev_dbg(iproc_i2c->device, "NAK data\n");
		return -ENXIO;

	case M_CMD_STATUS_TIMEOUT:
		dev_dbg(iproc_i2c->device, "bus timeout\n");
		return -ETIMEDOUT;

	case M_CMD_STATUS_FIFO_UNDERRUN:
		dev_dbg(iproc_i2c->device, "FIFO under-run\n");
		return -ENXIO;

	case M_CMD_STATUS_RX_FIFO_FULL:
		dev_dbg(iproc_i2c->device, "RX FIFO full\n");
		return -ETIMEDOUT;

	default:
		dev_dbg(iproc_i2c->device, "unknown error code=%d\n", val);

		/* re-initialize i2c for recovery */
		bcm_iproc_i2c_enable_disable(iproc_i2c, false);
		bcm_iproc_i2c_init(iproc_i2c);
		bcm_iproc_i2c_enable_disable(iproc_i2c, true);

		return -EIO;
	}
}

static int bcm_iproc_i2c_xfer_wait(struct bcm_iproc_i2c_dev *iproc_i2c,
				   struct i2c_msg *msg,
				   u32 cmd)
{
	unsigned long time_left = msecs_to_jiffies(I2C_TIMEOUT_MSEC);
	u32 val, status;
	int ret;

	iproc_i2c_wr_reg(iproc_i2c, M_CMD_OFFSET, cmd);

	if (iproc_i2c->irq) {
		time_left = wait_for_completion_timeout(&iproc_i2c->done,
							time_left);
		/* disable all interrupts */
		iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, 0);
		/* read it back to flush the write */
		iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
		/* make sure the interrupt handler isn't running */
		synchronize_irq(iproc_i2c->irq);

	} else { /* polling mode */
		unsigned long timeout = jiffies + time_left;

		do {
			status = iproc_i2c_rd_reg(iproc_i2c,
						  IS_OFFSET) & ISR_MASK;
			bcm_iproc_i2c_process_m_event(iproc_i2c, status);
			iproc_i2c_wr_reg(iproc_i2c, IS_OFFSET, status);

			if (time_after(jiffies, timeout)) {
				time_left = 0;
				break;
			}

			cpu_relax();
			cond_resched();
		} while (!iproc_i2c->xfer_is_done);
	}

	if (!time_left && !iproc_i2c->xfer_is_done) {
		dev_err(iproc_i2c->device, "transaction timed out\n");

		/* flush both TX/RX FIFOs */
		val = BIT(M_FIFO_RX_FLUSH_SHIFT) | BIT(M_FIFO_TX_FLUSH_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, M_FIFO_CTRL_OFFSET, val);
		return -ETIMEDOUT;
	}

	ret = bcm_iproc_i2c_check_status(iproc_i2c, msg);
	if (ret) {
		/* flush both TX/RX FIFOs */
		val = BIT(M_FIFO_RX_FLUSH_SHIFT) | BIT(M_FIFO_TX_FLUSH_SHIFT);
		iproc_i2c_wr_reg(iproc_i2c, M_FIFO_CTRL_OFFSET, val);
		return ret;
	}

	return 0;
}

static int bcm_iproc_i2c_xfer_single_msg(struct bcm_iproc_i2c_dev *iproc_i2c,
					 struct i2c_msg *msg)
{
	int i;
	u8 addr;
	u32 val, tmp, val_intr_en;
	unsigned int tx_bytes;

	/* check if bus is busy */
	if (!!(iproc_i2c_rd_reg(iproc_i2c,
				M_CMD_OFFSET) & BIT(M_CMD_START_BUSY_SHIFT))) {
		dev_warn(iproc_i2c->device, "bus is busy\n");
		return -EBUSY;
	}

	iproc_i2c->msg = msg;

	/* format and load slave address into the TX FIFO */
	addr = i2c_8bit_addr_from_msg(msg);
	iproc_i2c_wr_reg(iproc_i2c, M_TX_OFFSET, addr);

	/*
	 * For a write transaction, load data into the TX FIFO. Only allow
	 * loading up to TX FIFO size - 1 bytes of data since the first byte
	 * has been used up by the slave address
	 */
	tx_bytes = min_t(unsigned int, msg->len, M_TX_RX_FIFO_SIZE - 1);
	if (!(msg->flags & I2C_M_RD)) {
		for (i = 0; i < tx_bytes; i++) {
			val = msg->buf[i];

			/* mark the last byte */
			if (i == msg->len - 1)
				val |= BIT(M_TX_WR_STATUS_SHIFT);

			iproc_i2c_wr_reg(iproc_i2c, M_TX_OFFSET, val);
		}
		iproc_i2c->tx_bytes = tx_bytes;
	}

	/* mark as incomplete before starting the transaction */
	if (iproc_i2c->irq)
		reinit_completion(&iproc_i2c->done);

	iproc_i2c->xfer_is_done = 0;

	/*
	 * Enable the "start busy" interrupt, which will be triggered after the
	 * transaction is done, i.e., the internal start_busy bit, transitions
	 * from 1 to 0.
	 */
	val_intr_en = BIT(IE_M_START_BUSY_SHIFT);

	/*
	 * If TX data size is larger than the TX FIFO, need to enable TX
	 * underrun interrupt, which will be triggerred when the TX FIFO is
	 * empty. When that happens we can then pump more data into the FIFO
	 */
	if (!(msg->flags & I2C_M_RD) &&
	    msg->len > iproc_i2c->tx_bytes)
		val_intr_en |= BIT(IE_M_TX_UNDERRUN_SHIFT);

	/*
	 * Now we can activate the transfer. For a read operation, specify the
	 * number of bytes to read
	 */
	val = BIT(M_CMD_START_BUSY_SHIFT);
	if (msg->flags & I2C_M_RD) {
		iproc_i2c->rx_bytes = 0;
		if (msg->len > M_RX_FIFO_MAX_THLD_VALUE)
			iproc_i2c->thld_bytes = M_RX_FIFO_THLD_VALUE;
		else
			iproc_i2c->thld_bytes = msg->len;

		/* set threshold value */
		tmp = iproc_i2c_rd_reg(iproc_i2c, M_FIFO_CTRL_OFFSET);
		tmp &= ~(M_FIFO_RX_THLD_MASK << M_FIFO_RX_THLD_SHIFT);
		tmp |= iproc_i2c->thld_bytes << M_FIFO_RX_THLD_SHIFT;
		iproc_i2c_wr_reg(iproc_i2c, M_FIFO_CTRL_OFFSET, tmp);

		/* enable the RX threshold interrupt */
		val_intr_en |= BIT(IE_M_RX_THLD_SHIFT);

		val |= (M_CMD_PROTOCOL_BLK_RD << M_CMD_PROTOCOL_SHIFT) |
		       (msg->len << M_CMD_RD_CNT_SHIFT);
	} else {
		val |= (M_CMD_PROTOCOL_BLK_WR << M_CMD_PROTOCOL_SHIFT);
	}

	if (iproc_i2c->irq)
		iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, val_intr_en);

	return bcm_iproc_i2c_xfer_wait(iproc_i2c, msg, val);
}

static int bcm_iproc_i2c_xfer(struct i2c_adapter *adapter,
			      struct i2c_msg msgs[], int num)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = i2c_get_adapdata(adapter);
	int ret, i;

	/* go through all messages */
	for (i = 0; i < num; i++) {
		ret = bcm_iproc_i2c_xfer_single_msg(iproc_i2c, &msgs[i]);
		if (ret) {
			dev_dbg(iproc_i2c->device, "xfer failed\n");
			return ret;
		}
	}

	return num;
}

static uint32_t bcm_iproc_i2c_functionality(struct i2c_adapter *adap)
{
	u32 val;

	/* We do not support the SMBUS Quick command */
	val = I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);

	if (adap->algo->reg_slave)
		val |= I2C_FUNC_SLAVE;

	return val;
}

static struct i2c_algorithm bcm_iproc_algo = {
	.master_xfer = bcm_iproc_i2c_xfer,
	.functionality = bcm_iproc_i2c_functionality,
	.reg_slave = bcm_iproc_i2c_reg_slave,
	.unreg_slave = bcm_iproc_i2c_unreg_slave,
};

static const struct i2c_adapter_quirks bcm_iproc_i2c_quirks = {
	.max_read_len = M_RX_MAX_READ_LEN,
};

static int bcm_iproc_i2c_cfg_speed(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	unsigned int bus_speed;
	u32 val;
	int ret = of_property_read_u32(iproc_i2c->device->of_node,
				       "clock-frequency", &bus_speed);
	if (ret < 0) {
		dev_info(iproc_i2c->device,
			"unable to interpret clock-frequency DT property\n");
		bus_speed = 100000;
	}

	if (bus_speed < 100000) {
		dev_err(iproc_i2c->device, "%d Hz bus speed not supported\n",
			bus_speed);
		dev_err(iproc_i2c->device,
			"valid speeds are 100khz and 400khz\n");
		return -EINVAL;
	} else if (bus_speed < 400000) {
		bus_speed = 100000;
	} else {
		bus_speed = 400000;
	}

	iproc_i2c->bus_speed = bus_speed;
	val = iproc_i2c_rd_reg(iproc_i2c, TIM_CFG_OFFSET);
	val &= ~BIT(TIM_CFG_MODE_400_SHIFT);
	val |= (bus_speed == 400000) << TIM_CFG_MODE_400_SHIFT;
	iproc_i2c_wr_reg(iproc_i2c, TIM_CFG_OFFSET, val);

	dev_info(iproc_i2c->device, "bus set to %u Hz\n", bus_speed);

	return 0;
}

static int bcm_iproc_i2c_probe(struct platform_device *pdev)
{
	int irq, ret = 0;
	struct bcm_iproc_i2c_dev *iproc_i2c;
	struct i2c_adapter *adap;
	struct resource *res;

	iproc_i2c = devm_kzalloc(&pdev->dev, sizeof(*iproc_i2c),
				 GFP_KERNEL);
	if (!iproc_i2c)
		return -ENOMEM;

	platform_set_drvdata(pdev, iproc_i2c);
	iproc_i2c->device = &pdev->dev;
	iproc_i2c->type =
		(enum bcm_iproc_i2c_type)of_device_get_match_data(&pdev->dev);
	init_completion(&iproc_i2c->done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iproc_i2c->base = devm_ioremap_resource(iproc_i2c->device, res);
	if (IS_ERR(iproc_i2c->base))
		return PTR_ERR(iproc_i2c->base);

	if (iproc_i2c->type == IPROC_I2C_NIC) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		iproc_i2c->idm_base = devm_ioremap_resource(iproc_i2c->device,
							    res);
		if (IS_ERR(iproc_i2c->idm_base))
			return PTR_ERR(iproc_i2c->idm_base);

		ret = of_property_read_u32(iproc_i2c->device->of_node,
					   "brcm,ape-hsls-addr-mask",
					   &iproc_i2c->ape_addr_mask);
		if (ret < 0) {
			dev_err(iproc_i2c->device,
				"'brcm,ape-hsls-addr-mask' missing\n");
			return -EINVAL;
		}

		spin_lock_init(&iproc_i2c->idm_lock);

		/* no slave support */
		bcm_iproc_algo.reg_slave = NULL;
		bcm_iproc_algo.unreg_slave = NULL;
	}

	ret = bcm_iproc_i2c_init(iproc_i2c);
	if (ret)
		return ret;

	ret = bcm_iproc_i2c_cfg_speed(iproc_i2c);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		ret = devm_request_irq(iproc_i2c->device, irq,
				       bcm_iproc_i2c_isr, 0, pdev->name,
				       iproc_i2c);
		if (ret < 0) {
			dev_err(iproc_i2c->device,
				"unable to request irq %i\n", irq);
			return ret;
		}

		iproc_i2c->irq = irq;
	} else {
		dev_warn(iproc_i2c->device,
			 "no irq resource, falling back to poll mode\n");
	}

	bcm_iproc_i2c_enable_disable(iproc_i2c, true);

	adap = &iproc_i2c->adapter;
	i2c_set_adapdata(adap, iproc_i2c);
	snprintf(adap->name, sizeof(adap->name),
		"Broadcom iProc (%s)",
		of_node_full_name(iproc_i2c->device->of_node));
	adap->algo = &bcm_iproc_algo;
	adap->quirks = &bcm_iproc_i2c_quirks;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	return i2c_add_adapter(adap);
}

static int bcm_iproc_i2c_remove(struct platform_device *pdev)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = platform_get_drvdata(pdev);

	if (iproc_i2c->irq) {
		/*
		 * Make sure there's no pending interrupt when we remove the
		 * adapter
		 */
		iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, 0);
		iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
		synchronize_irq(iproc_i2c->irq);
	}

	i2c_del_adapter(&iproc_i2c->adapter);
	bcm_iproc_i2c_enable_disable(iproc_i2c, false);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int bcm_iproc_i2c_suspend(struct device *dev)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = dev_get_drvdata(dev);

	if (iproc_i2c->irq) {
		/*
		 * Make sure there's no pending interrupt when we go into
		 * suspend
		 */
		iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, 0);
		iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
		synchronize_irq(iproc_i2c->irq);
	}

	/* now disable the controller */
	bcm_iproc_i2c_enable_disable(iproc_i2c, false);

	return 0;
}

static int bcm_iproc_i2c_resume(struct device *dev)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = dev_get_drvdata(dev);
	int ret;
	u32 val;

	/*
	 * Power domain could have been shut off completely in system deep
	 * sleep, so re-initialize the block here
	 */
	ret = bcm_iproc_i2c_init(iproc_i2c);
	if (ret)
		return ret;

	/* configure to the desired bus speed */
	val = iproc_i2c_rd_reg(iproc_i2c, TIM_CFG_OFFSET);
	val &= ~BIT(TIM_CFG_MODE_400_SHIFT);
	val |= (iproc_i2c->bus_speed == 400000) << TIM_CFG_MODE_400_SHIFT;
	iproc_i2c_wr_reg(iproc_i2c, TIM_CFG_OFFSET, val);

	bcm_iproc_i2c_enable_disable(iproc_i2c, true);

	return 0;
}

static const struct dev_pm_ops bcm_iproc_i2c_pm_ops = {
	.suspend_late = &bcm_iproc_i2c_suspend,
	.resume_early = &bcm_iproc_i2c_resume
};

#define BCM_IPROC_I2C_PM_OPS (&bcm_iproc_i2c_pm_ops)
#else
#define BCM_IPROC_I2C_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */


static int bcm_iproc_i2c_reg_slave(struct i2c_client *slave)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = i2c_get_adapdata(slave->adapter);

	if (iproc_i2c->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	iproc_i2c->slave = slave;
	bcm_iproc_i2c_slave_init(iproc_i2c, false);
	return 0;
}

static int bcm_iproc_i2c_unreg_slave(struct i2c_client *slave)
{
	u32 tmp;
	struct bcm_iproc_i2c_dev *iproc_i2c = i2c_get_adapdata(slave->adapter);

	if (!iproc_i2c->slave)
		return -EINVAL;

	iproc_i2c->slave = NULL;

	/* disable all slave interrupts */
	tmp = iproc_i2c_rd_reg(iproc_i2c, IE_OFFSET);
	tmp &= ~(IE_S_ALL_INTERRUPT_MASK <<
			IE_S_ALL_INTERRUPT_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, IE_OFFSET, tmp);

	/* Erase the slave address programmed */
	tmp = iproc_i2c_rd_reg(iproc_i2c, S_CFG_SMBUS_ADDR_OFFSET);
	tmp &= ~BIT(S_CFG_EN_NIC_SMB_ADDR3_SHIFT);
	iproc_i2c_wr_reg(iproc_i2c, S_CFG_SMBUS_ADDR_OFFSET, tmp);

	return 0;
}

static const struct of_device_id bcm_iproc_i2c_of_match[] = {
	{
		.compatible = "brcm,iproc-i2c",
		.data = (int *)IPROC_I2C,
	}, {
		.compatible = "brcm,iproc-nic-i2c",
		.data = (int *)IPROC_I2C_NIC,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm_iproc_i2c_of_match);

static struct platform_driver bcm_iproc_i2c_driver = {
	.driver = {
		.name = "bcm-iproc-i2c",
		.of_match_table = bcm_iproc_i2c_of_match,
		.pm = BCM_IPROC_I2C_PM_OPS,
	},
	.probe = bcm_iproc_i2c_probe,
	.remove = bcm_iproc_i2c_remove,
};
module_platform_driver(bcm_iproc_i2c_driver);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom iProc I2C Driver");
MODULE_LICENSE("GPL v2");

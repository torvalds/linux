/*
 * i2c-xiic.c
 * Copyright (c) 2002-2007 Xilinx Inc.
 * Copyright (c) 2009-2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * This code was implemented by Mocean Laboratories AB when porting linux
 * to the automotive development board Russellville. The copyright holder
 * as seen in the header is Intel corporation.
 * Mocean Laboratories forked off the GNU/Linux platform work into a
 * separate company called Pelagicore AB, which committed the code to the
 * kernel.
 */

/* Supports:
 * Xilinx IIC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/i2c-xiic.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>

#define DRIVER_NAME "xiic-i2c"

enum xilinx_i2c_state {
	STATE_DONE,
	STATE_ERROR,
	STATE_START
};

enum xiic_endian {
	LITTLE,
	BIG
};

/**
 * struct xiic_i2c - Internal representation of the XIIC I2C bus
 * @base:	Memory base of the HW registers
 * @wait:	Wait queue for callers
 * @adap:	Kernel adapter representation
 * @tx_msg:	Messages from above to be sent
 * @lock:	Mutual exclusion
 * @tx_pos:	Current pos in TX message
 * @nmsgs:	Number of messages in tx_msg
 * @state:	See STATE_
 * @rx_msg:	Current RX message
 * @rx_pos:	Position within current RX message
 */
struct xiic_i2c {
	void __iomem		*base;
	wait_queue_head_t	wait;
	struct i2c_adapter	adap;
	struct i2c_msg		*tx_msg;
	spinlock_t		lock;
	unsigned int		tx_pos;
	unsigned int		nmsgs;
	enum xilinx_i2c_state	state;
	struct i2c_msg		*rx_msg;
	int			rx_pos;
	enum xiic_endian	endianness;
};


#define XIIC_MSB_OFFSET 0
#define XIIC_REG_OFFSET (0x100+XIIC_MSB_OFFSET)

/*
 * Register offsets in bytes from RegisterBase. Three is added to the
 * base offset to access LSB (IBM style) of the word
 */
#define XIIC_CR_REG_OFFSET   (0x00+XIIC_REG_OFFSET)	/* Control Register   */
#define XIIC_SR_REG_OFFSET   (0x04+XIIC_REG_OFFSET)	/* Status Register    */
#define XIIC_DTR_REG_OFFSET  (0x08+XIIC_REG_OFFSET)	/* Data Tx Register   */
#define XIIC_DRR_REG_OFFSET  (0x0C+XIIC_REG_OFFSET)	/* Data Rx Register   */
#define XIIC_ADR_REG_OFFSET  (0x10+XIIC_REG_OFFSET)	/* Address Register   */
#define XIIC_TFO_REG_OFFSET  (0x14+XIIC_REG_OFFSET)	/* Tx FIFO Occupancy  */
#define XIIC_RFO_REG_OFFSET  (0x18+XIIC_REG_OFFSET)	/* Rx FIFO Occupancy  */
#define XIIC_TBA_REG_OFFSET  (0x1C+XIIC_REG_OFFSET)	/* 10 Bit Address reg */
#define XIIC_RFD_REG_OFFSET  (0x20+XIIC_REG_OFFSET)	/* Rx FIFO Depth reg  */
#define XIIC_GPO_REG_OFFSET  (0x24+XIIC_REG_OFFSET)	/* Output Register    */

/* Control Register masks */
#define XIIC_CR_ENABLE_DEVICE_MASK        0x01	/* Device enable = 1      */
#define XIIC_CR_TX_FIFO_RESET_MASK        0x02	/* Transmit FIFO reset=1  */
#define XIIC_CR_MSMS_MASK                 0x04	/* Master starts Txing=1  */
#define XIIC_CR_DIR_IS_TX_MASK            0x08	/* Dir of tx. Txing=1     */
#define XIIC_CR_NO_ACK_MASK               0x10	/* Tx Ack. NO ack = 1     */
#define XIIC_CR_REPEATED_START_MASK       0x20	/* Repeated start = 1     */
#define XIIC_CR_GENERAL_CALL_MASK         0x40	/* Gen Call enabled = 1   */

/* Status Register masks */
#define XIIC_SR_GEN_CALL_MASK             0x01	/* 1=a mstr issued a GC   */
#define XIIC_SR_ADDR_AS_SLAVE_MASK        0x02	/* 1=when addr as slave   */
#define XIIC_SR_BUS_BUSY_MASK             0x04	/* 1 = bus is busy        */
#define XIIC_SR_MSTR_RDING_SLAVE_MASK     0x08	/* 1=Dir: mstr <-- slave  */
#define XIIC_SR_TX_FIFO_FULL_MASK         0x10	/* 1 = Tx FIFO full       */
#define XIIC_SR_RX_FIFO_FULL_MASK         0x20	/* 1 = Rx FIFO full       */
#define XIIC_SR_RX_FIFO_EMPTY_MASK        0x40	/* 1 = Rx FIFO empty      */
#define XIIC_SR_TX_FIFO_EMPTY_MASK        0x80	/* 1 = Tx FIFO empty      */

/* Interrupt Status Register masks    Interrupt occurs when...       */
#define XIIC_INTR_ARB_LOST_MASK           0x01	/* 1 = arbitration lost   */
#define XIIC_INTR_TX_ERROR_MASK           0x02	/* 1=Tx error/msg complete */
#define XIIC_INTR_TX_EMPTY_MASK           0x04	/* 1 = Tx FIFO/reg empty  */
#define XIIC_INTR_RX_FULL_MASK            0x08	/* 1=Rx FIFO/reg=OCY level */
#define XIIC_INTR_BNB_MASK                0x10	/* 1 = Bus not busy       */
#define XIIC_INTR_AAS_MASK                0x20	/* 1 = when addr as slave */
#define XIIC_INTR_NAAS_MASK               0x40	/* 1 = not addr as slave  */
#define XIIC_INTR_TX_HALF_MASK            0x80	/* 1 = TX FIFO half empty */

/* The following constants specify the depth of the FIFOs */
#define IIC_RX_FIFO_DEPTH         16	/* Rx fifo capacity               */
#define IIC_TX_FIFO_DEPTH         16	/* Tx fifo capacity               */

/* The following constants specify groups of interrupts that are typically
 * enabled or disables at the same time
 */
#define XIIC_TX_INTERRUPTS                           \
(XIIC_INTR_TX_ERROR_MASK | XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_HALF_MASK)

#define XIIC_TX_RX_INTERRUPTS (XIIC_INTR_RX_FULL_MASK | XIIC_TX_INTERRUPTS)

/* The following constants are used with the following macros to specify the
 * operation, a read or write operation.
 */
#define XIIC_READ_OPERATION  1
#define XIIC_WRITE_OPERATION 0

/*
 * Tx Fifo upper bit masks.
 */
#define XIIC_TX_DYN_START_MASK            0x0100 /* 1 = Set dynamic start */
#define XIIC_TX_DYN_STOP_MASK             0x0200 /* 1 = Set dynamic stop */

/*
 * The following constants define the register offsets for the Interrupt
 * registers. There are some holes in the memory map for reserved addresses
 * to allow other registers to be added and still match the memory map of the
 * interrupt controller registers
 */
#define XIIC_DGIER_OFFSET    0x1C /* Device Global Interrupt Enable Register */
#define XIIC_IISR_OFFSET     0x20 /* Interrupt Status Register */
#define XIIC_IIER_OFFSET     0x28 /* Interrupt Enable Register */
#define XIIC_RESETR_OFFSET   0x40 /* Reset Register */

#define XIIC_RESET_MASK             0xAUL

/*
 * The following constant is used for the device global interrupt enable
 * register, to enable all interrupts for the device, this is the only bit
 * in the register
 */
#define XIIC_GINTR_ENABLE_MASK      0x80000000UL

#define xiic_tx_space(i2c) ((i2c)->tx_msg->len - (i2c)->tx_pos)
#define xiic_rx_space(i2c) ((i2c)->rx_msg->len - (i2c)->rx_pos)

static void xiic_start_xfer(struct xiic_i2c *i2c);
static void __xiic_start_xfer(struct xiic_i2c *i2c);

/*
 * For the register read and write functions, a little-endian and big-endian
 * version are necessary. Endianness is detected during the probe function.
 * Only the least significant byte [doublet] of the register are ever
 * accessed. This requires an offset of 3 [2] from the base address for
 * big-endian systems.
 */

static inline void xiic_setreg8(struct xiic_i2c *i2c, int reg, u8 value)
{
	if (i2c->endianness == LITTLE)
		iowrite8(value, i2c->base + reg);
	else
		iowrite8(value, i2c->base + reg + 3);
}

static inline u8 xiic_getreg8(struct xiic_i2c *i2c, int reg)
{
	u8 ret;

	if (i2c->endianness == LITTLE)
		ret = ioread8(i2c->base + reg);
	else
		ret = ioread8(i2c->base + reg + 3);
	return ret;
}

static inline void xiic_setreg16(struct xiic_i2c *i2c, int reg, u16 value)
{
	if (i2c->endianness == LITTLE)
		iowrite16(value, i2c->base + reg);
	else
		iowrite16be(value, i2c->base + reg + 2);
}

static inline void xiic_setreg32(struct xiic_i2c *i2c, int reg, int value)
{
	if (i2c->endianness == LITTLE)
		iowrite32(value, i2c->base + reg);
	else
		iowrite32be(value, i2c->base + reg);
}

static inline int xiic_getreg32(struct xiic_i2c *i2c, int reg)
{
	u32 ret;

	if (i2c->endianness == LITTLE)
		ret = ioread32(i2c->base + reg);
	else
		ret = ioread32be(i2c->base + reg);
	return ret;
}

static inline void xiic_irq_dis(struct xiic_i2c *i2c, u32 mask)
{
	u32 ier = xiic_getreg32(i2c, XIIC_IIER_OFFSET);
	xiic_setreg32(i2c, XIIC_IIER_OFFSET, ier & ~mask);
}

static inline void xiic_irq_en(struct xiic_i2c *i2c, u32 mask)
{
	u32 ier = xiic_getreg32(i2c, XIIC_IIER_OFFSET);
	xiic_setreg32(i2c, XIIC_IIER_OFFSET, ier | mask);
}

static inline void xiic_irq_clr(struct xiic_i2c *i2c, u32 mask)
{
	u32 isr = xiic_getreg32(i2c, XIIC_IISR_OFFSET);
	xiic_setreg32(i2c, XIIC_IISR_OFFSET, isr & mask);
}

static inline void xiic_irq_clr_en(struct xiic_i2c *i2c, u32 mask)
{
	xiic_irq_clr(i2c, mask);
	xiic_irq_en(i2c, mask);
}

static void xiic_clear_rx_fifo(struct xiic_i2c *i2c)
{
	u8 sr;
	for (sr = xiic_getreg8(i2c, XIIC_SR_REG_OFFSET);
		!(sr & XIIC_SR_RX_FIFO_EMPTY_MASK);
		sr = xiic_getreg8(i2c, XIIC_SR_REG_OFFSET))
		xiic_getreg8(i2c, XIIC_DRR_REG_OFFSET);
}

static void xiic_reinit(struct xiic_i2c *i2c)
{
	xiic_setreg32(i2c, XIIC_RESETR_OFFSET, XIIC_RESET_MASK);

	/* Set receive Fifo depth to maximum (zero based). */
	xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, IIC_RX_FIFO_DEPTH - 1);

	/* Reset Tx Fifo. */
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);

	/* Enable IIC Device, remove Tx Fifo reset & disable general call. */
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);

	/* make sure RX fifo is empty */
	xiic_clear_rx_fifo(i2c);

	/* Enable interrupts */
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, XIIC_GINTR_ENABLE_MASK);

	xiic_irq_clr_en(i2c, XIIC_INTR_AAS_MASK | XIIC_INTR_ARB_LOST_MASK);
}

static void xiic_deinit(struct xiic_i2c *i2c)
{
	u8 cr;

	xiic_setreg32(i2c, XIIC_RESETR_OFFSET, XIIC_RESET_MASK);

	/* Disable IIC Device. */
	cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr & ~XIIC_CR_ENABLE_DEVICE_MASK);
}

static void xiic_read_rx(struct xiic_i2c *i2c)
{
	u8 bytes_in_fifo;
	int i;

	bytes_in_fifo = xiic_getreg8(i2c, XIIC_RFO_REG_OFFSET) + 1;

	dev_dbg(i2c->adap.dev.parent,
		"%s entry, bytes in fifo: %d, msg: %d, SR: 0x%x, CR: 0x%x\n",
		__func__, bytes_in_fifo, xiic_rx_space(i2c),
		xiic_getreg8(i2c, XIIC_SR_REG_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

	if (bytes_in_fifo > xiic_rx_space(i2c))
		bytes_in_fifo = xiic_rx_space(i2c);

	for (i = 0; i < bytes_in_fifo; i++)
		i2c->rx_msg->buf[i2c->rx_pos++] =
			xiic_getreg8(i2c, XIIC_DRR_REG_OFFSET);

	xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET,
		(xiic_rx_space(i2c) > IIC_RX_FIFO_DEPTH) ?
		IIC_RX_FIFO_DEPTH - 1 :  xiic_rx_space(i2c) - 1);
}

static int xiic_tx_fifo_space(struct xiic_i2c *i2c)
{
	/* return the actual space left in the FIFO */
	return IIC_TX_FIFO_DEPTH - xiic_getreg8(i2c, XIIC_TFO_REG_OFFSET) - 1;
}

static void xiic_fill_tx_fifo(struct xiic_i2c *i2c)
{
	u8 fifo_space = xiic_tx_fifo_space(i2c);
	int len = xiic_tx_space(i2c);

	len = (len > fifo_space) ? fifo_space : len;

	dev_dbg(i2c->adap.dev.parent, "%s entry, len: %d, fifo space: %d\n",
		__func__, len, fifo_space);

	while (len--) {
		u16 data = i2c->tx_msg->buf[i2c->tx_pos++];
		if ((xiic_tx_space(i2c) == 0) && (i2c->nmsgs == 1)) {
			/* last message in transfer -> STOP */
			data |= XIIC_TX_DYN_STOP_MASK;
			dev_dbg(i2c->adap.dev.parent, "%s TX STOP\n", __func__);
		}
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, data);
	}
}

static void xiic_wakeup(struct xiic_i2c *i2c, int code)
{
	i2c->tx_msg = NULL;
	i2c->rx_msg = NULL;
	i2c->nmsgs = 0;
	i2c->state = code;
	wake_up(&i2c->wait);
}

static void xiic_process(struct xiic_i2c *i2c)
{
	u32 pend, isr, ier;
	u32 clr = 0;

	/* Get the interrupt Status from the IPIF. There is no clearing of
	 * interrupts in the IPIF. Interrupts must be cleared at the source.
	 * To find which interrupts are pending; AND interrupts pending with
	 * interrupts masked.
	 */
	isr = xiic_getreg32(i2c, XIIC_IISR_OFFSET);
	ier = xiic_getreg32(i2c, XIIC_IIER_OFFSET);
	pend = isr & ier;

	dev_dbg(i2c->adap.dev.parent, "%s: IER: 0x%x, ISR: 0x%x, pend: 0x%x\n",
		__func__, ier, isr, pend);
	dev_dbg(i2c->adap.dev.parent, "%s: SR: 0x%x, msg: %p, nmsgs: %d\n",
		__func__, xiic_getreg8(i2c, XIIC_SR_REG_OFFSET),
		i2c->tx_msg, i2c->nmsgs);

	/* Do not processes a devices interrupts if the device has no
	 * interrupts pending
	 */
	if (!pend)
		return;

	/* Service requesting interrupt */
	if ((pend & XIIC_INTR_ARB_LOST_MASK) ||
		((pend & XIIC_INTR_TX_ERROR_MASK) &&
		!(pend & XIIC_INTR_RX_FULL_MASK))) {
		/* bus arbritration lost, or...
		 * Transmit error _OR_ RX completed
		 * if this happens when RX_FULL is not set
		 * this is probably a TX error
		 */

		dev_dbg(i2c->adap.dev.parent, "%s error\n", __func__);

		/* dynamic mode seem to suffer from problems if we just flushes
		 * fifos and the next message is a TX with len 0 (only addr)
		 * reset the IP instead of just flush fifos
		 */
		xiic_reinit(i2c);

		if (i2c->tx_msg)
			xiic_wakeup(i2c, STATE_ERROR);

	} else if (pend & XIIC_INTR_RX_FULL_MASK) {
		/* Receive register/FIFO is full */

		clr = XIIC_INTR_RX_FULL_MASK;
		if (!i2c->rx_msg) {
			dev_dbg(i2c->adap.dev.parent,
				"%s unexpexted RX IRQ\n", __func__);
			xiic_clear_rx_fifo(i2c);
			goto out;
		}

		xiic_read_rx(i2c);
		if (xiic_rx_space(i2c) == 0) {
			/* this is the last part of the message */
			i2c->rx_msg = NULL;

			/* also clear TX error if there (RX complete) */
			clr |= (isr & XIIC_INTR_TX_ERROR_MASK);

			dev_dbg(i2c->adap.dev.parent,
				"%s end of message, nmsgs: %d\n",
				__func__, i2c->nmsgs);

			/* send next message if this wasn't the last,
			 * otherwise the transfer will be finialise when
			 * receiving the bus not busy interrupt
			 */
			if (i2c->nmsgs > 1) {
				i2c->nmsgs--;
				i2c->tx_msg++;
				dev_dbg(i2c->adap.dev.parent,
					"%s will start next...\n", __func__);

				__xiic_start_xfer(i2c);
			}
		}
	} else if (pend & XIIC_INTR_BNB_MASK) {
		/* IIC bus has transitioned to not busy */
		clr = XIIC_INTR_BNB_MASK;

		/* The bus is not busy, disable BusNotBusy interrupt */
		xiic_irq_dis(i2c, XIIC_INTR_BNB_MASK);

		if (!i2c->tx_msg)
			goto out;

		if ((i2c->nmsgs == 1) && !i2c->rx_msg &&
			xiic_tx_space(i2c) == 0)
			xiic_wakeup(i2c, STATE_DONE);
		else
			xiic_wakeup(i2c, STATE_ERROR);

	} else if (pend & (XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_HALF_MASK)) {
		/* Transmit register/FIFO is empty or ½ empty */

		clr = pend &
			(XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_HALF_MASK);

		if (!i2c->tx_msg) {
			dev_dbg(i2c->adap.dev.parent,
				"%s unexpexted TX IRQ\n", __func__);
			goto out;
		}

		xiic_fill_tx_fifo(i2c);

		/* current message sent and there is space in the fifo */
		if (!xiic_tx_space(i2c) && xiic_tx_fifo_space(i2c) >= 2) {
			dev_dbg(i2c->adap.dev.parent,
				"%s end of message sent, nmsgs: %d\n",
				__func__, i2c->nmsgs);
			if (i2c->nmsgs > 1) {
				i2c->nmsgs--;
				i2c->tx_msg++;
				__xiic_start_xfer(i2c);
			} else {
				xiic_irq_dis(i2c, XIIC_INTR_TX_HALF_MASK);

				dev_dbg(i2c->adap.dev.parent,
					"%s Got TX IRQ but no more to do...\n",
					__func__);
			}
		} else if (!xiic_tx_space(i2c) && (i2c->nmsgs == 1))
			/* current frame is sent and is last,
			 * make sure to disable tx half
			 */
			xiic_irq_dis(i2c, XIIC_INTR_TX_HALF_MASK);
	} else {
		/* got IRQ which is not acked */
		dev_err(i2c->adap.dev.parent, "%s Got unexpected IRQ\n",
			__func__);
		clr = pend;
	}
out:
	dev_dbg(i2c->adap.dev.parent, "%s clr: 0x%x\n", __func__, clr);

	xiic_setreg32(i2c, XIIC_IISR_OFFSET, clr);
}

static int xiic_bus_busy(struct xiic_i2c *i2c)
{
	u8 sr = xiic_getreg8(i2c, XIIC_SR_REG_OFFSET);

	return (sr & XIIC_SR_BUS_BUSY_MASK) ? -EBUSY : 0;
}

static int xiic_busy(struct xiic_i2c *i2c)
{
	int tries = 3;
	int err;

	if (i2c->tx_msg)
		return -EBUSY;

	/* for instance if previous transfer was terminated due to TX error
	 * it might be that the bus is on it's way to become available
	 * give it at most 3 ms to wake
	 */
	err = xiic_bus_busy(i2c);
	while (err && tries--) {
		mdelay(1);
		err = xiic_bus_busy(i2c);
	}

	return err;
}

static void xiic_start_recv(struct xiic_i2c *i2c)
{
	u8 rx_watermark;
	struct i2c_msg *msg = i2c->rx_msg = i2c->tx_msg;

	/* Clear and enable Rx full interrupt. */
	xiic_irq_clr_en(i2c, XIIC_INTR_RX_FULL_MASK | XIIC_INTR_TX_ERROR_MASK);

	/* we want to get all but last byte, because the TX_ERROR IRQ is used
	 * to inidicate error ACK on the address, and negative ack on the last
	 * received byte, so to not mix them receive all but last.
	 * In the case where there is only one byte to receive
	 * we can check if ERROR and RX full is set at the same time
	 */
	rx_watermark = msg->len;
	if (rx_watermark > IIC_RX_FIFO_DEPTH)
		rx_watermark = IIC_RX_FIFO_DEPTH;
	xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, rx_watermark - 1);

	if (!(msg->flags & I2C_M_NOSTART))
		/* write the address */
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET,
			(msg->addr << 1) | XIIC_READ_OPERATION |
			XIIC_TX_DYN_START_MASK);

	xiic_irq_clr_en(i2c, XIIC_INTR_BNB_MASK);

	xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET,
		msg->len | ((i2c->nmsgs == 1) ? XIIC_TX_DYN_STOP_MASK : 0));
	if (i2c->nmsgs == 1)
		/* very last, enable bus not busy as well */
		xiic_irq_clr_en(i2c, XIIC_INTR_BNB_MASK);

	/* the message is tx:ed */
	i2c->tx_pos = msg->len;
}

static void xiic_start_send(struct xiic_i2c *i2c)
{
	struct i2c_msg *msg = i2c->tx_msg;

	xiic_irq_clr(i2c, XIIC_INTR_TX_ERROR_MASK);

	dev_dbg(i2c->adap.dev.parent, "%s entry, msg: %p, len: %d",
		__func__, msg, msg->len);
	dev_dbg(i2c->adap.dev.parent, "%s entry, ISR: 0x%x, CR: 0x%x\n",
		__func__, xiic_getreg32(i2c, XIIC_IISR_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

	if (!(msg->flags & I2C_M_NOSTART)) {
		/* write the address */
		u16 data = ((msg->addr << 1) & 0xfe) | XIIC_WRITE_OPERATION |
			XIIC_TX_DYN_START_MASK;
		if ((i2c->nmsgs == 1) && msg->len == 0)
			/* no data and last message -> add STOP */
			data |= XIIC_TX_DYN_STOP_MASK;

		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, data);
	}

	xiic_fill_tx_fifo(i2c);

	/* Clear any pending Tx empty, Tx Error and then enable them. */
	xiic_irq_clr_en(i2c, XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_ERROR_MASK |
		XIIC_INTR_BNB_MASK);
}

static irqreturn_t xiic_isr(int irq, void *dev_id)
{
	struct xiic_i2c *i2c = dev_id;

	spin_lock(&i2c->lock);
	/* disable interrupts globally */
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, 0);

	dev_dbg(i2c->adap.dev.parent, "%s entry\n", __func__);

	xiic_process(i2c);

	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, XIIC_GINTR_ENABLE_MASK);
	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}

static void __xiic_start_xfer(struct xiic_i2c *i2c)
{
	int first = 1;
	int fifo_space = xiic_tx_fifo_space(i2c);
	dev_dbg(i2c->adap.dev.parent, "%s entry, msg: %p, fifos space: %d\n",
		__func__, i2c->tx_msg, fifo_space);

	if (!i2c->tx_msg)
		return;

	i2c->rx_pos = 0;
	i2c->tx_pos = 0;
	i2c->state = STATE_START;
	while ((fifo_space >= 2) && (first || (i2c->nmsgs > 1))) {
		if (!first) {
			i2c->nmsgs--;
			i2c->tx_msg++;
			i2c->tx_pos = 0;
		} else
			first = 0;

		if (i2c->tx_msg->flags & I2C_M_RD) {
			/* we dont date putting several reads in the FIFO */
			xiic_start_recv(i2c);
			return;
		} else {
			xiic_start_send(i2c);
			if (xiic_tx_space(i2c) != 0) {
				/* the message could not be completely sent */
				break;
			}
		}

		fifo_space = xiic_tx_fifo_space(i2c);
	}

	/* there are more messages or the current one could not be completely
	 * put into the FIFO, also enable the half empty interrupt
	 */
	if (i2c->nmsgs > 1 || xiic_tx_space(i2c))
		xiic_irq_clr_en(i2c, XIIC_INTR_TX_HALF_MASK);

}

static void xiic_start_xfer(struct xiic_i2c *i2c)
{
	unsigned long flags;

	spin_lock_irqsave(&i2c->lock, flags);
	xiic_reinit(i2c);
	/* disable interrupts globally */
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, 0);
	spin_unlock_irqrestore(&i2c->lock, flags);

	__xiic_start_xfer(i2c);
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, XIIC_GINTR_ENABLE_MASK);
}

static int xiic_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct xiic_i2c *i2c = i2c_get_adapdata(adap);
	int err;

	dev_dbg(adap->dev.parent, "%s entry SR: 0x%x\n", __func__,
		xiic_getreg8(i2c, XIIC_SR_REG_OFFSET));

	err = xiic_busy(i2c);
	if (err)
		return err;

	i2c->tx_msg = msgs;
	i2c->nmsgs = num;

	xiic_start_xfer(i2c);

	if (wait_event_timeout(i2c->wait, (i2c->state == STATE_ERROR) ||
		(i2c->state == STATE_DONE), HZ))
		return (i2c->state == STATE_DONE) ? num : -EIO;
	else {
		i2c->tx_msg = NULL;
		i2c->rx_msg = NULL;
		i2c->nmsgs = 0;
		return -ETIMEDOUT;
	}
}

static u32 xiic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm xiic_algorithm = {
	.master_xfer = xiic_xfer,
	.functionality = xiic_func,
};

static struct i2c_adapter xiic_adapter = {
	.owner = THIS_MODULE,
	.name = DRIVER_NAME,
	.class = I2C_CLASS_DEPRECATED,
	.algo = &xiic_algorithm,
};


static int xiic_i2c_probe(struct platform_device *pdev)
{
	struct xiic_i2c *i2c;
	struct xiic_i2c_platform_data *pdata;
	struct resource *res;
	int ret, irq;
	u8 i;
	u32 sr;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	pdata = dev_get_platdata(&pdev->dev);

	/* hook up driver to tree */
	platform_set_drvdata(pdev, i2c);
	i2c->adap = xiic_adapter;
	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = pdev->dev.of_node;

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	ret = devm_request_irq(&pdev->dev, irq, xiic_isr, 0, pdev->name, i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return ret;
	}

	/*
	 * Detect endianness
	 * Try to reset the TX FIFO. Then check the EMPTY flag. If it is not
	 * set, assume that the endianness was wrong and swap.
	 */
	i2c->endianness = LITTLE;
	xiic_setreg32(i2c, XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);
	/* Reset is cleared in xiic_reinit */
	sr = xiic_getreg32(i2c, XIIC_SR_REG_OFFSET);
	if (!(sr & XIIC_SR_TX_FIFO_EMPTY_MASK))
		i2c->endianness = BIG;

	xiic_reinit(i2c);

	/* add i2c adapter to i2c tree */
	ret = i2c_add_adapter(&i2c->adap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add adapter\n");
		xiic_deinit(i2c);
		return ret;
	}

	if (pdata) {
		/* add in known devices to the bus */
		for (i = 0; i < pdata->num_devices; i++)
			i2c_new_device(&i2c->adap, pdata->devices + i);
	}

	return 0;
}

static int xiic_i2c_remove(struct platform_device *pdev)
{
	struct xiic_i2c *i2c = platform_get_drvdata(pdev);

	/* remove adapter & data */
	i2c_del_adapter(&i2c->adap);

	xiic_deinit(i2c);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id xiic_of_match[] = {
	{ .compatible = "xlnx,xps-iic-2.00.a", },
	{},
};
MODULE_DEVICE_TABLE(of, xiic_of_match);
#endif

static struct platform_driver xiic_i2c_driver = {
	.probe   = xiic_i2c_probe,
	.remove  = xiic_i2c_remove,
	.driver  = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(xiic_of_match),
	},
};

module_platform_driver(xiic_i2c_driver);

MODULE_AUTHOR("info@mocean-labs.com");
MODULE_DESCRIPTION("Xilinx I2C bus driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:"DRIVER_NAME);

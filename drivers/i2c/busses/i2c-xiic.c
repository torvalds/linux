// SPDX-License-Identifier: GPL-2.0-only
/*
 * i2c-xiic.c
 * Copyright (c) 2002-2007 Xilinx Inc.
 * Copyright (c) 2009-2010 Intel Corporation
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
#include <linux/completion.h>
#include <linux/platform_data/i2c-xiic.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME "xiic-i2c"
#define DYNAMIC_MODE_READ_BROKEN_BIT	BIT(0)
#define SMBUS_BLOCK_READ_MIN_LEN	3

enum xilinx_i2c_state {
	STATE_DONE,
	STATE_ERROR,
	STATE_START
};

enum xiic_endian {
	LITTLE,
	BIG
};

enum i2c_scl_freq {
	REG_VALUES_100KHZ = 0,
	REG_VALUES_400KHZ = 1,
	REG_VALUES_1MHZ = 2
};

/**
 * struct xiic_i2c - Internal representation of the XIIC I2C bus
 * @dev: Pointer to device structure
 * @base: Memory base of the HW registers
 * @completion:	Completion for callers
 * @adap: Kernel adapter representation
 * @tx_msg: Messages from above to be sent
 * @lock: Mutual exclusion
 * @tx_pos: Current pos in TX message
 * @nmsgs: Number of messages in tx_msg
 * @rx_msg: Current RX message
 * @rx_pos: Position within current RX message
 * @endianness: big/little-endian byte order
 * @clk: Pointer to AXI4-lite input clock
 * @state: See STATE_
 * @singlemaster: Indicates bus is single master
 * @dynamic: Mode of controller
 * @prev_msg_tx: Previous message is Tx
 * @quirks: To hold platform specific bug info
 * @smbus_block_read: Flag to handle block read
 * @input_clk: Input clock to I2C controller
 * @i2c_clk: I2C SCL frequency
 */
struct xiic_i2c {
	struct device *dev;
	void __iomem *base;
	struct completion completion;
	struct i2c_adapter adap;
	struct i2c_msg *tx_msg;
	struct mutex lock;
	unsigned int tx_pos;
	unsigned int nmsgs;
	struct i2c_msg *rx_msg;
	int rx_pos;
	enum xiic_endian endianness;
	struct clk *clk;
	enum xilinx_i2c_state state;
	bool singlemaster;
	bool dynamic;
	bool prev_msg_tx;
	u32 quirks;
	bool smbus_block_read;
	unsigned long input_clk;
	unsigned int i2c_clk;
};

struct xiic_version_data {
	u32 quirks;
};

/**
 * struct timing_regs - AXI I2C timing registers that depend on I2C spec
 * @tsusta: setup time for a repeated START condition
 * @tsusto: setup time for a STOP condition
 * @thdsta: hold time for a repeated START condition
 * @tsudat: setup time for data
 * @tbuf: bus free time between STOP and START
 */
struct timing_regs {
	unsigned int tsusta;
	unsigned int tsusto;
	unsigned int thdsta;
	unsigned int tsudat;
	unsigned int tbuf;
};

/* Reg values in ns derived from I2C spec and AXI I2C PG for different frequencies */
static const struct timing_regs timing_reg_values[] = {
	{ 5700, 5000, 4300, 550, 5000 }, /* Reg values for 100KHz */
	{ 900, 900, 900, 400, 1600 },    /* Reg values for 400KHz */
	{ 380, 380, 380, 170, 620 },     /* Reg values for 1MHz   */
};

#define XIIC_MSB_OFFSET 0
#define XIIC_REG_OFFSET (0x100 + XIIC_MSB_OFFSET)

/*
 * Register offsets in bytes from RegisterBase. Three is added to the
 * base offset to access LSB (IBM style) of the word
 */
#define XIIC_CR_REG_OFFSET   (0x00 + XIIC_REG_OFFSET)	/* Control Register   */
#define XIIC_SR_REG_OFFSET   (0x04 + XIIC_REG_OFFSET)	/* Status Register    */
#define XIIC_DTR_REG_OFFSET  (0x08 + XIIC_REG_OFFSET)	/* Data Tx Register   */
#define XIIC_DRR_REG_OFFSET  (0x0C + XIIC_REG_OFFSET)	/* Data Rx Register   */
#define XIIC_ADR_REG_OFFSET  (0x10 + XIIC_REG_OFFSET)	/* Address Register   */
#define XIIC_TFO_REG_OFFSET  (0x14 + XIIC_REG_OFFSET)	/* Tx FIFO Occupancy  */
#define XIIC_RFO_REG_OFFSET  (0x18 + XIIC_REG_OFFSET)	/* Rx FIFO Occupancy  */
#define XIIC_TBA_REG_OFFSET  (0x1C + XIIC_REG_OFFSET)	/* 10 Bit Address reg */
#define XIIC_RFD_REG_OFFSET  (0x20 + XIIC_REG_OFFSET)	/* Rx FIFO Depth reg  */
#define XIIC_GPO_REG_OFFSET  (0x24 + XIIC_REG_OFFSET)	/* Output Register    */

/*
 * Timing register offsets from RegisterBase. These are used only for
 * setting i2c clock frequency for the line.
 */
#define XIIC_TSUSTA_REG_OFFSET (0x28 + XIIC_REG_OFFSET) /* TSUSTA Register */
#define XIIC_TSUSTO_REG_OFFSET (0x2C + XIIC_REG_OFFSET) /* TSUSTO Register */
#define XIIC_THDSTA_REG_OFFSET (0x30 + XIIC_REG_OFFSET) /* THDSTA Register */
#define XIIC_TSUDAT_REG_OFFSET (0x34 + XIIC_REG_OFFSET) /* TSUDAT Register */
#define XIIC_TBUF_REG_OFFSET   (0x38 + XIIC_REG_OFFSET) /* TBUF Register */
#define XIIC_THIGH_REG_OFFSET  (0x3C + XIIC_REG_OFFSET) /* THIGH Register */
#define XIIC_TLOW_REG_OFFSET   (0x40 + XIIC_REG_OFFSET) /* TLOW Register */
#define XIIC_THDDAT_REG_OFFSET (0x44 + XIIC_REG_OFFSET) /* THDDAT Register */

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

/*
 * Tx Fifo upper bit masks.
 */
#define XIIC_TX_DYN_START_MASK            0x0100 /* 1 = Set dynamic start */
#define XIIC_TX_DYN_STOP_MASK             0x0200 /* 1 = Set dynamic stop */

/* Dynamic mode constants */
#define MAX_READ_LENGTH_DYNAMIC                255 /* Max length for dynamic read */

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

#define XIIC_PM_TIMEOUT		1000	/* ms */
/* timeout waiting for the controller to respond */
#define XIIC_I2C_TIMEOUT	(msecs_to_jiffies(1000))
/* timeout waiting for the controller finish transfers */
#define XIIC_XFER_TIMEOUT	(msecs_to_jiffies(10000))

/*
 * The following constant is used for the device global interrupt enable
 * register, to enable all interrupts for the device, this is the only bit
 * in the register
 */
#define XIIC_GINTR_ENABLE_MASK      0x80000000UL

#define xiic_tx_space(i2c) ((i2c)->tx_msg->len - (i2c)->tx_pos)
#define xiic_rx_space(i2c) ((i2c)->rx_msg->len - (i2c)->rx_pos)

static int xiic_start_xfer(struct xiic_i2c *i2c, struct i2c_msg *msgs, int num);
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

static int xiic_clear_rx_fifo(struct xiic_i2c *i2c)
{
	u8 sr;
	unsigned long timeout;

	timeout = jiffies + XIIC_I2C_TIMEOUT;
	for (sr = xiic_getreg8(i2c, XIIC_SR_REG_OFFSET);
		!(sr & XIIC_SR_RX_FIFO_EMPTY_MASK);
		sr = xiic_getreg8(i2c, XIIC_SR_REG_OFFSET)) {
		xiic_getreg8(i2c, XIIC_DRR_REG_OFFSET);
		if (time_after(jiffies, timeout)) {
			dev_err(i2c->dev, "Failed to clear rx fifo\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int xiic_wait_tx_empty(struct xiic_i2c *i2c)
{
	u8 isr;
	unsigned long timeout;

	timeout = jiffies + XIIC_I2C_TIMEOUT;
	for (isr = xiic_getreg32(i2c, XIIC_IISR_OFFSET);
		!(isr & XIIC_INTR_TX_EMPTY_MASK);
			isr = xiic_getreg32(i2c, XIIC_IISR_OFFSET)) {
		if (time_after(jiffies, timeout)) {
			dev_err(i2c->dev, "Timeout waiting at Tx empty\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/**
 * xiic_setclk - Sets the configured clock rate
 * @i2c: Pointer to the xiic device structure
 *
 * The timing register values are calculated according to the input clock
 * frequency and configured scl frequency. For details, please refer the
 * AXI I2C PG and NXP I2C Spec.
 * Supported frequencies are 100KHz, 400KHz and 1MHz.
 *
 * Return: 0 on success (Supported frequency selected or not configurable in SW)
 *        -EINVAL on failure (scl frequency not supported or THIGH is 0)
 */
static int xiic_setclk(struct xiic_i2c *i2c)
{
	unsigned int clk_in_mhz;
	unsigned int index = 0;
	u32 reg_val;

	dev_dbg(i2c->adap.dev.parent,
		"%s entry, i2c->input_clk: %ld, i2c->i2c_clk: %d\n",
		__func__, i2c->input_clk, i2c->i2c_clk);

	/* If not specified in DT, do not configure in SW. Rely only on Vivado design */
	if (!i2c->i2c_clk || !i2c->input_clk)
		return 0;

	clk_in_mhz = DIV_ROUND_UP(i2c->input_clk, 1000000);

	switch (i2c->i2c_clk) {
	case I2C_MAX_FAST_MODE_PLUS_FREQ:
		index = REG_VALUES_1MHZ;
		break;
	case I2C_MAX_FAST_MODE_FREQ:
		index = REG_VALUES_400KHZ;
		break;
	case I2C_MAX_STANDARD_MODE_FREQ:
		index = REG_VALUES_100KHZ;
		break;
	default:
		dev_warn(i2c->adap.dev.parent, "Unsupported scl frequency\n");
		return -EINVAL;
	}

	/*
	 * Value to be stored in a register is the number of clock cycles required
	 * for the time duration. So the time is divided by the input clock time
	 * period to get the number of clock cycles required. Refer Xilinx AXI I2C
	 * PG document and I2C specification for further details.
	 */

	/* THIGH - Depends on SCL clock frequency(i2c_clk) as below */
	reg_val = (DIV_ROUND_UP(i2c->input_clk, 2 * i2c->i2c_clk)) - 7;
	if (reg_val == 0)
		return -EINVAL;

	xiic_setreg32(i2c, XIIC_THIGH_REG_OFFSET, reg_val - 1);

	/* TLOW - Value same as THIGH */
	xiic_setreg32(i2c, XIIC_TLOW_REG_OFFSET, reg_val - 1);

	/* TSUSTA */
	reg_val = (timing_reg_values[index].tsusta * clk_in_mhz) / 1000;
	xiic_setreg32(i2c, XIIC_TSUSTA_REG_OFFSET, reg_val - 1);

	/* TSUSTO */
	reg_val = (timing_reg_values[index].tsusto * clk_in_mhz) / 1000;
	xiic_setreg32(i2c, XIIC_TSUSTO_REG_OFFSET, reg_val - 1);

	/* THDSTA */
	reg_val = (timing_reg_values[index].thdsta * clk_in_mhz) / 1000;
	xiic_setreg32(i2c, XIIC_THDSTA_REG_OFFSET, reg_val - 1);

	/* TSUDAT */
	reg_val = (timing_reg_values[index].tsudat * clk_in_mhz) / 1000;
	xiic_setreg32(i2c, XIIC_TSUDAT_REG_OFFSET, reg_val - 1);

	/* TBUF */
	reg_val = (timing_reg_values[index].tbuf * clk_in_mhz) / 1000;
	xiic_setreg32(i2c, XIIC_TBUF_REG_OFFSET, reg_val - 1);

	/* THDDAT */
	xiic_setreg32(i2c, XIIC_THDDAT_REG_OFFSET, 1);

	return 0;
}

static int xiic_reinit(struct xiic_i2c *i2c)
{
	int ret;

	xiic_setreg32(i2c, XIIC_RESETR_OFFSET, XIIC_RESET_MASK);

	ret = xiic_setclk(i2c);
	if (ret)
		return ret;

	/* Set receive Fifo depth to maximum (zero based). */
	xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, IIC_RX_FIFO_DEPTH - 1);

	/* Reset Tx Fifo. */
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);

	/* Enable IIC Device, remove Tx Fifo reset & disable general call. */
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);

	/* make sure RX fifo is empty */
	ret = xiic_clear_rx_fifo(i2c);
	if (ret)
		return ret;

	/* Enable interrupts */
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, XIIC_GINTR_ENABLE_MASK);

	xiic_irq_clr_en(i2c, XIIC_INTR_ARB_LOST_MASK);

	return 0;
}

static void xiic_deinit(struct xiic_i2c *i2c)
{
	u8 cr;

	xiic_setreg32(i2c, XIIC_RESETR_OFFSET, XIIC_RESET_MASK);

	/* Disable IIC Device. */
	cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
	xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr & ~XIIC_CR_ENABLE_DEVICE_MASK);
}

static void xiic_smbus_block_read_setup(struct xiic_i2c *i2c)
{
	u8 rxmsg_len, rfd_set = 0;

	/*
	 * Clear the I2C_M_RECV_LEN flag to avoid setting
	 * message length again
	 */
	i2c->rx_msg->flags &= ~I2C_M_RECV_LEN;

	/* Set smbus_block_read flag to identify in isr */
	i2c->smbus_block_read = true;

	/* Read byte from rx fifo and set message length */
	rxmsg_len = xiic_getreg8(i2c, XIIC_DRR_REG_OFFSET);

	i2c->rx_msg->buf[i2c->rx_pos++] = rxmsg_len;

	/* Check if received length is valid */
	if (rxmsg_len <= I2C_SMBUS_BLOCK_MAX) {
		/* Set Receive fifo depth */
		if (rxmsg_len > IIC_RX_FIFO_DEPTH) {
			/*
			 * When Rx msg len greater than or equal to Rx fifo capacity
			 * Receive fifo depth should set to Rx fifo capacity minus 1
			 */
			rfd_set = IIC_RX_FIFO_DEPTH - 1;
			i2c->rx_msg->len = rxmsg_len + 1;
		} else if ((rxmsg_len == 1) ||
			(rxmsg_len == 0)) {
			/*
			 * Minimum of 3 bytes required to exit cleanly. 1 byte
			 * already received, Second byte is being received. Have
			 * to set NACK in read_rx before receiving the last byte
			 */
			rfd_set = 0;
			i2c->rx_msg->len = SMBUS_BLOCK_READ_MIN_LEN;
		} else {
			/*
			 * When Rx msg len less than Rx fifo capacity
			 * Receive fifo depth should set to Rx msg len minus 2
			 */
			rfd_set = rxmsg_len - 2;
			i2c->rx_msg->len = rxmsg_len + 1;
		}
		xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, rfd_set);

		return;
	}

	/* Invalid message length, trigger STATE_ERROR with tx_msg_len in ISR */
	i2c->tx_msg->len = 3;
	i2c->smbus_block_read = false;
	dev_err(i2c->adap.dev.parent, "smbus_block_read Invalid msg length\n");
}

static void xiic_read_rx(struct xiic_i2c *i2c)
{
	u8 bytes_in_fifo, cr = 0, bytes_to_read = 0;
	u32 bytes_rem = 0;
	int i;

	bytes_in_fifo = xiic_getreg8(i2c, XIIC_RFO_REG_OFFSET) + 1;

	dev_dbg(i2c->adap.dev.parent,
		"%s entry, bytes in fifo: %d, rem: %d, SR: 0x%x, CR: 0x%x\n",
		__func__, bytes_in_fifo, xiic_rx_space(i2c),
		xiic_getreg8(i2c, XIIC_SR_REG_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

	if (bytes_in_fifo > xiic_rx_space(i2c))
		bytes_in_fifo = xiic_rx_space(i2c);

	bytes_to_read = bytes_in_fifo;

	if (!i2c->dynamic) {
		bytes_rem = xiic_rx_space(i2c) - bytes_in_fifo;

		/* Set msg length if smbus_block_read */
		if (i2c->rx_msg->flags & I2C_M_RECV_LEN) {
			xiic_smbus_block_read_setup(i2c);
			return;
		}

		if (bytes_rem > IIC_RX_FIFO_DEPTH) {
			bytes_to_read = bytes_in_fifo;
		} else if (bytes_rem > 1) {
			bytes_to_read = bytes_rem - 1;
		} else if (bytes_rem == 1) {
			bytes_to_read = 1;
			/* Set NACK in CR to indicate slave transmitter */
			cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr |
					XIIC_CR_NO_ACK_MASK);
		} else if (bytes_rem == 0) {
			bytes_to_read = bytes_in_fifo;

			/* Generate stop on the bus if it is last message */
			if (i2c->nmsgs == 1) {
				cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
				xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr &
						~XIIC_CR_MSMS_MASK);
			}

			/* Make TXACK=0, clean up for next transaction */
			cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr &
					~XIIC_CR_NO_ACK_MASK);
		}
	}

	/* Read the fifo */
	for (i = 0; i < bytes_to_read; i++) {
		i2c->rx_msg->buf[i2c->rx_pos++] =
			xiic_getreg8(i2c, XIIC_DRR_REG_OFFSET);
	}

	if (i2c->dynamic) {
		u8 bytes;

		/* Receive remaining bytes if less than fifo depth */
		bytes = min_t(u8, xiic_rx_space(i2c), IIC_RX_FIFO_DEPTH);
		bytes--;
		xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, bytes);
	}
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

		if (!xiic_tx_space(i2c) && i2c->nmsgs == 1) {
			/* last message in transfer -> STOP */
			if (i2c->dynamic) {
				data |= XIIC_TX_DYN_STOP_MASK;
			} else {
				u8 cr;
				int status;

				/* Wait till FIFO is empty so STOP is sent last */
				status = xiic_wait_tx_empty(i2c);
				if (status)
					return;

				/* Write to CR to stop */
				cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
				xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr &
					     ~XIIC_CR_MSMS_MASK);
			}
			dev_dbg(i2c->adap.dev.parent, "%s TX STOP\n", __func__);
		}
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, data);
	}
}

static void xiic_wakeup(struct xiic_i2c *i2c, enum xilinx_i2c_state code)
{
	i2c->tx_msg = NULL;
	i2c->rx_msg = NULL;
	i2c->nmsgs = 0;
	i2c->state = code;
	complete(&i2c->completion);
}

static irqreturn_t xiic_process(int irq, void *dev_id)
{
	struct xiic_i2c *i2c = dev_id;
	u32 pend, isr, ier;
	u32 clr = 0;
	int xfer_more = 0;
	int wakeup_req = 0;
	enum xilinx_i2c_state wakeup_code = STATE_DONE;
	int ret;

	/* Get the interrupt Status from the IPIF. There is no clearing of
	 * interrupts in the IPIF. Interrupts must be cleared at the source.
	 * To find which interrupts are pending; AND interrupts pending with
	 * interrupts masked.
	 */
	mutex_lock(&i2c->lock);
	isr = xiic_getreg32(i2c, XIIC_IISR_OFFSET);
	ier = xiic_getreg32(i2c, XIIC_IIER_OFFSET);
	pend = isr & ier;

	dev_dbg(i2c->adap.dev.parent, "%s: IER: 0x%x, ISR: 0x%x, pend: 0x%x\n",
		__func__, ier, isr, pend);
	dev_dbg(i2c->adap.dev.parent, "%s: SR: 0x%x, msg: %p, nmsgs: %d\n",
		__func__, xiic_getreg8(i2c, XIIC_SR_REG_OFFSET),
		i2c->tx_msg, i2c->nmsgs);
	dev_dbg(i2c->adap.dev.parent, "%s, ISR: 0x%x, CR: 0x%x\n",
		__func__, xiic_getreg32(i2c, XIIC_IISR_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

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
		ret = xiic_reinit(i2c);
		if (!ret)
			dev_dbg(i2c->adap.dev.parent, "reinit failed\n");

		if (i2c->rx_msg) {
			wakeup_req = 1;
			wakeup_code = STATE_ERROR;
		}
		if (i2c->tx_msg) {
			wakeup_req = 1;
			wakeup_code = STATE_ERROR;
		}
	}
	if (pend & XIIC_INTR_RX_FULL_MASK) {
		/* Receive register/FIFO is full */

		clr |= XIIC_INTR_RX_FULL_MASK;
		if (!i2c->rx_msg) {
			dev_dbg(i2c->adap.dev.parent,
				"%s unexpected RX IRQ\n", __func__);
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
				xfer_more = 1;
			}
		}
	}
	if (pend & (XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_HALF_MASK)) {
		/* Transmit register/FIFO is empty or ½ empty */

		clr |= (pend &
			(XIIC_INTR_TX_EMPTY_MASK | XIIC_INTR_TX_HALF_MASK));

		if (!i2c->tx_msg) {
			dev_dbg(i2c->adap.dev.parent,
				"%s unexpected TX IRQ\n", __func__);
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
				xfer_more = 1;
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
	}

	if (pend & XIIC_INTR_BNB_MASK) {
		/* IIC bus has transitioned to not busy */
		clr |= XIIC_INTR_BNB_MASK;

		/* The bus is not busy, disable BusNotBusy interrupt */
		xiic_irq_dis(i2c, XIIC_INTR_BNB_MASK);

		if (i2c->tx_msg && i2c->smbus_block_read) {
			i2c->smbus_block_read = false;
			/* Set requested message len=1 to indicate STATE_DONE */
			i2c->tx_msg->len = 1;
		}

		if (!i2c->tx_msg)
			goto out;

		wakeup_req = 1;

		if (i2c->nmsgs == 1 && !i2c->rx_msg &&
		    xiic_tx_space(i2c) == 0)
			wakeup_code = STATE_DONE;
		else
			wakeup_code = STATE_ERROR;
	}

out:
	dev_dbg(i2c->adap.dev.parent, "%s clr: 0x%x\n", __func__, clr);

	xiic_setreg32(i2c, XIIC_IISR_OFFSET, clr);
	if (xfer_more)
		__xiic_start_xfer(i2c);
	if (wakeup_req)
		xiic_wakeup(i2c, wakeup_code);

	WARN_ON(xfer_more && wakeup_req);

	mutex_unlock(&i2c->lock);
	return IRQ_HANDLED;
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

	if (i2c->tx_msg || i2c->rx_msg)
		return -EBUSY;

	/* In single master mode bus can only be busy, when in use by this
	 * driver. If the register indicates bus being busy for some reason we
	 * should ignore it, since bus will never be released and i2c will be
	 * stuck forever.
	 */
	if (i2c->singlemaster) {
		return 0;
	}

	/* for instance if previous transfer was terminated due to TX error
	 * it might be that the bus is on it's way to become available
	 * give it at most 3 ms to wake
	 */
	err = xiic_bus_busy(i2c);
	while (err && tries--) {
		msleep(1);
		err = xiic_bus_busy(i2c);
	}

	return err;
}

static void xiic_start_recv(struct xiic_i2c *i2c)
{
	u16 rx_watermark;
	u8 cr = 0, rfd_set = 0;
	struct i2c_msg *msg = i2c->rx_msg = i2c->tx_msg;

	dev_dbg(i2c->adap.dev.parent, "%s entry, ISR: 0x%x, CR: 0x%x\n",
		__func__, xiic_getreg32(i2c, XIIC_IISR_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

	/* Disable Tx interrupts */
	xiic_irq_dis(i2c, XIIC_INTR_TX_HALF_MASK | XIIC_INTR_TX_EMPTY_MASK);

	if (i2c->dynamic) {
		u8 bytes;
		u16 val;

		/* Clear and enable Rx full interrupt. */
		xiic_irq_clr_en(i2c, XIIC_INTR_RX_FULL_MASK |
				XIIC_INTR_TX_ERROR_MASK);

		/*
		 * We want to get all but last byte, because the TX_ERROR IRQ
		 * is used to indicate error ACK on the address, and
		 * negative ack on the last received byte, so to not mix
		 * them receive all but last.
		 * In the case where there is only one byte to receive
		 * we can check if ERROR and RX full is set at the same time
		 */
		rx_watermark = msg->len;
		bytes = min_t(u8, rx_watermark, IIC_RX_FIFO_DEPTH);

		if (rx_watermark > 0)
			bytes--;
		xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, bytes);

		/* write the address */
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET,
			      i2c_8bit_addr_from_msg(msg) |
			      XIIC_TX_DYN_START_MASK);

		/* If last message, include dynamic stop bit with length */
		val = (i2c->nmsgs == 1) ? XIIC_TX_DYN_STOP_MASK : 0;
		val |= msg->len;

		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, val);

		xiic_irq_clr_en(i2c, XIIC_INTR_BNB_MASK);
	} else {
		/*
		 * If previous message is Tx, make sure that Tx FIFO is empty
		 * before starting a new transfer as the repeated start in
		 * standard mode can corrupt the transaction if there are
		 * still bytes to be transmitted in FIFO
		 */
		if (i2c->prev_msg_tx) {
			int status;

			status = xiic_wait_tx_empty(i2c);
			if (status)
				return;
		}

		cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);

		/* Set Receive fifo depth */
		rx_watermark = msg->len;
		if (rx_watermark > IIC_RX_FIFO_DEPTH) {
			rfd_set = IIC_RX_FIFO_DEPTH - 1;
		} else if (rx_watermark == 1) {
			rfd_set = rx_watermark - 1;

			/* Set No_ACK, except for smbus_block_read */
			if (!(i2c->rx_msg->flags & I2C_M_RECV_LEN)) {
				/* Handle single byte transfer separately */
				cr |= XIIC_CR_NO_ACK_MASK;
			}
		} else if (rx_watermark == 0) {
			rfd_set = rx_watermark;
		} else {
			rfd_set = rx_watermark - 2;
		}
		/* Check if RSTA should be set */
		if (cr & XIIC_CR_MSMS_MASK) {
			/* Already a master, RSTA should be set */
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, (cr |
					XIIC_CR_REPEATED_START_MASK) &
					~(XIIC_CR_DIR_IS_TX_MASK));
		}

		xiic_setreg8(i2c, XIIC_RFD_REG_OFFSET, rfd_set);

		/* Clear and enable Rx full and transmit complete interrupts */
		xiic_irq_clr_en(i2c, XIIC_INTR_RX_FULL_MASK |
				XIIC_INTR_TX_ERROR_MASK);

		/* Write the address */
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET,
			      i2c_8bit_addr_from_msg(msg));

		/* Write to Control Register,to start transaction in Rx mode */
		if ((cr & XIIC_CR_MSMS_MASK) == 0) {
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, (cr |
					XIIC_CR_MSMS_MASK)
					& ~(XIIC_CR_DIR_IS_TX_MASK));
		}
		dev_dbg(i2c->adap.dev.parent, "%s end, ISR: 0x%x, CR: 0x%x\n",
			__func__, xiic_getreg32(i2c, XIIC_IISR_OFFSET),
			xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));
	}

	if (i2c->nmsgs == 1)
		/* very last, enable bus not busy as well */
		xiic_irq_clr_en(i2c, XIIC_INTR_BNB_MASK);

	/* the message is tx:ed */
	i2c->tx_pos = msg->len;

	/* Enable interrupts */
	xiic_setreg32(i2c, XIIC_DGIER_OFFSET, XIIC_GINTR_ENABLE_MASK);

	i2c->prev_msg_tx = false;
}

static void xiic_start_send(struct xiic_i2c *i2c)
{
	u8 cr = 0;
	u16 data;
	struct i2c_msg *msg = i2c->tx_msg;

	dev_dbg(i2c->adap.dev.parent, "%s entry, msg: %p, len: %d",
		__func__, msg, msg->len);
	dev_dbg(i2c->adap.dev.parent, "%s entry, ISR: 0x%x, CR: 0x%x\n",
		__func__, xiic_getreg32(i2c, XIIC_IISR_OFFSET),
		xiic_getreg8(i2c, XIIC_CR_REG_OFFSET));

	if (i2c->dynamic) {
		/* write the address */
		data = i2c_8bit_addr_from_msg(msg) |
				XIIC_TX_DYN_START_MASK;

		if (i2c->nmsgs == 1 && msg->len == 0)
			/* no data and last message -> add STOP */
			data |= XIIC_TX_DYN_STOP_MASK;

		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, data);

		/* Clear any pending Tx empty, Tx Error and then enable them */
		xiic_irq_clr_en(i2c, XIIC_INTR_TX_EMPTY_MASK |
				XIIC_INTR_TX_ERROR_MASK |
				XIIC_INTR_BNB_MASK |
				((i2c->nmsgs > 1 || xiic_tx_space(i2c)) ?
				XIIC_INTR_TX_HALF_MASK : 0));

		xiic_fill_tx_fifo(i2c);
	} else {
		/*
		 * If previous message is Tx, make sure that Tx FIFO is empty
		 * before starting a new transfer as the repeated start in
		 * standard mode can corrupt the transaction if there are
		 * still bytes to be transmitted in FIFO
		 */
		if (i2c->prev_msg_tx) {
			int status;

			status = xiic_wait_tx_empty(i2c);
			if (status)
				return;
		}
		/* Check if RSTA should be set */
		cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
		if (cr & XIIC_CR_MSMS_MASK) {
			/* Already a master, RSTA should be set */
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, (cr |
					XIIC_CR_REPEATED_START_MASK |
					XIIC_CR_DIR_IS_TX_MASK) &
					~(XIIC_CR_NO_ACK_MASK));
		}

		/* Write address to FIFO */
		data = i2c_8bit_addr_from_msg(msg);
		xiic_setreg16(i2c, XIIC_DTR_REG_OFFSET, data);

		/* Fill fifo */
		xiic_fill_tx_fifo(i2c);

		if ((cr & XIIC_CR_MSMS_MASK) == 0) {
			/* Start Tx by writing to CR */
			cr = xiic_getreg8(i2c, XIIC_CR_REG_OFFSET);
			xiic_setreg8(i2c, XIIC_CR_REG_OFFSET, cr |
					XIIC_CR_MSMS_MASK |
					XIIC_CR_DIR_IS_TX_MASK);
		}

		/* Clear any pending Tx empty, Tx Error and then enable them */
		xiic_irq_clr_en(i2c, XIIC_INTR_TX_EMPTY_MASK |
				XIIC_INTR_TX_ERROR_MASK |
				XIIC_INTR_BNB_MASK);
	}
	i2c->prev_msg_tx = true;
}

static void __xiic_start_xfer(struct xiic_i2c *i2c)
{
	int fifo_space = xiic_tx_fifo_space(i2c);

	dev_dbg(i2c->adap.dev.parent, "%s entry, msg: %p, fifos space: %d\n",
		__func__, i2c->tx_msg, fifo_space);

	if (!i2c->tx_msg)
		return;

	i2c->rx_pos = 0;
	i2c->tx_pos = 0;
	i2c->state = STATE_START;
	if (i2c->tx_msg->flags & I2C_M_RD) {
		/* we dont date putting several reads in the FIFO */
		xiic_start_recv(i2c);
	} else {
		xiic_start_send(i2c);
	}
}

static int xiic_start_xfer(struct xiic_i2c *i2c, struct i2c_msg *msgs, int num)
{
	bool broken_read, max_read_len, smbus_blk_read;
	int ret, count;

	mutex_lock(&i2c->lock);

	ret = xiic_busy(i2c);
	if (ret)
		goto out;

	i2c->tx_msg = msgs;
	i2c->rx_msg = NULL;
	i2c->nmsgs = num;
	init_completion(&i2c->completion);

	/* Decide standard mode or Dynamic mode */
	i2c->dynamic = true;

	/* Initialize prev message type */
	i2c->prev_msg_tx = false;

	/*
	 * Scan through nmsgs, use dynamic mode when none of the below three
	 * conditions occur. We need standard mode even if one condition holds
	 * true in the entire array of messages in a single transfer.
	 * If read transaction as dynamic mode is broken for delayed reads
	 * in xlnx,axi-iic-2.0 / xlnx,xps-iic-2.00.a IP versions.
	 * If read length is > 255 bytes.
	 * If smbus_block_read transaction.
	 */
	for (count = 0; count < i2c->nmsgs; count++) {
		broken_read = (i2c->quirks & DYNAMIC_MODE_READ_BROKEN_BIT) &&
				(i2c->tx_msg[count].flags & I2C_M_RD);
		max_read_len = (i2c->tx_msg[count].flags & I2C_M_RD) &&
				(i2c->tx_msg[count].len > MAX_READ_LENGTH_DYNAMIC);
		smbus_blk_read = (i2c->tx_msg[count].flags & I2C_M_RECV_LEN);

		if (broken_read || max_read_len || smbus_blk_read) {
			i2c->dynamic = false;
			break;
		}
	}

	ret = xiic_reinit(i2c);
	if (!ret)
		__xiic_start_xfer(i2c);

out:
	mutex_unlock(&i2c->lock);

	return ret;
}

static int xiic_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct xiic_i2c *i2c = i2c_get_adapdata(adap);
	int err;

	dev_dbg(adap->dev.parent, "%s entry SR: 0x%x\n", __func__,
		xiic_getreg8(i2c, XIIC_SR_REG_OFFSET));

	err = pm_runtime_resume_and_get(i2c->dev);
	if (err < 0)
		return err;

	err = xiic_start_xfer(i2c, msgs, num);
	if (err < 0) {
		dev_err(adap->dev.parent, "Error xiic_start_xfer\n");
		goto out;
	}

	err = wait_for_completion_timeout(&i2c->completion, XIIC_XFER_TIMEOUT);
	mutex_lock(&i2c->lock);
	if (err == 0) {	/* Timeout */
		i2c->tx_msg = NULL;
		i2c->rx_msg = NULL;
		i2c->nmsgs = 0;
		err = -ETIMEDOUT;
	} else {
		err = (i2c->state == STATE_DONE) ? num : -EIO;
	}
	mutex_unlock(&i2c->lock);

out:
	pm_runtime_mark_last_busy(i2c->dev);
	pm_runtime_put_autosuspend(i2c->dev);
	return err;
}

static u32 xiic_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm xiic_algorithm = {
	.master_xfer = xiic_xfer,
	.functionality = xiic_func,
};

static const struct i2c_adapter xiic_adapter = {
	.owner = THIS_MODULE,
	.class = I2C_CLASS_DEPRECATED,
	.algo = &xiic_algorithm,
};

#if defined(CONFIG_OF)
static const struct xiic_version_data xiic_2_00 = {
	.quirks = DYNAMIC_MODE_READ_BROKEN_BIT,
};

static const struct of_device_id xiic_of_match[] = {
	{ .compatible = "xlnx,xps-iic-2.00.a", .data = &xiic_2_00 },
	{ .compatible = "xlnx,axi-iic-2.1", },
	{},
};
MODULE_DEVICE_TABLE(of, xiic_of_match);
#endif

static int xiic_i2c_probe(struct platform_device *pdev)
{
	struct xiic_i2c *i2c;
	struct xiic_i2c_platform_data *pdata;
	const struct of_device_id *match;
	struct resource *res;
	int ret, irq;
	u8 i;
	u32 sr;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	match = of_match_node(xiic_of_match, pdev->dev.of_node);
	if (match && match->data) {
		const struct xiic_version_data *data = match->data;

		i2c->quirks = data->quirks;
	}

	i2c->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
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
	snprintf(i2c->adap.name, sizeof(i2c->adap.name),
		 DRIVER_NAME " %s", pdev->name);

	mutex_init(&i2c->lock);

	i2c->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c->clk),
				     "failed to enable input clock.\n");

	i2c->dev = &pdev->dev;
	pm_runtime_set_autosuspend_delay(i2c->dev, XIIC_PM_TIMEOUT);
	pm_runtime_use_autosuspend(i2c->dev);
	pm_runtime_set_active(i2c->dev);
	pm_runtime_enable(i2c->dev);

	/* SCL frequency configuration */
	i2c->input_clk = clk_get_rate(i2c->clk);
	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
				   &i2c->i2c_clk);
	/* If clock-frequency not specified in DT, do not configure in SW */
	if (ret || i2c->i2c_clk > I2C_MAX_FAST_MODE_PLUS_FREQ)
		i2c->i2c_clk = 0;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					xiic_process, IRQF_ONESHOT,
					pdev->name, i2c);

	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		goto err_pm_disable;
	}

	i2c->singlemaster =
		of_property_read_bool(pdev->dev.of_node, "single-master");

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

	ret = xiic_reinit(i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot xiic_reinit\n");
		goto err_pm_disable;
	}

	/* add i2c adapter to i2c tree */
	ret = i2c_add_adapter(&i2c->adap);
	if (ret) {
		xiic_deinit(i2c);
		goto err_pm_disable;
	}

	if (pdata) {
		/* add in known devices to the bus */
		for (i = 0; i < pdata->num_devices; i++)
			i2c_new_client_device(&i2c->adap, pdata->devices + i);
	}

	dev_dbg(&pdev->dev, "mmio %08lx irq %d scl clock frequency %d\n",
		(unsigned long)res->start, irq, i2c->i2c_clk);

	return 0;

err_pm_disable:
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void xiic_i2c_remove(struct platform_device *pdev)
{
	struct xiic_i2c *i2c = platform_get_drvdata(pdev);
	int ret;

	/* remove adapter & data */
	i2c_del_adapter(&i2c->adap);

	ret = pm_runtime_get_sync(i2c->dev);

	if (ret < 0)
		dev_warn(&pdev->dev, "Failed to activate device for removal (%pe)\n",
			 ERR_PTR(ret));
	else
		xiic_deinit(i2c);

	pm_runtime_put_sync(i2c->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
}

static int __maybe_unused xiic_i2c_runtime_suspend(struct device *dev)
{
	struct xiic_i2c *i2c = dev_get_drvdata(dev);

	clk_disable(i2c->clk);

	return 0;
}

static int __maybe_unused xiic_i2c_runtime_resume(struct device *dev)
{
	struct xiic_i2c *i2c = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(i2c->clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops xiic_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(xiic_i2c_runtime_suspend,
			   xiic_i2c_runtime_resume, NULL)
};

static struct platform_driver xiic_i2c_driver = {
	.probe   = xiic_i2c_probe,
	.remove_new = xiic_i2c_remove,
	.driver  = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(xiic_of_match),
		.pm = &xiic_dev_pm_ops,
	},
};

module_platform_driver(xiic_i2c_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("info@mocean-labs.com");
MODULE_DESCRIPTION("Xilinx I2C bus driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip PCI1XXXX I2C adapter driver for PCIe Switch
 * which has I2C controller in one of its downstream functions
 *
 * Copyright (C) 2021 - 2022 Microchip Technology Inc.
 *
 * Authors: Tharun Kumar P <tharunkumar.pasumarthi@microchip.com>
 *          Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#define SMBUS_MAST_CORE_ADDR_BASE		0x00000
#define SMBUS_MAST_SYS_REG_ADDR_BASE		0x01000

/* SMB register space. */
#define SMB_CORE_CTRL_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x00)

#define SMB_CORE_CTRL_ESO		BIT(6)
#define SMB_CORE_CTRL_FW_ACK		BIT(4)
#define SMB_CORE_CTRL_ACK		BIT(0)

#define SMB_CORE_CMD_REG_OFF3	(SMBUS_MAST_CORE_ADDR_BASE + 0x0F)
#define SMB_CORE_CMD_REG_OFF2	(SMBUS_MAST_CORE_ADDR_BASE + 0x0E)
#define SMB_CORE_CMD_REG_OFF1	(SMBUS_MAST_CORE_ADDR_BASE + 0x0D)

#define SMB_CORE_CMD_READM		BIT(4)
#define SMB_CORE_CMD_STOP		BIT(2)
#define SMB_CORE_CMD_START		BIT(0)

#define SMB_CORE_CMD_REG_OFF0	(SMBUS_MAST_CORE_ADDR_BASE + 0x0C)

#define SMB_CORE_CMD_M_PROCEED		BIT(1)
#define SMB_CORE_CMD_M_RUN		BIT(0)

#define SMB_CORE_SR_HOLD_TIME_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x18)

/*
 * SR_HOLD_TIME_XK_TICKS field will indicate the number of ticks of the
 * baud clock required to program 'Hold Time' at X KHz.
 */
#define SR_HOLD_TIME_100K_TICKS		150
#define SR_HOLD_TIME_400K_TICKS		20
#define SR_HOLD_TIME_1000K_TICKS	12

#define SMB_CORE_COMPLETION_REG_OFF3	(SMBUS_MAST_CORE_ADDR_BASE + 0x23)

#define COMPLETION_MDONE		BIT(6)
#define COMPLETION_IDLE			BIT(5)
#define COMPLETION_MNAKX		BIT(0)

#define SMB_CORE_IDLE_SCALING_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x24)

/*
 * FAIR_BUS_IDLE_MIN_XK_TICKS field will indicate the number of ticks of
 * the baud clock required to program 'fair idle delay' at X KHz. Fair idle
 * delay establishes the MCTP T(IDLE_DELAY) period.
 */
#define FAIR_BUS_IDLE_MIN_100K_TICKS		992
#define FAIR_BUS_IDLE_MIN_400K_TICKS		500
#define FAIR_BUS_IDLE_MIN_1000K_TICKS		500

/*
 * FAIR_IDLE_DELAY_XK_TICKS field will indicate the number of ticks of the
 * baud clock required to satisfy the fairness protocol at X KHz.
 */
#define FAIR_IDLE_DELAY_100K_TICKS	963
#define FAIR_IDLE_DELAY_400K_TICKS	156
#define FAIR_IDLE_DELAY_1000K_TICKS	156

#define SMB_IDLE_SCALING_100K		\
	((FAIR_IDLE_DELAY_100K_TICKS << 16) | FAIR_BUS_IDLE_MIN_100K_TICKS)
#define SMB_IDLE_SCALING_400K		\
	((FAIR_IDLE_DELAY_400K_TICKS << 16) | FAIR_BUS_IDLE_MIN_400K_TICKS)
#define SMB_IDLE_SCALING_1000K	\
	((FAIR_IDLE_DELAY_1000K_TICKS << 16) | FAIR_BUS_IDLE_MIN_1000K_TICKS)

#define SMB_CORE_CONFIG_REG3		(SMBUS_MAST_CORE_ADDR_BASE + 0x2B)

#define SMB_CONFIG3_ENMI		BIT(6)
#define SMB_CONFIG3_ENIDI		BIT(5)

#define SMB_CORE_CONFIG_REG2		(SMBUS_MAST_CORE_ADDR_BASE + 0x2A)
#define SMB_CORE_CONFIG_REG1		(SMBUS_MAST_CORE_ADDR_BASE + 0x29)

#define SMB_CONFIG1_ASR			BIT(7)
#define SMB_CONFIG1_ENAB		BIT(2)
#define SMB_CONFIG1_RESET		BIT(1)
#define SMB_CONFIG1_FEN			BIT(0)

#define SMB_CORE_BUS_CLK_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x2C)

/*
 * BUS_CLK_XK_LOW_PERIOD_TICKS field defines the number of I2C Baud Clock
 * periods that make up the low phase of the I2C/SMBus bus clock at X KHz.
 */
#define BUS_CLK_100K_LOW_PERIOD_TICKS		156
#define BUS_CLK_400K_LOW_PERIOD_TICKS		41
#define BUS_CLK_1000K_LOW_PERIOD_TICKS		15

/*
 * BUS_CLK_XK_HIGH_PERIOD_TICKS field defines the number of I2C Baud Clock
 * periods that make up the high phase of the I2C/SMBus bus clock at X KHz.
 */
#define BUS_CLK_100K_HIGH_PERIOD_TICKS	154
#define BUS_CLK_400K_HIGH_PERIOD_TICKS	35
#define BUS_CLK_1000K_HIGH_PERIOD_TICKS	14

#define BUS_CLK_100K			\
	((BUS_CLK_100K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_100K_LOW_PERIOD_TICKS)
#define BUS_CLK_400K			\
	((BUS_CLK_400K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_400K_LOW_PERIOD_TICKS)
#define BUS_CLK_1000K			\
	((BUS_CLK_1000K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_1000K_LOW_PERIOD_TICKS)

#define SMB_CORE_CLK_SYNC_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x3C)

/*
 * CLK_SYNC_XK defines the number of clock cycles to sync up to the external
 * clock before comparing the internal and external clocks for clock stretching
 * at X KHz.
 */
#define CLK_SYNC_100K			4
#define CLK_SYNC_400K			4
#define CLK_SYNC_1000K			4

#define SMB_CORE_DATA_TIMING_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x40)

/*
 *
 * FIRST_START_HOLD_XK_TICKS will indicate the number of ticks of the baud
 * clock required to program 'FIRST_START_HOLD' timer at X KHz. This timer
 * determines the SCLK hold time following SDAT driven low during the first
 * START bit in a transfer.
 */
#define FIRST_START_HOLD_100K_TICKS	23
#define FIRST_START_HOLD_400K_TICKS	8
#define FIRST_START_HOLD_1000K_TICKS	12

/*
 * STOP_SETUP_XK_TICKS will indicate the number of ticks of the baud clock
 * required to program 'STOP_SETUP' timer at X KHz. This timer determines the
 * SDAT setup time from the rising edge of SCLK for a STOP condition.
 */
#define STOP_SETUP_100K_TICKS		150
#define STOP_SETUP_400K_TICKS		20
#define STOP_SETUP_1000K_TICKS		12

/*
 * RESTART_SETUP_XK_TICKS will indicate the number of ticks of the baud clock
 * required to program 'RESTART_SETUP' timer at X KHz. This timer determines the
 * SDAT setup time from the rising edge of SCLK for a repeated START condition.
 */
#define RESTART_SETUP_100K_TICKS	156
#define RESTART_SETUP_400K_TICKS	20
#define RESTART_SETUP_1000K_TICKS	12

/*
 * DATA_HOLD_XK_TICKS will indicate the number of ticks of the baud clock
 * required to program 'DATA_HOLD' timer at X KHz. This timer determines the
 * SDAT hold time following SCLK driven low.
 */
#define DATA_HOLD_100K_TICKS		12
#define DATA_HOLD_400K_TICKS		2
#define DATA_HOLD_1000K_TICKS		2

#define DATA_TIMING_100K		\
	((FIRST_START_HOLD_100K_TICKS << 24) | (STOP_SETUP_100K_TICKS << 16) | \
	(RESTART_SETUP_100K_TICKS << 8) | DATA_HOLD_100K_TICKS)
#define DATA_TIMING_400K		\
	((FIRST_START_HOLD_400K_TICKS << 24) | (STOP_SETUP_400K_TICKS << 16) | \
	(RESTART_SETUP_400K_TICKS << 8) | DATA_HOLD_400K_TICKS)
#define DATA_TIMING_1000K		\
	((FIRST_START_HOLD_1000K_TICKS << 24) | (STOP_SETUP_1000K_TICKS << 16) | \
	(RESTART_SETUP_1000K_TICKS << 8) | DATA_HOLD_1000K_TICKS)

#define SMB_CORE_TO_SCALING_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x44)

/*
 * BUS_IDLE_MIN_XK_TICKS defines Bus Idle Minimum Time.
 * Bus Idle Minimum time = BUS_IDLE_MIN[7:0] x Baud_Clock_Period x
 * (BUS_IDLE_MIN_XK_TICKS[7] ? 4,1)
 */
#define BUS_IDLE_MIN_100K_TICKS		36UL
#define BUS_IDLE_MIN_400K_TICKS		10UL
#define BUS_IDLE_MIN_1000K_TICKS	4UL

/*
 * CTRL_CUM_TIME_OUT_XK_TICKS defines SMBus Controller Cumulative Time-Out.
 * SMBus Controller Cumulative Time-Out duration =
 * CTRL_CUM_TIME_OUT_XK_TICKS[7:0] x Baud_Clock_Period x 2048
 */
#define CTRL_CUM_TIME_OUT_100K_TICKS		76
#define CTRL_CUM_TIME_OUT_400K_TICKS		76
#define CTRL_CUM_TIME_OUT_1000K_TICKS		76

/*
 * TARGET_CUM_TIME_OUT_XK_TICKS defines SMBus Target Cumulative Time-Out duration.
 * SMBus Target Cumulative Time-Out duration = TARGET_CUM_TIME_OUT_XK_TICKS[7:0] x
 * Baud_Clock_Period x 4096
 */
#define TARGET_CUM_TIME_OUT_100K_TICKS	95
#define TARGET_CUM_TIME_OUT_400K_TICKS	95
#define TARGET_CUM_TIME_OUT_1000K_TICKS	95

/*
 * CLOCK_HIGH_TIME_OUT_XK defines Clock High time out period.
 * Clock High time out period = CLOCK_HIGH_TIME_OUT_XK[7:0] x Baud_Clock_Period x 8
 */
#define CLOCK_HIGH_TIME_OUT_100K_TICKS	97
#define CLOCK_HIGH_TIME_OUT_400K_TICKS	97
#define CLOCK_HIGH_TIME_OUT_1000K_TICKS	97

#define TO_SCALING_100K		\
	((BUS_IDLE_MIN_100K_TICKS << 24) | (CTRL_CUM_TIME_OUT_100K_TICKS << 16) | \
	(TARGET_CUM_TIME_OUT_100K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_100K_TICKS)
#define TO_SCALING_400K		\
	((BUS_IDLE_MIN_400K_TICKS << 24) | (CTRL_CUM_TIME_OUT_400K_TICKS << 16) | \
	(TARGET_CUM_TIME_OUT_400K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_400K_TICKS)
#define TO_SCALING_1000K		\
	((BUS_IDLE_MIN_1000K_TICKS << 24) | (CTRL_CUM_TIME_OUT_1000K_TICKS << 16) | \
	(TARGET_CUM_TIME_OUT_1000K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_1000K_TICKS)

#define I2C_SCL_PAD_CTRL_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x100)
#define I2C_SDA_PAD_CTRL_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x101)

#define I2C_FOD_EN			BIT(4)
#define I2C_PULL_UP_EN			BIT(3)
#define I2C_PULL_DOWN_EN		BIT(2)
#define I2C_INPUT_EN			BIT(1)
#define I2C_OUTPUT_EN			BIT(0)

#define SMBUS_CONTROL_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x200)

#define CTL_RESET_COUNTERS		BIT(3)
#define CTL_TRANSFER_DIR		BIT(2)
#define CTL_HOST_FIFO_ENTRY		BIT(1)
#define CTL_RUN				BIT(0)

#define I2C_DIRN_WRITE			0
#define I2C_DIRN_READ			1

#define SMBUS_STATUS_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x204)

#define STA_DMA_TERM			BIT(7)
#define STA_DMA_REQ			BIT(6)
#define STA_THRESHOLD			BIT(2)
#define STA_BUF_FULL			BIT(1)
#define STA_BUF_EMPTY			BIT(0)

#define SMBUS_INTR_STAT_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x208)

#define INTR_STAT_DMA_TERM		BIT(7)
#define INTR_STAT_THRESHOLD		BIT(2)
#define INTR_STAT_BUF_FULL		BIT(1)
#define INTR_STAT_BUF_EMPTY		BIT(0)

#define SMBUS_INTR_MSK_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x20C)

#define INTR_MSK_DMA_TERM		BIT(7)
#define INTR_MSK_THRESHOLD		BIT(2)
#define INTR_MSK_BUF_FULL		BIT(1)
#define INTR_MSK_BUF_EMPTY		BIT(0)

#define ALL_NW_LAYER_INTERRUPTS  \
	(INTR_MSK_DMA_TERM | INTR_MSK_THRESHOLD | INTR_MSK_BUF_FULL | \
	 INTR_MSK_BUF_EMPTY)

#define SMBUS_MCU_COUNTER_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x214)

#define SMBALERT_MST_PAD_CTRL_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x230)

#define SMBALERT_MST_PU			BIT(0)

#define SMBUS_GEN_INT_STAT_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x23C)

#define SMBUS_GEN_INT_MASK_REG_OFF	(SMBUS_MAST_CORE_ADDR_BASE + 0x240)

#define SMBALERT_INTR_MASK		BIT(10)
#define I2C_BUF_MSTR_INTR_MASK		BIT(9)
#define I2C_INTR_MASK			BIT(8)
#define SMBALERT_WAKE_INTR_MASK		BIT(2)
#define I2C_BUF_MSTR_WAKE_INTR_MASK	BIT(1)
#define I2C_WAKE_INTR_MASK		BIT(0)

#define ALL_HIGH_LAYER_INTR     \
	(SMBALERT_INTR_MASK | I2C_BUF_MSTR_INTR_MASK | I2C_INTR_MASK | \
	SMBALERT_WAKE_INTR_MASK | I2C_BUF_MSTR_WAKE_INTR_MASK | \
	I2C_WAKE_INTR_MASK)

#define SMBUS_RESET_REG		(SMBUS_MAST_CORE_ADDR_BASE + 0x248)

#define PERI_SMBUS_D3_RESET_DIS		BIT(16)

#define SMBUS_MST_BUF		(SMBUS_MAST_CORE_ADDR_BASE + 0x280)

#define SMBUS_BUF_MAX_SIZE		0x80

#define I2C_FLAGS_DIRECT_MODE		BIT(7)
#define I2C_FLAGS_POLLING_MODE		BIT(6)
#define I2C_FLAGS_STOP			BIT(5)
#define I2C_FLAGS_SMB_BLK_READ		BIT(4)

#define PCI1XXXX_I2C_TIMEOUT_MS		1000

/* General Purpose Register. */
#define SMB_GPR_REG		(SMBUS_MAST_CORE_ADDR_BASE + 0x1000 + 0x0c00 + \
				0x00)

/* Lock Register. */
#define SMB_GPR_LOCK_REG	(SMBUS_MAST_CORE_ADDR_BASE + 0x1000 + 0x0000 + \
				0x00A0)

#define SMBUS_PERI_LOCK		BIT(3)

struct pci1xxxx_i2c {
	struct completion i2c_xfer_done;
	bool i2c_xfer_in_progress;
	struct i2c_adapter adap;
	void __iomem *i2c_base;
	u32 freq;
	u32 flags;
};

static int set_sys_lock(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMB_GPR_LOCK_REG;
	u8 data;

	writel(SMBUS_PERI_LOCK, p);
	data = readl(p);
	if (data != SMBUS_PERI_LOCK)
		return -EPERM;

	return 0;
}

static int release_sys_lock(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMB_GPR_LOCK_REG;
	u8 data;

	data = readl(p);
	if (data != SMBUS_PERI_LOCK)
		return 0;

	writel(0, p);
	data = readl(p);
	if (data & SMBUS_PERI_LOCK)
		return -EPERM;

	return 0;
}

static void pci1xxxx_ack_high_level_intr(struct pci1xxxx_i2c *i2c, u16 intr_msk)
{
	writew(intr_msk, i2c->i2c_base + SMBUS_GEN_INT_STAT_REG_OFF);
}

static void pci1xxxx_i2c_configure_smbalert_pin(struct pci1xxxx_i2c *i2c,
						bool enable)
{
	void __iomem *p = i2c->i2c_base + SMBALERT_MST_PAD_CTRL_REG_OFF;
	u8 regval;

	regval = readb(p);

	if (enable)
		regval |= SMBALERT_MST_PU;
	else
		regval &= ~SMBALERT_MST_PU;

	writeb(regval, p);
}

static void pci1xxxx_i2c_send_start_stop(struct pci1xxxx_i2c *i2c, bool start)
{
	void __iomem *p = i2c->i2c_base + SMB_CORE_CMD_REG_OFF1;
	u8 regval;

	regval = readb(p);

	if (start)
		regval |= SMB_CORE_CMD_START;
	else
		regval |= SMB_CORE_CMD_STOP;

	writeb(regval, p);
}

/*
 * When accessing the core control reg, we should not do a read modified write
 * as they are write '1' to clear bits. Instead we need to write with the
 * specific bits that needs to be set.
 */
static void pci1xxxx_i2c_set_clear_FW_ACK(struct pci1xxxx_i2c *i2c, bool set)
{
	u8 regval;

	if (set)
		regval = SMB_CORE_CTRL_FW_ACK | SMB_CORE_CTRL_ESO | SMB_CORE_CTRL_ACK;
	else
		regval = SMB_CORE_CTRL_ESO | SMB_CORE_CTRL_ACK;

	writeb(regval, i2c->i2c_base + SMB_CORE_CTRL_REG_OFF);
}

static void pci1xxxx_i2c_buffer_write(struct pci1xxxx_i2c *i2c, u8 slaveaddr,
				      u8 transferlen, unsigned char *buf)
{
	void __iomem *p = i2c->i2c_base + SMBUS_MST_BUF;

	if (slaveaddr)
		writeb(slaveaddr, p++);

	if (buf)
		memcpy_toio(p, buf, transferlen);
}

/*
 * When accessing the core control reg, we should not do a read modified write
 * as there are write '1' to clear bits. Instead we need to write with the
 * specific bits that needs to be set.
 */
static void pci1xxxx_i2c_enable_ESO(struct pci1xxxx_i2c *i2c)
{
	writeb(SMB_CORE_CTRL_ESO, i2c->i2c_base + SMB_CORE_CTRL_REG_OFF);
}

static void pci1xxxx_i2c_reset_counters(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMBUS_CONTROL_REG_OFF;
	u8 regval;

	regval = readb(p);
	regval |= CTL_RESET_COUNTERS;
	writeb(regval, p);
}

static void pci1xxxx_i2c_set_transfer_dir(struct pci1xxxx_i2c *i2c, u8 direction)
{
	void __iomem *p = i2c->i2c_base + SMBUS_CONTROL_REG_OFF;
	u8 regval;

	regval = readb(p);
	if (direction == I2C_DIRN_WRITE)
		regval &= ~CTL_TRANSFER_DIR;
	else
		regval |= CTL_TRANSFER_DIR;

	writeb(regval, p);
}

static void pci1xxxx_i2c_set_mcu_count(struct pci1xxxx_i2c *i2c, u8 count)
{
	writeb(count, i2c->i2c_base + SMBUS_MCU_COUNTER_REG_OFF);
}

static void pci1xxxx_i2c_set_read_count(struct pci1xxxx_i2c *i2c, u8 readcount)
{
	writeb(readcount, i2c->i2c_base + SMB_CORE_CMD_REG_OFF3);
}

static void pci1xxxx_i2c_set_write_count(struct pci1xxxx_i2c *i2c, u8 writecount)
{
	writeb(writecount, i2c->i2c_base + SMB_CORE_CMD_REG_OFF2);
}

static void pci1xxxx_i2c_set_DMA_run(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMBUS_CONTROL_REG_OFF;
	u8 regval;

	regval = readb(p);
	regval |= CTL_RUN;
	writeb(regval, p);
}

static void pci1xxxx_i2c_set_mrun_proceed(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMB_CORE_CMD_REG_OFF0;
	u8 regval;

	regval = readb(p);
	regval |= SMB_CORE_CMD_M_RUN;
	regval |= SMB_CORE_CMD_M_PROCEED;
	writeb(regval, p);
}

static void pci1xxxx_i2c_start_DMA(struct pci1xxxx_i2c *i2c)
{
	pci1xxxx_i2c_set_DMA_run(i2c);
	pci1xxxx_i2c_set_mrun_proceed(i2c);
}

static void pci1xxxx_i2c_config_asr(struct pci1xxxx_i2c *i2c, bool enable)
{
	void __iomem *p = i2c->i2c_base + SMB_CORE_CONFIG_REG1;
	u8 regval;

	regval = readb(p);
	if (enable)
		regval |= SMB_CONFIG1_ASR;
	else
		regval &= ~SMB_CONFIG1_ASR;
	writeb(regval, p);
}

static irqreturn_t pci1xxxx_i2c_isr(int irq, void *dev)
{
	struct pci1xxxx_i2c *i2c = dev;
	void __iomem *p1 = i2c->i2c_base + SMBUS_GEN_INT_STAT_REG_OFF;
	void __iomem *p2 = i2c->i2c_base + SMBUS_INTR_STAT_REG_OFF;
	irqreturn_t intr_handled = IRQ_NONE;
	u16 reg1;
	u8 reg3;

	/*
	 *  Read the SMBus interrupt status register to see if the
	 *  DMA_TERM interrupt has caused this callback.
	 */
	reg1 = readw(p1);

	if (reg1 & I2C_BUF_MSTR_INTR_MASK) {
		reg3 = readb(p2);
		if (reg3 & INTR_STAT_DMA_TERM) {
			complete(&i2c->i2c_xfer_done);
			intr_handled = IRQ_HANDLED;
			writeb(INTR_STAT_DMA_TERM, p2);
		}
		pci1xxxx_ack_high_level_intr(i2c, I2C_BUF_MSTR_INTR_MASK);
	}

	if (reg1 & SMBALERT_INTR_MASK) {
		intr_handled = IRQ_HANDLED;
		pci1xxxx_ack_high_level_intr(i2c, SMBALERT_INTR_MASK);
	}

	return intr_handled;
}

static void pci1xxxx_i2c_set_count(struct pci1xxxx_i2c *i2c, u8 mcucount,
				   u8 writecount, u8 readcount)
{
	pci1xxxx_i2c_set_mcu_count(i2c, mcucount);
	pci1xxxx_i2c_set_write_count(i2c, writecount);
	pci1xxxx_i2c_set_read_count(i2c, readcount);
}

static void pci1xxxx_i2c_set_readm(struct pci1xxxx_i2c *i2c, bool enable)
{
	void __iomem *p = i2c->i2c_base + SMB_CORE_CMD_REG_OFF1;
	u8 regval;

	regval = readb(p);
	if (enable)
		regval |= SMB_CORE_CMD_READM;
	else
		regval &= ~SMB_CORE_CMD_READM;

	writeb(regval, p);
}

static void pci1xxxx_ack_nw_layer_intr(struct pci1xxxx_i2c *i2c, u8 ack_intr_msk)
{
	writeb(ack_intr_msk, i2c->i2c_base + SMBUS_INTR_STAT_REG_OFF);
}

static void pci1xxxx_config_nw_layer_intr(struct pci1xxxx_i2c *i2c,
					  u8 intr_msk, bool enable)
{
	void __iomem *p = i2c->i2c_base + SMBUS_INTR_MSK_REG_OFF;
	u8 regval;

	regval = readb(p);
	if (enable)
		regval &= ~intr_msk;
	else
		regval |= intr_msk;

	writeb(regval, p);
}

static void pci1xxxx_i2c_config_padctrl(struct pci1xxxx_i2c *i2c, bool enable)
{
	void __iomem *p1 = i2c->i2c_base + I2C_SCL_PAD_CTRL_REG_OFF;
	void __iomem *p2 = i2c->i2c_base + I2C_SDA_PAD_CTRL_REG_OFF;
	u8 regval;

	regval = readb(p1);
	if (enable)
		regval |= I2C_INPUT_EN | I2C_OUTPUT_EN;
	else
		regval &= ~(I2C_INPUT_EN | I2C_OUTPUT_EN);

	writeb(regval, p1);

	regval = readb(p2);
	if (enable)
		regval |= I2C_INPUT_EN | I2C_OUTPUT_EN;
	else
		regval &= ~(I2C_INPUT_EN | I2C_OUTPUT_EN);

	writeb(regval, p2);
}

static void pci1xxxx_i2c_set_mode(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p = i2c->i2c_base + SMBUS_CONTROL_REG_OFF;
	u8 regval;

	regval = readb(p);
	if (i2c->flags & I2C_FLAGS_DIRECT_MODE)
		regval &= ~CTL_HOST_FIFO_ENTRY;
	else
		regval |= CTL_HOST_FIFO_ENTRY;

	writeb(regval, p);
}

static void pci1xxxx_i2c_config_high_level_intr(struct pci1xxxx_i2c *i2c,
						u16 intr_msk, bool enable)
{
	void __iomem *p = i2c->i2c_base + SMBUS_GEN_INT_MASK_REG_OFF;
	u16 regval;

	regval = readw(p);
	if (enable)
		regval &= ~intr_msk;
	else
		regval |= intr_msk;
	writew(regval, p);
}

static void pci1xxxx_i2c_configure_core_reg(struct pci1xxxx_i2c *i2c, bool enable)
{
	void __iomem *p1 = i2c->i2c_base + SMB_CORE_CONFIG_REG1;
	void __iomem *p3 = i2c->i2c_base + SMB_CORE_CONFIG_REG3;
	u8 reg1;
	u8 reg3;

	reg1 = readb(p1);
	reg3 = readb(p3);
	if (enable) {
		reg1 |= SMB_CONFIG1_ENAB | SMB_CONFIG1_FEN;
		reg3 |= SMB_CONFIG3_ENMI | SMB_CONFIG3_ENIDI;
	} else {
		reg1 &= ~(SMB_CONFIG1_ENAB | SMB_CONFIG1_FEN);
		reg3 &= ~(SMB_CONFIG3_ENMI | SMB_CONFIG3_ENIDI);
	}

	writeb(reg1, p1);
	writeb(reg3, p3);
}

static void pci1xxxx_i2c_set_freq(struct pci1xxxx_i2c *i2c)
{
	void __iomem *bp = i2c->i2c_base;
	void __iomem *p_idle_scaling = bp + SMB_CORE_IDLE_SCALING_REG_OFF;
	void __iomem *p_data_timing = bp + SMB_CORE_DATA_TIMING_REG_OFF;
	void __iomem *p_hold_time = bp + SMB_CORE_SR_HOLD_TIME_REG_OFF;
	void __iomem *p_to_scaling = bp + SMB_CORE_TO_SCALING_REG_OFF;
	void __iomem *p_clk_sync = bp + SMB_CORE_CLK_SYNC_REG_OFF;
	void __iomem *p_clk_reg = bp + SMB_CORE_BUS_CLK_REG_OFF;

	switch (i2c->freq) {
	case I2C_MAX_STANDARD_MODE_FREQ:
		writeb(SR_HOLD_TIME_100K_TICKS, p_hold_time);
		writel(SMB_IDLE_SCALING_100K, p_idle_scaling);
		writew(BUS_CLK_100K, p_clk_reg);
		writel(CLK_SYNC_100K, p_clk_sync);
		writel(DATA_TIMING_100K, p_data_timing);
		writel(TO_SCALING_100K, p_to_scaling);
		break;

	case I2C_MAX_FAST_MODE_PLUS_FREQ:
		writeb(SR_HOLD_TIME_1000K_TICKS, p_hold_time);
		writel(SMB_IDLE_SCALING_1000K, p_idle_scaling);
		writew(BUS_CLK_1000K, p_clk_reg);
		writel(CLK_SYNC_1000K, p_clk_sync);
		writel(DATA_TIMING_1000K, p_data_timing);
		writel(TO_SCALING_1000K, p_to_scaling);
		break;

	case I2C_MAX_FAST_MODE_FREQ:
	default:
		writeb(SR_HOLD_TIME_400K_TICKS, p_hold_time);
		writel(SMB_IDLE_SCALING_400K, p_idle_scaling);
		writew(BUS_CLK_400K, p_clk_reg);
		writel(CLK_SYNC_400K, p_clk_sync);
		writel(DATA_TIMING_400K, p_data_timing);
		writel(TO_SCALING_400K, p_to_scaling);
		break;
	}
}

static void pci1xxxx_i2c_init(struct pci1xxxx_i2c *i2c)
{
	void __iomem *p2 = i2c->i2c_base + SMBUS_STATUS_REG_OFF;
	void __iomem *p1 = i2c->i2c_base + SMB_GPR_REG;
	u8 regval;
	int ret;

	ret = set_sys_lock(i2c);
	if (ret == -EPERM) {
		/*
		 * Configure I2C Fast Mode as default frequency if unable
		 * to acquire sys lock.
		 */
		regval = 0;
	} else {
		regval = readl(p1);
		release_sys_lock(i2c);
	}

	switch (regval) {
	case 0:
		i2c->freq = I2C_MAX_FAST_MODE_FREQ;
		pci1xxxx_i2c_set_freq(i2c);
		break;
	case 1:
		i2c->freq = I2C_MAX_STANDARD_MODE_FREQ;
		pci1xxxx_i2c_set_freq(i2c);
		break;
	case 2:
		i2c->freq = I2C_MAX_FAST_MODE_PLUS_FREQ;
		pci1xxxx_i2c_set_freq(i2c);
		break;
	case 3:
	default:
		break;
	}

	pci1xxxx_i2c_config_padctrl(i2c, true);
	i2c->flags |= I2C_FLAGS_DIRECT_MODE;
	pci1xxxx_i2c_set_mode(i2c);

	/*
	 * Added as a precaution since BUF_EMPTY in status register
	 * also trigered an Interrupt.
	 */
	writeb(STA_BUF_EMPTY, p2);

	/* Configure core I2c control registers. */
	pci1xxxx_i2c_configure_core_reg(i2c, true);

	/*
	 * Enable pull-up for the SMB alert pin which is just used for
	 * wakeup right now.
	 */
	pci1xxxx_i2c_configure_smbalert_pin(i2c, true);
}

static void pci1xxxx_i2c_clear_flags(struct pci1xxxx_i2c *i2c)
{
	u8 regval;

	/* Reset the internal buffer counters. */
	pci1xxxx_i2c_reset_counters(i2c);

	/* Clear low level interrupts. */
	regval = COMPLETION_MNAKX | COMPLETION_IDLE | COMPLETION_MDONE;
	writeb(regval, i2c->i2c_base + SMB_CORE_COMPLETION_REG_OFF3);
	reinit_completion(&i2c->i2c_xfer_done);
	pci1xxxx_ack_nw_layer_intr(i2c, ALL_NW_LAYER_INTERRUPTS);
	pci1xxxx_ack_high_level_intr(i2c, ALL_HIGH_LAYER_INTR);
}

static int pci1xxxx_i2c_read(struct pci1xxxx_i2c *i2c, u8 slaveaddr,
			     unsigned char *buf, u16 total_len)
{
	void __iomem *p2 = i2c->i2c_base + SMB_CORE_COMPLETION_REG_OFF3;
	void __iomem *p1 = i2c->i2c_base + SMB_CORE_CMD_REG_OFF1;
	void __iomem *p3 = i2c->i2c_base + SMBUS_MST_BUF;
	unsigned long time_left;
	u16 remainingbytes;
	u8 transferlen;
	int retval = 0;
	u8 read_count;
	u32 regval;
	u16 count;

	/* Enable I2C host controller by setting the ESO bit in the CONTROL REG. */
	pci1xxxx_i2c_enable_ESO(i2c);
	pci1xxxx_i2c_clear_flags(i2c);
	pci1xxxx_config_nw_layer_intr(i2c, INTR_MSK_DMA_TERM, true);
	pci1xxxx_i2c_config_high_level_intr(i2c, I2C_BUF_MSTR_INTR_MASK, true);

	/*
	 * The I2C transfer could be more than 128 bytes. Our Core is
	 * capable of only sending 128 at a time.
	 * As far as the I2C read is concerned, initailly send the
	 * read slave address along with the number of bytes to read in
	 * ReadCount. After sending the slave address the interrupt
	 * is generated. On seeing the ACK for the slave address, reverse the
	 * buffer direction and run the DMA to initiate Read from slave.
	 */
	for (count = 0; count < total_len; count += transferlen) {

		/*
		 * Before start of any transaction clear the existing
		 * START/STOP conditions.
		 */
		writeb(0, p1);
		remainingbytes = total_len - count;
		transferlen = min_t(u16, remainingbytes, SMBUS_BUF_MAX_SIZE);

		/*
		 * Send STOP bit for the last chunk in the transaction.
		 * For I2C read transaction of more than BUF_SIZE, NACK should
		 * only be sent for the last read.
		 * Hence a bit FW_ACK is set for all the read chunks except for
		 * the last chunk. For the last chunk NACK should be sent and
		 * FW_ACK is cleared Send STOP only when I2C_FLAGS_STOP bit is
		 * set in the flags and only for the last transaction.
		 */
		if ((count + transferlen >= total_len) &&
		    (i2c->flags & I2C_FLAGS_STOP)) {
			pci1xxxx_i2c_set_clear_FW_ACK(i2c, false);
			pci1xxxx_i2c_send_start_stop(i2c, 0);
		} else {
			pci1xxxx_i2c_set_clear_FW_ACK(i2c, true);
		}

		/* Send START bit for the first transaction. */
		if (count == 0) {
			pci1xxxx_i2c_set_transfer_dir(i2c, I2C_DIRN_WRITE);
			pci1xxxx_i2c_send_start_stop(i2c, 1);

			/* Write I2c buffer with just the slave addr. */
			pci1xxxx_i2c_buffer_write(i2c, slaveaddr, 0, NULL);

			/* Set the count. Readcount is the transfer bytes. */
			pci1xxxx_i2c_set_count(i2c, 1, 1, transferlen);

			/*
			 * Set the Auto_start_read bit so that the HW itself
			 * will take care of the read phase.
			 */
			pci1xxxx_i2c_config_asr(i2c, true);
			if (i2c->flags & I2C_FLAGS_SMB_BLK_READ)
				pci1xxxx_i2c_set_readm(i2c, true);
		} else {
			pci1xxxx_i2c_set_count(i2c, 0, 0, transferlen);
			pci1xxxx_i2c_config_asr(i2c, false);
			pci1xxxx_i2c_clear_flags(i2c);
			pci1xxxx_i2c_set_transfer_dir(i2c, I2C_DIRN_READ);
		}

		/* Start the DMA. */
		pci1xxxx_i2c_start_DMA(i2c);

		/* Wait for the DMA_TERM interrupt. */
		time_left = wait_for_completion_timeout(&i2c->i2c_xfer_done,
			    msecs_to_jiffies(PCI1XXXX_I2C_TIMEOUT_MS));
		if (time_left == 0) {
			/* Reset the I2C core to release the bus lock. */
			pci1xxxx_i2c_init(i2c);
			retval = -ETIMEDOUT;
			goto cleanup;
		}

		/* Read the completion reg to know the reason for DMA_TERM. */
		regval = readb(p2);

		/* Slave did not respond. */
		if (regval & COMPLETION_MNAKX) {
			writeb(COMPLETION_MNAKX, p2);
			retval = -ETIMEDOUT;
			goto cleanup;
		}

		if (i2c->flags & I2C_FLAGS_SMB_BLK_READ) {
			buf[0] = readb(p3);
			read_count = buf[0];
			memcpy_fromio(&buf[1], p3 + 1, read_count);
		} else {
			memcpy_fromio(&buf[count], p3, transferlen);
		}
	}

cleanup:
	/* Disable all the interrupts. */
	pci1xxxx_config_nw_layer_intr(i2c, INTR_MSK_DMA_TERM, false);
	pci1xxxx_i2c_config_high_level_intr(i2c, I2C_BUF_MSTR_INTR_MASK, false);
	pci1xxxx_i2c_config_asr(i2c, false);
	return retval;
}

static int pci1xxxx_i2c_write(struct pci1xxxx_i2c *i2c, u8 slaveaddr,
			      unsigned char *buf, u16 total_len)
{
	void __iomem *p2 = i2c->i2c_base + SMB_CORE_COMPLETION_REG_OFF3;
	void __iomem *p1 = i2c->i2c_base + SMB_CORE_CMD_REG_OFF1;
	unsigned long time_left;
	u16 remainingbytes;
	u8 actualwritelen;
	u8 transferlen;
	int retval = 0;
	u32 regval;
	u16 count;

	/* Enable I2C host controller by setting the ESO bit in the CONTROL REG. */
	pci1xxxx_i2c_enable_ESO(i2c);

	/* Set the Buffer direction. */
	pci1xxxx_i2c_set_transfer_dir(i2c, I2C_DIRN_WRITE);
	pci1xxxx_config_nw_layer_intr(i2c, INTR_MSK_DMA_TERM, true);
	pci1xxxx_i2c_config_high_level_intr(i2c, I2C_BUF_MSTR_INTR_MASK, true);

	/*
	 * The i2c transfer could be more than 128 bytes. Our Core is
	 * capable of only sending 128 at a time.
	 */
	for (count = 0; count < total_len; count += transferlen) {
		/*
		 * Before start of any transaction clear the existing
		 * START/STOP conditions.
		 */
		writeb(0, p1);
		pci1xxxx_i2c_clear_flags(i2c);
		remainingbytes = total_len - count;

		/* If it is the starting of the transaction send START. */
		if (count == 0) {
			pci1xxxx_i2c_send_start_stop(i2c, 1);

			/* -1 for the slave address. */
			transferlen = min_t(u16, SMBUS_BUF_MAX_SIZE - 1,
					    remainingbytes);
			pci1xxxx_i2c_buffer_write(i2c, slaveaddr,
						  transferlen, &buf[count]);
			/*
			 * The actual number of bytes written on the I2C bus
			 * is including the slave address.
			 */
			actualwritelen = transferlen + 1;
		} else {
			transferlen = min_t(u16, SMBUS_BUF_MAX_SIZE, remainingbytes);
			pci1xxxx_i2c_buffer_write(i2c, 0, transferlen, &buf[count]);
			actualwritelen = transferlen;
		}

		pci1xxxx_i2c_set_count(i2c, actualwritelen, actualwritelen, 0);

		/*
		 * Send STOP only when I2C_FLAGS_STOP bit is set in the flags and
		 * only for the last transaction.
		 */
		if (remainingbytes <= transferlen &&
		   (i2c->flags & I2C_FLAGS_STOP))
			pci1xxxx_i2c_send_start_stop(i2c, 0);

		pci1xxxx_i2c_start_DMA(i2c);

		/*
		 * Wait for the DMA_TERM interrupt.
		 */
		time_left = wait_for_completion_timeout(&i2c->i2c_xfer_done,
			    msecs_to_jiffies(PCI1XXXX_I2C_TIMEOUT_MS));
		if (time_left == 0) {
			/* Reset the I2C core to release the bus lock. */
			pci1xxxx_i2c_init(i2c);
			retval = -ETIMEDOUT;
			goto cleanup;
		}

		regval = readb(p2);
		if (regval & COMPLETION_MNAKX) {
			writeb(COMPLETION_MNAKX, p2);
			retval = -ETIMEDOUT;
			goto cleanup;
		}
	}
cleanup:
	/* Disable all the interrupts. */
	pci1xxxx_config_nw_layer_intr(i2c, INTR_MSK_DMA_TERM, false);
	pci1xxxx_i2c_config_high_level_intr(i2c, I2C_BUF_MSTR_INTR_MASK, false);

	return retval;
}

static int pci1xxxx_i2c_xfer(struct i2c_adapter *adap,
			     struct i2c_msg *msgs, int num)
{
	struct pci1xxxx_i2c *i2c = i2c_get_adapdata(adap);
	u8 slaveaddr;
	int retval;
	u32 i;

	i2c->i2c_xfer_in_progress = true;
	for (i = 0; i < num; i++) {
		slaveaddr = i2c_8bit_addr_from_msg(&msgs[i]);

		/*
		 * Send the STOP bit if the transfer is the final one or
		 * if the I2C_M_STOP flag is set.
		 */
		if ((i == num - 1) || (msgs[i].flags & I2C_M_STOP))
			i2c->flags |= I2C_FLAGS_STOP;
		else
			i2c->flags &= ~I2C_FLAGS_STOP;

		if (msgs[i].flags & I2C_M_RECV_LEN)
			i2c->flags |= I2C_FLAGS_SMB_BLK_READ;
		else
			i2c->flags &= ~I2C_FLAGS_SMB_BLK_READ;

		if (msgs[i].flags & I2C_M_RD)
			retval = pci1xxxx_i2c_read(i2c, slaveaddr,
						   msgs[i].buf, msgs[i].len);
		else
			retval = pci1xxxx_i2c_write(i2c, slaveaddr,
						    msgs[i].buf, msgs[i].len);

		if (retval < 0)
			break;
	}
	i2c->i2c_xfer_in_progress = false;

	if (retval < 0)
		return retval;

	return num;
}

/*
 * List of supported functions by the driver.
 */
static u32 pci1xxxx_i2c_get_funcs(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_PROTOCOL_MANGLING |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_PROC_CALL |
		I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm pci1xxxx_i2c_algo = {
	.master_xfer = pci1xxxx_i2c_xfer,
	.functionality = pci1xxxx_i2c_get_funcs,
};

static const struct i2c_adapter_quirks pci1xxxx_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static const struct i2c_adapter pci1xxxx_i2c_ops = {
	.owner	= THIS_MODULE,
	.name	= "PCI1xxxx I2C Adapter",
	.algo	= &pci1xxxx_i2c_algo,
	.quirks = &pci1xxxx_i2c_quirks,
};

static int pci1xxxx_i2c_suspend(struct device *dev)
{
	struct pci1xxxx_i2c *i2c = dev_get_drvdata(dev);
	void __iomem *p = i2c->i2c_base + SMBUS_RESET_REG;
	struct pci_dev *pdev = to_pci_dev(dev);
	u32 regval;

	i2c_mark_adapter_suspended(&i2c->adap);

	/*
	 * If the system is put into 'suspend' state when the I2C transfer is in
	 * progress, wait until the transfer completes.
	 */
	while (i2c->i2c_xfer_in_progress)
		msleep(20);

	pci1xxxx_i2c_config_high_level_intr(i2c, SMBALERT_WAKE_INTR_MASK, true);

	/*
	 * Enable the PERST_DIS bit to mask the PERST from resetting the core
	 * registers.
	 */
	regval = readl(p);
	regval |= PERI_SMBUS_D3_RESET_DIS;
	writel(regval, p);

	/* Enable PCI wake in the PMCSR register. */
	device_set_wakeup_enable(dev, true);
	pci_wake_from_d3(pdev, true);

	return 0;
}

static int pci1xxxx_i2c_resume(struct device *dev)
{
	struct pci1xxxx_i2c *i2c = dev_get_drvdata(dev);
	void __iomem *p1 = i2c->i2c_base + SMBUS_GEN_INT_STAT_REG_OFF;
	void __iomem *p2 = i2c->i2c_base + SMBUS_RESET_REG;
	struct pci_dev *pdev = to_pci_dev(dev);
	u32 regval;

	regval = readw(p1);
	writew(regval, p1);
	pci1xxxx_i2c_config_high_level_intr(i2c, SMBALERT_WAKE_INTR_MASK, false);
	regval = readl(p2);
	regval &= ~PERI_SMBUS_D3_RESET_DIS;
	writel(regval, p2);
	i2c_mark_adapter_resumed(&i2c->adap);
	pci_wake_from_d3(pdev, false);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pci1xxxx_i2c_pm_ops, pci1xxxx_i2c_suspend,
			 pci1xxxx_i2c_resume);

static void pci1xxxx_i2c_shutdown(void *data)
{
	struct pci1xxxx_i2c *i2c = data;

	pci1xxxx_i2c_config_padctrl(i2c, false);
	pci1xxxx_i2c_configure_core_reg(i2c, false);
}

static int pci1xxxx_i2c_probe_pci(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct pci1xxxx_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	pci_set_drvdata(pdev, i2c);
	i2c->i2c_xfer_in_progress = false;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	/*
	 * We are getting the base address of the SMB core. SMB core uses
	 * BAR0 and size is 32K.
	 */
	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret < 0)
		return ret;

	i2c->i2c_base =	pcim_iomap_table(pdev)[0];
	init_completion(&i2c->i2c_xfer_done);
	pci1xxxx_i2c_init(i2c);

	ret = devm_add_action(dev, pci1xxxx_i2c_shutdown, i2c);
	if (ret)
		return ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, pci_irq_vector(pdev, 0), pci1xxxx_i2c_isr,
			       0, pci_name(pdev), i2c);
	if (ret)
		return ret;

	i2c->adap = pci1xxxx_i2c_ops;
	i2c->adap.dev.parent = dev;

	snprintf(i2c->adap.name, sizeof(i2c->adap.name),
		 "MCHP PCI1xxxx i2c adapter at %s", pci_name(pdev));

	i2c_set_adapdata(&i2c->adap, i2c);

	ret = devm_i2c_add_adapter(dev, &i2c->adap);
	if (ret)
		return dev_err_probe(dev, ret, "i2c add adapter failed\n");

	return 0;
}

static const struct pci_device_id pci1xxxx_i2c_pci_id_table[] = {
	{ PCI_VDEVICE(EFAR, 0xA003) },
	{ PCI_VDEVICE(EFAR, 0xA013) },
	{ PCI_VDEVICE(EFAR, 0xA023) },
	{ PCI_VDEVICE(EFAR, 0xA033) },
	{ PCI_VDEVICE(EFAR, 0xA043) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pci1xxxx_i2c_pci_id_table);

static struct pci_driver pci1xxxx_i2c_pci_driver = {
	.name		= "i2c-mchp-pci1xxxx",
	.id_table	= pci1xxxx_i2c_pci_id_table,
	.probe		= pci1xxxx_i2c_probe_pci,
	.driver = {
		.pm = pm_sleep_ptr(&pci1xxxx_i2c_pm_ops),
	},
};
module_pci_driver(pci1xxxx_i2c_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tharun Kumar P<tharunkumar.pasumarthi@microchip.com>");
MODULE_AUTHOR("Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>");
MODULE_DESCRIPTION("Microchip Technology Inc. pci1xxxx I2C bus driver");

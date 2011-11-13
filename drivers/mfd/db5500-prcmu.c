/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 *
 * U5500 PRCM Unit interface driver
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/db5500-regs.h>
#include "dbx500-prcmu-regs.h"

#define _PRCM_MB_HEADER (tcdm_base + 0xFE8)
#define PRCM_REQ_MB0_HEADER (_PRCM_MB_HEADER + 0x0)
#define PRCM_REQ_MB1_HEADER (_PRCM_MB_HEADER + 0x1)
#define PRCM_REQ_MB2_HEADER (_PRCM_MB_HEADER + 0x2)
#define PRCM_REQ_MB3_HEADER (_PRCM_MB_HEADER + 0x3)
#define PRCM_REQ_MB4_HEADER (_PRCM_MB_HEADER + 0x4)
#define PRCM_REQ_MB5_HEADER (_PRCM_MB_HEADER + 0x5)
#define PRCM_REQ_MB6_HEADER (_PRCM_MB_HEADER + 0x6)
#define PRCM_REQ_MB7_HEADER (_PRCM_MB_HEADER + 0x7)
#define PRCM_ACK_MB0_HEADER (_PRCM_MB_HEADER + 0x8)
#define PRCM_ACK_MB1_HEADER (_PRCM_MB_HEADER + 0x9)
#define PRCM_ACK_MB2_HEADER (_PRCM_MB_HEADER + 0xa)
#define PRCM_ACK_MB3_HEADER (_PRCM_MB_HEADER + 0xb)
#define PRCM_ACK_MB4_HEADER (_PRCM_MB_HEADER + 0xc)
#define PRCM_ACK_MB5_HEADER (_PRCM_MB_HEADER + 0xd)
#define PRCM_ACK_MB6_HEADER (_PRCM_MB_HEADER + 0xe)
#define PRCM_ACK_MB7_HEADER (_PRCM_MB_HEADER + 0xf)

/* Req Mailboxes */
#define PRCM_REQ_MB0 (tcdm_base + 0xFD8)
#define PRCM_REQ_MB1 (tcdm_base + 0xFCC)
#define PRCM_REQ_MB2 (tcdm_base + 0xFC4)
#define PRCM_REQ_MB3 (tcdm_base + 0xFC0)
#define PRCM_REQ_MB4 (tcdm_base + 0xF98)
#define PRCM_REQ_MB5 (tcdm_base + 0xF90)
#define PRCM_REQ_MB6 (tcdm_base + 0xF8C)
#define PRCM_REQ_MB7 (tcdm_base + 0xF84)

/* Ack Mailboxes */
#define PRCM_ACK_MB0 (tcdm_base + 0xF38)
#define PRCM_ACK_MB1 (tcdm_base + 0xF30)
#define PRCM_ACK_MB2 (tcdm_base + 0xF24)
#define PRCM_ACK_MB3 (tcdm_base + 0xF20)
#define PRCM_ACK_MB4 (tcdm_base + 0xF1C)
#define PRCM_ACK_MB5 (tcdm_base + 0xF14)
#define PRCM_ACK_MB6 (tcdm_base + 0xF0C)
#define PRCM_ACK_MB7 (tcdm_base + 0xF08)

enum mb_return_code {
	RC_SUCCESS,
	RC_FAIL,
};

/* Mailbox 0 headers. */
enum mb0_header {
	/* request */
	RMB0H_PWR_STATE_TRANS = 1,
	RMB0H_WAKE_UP_CFG,
	RMB0H_RD_WAKE_UP_ACK,
	/* acknowledge */
	AMB0H_WAKE_UP = 1,
};

/* Mailbox 5 headers. */
enum mb5_header {
	MB5H_I2C_WRITE = 1,
	MB5H_I2C_READ,
};

/* Request mailbox 5 fields. */
#define PRCM_REQ_MB5_I2C_SLAVE (PRCM_REQ_MB5 + 0)
#define PRCM_REQ_MB5_I2C_REG (PRCM_REQ_MB5 + 1)
#define PRCM_REQ_MB5_I2C_SIZE (PRCM_REQ_MB5 + 2)
#define PRCM_REQ_MB5_I2C_DATA (PRCM_REQ_MB5 + 4)

/* Acknowledge mailbox 5 fields. */
#define PRCM_ACK_MB5_RETURN_CODE (PRCM_ACK_MB5 + 0)
#define PRCM_ACK_MB5_I2C_DATA (PRCM_ACK_MB5 + 4)

#define NUM_MB 8
#define MBOX_BIT BIT
#define ALL_MBOX_BITS (MBOX_BIT(NUM_MB) - 1)

/*
* Used by MCDE to setup all necessary PRCMU registers
*/
#define PRCMU_RESET_DSIPLL			0x00004000
#define PRCMU_UNCLAMP_DSIPLL			0x00400800

/* HDMI CLK MGT PLLSW=001 (PLLSOC0), PLLDIV=0x8, = 50 Mhz*/
#define PRCMU_DSI_CLOCK_SETTING			0x00000128
/* TVCLK_MGT PLLSW=001 (PLLSOC0) PLLDIV=0x13, = 19.05 MHZ */
#define PRCMU_DSI_LP_CLOCK_SETTING		0x00000135
#define PRCMU_PLLDSI_FREQ_SETTING		0x00020121
#define PRCMU_DSI_PLLOUT_SEL_SETTING		0x00000002
#define PRCMU_ENABLE_ESCAPE_CLOCK_DIV		0x03000201
#define PRCMU_DISABLE_ESCAPE_CLOCK_DIV		0x00000101

#define PRCMU_ENABLE_PLLDSI			0x00000001
#define PRCMU_DISABLE_PLLDSI			0x00000000

#define PRCMU_DSI_RESET_SW			0x00000003
#define PRCMU_RESOUTN0_PIN			0x00000001
#define PRCMU_RESOUTN1_PIN			0x00000002
#define PRCMU_RESOUTN2_PIN			0x00000004

#define PRCMU_PLLDSI_LOCKP_LOCKED		0x3

/*
 * mb0_transfer - state needed for mailbox 0 communication.
 * @lock:		The transaction lock.
 */
static struct {
	spinlock_t lock;
} mb0_transfer;

/*
 * mb5_transfer - state needed for mailbox 5 communication.
 * @lock:	The transaction lock.
 * @work:	The transaction completion structure.
 * @ack:	Reply ("acknowledge") data.
 */
static struct {
	struct mutex lock;
	struct completion work;
	struct {
		u8 header;
		u8 status;
		u8 value[4];
	} ack;
} mb5_transfer;

/* PRCMU TCDM base IO address. */
static __iomem void *tcdm_base;

/**
 * db5500_prcmu_abb_read() - Read register value(s) from the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The read out value(s).
 * @size:	The number of registers to read.
 *
 * Reads register value(s) from the ABB.
 * @size has to be <= 4.
 */
int db5500_prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if ((size < 1) || (4 < size))
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();
	writeb(slave, PRCM_REQ_MB5_I2C_SLAVE);
	writeb(reg, PRCM_REQ_MB5_I2C_REG);
	writeb(size, PRCM_REQ_MB5_I2C_SIZE);
	writeb(MB5H_I2C_READ, PRCM_REQ_MB5_HEADER);

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb5_transfer.work);

	r = 0;
	if ((mb5_transfer.ack.header == MB5H_I2C_READ) &&
		(mb5_transfer.ack.status == RC_SUCCESS))
		memcpy(value, mb5_transfer.ack.value, (size_t)size);
	else
		r = -EIO;

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

/**
 * db5500_prcmu_abb_write() - Write register value(s) to the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The value(s) to write.
 * @size:	The number of registers to write.
 *
 * Writes register value(s) to the ABB.
 * @size has to be <= 4.
 */
int db5500_prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if ((size < 1) || (4 < size))
		return -EINVAL;

	mutex_lock(&mb5_transfer.lock);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();
	writeb(slave, PRCM_REQ_MB5_I2C_SLAVE);
	writeb(reg, PRCM_REQ_MB5_I2C_REG);
	writeb(size, PRCM_REQ_MB5_I2C_SIZE);
	memcpy_toio(PRCM_REQ_MB5_I2C_DATA, value, size);
	writeb(MB5H_I2C_WRITE, PRCM_REQ_MB5_HEADER);

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);
	wait_for_completion(&mb5_transfer.work);

	if ((mb5_transfer.ack.header == MB5H_I2C_WRITE) &&
		(mb5_transfer.ack.status == RC_SUCCESS))
		r = 0;
	else
		r = -EIO;

	mutex_unlock(&mb5_transfer.lock);

	return r;
}

int db5500_prcmu_enable_dsipll(void)
{
	int i;

	/* Enable DSIPLL_RESETN resets */
	writel(PRCMU_RESET_DSIPLL, PRCM_APE_RESETN_CLR);
	/* Unclamp DSIPLL in/out */
	writel(PRCMU_UNCLAMP_DSIPLL, PRCM_MMIP_LS_CLAMP_CLR);
	/* Set DSI PLL FREQ */
	writel(PRCMU_PLLDSI_FREQ_SETTING, PRCM_PLLDSI_FREQ);
	writel(PRCMU_DSI_PLLOUT_SEL_SETTING,
		PRCM_DSI_PLLOUT_SEL);
	/* Enable Escape clocks */
	writel(PRCMU_ENABLE_ESCAPE_CLOCK_DIV, PRCM_DSITVCLK_DIV);

	/* Start DSI PLL */
	writel(PRCMU_ENABLE_PLLDSI, PRCM_PLLDSI_ENABLE);
	/* Reset DSI PLL */
	writel(PRCMU_DSI_RESET_SW, PRCM_DSI_SW_RESET);
	for (i = 0; i < 10; i++) {
		if ((readl(PRCM_PLLDSI_LOCKP) &
			PRCMU_PLLDSI_LOCKP_LOCKED) == PRCMU_PLLDSI_LOCKP_LOCKED)
			break;
		udelay(100);
	}
	/* Release DSIPLL_RESETN */
	writel(PRCMU_RESET_DSIPLL, PRCM_APE_RESETN_SET);
	return 0;
}

int db5500_prcmu_disable_dsipll(void)
{
	/* Disable dsi pll */
	writel(PRCMU_DISABLE_PLLDSI, PRCM_PLLDSI_ENABLE);
	/* Disable  escapeclock */
	writel(PRCMU_DISABLE_ESCAPE_CLOCK_DIV, PRCM_DSITVCLK_DIV);
	return 0;
}

int db5500_prcmu_set_display_clocks(void)
{
	/* HDMI and TVCLK Should be handled somewhere else */
	/* PLLDIV=8, PLLSW=2, CLKEN=1 */
	writel(PRCMU_DSI_CLOCK_SETTING, PRCM_HDMICLK_MGT);
	/* PLLDIV=14, PLLSW=2, CLKEN=1 */
	writel(PRCMU_DSI_LP_CLOCK_SETTING, PRCM_TVCLK_MGT);
	return 0;
}

static void ack_dbb_wakeup(void)
{
	unsigned long flags;

	spin_lock_irqsave(&mb0_transfer.lock, flags);

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(0))
		cpu_relax();

	writeb(RMB0H_RD_WAKE_UP_ACK, PRCM_REQ_MB0_HEADER);
	writel(MBOX_BIT(0), PRCM_MBOX_CPU_SET);

	spin_unlock_irqrestore(&mb0_transfer.lock, flags);
}

static inline void print_unknown_header_warning(u8 n, u8 header)
{
	pr_warning("prcmu: Unknown message header (%d) in mailbox %d.\n",
		header, n);
}

static bool read_mailbox_0(void)
{
	bool r;
	u8 header;

	header = readb(PRCM_ACK_MB0_HEADER);
	switch (header) {
	case AMB0H_WAKE_UP:
		r = true;
		break;
	default:
		print_unknown_header_warning(0, header);
		r = false;
		break;
	}
	writel(MBOX_BIT(0), PRCM_ARM_IT1_CLR);
	return r;
}

static bool read_mailbox_1(void)
{
	writel(MBOX_BIT(1), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_2(void)
{
	writel(MBOX_BIT(2), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_3(void)
{
	writel(MBOX_BIT(3), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_4(void)
{
	writel(MBOX_BIT(4), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_5(void)
{
	u8 header;

	header = readb(PRCM_ACK_MB5_HEADER);
	switch (header) {
	case MB5H_I2C_READ:
		memcpy_fromio(mb5_transfer.ack.value, PRCM_ACK_MB5_I2C_DATA, 4);
	case MB5H_I2C_WRITE:
		mb5_transfer.ack.header = header;
		mb5_transfer.ack.status = readb(PRCM_ACK_MB5_RETURN_CODE);
		complete(&mb5_transfer.work);
		break;
	default:
		print_unknown_header_warning(5, header);
		break;
	}
	writel(MBOX_BIT(5), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_6(void)
{
	writel(MBOX_BIT(6), PRCM_ARM_IT1_CLR);
	return false;
}

static bool read_mailbox_7(void)
{
	writel(MBOX_BIT(7), PRCM_ARM_IT1_CLR);
	return false;
}

static bool (* const read_mailbox[NUM_MB])(void) = {
	read_mailbox_0,
	read_mailbox_1,
	read_mailbox_2,
	read_mailbox_3,
	read_mailbox_4,
	read_mailbox_5,
	read_mailbox_6,
	read_mailbox_7
};

static irqreturn_t prcmu_irq_handler(int irq, void *data)
{
	u32 bits;
	u8 n;
	irqreturn_t r;

	bits = (readl(PRCM_ARM_IT1_VAL) & ALL_MBOX_BITS);
	if (unlikely(!bits))
		return IRQ_NONE;

	r = IRQ_HANDLED;
	for (n = 0; bits; n++) {
		if (bits & MBOX_BIT(n)) {
			bits -= MBOX_BIT(n);
			if (read_mailbox[n]())
				r = IRQ_WAKE_THREAD;
		}
	}
	return r;
}

static irqreturn_t prcmu_irq_thread_fn(int irq, void *data)
{
	ack_dbb_wakeup();
	return IRQ_HANDLED;
}

void __init db5500_prcmu_early_init(void)
{
	tcdm_base = __io_address(U5500_PRCMU_TCDM_BASE);
	spin_lock_init(&mb0_transfer.lock);
	mutex_init(&mb5_transfer.lock);
	init_completion(&mb5_transfer.work);
}

/**
 * prcmu_fw_init - arch init call for the Linux PRCMU fw init logic
 *
 */
int __init db5500_prcmu_init(void)
{
	int r = 0;

	if (ux500_is_svp() || !cpu_is_u5500())
		return -ENODEV;

	/* Clean up the mailbox interrupts after pre-kernel code. */
	writel(ALL_MBOX_BITS, PRCM_ARM_IT1_CLR);

	r = request_threaded_irq(IRQ_DB5500_PRCMU1, prcmu_irq_handler,
		prcmu_irq_thread_fn, 0, "prcmu", NULL);
	if (r < 0) {
		pr_err("prcmu: Failed to allocate IRQ_DB5500_PRCMU1.\n");
		return -EBUSY;
	}
	return 0;
}

arch_initcall(db5500_prcmu_init);

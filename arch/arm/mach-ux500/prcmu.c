/*
 * Copyright (C) ST Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 *
 * U8500 PRCMU driver.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>

#include <mach/hardware.h>
#include <mach/prcmu-regs.h>

#define PRCMU_TCDM_BASE __io_address(U8500_PRCMU_TCDM_BASE)

#define REQ_MB5 (PRCMU_TCDM_BASE + 0xE44)
#define ACK_MB5 (PRCMU_TCDM_BASE + 0xDF4)

#define REQ_MB5_I2C_SLAVE_OP (REQ_MB5)
#define REQ_MB5_I2C_HW_BITS (REQ_MB5 + 1)
#define REQ_MB5_I2C_REG (REQ_MB5 + 2)
#define REQ_MB5_I2C_VAL (REQ_MB5 + 3)

#define ACK_MB5_I2C_STATUS (ACK_MB5 + 1)
#define ACK_MB5_I2C_VAL (ACK_MB5 + 3)

#define I2C_WRITE(slave) ((slave) << 1)
#define I2C_READ(slave) (((slave) << 1) | BIT(0))
#define I2C_STOP_EN BIT(3)

enum ack_mb5_status {
	I2C_WR_OK = 0x01,
	I2C_RD_OK = 0x02,
};

#define MBOX_BIT BIT
#define NUM_MBOX 8

static struct {
	struct mutex lock;
	struct completion work;
	bool failed;
	struct {
		u8 status;
		u8 value;
	} ack;
} mb5_transfer;

/**
 * prcmu_abb_read() - Read register value(s) from the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The read out value(s).
 * @size:	The number of registers to read.
 *
 * Reads register value(s) from the ABB.
 * @size has to be 1 for the current firmware version.
 */
int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if (size != 1)
		return -EINVAL;

	r = mutex_lock_interruptible(&mb5_transfer.lock);
	if (r)
		return r;

	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();

	writeb(I2C_READ(slave), REQ_MB5_I2C_SLAVE_OP);
	writeb(I2C_STOP_EN, REQ_MB5_I2C_HW_BITS);
	writeb(reg, REQ_MB5_I2C_REG);

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);
	if (!wait_for_completion_timeout(&mb5_transfer.work,
			msecs_to_jiffies(500))) {
		pr_err("prcmu: prcmu_abb_read timed out.\n");
		r = -EIO;
		goto unlock_and_return;
	}
	r = ((mb5_transfer.ack.status == I2C_RD_OK) ? 0 : -EIO);
	if (!r)
		*value = mb5_transfer.ack.value;

unlock_and_return:
	mutex_unlock(&mb5_transfer.lock);
	return r;
}
EXPORT_SYMBOL(prcmu_abb_read);

/**
 * prcmu_abb_write() - Write register value(s) to the ABB.
 * @slave:	The I2C slave address.
 * @reg:	The (start) register address.
 * @value:	The value(s) to write.
 * @size:	The number of registers to write.
 *
 * Reads register value(s) from the ABB.
 * @size has to be 1 for the current firmware version.
 */
int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	int r;

	if (size != 1)
		return -EINVAL;

	r = mutex_lock_interruptible(&mb5_transfer.lock);
	if (r)
		return r;


	while (readl(PRCM_MBOX_CPU_VAL) & MBOX_BIT(5))
		cpu_relax();

	writeb(I2C_WRITE(slave), REQ_MB5_I2C_SLAVE_OP);
	writeb(I2C_STOP_EN, REQ_MB5_I2C_HW_BITS);
	writeb(reg, REQ_MB5_I2C_REG);
	writeb(*value, REQ_MB5_I2C_VAL);

	writel(MBOX_BIT(5), PRCM_MBOX_CPU_SET);
	if (!wait_for_completion_timeout(&mb5_transfer.work,
			msecs_to_jiffies(500))) {
		pr_err("prcmu: prcmu_abb_write timed out.\n");
		r = -EIO;
		goto unlock_and_return;
	}
	r = ((mb5_transfer.ack.status == I2C_WR_OK) ? 0 : -EIO);

unlock_and_return:
	mutex_unlock(&mb5_transfer.lock);
	return r;
}
EXPORT_SYMBOL(prcmu_abb_write);

static void read_mailbox_0(void)
{
	writel(MBOX_BIT(0), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_1(void)
{
	writel(MBOX_BIT(1), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_2(void)
{
	writel(MBOX_BIT(2), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_3(void)
{
	writel(MBOX_BIT(3), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_4(void)
{
	writel(MBOX_BIT(4), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_5(void)
{
	mb5_transfer.ack.status = readb(ACK_MB5_I2C_STATUS);
	mb5_transfer.ack.value = readb(ACK_MB5_I2C_VAL);
	complete(&mb5_transfer.work);
	writel(MBOX_BIT(5), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_6(void)
{
	writel(MBOX_BIT(6), PRCM_ARM_IT1_CLEAR);
}

static void read_mailbox_7(void)
{
	writel(MBOX_BIT(7), PRCM_ARM_IT1_CLEAR);
}

static void (* const read_mailbox[NUM_MBOX])(void) = {
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

	bits = (readl(PRCM_ARM_IT1_VAL) & (MBOX_BIT(NUM_MBOX) - 1));
	if (unlikely(!bits))
		return IRQ_NONE;

	for (n = 0; bits; n++) {
		if (bits & MBOX_BIT(n)) {
			bits -= MBOX_BIT(n);
			read_mailbox[n]();
		}
	}
	return IRQ_HANDLED;
}

static int __init prcmu_init(void)
{
	mutex_init(&mb5_transfer.lock);
	init_completion(&mb5_transfer.work);

	/* Clean up the mailbox interrupts after pre-kernel code. */
	writel((MBOX_BIT(NUM_MBOX) - 1), PRCM_ARM_IT1_CLEAR);

	return request_irq(IRQ_PRCMU, prcmu_irq_handler, 0, "prcmu", NULL);
}

arch_initcall(prcmu_init);

/*
 * Copyright 2012 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/types.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/amba/bus.h>

#include <linux/pl320-ipc.h>

#define IPCMxSOURCE(m)		((m) * 0x40)
#define IPCMxDSET(m)		(((m) * 0x40) + 0x004)
#define IPCMxDCLEAR(m)		(((m) * 0x40) + 0x008)
#define IPCMxDSTATUS(m)		(((m) * 0x40) + 0x00C)
#define IPCMxMODE(m)		(((m) * 0x40) + 0x010)
#define IPCMxMSET(m)		(((m) * 0x40) + 0x014)
#define IPCMxMCLEAR(m)		(((m) * 0x40) + 0x018)
#define IPCMxMSTATUS(m)		(((m) * 0x40) + 0x01C)
#define IPCMxSEND(m)		(((m) * 0x40) + 0x020)
#define IPCMxDR(m, dr)		(((m) * 0x40) + ((dr) * 4) + 0x024)

#define IPCMMIS(irq)		(((irq) * 8) + 0x800)
#define IPCMRIS(irq)		(((irq) * 8) + 0x804)

#define MBOX_MASK(n)		(1 << (n))
#define IPC_TX_MBOX		1
#define IPC_RX_MBOX		2

#define CHAN_MASK(n)		(1 << (n))
#define A9_SOURCE		1
#define M3_SOURCE		0

static void __iomem *ipc_base;
static int ipc_irq;
static DEFINE_MUTEX(ipc_m1_lock);
static DECLARE_COMPLETION(ipc_completion);
static ATOMIC_NOTIFIER_HEAD(ipc_notifier);

static inline void set_destination(int source, int mbox)
{
	__raw_writel(CHAN_MASK(source), ipc_base + IPCMxDSET(mbox));
	__raw_writel(CHAN_MASK(source), ipc_base + IPCMxMSET(mbox));
}

static inline void clear_destination(int source, int mbox)
{
	__raw_writel(CHAN_MASK(source), ipc_base + IPCMxDCLEAR(mbox));
	__raw_writel(CHAN_MASK(source), ipc_base + IPCMxMCLEAR(mbox));
}

static void __ipc_send(int mbox, u32 *data)
{
	int i;
	for (i = 0; i < 7; i++)
		__raw_writel(data[i], ipc_base + IPCMxDR(mbox, i));
	__raw_writel(0x1, ipc_base + IPCMxSEND(mbox));
}

static u32 __ipc_rcv(int mbox, u32 *data)
{
	int i;
	for (i = 0; i < 7; i++)
		data[i] = __raw_readl(ipc_base + IPCMxDR(mbox, i));
	return data[1];
}

/* blocking implmentation from the A9 side, not usuable in interrupts! */
int pl320_ipc_transmit(u32 *data)
{
	int ret;

	mutex_lock(&ipc_m1_lock);

	init_completion(&ipc_completion);
	__ipc_send(IPC_TX_MBOX, data);
	ret = wait_for_completion_timeout(&ipc_completion,
					  msecs_to_jiffies(1000));
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = __ipc_rcv(IPC_TX_MBOX, data);
out:
	mutex_unlock(&ipc_m1_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pl320_ipc_transmit);

static irqreturn_t ipc_handler(int irq, void *dev)
{
	u32 irq_stat;
	u32 data[7];

	irq_stat = __raw_readl(ipc_base + IPCMMIS(1));
	if (irq_stat & MBOX_MASK(IPC_TX_MBOX)) {
		__raw_writel(0, ipc_base + IPCMxSEND(IPC_TX_MBOX));
		complete(&ipc_completion);
	}
	if (irq_stat & MBOX_MASK(IPC_RX_MBOX)) {
		__ipc_rcv(IPC_RX_MBOX, data);
		atomic_notifier_call_chain(&ipc_notifier, data[0], data + 1);
		__raw_writel(2, ipc_base + IPCMxSEND(IPC_RX_MBOX));
	}

	return IRQ_HANDLED;
}

int pl320_ipc_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ipc_notifier, nb);
}
EXPORT_SYMBOL_GPL(pl320_ipc_register_notifier);

int pl320_ipc_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ipc_notifier, nb);
}
EXPORT_SYMBOL_GPL(pl320_ipc_unregister_notifier);

static int pl320_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;

	ipc_base = ioremap(adev->res.start, resource_size(&adev->res));
	if (ipc_base == NULL)
		return -ENOMEM;

	__raw_writel(0, ipc_base + IPCMxSEND(IPC_TX_MBOX));

	ipc_irq = adev->irq[0];
	ret = request_irq(ipc_irq, ipc_handler, 0, dev_name(&adev->dev), NULL);
	if (ret < 0)
		goto err;

	/* Init slow mailbox */
	__raw_writel(CHAN_MASK(A9_SOURCE),
			ipc_base + IPCMxSOURCE(IPC_TX_MBOX));
	__raw_writel(CHAN_MASK(M3_SOURCE),
			ipc_base + IPCMxDSET(IPC_TX_MBOX));
	__raw_writel(CHAN_MASK(M3_SOURCE) | CHAN_MASK(A9_SOURCE),
		     ipc_base + IPCMxMSET(IPC_TX_MBOX));

	/* Init receive mailbox */
	__raw_writel(CHAN_MASK(M3_SOURCE),
			ipc_base + IPCMxSOURCE(IPC_RX_MBOX));
	__raw_writel(CHAN_MASK(A9_SOURCE),
			ipc_base + IPCMxDSET(IPC_RX_MBOX));
	__raw_writel(CHAN_MASK(M3_SOURCE) | CHAN_MASK(A9_SOURCE),
		     ipc_base + IPCMxMSET(IPC_RX_MBOX));

	return 0;
err:
	iounmap(ipc_base);
	return ret;
}

static struct amba_id pl320_ids[] = {
	{
		.id	= 0x00041320,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver pl320_driver = {
	.drv = {
		.name	= "pl320",
	},
	.id_table	= pl320_ids,
	.probe		= pl320_probe,
};

static int __init ipc_init(void)
{
	return amba_driver_register(&pl320_driver);
}
subsys_initcall(ipc_init);

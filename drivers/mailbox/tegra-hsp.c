// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include <dt-bindings/mailbox/tegra186-hsp.h>

#include "mailbox.h"

#define HSP_INT_IE(x)		(0x100 + ((x) * 4))
#define HSP_INT_IV		0x300
#define HSP_INT_IR		0x304

#define HSP_INT_EMPTY_SHIFT	0
#define HSP_INT_EMPTY_MASK	0xff
#define HSP_INT_FULL_SHIFT	8
#define HSP_INT_FULL_MASK	0xff

#define HSP_INT_DIMENSIONING	0x380
#define HSP_nSM_SHIFT		0
#define HSP_nSS_SHIFT		4
#define HSP_nAS_SHIFT		8
#define HSP_nDB_SHIFT		12
#define HSP_nSI_SHIFT		16
#define HSP_nINT_MASK		0xf

#define HSP_DB_TRIGGER	0x0
#define HSP_DB_ENABLE	0x4
#define HSP_DB_RAW	0x8
#define HSP_DB_PENDING	0xc

#define HSP_SM_SHRD_MBOX	0x0
#define HSP_SM_SHRD_MBOX_FULL	BIT(31)
#define HSP_SM_SHRD_MBOX_FULL_INT_IE	0x04
#define HSP_SM_SHRD_MBOX_EMPTY_INT_IE	0x08

#define HSP_DB_CCPLEX		1
#define HSP_DB_BPMP		3
#define HSP_DB_MAX		7

struct tegra_hsp_channel;
struct tegra_hsp;

struct tegra_hsp_channel {
	struct tegra_hsp *hsp;
	struct mbox_chan *chan;
	void __iomem *regs;
};

struct tegra_hsp_doorbell {
	struct tegra_hsp_channel channel;
	struct list_head list;
	const char *name;
	unsigned int master;
	unsigned int index;
};

struct tegra_hsp_mailbox {
	struct tegra_hsp_channel channel;
	unsigned int index;
	bool producer;
};

struct tegra_hsp_db_map {
	const char *name;
	unsigned int master;
	unsigned int index;
};

struct tegra_hsp_soc {
	const struct tegra_hsp_db_map *map;
	bool has_per_mb_ie;
};

struct tegra_hsp {
	struct device *dev;
	const struct tegra_hsp_soc *soc;
	struct mbox_controller mbox_db;
	struct mbox_controller mbox_sm;
	void __iomem *regs;
	unsigned int doorbell_irq;
	unsigned int *shared_irqs;
	unsigned int shared_irq;
	unsigned int num_sm;
	unsigned int num_as;
	unsigned int num_ss;
	unsigned int num_db;
	unsigned int num_si;
	spinlock_t lock;

	struct list_head doorbells;
	struct tegra_hsp_mailbox *mailboxes;

	unsigned long mask;
};

static inline u32 tegra_hsp_readl(struct tegra_hsp *hsp, unsigned int offset)
{
	return readl(hsp->regs + offset);
}

static inline void tegra_hsp_writel(struct tegra_hsp *hsp, u32 value,
				    unsigned int offset)
{
	writel(value, hsp->regs + offset);
}

static inline u32 tegra_hsp_channel_readl(struct tegra_hsp_channel *channel,
					  unsigned int offset)
{
	return readl(channel->regs + offset);
}

static inline void tegra_hsp_channel_writel(struct tegra_hsp_channel *channel,
					    u32 value, unsigned int offset)
{
	writel(value, channel->regs + offset);
}

static bool tegra_hsp_doorbell_can_ring(struct tegra_hsp_doorbell *db)
{
	u32 value;

	value = tegra_hsp_channel_readl(&db->channel, HSP_DB_ENABLE);

	return (value & BIT(TEGRA_HSP_DB_MASTER_CCPLEX)) != 0;
}

static struct tegra_hsp_doorbell *
__tegra_hsp_doorbell_get(struct tegra_hsp *hsp, unsigned int master)
{
	struct tegra_hsp_doorbell *entry;

	list_for_each_entry(entry, &hsp->doorbells, list)
		if (entry->master == master)
			return entry;

	return NULL;
}

static struct tegra_hsp_doorbell *
tegra_hsp_doorbell_get(struct tegra_hsp *hsp, unsigned int master)
{
	struct tegra_hsp_doorbell *db;
	unsigned long flags;

	spin_lock_irqsave(&hsp->lock, flags);
	db = __tegra_hsp_doorbell_get(hsp, master);
	spin_unlock_irqrestore(&hsp->lock, flags);

	return db;
}

static irqreturn_t tegra_hsp_doorbell_irq(int irq, void *data)
{
	struct tegra_hsp *hsp = data;
	struct tegra_hsp_doorbell *db;
	unsigned long master, value;

	db = tegra_hsp_doorbell_get(hsp, TEGRA_HSP_DB_MASTER_CCPLEX);
	if (!db)
		return IRQ_NONE;

	value = tegra_hsp_channel_readl(&db->channel, HSP_DB_PENDING);
	tegra_hsp_channel_writel(&db->channel, value, HSP_DB_PENDING);

	spin_lock(&hsp->lock);

	for_each_set_bit(master, &value, hsp->mbox_db.num_chans) {
		struct tegra_hsp_doorbell *db;

		db = __tegra_hsp_doorbell_get(hsp, master);
		/*
		 * Depending on the bootloader chain, the CCPLEX doorbell will
		 * have some doorbells enabled, which means that requesting an
		 * interrupt will immediately fire.
		 *
		 * In that case, db->channel.chan will still be NULL here and
		 * cause a crash if not properly guarded.
		 *
		 * It remains to be seen if ignoring the doorbell in that case
		 * is the correct solution.
		 */
		if (db && db->channel.chan)
			mbox_chan_received_data(db->channel.chan, NULL);
	}

	spin_unlock(&hsp->lock);

	return IRQ_HANDLED;
}

static irqreturn_t tegra_hsp_shared_irq(int irq, void *data)
{
	struct tegra_hsp *hsp = data;
	unsigned long bit, mask;
	u32 status, value;
	void *msg;

	status = tegra_hsp_readl(hsp, HSP_INT_IR) & hsp->mask;

	/* process EMPTY interrupts first */
	mask = (status >> HSP_INT_EMPTY_SHIFT) & HSP_INT_EMPTY_MASK;

	for_each_set_bit(bit, &mask, hsp->num_sm) {
		struct tegra_hsp_mailbox *mb = &hsp->mailboxes[bit];

		if (mb->producer) {
			/*
			 * Disable EMPTY interrupts until data is sent with
			 * the next message. These interrupts are level-
			 * triggered, so if we kept them enabled they would
			 * constantly trigger until we next write data into
			 * the message.
			 */
			spin_lock(&hsp->lock);

			hsp->mask &= ~BIT(HSP_INT_EMPTY_SHIFT + mb->index);
			tegra_hsp_writel(hsp, hsp->mask,
					 HSP_INT_IE(hsp->shared_irq));

			spin_unlock(&hsp->lock);

			mbox_chan_txdone(mb->channel.chan, 0);
		}
	}

	/* process FULL interrupts */
	mask = (status >> HSP_INT_FULL_SHIFT) & HSP_INT_FULL_MASK;

	for_each_set_bit(bit, &mask, hsp->num_sm) {
		struct tegra_hsp_mailbox *mb = &hsp->mailboxes[bit];

		if (!mb->producer) {
			value = tegra_hsp_channel_readl(&mb->channel,
							HSP_SM_SHRD_MBOX);
			value &= ~HSP_SM_SHRD_MBOX_FULL;
			msg = (void *)(unsigned long)value;
			mbox_chan_received_data(mb->channel.chan, msg);

			/*
			 * Need to clear all bits here since some producers,
			 * such as TCU, depend on fields in the register
			 * getting cleared by the consumer.
			 *
			 * The mailbox API doesn't give the consumers a way
			 * of doing that explicitly, so we have to make sure
			 * we cover all possible cases.
			 */
			tegra_hsp_channel_writel(&mb->channel, 0x0,
						 HSP_SM_SHRD_MBOX);
		}
	}

	return IRQ_HANDLED;
}

static struct tegra_hsp_channel *
tegra_hsp_doorbell_create(struct tegra_hsp *hsp, const char *name,
			  unsigned int master, unsigned int index)
{
	struct tegra_hsp_doorbell *db;
	unsigned int offset;
	unsigned long flags;

	db = devm_kzalloc(hsp->dev, sizeof(*db), GFP_KERNEL);
	if (!db)
		return ERR_PTR(-ENOMEM);

	offset = (1 + (hsp->num_sm / 2) + hsp->num_ss + hsp->num_as) * SZ_64K;
	offset += index * 0x100;

	db->channel.regs = hsp->regs + offset;
	db->channel.hsp = hsp;

	db->name = devm_kstrdup_const(hsp->dev, name, GFP_KERNEL);
	db->master = master;
	db->index = index;

	spin_lock_irqsave(&hsp->lock, flags);
	list_add_tail(&db->list, &hsp->doorbells);
	spin_unlock_irqrestore(&hsp->lock, flags);

	return &db->channel;
}

static int tegra_hsp_doorbell_send_data(struct mbox_chan *chan, void *data)
{
	struct tegra_hsp_doorbell *db = chan->con_priv;

	tegra_hsp_channel_writel(&db->channel, 1, HSP_DB_TRIGGER);

	return 0;
}

static int tegra_hsp_doorbell_startup(struct mbox_chan *chan)
{
	struct tegra_hsp_doorbell *db = chan->con_priv;
	struct tegra_hsp *hsp = db->channel.hsp;
	struct tegra_hsp_doorbell *ccplex;
	unsigned long flags;
	u32 value;

	if (db->master >= chan->mbox->num_chans) {
		dev_err(chan->mbox->dev,
			"invalid master ID %u for HSP channel\n",
			db->master);
		return -EINVAL;
	}

	ccplex = tegra_hsp_doorbell_get(hsp, TEGRA_HSP_DB_MASTER_CCPLEX);
	if (!ccplex)
		return -ENODEV;

	if (!tegra_hsp_doorbell_can_ring(db))
		return -ENODEV;

	spin_lock_irqsave(&hsp->lock, flags);

	value = tegra_hsp_channel_readl(&ccplex->channel, HSP_DB_ENABLE);
	value |= BIT(db->master);
	tegra_hsp_channel_writel(&ccplex->channel, value, HSP_DB_ENABLE);

	spin_unlock_irqrestore(&hsp->lock, flags);

	return 0;
}

static void tegra_hsp_doorbell_shutdown(struct mbox_chan *chan)
{
	struct tegra_hsp_doorbell *db = chan->con_priv;
	struct tegra_hsp *hsp = db->channel.hsp;
	struct tegra_hsp_doorbell *ccplex;
	unsigned long flags;
	u32 value;

	ccplex = tegra_hsp_doorbell_get(hsp, TEGRA_HSP_DB_MASTER_CCPLEX);
	if (!ccplex)
		return;

	spin_lock_irqsave(&hsp->lock, flags);

	value = tegra_hsp_channel_readl(&ccplex->channel, HSP_DB_ENABLE);
	value &= ~BIT(db->master);
	tegra_hsp_channel_writel(&ccplex->channel, value, HSP_DB_ENABLE);

	spin_unlock_irqrestore(&hsp->lock, flags);
}

static const struct mbox_chan_ops tegra_hsp_db_ops = {
	.send_data = tegra_hsp_doorbell_send_data,
	.startup = tegra_hsp_doorbell_startup,
	.shutdown = tegra_hsp_doorbell_shutdown,
};

static int tegra_hsp_mailbox_send_data(struct mbox_chan *chan, void *data)
{
	struct tegra_hsp_mailbox *mb = chan->con_priv;
	struct tegra_hsp *hsp = mb->channel.hsp;
	unsigned long flags;
	u32 value;

	if (WARN_ON(!mb->producer))
		return -EPERM;

	/* copy data and mark mailbox full */
	value = (u32)(unsigned long)data;
	value |= HSP_SM_SHRD_MBOX_FULL;

	tegra_hsp_channel_writel(&mb->channel, value, HSP_SM_SHRD_MBOX);

	/* enable EMPTY interrupt for the shared mailbox */
	spin_lock_irqsave(&hsp->lock, flags);

	hsp->mask |= BIT(HSP_INT_EMPTY_SHIFT + mb->index);
	tegra_hsp_writel(hsp, hsp->mask, HSP_INT_IE(hsp->shared_irq));

	spin_unlock_irqrestore(&hsp->lock, flags);

	return 0;
}

static int tegra_hsp_mailbox_flush(struct mbox_chan *chan,
				   unsigned long timeout)
{
	struct tegra_hsp_mailbox *mb = chan->con_priv;
	struct tegra_hsp_channel *ch = &mb->channel;
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = tegra_hsp_channel_readl(ch, HSP_SM_SHRD_MBOX);
		if ((value & HSP_SM_SHRD_MBOX_FULL) == 0) {
			mbox_chan_txdone(chan, 0);
			return 0;
		}

		udelay(1);
	}

	return -ETIME;
}

static int tegra_hsp_mailbox_startup(struct mbox_chan *chan)
{
	struct tegra_hsp_mailbox *mb = chan->con_priv;
	struct tegra_hsp_channel *ch = &mb->channel;
	struct tegra_hsp *hsp = mb->channel.hsp;
	unsigned long flags;

	chan->txdone_method = TXDONE_BY_IRQ;

	/*
	 * Shared mailboxes start out as consumers by default. FULL and EMPTY
	 * interrupts are coalesced at the same shared interrupt.
	 *
	 * Keep EMPTY interrupts disabled at startup and only enable them when
	 * the mailbox is actually full. This is required because the FULL and
	 * EMPTY interrupts are level-triggered, so keeping EMPTY interrupts
	 * enabled all the time would cause an interrupt storm while mailboxes
	 * are idle.
	 */

	spin_lock_irqsave(&hsp->lock, flags);

	if (mb->producer)
		hsp->mask &= ~BIT(HSP_INT_EMPTY_SHIFT + mb->index);
	else
		hsp->mask |= BIT(HSP_INT_FULL_SHIFT + mb->index);

	tegra_hsp_writel(hsp, hsp->mask, HSP_INT_IE(hsp->shared_irq));

	spin_unlock_irqrestore(&hsp->lock, flags);

	if (hsp->soc->has_per_mb_ie) {
		if (mb->producer)
			tegra_hsp_channel_writel(ch, 0x0,
						 HSP_SM_SHRD_MBOX_EMPTY_INT_IE);
		else
			tegra_hsp_channel_writel(ch, 0x1,
						 HSP_SM_SHRD_MBOX_FULL_INT_IE);
	}

	return 0;
}

static void tegra_hsp_mailbox_shutdown(struct mbox_chan *chan)
{
	struct tegra_hsp_mailbox *mb = chan->con_priv;
	struct tegra_hsp_channel *ch = &mb->channel;
	struct tegra_hsp *hsp = mb->channel.hsp;
	unsigned long flags;

	if (hsp->soc->has_per_mb_ie) {
		if (mb->producer)
			tegra_hsp_channel_writel(ch, 0x0,
						 HSP_SM_SHRD_MBOX_EMPTY_INT_IE);
		else
			tegra_hsp_channel_writel(ch, 0x0,
						 HSP_SM_SHRD_MBOX_FULL_INT_IE);
	}

	spin_lock_irqsave(&hsp->lock, flags);

	if (mb->producer)
		hsp->mask &= ~BIT(HSP_INT_EMPTY_SHIFT + mb->index);
	else
		hsp->mask &= ~BIT(HSP_INT_FULL_SHIFT + mb->index);

	tegra_hsp_writel(hsp, hsp->mask, HSP_INT_IE(hsp->shared_irq));

	spin_unlock_irqrestore(&hsp->lock, flags);
}

static const struct mbox_chan_ops tegra_hsp_sm_ops = {
	.send_data = tegra_hsp_mailbox_send_data,
	.flush = tegra_hsp_mailbox_flush,
	.startup = tegra_hsp_mailbox_startup,
	.shutdown = tegra_hsp_mailbox_shutdown,
};

static struct mbox_chan *tegra_hsp_db_xlate(struct mbox_controller *mbox,
					    const struct of_phandle_args *args)
{
	struct tegra_hsp *hsp = container_of(mbox, struct tegra_hsp, mbox_db);
	unsigned int type = args->args[0], master = args->args[1];
	struct tegra_hsp_channel *channel = ERR_PTR(-ENODEV);
	struct tegra_hsp_doorbell *db;
	struct mbox_chan *chan;
	unsigned long flags;
	unsigned int i;

	if (type != TEGRA_HSP_MBOX_TYPE_DB || !hsp->doorbell_irq)
		return ERR_PTR(-ENODEV);

	db = tegra_hsp_doorbell_get(hsp, master);
	if (db)
		channel = &db->channel;

	if (IS_ERR(channel))
		return ERR_CAST(channel);

	spin_lock_irqsave(&hsp->lock, flags);

	for (i = 0; i < mbox->num_chans; i++) {
		chan = &mbox->chans[i];
		if (!chan->con_priv) {
			channel->chan = chan;
			chan->con_priv = db;
			break;
		}

		chan = NULL;
	}

	spin_unlock_irqrestore(&hsp->lock, flags);

	return chan ?: ERR_PTR(-EBUSY);
}

static struct mbox_chan *tegra_hsp_sm_xlate(struct mbox_controller *mbox,
					    const struct of_phandle_args *args)
{
	struct tegra_hsp *hsp = container_of(mbox, struct tegra_hsp, mbox_sm);
	unsigned int type = args->args[0], index;
	struct tegra_hsp_mailbox *mb;

	index = args->args[1] & TEGRA_HSP_SM_MASK;

	if (type != TEGRA_HSP_MBOX_TYPE_SM || !hsp->shared_irqs ||
	    index >= hsp->num_sm)
		return ERR_PTR(-ENODEV);

	mb = &hsp->mailboxes[index];

	if ((args->args[1] & TEGRA_HSP_SM_FLAG_TX) == 0)
		mb->producer = false;
	else
		mb->producer = true;

	return mb->channel.chan;
}

static int tegra_hsp_add_doorbells(struct tegra_hsp *hsp)
{
	const struct tegra_hsp_db_map *map = hsp->soc->map;
	struct tegra_hsp_channel *channel;

	while (map->name) {
		channel = tegra_hsp_doorbell_create(hsp, map->name,
						    map->master, map->index);
		if (IS_ERR(channel))
			return PTR_ERR(channel);

		map++;
	}

	return 0;
}

static int tegra_hsp_add_mailboxes(struct tegra_hsp *hsp, struct device *dev)
{
	int i;

	hsp->mailboxes = devm_kcalloc(dev, hsp->num_sm, sizeof(*hsp->mailboxes),
				      GFP_KERNEL);
	if (!hsp->mailboxes)
		return -ENOMEM;

	for (i = 0; i < hsp->num_sm; i++) {
		struct tegra_hsp_mailbox *mb = &hsp->mailboxes[i];

		mb->index = i;

		mb->channel.hsp = hsp;
		mb->channel.regs = hsp->regs + SZ_64K + i * SZ_32K;
		mb->channel.chan = &hsp->mbox_sm.chans[i];
		mb->channel.chan->con_priv = mb;
	}

	return 0;
}

static int tegra_hsp_request_shared_irq(struct tegra_hsp *hsp)
{
	unsigned int i, irq = 0;
	int err;

	for (i = 0; i < hsp->num_si; i++) {
		irq = hsp->shared_irqs[i];
		if (irq <= 0)
			continue;

		err = devm_request_irq(hsp->dev, irq, tegra_hsp_shared_irq, 0,
				       dev_name(hsp->dev), hsp);
		if (err < 0) {
			dev_err(hsp->dev, "failed to request interrupt: %d\n",
				err);
			continue;
		}

		hsp->shared_irq = i;

		/* disable all interrupts */
		tegra_hsp_writel(hsp, 0, HSP_INT_IE(hsp->shared_irq));

		dev_dbg(hsp->dev, "interrupt requested: %u\n", irq);

		break;
	}

	if (i == hsp->num_si) {
		dev_err(hsp->dev, "failed to find available interrupt\n");
		return -ENOENT;
	}

	return 0;
}

static int tegra_hsp_probe(struct platform_device *pdev)
{
	struct tegra_hsp *hsp;
	struct resource *res;
	unsigned int i;
	u32 value;
	int err;

	hsp = devm_kzalloc(&pdev->dev, sizeof(*hsp), GFP_KERNEL);
	if (!hsp)
		return -ENOMEM;

	hsp->dev = &pdev->dev;
	hsp->soc = of_device_get_match_data(&pdev->dev);
	INIT_LIST_HEAD(&hsp->doorbells);
	spin_lock_init(&hsp->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hsp->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hsp->regs))
		return PTR_ERR(hsp->regs);

	value = tegra_hsp_readl(hsp, HSP_INT_DIMENSIONING);
	hsp->num_sm = (value >> HSP_nSM_SHIFT) & HSP_nINT_MASK;
	hsp->num_ss = (value >> HSP_nSS_SHIFT) & HSP_nINT_MASK;
	hsp->num_as = (value >> HSP_nAS_SHIFT) & HSP_nINT_MASK;
	hsp->num_db = (value >> HSP_nDB_SHIFT) & HSP_nINT_MASK;
	hsp->num_si = (value >> HSP_nSI_SHIFT) & HSP_nINT_MASK;

	err = platform_get_irq_byname(pdev, "doorbell");
	if (err >= 0)
		hsp->doorbell_irq = err;

	if (hsp->num_si > 0) {
		unsigned int count = 0;

		hsp->shared_irqs = devm_kcalloc(&pdev->dev, hsp->num_si,
						sizeof(*hsp->shared_irqs),
						GFP_KERNEL);
		if (!hsp->shared_irqs)
			return -ENOMEM;

		for (i = 0; i < hsp->num_si; i++) {
			char *name;

			name = kasprintf(GFP_KERNEL, "shared%u", i);
			if (!name)
				return -ENOMEM;

			err = platform_get_irq_byname(pdev, name);
			if (err >= 0) {
				hsp->shared_irqs[i] = err;
				count++;
			}

			kfree(name);
		}

		if (count == 0) {
			devm_kfree(&pdev->dev, hsp->shared_irqs);
			hsp->shared_irqs = NULL;
		}
	}

	/* setup the doorbell controller */
	hsp->mbox_db.of_xlate = tegra_hsp_db_xlate;
	hsp->mbox_db.num_chans = 32;
	hsp->mbox_db.dev = &pdev->dev;
	hsp->mbox_db.ops = &tegra_hsp_db_ops;

	hsp->mbox_db.chans = devm_kcalloc(&pdev->dev, hsp->mbox_db.num_chans,
					  sizeof(*hsp->mbox_db.chans),
					  GFP_KERNEL);
	if (!hsp->mbox_db.chans)
		return -ENOMEM;

	if (hsp->doorbell_irq) {
		err = tegra_hsp_add_doorbells(hsp);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to add doorbells: %d\n",
			        err);
			return err;
		}
	}

	err = devm_mbox_controller_register(&pdev->dev, &hsp->mbox_db);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register doorbell mailbox: %d\n",
			err);
		return err;
	}

	/* setup the shared mailbox controller */
	hsp->mbox_sm.of_xlate = tegra_hsp_sm_xlate;
	hsp->mbox_sm.num_chans = hsp->num_sm;
	hsp->mbox_sm.dev = &pdev->dev;
	hsp->mbox_sm.ops = &tegra_hsp_sm_ops;

	hsp->mbox_sm.chans = devm_kcalloc(&pdev->dev, hsp->mbox_sm.num_chans,
					  sizeof(*hsp->mbox_sm.chans),
					  GFP_KERNEL);
	if (!hsp->mbox_sm.chans)
		return -ENOMEM;

	if (hsp->shared_irqs) {
		err = tegra_hsp_add_mailboxes(hsp, &pdev->dev);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to add mailboxes: %d\n",
			        err);
			return err;
		}
	}

	err = devm_mbox_controller_register(&pdev->dev, &hsp->mbox_sm);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register shared mailbox: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, hsp);

	if (hsp->doorbell_irq) {
		err = devm_request_irq(&pdev->dev, hsp->doorbell_irq,
				       tegra_hsp_doorbell_irq, IRQF_NO_SUSPEND,
				       dev_name(&pdev->dev), hsp);
		if (err < 0) {
			dev_err(&pdev->dev,
			        "failed to request doorbell IRQ#%u: %d\n",
				hsp->doorbell_irq, err);
			return err;
		}
	}

	if (hsp->shared_irqs) {
		err = tegra_hsp_request_shared_irq(hsp);
		if (err < 0)
			return err;
	}

	return 0;
}

static int __maybe_unused tegra_hsp_resume(struct device *dev)
{
	struct tegra_hsp *hsp = dev_get_drvdata(dev);
	unsigned int i;
	struct tegra_hsp_doorbell *db;

	list_for_each_entry(db, &hsp->doorbells, list) {
		if (db && db->channel.chan)
			tegra_hsp_doorbell_startup(db->channel.chan);
	}

	for (i = 0; i < hsp->num_sm; i++) {
		struct tegra_hsp_mailbox *mb = &hsp->mailboxes[i];

		if (mb->channel.chan->cl)
			tegra_hsp_mailbox_startup(mb->channel.chan);
	}

	return 0;
}

static const struct dev_pm_ops tegra_hsp_pm_ops = {
	.resume_noirq = tegra_hsp_resume,
};

static const struct tegra_hsp_db_map tegra186_hsp_db_map[] = {
	{ "ccplex", TEGRA_HSP_DB_MASTER_CCPLEX, HSP_DB_CCPLEX, },
	{ "bpmp",   TEGRA_HSP_DB_MASTER_BPMP,   HSP_DB_BPMP,   },
	{ /* sentinel */ }
};

static const struct tegra_hsp_soc tegra186_hsp_soc = {
	.map = tegra186_hsp_db_map,
	.has_per_mb_ie = false,
};

static const struct tegra_hsp_soc tegra194_hsp_soc = {
	.map = tegra186_hsp_db_map,
	.has_per_mb_ie = true,
};

static const struct of_device_id tegra_hsp_match[] = {
	{ .compatible = "nvidia,tegra186-hsp", .data = &tegra186_hsp_soc },
	{ .compatible = "nvidia,tegra194-hsp", .data = &tegra194_hsp_soc },
	{ }
};

static struct platform_driver tegra_hsp_driver = {
	.driver = {
		.name = "tegra-hsp",
		.of_match_table = tegra_hsp_match,
		.pm = &tegra_hsp_pm_ops,
	},
	.probe = tegra_hsp_probe,
};

static int __init tegra_hsp_init(void)
{
	return platform_driver_register(&tegra_hsp_driver);
}
core_initcall(tegra_hsp_init);

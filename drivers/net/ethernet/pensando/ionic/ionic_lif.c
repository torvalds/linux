// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/cpumask.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

static struct ionic_lif *ionic_lif_alloc(struct ionic *ionic, unsigned int index)
{
	struct device *dev = ionic->dev;
	struct net_device *netdev;
	struct ionic_lif *lif;
	int err;

	netdev = alloc_etherdev_mqs(sizeof(*lif),
				    ionic->ntxqs_per_lif, ionic->ntxqs_per_lif);
	if (!netdev) {
		dev_err(dev, "Cannot allocate netdev, aborting\n");
		return ERR_PTR(-ENOMEM);
	}

	SET_NETDEV_DEV(netdev, dev);

	lif = netdev_priv(netdev);
	lif->netdev = netdev;

	lif->neqs = ionic->neqs_per_lif;
	lif->nxqs = ionic->ntxqs_per_lif;

	lif->ionic = ionic;
	lif->index = index;

	snprintf(lif->name, sizeof(lif->name), "lif%u", index);

	/* allocate lif info */
	lif->info_sz = ALIGN(sizeof(*lif->info), PAGE_SIZE);
	lif->info = dma_alloc_coherent(dev, lif->info_sz,
				       &lif->info_pa, GFP_KERNEL);
	if (!lif->info) {
		dev_err(dev, "Failed to allocate lif info, aborting\n");
		err = -ENOMEM;
		goto err_out_free_netdev;
	}

	list_add_tail(&lif->list, &ionic->lifs);

	return lif;

err_out_free_netdev:
	free_netdev(lif->netdev);
	lif = NULL;

	return ERR_PTR(err);
}

int ionic_lifs_alloc(struct ionic *ionic)
{
	struct ionic_lif *lif;

	INIT_LIST_HEAD(&ionic->lifs);

	/* only build the first lif, others are for later features */
	set_bit(0, ionic->lifbits);
	lif = ionic_lif_alloc(ionic, 0);

	return PTR_ERR_OR_ZERO(lif);
}

static void ionic_lif_reset(struct ionic_lif *lif)
{
	struct ionic_dev *idev = &lif->ionic->idev;

	mutex_lock(&lif->ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_reset(idev, lif->index);
	ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&lif->ionic->dev_cmd_lock);
}

static void ionic_lif_free(struct ionic_lif *lif)
{
	struct device *dev = lif->ionic->dev;

	ionic_lif_reset(lif);

	/* free lif info */
	dma_free_coherent(dev, lif->info_sz, lif->info, lif->info_pa);
	lif->info = NULL;
	lif->info_pa = 0;

	/* free netdev & lif */
	ionic_debugfs_del_lif(lif);
	list_del(&lif->list);
	free_netdev(lif->netdev);
}

void ionic_lifs_free(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);

		ionic_lif_free(lif);
	}
}

static void ionic_lif_deinit(struct ionic_lif *lif)
{
	if (!test_bit(IONIC_LIF_INITED, lif->state))
		return;

	clear_bit(IONIC_LIF_INITED, lif->state);

	ionic_lif_reset(lif);
}

void ionic_lifs_deinit(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);
		ionic_lif_deinit(lif);
	}
}

static int ionic_lif_init(struct ionic_lif *lif)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_lif_init_comp comp;
	int err;

	ionic_debugfs_add_lif(lif);

	mutex_lock(&lif->ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_init(idev, lif->index, lif->info_pa);
	err = ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
	ionic_dev_cmd_comp(idev, (union ionic_dev_cmd_comp *)&comp);
	mutex_unlock(&lif->ionic->dev_cmd_lock);
	if (err)
		return err;

	lif->hw_index = le16_to_cpu(comp.hw_index);

	set_bit(IONIC_LIF_INITED, lif->state);

	return 0;
}

int ionic_lifs_init(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;
	int err;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);
		err = ionic_lif_init(lif);
		if (err)
			return err;
	}

	return 0;
}

int ionic_lif_identify(struct ionic *ionic, u8 lif_type,
		       union ionic_lif_identity *lid)
{
	struct ionic_dev *idev = &ionic->idev;
	size_t sz;
	int err;

	sz = min(sizeof(*lid), sizeof(idev->dev_cmd_regs->data));

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_identify(idev, lif_type, IONIC_IDENTITY_VERSION_1);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	memcpy_fromio(lid, &idev->dev_cmd_regs->data, sz);
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err)
		return (err);

	dev_dbg(ionic->dev, "capabilities 0x%llx\n",
		le64_to_cpu(lid->capabilities));

	dev_dbg(ionic->dev, "eth.max_ucast_filters %d\n",
		le32_to_cpu(lid->eth.max_ucast_filters));
	dev_dbg(ionic->dev, "eth.max_mcast_filters %d\n",
		le32_to_cpu(lid->eth.max_mcast_filters));
	dev_dbg(ionic->dev, "eth.features 0x%llx\n",
		le64_to_cpu(lid->eth.config.features));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_ADMINQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_ADMINQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_NOTIFYQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_NOTIFYQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_RXQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_RXQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_TXQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_TXQ]));
	dev_dbg(ionic->dev, "eth.config.name %s\n", lid->eth.config.name);
	dev_dbg(ionic->dev, "eth.config.mac %pM\n", lid->eth.config.mac);
	dev_dbg(ionic->dev, "eth.config.mtu %d\n",
		le32_to_cpu(lid->eth.config.mtu));

	return 0;
}

int ionic_lifs_size(struct ionic *ionic)
{
	struct ionic_identity *ident = &ionic->ident;
	unsigned int nintrs, dev_nintrs;
	union ionic_lif_config *lc;
	unsigned int ntxqs_per_lif;
	unsigned int nrxqs_per_lif;
	unsigned int neqs_per_lif;
	unsigned int nnqs_per_lif;
	unsigned int nxqs, neqs;
	unsigned int min_intrs;
	int err;

	lc = &ident->lif.eth.config;
	dev_nintrs = le32_to_cpu(ident->dev.nintrs);
	neqs_per_lif = le32_to_cpu(ident->lif.rdma.eq_qtype.qid_count);
	nnqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_NOTIFYQ]);
	ntxqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_TXQ]);
	nrxqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_RXQ]);

	nxqs = min(ntxqs_per_lif, nrxqs_per_lif);
	nxqs = min(nxqs, num_online_cpus());
	neqs = min(neqs_per_lif, num_online_cpus());

try_again:
	/* interrupt usage:
	 *    1 for master lif adminq/notifyq
	 *    1 for each CPU for master lif TxRx queue pairs
	 *    whatever's left is for RDMA queues
	 */
	nintrs = 1 + nxqs + neqs;
	min_intrs = 2;  /* adminq + 1 TxRx queue pair */

	if (nintrs > dev_nintrs)
		goto try_fewer;

	err = ionic_bus_alloc_irq_vectors(ionic, nintrs);
	if (err < 0 && err != -ENOSPC) {
		dev_err(ionic->dev, "Can't get intrs from OS: %d\n", err);
		return err;
	}
	if (err == -ENOSPC)
		goto try_fewer;

	if (err != nintrs) {
		ionic_bus_free_irq_vectors(ionic);
		goto try_fewer;
	}

	ionic->nnqs_per_lif = nnqs_per_lif;
	ionic->neqs_per_lif = neqs;
	ionic->ntxqs_per_lif = nxqs;
	ionic->nrxqs_per_lif = nxqs;
	ionic->nintrs = nintrs;

	ionic_debugfs_add_sizes(ionic);

	return 0;

try_fewer:
	if (nnqs_per_lif > 1) {
		nnqs_per_lif >>= 1;
		goto try_again;
	}
	if (neqs > 1) {
		neqs >>= 1;
		goto try_again;
	}
	if (nxqs > 1) {
		nxqs >>= 1;
		goto try_again;
	}
	dev_err(ionic->dev, "Can't get minimum %d intrs from OS\n", min_intrs);
	return -ENOSPC;
}

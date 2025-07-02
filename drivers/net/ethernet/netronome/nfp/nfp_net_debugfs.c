// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "nfp_net.h"
#include "nfp_net_dp.h"

static struct dentry *nfp_dir;

static int nfp_rx_q_show(struct seq_file *file, void *data)
{
	struct nfp_net_r_vector *r_vec = file->private;
	struct nfp_net_rx_ring *rx_ring;
	int fl_rd_p, fl_wr_p, rxd_cnt;
	struct nfp_net_rx_desc *rxd;
	struct nfp_net *nn;
	void *frag;
	int i;

	rtnl_lock();

	if (!r_vec->nfp_net || !r_vec->rx_ring)
		goto out;
	nn = r_vec->nfp_net;
	rx_ring = r_vec->rx_ring;
	if (!nfp_net_running(nn))
		goto out;

	rxd_cnt = rx_ring->cnt;

	fl_rd_p = nfp_qcp_rd_ptr_read(rx_ring->qcp_fl);
	fl_wr_p = nfp_qcp_wr_ptr_read(rx_ring->qcp_fl);

	seq_printf(file, "RX[%02d,%02d]: cnt=%u dma=%pad host=%p   H_RD=%u H_WR=%u FL_RD=%u FL_WR=%u\n",
		   rx_ring->idx, rx_ring->fl_qcidx,
		   rx_ring->cnt, &rx_ring->dma, rx_ring->rxds,
		   rx_ring->rd_p, rx_ring->wr_p, fl_rd_p, fl_wr_p);

	for (i = 0; i < rxd_cnt; i++) {
		rxd = &rx_ring->rxds[i];
		seq_printf(file, "%04d: 0x%08x 0x%08x", i,
			   rxd->vals[0], rxd->vals[1]);

		if (!r_vec->xsk_pool) {
			frag = READ_ONCE(rx_ring->rxbufs[i].frag);
			if (frag)
				seq_printf(file, " frag=%p", frag);

			if (rx_ring->rxbufs[i].dma_addr)
				seq_printf(file, " dma_addr=%pad",
					   &rx_ring->rxbufs[i].dma_addr);
		} else {
			if (rx_ring->xsk_rxbufs[i].dma_addr)
				seq_printf(file, " dma_addr=%pad",
					   &rx_ring->xsk_rxbufs[i].dma_addr);
		}

		if (i == rx_ring->rd_p % rxd_cnt)
			seq_puts(file, " H_RD ");
		if (i == rx_ring->wr_p % rxd_cnt)
			seq_puts(file, " H_WR ");
		if (i == fl_rd_p % rxd_cnt)
			seq_puts(file, " FL_RD");
		if (i == fl_wr_p % rxd_cnt)
			seq_puts(file, " FL_WR");

		seq_putc(file, '\n');
	}
out:
	rtnl_unlock();
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(nfp_rx_q);

static int nfp_tx_q_show(struct seq_file *file, void *data);
DEFINE_SHOW_ATTRIBUTE(nfp_tx_q);

static int __nfp_tx_q_show(struct seq_file *file, void *data, bool is_xdp)
{
	struct nfp_net_r_vector *r_vec = file->private;
	struct nfp_net_tx_ring *tx_ring;
	struct nfp_net *nn;
	int d_rd_p, d_wr_p;

	rtnl_lock();

	if (is_xdp)
		tx_ring = r_vec->xdp_ring;
	else
		tx_ring = r_vec->tx_ring;
	if (!r_vec->nfp_net || !tx_ring)
		goto out;
	nn = r_vec->nfp_net;
	if (!nfp_net_running(nn))
		goto out;

	d_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);
	d_wr_p = nfp_qcp_wr_ptr_read(tx_ring->qcp_q);

	seq_printf(file, "TX[%02d,%02d%s]: cnt=%u dma=%pad host=%p   H_RD=%u H_WR=%u D_RD=%u D_WR=%u",
		   tx_ring->idx, tx_ring->qcidx,
		   tx_ring == r_vec->tx_ring ? "" : "xdp",
		   tx_ring->cnt, &tx_ring->dma, tx_ring->txds,
		   tx_ring->rd_p, tx_ring->wr_p, d_rd_p, d_wr_p);
	if (tx_ring->txrwb)
		seq_printf(file, " TXRWB=%llu", *tx_ring->txrwb);
	seq_putc(file, '\n');

	nfp_net_debugfs_print_tx_descs(file, &nn->dp, r_vec, tx_ring,
				       d_rd_p, d_wr_p);
out:
	rtnl_unlock();
	return 0;
}

static int nfp_tx_q_show(struct seq_file *file, void *data)
{
	return __nfp_tx_q_show(file, data, false);
}

static int nfp_xdp_q_show(struct seq_file *file, void *data)
{
	return __nfp_tx_q_show(file, data, true);
}
DEFINE_SHOW_ATTRIBUTE(nfp_xdp_q);

void nfp_net_debugfs_vnic_add(struct nfp_net *nn, struct dentry *ddir)
{
	struct dentry *queues, *tx, *rx, *xdp;
	char name[20];
	int i;

	if (IS_ERR_OR_NULL(nfp_dir))
		return;

	if (nfp_net_is_data_vnic(nn))
		sprintf(name, "vnic%d", nn->id);
	else
		strcpy(name, "ctrl-vnic");
	nn->debugfs_dir = debugfs_create_dir(name, ddir);

	/* Create queue debugging sub-tree */
	queues = debugfs_create_dir("queue", nn->debugfs_dir);

	rx = debugfs_create_dir("rx", queues);
	tx = debugfs_create_dir("tx", queues);
	xdp = debugfs_create_dir("xdp", queues);

	for (i = 0; i < min(nn->max_rx_rings, nn->max_r_vecs); i++) {
		sprintf(name, "%d", i);
		debugfs_create_file(name, 0400, rx,
				    &nn->r_vecs[i], &nfp_rx_q_fops);
		debugfs_create_file(name, 0400, xdp,
				    &nn->r_vecs[i], &nfp_xdp_q_fops);
	}

	for (i = 0; i < min(nn->max_tx_rings, nn->max_r_vecs); i++) {
		sprintf(name, "%d", i);
		debugfs_create_file(name, 0400, tx,
				    &nn->r_vecs[i], &nfp_tx_q_fops);
	}
}

struct dentry *nfp_net_debugfs_device_add(struct pci_dev *pdev)
{
	return debugfs_create_dir(pci_name(pdev), nfp_dir);
}

void nfp_net_debugfs_dir_clean(struct dentry **dir)
{
	debugfs_remove_recursive(*dir);
	*dir = NULL;
}

void nfp_net_debugfs_create(void)
{
	nfp_dir = debugfs_create_dir("nfp_net", NULL);
}

void nfp_net_debugfs_destroy(void)
{
	debugfs_remove_recursive(nfp_dir);
	nfp_dir = NULL;
}

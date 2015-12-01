/*
 * Copyright (C) 2015 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "nfp_net.h"

static struct dentry *nfp_dir;

static int nfp_net_debugfs_rx_q_read(struct seq_file *file, void *data)
{
	struct nfp_net_rx_ring *rx_ring = file->private;
	int fl_rd_p, fl_wr_p, rx_rd_p, rx_wr_p, rxd_cnt;
	struct nfp_net_rx_desc *rxd;
	struct sk_buff *skb;
	struct nfp_net *nn;
	int i;

	rtnl_lock();

	if (!rx_ring->r_vec || !rx_ring->r_vec->nfp_net)
		goto out;
	nn = rx_ring->r_vec->nfp_net;
	if (!netif_running(nn->netdev))
		goto out;

	rxd_cnt = rx_ring->cnt;

	fl_rd_p = nfp_qcp_rd_ptr_read(rx_ring->qcp_fl);
	fl_wr_p = nfp_qcp_wr_ptr_read(rx_ring->qcp_fl);
	rx_rd_p = nfp_qcp_rd_ptr_read(rx_ring->qcp_rx);
	rx_wr_p = nfp_qcp_wr_ptr_read(rx_ring->qcp_rx);

	seq_printf(file, "RX[%02d]: H_RD=%d H_WR=%d FL_RD=%d FL_WR=%d RX_RD=%d RX_WR=%d\n",
		   rx_ring->idx, rx_ring->rd_p, rx_ring->wr_p,
		   fl_rd_p, fl_wr_p, rx_rd_p, rx_wr_p);

	for (i = 0; i < rxd_cnt; i++) {
		rxd = &rx_ring->rxds[i];
		seq_printf(file, "%04d: 0x%08x 0x%08x", i,
			   rxd->vals[0], rxd->vals[1]);

		skb = READ_ONCE(rx_ring->rxbufs[i].skb);
		if (skb)
			seq_printf(file, " skb->head=%p skb->data=%p",
				   skb->head, skb->data);

		if (rx_ring->rxbufs[i].dma_addr)
			seq_printf(file, " dma_addr=%pad",
				   &rx_ring->rxbufs[i].dma_addr);

		if (i == rx_ring->rd_p % rxd_cnt)
			seq_puts(file, " H_RD ");
		if (i == rx_ring->wr_p % rxd_cnt)
			seq_puts(file, " H_WR ");
		if (i == fl_rd_p % rxd_cnt)
			seq_puts(file, " FL_RD");
		if (i == fl_wr_p % rxd_cnt)
			seq_puts(file, " FL_WR");
		if (i == rx_rd_p % rxd_cnt)
			seq_puts(file, " RX_RD");
		if (i == rx_wr_p % rxd_cnt)
			seq_puts(file, " RX_WR");

		seq_putc(file, '\n');
	}
out:
	rtnl_unlock();
	return 0;
}

static int nfp_net_debugfs_rx_q_open(struct inode *inode, struct file *f)
{
	return single_open(f, nfp_net_debugfs_rx_q_read, inode->i_private);
}

static const struct file_operations nfp_rx_q_fops = {
	.owner = THIS_MODULE,
	.open = nfp_net_debugfs_rx_q_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static int nfp_net_debugfs_tx_q_read(struct seq_file *file, void *data)
{
	struct nfp_net_tx_ring *tx_ring = file->private;
	struct nfp_net_tx_desc *txd;
	int d_rd_p, d_wr_p, txd_cnt;
	struct sk_buff *skb;
	struct nfp_net *nn;
	int i;

	rtnl_lock();

	if (!tx_ring->r_vec || !tx_ring->r_vec->nfp_net)
		goto out;
	nn = tx_ring->r_vec->nfp_net;
	if (!netif_running(nn->netdev))
		goto out;

	txd_cnt = tx_ring->cnt;

	d_rd_p = nfp_qcp_rd_ptr_read(tx_ring->qcp_q);
	d_wr_p = nfp_qcp_wr_ptr_read(tx_ring->qcp_q);

	seq_printf(file, "TX[%02d]: H_RD=%d H_WR=%d D_RD=%d D_WR=%d\n",
		   tx_ring->idx, tx_ring->rd_p, tx_ring->wr_p, d_rd_p, d_wr_p);

	for (i = 0; i < txd_cnt; i++) {
		txd = &tx_ring->txds[i];
		seq_printf(file, "%04d: 0x%08x 0x%08x 0x%08x 0x%08x", i,
			   txd->vals[0], txd->vals[1],
			   txd->vals[2], txd->vals[3]);

		skb = READ_ONCE(tx_ring->txbufs[i].skb);
		if (skb)
			seq_printf(file, " skb->head=%p skb->data=%p",
				   skb->head, skb->data);
		if (tx_ring->txbufs[i].dma_addr)
			seq_printf(file, " dma_addr=%pad",
				   &tx_ring->txbufs[i].dma_addr);

		if (i == tx_ring->rd_p % txd_cnt)
			seq_puts(file, " H_RD");
		if (i == tx_ring->wr_p % txd_cnt)
			seq_puts(file, " H_WR");
		if (i == d_rd_p % txd_cnt)
			seq_puts(file, " D_RD");
		if (i == d_wr_p % txd_cnt)
			seq_puts(file, " D_WR");

		seq_putc(file, '\n');
	}
out:
	rtnl_unlock();
	return 0;
}

static int nfp_net_debugfs_tx_q_open(struct inode *inode, struct file *f)
{
	return single_open(f, nfp_net_debugfs_tx_q_read, inode->i_private);
}

static const struct file_operations nfp_tx_q_fops = {
	.owner = THIS_MODULE,
	.open = nfp_net_debugfs_tx_q_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

void nfp_net_debugfs_adapter_add(struct nfp_net *nn)
{
	static struct dentry *queues, *tx, *rx;
	char int_name[16];
	int i;

	if (IS_ERR_OR_NULL(nfp_dir))
		return;

	nn->debugfs_dir = debugfs_create_dir(pci_name(nn->pdev), nfp_dir);
	if (IS_ERR_OR_NULL(nn->debugfs_dir))
		return;

	/* Create queue debugging sub-tree */
	queues = debugfs_create_dir("queue", nn->debugfs_dir);
	if (IS_ERR_OR_NULL(nn->debugfs_dir))
		return;

	rx = debugfs_create_dir("rx", queues);
	tx = debugfs_create_dir("tx", queues);
	if (IS_ERR_OR_NULL(rx) || IS_ERR_OR_NULL(tx))
		return;

	for (i = 0; i < nn->num_rx_rings; i++) {
		sprintf(int_name, "%d", i);
		debugfs_create_file(int_name, S_IRUSR, rx,
				    &nn->rx_rings[i], &nfp_rx_q_fops);
	}

	for (i = 0; i < nn->num_tx_rings; i++) {
		sprintf(int_name, "%d", i);
		debugfs_create_file(int_name, S_IRUSR, tx,
				    &nn->tx_rings[i], &nfp_tx_q_fops);
	}
}

void nfp_net_debugfs_adapter_del(struct nfp_net *nn)
{
	debugfs_remove_recursive(nn->debugfs_dir);
	nn->debugfs_dir = NULL;
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

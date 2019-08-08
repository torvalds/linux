// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Intel SCIF driver.
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "../common/mic_dev.h"
#include "scif_main.h"

/* Debugfs parent dir */
static struct dentry *scif_dbg;

static int scif_dev_show(struct seq_file *s, void *unused)
{
	int node;

	seq_printf(s, "Total Nodes %d Self Node Id %d Maxid %d\n",
		   scif_info.total, scif_info.nodeid,
		   scif_info.maxid);

	if (!scif_dev)
		return 0;

	seq_printf(s, "%-16s\t%-16s\n", "node_id", "state");

	for (node = 0; node <= scif_info.maxid; node++)
		seq_printf(s, "%-16d\t%-16s\n", scif_dev[node].node,
			   _scifdev_alive(&scif_dev[node]) ?
			   "Running" : "Offline");
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(scif_dev);

static void scif_display_window(struct scif_window *window, struct seq_file *s)
{
	int j;
	struct scatterlist *sg;
	scif_pinned_pages_t pin = window->pinned_pages;

	seq_printf(s, "window %p type %d temp %d offset 0x%llx ",
		   window, window->type, window->temp, window->offset);
	seq_printf(s, "nr_pages 0x%llx nr_contig_chunks 0x%x prot %d ",
		   window->nr_pages, window->nr_contig_chunks, window->prot);
	seq_printf(s, "ref_count %d magic 0x%llx peer_window 0x%llx ",
		   window->ref_count, window->magic, window->peer_window);
	seq_printf(s, "unreg_state 0x%x va_for_temp 0x%lx\n",
		   window->unreg_state, window->va_for_temp);

	for (j = 0; j < window->nr_contig_chunks; j++)
		seq_printf(s, "page[%d] dma_addr 0x%llx num_pages 0x%llx\n", j,
			   window->dma_addr[j], window->num_pages[j]);

	if (window->type == SCIF_WINDOW_SELF && pin)
		for (j = 0; j < window->nr_pages; j++)
			seq_printf(s, "page[%d] = pinned_pages %p address %p\n",
				   j, pin->pages[j],
				   page_address(pin->pages[j]));

	if (window->st)
		for_each_sg(window->st->sgl, sg, window->st->nents, j)
			seq_printf(s, "sg[%d] dma addr 0x%llx length 0x%x\n",
				   j, sg_dma_address(sg), sg_dma_len(sg));
}

static void scif_display_all_windows(struct list_head *head, struct seq_file *s)
{
	struct list_head *item;
	struct scif_window *window;

	list_for_each(item, head) {
		window = list_entry(item, struct scif_window, list);
		scif_display_window(window, s);
	}
}

static int scif_rma_show(struct seq_file *s, void *unused)
{
	struct scif_endpt *ep;
	struct list_head *pos;

	mutex_lock(&scif_info.connlock);
	list_for_each(pos, &scif_info.connected) {
		ep = list_entry(pos, struct scif_endpt, list);
		seq_printf(s, "ep %p self windows\n", ep);
		mutex_lock(&ep->rma_info.rma_lock);
		scif_display_all_windows(&ep->rma_info.reg_list, s);
		seq_printf(s, "ep %p remote windows\n", ep);
		scif_display_all_windows(&ep->rma_info.remote_reg_list, s);
		mutex_unlock(&ep->rma_info.rma_lock);
	}
	mutex_unlock(&scif_info.connlock);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(scif_rma);

void __init scif_init_debugfs(void)
{
	scif_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!scif_dbg) {
		dev_err(scif_info.mdev.this_device,
			"can't create debugfs dir scif\n");
		return;
	}

	debugfs_create_file("scif_dev", 0444, scif_dbg, NULL, &scif_dev_fops);
	debugfs_create_file("scif_rma", 0444, scif_dbg, NULL, &scif_rma_fops);
	debugfs_create_u8("en_msg_log", 0666, scif_dbg, &scif_info.en_msg_log);
	debugfs_create_u8("p2p_enable", 0666, scif_dbg, &scif_info.p2p_enable);
}

void scif_exit_debugfs(void)
{
	debugfs_remove_recursive(scif_dbg);
}

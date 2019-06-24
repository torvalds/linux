// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Technologies HIDMA debug file
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/pm_runtime.h>

#include "hidma.h"

static void hidma_ll_chstats(struct seq_file *s, void *llhndl, u32 tre_ch)
{
	struct hidma_lldev *lldev = llhndl;
	struct hidma_tre *tre;
	u32 length;
	dma_addr_t src_start;
	dma_addr_t dest_start;
	u32 *tre_local;

	if (tre_ch >= lldev->nr_tres) {
		dev_err(lldev->dev, "invalid TRE number in chstats:%d", tre_ch);
		return;
	}
	tre = &lldev->trepool[tre_ch];
	seq_printf(s, "------Channel %d -----\n", tre_ch);
	seq_printf(s, "allocated=%d\n", atomic_read(&tre->allocated));
	seq_printf(s, "queued = 0x%x\n", tre->queued);
	seq_printf(s, "err_info = 0x%x\n", tre->err_info);
	seq_printf(s, "err_code = 0x%x\n", tre->err_code);
	seq_printf(s, "status = 0x%x\n", tre->status);
	seq_printf(s, "idx = 0x%x\n", tre->idx);
	seq_printf(s, "dma_sig = 0x%x\n", tre->dma_sig);
	seq_printf(s, "dev_name=%s\n", tre->dev_name);
	seq_printf(s, "callback=%p\n", tre->callback);
	seq_printf(s, "data=%p\n", tre->data);
	seq_printf(s, "tre_index = 0x%x\n", tre->tre_index);

	tre_local = &tre->tre_local[0];
	src_start = tre_local[HIDMA_TRE_SRC_LOW_IDX];
	src_start = ((u64) (tre_local[HIDMA_TRE_SRC_HI_IDX]) << 32) + src_start;
	dest_start = tre_local[HIDMA_TRE_DEST_LOW_IDX];
	dest_start += ((u64) (tre_local[HIDMA_TRE_DEST_HI_IDX]) << 32);
	length = tre_local[HIDMA_TRE_LEN_IDX];

	seq_printf(s, "src=%pap\n", &src_start);
	seq_printf(s, "dest=%pap\n", &dest_start);
	seq_printf(s, "length = 0x%x\n", length);
}

static void hidma_ll_devstats(struct seq_file *s, void *llhndl)
{
	struct hidma_lldev *lldev = llhndl;

	seq_puts(s, "------Device -----\n");
	seq_printf(s, "lldev init = 0x%x\n", lldev->initialized);
	seq_printf(s, "trch_state = 0x%x\n", lldev->trch_state);
	seq_printf(s, "evch_state = 0x%x\n", lldev->evch_state);
	seq_printf(s, "chidx = 0x%x\n", lldev->chidx);
	seq_printf(s, "nr_tres = 0x%x\n", lldev->nr_tres);
	seq_printf(s, "trca=%p\n", lldev->trca);
	seq_printf(s, "tre_ring=%p\n", lldev->tre_ring);
	seq_printf(s, "tre_ring_handle=%pap\n", &lldev->tre_dma);
	seq_printf(s, "tre_ring_size = 0x%x\n", lldev->tre_ring_size);
	seq_printf(s, "tre_processed_off = 0x%x\n", lldev->tre_processed_off);
	seq_printf(s, "pending_tre_count=%d\n",
			atomic_read(&lldev->pending_tre_count));
	seq_printf(s, "evca=%p\n", lldev->evca);
	seq_printf(s, "evre_ring=%p\n", lldev->evre_ring);
	seq_printf(s, "evre_ring_handle=%pap\n", &lldev->evre_dma);
	seq_printf(s, "evre_ring_size = 0x%x\n", lldev->evre_ring_size);
	seq_printf(s, "evre_processed_off = 0x%x\n", lldev->evre_processed_off);
	seq_printf(s, "tre_write_offset = 0x%x\n", lldev->tre_write_offset);
}

/*
 * hidma_chan_show: display HIDMA channel statistics
 *
 * Display the statistics for the current HIDMA virtual channel device.
 */
static int hidma_chan_show(struct seq_file *s, void *unused)
{
	struct hidma_chan *mchan = s->private;
	struct hidma_desc *mdesc;
	struct hidma_dev *dmadev = mchan->dmadev;

	pm_runtime_get_sync(dmadev->ddev.dev);
	seq_printf(s, "paused=%u\n", mchan->paused);
	seq_printf(s, "dma_sig=%u\n", mchan->dma_sig);
	seq_puts(s, "prepared\n");
	list_for_each_entry(mdesc, &mchan->prepared, node)
		hidma_ll_chstats(s, mchan->dmadev->lldev, mdesc->tre_ch);

	seq_puts(s, "active\n");
	list_for_each_entry(mdesc, &mchan->active, node)
		hidma_ll_chstats(s, mchan->dmadev->lldev, mdesc->tre_ch);

	seq_puts(s, "completed\n");
	list_for_each_entry(mdesc, &mchan->completed, node)
		hidma_ll_chstats(s, mchan->dmadev->lldev, mdesc->tre_ch);

	hidma_ll_devstats(s, mchan->dmadev->lldev);
	pm_runtime_mark_last_busy(dmadev->ddev.dev);
	pm_runtime_put_autosuspend(dmadev->ddev.dev);
	return 0;
}

/*
 * hidma_dma_show: display HIDMA device info
 *
 * Display the info for the current HIDMA device.
 */
static int hidma_dma_show(struct seq_file *s, void *unused)
{
	struct hidma_dev *dmadev = s->private;
	resource_size_t sz;

	seq_printf(s, "nr_descriptors=%d\n", dmadev->nr_descriptors);
	seq_printf(s, "dev_trca=%p\n", &dmadev->dev_trca);
	seq_printf(s, "dev_trca_phys=%pa\n", &dmadev->trca_resource->start);
	sz = resource_size(dmadev->trca_resource);
	seq_printf(s, "dev_trca_size=%pa\n", &sz);
	seq_printf(s, "dev_evca=%p\n", &dmadev->dev_evca);
	seq_printf(s, "dev_evca_phys=%pa\n", &dmadev->evca_resource->start);
	sz = resource_size(dmadev->evca_resource);
	seq_printf(s, "dev_evca_size=%pa\n", &sz);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hidma_chan);
DEFINE_SHOW_ATTRIBUTE(hidma_dma);

void hidma_debug_uninit(struct hidma_dev *dmadev)
{
	debugfs_remove_recursive(dmadev->debugfs);
}

int hidma_debug_init(struct hidma_dev *dmadev)
{
	int rc = 0;
	int chidx = 0;
	struct list_head *position = NULL;

	dmadev->debugfs = debugfs_create_dir(dev_name(dmadev->ddev.dev), NULL);
	if (!dmadev->debugfs) {
		rc = -ENODEV;
		return rc;
	}

	/* walk through the virtual channel list */
	list_for_each(position, &dmadev->ddev.channels) {
		struct hidma_chan *chan;

		chan = list_entry(position, struct hidma_chan,
				  chan.device_node);
		sprintf(chan->dbg_name, "chan%d", chidx);
		chan->debugfs = debugfs_create_dir(chan->dbg_name,
						   dmadev->debugfs);
		if (!chan->debugfs) {
			rc = -ENOMEM;
			goto cleanup;
		}
		chan->stats = debugfs_create_file("stats", S_IRUGO,
						  chan->debugfs, chan,
						  &hidma_chan_fops);
		if (!chan->stats) {
			rc = -ENOMEM;
			goto cleanup;
		}
		chidx++;
	}

	dmadev->stats = debugfs_create_file("stats", S_IRUGO,
					    dmadev->debugfs, dmadev,
					    &hidma_dma_fops);
	if (!dmadev->stats) {
		rc = -ENOMEM;
		goto cleanup;
	}

	return 0;
cleanup:
	hidma_debug_uninit(dmadev);
	return rc;
}

/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/kdebug.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/export.h>

#include "csio_init.h"
#include "csio_defs.h"

#define CSIO_MIN_MEMPOOL_SZ	64

static struct dentry *csio_debugfs_root;

static struct scsi_transport_template *csio_fcoe_transport;
static struct scsi_transport_template *csio_fcoe_transport_vport;

/*
 * debugfs support
 */
static ssize_t
csio_mem_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	loff_t avail = file_inode(file)->i_size;
	unsigned int mem = (uintptr_t)file->private_data & 3;
	struct csio_hw *hw = file->private_data - mem;

	if (pos < 0)
		return -EINVAL;
	if (pos >= avail)
		return 0;
	if (count > avail - pos)
		count = avail - pos;

	while (count) {
		size_t len;
		int ret, ofst;
		__be32 data[16];

		if (mem == MEM_MC)
			ret = hw->chip_ops->chip_mc_read(hw, 0, pos,
							 data, NULL);
		else
			ret = hw->chip_ops->chip_edc_read(hw, mem, pos,
							  data, NULL);
		if (ret)
			return ret;

		ofst = pos % sizeof(data);
		len = min(count, sizeof(data) - ofst);
		if (copy_to_user(buf, (u8 *)data + ofst, len))
			return -EFAULT;

		buf += len;
		pos += len;
		count -= len;
	}
	count = pos - *ppos;
	*ppos = pos;
	return count;
}

static const struct file_operations csio_mem_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	.read    = csio_mem_read,
	.llseek  = default_llseek,
};

void csio_add_debugfs_mem(struct csio_hw *hw, const char *name,
				 unsigned int idx, unsigned int size_mb)
{
	debugfs_create_file_size(name, S_IRUSR, hw->debugfs_root,
				 (void *)hw + idx, &csio_mem_debugfs_fops,
				 size_mb << 20);
}

static int csio_setup_debugfs(struct csio_hw *hw)
{
	int i;

	if (IS_ERR_OR_NULL(hw->debugfs_root))
		return -1;

	i = csio_rd_reg32(hw, MA_TARGET_MEM_ENABLE_A);
	if (i & EDRAM0_ENABLE_F)
		csio_add_debugfs_mem(hw, "edc0", MEM_EDC0, 5);
	if (i & EDRAM1_ENABLE_F)
		csio_add_debugfs_mem(hw, "edc1", MEM_EDC1, 5);

	hw->chip_ops->chip_dfs_create_ext_mem(hw);
	return 0;
}

/*
 * csio_dfs_create - Creates and sets up per-hw debugfs.
 *
 */
static int
csio_dfs_create(struct csio_hw *hw)
{
	if (csio_debugfs_root) {
		hw->debugfs_root = debugfs_create_dir(pci_name(hw->pdev),
							csio_debugfs_root);
		csio_setup_debugfs(hw);
	}

	return 0;
}

/*
 * csio_dfs_destroy - Destroys per-hw debugfs.
 */
static void
csio_dfs_destroy(struct csio_hw *hw)
{
	debugfs_remove_recursive(hw->debugfs_root);
}

/*
 * csio_dfs_init - Debug filesystem initialization for the module.
 *
 */
static void
csio_dfs_init(void)
{
	csio_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
}

/*
 * csio_dfs_exit - debugfs cleanup for the module.
 */
static void
csio_dfs_exit(void)
{
	debugfs_remove(csio_debugfs_root);
}

/*
 * csio_pci_init - PCI initialization.
 * @pdev: PCI device.
 * @bars: Bitmask of bars to be requested.
 *
 * Initializes the PCI function by enabling MMIO, setting bus
 * mastership and setting DMA mask.
 */
static int
csio_pci_init(struct pci_dev *pdev, int *bars)
{
	int rv = -ENODEV;

	*bars = pci_select_bars(pdev, IORESOURCE_MEM);

	if (pci_enable_device_mem(pdev))
		goto err;

	if (pci_request_selected_regions(pdev, *bars, KBUILD_MODNAME))
		goto err_disable_device;

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	rv = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rv)
		rv = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rv) {
		rv = -ENODEV;
		dev_err(&pdev->dev, "No suitable DMA available.\n");
		goto err_release_regions;
	}

	return 0;

err_release_regions:
	pci_release_selected_regions(pdev, *bars);
err_disable_device:
	pci_disable_device(pdev);
err:
	return rv;

}

/*
 * csio_pci_exit - PCI unitialization.
 * @pdev: PCI device.
 * @bars: Bars to be released.
 *
 */
static void
csio_pci_exit(struct pci_dev *pdev, int *bars)
{
	pci_release_selected_regions(pdev, *bars);
	pci_disable_device(pdev);
}

/*
 * csio_hw_init_workers - Initialize the HW module's worker threads.
 * @hw: HW module.
 *
 */
static void
csio_hw_init_workers(struct csio_hw *hw)
{
	INIT_WORK(&hw->evtq_work, csio_evtq_worker);
}

static void
csio_hw_exit_workers(struct csio_hw *hw)
{
	cancel_work_sync(&hw->evtq_work);
}

static int
csio_create_queues(struct csio_hw *hw)
{
	int i, j;
	struct csio_mgmtm *mgmtm = csio_hw_to_mgmtm(hw);
	int rv;
	struct csio_scsi_cpu_info *info;

	if (hw->flags & CSIO_HWF_Q_FW_ALLOCED)
		return 0;

	if (hw->intr_mode != CSIO_IM_MSIX) {
		rv = csio_wr_iq_create(hw, NULL, hw->intr_iq_idx,
					0, hw->pport[0].portid, false, NULL);
		if (rv != 0) {
			csio_err(hw, " Forward Interrupt IQ failed!: %d\n", rv);
			return rv;
		}
	}

	/* FW event queue */
	rv = csio_wr_iq_create(hw, NULL, hw->fwevt_iq_idx,
			       csio_get_fwevt_intr_idx(hw),
			       hw->pport[0].portid, true, NULL);
	if (rv != 0) {
		csio_err(hw, "FW event IQ config failed!: %d\n", rv);
		return rv;
	}

	/* Create mgmt queue */
	rv = csio_wr_eq_create(hw, NULL, mgmtm->eq_idx,
			mgmtm->iq_idx, hw->pport[0].portid, NULL);

	if (rv != 0) {
		csio_err(hw, "Mgmt EQ create failed!: %d\n", rv);
		goto err;
	}

	/* Create SCSI queues */
	for (i = 0; i < hw->num_pports; i++) {
		info = &hw->scsi_cpu_info[i];

		for (j = 0; j < info->max_cpus; j++) {
			struct csio_scsi_qset *sqset = &hw->sqset[i][j];

			rv = csio_wr_iq_create(hw, NULL, sqset->iq_idx,
					       sqset->intr_idx, i, false, NULL);
			if (rv != 0) {
				csio_err(hw,
				   "SCSI module IQ config failed [%d][%d]:%d\n",
				   i, j, rv);
				goto err;
			}
			rv = csio_wr_eq_create(hw, NULL, sqset->eq_idx,
					       sqset->iq_idx, i, NULL);
			if (rv != 0) {
				csio_err(hw,
				   "SCSI module EQ config failed [%d][%d]:%d\n",
				   i, j, rv);
				goto err;
			}
		} /* for all CPUs */
	} /* For all ports */

	hw->flags |= CSIO_HWF_Q_FW_ALLOCED;
	return 0;
err:
	csio_wr_destroy_queues(hw, true);
	return -EINVAL;
}

/*
 * csio_config_queues - Configure the DMA queues.
 * @hw: HW module.
 *
 * Allocates memory for queues are registers them with FW.
 */
int
csio_config_queues(struct csio_hw *hw)
{
	int i, j, idx, k = 0;
	int rv;
	struct csio_scsi_qset *sqset;
	struct csio_mgmtm *mgmtm = csio_hw_to_mgmtm(hw);
	struct csio_scsi_qset *orig;
	struct csio_scsi_cpu_info *info;

	if (hw->flags & CSIO_HWF_Q_MEM_ALLOCED)
		return csio_create_queues(hw);

	/* Calculate number of SCSI queues for MSIX we would like */
	hw->num_scsi_msix_cpus = num_online_cpus();
	hw->num_sqsets = num_online_cpus() * hw->num_pports;

	if (hw->num_sqsets > CSIO_MAX_SCSI_QSETS) {
		hw->num_sqsets = CSIO_MAX_SCSI_QSETS;
		hw->num_scsi_msix_cpus = CSIO_MAX_SCSI_CPU;
	}

	/* Initialize max_cpus, may get reduced during msix allocations */
	for (i = 0; i < hw->num_pports; i++)
		hw->scsi_cpu_info[i].max_cpus = hw->num_scsi_msix_cpus;

	csio_dbg(hw, "nsqsets:%d scpus:%d\n",
		    hw->num_sqsets, hw->num_scsi_msix_cpus);

	csio_intr_enable(hw);

	if (hw->intr_mode != CSIO_IM_MSIX) {

		/* Allocate Forward interrupt iq. */
		hw->intr_iq_idx = csio_wr_alloc_q(hw, CSIO_INTR_IQSIZE,
						CSIO_INTR_WRSIZE, CSIO_INGRESS,
						(void *)hw, 0, 0, NULL);
		if (hw->intr_iq_idx == -1) {
			csio_err(hw,
				 "Forward interrupt queue creation failed\n");
			goto intr_disable;
		}
	}

	/* Allocate the FW evt queue */
	hw->fwevt_iq_idx = csio_wr_alloc_q(hw, CSIO_FWEVT_IQSIZE,
					   CSIO_FWEVT_WRSIZE,
					   CSIO_INGRESS, (void *)hw,
					   CSIO_FWEVT_FLBUFS, 0,
					   csio_fwevt_intx_handler);
	if (hw->fwevt_iq_idx == -1) {
		csio_err(hw, "FW evt queue creation failed\n");
		goto intr_disable;
	}

	/* Allocate the mgmt queue */
	mgmtm->eq_idx = csio_wr_alloc_q(hw, CSIO_MGMT_EQSIZE,
				      CSIO_MGMT_EQ_WRSIZE,
				      CSIO_EGRESS, (void *)hw, 0, 0, NULL);
	if (mgmtm->eq_idx == -1) {
		csio_err(hw, "Failed to alloc egress queue for mgmt module\n");
		goto intr_disable;
	}

	/* Use FW IQ for MGMT req completion */
	mgmtm->iq_idx = hw->fwevt_iq_idx;

	/* Allocate SCSI queues */
	for (i = 0; i < hw->num_pports; i++) {
		info = &hw->scsi_cpu_info[i];

		for (j = 0; j < hw->num_scsi_msix_cpus; j++) {
			sqset = &hw->sqset[i][j];

			if (j >= info->max_cpus) {
				k = j % info->max_cpus;
				orig = &hw->sqset[i][k];
				sqset->eq_idx = orig->eq_idx;
				sqset->iq_idx = orig->iq_idx;
				continue;
			}

			idx = csio_wr_alloc_q(hw, csio_scsi_eqsize, 0,
					      CSIO_EGRESS, (void *)hw, 0, 0,
					      NULL);
			if (idx == -1) {
				csio_err(hw, "EQ creation failed for idx:%d\n",
					    idx);
				goto intr_disable;
			}

			sqset->eq_idx = idx;

			idx = csio_wr_alloc_q(hw, CSIO_SCSI_IQSIZE,
					     CSIO_SCSI_IQ_WRSZ, CSIO_INGRESS,
					     (void *)hw, 0, 0,
					     csio_scsi_intx_handler);
			if (idx == -1) {
				csio_err(hw, "IQ creation failed for idx:%d\n",
					    idx);
				goto intr_disable;
			}
			sqset->iq_idx = idx;
		} /* for all CPUs */
	} /* For all ports */

	hw->flags |= CSIO_HWF_Q_MEM_ALLOCED;

	rv = csio_create_queues(hw);
	if (rv != 0)
		goto intr_disable;

	/*
	 * Now request IRQs for the vectors. In the event of a failure,
	 * cleanup is handled internally by this function.
	 */
	rv = csio_request_irqs(hw);
	if (rv != 0)
		return -EINVAL;

	return 0;

intr_disable:
	csio_intr_disable(hw, false);

	return -EINVAL;
}

static int
csio_resource_alloc(struct csio_hw *hw)
{
	struct csio_wrm *wrm = csio_hw_to_wrm(hw);
	int rv = -ENOMEM;

	wrm->num_q = ((CSIO_MAX_SCSI_QSETS * 2) + CSIO_HW_NIQ +
		       CSIO_HW_NEQ + CSIO_HW_NFLQ + CSIO_HW_NINTXQ);

	hw->mb_mempool = mempool_create_kmalloc_pool(CSIO_MIN_MEMPOOL_SZ,
						  sizeof(struct csio_mb));
	if (!hw->mb_mempool)
		goto err;

	hw->rnode_mempool = mempool_create_kmalloc_pool(CSIO_MIN_MEMPOOL_SZ,
						     sizeof(struct csio_rnode));
	if (!hw->rnode_mempool)
		goto err_free_mb_mempool;

	hw->scsi_dma_pool = dma_pool_create("csio_scsi_dma_pool",
					    &hw->pdev->dev, CSIO_SCSI_RSP_LEN,
					    8, 0);
	if (!hw->scsi_dma_pool)
		goto err_free_rn_pool;

	return 0;

err_free_rn_pool:
	mempool_destroy(hw->rnode_mempool);
	hw->rnode_mempool = NULL;
err_free_mb_mempool:
	mempool_destroy(hw->mb_mempool);
	hw->mb_mempool = NULL;
err:
	return rv;
}

static void
csio_resource_free(struct csio_hw *hw)
{
	dma_pool_destroy(hw->scsi_dma_pool);
	hw->scsi_dma_pool = NULL;
	mempool_destroy(hw->rnode_mempool);
	hw->rnode_mempool = NULL;
	mempool_destroy(hw->mb_mempool);
	hw->mb_mempool = NULL;
}

/*
 * csio_hw_alloc - Allocate and initialize the HW module.
 * @pdev: PCI device.
 *
 * Allocates HW structure, DMA, memory resources, maps BARS to
 * host memory and initializes HW module.
 */
static struct csio_hw *csio_hw_alloc(struct pci_dev *pdev)
{
	struct csio_hw *hw;

	hw = kzalloc(sizeof(struct csio_hw), GFP_KERNEL);
	if (!hw)
		goto err;

	hw->pdev = pdev;
	strncpy(hw->drv_version, CSIO_DRV_VERSION, 32);

	/* memory pool/DMA pool allocation */
	if (csio_resource_alloc(hw))
		goto err_free_hw;

	/* Get the start address of registers from BAR 0 */
	hw->regstart = ioremap(pci_resource_start(pdev, 0),
				       pci_resource_len(pdev, 0));
	if (!hw->regstart) {
		csio_err(hw, "Could not map BAR 0, regstart = %p\n",
			 hw->regstart);
		goto err_resource_free;
	}

	csio_hw_init_workers(hw);

	if (csio_hw_init(hw))
		goto err_unmap_bar;

	csio_dfs_create(hw);

	csio_dbg(hw, "hw:%p\n", hw);

	return hw;

err_unmap_bar:
	csio_hw_exit_workers(hw);
	iounmap(hw->regstart);
err_resource_free:
	csio_resource_free(hw);
err_free_hw:
	kfree(hw);
err:
	return NULL;
}

/*
 * csio_hw_free - Uninitialize and free the HW module.
 * @hw: The HW module
 *
 * Disable interrupts, uninit the HW module, free resources, free hw.
 */
static void
csio_hw_free(struct csio_hw *hw)
{
	csio_intr_disable(hw, true);
	csio_hw_exit_workers(hw);
	csio_hw_exit(hw);
	iounmap(hw->regstart);
	csio_dfs_destroy(hw);
	csio_resource_free(hw);
	kfree(hw);
}

/**
 * csio_shost_init - Create and initialize the lnode module.
 * @hw:		The HW module.
 * @dev:	The device associated with this invocation.
 * @probe:	Called from probe context or not?
 * @os_pln:	Parent lnode if any.
 *
 * Allocates lnode structure via scsi_host_alloc, initializes
 * shost, initializes lnode module and registers with SCSI ML
 * via scsi_host_add. This function is shared between physical and
 * virtual node ports.
 */
struct csio_lnode *
csio_shost_init(struct csio_hw *hw, struct device *dev,
		  bool probe, struct csio_lnode *pln)
{
	struct Scsi_Host  *shost = NULL;
	struct csio_lnode *ln;

	csio_fcoe_shost_template.cmd_per_lun = csio_lun_qdepth;
	csio_fcoe_shost_vport_template.cmd_per_lun = csio_lun_qdepth;

	/*
	 * hw->pdev is the physical port's PCI dev structure,
	 * which will be different from the NPIV dev structure.
	 */
	if (dev == &hw->pdev->dev)
		shost = scsi_host_alloc(
				&csio_fcoe_shost_template,
				sizeof(struct csio_lnode));
	else
		shost = scsi_host_alloc(
				&csio_fcoe_shost_vport_template,
				sizeof(struct csio_lnode));

	if (!shost)
		goto err;

	ln = shost_priv(shost);
	memset(ln, 0, sizeof(struct csio_lnode));

	/* Link common lnode to this lnode */
	ln->dev_num = (shost->host_no << 16);

	shost->can_queue = CSIO_MAX_QUEUE;
	shost->this_id = -1;
	shost->unique_id = shost->host_no;
	shost->max_cmd_len = 16; /* Max CDB length supported */
	shost->max_id = min_t(uint32_t, csio_fcoe_rnodes,
			      hw->fres_info.max_ssns);
	shost->max_lun = CSIO_MAX_LUN;
	if (dev == &hw->pdev->dev)
		shost->transportt = csio_fcoe_transport;
	else
		shost->transportt = csio_fcoe_transport_vport;

	/* root lnode */
	if (!hw->rln)
		hw->rln = ln;

	/* Other initialization here: Common, Transport specific */
	if (csio_lnode_init(ln, hw, pln))
		goto err_shost_put;

	if (scsi_add_host_with_dma(shost, dev, &hw->pdev->dev))
		goto err_lnode_exit;

	return ln;

err_lnode_exit:
	csio_lnode_exit(ln);
err_shost_put:
	scsi_host_put(shost);
err:
	return NULL;
}

/**
 * csio_shost_exit - De-instantiate the shost.
 * @ln:		The lnode module corresponding to the shost.
 *
 */
void
csio_shost_exit(struct csio_lnode *ln)
{
	struct Scsi_Host *shost = csio_ln_to_shost(ln);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	/* Inform transport */
	fc_remove_host(shost);

	/* Inform SCSI ML */
	scsi_remove_host(shost);

	/* Flush all the events, so that any rnode removal events
	 * already queued are all handled, before we remove the lnode.
	 */
	spin_lock_irq(&hw->lock);
	csio_evtq_flush(hw);
	spin_unlock_irq(&hw->lock);

	csio_lnode_exit(ln);
	scsi_host_put(shost);
}

struct csio_lnode *
csio_lnode_alloc(struct csio_hw *hw)
{
	return csio_shost_init(hw, &hw->pdev->dev, false, NULL);
}

void
csio_lnodes_block_request(struct csio_hw *hw)
{
	struct Scsi_Host  *shost;
	struct csio_lnode *sln;
	struct csio_lnode *ln;
	struct list_head *cur_ln, *cur_cln;
	struct csio_lnode **lnode_list;
	int cur_cnt = 0, ii;

	lnode_list = kzalloc((sizeof(struct csio_lnode *) * hw->num_lns),
			GFP_KERNEL);
	if (!lnode_list) {
		csio_err(hw, "Failed to allocate lnodes_list");
		return;
	}

	spin_lock_irq(&hw->lock);
	/* Traverse sibling lnodes */
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;
		lnode_list[cur_cnt++] = sln;

		/* Traverse children lnodes */
		list_for_each(cur_cln, &sln->cln_head)
			lnode_list[cur_cnt++] = (struct csio_lnode *) cur_cln;
	}
	spin_unlock_irq(&hw->lock);

	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "Blocking IOs on lnode: %p\n", lnode_list[ii]);
		ln = lnode_list[ii];
		shost = csio_ln_to_shost(ln);
		scsi_block_requests(shost);

	}
	kfree(lnode_list);
}

void
csio_lnodes_unblock_request(struct csio_hw *hw)
{
	struct csio_lnode *ln;
	struct Scsi_Host  *shost;
	struct csio_lnode *sln;
	struct list_head *cur_ln, *cur_cln;
	struct csio_lnode **lnode_list;
	int cur_cnt = 0, ii;

	lnode_list = kzalloc((sizeof(struct csio_lnode *) * hw->num_lns),
			GFP_KERNEL);
	if (!lnode_list) {
		csio_err(hw, "Failed to allocate lnodes_list");
		return;
	}

	spin_lock_irq(&hw->lock);
	/* Traverse sibling lnodes */
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;
		lnode_list[cur_cnt++] = sln;

		/* Traverse children lnodes */
		list_for_each(cur_cln, &sln->cln_head)
			lnode_list[cur_cnt++] = (struct csio_lnode *) cur_cln;
	}
	spin_unlock_irq(&hw->lock);

	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "unblocking IOs on lnode: %p\n", lnode_list[ii]);
		ln = lnode_list[ii];
		shost = csio_ln_to_shost(ln);
		scsi_unblock_requests(shost);
	}
	kfree(lnode_list);
}

void
csio_lnodes_block_by_port(struct csio_hw *hw, uint8_t portid)
{
	struct csio_lnode *ln;
	struct Scsi_Host  *shost;
	struct csio_lnode *sln;
	struct list_head *cur_ln, *cur_cln;
	struct csio_lnode **lnode_list;
	int cur_cnt = 0, ii;

	lnode_list = kzalloc((sizeof(struct csio_lnode *) * hw->num_lns),
			GFP_KERNEL);
	if (!lnode_list) {
		csio_err(hw, "Failed to allocate lnodes_list");
		return;
	}

	spin_lock_irq(&hw->lock);
	/* Traverse sibling lnodes */
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;
		if (sln->portid != portid)
			continue;

		lnode_list[cur_cnt++] = sln;

		/* Traverse children lnodes */
		list_for_each(cur_cln, &sln->cln_head)
			lnode_list[cur_cnt++] = (struct csio_lnode *) cur_cln;
	}
	spin_unlock_irq(&hw->lock);

	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "Blocking IOs on lnode: %p\n", lnode_list[ii]);
		ln = lnode_list[ii];
		shost = csio_ln_to_shost(ln);
		scsi_block_requests(shost);
	}
	kfree(lnode_list);
}

void
csio_lnodes_unblock_by_port(struct csio_hw *hw, uint8_t portid)
{
	struct csio_lnode *ln;
	struct Scsi_Host  *shost;
	struct csio_lnode *sln;
	struct list_head *cur_ln, *cur_cln;
	struct csio_lnode **lnode_list;
	int cur_cnt = 0, ii;

	lnode_list = kzalloc((sizeof(struct csio_lnode *) * hw->num_lns),
			GFP_KERNEL);
	if (!lnode_list) {
		csio_err(hw, "Failed to allocate lnodes_list");
		return;
	}

	spin_lock_irq(&hw->lock);
	/* Traverse sibling lnodes */
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;
		if (sln->portid != portid)
			continue;
		lnode_list[cur_cnt++] = sln;

		/* Traverse children lnodes */
		list_for_each(cur_cln, &sln->cln_head)
			lnode_list[cur_cnt++] = (struct csio_lnode *) cur_cln;
	}
	spin_unlock_irq(&hw->lock);

	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "unblocking IOs on lnode: %p\n", lnode_list[ii]);
		ln = lnode_list[ii];
		shost = csio_ln_to_shost(ln);
		scsi_unblock_requests(shost);
	}
	kfree(lnode_list);
}

void
csio_lnodes_exit(struct csio_hw *hw, bool npiv)
{
	struct csio_lnode *sln;
	struct csio_lnode *ln;
	struct list_head *cur_ln, *cur_cln;
	struct csio_lnode **lnode_list;
	int cur_cnt = 0, ii;

	lnode_list = kzalloc((sizeof(struct csio_lnode *) * hw->num_lns),
			GFP_KERNEL);
	if (!lnode_list) {
		csio_err(hw, "lnodes_exit: Failed to allocate lnodes_list.\n");
		return;
	}

	/* Get all child lnodes(NPIV ports) */
	spin_lock_irq(&hw->lock);
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;

		/* Traverse children lnodes */
		list_for_each(cur_cln, &sln->cln_head)
			lnode_list[cur_cnt++] = (struct csio_lnode *) cur_cln;
	}
	spin_unlock_irq(&hw->lock);

	/* Delete NPIV lnodes */
	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "Deleting child lnode: %p\n", lnode_list[ii]);
		ln = lnode_list[ii];
		fc_vport_terminate(ln->fc_vport);
	}

	/* Delete only npiv lnodes */
	if (npiv)
		goto free_lnodes;

	cur_cnt = 0;
	/* Get all physical lnodes */
	spin_lock_irq(&hw->lock);
	/* Traverse sibling lnodes */
	list_for_each(cur_ln, &hw->sln_head) {
		sln = (struct csio_lnode *) cur_ln;
		lnode_list[cur_cnt++] = sln;
	}
	spin_unlock_irq(&hw->lock);

	/* Delete physical lnodes */
	for (ii = 0; ii < cur_cnt; ii++) {
		csio_dbg(hw, "Deleting parent lnode: %p\n", lnode_list[ii]);
		csio_shost_exit(lnode_list[ii]);
	}

free_lnodes:
	kfree(lnode_list);
}

/*
 * csio_lnode_init_post: Set lnode attributes after starting HW.
 * @ln: lnode.
 *
 */
static void
csio_lnode_init_post(struct csio_lnode *ln)
{
	struct Scsi_Host  *shost = csio_ln_to_shost(ln);

	csio_fchost_attr_init(ln);

	scsi_scan_host(shost);
}

/*
 * csio_probe_one - Instantiate this function.
 * @pdev: PCI device
 * @id: Device ID
 *
 * This is the .probe() callback of the driver. This function:
 * - Initializes the PCI function by enabling MMIO, setting bus
 *   mastership and setting DMA mask.
 * - Allocates HW structure, DMA, memory resources, maps BARS to
 *   host memory and initializes HW module.
 * - Allocates lnode structure via scsi_host_alloc, initializes
 *   shost, initialized lnode module and registers with SCSI ML
 *   via scsi_host_add.
 * - Enables interrupts, and starts the chip by kicking off the
 *   HW state machine.
 * - Once hardware is ready, initiated scan of the host via
 *   scsi_scan_host.
 */
static int csio_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rv;
	int bars;
	int i;
	struct csio_hw *hw;
	struct csio_lnode *ln;

	/* probe only T5 and T6 cards */
	if (!csio_is_t5((pdev->device & CSIO_HW_CHIP_MASK)) &&
	    !csio_is_t6((pdev->device & CSIO_HW_CHIP_MASK)))
		return -ENODEV;

	rv = csio_pci_init(pdev, &bars);
	if (rv)
		goto err;

	hw = csio_hw_alloc(pdev);
	if (!hw) {
		rv = -ENODEV;
		goto err_pci_exit;
	}

	if (!pcie_relaxed_ordering_enabled(pdev))
		hw->flags |= CSIO_HWF_ROOT_NO_RELAXED_ORDERING;

	pci_set_drvdata(pdev, hw);

	rv = csio_hw_start(hw);
	if (rv) {
		if (rv == -EINVAL) {
			dev_err(&pdev->dev,
				"Failed to start FW, continuing in debug mode.\n");
			return 0;
		}
		goto err_lnode_exit;
	}

	sprintf(hw->fwrev_str, "%u.%u.%u.%u\n",
		    FW_HDR_FW_VER_MAJOR_G(hw->fwrev),
		    FW_HDR_FW_VER_MINOR_G(hw->fwrev),
		    FW_HDR_FW_VER_MICRO_G(hw->fwrev),
		    FW_HDR_FW_VER_BUILD_G(hw->fwrev));

	for (i = 0; i < hw->num_pports; i++) {
		ln = csio_shost_init(hw, &pdev->dev, true, NULL);
		if (!ln) {
			rv = -ENODEV;
			break;
		}
		/* Initialize portid */
		ln->portid = hw->pport[i].portid;

		spin_lock_irq(&hw->lock);
		if (csio_lnode_start(ln) != 0)
			rv = -ENODEV;
		spin_unlock_irq(&hw->lock);

		if (rv)
			break;

		csio_lnode_init_post(ln);
	}

	if (rv)
		goto err_lnode_exit;

	return 0;

err_lnode_exit:
	csio_lnodes_block_request(hw);
	spin_lock_irq(&hw->lock);
	csio_hw_stop(hw);
	spin_unlock_irq(&hw->lock);
	csio_lnodes_unblock_request(hw);
	csio_lnodes_exit(hw, 0);
	csio_hw_free(hw);
err_pci_exit:
	csio_pci_exit(pdev, &bars);
err:
	dev_err(&pdev->dev, "probe of device failed: %d\n", rv);
	return rv;
}

/*
 * csio_remove_one - Remove one instance of the driver at this PCI function.
 * @pdev: PCI device
 *
 * Used during hotplug operation.
 */
static void csio_remove_one(struct pci_dev *pdev)
{
	struct csio_hw *hw = pci_get_drvdata(pdev);
	int bars = pci_select_bars(pdev, IORESOURCE_MEM);

	csio_lnodes_block_request(hw);
	spin_lock_irq(&hw->lock);

	/* Stops lnode, Rnode s/m
	 * Quiesce IOs.
	 * All sessions with remote ports are unregistered.
	 */
	csio_hw_stop(hw);
	spin_unlock_irq(&hw->lock);
	csio_lnodes_unblock_request(hw);

	csio_lnodes_exit(hw, 0);
	csio_hw_free(hw);
	csio_pci_exit(pdev, &bars);
}

/*
 * csio_pci_error_detected - PCI error was detected
 * @pdev: PCI device
 *
 */
static pci_ers_result_t
csio_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct csio_hw *hw = pci_get_drvdata(pdev);

	csio_lnodes_block_request(hw);
	spin_lock_irq(&hw->lock);

	/* Post PCI error detected evt to HW s/m
	 * HW s/m handles this evt by quiescing IOs, unregisters rports
	 * and finally takes the device to offline.
	 */
	csio_post_event(&hw->sm, CSIO_HWE_PCIERR_DETECTED);
	spin_unlock_irq(&hw->lock);
	csio_lnodes_unblock_request(hw);
	csio_lnodes_exit(hw, 0);
	csio_intr_disable(hw, true);
	pci_disable_device(pdev);
	return state == pci_channel_io_perm_failure ?
		PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_NEED_RESET;
}

/*
 * csio_pci_slot_reset - PCI slot has been reset.
 * @pdev: PCI device
 *
 */
static pci_ers_result_t
csio_pci_slot_reset(struct pci_dev *pdev)
{
	struct csio_hw *hw = pci_get_drvdata(pdev);
	int ready;

	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev, "cannot re-enable device in slot reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	/* Bring HW s/m to ready state.
	 * but don't resume IOs.
	 */
	spin_lock_irq(&hw->lock);
	csio_post_event(&hw->sm, CSIO_HWE_PCIERR_SLOT_RESET);
	ready = csio_is_hw_ready(hw);
	spin_unlock_irq(&hw->lock);

	if (ready) {
		return PCI_ERS_RESULT_RECOVERED;
	} else {
		dev_err(&pdev->dev, "Can't initialize HW when in slot reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
}

/*
 * csio_pci_resume - Resume normal operations
 * @pdev: PCI device
 *
 */
static void
csio_pci_resume(struct pci_dev *pdev)
{
	struct csio_hw *hw = pci_get_drvdata(pdev);
	struct csio_lnode *ln;
	int rv = 0;
	int i;

	/* Bring the LINK UP and Resume IO */

	for (i = 0; i < hw->num_pports; i++) {
		ln = csio_shost_init(hw, &pdev->dev, true, NULL);
		if (!ln) {
			rv = -ENODEV;
			break;
		}
		/* Initialize portid */
		ln->portid = hw->pport[i].portid;

		spin_lock_irq(&hw->lock);
		if (csio_lnode_start(ln) != 0)
			rv = -ENODEV;
		spin_unlock_irq(&hw->lock);

		if (rv)
			break;

		csio_lnode_init_post(ln);
	}

	if (rv)
		goto err_resume_exit;

	return;

err_resume_exit:
	csio_lnodes_block_request(hw);
	spin_lock_irq(&hw->lock);
	csio_hw_stop(hw);
	spin_unlock_irq(&hw->lock);
	csio_lnodes_unblock_request(hw);
	csio_lnodes_exit(hw, 0);
	csio_hw_free(hw);
	dev_err(&pdev->dev, "resume of device failed: %d\n", rv);
}

static struct pci_error_handlers csio_err_handler = {
	.error_detected = csio_pci_error_detected,
	.slot_reset	= csio_pci_slot_reset,
	.resume		= csio_pci_resume,
};

/*
 *  Macros needed to support the PCI Device ID Table ...
 */
#define CH_PCI_DEVICE_ID_TABLE_DEFINE_BEGIN \
	static const struct pci_device_id csio_pci_tbl[] = {
/* Define for FCoE uses PF6 */
#define CH_PCI_DEVICE_ID_FUNCTION	0x6

#define CH_PCI_ID_TABLE_ENTRY(devid) \
		{ PCI_VDEVICE(CHELSIO, (devid)), 0 }

#define CH_PCI_DEVICE_ID_TABLE_DEFINE_END { 0, } }

#include "t4_pci_id_tbl.h"

static struct pci_driver csio_pci_driver = {
	.name		= KBUILD_MODNAME,
	.driver		= {
		.owner	= THIS_MODULE,
	},
	.id_table	= csio_pci_tbl,
	.probe		= csio_probe_one,
	.remove		= csio_remove_one,
	.err_handler	= &csio_err_handler,
};

/*
 * csio_init - Chelsio storage driver initialization function.
 *
 */
static int __init
csio_init(void)
{
	int rv = -ENOMEM;

	pr_info("%s %s\n", CSIO_DRV_DESC, CSIO_DRV_VERSION);

	csio_dfs_init();

	csio_fcoe_transport = fc_attach_transport(&csio_fc_transport_funcs);
	if (!csio_fcoe_transport)
		goto err;

	csio_fcoe_transport_vport =
			fc_attach_transport(&csio_fc_transport_vport_funcs);
	if (!csio_fcoe_transport_vport)
		goto err_vport;

	rv = pci_register_driver(&csio_pci_driver);
	if (rv)
		goto err_pci;

	return 0;

err_pci:
	fc_release_transport(csio_fcoe_transport_vport);
err_vport:
	fc_release_transport(csio_fcoe_transport);
err:
	csio_dfs_exit();
	return rv;
}

/*
 * csio_exit - Chelsio storage driver uninitialization .
 *
 * Function that gets called in the unload path.
 */
static void __exit
csio_exit(void)
{
	pci_unregister_driver(&csio_pci_driver);
	csio_dfs_exit();
	fc_release_transport(csio_fcoe_transport_vport);
	fc_release_transport(csio_fcoe_transport);
}

module_init(csio_init);
module_exit(csio_exit);
MODULE_AUTHOR(CSIO_DRV_AUTHOR);
MODULE_DESCRIPTION(CSIO_DRV_DESC);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, csio_pci_tbl);
MODULE_VERSION(CSIO_DRV_VERSION);
MODULE_FIRMWARE(FW_FNAME_T5);
MODULE_FIRMWARE(FW_FNAME_T6);

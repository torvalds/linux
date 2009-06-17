/*
 * SN Platform GRU Driver
 *
 *              FILE OPERATIONS & DRIVER INITIALIZATION
 *
 * This file supports the user system call for file open, close, mmap, etc.
 * This also incudes the driver initialization code.
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <asm/uv/uv.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"

#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_mmrs.h>

struct gru_blade_state *gru_base[GRU_MAX_BLADES] __read_mostly;
unsigned long gru_start_paddr __read_mostly;
unsigned long gru_end_paddr __read_mostly;
unsigned int gru_max_gids __read_mostly;
struct gru_stats_s gru_stats;

/* Guaranteed user available resources on each node */
static int max_user_cbrs, max_user_dsr_bytes;

static struct file_operations gru_fops;
static struct miscdevice gru_miscdev;


/*
 * gru_vma_close
 *
 * Called when unmapping a device mapping. Frees all gru resources
 * and tables belonging to the vma.
 */
static void gru_vma_close(struct vm_area_struct *vma)
{
	struct gru_vma_data *vdata;
	struct gru_thread_state *gts;
	struct list_head *entry, *next;

	if (!vma->vm_private_data)
		return;

	vdata = vma->vm_private_data;
	vma->vm_private_data = NULL;
	gru_dbg(grudev, "vma %p, file %p, vdata %p\n", vma, vma->vm_file,
				vdata);
	list_for_each_safe(entry, next, &vdata->vd_head) {
		gts =
		    list_entry(entry, struct gru_thread_state, ts_next);
		list_del(&gts->ts_next);
		mutex_lock(&gts->ts_ctxlock);
		if (gts->ts_gru)
			gru_unload_context(gts, 0);
		mutex_unlock(&gts->ts_ctxlock);
		gts_drop(gts);
	}
	kfree(vdata);
	STAT(vdata_free);
}

/*
 * gru_file_mmap
 *
 * Called when mmaping the device.  Initializes the vma with a fault handler
 * and private data structure necessary to allocate, track, and free the
 * underlying pages.
 */
static int gru_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) != (VM_SHARED | VM_WRITE))
		return -EPERM;

	if (vma->vm_start & (GRU_GSEG_PAGESIZE - 1) ||
				vma->vm_end & (GRU_GSEG_PAGESIZE - 1))
		return -EINVAL;

	vma->vm_flags |=
	    (VM_IO | VM_DONTCOPY | VM_LOCKED | VM_DONTEXPAND | VM_PFNMAP |
			VM_RESERVED);
	vma->vm_page_prot = PAGE_SHARED;
	vma->vm_ops = &gru_vm_ops;

	vma->vm_private_data = gru_alloc_vma_data(vma, 0);
	if (!vma->vm_private_data)
		return -ENOMEM;

	gru_dbg(grudev, "file %p, vaddr 0x%lx, vma %p, vdata %p\n",
		file, vma->vm_start, vma, vma->vm_private_data);
	return 0;
}

/*
 * Create a new GRU context
 */
static int gru_create_new_context(unsigned long arg)
{
	struct gru_create_context_req req;
	struct vm_area_struct *vma;
	struct gru_vma_data *vdata;
	int ret = -EINVAL;


	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	if (req.data_segment_bytes == 0 ||
				req.data_segment_bytes > max_user_dsr_bytes)
		return -EINVAL;
	if (!req.control_blocks || !req.maximum_thread_count ||
				req.control_blocks > max_user_cbrs)
		return -EINVAL;

	if (!(req.options & GRU_OPT_MISS_MASK))
		req.options |= GRU_OPT_MISS_FMM_INTR;

	down_write(&current->mm->mmap_sem);
	vma = gru_find_vma(req.gseg);
	if (vma) {
		vdata = vma->vm_private_data;
		vdata->vd_user_options = req.options;
		vdata->vd_dsr_au_count =
		    GRU_DS_BYTES_TO_AU(req.data_segment_bytes);
		vdata->vd_cbr_au_count = GRU_CB_COUNT_TO_AU(req.control_blocks);
		ret = 0;
	}
	up_write(&current->mm->mmap_sem);

	return ret;
}

/*
 * Get GRU configuration info (temp - for emulator testing)
 */
static long gru_get_config_info(unsigned long arg)
{
	struct gru_config_info info;
	int nodesperblade;

	if (num_online_nodes() > 1 &&
			(uv_node_to_blade_id(1) == uv_node_to_blade_id(0)))
		nodesperblade = 2;
	else
		nodesperblade = 1;
	info.cpus = num_online_cpus();
	info.nodes = num_online_nodes();
	info.blades = info.nodes / nodesperblade;
	info.chiplets = GRU_CHIPLETS_PER_BLADE * info.blades;

	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/*
 * Get GRU chiplet status
 */
static long gru_get_chiplet_status(unsigned long arg)
{
	struct gru_state *gru;
	struct gru_chiplet_info info;

	if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
		return -EFAULT;

	if (info.node == -1)
		info.node = numa_node_id();
	if (info.node >= num_possible_nodes() ||
			info.chiplet >= GRU_CHIPLETS_PER_HUB ||
			info.node < 0 || info.chiplet < 0)
		return -EINVAL;

	info.blade = uv_node_to_blade_id(info.node);
	gru = get_gru(info.blade, info.chiplet);

	info.total_dsr_bytes = GRU_NUM_DSR_BYTES;
	info.total_cbr = GRU_NUM_CB;
	info.total_user_dsr_bytes = GRU_NUM_DSR_BYTES -
		gru->gs_reserved_dsr_bytes;
	info.total_user_cbr = GRU_NUM_CB - gru->gs_reserved_cbrs;
	info.free_user_dsr_bytes = hweight64(gru->gs_dsr_map) *
			GRU_DSR_AU_BYTES;
	info.free_user_cbr = hweight64(gru->gs_cbr_map) * GRU_CBR_AU_SIZE;

	if (copy_to_user((void __user *)arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/*
 * gru_file_unlocked_ioctl
 *
 * Called to update file attributes via IOCTL calls.
 */
static long gru_file_unlocked_ioctl(struct file *file, unsigned int req,
				    unsigned long arg)
{
	int err = -EBADRQC;

	gru_dbg(grudev, "file %p\n", file);

	switch (req) {
	case GRU_CREATE_CONTEXT:
		err = gru_create_new_context(arg);
		break;
	case GRU_SET_TASK_SLICE:
		err = gru_set_task_slice(arg);
		break;
	case GRU_USER_GET_EXCEPTION_DETAIL:
		err = gru_get_exception_detail(arg);
		break;
	case GRU_USER_UNLOAD_CONTEXT:
		err = gru_user_unload_context(arg);
		break;
	case GRU_GET_CHIPLET_STATUS:
		err = gru_get_chiplet_status(arg);
		break;
	case GRU_USER_FLUSH_TLB:
		err = gru_user_flush_tlb(arg);
		break;
	case GRU_USER_CALL_OS:
		err = gru_handle_user_call_os(arg);
		break;
	case GRU_GET_CONFIG_INFO:
		err = gru_get_config_info(arg);
		break;
	}
	return err;
}

/*
 * Called at init time to build tables for all GRUs that are present in the
 * system.
 */
static void gru_init_chiplet(struct gru_state *gru, unsigned long paddr,
			     void *vaddr, int nid, int bid, int grunum)
{
	spin_lock_init(&gru->gs_lock);
	spin_lock_init(&gru->gs_asid_lock);
	gru->gs_gru_base_paddr = paddr;
	gru->gs_gru_base_vaddr = vaddr;
	gru->gs_gid = bid * GRU_CHIPLETS_PER_BLADE + grunum;
	gru->gs_blade = gru_base[bid];
	gru->gs_blade_id = bid;
	gru->gs_cbr_map = (GRU_CBR_AU == 64) ? ~0 : (1UL << GRU_CBR_AU) - 1;
	gru->gs_dsr_map = (1UL << GRU_DSR_AU) - 1;
	gru->gs_asid_limit = MAX_ASID;
	gru_tgh_flush_init(gru);
	if (gru->gs_gid >= gru_max_gids)
		gru_max_gids = gru->gs_gid + 1;
	gru_dbg(grudev, "bid %d, nid %d, gid %d, vaddr %p (0x%lx)\n",
		bid, nid, gru->gs_gid, gru->gs_gru_base_vaddr,
		gru->gs_gru_base_paddr);
	gru_kservices_init(gru);
}

static int gru_init_tables(unsigned long gru_base_paddr, void *gru_base_vaddr)
{
	int pnode, nid, bid, chip;
	int cbrs, dsrbytes, n;
	int order = get_order(sizeof(struct gru_blade_state));
	struct page *page;
	struct gru_state *gru;
	unsigned long paddr;
	void *vaddr;

	max_user_cbrs = GRU_NUM_CB;
	max_user_dsr_bytes = GRU_NUM_DSR_BYTES;
	for_each_online_node(nid) {
		bid = uv_node_to_blade_id(nid);
		pnode = uv_node_to_pnode(nid);
		if (bid < 0 || gru_base[bid])
			continue;
		page = alloc_pages_exact_node(nid, GFP_KERNEL, order);
		if (!page)
			goto fail;
		gru_base[bid] = page_address(page);
		memset(gru_base[bid], 0, sizeof(struct gru_blade_state));
		gru_base[bid]->bs_lru_gru = &gru_base[bid]->bs_grus[0];
		spin_lock_init(&gru_base[bid]->bs_lock);

		dsrbytes = 0;
		cbrs = 0;
		for (gru = gru_base[bid]->bs_grus, chip = 0;
				chip < GRU_CHIPLETS_PER_BLADE;
				chip++, gru++) {
			paddr = gru_chiplet_paddr(gru_base_paddr, pnode, chip);
			vaddr = gru_chiplet_vaddr(gru_base_vaddr, pnode, chip);
			gru_init_chiplet(gru, paddr, vaddr, nid, bid, chip);
			n = hweight64(gru->gs_cbr_map) * GRU_CBR_AU_SIZE;
			cbrs = max(cbrs, n);
			n = hweight64(gru->gs_dsr_map) * GRU_DSR_AU_BYTES;
			dsrbytes = max(dsrbytes, n);
		}
		max_user_cbrs = min(max_user_cbrs, cbrs);
		max_user_dsr_bytes = min(max_user_dsr_bytes, dsrbytes);
	}

	return 0;

fail:
	for (nid--; nid >= 0; nid--)
		free_pages((unsigned long)gru_base[nid], order);
	return -ENOMEM;
}

#ifdef CONFIG_IA64

static int get_base_irq(void)
{
	return IRQ_GRU;
}

#elif defined CONFIG_X86_64

static void noop(unsigned int irq)
{
}

static struct irq_chip gru_chip = {
	.name		= "gru",
	.mask		= noop,
	.unmask		= noop,
	.ack		= noop,
};

static int get_base_irq(void)
{
	set_irq_chip(IRQ_GRU, &gru_chip);
	set_irq_chip(IRQ_GRU + 1, &gru_chip);
	return IRQ_GRU;
}
#endif

/*
 * gru_init
 *
 * Called at boot or module load time to initialize the GRUs.
 */
static int __init gru_init(void)
{
	int ret, irq, chip;
	char id[10];
	void *gru_start_vaddr;

	if (!is_uv_system())
		return 0;

#if defined CONFIG_IA64
	gru_start_paddr = 0xd000000000UL; /* ZZZZZZZZZZZZZZZZZZZ fixme */
#else
	gru_start_paddr = uv_read_local_mmr(UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR) &
				0x7fffffffffffUL;
#endif
	gru_start_vaddr = __va(gru_start_paddr);
	gru_end_paddr = gru_start_paddr + GRU_MAX_BLADES * GRU_SIZE;
	printk(KERN_INFO "GRU space: 0x%lx - 0x%lx\n",
	       gru_start_paddr, gru_end_paddr);
	irq = get_base_irq();
	for (chip = 0; chip < GRU_CHIPLETS_PER_BLADE; chip++) {
		ret = request_irq(irq + chip, gru_intr, 0, id, NULL);
		/* TODO: fix irq handling on x86. For now ignore failure because
		 * interrupts are not required & not yet fully supported */
		if (ret) {
			printk(KERN_WARNING
			       "!!!WARNING: GRU ignoring request failure!!!\n");
			ret = 0;
		}
		if (ret) {
			printk(KERN_ERR "%s: request_irq failed\n",
			       GRU_DRIVER_ID_STR);
			goto exit1;
		}
	}

	ret = misc_register(&gru_miscdev);
	if (ret) {
		printk(KERN_ERR "%s: misc_register failed\n",
		       GRU_DRIVER_ID_STR);
		goto exit1;
	}

	ret = gru_proc_init();
	if (ret) {
		printk(KERN_ERR "%s: proc init failed\n", GRU_DRIVER_ID_STR);
		goto exit2;
	}

	ret = gru_init_tables(gru_start_paddr, gru_start_vaddr);
	if (ret) {
		printk(KERN_ERR "%s: init tables failed\n", GRU_DRIVER_ID_STR);
		goto exit3;
	}

	printk(KERN_INFO "%s: v%s\n", GRU_DRIVER_ID_STR,
	       GRU_DRIVER_VERSION_STR);
	return 0;

exit3:
	gru_proc_exit();
exit2:
	misc_deregister(&gru_miscdev);
exit1:
	for (--chip; chip >= 0; chip--)
		free_irq(irq + chip, NULL);
	return ret;

}

static void __exit gru_exit(void)
{
	int i, bid, gid;
	int order = get_order(sizeof(struct gru_state) *
			      GRU_CHIPLETS_PER_BLADE);

	if (!is_uv_system())
		return;

	for (i = 0; i < GRU_CHIPLETS_PER_BLADE; i++)
		free_irq(IRQ_GRU + i, NULL);

	foreach_gid(gid)
		gru_kservices_exit(GID_TO_GRU(gid));

	for (bid = 0; bid < GRU_MAX_BLADES; bid++)
		free_pages((unsigned long)gru_base[bid], order);

	misc_deregister(&gru_miscdev);
	gru_proc_exit();
}

static struct file_operations gru_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= gru_file_unlocked_ioctl,
	.mmap		= gru_file_mmap,
};

static struct miscdevice gru_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "gru",
	.fops		= &gru_fops,
};

struct vm_operations_struct gru_vm_ops = {
	.close		= gru_vma_close,
	.fault		= gru_fault,
};

#ifndef MODULE
fs_initcall(gru_init);
#else
module_init(gru_init);
#endif
module_exit(gru_exit);

module_param(gru_options, ulong, 0644);
MODULE_PARM_DESC(gru_options, "Various debug options");

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(GRU_DRIVER_ID_STR GRU_DRIVER_VERSION_STR);
MODULE_VERSION(GRU_DRIVER_VERSION_STR);


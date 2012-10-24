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
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#ifdef CONFIG_X86_64
#include <asm/uv/uv_irq.h>
#endif
#include <asm/uv/uv.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"

#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_mmrs.h>

struct gru_blade_state *gru_base[GRU_MAX_BLADES] __read_mostly;
unsigned long gru_start_paddr __read_mostly;
void *gru_start_vaddr __read_mostly;
unsigned long gru_end_paddr __read_mostly;
unsigned int gru_max_gids __read_mostly;
struct gru_stats_s gru_stats;

/* Guaranteed user available resources on each node */
static int max_user_cbrs, max_user_dsr_bytes;

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
 * Called when mmapping the device.  Initializes the vma with a fault handler
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

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_LOCKED |
			 VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP;
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

	if (req.data_segment_bytes > max_user_dsr_bytes)
		return -EINVAL;
	if (req.control_blocks > max_user_cbrs || !req.maximum_thread_count)
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
		vdata->vd_tlb_preload_count = req.tlb_preload_count;
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
 * gru_file_unlocked_ioctl
 *
 * Called to update file attributes via IOCTL calls.
 */
static long gru_file_unlocked_ioctl(struct file *file, unsigned int req,
				    unsigned long arg)
{
	int err = -EBADRQC;

	gru_dbg(grudev, "file %p, req 0x%x, 0x%lx\n", file, req, arg);

	switch (req) {
	case GRU_CREATE_CONTEXT:
		err = gru_create_new_context(arg);
		break;
	case GRU_SET_CONTEXT_OPTION:
		err = gru_set_context_option(arg);
		break;
	case GRU_USER_GET_EXCEPTION_DETAIL:
		err = gru_get_exception_detail(arg);
		break;
	case GRU_USER_UNLOAD_CONTEXT:
		err = gru_user_unload_context(arg);
		break;
	case GRU_USER_FLUSH_TLB:
		err = gru_user_flush_tlb(arg);
		break;
	case GRU_USER_CALL_OS:
		err = gru_handle_user_call_os(arg);
		break;
	case GRU_GET_GSEG_STATISTICS:
		err = gru_get_gseg_statistics(arg);
		break;
	case GRU_KTEST:
		err = gru_ktest(arg);
		break;
	case GRU_GET_CONFIG_INFO:
		err = gru_get_config_info(arg);
		break;
	case GRU_DUMP_CHIPLET_STATE:
		err = gru_dump_chiplet_request(arg);
		break;
	}
	return err;
}

/*
 * Called at init time to build tables for all GRUs that are present in the
 * system.
 */
static void gru_init_chiplet(struct gru_state *gru, unsigned long paddr,
			     void *vaddr, int blade_id, int chiplet_id)
{
	spin_lock_init(&gru->gs_lock);
	spin_lock_init(&gru->gs_asid_lock);
	gru->gs_gru_base_paddr = paddr;
	gru->gs_gru_base_vaddr = vaddr;
	gru->gs_gid = blade_id * GRU_CHIPLETS_PER_BLADE + chiplet_id;
	gru->gs_blade = gru_base[blade_id];
	gru->gs_blade_id = blade_id;
	gru->gs_chiplet_id = chiplet_id;
	gru->gs_cbr_map = (GRU_CBR_AU == 64) ? ~0 : (1UL << GRU_CBR_AU) - 1;
	gru->gs_dsr_map = (1UL << GRU_DSR_AU) - 1;
	gru->gs_asid_limit = MAX_ASID;
	gru_tgh_flush_init(gru);
	if (gru->gs_gid >= gru_max_gids)
		gru_max_gids = gru->gs_gid + 1;
	gru_dbg(grudev, "bid %d, gid %d, vaddr %p (0x%lx)\n",
		blade_id, gru->gs_gid, gru->gs_gru_base_vaddr,
		gru->gs_gru_base_paddr);
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
	for_each_possible_blade(bid) {
		pnode = uv_blade_to_pnode(bid);
		nid = uv_blade_to_memory_nid(bid);/* -1 if no memory on blade */
		page = alloc_pages_node(nid, GFP_KERNEL, order);
		if (!page)
			goto fail;
		gru_base[bid] = page_address(page);
		memset(gru_base[bid], 0, sizeof(struct gru_blade_state));
		gru_base[bid]->bs_lru_gru = &gru_base[bid]->bs_grus[0];
		spin_lock_init(&gru_base[bid]->bs_lock);
		init_rwsem(&gru_base[bid]->bs_kgts_sema);

		dsrbytes = 0;
		cbrs = 0;
		for (gru = gru_base[bid]->bs_grus, chip = 0;
				chip < GRU_CHIPLETS_PER_BLADE;
				chip++, gru++) {
			paddr = gru_chiplet_paddr(gru_base_paddr, pnode, chip);
			vaddr = gru_chiplet_vaddr(gru_base_vaddr, pnode, chip);
			gru_init_chiplet(gru, paddr, vaddr, bid, chip);
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
	for (bid--; bid >= 0; bid--)
		free_pages((unsigned long)gru_base[bid], order);
	return -ENOMEM;
}

static void gru_free_tables(void)
{
	int bid;
	int order = get_order(sizeof(struct gru_state) *
			      GRU_CHIPLETS_PER_BLADE);

	for (bid = 0; bid < GRU_MAX_BLADES; bid++)
		free_pages((unsigned long)gru_base[bid], order);
}

static unsigned long gru_chiplet_cpu_to_mmr(int chiplet, int cpu, int *corep)
{
	unsigned long mmr = 0;
	int core;

	/*
	 * We target the cores of a blade and not the hyperthreads themselves.
	 * There is a max of 8 cores per socket and 2 sockets per blade,
	 * making for a max total of 16 cores (i.e., 16 CPUs without
	 * hyperthreading and 32 CPUs with hyperthreading).
	 */
	core = uv_cpu_core_number(cpu) + UV_MAX_INT_CORES * uv_cpu_socket_number(cpu);
	if (core >= GRU_NUM_TFM || uv_cpu_ht_number(cpu))
		return 0;

	if (chiplet == 0) {
		mmr = UVH_GR0_TLB_INT0_CONFIG +
		    core * (UVH_GR0_TLB_INT1_CONFIG - UVH_GR0_TLB_INT0_CONFIG);
	} else if (chiplet == 1) {
		mmr = UVH_GR1_TLB_INT0_CONFIG +
		    core * (UVH_GR1_TLB_INT1_CONFIG - UVH_GR1_TLB_INT0_CONFIG);
	} else {
		BUG();
	}

	*corep = core;
	return mmr;
}

#ifdef CONFIG_IA64

static int gru_irq_count[GRU_CHIPLETS_PER_BLADE];

static void gru_noop(struct irq_data *d)
{
}

static struct irq_chip gru_chip[GRU_CHIPLETS_PER_BLADE] = {
	[0 ... GRU_CHIPLETS_PER_BLADE - 1] {
		.irq_mask	= gru_noop,
		.irq_unmask	= gru_noop,
		.irq_ack	= gru_noop
	}
};

static int gru_chiplet_setup_tlb_irq(int chiplet, char *irq_name,
			irq_handler_t irq_handler, int cpu, int blade)
{
	unsigned long mmr;
	int irq = IRQ_GRU + chiplet;
	int ret, core;

	mmr = gru_chiplet_cpu_to_mmr(chiplet, cpu, &core);
	if (mmr == 0)
		return 0;

	if (gru_irq_count[chiplet] == 0) {
		gru_chip[chiplet].name = irq_name;
		ret = irq_set_chip(irq, &gru_chip[chiplet]);
		if (ret) {
			printk(KERN_ERR "%s: set_irq_chip failed, errno=%d\n",
			       GRU_DRIVER_ID_STR, -ret);
			return ret;
		}

		ret = request_irq(irq, irq_handler, 0, irq_name, NULL);
		if (ret) {
			printk(KERN_ERR "%s: request_irq failed, errno=%d\n",
			       GRU_DRIVER_ID_STR, -ret);
			return ret;
		}
	}
	gru_irq_count[chiplet]++;

	return 0;
}

static void gru_chiplet_teardown_tlb_irq(int chiplet, int cpu, int blade)
{
	unsigned long mmr;
	int core, irq = IRQ_GRU + chiplet;

	if (gru_irq_count[chiplet] == 0)
		return;

	mmr = gru_chiplet_cpu_to_mmr(chiplet, cpu, &core);
	if (mmr == 0)
		return;

	if (--gru_irq_count[chiplet] == 0)
		free_irq(irq, NULL);
}

#elif defined CONFIG_X86_64

static int gru_chiplet_setup_tlb_irq(int chiplet, char *irq_name,
			irq_handler_t irq_handler, int cpu, int blade)
{
	unsigned long mmr;
	int irq, core;
	int ret;

	mmr = gru_chiplet_cpu_to_mmr(chiplet, cpu, &core);
	if (mmr == 0)
		return 0;

	irq = uv_setup_irq(irq_name, cpu, blade, mmr, UV_AFFINITY_CPU);
	if (irq < 0) {
		printk(KERN_ERR "%s: uv_setup_irq failed, errno=%d\n",
		       GRU_DRIVER_ID_STR, -irq);
		return irq;
	}

	ret = request_irq(irq, irq_handler, 0, irq_name, NULL);
	if (ret) {
		uv_teardown_irq(irq);
		printk(KERN_ERR "%s: request_irq failed, errno=%d\n",
		       GRU_DRIVER_ID_STR, -ret);
		return ret;
	}
	gru_base[blade]->bs_grus[chiplet].gs_irq[core] = irq;
	return 0;
}

static void gru_chiplet_teardown_tlb_irq(int chiplet, int cpu, int blade)
{
	int irq, core;
	unsigned long mmr;

	mmr = gru_chiplet_cpu_to_mmr(chiplet, cpu, &core);
	if (mmr) {
		irq = gru_base[blade]->bs_grus[chiplet].gs_irq[core];
		if (irq) {
			free_irq(irq, NULL);
			uv_teardown_irq(irq);
		}
	}
}

#endif

static void gru_teardown_tlb_irqs(void)
{
	int blade;
	int cpu;

	for_each_online_cpu(cpu) {
		blade = uv_cpu_to_blade_id(cpu);
		gru_chiplet_teardown_tlb_irq(0, cpu, blade);
		gru_chiplet_teardown_tlb_irq(1, cpu, blade);
	}
	for_each_possible_blade(blade) {
		if (uv_blade_nr_possible_cpus(blade))
			continue;
		gru_chiplet_teardown_tlb_irq(0, 0, blade);
		gru_chiplet_teardown_tlb_irq(1, 0, blade);
	}
}

static int gru_setup_tlb_irqs(void)
{
	int blade;
	int cpu;
	int ret;

	for_each_online_cpu(cpu) {
		blade = uv_cpu_to_blade_id(cpu);
		ret = gru_chiplet_setup_tlb_irq(0, "GRU0_TLB", gru0_intr, cpu, blade);
		if (ret != 0)
			goto exit1;

		ret = gru_chiplet_setup_tlb_irq(1, "GRU1_TLB", gru1_intr, cpu, blade);
		if (ret != 0)
			goto exit1;
	}
	for_each_possible_blade(blade) {
		if (uv_blade_nr_possible_cpus(blade))
			continue;
		ret = gru_chiplet_setup_tlb_irq(0, "GRU0_TLB", gru_intr_mblade, 0, blade);
		if (ret != 0)
			goto exit1;

		ret = gru_chiplet_setup_tlb_irq(1, "GRU1_TLB", gru_intr_mblade, 0, blade);
		if (ret != 0)
			goto exit1;
	}

	return 0;

exit1:
	gru_teardown_tlb_irqs();
	return ret;
}

/*
 * gru_init
 *
 * Called at boot or module load time to initialize the GRUs.
 */
static int __init gru_init(void)
{
	int ret;

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
	ret = misc_register(&gru_miscdev);
	if (ret) {
		printk(KERN_ERR "%s: misc_register failed\n",
		       GRU_DRIVER_ID_STR);
		goto exit0;
	}

	ret = gru_proc_init();
	if (ret) {
		printk(KERN_ERR "%s: proc init failed\n", GRU_DRIVER_ID_STR);
		goto exit1;
	}

	ret = gru_init_tables(gru_start_paddr, gru_start_vaddr);
	if (ret) {
		printk(KERN_ERR "%s: init tables failed\n", GRU_DRIVER_ID_STR);
		goto exit2;
	}

	ret = gru_setup_tlb_irqs();
	if (ret != 0)
		goto exit3;

	gru_kservices_init();

	printk(KERN_INFO "%s: v%s\n", GRU_DRIVER_ID_STR,
	       GRU_DRIVER_VERSION_STR);
	return 0;

exit3:
	gru_free_tables();
exit2:
	gru_proc_exit();
exit1:
	misc_deregister(&gru_miscdev);
exit0:
	return ret;

}

static void __exit gru_exit(void)
{
	if (!is_uv_system())
		return;

	gru_teardown_tlb_irqs();
	gru_kservices_exit();
	gru_free_tables();
	misc_deregister(&gru_miscdev);
	gru_proc_exit();
}

static const struct file_operations gru_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= gru_file_unlocked_ioctl,
	.mmap		= gru_file_mmap,
	.llseek		= noop_llseek,
};

static struct miscdevice gru_miscdev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "gru",
	.fops		= &gru_fops,
};

const struct vm_operations_struct gru_vm_ops = {
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


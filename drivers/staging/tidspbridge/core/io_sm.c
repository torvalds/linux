/*
 * io_sm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * IO dispatcher for a shared memory channel driver.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * Channel Invariant:
 * There is an important invariant condition which must be maintained per
 * channel outside of bridge_chnl_get_ioc() and IO_Dispatch(), violation of
 * which may cause timeouts and/or failure of the sync_wait_on_event
 * function.
 */
#include <linux/types.h>
#include <linux/list.h>

/* Host OS */
#include <dspbridge/host_os.h>
#include <linux/workqueue.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/* Services Layer */
#include <dspbridge/ntfy.h>
#include <dspbridge/sync.h>

/* Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/* Bridge Driver */
#include <dspbridge/dspdeh.h>
#include <dspbridge/dspio.h>
#include <dspbridge/dspioctl.h>
#include <dspbridge/wdt.h>
#include <_tiomap.h>
#include <tiomap_io.h>
#include <_tiomap_pwr.h>

/* Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/node.h>
#include <dspbridge/dev.h>

/* Others */
#include <dspbridge/rms_sh.h>
#include <dspbridge/mgr.h>
#include <dspbridge/drv.h>
#include "_cmm.h"
#include "module_list.h"

/* This */
#include <dspbridge/io_sm.h>
#include "_msg_sm.h"

/* Defines, Data Structures, Typedefs */
#define OUTPUTNOTREADY  0xffff
#define NOTENABLED      0xffff	/* Channel(s) not enabled */

#define EXTEND      "_EXT_END"

#define SWAP_WORD(x)     (x)
#define UL_PAGE_ALIGN_SIZE 0x10000	/* Page Align Size */

#define MAX_PM_REQS 32

#define MMU_FAULT_HEAD1 0xa5a5a5a5
#define MMU_FAULT_HEAD2 0x96969696
#define POLL_MAX 1000
#define MAX_MMU_DBGBUFF 10240

/* IO Manager: only one created per board */
struct io_mgr {
	/* These four fields must be the first fields in a io_mgr_ struct */
	/* Bridge device context */
	struct bridge_dev_context *bridge_context;
	/* Function interface to Bridge driver */
	struct bridge_drv_interface *intf_fxns;
	struct dev_object *dev_obj;	/* Device this board represents */

	/* These fields initialized in bridge_io_create() */
	struct chnl_mgr *chnl_mgr;
	struct shm *shared_mem;	/* Shared Memory control */
	u8 *input;		/* Address of input channel */
	u8 *output;		/* Address of output channel */
	struct msg_mgr *msg_mgr;	/* Message manager */
	/* Msg control for from DSP messages */
	struct msg_ctrl *msg_input_ctrl;
	/* Msg control for to DSP messages */
	struct msg_ctrl *msg_output_ctrl;
	u8 *msg_input;		/* Address of input messages */
	u8 *msg_output;		/* Address of output messages */
	u32 sm_buf_size;	/* Size of a shared memory I/O channel */
	bool shared_irq;	/* Is this IRQ shared? */
	u32 word_size;		/* Size in bytes of DSP word */
	u16 intr_val;		/* Interrupt value */
	/* Private extnd proc info; mmu setup */
	struct mgr_processorextinfo ext_proc_info;
	struct cmm_object *cmm_mgr;	/* Shared Mem Mngr */
	struct work_struct io_workq;	/* workqueue */
#if defined(CONFIG_TIDSPBRIDGE_BACKTRACE)
	u32 trace_buffer_begin;	/* Trace message start address */
	u32 trace_buffer_end;	/* Trace message end address */
	u32 trace_buffer_current;	/* Trace message current address */
	u32 gpp_read_pointer;		/* GPP Read pointer to Trace buffer */
	u8 *msg;
	u32 gpp_va;
	u32 dsp_va;
#endif
	/* IO Dpc */
	u32 dpc_req;		/* Number of requested DPC's. */
	u32 dpc_sched;		/* Number of executed DPC's. */
	struct tasklet_struct dpc_tasklet;
	spinlock_t dpc_lock;

};

/* Function Prototypes */
static void io_dispatch_pm(struct io_mgr *pio_mgr);
static void notify_chnl_complete(struct chnl_object *pchnl,
				 struct chnl_irp *chnl_packet_obj);
static void input_chnl(struct io_mgr *pio_mgr, struct chnl_object *pchnl,
			u8 io_mode);
static void output_chnl(struct io_mgr *pio_mgr, struct chnl_object *pchnl,
			u8 io_mode);
static void input_msg(struct io_mgr *pio_mgr, struct msg_mgr *hmsg_mgr);
static void output_msg(struct io_mgr *pio_mgr, struct msg_mgr *hmsg_mgr);
static u32 find_ready_output(struct chnl_mgr *chnl_mgr_obj,
			     struct chnl_object *pchnl, u32 mask);

/* Bus Addr (cached kernel) */
static int register_shm_segs(struct io_mgr *hio_mgr,
				    struct cod_manager *cod_man,
				    u32 dw_gpp_base_pa);

static inline void set_chnl_free(struct shm *sm, u32 chnl)
{
	sm->host_free_mask &= ~(1 << chnl);
}

static inline void set_chnl_busy(struct shm *sm, u32 chnl)
{
	sm->host_free_mask |= 1 << chnl;
}


/*
 *  ======== bridge_io_create ========
 *      Create an IO manager object.
 */
int bridge_io_create(struct io_mgr **io_man,
			    struct dev_object *hdev_obj,
			    const struct io_attrs *mgr_attrts)
{
	struct io_mgr *pio_mgr = NULL;
	struct bridge_dev_context *hbridge_context = NULL;
	struct cfg_devnode *dev_node_obj;
	struct chnl_mgr *hchnl_mgr;
	u8 dev_type;

	/* Check requirements */
	if (!io_man || !mgr_attrts || mgr_attrts->word_size == 0)
		return -EFAULT;

	*io_man = NULL;

	dev_get_chnl_mgr(hdev_obj, &hchnl_mgr);
	if (!hchnl_mgr || hchnl_mgr->iomgr)
		return -EFAULT;

	/*
	 * Message manager will be created when a file is loaded, since
	 * size of message buffer in shared memory is configurable in
	 * the base image.
	 */
	dev_get_bridge_context(hdev_obj, &hbridge_context);
	if (!hbridge_context)
		return -EFAULT;

	dev_get_dev_type(hdev_obj, &dev_type);

	/* Allocate IO manager object */
	pio_mgr = kzalloc(sizeof(struct io_mgr), GFP_KERNEL);
	if (!pio_mgr)
		return -ENOMEM;

	/* Initialize chnl_mgr object */
	pio_mgr->chnl_mgr = hchnl_mgr;
	pio_mgr->word_size = mgr_attrts->word_size;

	if (dev_type == DSP_UNIT) {
		/* Create an IO DPC */
		tasklet_init(&pio_mgr->dpc_tasklet, io_dpc, (u32) pio_mgr);

		/* Initialize DPC counters */
		pio_mgr->dpc_req = 0;
		pio_mgr->dpc_sched = 0;

		spin_lock_init(&pio_mgr->dpc_lock);

		if (dev_get_dev_node(hdev_obj, &dev_node_obj)) {
			bridge_io_destroy(pio_mgr);
			return -EIO;
		}
	}

	pio_mgr->bridge_context = hbridge_context;
	pio_mgr->shared_irq = mgr_attrts->irq_shared;
	if (dsp_wdt_init()) {
		bridge_io_destroy(pio_mgr);
		return -EPERM;
	}

	/* Return IO manager object to caller... */
	hchnl_mgr->iomgr = pio_mgr;
	*io_man = pio_mgr;

	return 0;
}

/*
 *  ======== bridge_io_destroy ========
 *  Purpose:
 *      Disable interrupts, destroy the IO manager.
 */
int bridge_io_destroy(struct io_mgr *hio_mgr)
{
	int status = 0;
	if (hio_mgr) {
		/* Free IO DPC object */
		tasklet_kill(&hio_mgr->dpc_tasklet);

#if defined(CONFIG_TIDSPBRIDGE_BACKTRACE)
		kfree(hio_mgr->msg);
#endif
		dsp_wdt_exit();
		/* Free this IO manager object */
		kfree(hio_mgr);
	} else {
		status = -EFAULT;
	}

	return status;
}

/*
 *  ======== bridge_io_on_loaded ========
 *  Purpose:
 *      Called when a new program is loaded to get shared memory buffer
 *      parameters from COFF file. ulSharedBufferBase and ulSharedBufferLimit
 *      are in DSP address units.
 */
int bridge_io_on_loaded(struct io_mgr *hio_mgr)
{
	struct cod_manager *cod_man;
	struct chnl_mgr *hchnl_mgr;
	struct msg_mgr *hmsg_mgr;
	u32 ul_shm_base;
	u32 ul_shm_base_offset;
	u32 ul_shm_limit;
	u32 ul_shm_length = -1;
	u32 ul_mem_length = -1;
	u32 ul_msg_base;
	u32 ul_msg_limit;
	u32 ul_msg_length = -1;
	u32 ul_ext_end;
	u32 ul_gpp_pa = 0;
	u32 ul_gpp_va = 0;
	u32 ul_dsp_va = 0;
	u32 ul_seg_size = 0;
	u32 ul_pad_size = 0;
	u32 i;
	int status = 0;
	u8 num_procs = 0;
	s32 ndx = 0;
	/* DSP MMU setup table */
	struct bridge_ioctl_extproc ae_proc[BRDIOCTL_NUMOFMMUTLB];
	struct cfg_hostres *host_res;
	struct bridge_dev_context *pbridge_context;
	u32 map_attrs;
	u32 shm0_end;
	u32 ul_dyn_ext_base;
	u32 ul_seg1_size = 0;
	u32 pa_curr = 0;
	u32 va_curr = 0;
	u32 gpp_va_curr = 0;
	u32 num_bytes = 0;
	u32 all_bits = 0;
	u32 page_size[] = { HW_PAGE_SIZE16MB, HW_PAGE_SIZE1MB,
		HW_PAGE_SIZE64KB, HW_PAGE_SIZE4KB
	};

	status = dev_get_bridge_context(hio_mgr->dev_obj, &pbridge_context);
	if (!pbridge_context) {
		status = -EFAULT;
		goto func_end;
	}

	host_res = pbridge_context->resources;
	if (!host_res) {
		status = -EFAULT;
		goto func_end;
	}
	status = dev_get_cod_mgr(hio_mgr->dev_obj, &cod_man);
	if (!cod_man) {
		status = -EFAULT;
		goto func_end;
	}
	hchnl_mgr = hio_mgr->chnl_mgr;
	/* The message manager is destroyed when the board is stopped. */
	dev_get_msg_mgr(hio_mgr->dev_obj, &hio_mgr->msg_mgr);
	hmsg_mgr = hio_mgr->msg_mgr;
	if (!hchnl_mgr || !hmsg_mgr) {
		status = -EFAULT;
		goto func_end;
	}
	if (hio_mgr->shared_mem)
		hio_mgr->shared_mem = NULL;

	/* Get start and length of channel part of shared memory */
	status = cod_get_sym_value(cod_man, CHNL_SHARED_BUFFER_BASE_SYM,
				   &ul_shm_base);
	if (status) {
		status = -EFAULT;
		goto func_end;
	}
	status = cod_get_sym_value(cod_man, CHNL_SHARED_BUFFER_LIMIT_SYM,
				   &ul_shm_limit);
	if (status) {
		status = -EFAULT;
		goto func_end;
	}
	if (ul_shm_limit <= ul_shm_base) {
		status = -EINVAL;
		goto func_end;
	}
	/* Get total length in bytes */
	ul_shm_length = (ul_shm_limit - ul_shm_base + 1) * hio_mgr->word_size;
	/* Calculate size of a PROCCOPY shared memory region */
	dev_dbg(bridge, "%s: (proc)proccopy shmmem size: 0x%x bytes\n",
		__func__, (ul_shm_length - sizeof(struct shm)));

	/* Get start and length of message part of shared memory */
	status = cod_get_sym_value(cod_man, MSG_SHARED_BUFFER_BASE_SYM,
					   &ul_msg_base);
	if (!status) {
		status = cod_get_sym_value(cod_man, MSG_SHARED_BUFFER_LIMIT_SYM,
					   &ul_msg_limit);
		if (!status) {
			if (ul_msg_limit <= ul_msg_base) {
				status = -EINVAL;
			} else {
				/*
				 * Length (bytes) of messaging part of shared
				 * memory.
				 */
				ul_msg_length =
				    (ul_msg_limit - ul_msg_base +
				     1) * hio_mgr->word_size;
				/*
				 * Total length (bytes) of shared memory:
				 * chnl + msg.
				 */
				ul_mem_length = ul_shm_length + ul_msg_length;
			}
		} else {
			status = -EFAULT;
		}
	} else {
		status = -EFAULT;
	}
	if (!status) {
#if defined(CONFIG_TIDSPBRIDGE_BACKTRACE)
		status =
		    cod_get_sym_value(cod_man, DSP_TRACESEC_END, &shm0_end);
#else
		status = cod_get_sym_value(cod_man, SHM0_SHARED_END_SYM,
					   &shm0_end);
#endif
		if (status)
			status = -EFAULT;
	}
	if (!status) {
		status =
		    cod_get_sym_value(cod_man, DYNEXTBASE, &ul_dyn_ext_base);
		if (status)
			status = -EFAULT;
	}
	if (!status) {
		status = cod_get_sym_value(cod_man, EXTEND, &ul_ext_end);
		if (status)
			status = -EFAULT;
	}
	if (!status) {
		/* Get memory reserved in host resources */
		(void)mgr_enum_processor_info(0, (struct dsp_processorinfo *)
					      &hio_mgr->ext_proc_info,
					      sizeof(struct
						     mgr_processorextinfo),
					      &num_procs);

		/* The first MMU TLB entry(TLB_0) in DCD is ShmBase. */
		ndx = 0;
		ul_gpp_pa = host_res->mem_phys[1];
		ul_gpp_va = host_res->mem_base[1];
		/* This is the virtual uncached ioremapped address!!! */
		/* Why can't we directly take the DSPVA from the symbols? */
		ul_dsp_va = hio_mgr->ext_proc_info.ty_tlb[0].dsp_virt;
		ul_seg_size = (shm0_end - ul_dsp_va) * hio_mgr->word_size;
		ul_seg1_size =
		    (ul_ext_end - ul_dyn_ext_base) * hio_mgr->word_size;
		/* 4K align */
		ul_seg1_size = (ul_seg1_size + 0xFFF) & (~0xFFFUL);
		/* 64K align */
		ul_seg_size = (ul_seg_size + 0xFFFF) & (~0xFFFFUL);
		ul_pad_size = UL_PAGE_ALIGN_SIZE - ((ul_gpp_pa + ul_seg1_size) %
						    UL_PAGE_ALIGN_SIZE);
		if (ul_pad_size == UL_PAGE_ALIGN_SIZE)
			ul_pad_size = 0x0;

		dev_dbg(bridge, "%s: ul_gpp_pa %x, ul_gpp_va %x, ul_dsp_va %x, "
			"shm0_end %x, ul_dyn_ext_base %x, ul_ext_end %x, "
			"ul_seg_size %x ul_seg1_size %x \n", __func__,
			ul_gpp_pa, ul_gpp_va, ul_dsp_va, shm0_end,
			ul_dyn_ext_base, ul_ext_end, ul_seg_size, ul_seg1_size);

		if ((ul_seg_size + ul_seg1_size + ul_pad_size) >
		    host_res->mem_length[1]) {
			pr_err("%s: shm Error, reserved 0x%x required 0x%x\n",
			       __func__, host_res->mem_length[1],
			       ul_seg_size + ul_seg1_size + ul_pad_size);
			status = -ENOMEM;
		}
	}
	if (status)
		goto func_end;

	pa_curr = ul_gpp_pa;
	va_curr = ul_dyn_ext_base * hio_mgr->word_size;
	gpp_va_curr = ul_gpp_va;
	num_bytes = ul_seg1_size;

	/*
	 * Try to fit into TLB entries. If not possible, push them to page
	 * tables. It is quite possible that if sections are not on
	 * bigger page boundary, we may end up making several small pages.
	 * So, push them onto page tables, if that is the case.
	 */
	map_attrs = 0x00000000;
	map_attrs = DSP_MAPLITTLEENDIAN;
	map_attrs |= DSP_MAPPHYSICALADDR;
	map_attrs |= DSP_MAPELEMSIZE32;
	map_attrs |= DSP_MAPDONOTLOCK;

	while (num_bytes) {
		/*
		 * To find the max. page size with which both PA & VA are
		 * aligned.
		 */
		all_bits = pa_curr | va_curr;
		dev_dbg(bridge, "all_bits %x, pa_curr %x, va_curr %x, "
			"num_bytes %x\n", all_bits, pa_curr, va_curr,
			num_bytes);
		for (i = 0; i < 4; i++) {
			if ((num_bytes >= page_size[i]) && ((all_bits &
							     (page_size[i] -
							      1)) == 0)) {
				status =
				    hio_mgr->intf_fxns->
				    brd_mem_map(hio_mgr->bridge_context,
						    pa_curr, va_curr,
						    page_size[i], map_attrs,
						    NULL);
				if (status)
					goto func_end;
				pa_curr += page_size[i];
				va_curr += page_size[i];
				gpp_va_curr += page_size[i];
				num_bytes -= page_size[i];
				/*
				 * Don't try smaller sizes. Hopefully we have
				 * reached an address aligned to a bigger page
				 * size.
				 */
				break;
			}
		}
	}
	pa_curr += ul_pad_size;
	va_curr += ul_pad_size;
	gpp_va_curr += ul_pad_size;

	/* Configure the TLB entries for the next cacheable segment */
	num_bytes = ul_seg_size;
	va_curr = ul_dsp_va * hio_mgr->word_size;
	while (num_bytes) {
		/*
		 * To find the max. page size with which both PA & VA are
		 * aligned.
		 */
		all_bits = pa_curr | va_curr;
		dev_dbg(bridge, "all_bits for Seg1 %x, pa_curr %x, "
			"va_curr %x, num_bytes %x\n", all_bits, pa_curr,
			va_curr, num_bytes);
		for (i = 0; i < 4; i++) {
			if (!(num_bytes >= page_size[i]) ||
			    !((all_bits & (page_size[i] - 1)) == 0))
				continue;
			if (ndx < MAX_LOCK_TLB_ENTRIES) {
				/*
				 * This is the physical address written to
				 * DSP MMU.
				 */
				ae_proc[ndx].gpp_pa = pa_curr;
				/*
				 * This is the virtual uncached ioremapped
				 * address!!!
				 */
				ae_proc[ndx].gpp_va = gpp_va_curr;
				ae_proc[ndx].dsp_va =
				    va_curr / hio_mgr->word_size;
				ae_proc[ndx].size = page_size[i];
				ae_proc[ndx].endianism = HW_LITTLE_ENDIAN;
				ae_proc[ndx].elem_size = HW_ELEM_SIZE16BIT;
				ae_proc[ndx].mixed_mode = HW_MMU_CPUES;
				dev_dbg(bridge, "shm MMU TLB entry PA %x"
					" VA %x DSP_VA %x Size %x\n",
					ae_proc[ndx].gpp_pa,
					ae_proc[ndx].gpp_va,
					ae_proc[ndx].dsp_va *
					hio_mgr->word_size, page_size[i]);
				ndx++;
			} else {
				status =
				    hio_mgr->intf_fxns->
				    brd_mem_map(hio_mgr->bridge_context,
						    pa_curr, va_curr,
						    page_size[i], map_attrs,
						    NULL);
				dev_dbg(bridge,
					"shm MMU PTE entry PA %x"
					" VA %x DSP_VA %x Size %x\n",
					ae_proc[ndx].gpp_pa,
					ae_proc[ndx].gpp_va,
					ae_proc[ndx].dsp_va *
					hio_mgr->word_size, page_size[i]);
				if (status)
					goto func_end;
			}
			pa_curr += page_size[i];
			va_curr += page_size[i];
			gpp_va_curr += page_size[i];
			num_bytes -= page_size[i];
			/*
			 * Don't try smaller sizes. Hopefully we have reached
			 * an address aligned to a bigger page size.
			 */
			break;
		}
	}

	/*
	 * Copy remaining entries from CDB. All entries are 1 MB and
	 * should not conflict with shm entries on MPU or DSP side.
	 */
	for (i = 3; i < 7 && ndx < BRDIOCTL_NUMOFMMUTLB; i++) {
		if (hio_mgr->ext_proc_info.ty_tlb[i].gpp_phys == 0)
			continue;

		if ((hio_mgr->ext_proc_info.ty_tlb[i].gpp_phys >
		     ul_gpp_pa - 0x100000
		     && hio_mgr->ext_proc_info.ty_tlb[i].gpp_phys <=
		     ul_gpp_pa + ul_seg_size)
		    || (hio_mgr->ext_proc_info.ty_tlb[i].dsp_virt >
			ul_dsp_va - 0x100000 / hio_mgr->word_size
			&& hio_mgr->ext_proc_info.ty_tlb[i].dsp_virt <=
			ul_dsp_va + ul_seg_size / hio_mgr->word_size)) {
			dev_dbg(bridge,
				"CDB MMU entry %d conflicts with "
				"shm.\n\tCDB: GppPa %x, DspVa %x.\n\tSHM: "
				"GppPa %x, DspVa %x, Bytes %x.\n", i,
				hio_mgr->ext_proc_info.ty_tlb[i].gpp_phys,
				hio_mgr->ext_proc_info.ty_tlb[i].dsp_virt,
				ul_gpp_pa, ul_dsp_va, ul_seg_size);
			status = -EPERM;
		} else {
			if (ndx < MAX_LOCK_TLB_ENTRIES) {
				ae_proc[ndx].dsp_va =
				    hio_mgr->ext_proc_info.ty_tlb[i].
				    dsp_virt;
				ae_proc[ndx].gpp_pa =
				    hio_mgr->ext_proc_info.ty_tlb[i].
				    gpp_phys;
				ae_proc[ndx].gpp_va = 0;
				/* 1 MB */
				ae_proc[ndx].size = 0x100000;
				dev_dbg(bridge, "shm MMU entry PA %x "
					"DSP_VA 0x%x\n", ae_proc[ndx].gpp_pa,
					ae_proc[ndx].dsp_va);
				ndx++;
			} else {
				status = hio_mgr->intf_fxns->brd_mem_map
				    (hio_mgr->bridge_context,
				     hio_mgr->ext_proc_info.ty_tlb[i].
				     gpp_phys,
				     hio_mgr->ext_proc_info.ty_tlb[i].
				     dsp_virt, 0x100000, map_attrs,
				     NULL);
			}
		}
		if (status)
			goto func_end;
	}

	map_attrs = 0x00000000;
	map_attrs = DSP_MAPLITTLEENDIAN;
	map_attrs |= DSP_MAPPHYSICALADDR;
	map_attrs |= DSP_MAPELEMSIZE32;
	map_attrs |= DSP_MAPDONOTLOCK;

	/* Map the L4 peripherals */
	i = 0;
	while (l4_peripheral_table[i].phys_addr) {
		status = hio_mgr->intf_fxns->brd_mem_map
		    (hio_mgr->bridge_context, l4_peripheral_table[i].phys_addr,
		     l4_peripheral_table[i].dsp_virt_addr, HW_PAGE_SIZE4KB,
		     map_attrs, NULL);
		if (status)
			goto func_end;
		i++;
	}

	for (i = ndx; i < BRDIOCTL_NUMOFMMUTLB; i++) {
		ae_proc[i].dsp_va = 0;
		ae_proc[i].gpp_pa = 0;
		ae_proc[i].gpp_va = 0;
		ae_proc[i].size = 0;
	}
	/*
	 * Set the shm physical address entry (grayed out in CDB file)
	 * to the virtual uncached ioremapped address of shm reserved
	 * on MPU.
	 */
	hio_mgr->ext_proc_info.ty_tlb[0].gpp_phys =
	    (ul_gpp_va + ul_seg1_size + ul_pad_size);

	/*
	 * Need shm Phys addr. IO supports only one DSP for now:
	 * num_procs = 1.
	 */
	if (!hio_mgr->ext_proc_info.ty_tlb[0].gpp_phys || num_procs != 1) {
		status = -EFAULT;
		goto func_end;
	} else {
		if (ae_proc[0].dsp_va > ul_shm_base) {
			status = -EPERM;
			goto func_end;
		}
		/* ul_shm_base may not be at ul_dsp_va address */
		ul_shm_base_offset = (ul_shm_base - ae_proc[0].dsp_va) *
		    hio_mgr->word_size;
		/*
		 * bridge_dev_ctrl() will set dev context dsp-mmu info. In
		 * bridge_brd_start() the MMU will be re-programed with MMU
		 * DSPVa-GPPPa pair info while DSP is in a known
		 * (reset) state.
		 */

		status =
		    hio_mgr->intf_fxns->dev_cntrl(hio_mgr->bridge_context,
						      BRDIOCTL_SETMMUCONFIG,
						      ae_proc);
		if (status)
			goto func_end;
		ul_shm_base = hio_mgr->ext_proc_info.ty_tlb[0].gpp_phys;
		ul_shm_base += ul_shm_base_offset;
		ul_shm_base = (u32) MEM_LINEAR_ADDRESS((void *)ul_shm_base,
						       ul_mem_length);
		if (ul_shm_base == 0) {
			status = -EFAULT;
			goto func_end;
		}
		/* Register SM */
		status =
		    register_shm_segs(hio_mgr, cod_man, ae_proc[0].gpp_pa);
	}

	hio_mgr->shared_mem = (struct shm *)ul_shm_base;
	hio_mgr->input = (u8 *) hio_mgr->shared_mem + sizeof(struct shm);
	hio_mgr->output = hio_mgr->input + (ul_shm_length -
					    sizeof(struct shm)) / 2;
	hio_mgr->sm_buf_size = hio_mgr->output - hio_mgr->input;

	/*  Set up Shared memory addresses for messaging. */
	hio_mgr->msg_input_ctrl = (struct msg_ctrl *)((u8 *) hio_mgr->shared_mem
						      + ul_shm_length);
	hio_mgr->msg_input =
	    (u8 *) hio_mgr->msg_input_ctrl + sizeof(struct msg_ctrl);
	hio_mgr->msg_output_ctrl =
	    (struct msg_ctrl *)((u8 *) hio_mgr->msg_input_ctrl +
				ul_msg_length / 2);
	hio_mgr->msg_output =
	    (u8 *) hio_mgr->msg_output_ctrl + sizeof(struct msg_ctrl);
	hmsg_mgr->max_msgs =
	    ((u8 *) hio_mgr->msg_output_ctrl - hio_mgr->msg_input)
	    / sizeof(struct msg_dspmsg);
	dev_dbg(bridge, "IO MGR shm details: shared_mem %p, input %p, "
		"output %p, msg_input_ctrl %p, msg_input %p, "
		"msg_output_ctrl %p, msg_output %p\n",
		(u8 *) hio_mgr->shared_mem, hio_mgr->input,
		hio_mgr->output, (u8 *) hio_mgr->msg_input_ctrl,
		hio_mgr->msg_input, (u8 *) hio_mgr->msg_output_ctrl,
		hio_mgr->msg_output);
	dev_dbg(bridge, "(proc) Mas msgs in shared memory: 0x%x\n",
		hmsg_mgr->max_msgs);
	memset((void *)hio_mgr->shared_mem, 0, sizeof(struct shm));

#if defined(CONFIG_TIDSPBRIDGE_BACKTRACE)
	/* Get the start address of trace buffer */
	status = cod_get_sym_value(cod_man, SYS_PUTCBEG,
				   &hio_mgr->trace_buffer_begin);
	if (status) {
		status = -EFAULT;
		goto func_end;
	}

	hio_mgr->gpp_read_pointer = hio_mgr->trace_buffer_begin =
	    (ul_gpp_va + ul_seg1_size + ul_pad_size) +
	    (hio_mgr->trace_buffer_begin - ul_dsp_va);
	/* Get the end address of trace buffer */
	status = cod_get_sym_value(cod_man, SYS_PUTCEND,
				   &hio_mgr->trace_buffer_end);
	if (status) {
		status = -EFAULT;
		goto func_end;
	}
	hio_mgr->trace_buffer_end =
	    (ul_gpp_va + ul_seg1_size + ul_pad_size) +
	    (hio_mgr->trace_buffer_end - ul_dsp_va);
	/* Get the current address of DSP write pointer */
	status = cod_get_sym_value(cod_man, BRIDGE_SYS_PUTC_CURRENT,
				   &hio_mgr->trace_buffer_current);
	if (status) {
		status = -EFAULT;
		goto func_end;
	}
	hio_mgr->trace_buffer_current =
	    (ul_gpp_va + ul_seg1_size + ul_pad_size) +
	    (hio_mgr->trace_buffer_current - ul_dsp_va);
	/* Calculate the size of trace buffer */
	kfree(hio_mgr->msg);
	hio_mgr->msg = kmalloc(((hio_mgr->trace_buffer_end -
				hio_mgr->trace_buffer_begin) *
				hio_mgr->word_size) + 2, GFP_KERNEL);
	if (!hio_mgr->msg)
		status = -ENOMEM;

	hio_mgr->dsp_va = ul_dsp_va;
	hio_mgr->gpp_va = (ul_gpp_va + ul_seg1_size + ul_pad_size);

#endif
func_end:
	return status;
}

/*
 *  ======== io_buf_size ========
 *      Size of shared memory I/O channel.
 */
u32 io_buf_size(struct io_mgr *hio_mgr)
{
	if (hio_mgr)
		return hio_mgr->sm_buf_size;
	else
		return 0;
}

/*
 *  ======== io_cancel_chnl ========
 *      Cancel IO on a given PCPY channel.
 */
void io_cancel_chnl(struct io_mgr *hio_mgr, u32 chnl)
{
	struct io_mgr *pio_mgr = (struct io_mgr *)hio_mgr;
	struct shm *sm;

	if (!hio_mgr)
		goto func_end;
	sm = hio_mgr->shared_mem;

	/* Inform DSP that we have no more buffers on this channel */
	set_chnl_free(sm, chnl);

	sm_interrupt_dsp(pio_mgr->bridge_context, MBX_PCPY_CLASS);
func_end:
	return;
}


/*
 *  ======== io_dispatch_pm ========
 *      Performs I/O dispatch on PM related messages from DSP
 */
static void io_dispatch_pm(struct io_mgr *pio_mgr)
{
	int status;
	u32 parg[2];

	/* Perform Power message processing here */
	parg[0] = pio_mgr->intr_val;

	/* Send the command to the Bridge clk/pwr manager to handle */
	if (parg[0] == MBX_PM_HIBERNATE_EN) {
		dev_dbg(bridge, "PM: Hibernate command\n");
		status = pio_mgr->intf_fxns->
				dev_cntrl(pio_mgr->bridge_context,
					      BRDIOCTL_PWR_HIBERNATE, parg);
		if (status)
			pr_err("%s: hibernate cmd failed 0x%x\n",
				       __func__, status);
	} else if (parg[0] == MBX_PM_OPP_REQ) {
		parg[1] = pio_mgr->shared_mem->opp_request.rqst_opp_pt;
		dev_dbg(bridge, "PM: Requested OPP = 0x%x\n", parg[1]);
		status = pio_mgr->intf_fxns->
				dev_cntrl(pio_mgr->bridge_context,
					BRDIOCTL_CONSTRAINT_REQUEST, parg);
		if (status)
			dev_dbg(bridge, "PM: Failed to set constraint "
				"= 0x%x\n", parg[1]);
	} else {
		dev_dbg(bridge, "PM: clk control value of msg = 0x%x\n",
			parg[0]);
		status = pio_mgr->intf_fxns->
				dev_cntrl(pio_mgr->bridge_context,
					      BRDIOCTL_CLK_CTRL, parg);
		if (status)
			dev_dbg(bridge, "PM: Failed to ctrl the DSP clk"
				"= 0x%x\n", *parg);
	}
}

/*
 *  ======== io_dpc ========
 *      Deferred procedure call for shared memory channel driver ISR.  Carries
 *      out the dispatch of I/O as a non-preemptible event.It can only be
 *      pre-empted      by an ISR.
 */
void io_dpc(unsigned long ref_data)
{
	struct io_mgr *pio_mgr = (struct io_mgr *)ref_data;
	struct chnl_mgr *chnl_mgr_obj;
	struct msg_mgr *msg_mgr_obj;
	struct deh_mgr *hdeh_mgr;
	u32 requested;
	u32 serviced;

	if (!pio_mgr)
		goto func_end;
	chnl_mgr_obj = pio_mgr->chnl_mgr;
	dev_get_msg_mgr(pio_mgr->dev_obj, &msg_mgr_obj);
	dev_get_deh_mgr(pio_mgr->dev_obj, &hdeh_mgr);
	if (!chnl_mgr_obj)
		goto func_end;

	requested = pio_mgr->dpc_req;
	serviced = pio_mgr->dpc_sched;

	if (serviced == requested)
		goto func_end;

	/* Process pending DPC's */
	do {
		/* Check value of interrupt reg to ensure it's a valid error */
		if ((pio_mgr->intr_val > DEH_BASE) &&
		    (pio_mgr->intr_val < DEH_LIMIT)) {
			/* Notify DSP/BIOS exception */
			if (hdeh_mgr) {
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
				print_dsp_debug_trace(pio_mgr);
#endif
				bridge_deh_notify(hdeh_mgr, DSP_SYSERROR,
						  pio_mgr->intr_val);
			}
		}
		/* Proc-copy chanel dispatch */
		input_chnl(pio_mgr, NULL, IO_SERVICE);
		output_chnl(pio_mgr, NULL, IO_SERVICE);

#ifdef CHNL_MESSAGES
		if (msg_mgr_obj) {
			/* Perform I/O dispatch on message queues */
			input_msg(pio_mgr, msg_mgr_obj);
			output_msg(pio_mgr, msg_mgr_obj);
		}

#endif
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
		if (pio_mgr->intr_val & MBX_DBG_SYSPRINTF) {
			/* Notify DSP Trace message */
			print_dsp_debug_trace(pio_mgr);
		}
#endif
		serviced++;
	} while (serviced != requested);
	pio_mgr->dpc_sched = requested;
func_end:
	return;
}

/*
 *  ======== io_mbox_msg ========
 *      Main interrupt handler for the shared memory IO manager.
 *      Calls the Bridge's CHNL_ISR to determine if this interrupt is ours, then
 *      schedules a DPC to dispatch I/O.
 */
int io_mbox_msg(struct notifier_block *self, unsigned long len, void *msg)
{
	struct io_mgr *pio_mgr;
	struct dev_object *dev_obj;
	unsigned long flags;

	dev_obj = dev_get_first();
	dev_get_io_mgr(dev_obj, &pio_mgr);

	if (!pio_mgr)
		return NOTIFY_BAD;

	pio_mgr->intr_val = (u16)((u32)msg);
	if (pio_mgr->intr_val & MBX_PM_CLASS)
		io_dispatch_pm(pio_mgr);

	if (pio_mgr->intr_val == MBX_DEH_RESET) {
		pio_mgr->intr_val = 0;
	} else {
		spin_lock_irqsave(&pio_mgr->dpc_lock, flags);
		pio_mgr->dpc_req++;
		spin_unlock_irqrestore(&pio_mgr->dpc_lock, flags);
		tasklet_schedule(&pio_mgr->dpc_tasklet);
	}
	return NOTIFY_OK;
}

/*
 *  ======== io_request_chnl ========
 *  Purpose:
 *      Request chanenel I/O from the DSP. Sets flags in shared memory, then
 *      interrupts the DSP.
 */
void io_request_chnl(struct io_mgr *io_manager, struct chnl_object *pchnl,
			u8 io_mode, u16 *mbx_val)
{
	struct chnl_mgr *chnl_mgr_obj;
	struct shm *sm;

	if (!pchnl || !mbx_val)
		goto func_end;
	chnl_mgr_obj = io_manager->chnl_mgr;
	sm = io_manager->shared_mem;
	if (io_mode == IO_INPUT) {
		/* Indicate to the DSP we have a buffer available for input */
		set_chnl_busy(sm, pchnl->chnl_id);
		*mbx_val = MBX_PCPY_CLASS;
	} else if (io_mode == IO_OUTPUT) {
		/*
		 * Record the fact that we have a buffer available for
		 * output.
		 */
		chnl_mgr_obj->output_mask |= (1 << pchnl->chnl_id);
	} else {
	}
func_end:
	return;
}

/*
 *  ======== iosm_schedule ========
 *      Schedule DPC for IO.
 */
void iosm_schedule(struct io_mgr *io_manager)
{
	unsigned long flags;

	if (!io_manager)
		return;

	/* Increment count of DPC's pending. */
	spin_lock_irqsave(&io_manager->dpc_lock, flags);
	io_manager->dpc_req++;
	spin_unlock_irqrestore(&io_manager->dpc_lock, flags);

	/* Schedule DPC */
	tasklet_schedule(&io_manager->dpc_tasklet);
}

/*
 *  ======== find_ready_output ========
 *      Search for a host output channel which is ready to send.  If this is
 *      called as a result of servicing the DPC, then implement a round
 *      robin search; otherwise, this was called by a client thread (via
 *      IO_Dispatch()), so just start searching from the current channel id.
 */
static u32 find_ready_output(struct chnl_mgr *chnl_mgr_obj,
			     struct chnl_object *pchnl, u32 mask)
{
	u32 ret = OUTPUTNOTREADY;
	u32 id, start_id;
	u32 shift;

	id = (pchnl !=
	      NULL ? pchnl->chnl_id : (chnl_mgr_obj->last_output + 1));
	id = ((id == CHNL_MAXCHANNELS) ? 0 : id);
	if (id >= CHNL_MAXCHANNELS)
		goto func_end;
	if (mask) {
		shift = (1 << id);
		start_id = id;
		do {
			if (mask & shift) {
				ret = id;
				if (pchnl == NULL)
					chnl_mgr_obj->last_output = id;
				break;
			}
			id = id + 1;
			id = ((id == CHNL_MAXCHANNELS) ? 0 : id);
			shift = (1 << id);
		} while (id != start_id);
	}
func_end:
	return ret;
}

/*
 *  ======== input_chnl ========
 *      Dispatch a buffer on an input channel.
 */
static void input_chnl(struct io_mgr *pio_mgr, struct chnl_object *pchnl,
			u8 io_mode)
{
	struct chnl_mgr *chnl_mgr_obj;
	struct shm *sm;
	u32 chnl_id;
	u32 bytes;
	struct chnl_irp *chnl_packet_obj = NULL;
	u32 dw_arg;
	bool clear_chnl = false;
	bool notify_client = false;

	sm = pio_mgr->shared_mem;
	chnl_mgr_obj = pio_mgr->chnl_mgr;

	/* Attempt to perform input */
	if (!sm->input_full)
		goto func_end;

	bytes = sm->input_size * chnl_mgr_obj->word_size;
	chnl_id = sm->input_id;
	dw_arg = sm->arg;
	if (chnl_id >= CHNL_MAXCHANNELS) {
		/* Shouldn't be here: would indicate corrupted shm. */
		goto func_end;
	}
	pchnl = chnl_mgr_obj->channels[chnl_id];
	if ((pchnl != NULL) && CHNL_IS_INPUT(pchnl->chnl_mode)) {
		if ((pchnl->state & ~CHNL_STATEEOS) == CHNL_STATEREADY) {
			/* Get the I/O request, and attempt a transfer */
			if (!list_empty(&pchnl->io_requests)) {
				if (!pchnl->cio_reqs)
					goto func_end;

				chnl_packet_obj = list_first_entry(
						&pchnl->io_requests,
						struct chnl_irp, link);
				list_del(&chnl_packet_obj->link);
				pchnl->cio_reqs--;

				/*
				 * Ensure we don't overflow the client's
				 * buffer.
				 */
				bytes = min(bytes, chnl_packet_obj->byte_size);
				memcpy(chnl_packet_obj->host_sys_buf,
						pio_mgr->input, bytes);
				pchnl->bytes_moved += bytes;
				chnl_packet_obj->byte_size = bytes;
				chnl_packet_obj->arg = dw_arg;
				chnl_packet_obj->status = CHNL_IOCSTATCOMPLETE;

				if (bytes == 0) {
					/*
					 * This assertion fails if the DSP
					 * sends EOS more than once on this
					 * channel.
					 */
					if (pchnl->state & CHNL_STATEEOS)
						goto func_end;
					/*
					 * Zero bytes indicates EOS. Update
					 * IOC status for this chirp, and also
					 * the channel state.
					 */
					chnl_packet_obj->status |=
						CHNL_IOCSTATEOS;
					pchnl->state |= CHNL_STATEEOS;
					/*
					 * Notify that end of stream has
					 * occurred.
					 */
					ntfy_notify(pchnl->ntfy_obj,
							DSP_STREAMDONE);
				}
				/* Tell DSP if no more I/O buffers available */
				if (list_empty(&pchnl->io_requests))
					set_chnl_free(sm, pchnl->chnl_id);
				clear_chnl = true;
				notify_client = true;
			} else {
				/*
				 * Input full for this channel, but we have no
				 * buffers available.  The channel must be
				 * "idling". Clear out the physical input
				 * channel.
				 */
				clear_chnl = true;
			}
		} else {
			/* Input channel cancelled: clear input channel */
			clear_chnl = true;
		}
	} else {
		/* DPC fired after host closed channel: clear input channel */
		clear_chnl = true;
	}
	if (clear_chnl) {
		/* Indicate to the DSP we have read the input */
		sm->input_full = 0;
		sm_interrupt_dsp(pio_mgr->bridge_context, MBX_PCPY_CLASS);
	}
	if (notify_client) {
		/* Notify client with IO completion record */
		notify_chnl_complete(pchnl, chnl_packet_obj);
	}
func_end:
	return;
}

/*
 *  ======== input_msg ========
 *      Copies messages from shared memory to the message queues.
 */
static void input_msg(struct io_mgr *pio_mgr, struct msg_mgr *hmsg_mgr)
{
	u32 num_msgs;
	u32 i;
	u8 *msg_input;
	struct msg_queue *msg_queue_obj;
	struct msg_frame *pmsg;
	struct msg_dspmsg msg;
	struct msg_ctrl *msg_ctr_obj;
	u32 input_empty;
	u32 addr;

	msg_ctr_obj = pio_mgr->msg_input_ctrl;
	/* Get the number of input messages to be read */
	input_empty = msg_ctr_obj->buf_empty;
	num_msgs = msg_ctr_obj->size;
	if (input_empty)
		return;

	msg_input = pio_mgr->msg_input;
	for (i = 0; i < num_msgs; i++) {
		/* Read the next message */
		addr = (u32) &(((struct msg_dspmsg *)msg_input)->msg.cmd);
		msg.msg.cmd =
			read_ext32_bit_dsp_data(pio_mgr->bridge_context, addr);
		addr = (u32) &(((struct msg_dspmsg *)msg_input)->msg.arg1);
		msg.msg.arg1 =
			read_ext32_bit_dsp_data(pio_mgr->bridge_context, addr);
		addr = (u32) &(((struct msg_dspmsg *)msg_input)->msg.arg2);
		msg.msg.arg2 =
			read_ext32_bit_dsp_data(pio_mgr->bridge_context, addr);
		addr = (u32) &(((struct msg_dspmsg *)msg_input)->msgq_id);
		msg.msgq_id =
			read_ext32_bit_dsp_data(pio_mgr->bridge_context, addr);
		msg_input += sizeof(struct msg_dspmsg);

		/* Determine which queue to put the message in */
		dev_dbg(bridge,	"input msg: cmd=0x%x arg1=0x%x "
				"arg2=0x%x msgq_id=0x%x\n", msg.msg.cmd,
				msg.msg.arg1, msg.msg.arg2, msg.msgq_id);
		/*
		 * Interrupt may occur before shared memory and message
		 * input locations have been set up. If all nodes were
		 * cleaned up, hmsg_mgr->max_msgs should be 0.
		 */
		list_for_each_entry(msg_queue_obj, &hmsg_mgr->queue_list,
				list_elem) {
			if (msg.msgq_id != msg_queue_obj->msgq_id)
				continue;
			/* Found it */
			if (msg.msg.cmd == RMS_EXITACK) {
				/*
				 * Call the node exit notification.
				 * The exit message does not get
				 * queued.
				 */
				(*hmsg_mgr->on_exit)(msg_queue_obj->arg,
						msg.msg.arg1);
				break;
			}
			/*
			 * Not an exit acknowledgement, queue
			 * the message.
			 */
			if (list_empty(&msg_queue_obj->msg_free_list)) {
				/*
				 * No free frame to copy the
				 * message into.
				 */
				pr_err("%s: no free msg frames,"
						" discarding msg\n",
						__func__);
				break;
			}

			pmsg = list_first_entry(&msg_queue_obj->msg_free_list,
					struct msg_frame, list_elem);
			list_del(&pmsg->list_elem);
			pmsg->msg_data = msg;
			list_add_tail(&pmsg->list_elem,
					&msg_queue_obj->msg_used_list);
			ntfy_notify(msg_queue_obj->ntfy_obj,
					DSP_NODEMESSAGEREADY);
			sync_set_event(msg_queue_obj->sync_event);
		}
	}
	/* Set the post SWI flag */
	if (num_msgs > 0) {
		/* Tell the DSP we've read the messages */
		msg_ctr_obj->buf_empty = true;
		msg_ctr_obj->post_swi = true;
		sm_interrupt_dsp(pio_mgr->bridge_context, MBX_PCPY_CLASS);
	}
}

/*
 *  ======== notify_chnl_complete ========
 *  Purpose:
 *      Signal the channel event, notifying the client that I/O has completed.
 */
static void notify_chnl_complete(struct chnl_object *pchnl,
				 struct chnl_irp *chnl_packet_obj)
{
	bool signal_event;

	if (!pchnl || !pchnl->sync_event || !chnl_packet_obj)
		goto func_end;

	/*
	 * Note: we signal the channel event only if the queue of IO
	 * completions is empty.  If it is not empty, the event is sure to be
	 * signalled by the only IO completion list consumer:
	 * bridge_chnl_get_ioc().
	 */
	signal_event = list_empty(&pchnl->io_completions);
	/* Enqueue the IO completion info for the client */
	list_add_tail(&chnl_packet_obj->link, &pchnl->io_completions);
	pchnl->cio_cs++;

	if (pchnl->cio_cs > pchnl->chnl_packets)
		goto func_end;
	/* Signal the channel event (if not already set) that IO is complete */
	if (signal_event)
		sync_set_event(pchnl->sync_event);

	/* Notify that IO is complete */
	ntfy_notify(pchnl->ntfy_obj, DSP_STREAMIOCOMPLETION);
func_end:
	return;
}

/*
 *  ======== output_chnl ========
 *  Purpose:
 *      Dispatch a buffer on an output channel.
 */
static void output_chnl(struct io_mgr *pio_mgr, struct chnl_object *pchnl,
			u8 io_mode)
{
	struct chnl_mgr *chnl_mgr_obj;
	struct shm *sm;
	u32 chnl_id;
	struct chnl_irp *chnl_packet_obj;
	u32 dw_dsp_f_mask;

	chnl_mgr_obj = pio_mgr->chnl_mgr;
	sm = pio_mgr->shared_mem;
	/* Attempt to perform output */
	if (sm->output_full)
		goto func_end;

	if (pchnl && !((pchnl->state & ~CHNL_STATEEOS) == CHNL_STATEREADY))
		goto func_end;

	/* Look to see if both a PC and DSP output channel are ready */
	dw_dsp_f_mask = sm->dsp_free_mask;
	chnl_id =
	    find_ready_output(chnl_mgr_obj, pchnl,
			      (chnl_mgr_obj->output_mask & dw_dsp_f_mask));
	if (chnl_id == OUTPUTNOTREADY)
		goto func_end;

	pchnl = chnl_mgr_obj->channels[chnl_id];
	if (!pchnl || list_empty(&pchnl->io_requests)) {
		/* Shouldn't get here */
		goto func_end;
	}

	if (!pchnl->cio_reqs)
		goto func_end;

	/* Get the I/O request, and attempt a transfer */
	chnl_packet_obj = list_first_entry(&pchnl->io_requests,
			struct chnl_irp, link);
	list_del(&chnl_packet_obj->link);

	pchnl->cio_reqs--;

	/* Record fact that no more I/O buffers available */
	if (list_empty(&pchnl->io_requests))
		chnl_mgr_obj->output_mask &= ~(1 << chnl_id);

	/* Transfer buffer to DSP side */
	chnl_packet_obj->byte_size = min(pio_mgr->sm_buf_size,
					chnl_packet_obj->byte_size);
	memcpy(pio_mgr->output,	chnl_packet_obj->host_sys_buf,
					chnl_packet_obj->byte_size);
	pchnl->bytes_moved += chnl_packet_obj->byte_size;
	/* Write all 32 bits of arg */
	sm->arg = chnl_packet_obj->arg;
#if _CHNL_WORDSIZE == 2
	/* Access can be different SM access word size (e.g. 16/32 bit words) */
	sm->output_id = (u16) chnl_id;
	sm->output_size = (u16) (chnl_packet_obj->byte_size +
				chnl_mgr_obj->word_size - 1) /
				(u16) chnl_mgr_obj->word_size;
#else
	sm->output_id = chnl_id;
	sm->output_size = (chnl_packet_obj->byte_size +
			chnl_mgr_obj->word_size - 1) / chnl_mgr_obj->word_size;
#endif
	sm->output_full =  1;
	/* Indicate to the DSP we have written the output */
	sm_interrupt_dsp(pio_mgr->bridge_context, MBX_PCPY_CLASS);
	/* Notify client with IO completion record (keep EOS) */
	chnl_packet_obj->status &= CHNL_IOCSTATEOS;
	notify_chnl_complete(pchnl, chnl_packet_obj);
	/* Notify if stream is done. */
	if (chnl_packet_obj->status & CHNL_IOCSTATEOS)
		ntfy_notify(pchnl->ntfy_obj, DSP_STREAMDONE);

func_end:
	return;
}

/*
 *  ======== output_msg ========
 *      Copies messages from the message queues to the shared memory.
 */
static void output_msg(struct io_mgr *pio_mgr, struct msg_mgr *hmsg_mgr)
{
	u32 num_msgs = 0;
	u32 i;
	struct msg_dspmsg *msg_output;
	struct msg_frame *pmsg;
	struct msg_ctrl *msg_ctr_obj;
	u32 val;
	u32 addr;

	msg_ctr_obj = pio_mgr->msg_output_ctrl;

	/* Check if output has been cleared */
	if (!msg_ctr_obj->buf_empty)
		return;

	num_msgs = (hmsg_mgr->msgs_pending > hmsg_mgr->max_msgs) ?
		hmsg_mgr->max_msgs : hmsg_mgr->msgs_pending;
	msg_output = (struct msg_dspmsg *) pio_mgr->msg_output;

	/* Copy num_msgs messages into shared memory */
	for (i = 0; i < num_msgs; i++) {
		if (list_empty(&hmsg_mgr->msg_used_list))
			continue;

		pmsg = list_first_entry(&hmsg_mgr->msg_used_list,
				struct msg_frame, list_elem);
		list_del(&pmsg->list_elem);

		val = (pmsg->msg_data).msgq_id;
		addr = (u32) &msg_output->msgq_id;
		write_ext32_bit_dsp_data(pio_mgr->bridge_context, addr, val);

		val = (pmsg->msg_data).msg.cmd;
		addr = (u32) &msg_output->msg.cmd;
		write_ext32_bit_dsp_data(pio_mgr->bridge_context, addr, val);

		val = (pmsg->msg_data).msg.arg1;
		addr = (u32) &msg_output->msg.arg1;
		write_ext32_bit_dsp_data(pio_mgr->bridge_context, addr, val);

		val = (pmsg->msg_data).msg.arg2;
		addr = (u32) &msg_output->msg.arg2;
		write_ext32_bit_dsp_data(pio_mgr->bridge_context, addr, val);

		msg_output++;
		list_add_tail(&pmsg->list_elem, &hmsg_mgr->msg_free_list);
		sync_set_event(hmsg_mgr->sync_event);
	}

	if (num_msgs > 0) {
		hmsg_mgr->msgs_pending -= num_msgs;
#if _CHNL_WORDSIZE == 2
		/*
		 * Access can be different SM access word size
		 * (e.g. 16/32 bit words)
		 */
		msg_ctr_obj->size = (u16) num_msgs;
#else
		msg_ctr_obj->size = num_msgs;
#endif
		msg_ctr_obj->buf_empty = false;
		/* Set the post SWI flag */
		msg_ctr_obj->post_swi = true;
		/* Tell the DSP we have written the output. */
		sm_interrupt_dsp(pio_mgr->bridge_context, MBX_PCPY_CLASS);
	}
}

/*
 *  ======== register_shm_segs ========
 *  purpose:
 *      Registers GPP SM segment with CMM.
 */
static int register_shm_segs(struct io_mgr *hio_mgr,
				    struct cod_manager *cod_man,
				    u32 dw_gpp_base_pa)
{
	int status = 0;
	u32 ul_shm0_base = 0;
	u32 shm0_end = 0;
	u32 ul_shm0_rsrvd_start = 0;
	u32 ul_rsrvd_size = 0;
	u32 ul_gpp_phys;
	u32 ul_dsp_virt;
	u32 ul_shm_seg_id0 = 0;
	u32 dw_offset, dw_gpp_base_va, ul_dsp_size;

	/*
	 * Read address and size info for first SM region.
	 * Get start of 1st SM Heap region.
	 */
	status =
	    cod_get_sym_value(cod_man, SHM0_SHARED_BASE_SYM, &ul_shm0_base);
	if (ul_shm0_base == 0) {
		status = -EPERM;
		goto func_end;
	}
	/* Get end of 1st SM Heap region */
	if (!status) {
		/* Get start and length of message part of shared memory */
		status = cod_get_sym_value(cod_man, SHM0_SHARED_END_SYM,
					   &shm0_end);
		if (shm0_end == 0) {
			status = -EPERM;
			goto func_end;
		}
	}
	/* Start of Gpp reserved region */
	if (!status) {
		/* Get start and length of message part of shared memory */
		status =
		    cod_get_sym_value(cod_man, SHM0_SHARED_RESERVED_BASE_SYM,
				      &ul_shm0_rsrvd_start);
		if (ul_shm0_rsrvd_start == 0) {
			status = -EPERM;
			goto func_end;
		}
	}
	/* Register with CMM */
	if (!status) {
		status = dev_get_cmm_mgr(hio_mgr->dev_obj, &hio_mgr->cmm_mgr);
		if (!status) {
			status = cmm_un_register_gppsm_seg(hio_mgr->cmm_mgr,
							   CMM_ALLSEGMENTS);
		}
	}
	/* Register new SM region(s) */
	if (!status && (shm0_end - ul_shm0_base) > 0) {
		/* Calc size (bytes) of SM the GPP can alloc from */
		ul_rsrvd_size =
		    (shm0_end - ul_shm0_rsrvd_start + 1) * hio_mgr->word_size;
		if (ul_rsrvd_size <= 0) {
			status = -EPERM;
			goto func_end;
		}
		/* Calc size of SM DSP can alloc from */
		ul_dsp_size =
		    (ul_shm0_rsrvd_start - ul_shm0_base) * hio_mgr->word_size;
		if (ul_dsp_size <= 0) {
			status = -EPERM;
			goto func_end;
		}
		/* First TLB entry reserved for Bridge SM use. */
		ul_gpp_phys = hio_mgr->ext_proc_info.ty_tlb[0].gpp_phys;
		/* Get size in bytes */
		ul_dsp_virt =
		    hio_mgr->ext_proc_info.ty_tlb[0].dsp_virt *
		    hio_mgr->word_size;
		/*
		 * Calc byte offset used to convert GPP phys <-> DSP byte
		 * address.
		 */
		if (dw_gpp_base_pa > ul_dsp_virt)
			dw_offset = dw_gpp_base_pa - ul_dsp_virt;
		else
			dw_offset = ul_dsp_virt - dw_gpp_base_pa;

		if (ul_shm0_rsrvd_start * hio_mgr->word_size < ul_dsp_virt) {
			status = -EPERM;
			goto func_end;
		}
		/*
		 * Calc Gpp phys base of SM region.
		 * This is actually uncached kernel virtual address.
		 */
		dw_gpp_base_va =
		    ul_gpp_phys + ul_shm0_rsrvd_start * hio_mgr->word_size -
		    ul_dsp_virt;
		/*
		 * Calc Gpp phys base of SM region.
		 * This is the physical address.
		 */
		dw_gpp_base_pa =
		    dw_gpp_base_pa + ul_shm0_rsrvd_start * hio_mgr->word_size -
		    ul_dsp_virt;
		/* Register SM Segment 0. */
		status =
		    cmm_register_gppsm_seg(hio_mgr->cmm_mgr, dw_gpp_base_pa,
					   ul_rsrvd_size, dw_offset,
					   (dw_gpp_base_pa >
					    ul_dsp_virt) ? CMM_ADDTODSPPA :
					   CMM_SUBFROMDSPPA,
					   (u32) (ul_shm0_base *
						  hio_mgr->word_size),
					   ul_dsp_size, &ul_shm_seg_id0,
					   dw_gpp_base_va);
		/* First SM region is seg_id = 1 */
		if (ul_shm_seg_id0 != 1)
			status = -EPERM;
	}
func_end:
	return status;
}

/* ZCPY IO routines. */
/*
 *  ======== IO_SHMcontrol ========
 *      Sets the requested shm setting.
 */
int io_sh_msetting(struct io_mgr *hio_mgr, u8 desc, void *pargs)
{
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 i;
	struct dspbridge_platform_data *pdata =
	    omap_dspbridge_dev->dev.platform_data;

	switch (desc) {
	case SHM_CURROPP:
		/* Update the shared memory with requested OPP information */
		if (pargs != NULL)
			hio_mgr->shared_mem->opp_table_struct.curr_opp_pt =
			    *(u32 *) pargs;
		else
			return -EPERM;
		break;
	case SHM_OPPINFO:
		/*
		 * Update the shared memory with the voltage, frequency,
		 * min and max frequency values for an OPP.
		 */
		for (i = 0; i <= dsp_max_opps; i++) {
			hio_mgr->shared_mem->opp_table_struct.opp_point[i].
			    voltage = vdd1_dsp_freq[i][0];
			dev_dbg(bridge, "OPP-shm: voltage: %d\n",
				vdd1_dsp_freq[i][0]);
			hio_mgr->shared_mem->opp_table_struct.
			    opp_point[i].frequency = vdd1_dsp_freq[i][1];
			dev_dbg(bridge, "OPP-shm: frequency: %d\n",
				vdd1_dsp_freq[i][1]);
			hio_mgr->shared_mem->opp_table_struct.opp_point[i].
			    min_freq = vdd1_dsp_freq[i][2];
			dev_dbg(bridge, "OPP-shm: min freq: %d\n",
				vdd1_dsp_freq[i][2]);
			hio_mgr->shared_mem->opp_table_struct.opp_point[i].
			    max_freq = vdd1_dsp_freq[i][3];
			dev_dbg(bridge, "OPP-shm: max freq: %d\n",
				vdd1_dsp_freq[i][3]);
		}
		hio_mgr->shared_mem->opp_table_struct.num_opp_pts =
		    dsp_max_opps;
		dev_dbg(bridge, "OPP-shm: max OPP number: %d\n", dsp_max_opps);
		/* Update the current OPP number */
		if (pdata->dsp_get_opp)
			i = (*pdata->dsp_get_opp) ();
		hio_mgr->shared_mem->opp_table_struct.curr_opp_pt = i;
		dev_dbg(bridge, "OPP-shm: value programmed = %d\n", i);
		break;
	case SHM_GETOPP:
		/* Get the OPP that DSP has requested */
		*(u32 *) pargs = hio_mgr->shared_mem->opp_request.rqst_opp_pt;
		break;
	default:
		break;
	}
#endif
	return 0;
}

/*
 *  ======== bridge_io_get_proc_load ========
 *      Gets the Processor's Load information
 */
int bridge_io_get_proc_load(struct io_mgr *hio_mgr,
				struct dsp_procloadstat *proc_lstat)
{
	if (!hio_mgr->shared_mem)
		return -EFAULT;

	proc_lstat->curr_load =
			hio_mgr->shared_mem->load_mon_info.curr_dsp_load;
	proc_lstat->predicted_load =
	    hio_mgr->shared_mem->load_mon_info.pred_dsp_load;
	proc_lstat->curr_dsp_freq =
	    hio_mgr->shared_mem->load_mon_info.curr_dsp_freq;
	proc_lstat->predicted_freq =
	    hio_mgr->shared_mem->load_mon_info.pred_dsp_freq;

	dev_dbg(bridge, "Curr Load = %d, Pred Load = %d, Curr Freq = %d, "
		"Pred Freq = %d\n", proc_lstat->curr_load,
		proc_lstat->predicted_load, proc_lstat->curr_dsp_freq,
		proc_lstat->predicted_freq);
	return 0;
}


#if defined(CONFIG_TIDSPBRIDGE_BACKTRACE)
void print_dsp_debug_trace(struct io_mgr *hio_mgr)
{
	u32 ul_new_message_length = 0, ul_gpp_cur_pointer;

	while (true) {
		/* Get the DSP current pointer */
		ul_gpp_cur_pointer =
		    *(u32 *) (hio_mgr->trace_buffer_current);
		ul_gpp_cur_pointer =
		    hio_mgr->gpp_va + (ul_gpp_cur_pointer -
					  hio_mgr->dsp_va);

		/* No new debug messages available yet */
		if (ul_gpp_cur_pointer == hio_mgr->gpp_read_pointer) {
			break;
		} else if (ul_gpp_cur_pointer > hio_mgr->gpp_read_pointer) {
			/* Continuous data */
			ul_new_message_length =
			    ul_gpp_cur_pointer - hio_mgr->gpp_read_pointer;

			memcpy(hio_mgr->msg,
			       (char *)hio_mgr->gpp_read_pointer,
			       ul_new_message_length);
			hio_mgr->msg[ul_new_message_length] = '\0';
			/*
			 * Advance the GPP trace pointer to DSP current
			 * pointer.
			 */
			hio_mgr->gpp_read_pointer += ul_new_message_length;
			/* Print the trace messages */
			pr_info("DSPTrace: %s\n", hio_mgr->msg);
		} else if (ul_gpp_cur_pointer < hio_mgr->gpp_read_pointer) {
			/* Handle trace buffer wraparound */
			memcpy(hio_mgr->msg,
			       (char *)hio_mgr->gpp_read_pointer,
			       hio_mgr->trace_buffer_end -
			       hio_mgr->gpp_read_pointer);
			ul_new_message_length =
			    ul_gpp_cur_pointer - hio_mgr->trace_buffer_begin;
			memcpy(&hio_mgr->msg[hio_mgr->trace_buffer_end -
					      hio_mgr->gpp_read_pointer],
			       (char *)hio_mgr->trace_buffer_begin,
			       ul_new_message_length);
			hio_mgr->msg[hio_mgr->trace_buffer_end -
				      hio_mgr->gpp_read_pointer +
				      ul_new_message_length] = '\0';
			/*
			 * Advance the GPP trace pointer to DSP current
			 * pointer.
			 */
			hio_mgr->gpp_read_pointer =
			    hio_mgr->trace_buffer_begin +
			    ul_new_message_length;
			/* Print the trace messages */
			pr_info("DSPTrace: %s\n", hio_mgr->msg);
		}
	}
}
#endif

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
/*
 *  ======== print_dsp_trace_buffer ========
 *      Prints the trace buffer returned from the DSP (if DBG_Trace is enabled).
 *  Parameters:
 *    hdeh_mgr:          Handle to DEH manager object
 *                      number of extra carriage returns to generate.
 *  Returns:
 *      0:        Success.
 *      -ENOMEM:    Unable to allocate memory.
 *  Requires:
 *      hdeh_mgr muse be valid. Checked in bridge_deh_notify.
 */
int print_dsp_trace_buffer(struct bridge_dev_context *hbridge_context)
{
	int status = 0;
	struct cod_manager *cod_mgr;
	u32 ul_trace_end;
	u32 ul_trace_begin;
	u32 trace_cur_pos;
	u32 ul_num_bytes = 0;
	u32 ul_num_words = 0;
	u32 ul_word_size = 2;
	char *psz_buf;
	char *str_beg;
	char *trace_end;
	char *buf_end;
	char *new_line;

	struct bridge_dev_context *pbridge_context = hbridge_context;
	struct bridge_drv_interface *intf_fxns;
	struct dev_object *dev_obj = (struct dev_object *)
	    pbridge_context->dev_obj;

	status = dev_get_cod_mgr(dev_obj, &cod_mgr);

	if (cod_mgr) {
		/* Look for SYS_PUTCBEG/SYS_PUTCEND */
		status =
		    cod_get_sym_value(cod_mgr, COD_TRACEBEG, &ul_trace_begin);
	} else {
		status = -EFAULT;
	}
	if (!status)
		status =
		    cod_get_sym_value(cod_mgr, COD_TRACEEND, &ul_trace_end);

	if (!status)
		/* trace_cur_pos will hold the address of a DSP pointer */
		status = cod_get_sym_value(cod_mgr, COD_TRACECURPOS,
							&trace_cur_pos);

	if (status)
		goto func_end;

	ul_num_bytes = (ul_trace_end - ul_trace_begin);

	ul_num_words = ul_num_bytes * ul_word_size;
	status = dev_get_intf_fxns(dev_obj, &intf_fxns);

	if (status)
		goto func_end;

	psz_buf = kzalloc(ul_num_bytes + 2, GFP_ATOMIC);
	if (psz_buf != NULL) {
		/* Read trace buffer data */
		status = (*intf_fxns->brd_read)(pbridge_context,
			(u8 *)psz_buf, (u32)ul_trace_begin,
			ul_num_bytes, 0);

		if (status)
			goto func_end;

		/* Pack and do newline conversion */
		pr_debug("PrintDspTraceBuffer: "
			"before pack and unpack.\n");
		pr_debug("%s: DSP Trace Buffer Begin:\n"
			"=======================\n%s\n",
			__func__, psz_buf);

		/* Read the value at the DSP address in trace_cur_pos. */
		status = (*intf_fxns->brd_read)(pbridge_context,
				(u8 *)&trace_cur_pos, (u32)trace_cur_pos,
				4, 0);
		if (status)
			goto func_end;
		/* Pack and do newline conversion */
		pr_info("DSP Trace Buffer Begin:\n"
			"=======================\n%s\n",
			psz_buf);


		/* convert to offset */
		trace_cur_pos = trace_cur_pos - ul_trace_begin;

		if (ul_num_bytes) {
			/*
			 * The buffer is not full, find the end of the
			 * data -- buf_end will be >= pszBuf after
			 * while.
			 */
			buf_end = &psz_buf[ul_num_bytes+1];
			/* DSP print position */
			trace_end = &psz_buf[trace_cur_pos];

			/*
			 * Search buffer for a new_line and replace it
			 * with '\0', then print as string.
			 * Continue until end of buffer is reached.
			 */
			str_beg = trace_end;
			ul_num_bytes = buf_end - str_beg;

			while (str_beg < buf_end) {
				new_line = strnchr(str_beg, ul_num_bytes,
								'\n');
				if (new_line && new_line < buf_end) {
					*new_line = 0;
					pr_debug("%s\n", str_beg);
					str_beg = ++new_line;
					ul_num_bytes = buf_end - str_beg;
				} else {
					/*
					 * Assume buffer empty if it contains
					 * a zero
					 */
					if (*str_beg != '\0') {
						str_beg[ul_num_bytes] = 0;
						pr_debug("%s\n", str_beg);
					}
					str_beg = buf_end;
					ul_num_bytes = 0;
				}
			}
			/*
			 * Search buffer for a nNewLine and replace it
			 * with '\0', then print as string.
			 * Continue until buffer is exhausted.
			 */
			str_beg = psz_buf;
			ul_num_bytes = trace_end - str_beg;

			while (str_beg < trace_end) {
				new_line = strnchr(str_beg, ul_num_bytes, '\n');
				if (new_line != NULL && new_line < trace_end) {
					*new_line = 0;
					pr_debug("%s\n", str_beg);
					str_beg = ++new_line;
					ul_num_bytes = trace_end - str_beg;
				} else {
					/*
					 * Assume buffer empty if it contains
					 * a zero
					 */
					if (*str_beg != '\0') {
						str_beg[ul_num_bytes] = 0;
						pr_debug("%s\n", str_beg);
					}
					str_beg = trace_end;
					ul_num_bytes = 0;
				}
			}
		}
		pr_info("\n=======================\n"
			"DSP Trace Buffer End:\n");
		kfree(psz_buf);
	} else {
		status = -ENOMEM;
	}
func_end:
	if (status)
		dev_dbg(bridge, "%s Failed, status 0x%x\n", __func__, status);
	return status;
}

/**
 * dump_dsp_stack() - This function dumps the data on the DSP stack.
 * @bridge_context:	Bridge driver's device context pointer.
 *
 */
int dump_dsp_stack(struct bridge_dev_context *bridge_context)
{
	int status = 0;
	struct cod_manager *code_mgr;
	struct node_mgr *node_mgr;
	u32 trace_begin;
	char name[256];
	struct {
		u32 head[2];
		u32 size;
	} mmu_fault_dbg_info;
	u32 *buffer;
	u32 *buffer_beg;
	u32 *buffer_end;
	u32 exc_type;
	u32 dyn_ext_base;
	u32 i;
	u32 offset_output;
	u32 total_size;
	u32 poll_cnt;
	const char *dsp_regs[] = {"EFR", "IERR", "ITSR", "NTSR",
				"IRP", "NRP", "AMR", "SSR",
				"ILC", "RILC", "IER", "CSR"};
	const char *exec_ctxt[] = {"Task", "SWI", "HWI", "Unknown"};
	struct bridge_drv_interface *intf_fxns;
	struct dev_object *dev_object = bridge_context->dev_obj;

	status = dev_get_cod_mgr(dev_object, &code_mgr);
	if (!code_mgr) {
		pr_debug("%s: Failed on dev_get_cod_mgr.\n", __func__);
		status = -EFAULT;
	}

	if (!status) {
		status = dev_get_node_manager(dev_object, &node_mgr);
		if (!node_mgr) {
			pr_debug("%s: Failed on dev_get_node_manager.\n",
								__func__);
			status = -EFAULT;
		}
	}

	if (!status) {
		/* Look for SYS_PUTCBEG/SYS_PUTCEND: */
		status =
			cod_get_sym_value(code_mgr, COD_TRACEBEG, &trace_begin);
		pr_debug("%s: trace_begin Value 0x%x\n",
			__func__, trace_begin);
		if (status)
			pr_debug("%s: Failed on cod_get_sym_value.\n",
								__func__);
	}
	if (!status)
		status = dev_get_intf_fxns(dev_object, &intf_fxns);
	/*
	 * Check for the "magic number" in the trace buffer.  If it has
	 * yet to appear then poll the trace buffer to wait for it.  Its
	 * appearance signals that the DSP has finished dumping its state.
	 */
	mmu_fault_dbg_info.head[0] = 0;
	mmu_fault_dbg_info.head[1] = 0;
	if (!status) {
		poll_cnt = 0;
		while ((mmu_fault_dbg_info.head[0] != MMU_FAULT_HEAD1 ||
			mmu_fault_dbg_info.head[1] != MMU_FAULT_HEAD2) &&
			poll_cnt < POLL_MAX) {

			/* Read DSP dump size from the DSP trace buffer... */
			status = (*intf_fxns->brd_read)(bridge_context,
				(u8 *)&mmu_fault_dbg_info, (u32)trace_begin,
				sizeof(mmu_fault_dbg_info), 0);

			if (status)
				break;

			poll_cnt++;
		}

		if (mmu_fault_dbg_info.head[0] != MMU_FAULT_HEAD1 &&
			mmu_fault_dbg_info.head[1] != MMU_FAULT_HEAD2) {
			status = -ETIME;
			pr_err("%s:No DSP MMU-Fault information available.\n",
							__func__);
		}
	}

	if (!status) {
		total_size = mmu_fault_dbg_info.size;
		/* Limit the size in case DSP went crazy */
		if (total_size > MAX_MMU_DBGBUFF)
			total_size = MAX_MMU_DBGBUFF;

		buffer = kzalloc(total_size, GFP_ATOMIC);
		if (!buffer) {
			status = -ENOMEM;
			pr_debug("%s: Failed to "
				"allocate stack dump buffer.\n", __func__);
			goto func_end;
		}

		buffer_beg = buffer;
		buffer_end =  buffer + total_size / 4;

		/* Read bytes from the DSP trace buffer... */
		status = (*intf_fxns->brd_read)(bridge_context,
				(u8 *)buffer, (u32)trace_begin,
				total_size, 0);
		if (status) {
			pr_debug("%s: Failed to Read Trace Buffer.\n",
								__func__);
			goto func_end;
		}

		pr_err("\nAproximate Crash Position:\n"
			"--------------------------\n");

		exc_type = buffer[3];
		if (!exc_type)
			i = buffer[79];         /* IRP */
		else
			i = buffer[80];         /* NRP */

		status =
		    cod_get_sym_value(code_mgr, DYNEXTBASE, &dyn_ext_base);
		if (status) {
			status = -EFAULT;
			goto func_end;
		}

		if ((i > dyn_ext_base) && (node_find_addr(node_mgr, i,
			0x1000, &offset_output, name) == 0))
			pr_err("0x%-8x [\"%s\" + 0x%x]\n", i, name,
							i - offset_output);
		else
			pr_err("0x%-8x [Unable to match to a symbol.]\n", i);

		buffer += 4;

		pr_err("\nExecution Info:\n"
			"---------------\n");

		if (*buffer < ARRAY_SIZE(exec_ctxt)) {
			pr_err("Execution context \t%s\n",
				exec_ctxt[*buffer++]);
		} else {
			pr_err("Execution context corrupt\n");
			kfree(buffer_beg);
			return -EFAULT;
		}
		pr_err("Task Handle\t\t0x%x\n", *buffer++);
		pr_err("Stack Pointer\t\t0x%x\n", *buffer++);
		pr_err("Stack Top\t\t0x%x\n", *buffer++);
		pr_err("Stack Bottom\t\t0x%x\n", *buffer++);
		pr_err("Stack Size\t\t0x%x\n", *buffer++);
		pr_err("Stack Size In Use\t0x%x\n", *buffer++);

		pr_err("\nCPU Registers\n"
			"---------------\n");

		for (i = 0; i < 32; i++) {
			if (i == 4 || i == 6 || i == 8)
				pr_err("A%d 0x%-8x [Function Argument %d]\n",
							i, *buffer++, i-3);
			else if (i == 15)
				pr_err("A15 0x%-8x [Frame Pointer]\n",
								*buffer++);
			else
				pr_err("A%d 0x%x\n", i, *buffer++);
		}

		pr_err("\nB0 0x%x\n", *buffer++);
		pr_err("B1 0x%x\n", *buffer++);
		pr_err("B2 0x%x\n", *buffer++);

		if ((*buffer > dyn_ext_base) && (node_find_addr(node_mgr,
			*buffer, 0x1000, &offset_output, name) == 0))

			pr_err("B3 0x%-8x [Function Return Pointer:"
				" \"%s\" + 0x%x]\n", *buffer, name,
				*buffer - offset_output);
		else
			pr_err("B3 0x%-8x [Function Return Pointer:"
				"Unable to match to a symbol.]\n", *buffer);

		buffer++;

		for (i = 4; i < 32; i++) {
			if (i == 4 || i == 6 || i == 8)
				pr_err("B%d 0x%-8x [Function Argument %d]\n",
							i, *buffer++, i-2);
			else if (i == 14)
				pr_err("B14 0x%-8x [Data Page Pointer]\n",
								*buffer++);
			else
				pr_err("B%d 0x%x\n", i, *buffer++);
		}

		pr_err("\n");

		for (i = 0; i < ARRAY_SIZE(dsp_regs); i++)
			pr_err("%s 0x%x\n", dsp_regs[i], *buffer++);

		pr_err("\nStack:\n"
			"------\n");

		for (i = 0; buffer < buffer_end; i++, buffer++) {
			if ((*buffer > dyn_ext_base) && (
				node_find_addr(node_mgr, *buffer , 0x600,
				&offset_output, name) == 0))
				pr_err("[%d] 0x%-8x [\"%s\" + 0x%x]\n",
					i, *buffer, name,
					*buffer - offset_output);
			else
				pr_err("[%d] 0x%x\n", i, *buffer);
		}
		kfree(buffer_beg);
	}
func_end:
	return status;
}

/**
 * dump_dl_modules() - This functions dumps the _DLModules loaded in DSP side
 * @bridge_context:		Bridge driver's device context pointer.
 *
 */
void dump_dl_modules(struct bridge_dev_context *bridge_context)
{
	struct cod_manager *code_mgr;
	struct bridge_drv_interface *intf_fxns;
	struct bridge_dev_context *bridge_ctxt = bridge_context;
	struct dev_object *dev_object = bridge_ctxt->dev_obj;
	struct modules_header modules_hdr;
	struct dll_module *module_struct = NULL;
	u32 module_dsp_addr;
	u32 module_size;
	u32 module_struct_size = 0;
	u32 sect_ndx;
	char *sect_str ;
	int status = 0;

	status = dev_get_intf_fxns(dev_object, &intf_fxns);
	if (status) {
		pr_debug("%s: Failed on dev_get_intf_fxns.\n", __func__);
		goto func_end;
	}

	status = dev_get_cod_mgr(dev_object, &code_mgr);
	if (!code_mgr) {
		pr_debug("%s: Failed on dev_get_cod_mgr.\n", __func__);
		status = -EFAULT;
		goto func_end;
	}

	/* Lookup  the address of the modules_header structure */
	status = cod_get_sym_value(code_mgr, "_DLModules", &module_dsp_addr);
	if (status) {
		pr_debug("%s: Failed on cod_get_sym_value for _DLModules.\n",
			__func__);
		goto func_end;
	}

	pr_debug("%s: _DLModules at 0x%x\n", __func__, module_dsp_addr);

	/* Copy the modules_header structure from DSP memory. */
	status = (*intf_fxns->brd_read)(bridge_context, (u8 *) &modules_hdr,
				(u32) module_dsp_addr, sizeof(modules_hdr), 0);

	if (status) {
		pr_debug("%s: Failed failed to read modules header.\n",
								__func__);
		goto func_end;
	}

	module_dsp_addr = modules_hdr.first_module;
	module_size = modules_hdr.first_module_size;

	pr_debug("%s: dll_module_header 0x%x %d\n", __func__, module_dsp_addr,
								module_size);

	pr_err("\nDynamically Loaded Modules:\n"
		"---------------------------\n");

	/* For each dll_module structure in the list... */
	while (module_size) {
		/*
		 * Allocate/re-allocate memory to hold the dll_module
		 * structure. The memory is re-allocated only if the existing
		 * allocation is too small.
		 */
		if (module_size > module_struct_size) {
			kfree(module_struct);
			module_struct = kzalloc(module_size+128, GFP_ATOMIC);
			module_struct_size = module_size+128;
			pr_debug("%s: allocated module struct %p %d\n",
				__func__, module_struct, module_struct_size);
			if (!module_struct)
				goto func_end;
		}
		/* Copy the dll_module structure from DSP memory */
		status = (*intf_fxns->brd_read)(bridge_context,
			(u8 *)module_struct, module_dsp_addr, module_size, 0);

		if (status) {
			pr_debug(
			"%s: Failed to read dll_module stuct for 0x%x.\n",
			__func__, module_dsp_addr);
			break;
		}

		/* Update info regarding the _next_ module in the list. */
		module_dsp_addr = module_struct->next_module;
		module_size = module_struct->next_module_size;

		pr_debug("%s: next module 0x%x %d, this module num sects %d\n",
			__func__, module_dsp_addr, module_size,
			module_struct->num_sects);

		/*
		 * The section name strings start immedialty following
		 * the array of dll_sect structures.
		 */
		sect_str = (char *) &module_struct->
					sects[module_struct->num_sects];
		pr_err("%s\n", sect_str);

		/*
		 * Advance to the first section name string.
		 * Each string follows the one before.
		 */
		sect_str += strlen(sect_str) + 1;

		/* Access each dll_sect structure and its name string. */
		for (sect_ndx = 0;
			sect_ndx < module_struct->num_sects; sect_ndx++) {
			pr_err("    Section: 0x%x ",
				module_struct->sects[sect_ndx].sect_load_adr);

			if (((u32) sect_str - (u32) module_struct) <
				module_struct_size) {
				pr_err("%s\n", sect_str);
				/* Each string follows the one before. */
				sect_str += strlen(sect_str)+1;
			} else {
				pr_err("<string error>\n");
				pr_debug("%s: section name sting address "
					"is invalid %p\n", __func__, sect_str);
			}
		}
	}
func_end:
	kfree(module_struct);
}
#endif

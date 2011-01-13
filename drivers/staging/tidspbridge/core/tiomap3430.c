/*
 * tiomap.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Processor Manager Driver for TI OMAP3430 EVM.
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

#include <plat/dsp.h>

#include <linux/types.h>
/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/drv.h>
#include <dspbridge/sync.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdefs.h>
#include <dspbridge/dspchnl.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/dspio.h>
#include <dspbridge/dspmsg.h>
#include <dspbridge/pwr.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/dspapi.h>
#include <dspbridge/dmm.h>
#include <dspbridge/wdt.h>

/*  ----------------------------------- Local */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "tiomap_io.h"

/* Offset in shared mem to write to in order to synchronize start with DSP */
#define SHMSYNCOFFSET 4		/* GPP byte offset */

#define BUFFERSIZE 1024

#define TIHELEN_ACKTIMEOUT  10000

#define MMU_SECTION_ADDR_MASK    0xFFF00000
#define MMU_SSECTION_ADDR_MASK   0xFF000000
#define MMU_LARGE_PAGE_MASK      0xFFFF0000
#define MMU_SMALL_PAGE_MASK      0xFFFFF000
#define OMAP3_IVA2_BOOTADDR_MASK 0xFFFFFC00
#define PAGES_II_LVL_TABLE   512
#define PHYS_TO_PAGE(phys)      pfn_to_page((phys) >> PAGE_SHIFT)

/*
 * This is a totally ugly layer violation, but needed until
 * omap_ctrl_set_dsp_boot*() are provided.
 */
#define OMAP3_IVA2_BOOTMOD_IDLE 1
#define OMAP2_CONTROL_GENERAL 0x270
#define OMAP343X_CONTROL_IVA2_BOOTADDR (OMAP2_CONTROL_GENERAL + 0x0190)
#define OMAP343X_CONTROL_IVA2_BOOTMOD (OMAP2_CONTROL_GENERAL + 0x0194)

#define OMAP343X_CTRL_REGADDR(reg) \
	OMAP2_L4_IO_ADDRESS(OMAP343X_CTRL_BASE + (reg))


/* Forward Declarations: */
static int bridge_brd_monitor(struct bridge_dev_context *dev_ctxt);
static int bridge_brd_read(struct bridge_dev_context *dev_ctxt,
				  u8 *host_buff,
				  u32 dsp_addr, u32 ul_num_bytes,
				  u32 mem_type);
static int bridge_brd_start(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_addr);
static int bridge_brd_status(struct bridge_dev_context *dev_ctxt,
				    int *board_state);
static int bridge_brd_stop(struct bridge_dev_context *dev_ctxt);
static int bridge_brd_write(struct bridge_dev_context *dev_ctxt,
				   u8 *host_buff,
				   u32 dsp_addr, u32 ul_num_bytes,
				   u32 mem_type);
static int bridge_brd_set_state(struct bridge_dev_context *dev_ctxt,
				    u32 brd_state);
static int bridge_brd_mem_copy(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_dest_addr, u32 dsp_src_addr,
				   u32 ul_num_bytes, u32 mem_type);
static int bridge_brd_mem_write(struct bridge_dev_context *dev_ctxt,
				    u8 *host_buff, u32 dsp_addr,
				    u32 ul_num_bytes, u32 mem_type);
static int bridge_brd_mem_map(struct bridge_dev_context *dev_ctxt,
				  u32 ul_mpu_addr, u32 virt_addr,
				  u32 ul_num_bytes, u32 ul_map_attr,
				  struct page **mapped_pages);
static int bridge_brd_mem_un_map(struct bridge_dev_context *dev_ctxt,
				     u32 virt_addr, u32 ul_num_bytes);
static int bridge_dev_create(struct bridge_dev_context
					**dev_cntxt,
					struct dev_object *hdev_obj,
					struct cfg_hostres *config_param);
static int bridge_dev_ctrl(struct bridge_dev_context *dev_context,
				  u32 dw_cmd, void *pargs);
static int bridge_dev_destroy(struct bridge_dev_context *dev_ctxt);
static u32 user_va2_pa(struct mm_struct *mm, u32 address);
static int pte_update(struct bridge_dev_context *dev_ctxt, u32 pa,
			     u32 va, u32 size,
			     struct hw_mmu_map_attrs_t *map_attrs);
static int pte_set(struct pg_table_attrs *pt, u32 pa, u32 va,
			  u32 size, struct hw_mmu_map_attrs_t *attrs);
static int mem_map_vmalloc(struct bridge_dev_context *dev_context,
				  u32 ul_mpu_addr, u32 virt_addr,
				  u32 ul_num_bytes,
				  struct hw_mmu_map_attrs_t *hw_attrs);

bool wait_for_start(struct bridge_dev_context *dev_context, u32 dw_sync_addr);

/*  ----------------------------------- Globals */

/* Attributes of L2 page tables for DSP MMU */
struct page_info {
	u32 num_entries;	/* Number of valid PTEs in the L2 PT */
};

/* Attributes used to manage the DSP MMU page tables */
struct pg_table_attrs {
	spinlock_t pg_lock;	/* Critical section object handle */

	u32 l1_base_pa;		/* Physical address of the L1 PT */
	u32 l1_base_va;		/* Virtual  address of the L1 PT */
	u32 l1_size;		/* Size of the L1 PT */
	u32 l1_tbl_alloc_pa;
	/* Physical address of Allocated mem for L1 table. May not be aligned */
	u32 l1_tbl_alloc_va;
	/* Virtual address of Allocated mem for L1 table. May not be aligned */
	u32 l1_tbl_alloc_sz;
	/* Size of consistent memory allocated for L1 table.
	 * May not be aligned */

	u32 l2_base_pa;		/* Physical address of the L2 PT */
	u32 l2_base_va;		/* Virtual  address of the L2 PT */
	u32 l2_size;		/* Size of the L2 PT */
	u32 l2_tbl_alloc_pa;
	/* Physical address of Allocated mem for L2 table. May not be aligned */
	u32 l2_tbl_alloc_va;
	/* Virtual address of Allocated mem for L2 table. May not be aligned */
	u32 l2_tbl_alloc_sz;
	/* Size of consistent memory allocated for L2 table.
	 * May not be aligned */

	u32 l2_num_pages;	/* Number of allocated L2 PT */
	/* Array [l2_num_pages] of L2 PT info structs */
	struct page_info *pg_info;
};

/*
 *  This Bridge driver's function interface table.
 */
static struct bridge_drv_interface drv_interface_fxns = {
	/* Bridge API ver. for which this bridge driver is built. */
	BRD_API_MAJOR_VERSION,
	BRD_API_MINOR_VERSION,
	bridge_dev_create,
	bridge_dev_destroy,
	bridge_dev_ctrl,
	bridge_brd_monitor,
	bridge_brd_start,
	bridge_brd_stop,
	bridge_brd_status,
	bridge_brd_read,
	bridge_brd_write,
	bridge_brd_set_state,
	bridge_brd_mem_copy,
	bridge_brd_mem_write,
	bridge_brd_mem_map,
	bridge_brd_mem_un_map,
	/* The following CHNL functions are provided by chnl_io.lib: */
	bridge_chnl_create,
	bridge_chnl_destroy,
	bridge_chnl_open,
	bridge_chnl_close,
	bridge_chnl_add_io_req,
	bridge_chnl_get_ioc,
	bridge_chnl_cancel_io,
	bridge_chnl_flush_io,
	bridge_chnl_get_info,
	bridge_chnl_get_mgr_info,
	bridge_chnl_idle,
	bridge_chnl_register_notify,
	/* The following IO functions are provided by chnl_io.lib: */
	bridge_io_create,
	bridge_io_destroy,
	bridge_io_on_loaded,
	bridge_io_get_proc_load,
	/* The following msg_ctrl functions are provided by chnl_io.lib: */
	bridge_msg_create,
	bridge_msg_create_queue,
	bridge_msg_delete,
	bridge_msg_delete_queue,
	bridge_msg_get,
	bridge_msg_put,
	bridge_msg_register_notify,
	bridge_msg_set_queue_id,
};

static inline void flush_all(struct bridge_dev_context *dev_context)
{
	if (dev_context->dw_brd_state == BRD_DSP_HIBERNATION ||
	    dev_context->dw_brd_state == BRD_HIBERNATION)
		wake_dsp(dev_context, NULL);

	hw_mmu_tlb_flush_all(dev_context->dw_dsp_mmu_base);
}

static void bad_page_dump(u32 pa, struct page *pg)
{
	pr_emerg("DSPBRIDGE: MAP function: COUNT 0 FOR PA 0x%x\n", pa);
	pr_emerg("Bad page state in process '%s'\n"
		 "page:%p flags:0x%0*lx mapping:%p mapcount:%d count:%d\n"
		 "Backtrace:\n",
		 current->comm, pg, (int)(2 * sizeof(unsigned long)),
		 (unsigned long)pg->flags, pg->mapping,
		 page_mapcount(pg), page_count(pg));
	dump_stack();
}

/*
 *  ======== bridge_drv_entry ========
 *  purpose:
 *      Bridge Driver entry point.
 */
void bridge_drv_entry(struct bridge_drv_interface **drv_intf,
		   const char *driver_file_name)
{

	DBC_REQUIRE(driver_file_name != NULL);

	io_sm_init();		/* Initialization of io_sm module */

	if (strcmp(driver_file_name, "UMA") == 0)
		*drv_intf = &drv_interface_fxns;
	else
		dev_dbg(bridge, "%s Unknown Bridge file name", __func__);

}

/*
 *  ======== bridge_brd_monitor ========
 *  purpose:
 *      This bridge_brd_monitor puts DSP into a Loadable state.
 *      i.e Application can load and start the device.
 *
 *  Preconditions:
 *      Device in 'OFF' state.
 */
static int bridge_brd_monitor(struct bridge_dev_context *dev_ctxt)
{
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 temp;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	temp = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
					OMAP_POWERSTATEST_MASK;
	if (!(temp & 0x02)) {
		/* IVA2 is not in ON state */
		/* Read and set PM_PWSTCTRL_IVA2  to ON */
		(*pdata->dsp_prm_rmw_bits)(OMAP_POWERSTATEST_MASK,
			PWRDM_POWER_ON, OMAP3430_IVA2_MOD, OMAP2_PM_PWSTCTRL);
		/* Set the SW supervised state transition */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_FORCE_WAKEUP,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);

		/* Wait until the state has moved to ON */
		while ((*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
						OMAP_INTRANSITION_MASK)
			;
		/* Disable Automatic transition */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_DISABLE_AUTO,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);
	}
	(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
	dsp_clk_enable(DSP_CLK_IVA2);

	/* set the device state to IDLE */
	dev_context->dw_brd_state = BRD_IDLE;

	return 0;
}

/*
 *  ======== bridge_brd_read ========
 *  purpose:
 *      Reads buffers for DSP memory.
 */
static int bridge_brd_read(struct bridge_dev_context *dev_ctxt,
				  u8 *host_buff, u32 dsp_addr,
				  u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 offset;
	u32 dsp_base_addr = dev_ctxt->dw_dsp_base_addr;

	if (dsp_addr < dev_context->dw_dsp_start_add) {
		status = -EPERM;
		return status;
	}
	/* change here to account for the 3 bands of the DSP internal memory */
	if ((dsp_addr - dev_context->dw_dsp_start_add) <
	    dev_context->dw_internal_size) {
		offset = dsp_addr - dev_context->dw_dsp_start_add;
	} else {
		status = read_ext_dsp_data(dev_context, host_buff, dsp_addr,
					   ul_num_bytes, mem_type);
		return status;
	}
	/* copy the data from  DSP memory, */
	memcpy(host_buff, (void *)(dsp_base_addr + offset), ul_num_bytes);
	return status;
}

/*
 *  ======== bridge_brd_set_state ========
 *  purpose:
 *      This routine updates the Board status.
 */
static int bridge_brd_set_state(struct bridge_dev_context *dev_ctxt,
				    u32 brd_state)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;

	dev_context->dw_brd_state = brd_state;
	return status;
}

/*
 *  ======== bridge_brd_start ========
 *  purpose:
 *      Initializes DSP MMU and Starts DSP.
 *
 *  Preconditions:
 *  a) DSP domain is 'ACTIVE'.
 *  b) DSP_RST1 is asserted.
 *  b) DSP_RST2 is released.
 */
static int bridge_brd_start(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_addr)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 dw_sync_addr = 0;
	u32 ul_shm_base;	/* Gpp Phys SM base addr(byte) */
	u32 ul_shm_base_virt;	/* Dsp Virt SM base addr */
	u32 ul_tlb_base_virt;	/* Base of MMU TLB entry */
	/* Offset of shm_base_virt from tlb_base_virt */
	u32 ul_shm_offset_virt;
	s32 entry_ndx;
	s32 itmp_entry_ndx = 0;	/* DSP-MMU TLB entry base address */
	struct cfg_hostres *resources = NULL;
	u32 temp;
	u32 ul_dsp_clk_rate;
	u32 ul_dsp_clk_addr;
	u32 ul_bios_gp_timer;
	u32 clk_cmd;
	struct io_mgr *hio_mgr;
	u32 ul_load_monitor_timer;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	/* The device context contains all the mmu setup info from when the
	 * last dsp base image was loaded. The first entry is always
	 * SHMMEM base. */
	/* Get SHM_BEG - convert to byte address */
	(void)dev_get_symbol(dev_context->hdev_obj, SHMBASENAME,
			     &ul_shm_base_virt);
	ul_shm_base_virt *= DSPWORDSIZE;
	DBC_ASSERT(ul_shm_base_virt != 0);
	/* DSP Virtual address */
	ul_tlb_base_virt = dev_context->atlb_entry[0].ul_dsp_va;
	DBC_ASSERT(ul_tlb_base_virt <= ul_shm_base_virt);
	ul_shm_offset_virt =
	    ul_shm_base_virt - (ul_tlb_base_virt * DSPWORDSIZE);
	/* Kernel logical address */
	ul_shm_base = dev_context->atlb_entry[0].ul_gpp_va + ul_shm_offset_virt;

	DBC_ASSERT(ul_shm_base != 0);
	/* 2nd wd is used as sync field */
	dw_sync_addr = ul_shm_base + SHMSYNCOFFSET;
	/* Write a signature into the shm base + offset; this will
	 * get cleared when the DSP program starts. */
	if ((ul_shm_base_virt == 0) || (ul_shm_base == 0)) {
		pr_err("%s: Illegal SM base\n", __func__);
		status = -EPERM;
	} else
		__raw_writel(0xffffffff, dw_sync_addr);

	if (!status) {
		resources = dev_context->resources;
		if (!resources)
			status = -EPERM;

		/* Assert RST1 i.e only the RST only for DSP megacell */
		if (!status) {
			(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK,
					OMAP3430_RST1_IVA2_MASK, OMAP3430_IVA2_MOD,
					OMAP2_RM_RSTCTRL);
			/* Mask address with 1K for compatibility */
			__raw_writel(dsp_addr & OMAP3_IVA2_BOOTADDR_MASK,
					OMAP343X_CTRL_REGADDR(
					OMAP343X_CONTROL_IVA2_BOOTADDR));
			/*
			 * Set bootmode to self loop if dsp_debug flag is true
			 */
			__raw_writel((dsp_debug) ? OMAP3_IVA2_BOOTMOD_IDLE : 0,
					OMAP343X_CTRL_REGADDR(
					OMAP343X_CONTROL_IVA2_BOOTMOD));
		}
	}
	if (!status) {
		/* Reset and Unreset the RST2, so that BOOTADDR is copied to
		 * IVA2 SYSC register */
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK,
			OMAP3430_RST2_IVA2_MASK, OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
		udelay(100);
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
		udelay(100);

		/* Disbale the DSP MMU */
		hw_mmu_disable(resources->dw_dmmu_base);
		/* Disable TWL */
		hw_mmu_twl_disable(resources->dw_dmmu_base);

		/* Only make TLB entry if both addresses are non-zero */
		for (entry_ndx = 0; entry_ndx < BRDIOCTL_NUMOFMMUTLB;
		     entry_ndx++) {
			struct bridge_ioctl_extproc *e = &dev_context->atlb_entry[entry_ndx];
			struct hw_mmu_map_attrs_t map_attrs = {
				.endianism = e->endianism,
				.element_size = e->elem_size,
				.mixed_size = e->mixed_mode,
			};

			if (!e->ul_gpp_pa || !e->ul_dsp_va)
				continue;

			dev_dbg(bridge,
					"MMU %d, pa: 0x%x, va: 0x%x, size: 0x%x",
					itmp_entry_ndx,
					e->ul_gpp_pa,
					e->ul_dsp_va,
					e->ul_size);

			hw_mmu_tlb_add(dev_context->dw_dsp_mmu_base,
					e->ul_gpp_pa,
					e->ul_dsp_va,
					e->ul_size,
					itmp_entry_ndx,
					&map_attrs, 1, 1);

			itmp_entry_ndx++;
		}
	}

	/* Lock the above TLB entries and get the BIOS and load monitor timer
	 * information */
	if (!status) {
		hw_mmu_num_locked_set(resources->dw_dmmu_base, itmp_entry_ndx);
		hw_mmu_victim_num_set(resources->dw_dmmu_base, itmp_entry_ndx);
		hw_mmu_ttb_set(resources->dw_dmmu_base,
			       dev_context->pt_attrs->l1_base_pa);
		hw_mmu_twl_enable(resources->dw_dmmu_base);
		/* Enable the SmartIdle and AutoIdle bit for MMU_SYSCONFIG */

		temp = __raw_readl((resources->dw_dmmu_base) + 0x10);
		temp = (temp & 0xFFFFFFEF) | 0x11;
		__raw_writel(temp, (resources->dw_dmmu_base) + 0x10);

		/* Let the DSP MMU run */
		hw_mmu_enable(resources->dw_dmmu_base);

		/* Enable the BIOS clock */
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_BIOSGPTIMER, &ul_bios_gp_timer);
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_LOADMON_GPTIMER,
				     &ul_load_monitor_timer);
	}

	if (!status) {
		if (ul_load_monitor_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_load_monitor_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge, "Not able to get the symbol for Load "
				"Monitor Timer\n");
		}
	}

	if (!status) {
		if (ul_bios_gp_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_bios_gp_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge,
				"Not able to get the symbol for BIOS Timer\n");
		}
	}

	if (!status) {
		/* Set the DSP clock rate */
		(void)dev_get_symbol(dev_context->hdev_obj,
				     "_BRIDGEINIT_DSP_FREQ", &ul_dsp_clk_addr);
		/*Set Autoidle Mode for IVA2 PLL */
		(*pdata->dsp_cm_write)(1 << OMAP3430_AUTO_IVA2_DPLL_SHIFT,
				OMAP3430_IVA2_MOD, OMAP3430_CM_AUTOIDLE_PLL);

		if ((unsigned int *)ul_dsp_clk_addr != NULL) {
			/* Get the clock rate */
			ul_dsp_clk_rate = dsp_clk_get_iva2_rate();
			dev_dbg(bridge, "%s: DSP clock rate (KHZ): 0x%x \n",
				__func__, ul_dsp_clk_rate);
			(void)bridge_brd_write(dev_context,
					       (u8 *) &ul_dsp_clk_rate,
					       ul_dsp_clk_addr, sizeof(u32), 0);
		}
		/*
		 * Enable Mailbox events and also drain any pending
		 * stale messages.
		 */
		dev_context->mbox = omap_mbox_get("dsp");
		if (IS_ERR(dev_context->mbox)) {
			dev_context->mbox = NULL;
			pr_err("%s: Failed to get dsp mailbox handle\n",
								__func__);
			status = -EPERM;
		}

	}
	if (!status) {
		dev_context->mbox->rxq->callback = (int (*)(void *))io_mbox_msg;

/*PM_IVA2GRPSEL_PER = 0xC0;*/
		temp = readl(resources->dw_per_pm_base + 0xA8);
		temp = (temp & 0xFFFFFF30) | 0xC0;
		writel(temp, resources->dw_per_pm_base + 0xA8);

/*PM_MPUGRPSEL_PER &= 0xFFFFFF3F; */
		temp = readl(resources->dw_per_pm_base + 0xA4);
		temp = (temp & 0xFFFFFF3F);
		writel(temp, resources->dw_per_pm_base + 0xA4);
/*CM_SLEEPDEP_PER |= 0x04; */
		temp = readl(resources->dw_per_base + 0x44);
		temp = (temp & 0xFFFFFFFB) | 0x04;
		writel(temp, resources->dw_per_base + 0x44);

/*CM_CLKSTCTRL_IVA2 = 0x00000003 -To Allow automatic transitions */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_ENABLE_AUTO,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);

		/* Let DSP go */
		dev_dbg(bridge, "%s Unreset\n", __func__);
		/* Enable DSP MMU Interrupts */
		hw_mmu_event_enable(resources->dw_dmmu_base,
				    HW_MMU_ALL_INTERRUPTS);
		/* release the RST1, DSP starts executing now .. */
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

		dev_dbg(bridge, "Waiting for Sync @ 0x%x\n", dw_sync_addr);
		dev_dbg(bridge, "DSP c_int00 Address =  0x%x\n", dsp_addr);
		if (dsp_debug)
			while (__raw_readw(dw_sync_addr))
				;

		/* Wait for DSP to clear word in shared memory */
		/* Read the Location */
		if (!wait_for_start(dev_context, dw_sync_addr))
			status = -ETIMEDOUT;

		/* Start wdt */
		dsp_wdt_sm_set((void *)ul_shm_base);
		dsp_wdt_enable(true);

		status = dev_get_io_mgr(dev_context->hdev_obj, &hio_mgr);
		if (hio_mgr) {
			io_sh_msetting(hio_mgr, SHM_OPPINFO, NULL);
			/* Write the synchronization bit to indicate the
			 * completion of OPP table update to DSP
			 */
			__raw_writel(0XCAFECAFE, dw_sync_addr);

			/* update board state */
			dev_context->dw_brd_state = BRD_RUNNING;
			/* (void)chnlsm_enable_interrupt(dev_context); */
		} else {
			dev_context->dw_brd_state = BRD_UNKNOWN;
		}
	}
	return status;
}

/*
 *  ======== bridge_brd_stop ========
 *  purpose:
 *      Puts DSP in self loop.
 *
 *  Preconditions :
 *  a) None
 */
static int bridge_brd_stop(struct bridge_dev_context *dev_ctxt)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	struct pg_table_attrs *pt_attrs;
	u32 dsp_pwr_state;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	if (dev_context->dw_brd_state == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive the IVA2 to 'Standby' mode,
	 * before turning off the clocks.. This is to ensure that there are no
	 * pending L3 or other transactons from IVA2 */
	dsp_pwr_state = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
					OMAP_POWERSTATEST_MASK;
	if (dsp_pwr_state != PWRDM_POWER_OFF) {
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
		sm_interrupt_dsp(dev_context, MBX_PM_DSPIDLE);
		mdelay(10);

		/* IVA2 is not in OFF state */
		/* Set PM_PWSTCTRL_IVA2  to OFF */
		(*pdata->dsp_prm_rmw_bits)(OMAP_POWERSTATEST_MASK,
			PWRDM_POWER_OFF, OMAP3430_IVA2_MOD, OMAP2_PM_PWSTCTRL);
		/* Set the SW supervised state transition for Sleep */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_FORCE_SLEEP,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);
	}
	udelay(10);
	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (dev_context->dw_dsp_ext_base_addr)
		dev_context->dw_dsp_ext_base_addr = 0;

	dev_context->dw_brd_state = BRD_STOPPED;	/* update board state */

	dsp_wdt_enable(false);

	/* This is a good place to clear the MMU page tables as well */
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
		memset((u8 *) pt_attrs->l1_base_va, 0x00, pt_attrs->l1_size);
		memset((u8 *) pt_attrs->l2_base_va, 0x00, pt_attrs->l2_size);
		memset((u8 *) pt_attrs->pg_info, 0x00,
		       (pt_attrs->l2_num_pages * sizeof(struct page_info)));
	}
	/* Disable the mailbox interrupts */
	if (dev_context->mbox) {
		omap_mbox_disable_irq(dev_context->mbox, IRQ_RX);
		omap_mbox_put(dev_context->mbox);
		dev_context->mbox = NULL;
	}
	/* Reset IVA2 clocks*/
	(*pdata->dsp_prm_write)(OMAP3430_RST1_IVA2_MASK | OMAP3430_RST2_IVA2_MASK |
			OMAP3430_RST3_IVA2_MASK, OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

	dsp_clock_disable_all(dev_context->dsp_per_clks);
	dsp_clk_disable(DSP_CLK_IVA2);

	return status;
}

/*
 *  ======== bridge_brd_status ========
 *      Returns the board status.
 */
static int bridge_brd_status(struct bridge_dev_context *dev_ctxt,
				    int *board_state)
{
	struct bridge_dev_context *dev_context = dev_ctxt;
	*board_state = dev_context->dw_brd_state;
	return 0;
}

/*
 *  ======== bridge_brd_write ========
 *      Copies the buffers to DSP internal or external memory.
 */
static int bridge_brd_write(struct bridge_dev_context *dev_ctxt,
				   u8 *host_buff, u32 dsp_addr,
				   u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;

	if (dsp_addr < dev_context->dw_dsp_start_add) {
		status = -EPERM;
		return status;
	}
	if ((dsp_addr - dev_context->dw_dsp_start_add) <
	    dev_context->dw_internal_size) {
		status = write_dsp_data(dev_ctxt, host_buff, dsp_addr,
					ul_num_bytes, mem_type);
	} else {
		status = write_ext_dsp_data(dev_context, host_buff, dsp_addr,
					    ul_num_bytes, mem_type, false);
	}

	return status;
}

/*
 *  ======== bridge_dev_create ========
 *      Creates a driver object. Puts DSP in self loop.
 */
static int bridge_dev_create(struct bridge_dev_context
					**dev_cntxt,
					struct dev_object *hdev_obj,
					struct cfg_hostres *config_param)
{
	int status = 0;
	struct bridge_dev_context *dev_context = NULL;
	s32 entry_ndx;
	struct cfg_hostres *resources = config_param;
	struct pg_table_attrs *pt_attrs;
	u32 pg_tbl_pa;
	u32 pg_tbl_va;
	u32 align_size;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	/* Allocate and initialize a data structure to contain the bridge driver
	 *  state, which becomes the context for later calls into this driver */
	dev_context = kzalloc(sizeof(struct bridge_dev_context), GFP_KERNEL);
	if (!dev_context) {
		status = -ENOMEM;
		goto func_end;
	}

	dev_context->dw_dsp_start_add = (u32) OMAP_GEM_BASE;
	dev_context->dw_self_loop = (u32) NULL;
	dev_context->dsp_per_clks = 0;
	dev_context->dw_internal_size = OMAP_DSP_SIZE;
	/*  Clear dev context MMU table entries.
	 *  These get set on bridge_io_on_loaded() call after program loaded. */
	for (entry_ndx = 0; entry_ndx < BRDIOCTL_NUMOFMMUTLB; entry_ndx++) {
		dev_context->atlb_entry[entry_ndx].ul_gpp_pa =
		    dev_context->atlb_entry[entry_ndx].ul_dsp_va = 0;
	}
	dev_context->dw_dsp_base_addr = (u32) MEM_LINEAR_ADDRESS((void *)
								 (config_param->
								  dw_mem_base
								  [3]),
								 config_param->
								 dw_mem_length
								 [3]);
	if (!dev_context->dw_dsp_base_addr)
		status = -EPERM;

	pt_attrs = kzalloc(sizeof(struct pg_table_attrs), GFP_KERNEL);
	if (pt_attrs != NULL) {
		/* Assuming that we use only DSP's memory map
		 * until 0x4000:0000 , we would need only 1024
		 * L1 enties i.e L1 size = 4K */
		pt_attrs->l1_size = 0x1000;
		align_size = pt_attrs->l1_size;
		/* Align sizes are expected to be power of 2 */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32) mem_alloc_phys_mem(pt_attrs->l1_size,
						     align_size, &pg_tbl_pa);

		/* Check if the PA is aligned for us */
		if ((pg_tbl_pa) & (align_size - 1)) {
			/* PA not aligned to page table size ,
			 * try with more allocation and align */
			mem_free_phys_mem((void *)pg_tbl_va, pg_tbl_pa,
					  pt_attrs->l1_size);
			/* we like to get aligned on L1 table size */
			pg_tbl_va =
			    (u32) mem_alloc_phys_mem((pt_attrs->l1_size) * 2,
						     align_size, &pg_tbl_pa);
			/* We should be able to get aligned table now */
			pt_attrs->l1_tbl_alloc_pa = pg_tbl_pa;
			pt_attrs->l1_tbl_alloc_va = pg_tbl_va;
			pt_attrs->l1_tbl_alloc_sz = pt_attrs->l1_size * 2;
			/* Align the PA to the next 'align'  boundary */
			pt_attrs->l1_base_pa =
			    ((pg_tbl_pa) +
			     (align_size - 1)) & (~(align_size - 1));
			pt_attrs->l1_base_va =
			    pg_tbl_va + (pt_attrs->l1_base_pa - pg_tbl_pa);
		} else {
			/* We got aligned PA, cool */
			pt_attrs->l1_tbl_alloc_pa = pg_tbl_pa;
			pt_attrs->l1_tbl_alloc_va = pg_tbl_va;
			pt_attrs->l1_tbl_alloc_sz = pt_attrs->l1_size;
			pt_attrs->l1_base_pa = pg_tbl_pa;
			pt_attrs->l1_base_va = pg_tbl_va;
		}
		if (pt_attrs->l1_base_va)
			memset((u8 *) pt_attrs->l1_base_va, 0x00,
			       pt_attrs->l1_size);

		/* number of L2 page tables = DMM pool used + SHMMEM +EXTMEM +
		 * L4 pages */
		pt_attrs->l2_num_pages = ((DMMPOOLSIZE >> 20) + 6);
		pt_attrs->l2_size = HW_MMU_COARSE_PAGE_SIZE *
		    pt_attrs->l2_num_pages;
		align_size = 4;	/* Make it u32 aligned */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32) mem_alloc_phys_mem(pt_attrs->l2_size,
						     align_size, &pg_tbl_pa);
		pt_attrs->l2_tbl_alloc_pa = pg_tbl_pa;
		pt_attrs->l2_tbl_alloc_va = pg_tbl_va;
		pt_attrs->l2_tbl_alloc_sz = pt_attrs->l2_size;
		pt_attrs->l2_base_pa = pg_tbl_pa;
		pt_attrs->l2_base_va = pg_tbl_va;

		if (pt_attrs->l2_base_va)
			memset((u8 *) pt_attrs->l2_base_va, 0x00,
			       pt_attrs->l2_size);

		pt_attrs->pg_info = kzalloc(pt_attrs->l2_num_pages *
					sizeof(struct page_info), GFP_KERNEL);
		dev_dbg(bridge,
			"L1 pa %x, va %x, size %x\n L2 pa %x, va "
			"%x, size %x\n", pt_attrs->l1_base_pa,
			pt_attrs->l1_base_va, pt_attrs->l1_size,
			pt_attrs->l2_base_pa, pt_attrs->l2_base_va,
			pt_attrs->l2_size);
		dev_dbg(bridge, "pt_attrs %p L2 NumPages %x pg_info %p\n",
			pt_attrs, pt_attrs->l2_num_pages, pt_attrs->pg_info);
	}
	if ((pt_attrs != NULL) && (pt_attrs->l1_base_va != 0) &&
	    (pt_attrs->l2_base_va != 0) && (pt_attrs->pg_info != NULL))
		dev_context->pt_attrs = pt_attrs;
	else
		status = -ENOMEM;

	if (!status) {
		spin_lock_init(&pt_attrs->pg_lock);
		dev_context->tc_word_swap_on = drv_datap->tc_wordswapon;

		/* Set the Clock Divisor for the DSP module */
		udelay(5);
		/* MMU address is obtained from the host
		 * resources struct */
		dev_context->dw_dsp_mmu_base = resources->dw_dmmu_base;
	}
	if (!status) {
		dev_context->hdev_obj = hdev_obj;
		/* Store current board state. */
		dev_context->dw_brd_state = BRD_UNKNOWN;
		dev_context->resources = resources;
		dsp_clk_enable(DSP_CLK_IVA2);
		bridge_brd_stop(dev_context);
		/* Return ptr to our device state to the DSP API for storage */
		*dev_cntxt = dev_context;
	} else {
		if (pt_attrs != NULL) {
			kfree(pt_attrs->pg_info);

			if (pt_attrs->l2_tbl_alloc_va) {
				mem_free_phys_mem((void *)
						  pt_attrs->l2_tbl_alloc_va,
						  pt_attrs->l2_tbl_alloc_pa,
						  pt_attrs->l2_tbl_alloc_sz);
			}
			if (pt_attrs->l1_tbl_alloc_va) {
				mem_free_phys_mem((void *)
						  pt_attrs->l1_tbl_alloc_va,
						  pt_attrs->l1_tbl_alloc_pa,
						  pt_attrs->l1_tbl_alloc_sz);
			}
		}
		kfree(pt_attrs);
		kfree(dev_context);
	}
func_end:
	return status;
}

/*
 *  ======== bridge_dev_ctrl ========
 *      Receives device specific commands.
 */
static int bridge_dev_ctrl(struct bridge_dev_context *dev_context,
				  u32 dw_cmd, void *pargs)
{
	int status = 0;
	struct bridge_ioctl_extproc *pa_ext_proc =
					(struct bridge_ioctl_extproc *)pargs;
	s32 ndx;

	switch (dw_cmd) {
	case BRDIOCTL_CHNLREAD:
		break;
	case BRDIOCTL_CHNLWRITE:
		break;
	case BRDIOCTL_SETMMUCONFIG:
		/* store away dsp-mmu setup values for later use */
		for (ndx = 0; ndx < BRDIOCTL_NUMOFMMUTLB; ndx++, pa_ext_proc++)
			dev_context->atlb_entry[ndx] = *pa_ext_proc;
		break;
	case BRDIOCTL_DEEPSLEEP:
	case BRDIOCTL_EMERGENCYSLEEP:
		/* Currently only DSP Idle is supported Need to update for
		 * later releases */
		status = sleep_dsp(dev_context, PWR_DEEPSLEEP, pargs);
		break;
	case BRDIOCTL_WAKEUP:
		status = wake_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_CLK_CTRL:
		status = 0;
		/* Looking For Baseport Fix for Clocks */
		status = dsp_peripheral_clk_ctrl(dev_context, pargs);
		break;
	case BRDIOCTL_PWR_HIBERNATE:
		status = handle_hibernation_from_dsp(dev_context);
		break;
	case BRDIOCTL_PRESCALE_NOTIFY:
		status = pre_scale_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_POSTSCALE_NOTIFY:
		status = post_scale_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_CONSTRAINT_REQUEST:
		status = handle_constraints_set(dev_context, pargs);
		break;
	default:
		status = -EPERM;
		break;
	}
	return status;
}

/*
 *  ======== bridge_dev_destroy ========
 *      Destroys the driver object.
 */
static int bridge_dev_destroy(struct bridge_dev_context *dev_ctxt)
{
	struct pg_table_attrs *pt_attrs;
	int status = 0;
	struct bridge_dev_context *dev_context = (struct bridge_dev_context *)
	    dev_ctxt;
	struct cfg_hostres *host_res;
	u32 shm_size;
	struct drv_data *drv_datap = dev_get_drvdata(bridge);

	/* It should never happen */
	if (!dev_ctxt)
		return -EFAULT;

	/* first put the device to stop state */
	bridge_brd_stop(dev_context);
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
		kfree(pt_attrs->pg_info);

		if (pt_attrs->l2_tbl_alloc_va) {
			mem_free_phys_mem((void *)pt_attrs->l2_tbl_alloc_va,
					  pt_attrs->l2_tbl_alloc_pa,
					  pt_attrs->l2_tbl_alloc_sz);
		}
		if (pt_attrs->l1_tbl_alloc_va) {
			mem_free_phys_mem((void *)pt_attrs->l1_tbl_alloc_va,
					  pt_attrs->l1_tbl_alloc_pa,
					  pt_attrs->l1_tbl_alloc_sz);
		}
		kfree(pt_attrs);

	}

	if (dev_context->resources) {
		host_res = dev_context->resources;
		shm_size = drv_datap->shm_size;
		if (shm_size >= 0x10000) {
			if ((host_res->dw_mem_base[1]) &&
			    (host_res->dw_mem_phys[1])) {
				mem_free_phys_mem((void *)
						  host_res->dw_mem_base
						  [1],
						  host_res->dw_mem_phys
						  [1], shm_size);
			}
		} else {
			dev_dbg(bridge, "%s: Error getting shm size "
				"from registry: %x. Not calling "
				"mem_free_phys_mem\n", __func__,
				status);
		}
		host_res->dw_mem_base[1] = 0;
		host_res->dw_mem_phys[1] = 0;

		if (host_res->dw_mem_base[0])
			iounmap((void *)host_res->dw_mem_base[0]);
		if (host_res->dw_mem_base[2])
			iounmap((void *)host_res->dw_mem_base[2]);
		if (host_res->dw_mem_base[3])
			iounmap((void *)host_res->dw_mem_base[3]);
		if (host_res->dw_mem_base[4])
			iounmap((void *)host_res->dw_mem_base[4]);
		if (host_res->dw_dmmu_base)
			iounmap(host_res->dw_dmmu_base);
		if (host_res->dw_per_base)
			iounmap(host_res->dw_per_base);
		if (host_res->dw_per_pm_base)
			iounmap((void *)host_res->dw_per_pm_base);
		if (host_res->dw_core_pm_base)
			iounmap((void *)host_res->dw_core_pm_base);
		if (host_res->dw_sys_ctrl_base)
			iounmap(host_res->dw_sys_ctrl_base);

		host_res->dw_mem_base[0] = (u32) NULL;
		host_res->dw_mem_base[2] = (u32) NULL;
		host_res->dw_mem_base[3] = (u32) NULL;
		host_res->dw_mem_base[4] = (u32) NULL;
		host_res->dw_dmmu_base = NULL;
		host_res->dw_sys_ctrl_base = NULL;

		kfree(host_res);
	}

	/* Free the driver's device context: */
	kfree(drv_datap->base_img);
	kfree(drv_datap);
	dev_set_drvdata(bridge, NULL);
	kfree((void *)dev_ctxt);
	return status;
}

static int bridge_brd_mem_copy(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_dest_addr, u32 dsp_src_addr,
				   u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	u32 src_addr = dsp_src_addr;
	u32 dest_addr = dsp_dest_addr;
	u32 copy_bytes = 0;
	u32 total_bytes = ul_num_bytes;
	u8 host_buf[BUFFERSIZE];
	struct bridge_dev_context *dev_context = dev_ctxt;
	while (total_bytes > 0 && !status) {
		copy_bytes =
		    total_bytes > BUFFERSIZE ? BUFFERSIZE : total_bytes;
		/* Read from External memory */
		status = read_ext_dsp_data(dev_ctxt, host_buf, src_addr,
					   copy_bytes, mem_type);
		if (!status) {
			if (dest_addr < (dev_context->dw_dsp_start_add +
					 dev_context->dw_internal_size)) {
				/* Write to Internal memory */
				status = write_dsp_data(dev_ctxt, host_buf,
							dest_addr, copy_bytes,
							mem_type);
			} else {
				/* Write to External memory */
				status =
				    write_ext_dsp_data(dev_ctxt, host_buf,
						       dest_addr, copy_bytes,
						       mem_type, false);
			}
		}
		total_bytes -= copy_bytes;
		src_addr += copy_bytes;
		dest_addr += copy_bytes;
	}
	return status;
}

/* Mem Write does not halt the DSP to write unlike bridge_brd_write */
static int bridge_brd_mem_write(struct bridge_dev_context *dev_ctxt,
				    u8 *host_buff, u32 dsp_addr,
				    u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 ul_remain_bytes = 0;
	u32 ul_bytes = 0;
	ul_remain_bytes = ul_num_bytes;
	while (ul_remain_bytes > 0 && !status) {
		ul_bytes =
		    ul_remain_bytes > BUFFERSIZE ? BUFFERSIZE : ul_remain_bytes;
		if (dsp_addr < (dev_context->dw_dsp_start_add +
				 dev_context->dw_internal_size)) {
			status =
			    write_dsp_data(dev_ctxt, host_buff, dsp_addr,
					   ul_bytes, mem_type);
		} else {
			status = write_ext_dsp_data(dev_ctxt, host_buff,
						    dsp_addr, ul_bytes,
						    mem_type, true);
		}
		ul_remain_bytes -= ul_bytes;
		dsp_addr += ul_bytes;
		host_buff = host_buff + ul_bytes;
	}
	return status;
}

/*
 *  ======== bridge_brd_mem_map ========
 *      This function maps MPU buffer to the DSP address space. It performs
 *  linear to physical address translation if required. It translates each
 *  page since linear addresses can be physically non-contiguous
 *  All address & size arguments are assumed to be page aligned (in proc.c)
 *
 *  TODO: Disable MMU while updating the page tables (but that'll stall DSP)
 */
static int bridge_brd_mem_map(struct bridge_dev_context *dev_ctxt,
				  u32 ul_mpu_addr, u32 virt_addr,
				  u32 ul_num_bytes, u32 ul_map_attr,
				  struct page **mapped_pages)
{
	u32 attrs;
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	struct hw_mmu_map_attrs_t hw_attrs;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	u32 write = 0;
	u32 num_usr_pgs = 0;
	struct page *mapped_page, *pg;
	s32 pg_num;
	u32 va = virt_addr;
	struct task_struct *curr_task = current;
	u32 pg_i = 0;
	u32 mpu_addr, pa;

	dev_dbg(bridge,
		"%s hDevCtxt %p, pa %x, va %x, size %x, ul_map_attr %x\n",
		__func__, dev_ctxt, ul_mpu_addr, virt_addr, ul_num_bytes,
		ul_map_attr);
	if (ul_num_bytes == 0)
		return -EINVAL;

	if (ul_map_attr & DSP_MAP_DIR_MASK) {
		attrs = ul_map_attr;
	} else {
		/* Assign default attributes */
		attrs = ul_map_attr | (DSP_MAPVIRTUALADDR | DSP_MAPELEMSIZE16);
	}
	/* Take mapping properties */
	if (attrs & DSP_MAPBIGENDIAN)
		hw_attrs.endianism = HW_BIG_ENDIAN;
	else
		hw_attrs.endianism = HW_LITTLE_ENDIAN;

	hw_attrs.mixed_size = (enum hw_mmu_mixed_size_t)
	    ((attrs & DSP_MAPMIXEDELEMSIZE) >> 2);
	/* Ignore element_size if mixed_size is enabled */
	if (hw_attrs.mixed_size == 0) {
		if (attrs & DSP_MAPELEMSIZE8) {
			/* Size is 8 bit */
			hw_attrs.element_size = HW_ELEM_SIZE8BIT;
		} else if (attrs & DSP_MAPELEMSIZE16) {
			/* Size is 16 bit */
			hw_attrs.element_size = HW_ELEM_SIZE16BIT;
		} else if (attrs & DSP_MAPELEMSIZE32) {
			/* Size is 32 bit */
			hw_attrs.element_size = HW_ELEM_SIZE32BIT;
		} else if (attrs & DSP_MAPELEMSIZE64) {
			/* Size is 64 bit */
			hw_attrs.element_size = HW_ELEM_SIZE64BIT;
		} else {
			/*
			 * Mixedsize isn't enabled, so size can't be
			 * zero here
			 */
			return -EINVAL;
		}
	}
	if (attrs & DSP_MAPDONOTLOCK)
		hw_attrs.donotlockmpupage = 1;
	else
		hw_attrs.donotlockmpupage = 0;

	if (attrs & DSP_MAPVMALLOCADDR) {
		return mem_map_vmalloc(dev_ctxt, ul_mpu_addr, virt_addr,
				       ul_num_bytes, &hw_attrs);
	}
	/*
	 * Do OS-specific user-va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to pte_update.
	 */
	if ((attrs & DSP_MAPPHYSICALADDR)) {
		status = pte_update(dev_context, ul_mpu_addr, virt_addr,
				    ul_num_bytes, &hw_attrs);
		goto func_cont;
	}

	/*
	 * Important Note: ul_mpu_addr is mapped from user application process
	 * to current process - it must lie completely within the current
	 * virtual memory address space in order to be of use to us here!
	 */
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ul_mpu_addr);
	if (vma)
		dev_dbg(bridge,
			"VMAfor UserBuf: ul_mpu_addr=%x, ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", ul_mpu_addr,
			ul_num_bytes, vma->vm_start, vma->vm_end,
			vma->vm_flags);

	/*
	 * It is observed that under some circumstances, the user buffer is
	 * spread across several VMAs. So loop through and check if the entire
	 * user buffer is covered
	 */
	while ((vma) && (ul_mpu_addr + ul_num_bytes > vma->vm_end)) {
		/* jump to the next VMA region */
		vma = find_vma(mm, vma->vm_end + 1);
		dev_dbg(bridge,
			"VMA for UserBuf ul_mpu_addr=%x ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", ul_mpu_addr,
			ul_num_bytes, vma->vm_start, vma->vm_end,
			vma->vm_flags);
	}
	if (!vma) {
		pr_err("%s: Failed to get VMA region for 0x%x (%d)\n",
		       __func__, ul_mpu_addr, ul_num_bytes);
		status = -EINVAL;
		up_read(&mm->mmap_sem);
		goto func_cont;
	}

	if (vma->vm_flags & VM_IO) {
		num_usr_pgs = ul_num_bytes / PG_SIZE4K;
		mpu_addr = ul_mpu_addr;

		/* Get the physical addresses for user buffer */
		for (pg_i = 0; pg_i < num_usr_pgs; pg_i++) {
			pa = user_va2_pa(mm, mpu_addr);
			if (!pa) {
				status = -EPERM;
				pr_err("DSPBRIDGE: VM_IO mapping physical"
				       "address is invalid\n");
				break;
			}
			if (pfn_valid(__phys_to_pfn(pa))) {
				pg = PHYS_TO_PAGE(pa);
				get_page(pg);
				if (page_count(pg) < 1) {
					pr_err("Bad page in VM_IO buffer\n");
					bad_page_dump(pa, pg);
				}
			}
			status = pte_set(dev_context->pt_attrs, pa,
					 va, HW_PAGE_SIZE4KB, &hw_attrs);
			if (status)
				break;

			va += HW_PAGE_SIZE4KB;
			mpu_addr += HW_PAGE_SIZE4KB;
			pa += HW_PAGE_SIZE4KB;
		}
	} else {
		num_usr_pgs = ul_num_bytes / PG_SIZE4K;
		if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
			write = 1;

		for (pg_i = 0; pg_i < num_usr_pgs; pg_i++) {
			pg_num = get_user_pages(curr_task, mm, ul_mpu_addr, 1,
						write, 1, &mapped_page, NULL);
			if (pg_num > 0) {
				if (page_count(mapped_page) < 1) {
					pr_err("Bad page count after doing"
					       "get_user_pages on"
					       "user buffer\n");
					bad_page_dump(page_to_phys(mapped_page),
						      mapped_page);
				}
				status = pte_set(dev_context->pt_attrs,
						 page_to_phys(mapped_page), va,
						 HW_PAGE_SIZE4KB, &hw_attrs);
				if (status)
					break;

				if (mapped_pages)
					mapped_pages[pg_i] = mapped_page;

				va += HW_PAGE_SIZE4KB;
				ul_mpu_addr += HW_PAGE_SIZE4KB;
			} else {
				pr_err("DSPBRIDGE: get_user_pages FAILED,"
				       "MPU addr = 0x%x,"
				       "vma->vm_flags = 0x%lx,"
				       "get_user_pages Err"
				       "Value = %d, Buffer"
				       "size=0x%x\n", ul_mpu_addr,
				       vma->vm_flags, pg_num, ul_num_bytes);
				status = -EPERM;
				break;
			}
		}
	}
	up_read(&mm->mmap_sem);
func_cont:
	if (status) {
		/*
		 * Roll out the mapped pages incase it failed in middle of
		 * mapping
		 */
		if (pg_i) {
			bridge_brd_mem_un_map(dev_context, virt_addr,
					   (pg_i * PG_SIZE4K));
		}
		status = -EPERM;
	}
	/*
	 * In any case, flush the TLB
	 * This is called from here instead from pte_update to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region
	 */
	flush_all(dev_context);
	dev_dbg(bridge, "%s status %x\n", __func__, status);
	return status;
}

/*
 *  ======== bridge_brd_mem_un_map ========
 *      Invalidate the PTEs for the DSP VA block to be unmapped.
 *
 *      PTEs of a mapped memory block are contiguous in any page table
 *      So, instead of looking up the PTE address for every 4K block,
 *      we clear consecutive PTEs until we unmap all the bytes
 */
static int bridge_brd_mem_un_map(struct bridge_dev_context *dev_ctxt,
				     u32 virt_addr, u32 ul_num_bytes)
{
	u32 l1_base_va;
	u32 l2_base_va;
	u32 l2_base_pa;
	u32 l2_page_num;
	u32 pte_val;
	u32 pte_size;
	u32 pte_count;
	u32 pte_addr_l1;
	u32 pte_addr_l2 = 0;
	u32 rem_bytes;
	u32 rem_bytes_l2;
	u32 va_curr;
	struct page *pg = NULL;
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	struct pg_table_attrs *pt = dev_context->pt_attrs;
	u32 temp;
	u32 paddr;
	u32 numof4k_pages = 0;

	va_curr = virt_addr;
	rem_bytes = ul_num_bytes;
	rem_bytes_l2 = 0;
	l1_base_va = pt->l1_base_va;
	pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va_curr);
	dev_dbg(bridge, "%s dev_ctxt %p, va %x, NumBytes %x l1_base_va %x, "
		"pte_addr_l1 %x\n", __func__, dev_ctxt, virt_addr,
		ul_num_bytes, l1_base_va, pte_addr_l1);

	while (rem_bytes && !status) {
		u32 va_curr_orig = va_curr;
		/* Find whether the L1 PTE points to a valid L2 PT */
		pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va_curr);
		pte_val = *(u32 *) pte_addr_l1;
		pte_size = hw_mmu_pte_size_l1(pte_val);

		if (pte_size != HW_MMU_COARSE_PAGE_SIZE)
			goto skip_coarse_page;

		/*
		 * Get the L2 PA from the L1 PTE, and find
		 * corresponding L2 VA
		 */
		l2_base_pa = hw_mmu_pte_coarse_l1(pte_val);
		l2_base_va = l2_base_pa - pt->l2_base_pa + pt->l2_base_va;
		l2_page_num =
		    (l2_base_pa - pt->l2_base_pa) / HW_MMU_COARSE_PAGE_SIZE;
		/*
		 * Find the L2 PTE address from which we will start
		 * clearing, the number of PTEs to be cleared on this
		 * page, and the size of VA space that needs to be
		 * cleared on this L2 page
		 */
		pte_addr_l2 = hw_mmu_pte_addr_l2(l2_base_va, va_curr);
		pte_count = pte_addr_l2 & (HW_MMU_COARSE_PAGE_SIZE - 1);
		pte_count = (HW_MMU_COARSE_PAGE_SIZE - pte_count) / sizeof(u32);
		if (rem_bytes < (pte_count * PG_SIZE4K))
			pte_count = rem_bytes / PG_SIZE4K;
		rem_bytes_l2 = pte_count * PG_SIZE4K;

		/*
		 * Unmap the VA space on this L2 PT. A quicker way
		 * would be to clear pte_count entries starting from
		 * pte_addr_l2. However, below code checks that we don't
		 * clear invalid entries or less than 64KB for a 64KB
		 * entry. Similar checking is done for L1 PTEs too
		 * below
		 */
		while (rem_bytes_l2 && !status) {
			pte_val = *(u32 *) pte_addr_l2;
			pte_size = hw_mmu_pte_size_l2(pte_val);
			/* va_curr aligned to pte_size? */
			if (pte_size == 0 || rem_bytes_l2 < pte_size ||
			    va_curr & (pte_size - 1)) {
				status = -EPERM;
				break;
			}

			/* Collect Physical addresses from VA */
			paddr = (pte_val & ~(pte_size - 1));
			if (pte_size == HW_PAGE_SIZE64KB)
				numof4k_pages = 16;
			else
				numof4k_pages = 1;
			temp = 0;
			while (temp++ < numof4k_pages) {
				if (!pfn_valid(__phys_to_pfn(paddr))) {
					paddr += HW_PAGE_SIZE4KB;
					continue;
				}
				pg = PHYS_TO_PAGE(paddr);
				if (page_count(pg) < 1) {
					pr_info("DSPBRIDGE: UNMAP function: "
						"COUNT 0 FOR PA 0x%x, size = "
						"0x%x\n", paddr, ul_num_bytes);
					bad_page_dump(paddr, pg);
				} else {
					set_page_dirty(pg);
					page_cache_release(pg);
				}
				paddr += HW_PAGE_SIZE4KB;
			}
			if (hw_mmu_pte_clear(pte_addr_l2, va_curr, pte_size)) {
				status = -EPERM;
				goto EXIT_LOOP;
			}

			status = 0;
			rem_bytes_l2 -= pte_size;
			va_curr += pte_size;
			pte_addr_l2 += (pte_size >> 12) * sizeof(u32);
		}
		spin_lock(&pt->pg_lock);
		if (rem_bytes_l2 == 0) {
			pt->pg_info[l2_page_num].num_entries -= pte_count;
			if (pt->pg_info[l2_page_num].num_entries == 0) {
				/*
				 * Clear the L1 PTE pointing to the L2 PT
				 */
				if (!hw_mmu_pte_clear(l1_base_va, va_curr_orig,
						     HW_MMU_COARSE_PAGE_SIZE))
					status = 0;
				else {
					status = -EPERM;
					spin_unlock(&pt->pg_lock);
					goto EXIT_LOOP;
				}
			}
			rem_bytes -= pte_count * PG_SIZE4K;
		} else
			status = -EPERM;

		spin_unlock(&pt->pg_lock);
		continue;
skip_coarse_page:
		/* va_curr aligned to pte_size? */
		/* pte_size = 1 MB or 16 MB */
		if (pte_size == 0 || rem_bytes < pte_size ||
		    va_curr & (pte_size - 1)) {
			status = -EPERM;
			break;
		}

		if (pte_size == HW_PAGE_SIZE1MB)
			numof4k_pages = 256;
		else
			numof4k_pages = 4096;
		temp = 0;
		/* Collect Physical addresses from VA */
		paddr = (pte_val & ~(pte_size - 1));
		while (temp++ < numof4k_pages) {
			if (pfn_valid(__phys_to_pfn(paddr))) {
				pg = PHYS_TO_PAGE(paddr);
				if (page_count(pg) < 1) {
					pr_info("DSPBRIDGE: UNMAP function: "
						"COUNT 0 FOR PA 0x%x, size = "
						"0x%x\n", paddr, ul_num_bytes);
					bad_page_dump(paddr, pg);
				} else {
					set_page_dirty(pg);
					page_cache_release(pg);
				}
			}
			paddr += HW_PAGE_SIZE4KB;
		}
		if (!hw_mmu_pte_clear(l1_base_va, va_curr, pte_size)) {
			status = 0;
			rem_bytes -= pte_size;
			va_curr += pte_size;
		} else {
			status = -EPERM;
			goto EXIT_LOOP;
		}
	}
	/*
	 * It is better to flush the TLB here, so that any stale old entries
	 * get flushed
	 */
EXIT_LOOP:
	flush_all(dev_context);
	dev_dbg(bridge,
		"%s: va_curr %x, pte_addr_l1 %x pte_addr_l2 %x rem_bytes %x,"
		" rem_bytes_l2 %x status %x\n", __func__, va_curr, pte_addr_l1,
		pte_addr_l2, rem_bytes, rem_bytes_l2, status);
	return status;
}

/*
 *  ======== user_va2_pa ========
 *  Purpose:
 *      This function walks through the page tables to convert a userland
 *      virtual address to physical address
 */
static u32 user_va2_pa(struct mm_struct *mm, u32 address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, address);
	if (!(pgd_none(*pgd) || pgd_bad(*pgd))) {
		pmd = pmd_offset(pgd, address);
		if (!(pmd_none(*pmd) || pmd_bad(*pmd))) {
			ptep = pte_offset_map(pmd, address);
			if (ptep) {
				pte = *ptep;
				if (pte_present(pte))
					return pte & PAGE_MASK;
			}
		}
	}

	return 0;
}

/*
 *  ======== pte_update ========
 *      This function calculates the optimum page-aligned addresses and sizes
 *      Caller must pass page-aligned values
 */
static int pte_update(struct bridge_dev_context *dev_ctxt, u32 pa,
			     u32 va, u32 size,
			     struct hw_mmu_map_attrs_t *map_attrs)
{
	u32 i;
	u32 all_bits;
	u32 pa_curr = pa;
	u32 va_curr = va;
	u32 num_bytes = size;
	struct bridge_dev_context *dev_context = dev_ctxt;
	int status = 0;
	u32 page_size[] = { HW_PAGE_SIZE16MB, HW_PAGE_SIZE1MB,
		HW_PAGE_SIZE64KB, HW_PAGE_SIZE4KB
	};

	while (num_bytes && !status) {
		/* To find the max. page size with which both PA & VA are
		 * aligned */
		all_bits = pa_curr | va_curr;

		for (i = 0; i < 4; i++) {
			if ((num_bytes >= page_size[i]) && ((all_bits &
							     (page_size[i] -
							      1)) == 0)) {
				status =
				    pte_set(dev_context->pt_attrs, pa_curr,
					    va_curr, page_size[i], map_attrs);
				pa_curr += page_size[i];
				va_curr += page_size[i];
				num_bytes -= page_size[i];
				/* Don't try smaller sizes. Hopefully we have
				 * reached an address aligned to a bigger page
				 * size */
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== pte_set ========
 *      This function calculates PTE address (MPU virtual) to be updated
 *      It also manages the L2 page tables
 */
static int pte_set(struct pg_table_attrs *pt, u32 pa, u32 va,
			  u32 size, struct hw_mmu_map_attrs_t *attrs)
{
	u32 i;
	u32 pte_val;
	u32 pte_addr_l1;
	u32 pte_size;
	/* Base address of the PT that will be updated */
	u32 pg_tbl_va;
	u32 l1_base_va;
	/* Compiler warns that the next three variables might be used
	 * uninitialized in this function. Doesn't seem so. Working around,
	 * anyways. */
	u32 l2_base_va = 0;
	u32 l2_base_pa = 0;
	u32 l2_page_num = 0;
	int status = 0;

	l1_base_va = pt->l1_base_va;
	pg_tbl_va = l1_base_va;
	if ((size == HW_PAGE_SIZE64KB) || (size == HW_PAGE_SIZE4KB)) {
		/* Find whether the L1 PTE points to a valid L2 PT */
		pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va);
		if (pte_addr_l1 <= (pt->l1_base_va + pt->l1_size)) {
			pte_val = *(u32 *) pte_addr_l1;
			pte_size = hw_mmu_pte_size_l1(pte_val);
		} else {
			return -EPERM;
		}
		spin_lock(&pt->pg_lock);
		if (pte_size == HW_MMU_COARSE_PAGE_SIZE) {
			/* Get the L2 PA from the L1 PTE, and find
			 * corresponding L2 VA */
			l2_base_pa = hw_mmu_pte_coarse_l1(pte_val);
			l2_base_va =
			    l2_base_pa - pt->l2_base_pa + pt->l2_base_va;
			l2_page_num =
			    (l2_base_pa -
			     pt->l2_base_pa) / HW_MMU_COARSE_PAGE_SIZE;
		} else if (pte_size == 0) {
			/* L1 PTE is invalid. Allocate a L2 PT and
			 * point the L1 PTE to it */
			/* Find a free L2 PT. */
			for (i = 0; (i < pt->l2_num_pages) &&
			     (pt->pg_info[i].num_entries != 0); i++)
				;
			if (i < pt->l2_num_pages) {
				l2_page_num = i;
				l2_base_pa = pt->l2_base_pa + (l2_page_num *
						HW_MMU_COARSE_PAGE_SIZE);
				l2_base_va = pt->l2_base_va + (l2_page_num *
						HW_MMU_COARSE_PAGE_SIZE);
				/* Endianness attributes are ignored for
				 * HW_MMU_COARSE_PAGE_SIZE */
				status =
				    hw_mmu_pte_set(l1_base_va, l2_base_pa, va,
						   HW_MMU_COARSE_PAGE_SIZE,
						   attrs);
			} else {
				status = -ENOMEM;
			}
		} else {
			/* Found valid L1 PTE of another size.
			 * Should not overwrite it. */
			status = -EPERM;
		}
		if (!status) {
			pg_tbl_va = l2_base_va;
			if (size == HW_PAGE_SIZE64KB)
				pt->pg_info[l2_page_num].num_entries += 16;
			else
				pt->pg_info[l2_page_num].num_entries++;
			dev_dbg(bridge, "PTE: L2 BaseVa %x, BasePa %x, PageNum "
				"%x, num_entries %x\n", l2_base_va,
				l2_base_pa, l2_page_num,
				pt->pg_info[l2_page_num].num_entries);
		}
		spin_unlock(&pt->pg_lock);
	}
	if (!status) {
		dev_dbg(bridge, "PTE: pg_tbl_va %x, pa %x, va %x, size %x\n",
			pg_tbl_va, pa, va, size);
		dev_dbg(bridge, "PTE: endianism %x, element_size %x, "
			"mixed_size %x\n", attrs->endianism,
			attrs->element_size, attrs->mixed_size);
		status = hw_mmu_pte_set(pg_tbl_va, pa, va, size, attrs);
	}

	return status;
}

/* Memory map kernel VA -- memory allocated with vmalloc */
static int mem_map_vmalloc(struct bridge_dev_context *dev_context,
				  u32 ul_mpu_addr, u32 virt_addr,
				  u32 ul_num_bytes,
				  struct hw_mmu_map_attrs_t *hw_attrs)
{
	int status = 0;
	struct page *page[1];
	u32 i;
	u32 pa_curr;
	u32 pa_next;
	u32 va_curr;
	u32 size_curr;
	u32 num_pages;
	u32 pa;
	u32 num_of4k_pages;
	u32 temp = 0;

	/*
	 * Do Kernel va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to pte_update.
	 */
	num_pages = ul_num_bytes / PAGE_SIZE;	/* PAGE_SIZE = OS page size */
	i = 0;
	va_curr = ul_mpu_addr;
	page[0] = vmalloc_to_page((void *)va_curr);
	pa_next = page_to_phys(page[0]);
	while (!status && (i < num_pages)) {
		/*
		 * Reuse pa_next from the previous iteraion to avoid
		 * an extra va2pa call
		 */
		pa_curr = pa_next;
		size_curr = PAGE_SIZE;
		/*
		 * If the next page is physically contiguous,
		 * map it with the current one by increasing
		 * the size of the region to be mapped
		 */
		while (++i < num_pages) {
			page[0] =
			    vmalloc_to_page((void *)(va_curr + size_curr));
			pa_next = page_to_phys(page[0]);

			if (pa_next == (pa_curr + size_curr))
				size_curr += PAGE_SIZE;
			else
				break;

		}
		if (pa_next == 0) {
			status = -ENOMEM;
			break;
		}
		pa = pa_curr;
		num_of4k_pages = size_curr / HW_PAGE_SIZE4KB;
		while (temp++ < num_of4k_pages) {
			get_page(PHYS_TO_PAGE(pa));
			pa += HW_PAGE_SIZE4KB;
		}
		status = pte_update(dev_context, pa_curr, virt_addr +
				    (va_curr - ul_mpu_addr), size_curr,
				    hw_attrs);
		va_curr += size_curr;
	}
	/*
	 * In any case, flush the TLB
	 * This is called from here instead from pte_update to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region
	 */
	flush_all(dev_context);
	dev_dbg(bridge, "%s status %x\n", __func__, status);
	return status;
}

/*
 *  ======== wait_for_start ========
 *      Wait for the singal from DSP that it has started, or time out.
 */
bool wait_for_start(struct bridge_dev_context *dev_context, u32 dw_sync_addr)
{
	u16 timeout = TIHELEN_ACKTIMEOUT;

	/*  Wait for response from board */
	while (__raw_readw(dw_sync_addr) && --timeout)
		udelay(10);

	/*  If timed out: return false */
	if (!timeout) {
		pr_err("%s: Timed out waiting DSP to Start\n", __func__);
		return false;
	}
	return true;
}

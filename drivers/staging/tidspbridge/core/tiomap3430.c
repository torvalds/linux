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
#include <plat/control.h>

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
#include "_deh.h"

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
static int bridge_dev_create(struct bridge_dev_context
					**dev_cntxt,
					struct dev_object *hdev_obj,
					struct cfg_hostres *config_param);
static int bridge_dev_ctrl(struct bridge_dev_context *dev_context,
				  u32 dw_cmd, void *pargs);
static int bridge_dev_destroy(struct bridge_dev_context *dev_ctxt);
bool wait_for_start(struct bridge_dev_context *dev_context, u32 dw_sync_addr);

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
	struct iommu *mmu = NULL;
	struct shm_segs *sm_sg;
	int l4_i = 0, tlb_i = 0;
	u32 sg0_da = 0, sg1_da = 0;
	struct bridge_ioctl_extproc *tlb = dev_context->atlb_entry;
	u32 dw_sync_addr = 0;
	u32 ul_shm_base;	/* Gpp Phys SM base addr(byte) */
	u32 ul_shm_base_virt;	/* Dsp Virt SM base addr */
	u32 ul_tlb_base_virt;	/* Base of MMU TLB entry */
	/* Offset of shm_base_virt from tlb_base_virt */
	u32 ul_shm_offset_virt;
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
	ul_tlb_base_virt = dev_context->sh_s.seg0_da;
	DBC_ASSERT(ul_tlb_base_virt <= ul_shm_base_virt);
	ul_shm_offset_virt =
	    ul_shm_base_virt - (ul_tlb_base_virt * DSPWORDSIZE);
	/* Kernel logical address */
	ul_shm_base = dev_context->sh_s.seg0_va + ul_shm_offset_virt;

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
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
		mmu = dev_context->dsp_mmu;
		if (mmu)
			iommu_put(mmu);
		mmu = iommu_get("iva2");
		if (IS_ERR(mmu)) {
			dev_err(bridge, "iommu_get failed!\n");
			dev_context->dsp_mmu = NULL;
			status = (int)mmu;
		}
	}
	if (!status) {
		dev_context->dsp_mmu = mmu;
		mmu->isr = mmu_fault_isr;
		sm_sg = &dev_context->sh_s;
		sg0_da = iommu_kmap(mmu, sm_sg->seg0_da, sm_sg->seg0_pa,
			sm_sg->seg0_size, IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);
		if (IS_ERR_VALUE(sg0_da)) {
			status = (int)sg0_da;
			sg0_da = 0;
		}
	}
	if (!status) {
		sg1_da = iommu_kmap(mmu, sm_sg->seg1_da, sm_sg->seg1_pa,
			sm_sg->seg1_size, IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);
		if (IS_ERR_VALUE(sg1_da)) {
			status = (int)sg1_da;
			sg1_da = 0;
		}
	}
	if (!status) {
		u32 da;
		for (tlb_i = 0; tlb_i < BRDIOCTL_NUMOFMMUTLB; tlb_i++) {
			if (!tlb[tlb_i].ul_gpp_pa)
				continue;

			dev_dbg(bridge, "IOMMU %d GppPa: 0x%x DspVa 0x%x Size"
				" 0x%x\n", tlb_i, tlb[tlb_i].ul_gpp_pa,
				tlb[tlb_i].ul_dsp_va, tlb[tlb_i].ul_size);

			da = iommu_kmap(mmu, tlb[tlb_i].ul_dsp_va,
				tlb[tlb_i].ul_gpp_pa, PAGE_SIZE,
				IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);
			if (IS_ERR_VALUE(da)) {
				status = (int)da;
				break;
			}
		}
	}
	if (!status) {
		u32 da;
		l4_i = 0;
		while (l4_peripheral_table[l4_i].phys_addr) {
			da = iommu_kmap(mmu, l4_peripheral_table[l4_i].
				dsp_virt_addr, l4_peripheral_table[l4_i].
				phys_addr, PAGE_SIZE,
				IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);
			if (IS_ERR_VALUE(da)) {
				status = (int)da;
				break;
			}
			l4_i++;
		}
	}

	/* Lock the above TLB entries and get the BIOS and load monitor timer
	 * information */
	if (!status) {
		/* Enable the BIOS clock */
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_BIOSGPTIMER, &ul_bios_gp_timer);
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_LOADMON_GPTIMER,
				     &ul_load_monitor_timer);

		if (ul_load_monitor_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_load_monitor_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge, "Not able to get the symbol for Load "
				"Monitor Timer\n");
		}

		if (ul_bios_gp_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_bios_gp_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge,
				"Not able to get the symbol for BIOS Timer\n");
		}

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
		/* release the RST1, DSP starts executing now .. */
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

		dev_dbg(bridge, "Waiting for Sync @ 0x%x\n", dw_sync_addr);
		dev_dbg(bridge, "DSP c_int00 Address =  0x%x\n", dsp_addr);
		if (dsp_debug)
			while (__raw_readw(dw_sync_addr))
				;;

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
			return 0;
		} else {
			dev_context->dw_brd_state = BRD_UNKNOWN;
		}
	}

	while (tlb_i--) {
		if (!tlb[tlb_i].ul_gpp_pa)
			continue;
		iommu_kunmap(mmu, tlb[tlb_i].ul_gpp_va);
	}
	while (l4_i--)
		iommu_kunmap(mmu, l4_peripheral_table[l4_i].dsp_virt_addr);
	if (sg0_da)
		iommu_kunmap(mmu, sg0_da);
	if (sg1_da)
		iommu_kunmap(mmu, sg1_da);
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
	u32 dsp_pwr_state;
	int i;
	struct bridge_ioctl_extproc *tlb = dev_context->atlb_entry;
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

	/* Reset DSP */
	(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK,
		OMAP3430_RST1_IVA2_MASK, OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

	/* Disable the mailbox interrupts */
	if (dev_context->mbox) {
		omap_mbox_disable_irq(dev_context->mbox, IRQ_RX);
		omap_mbox_put(dev_context->mbox);
		dev_context->mbox = NULL;
	}
	if (dev_context->dsp_mmu) {
		pr_err("Proc stop mmu if statement\n");
		for (i = 0; i < BRDIOCTL_NUMOFMMUTLB; i++) {
			if (!tlb[i].ul_gpp_pa)
				continue;
			iommu_kunmap(dev_context->dsp_mmu, tlb[i].ul_gpp_va);
		}
		i = 0;
		while (l4_peripheral_table[i].phys_addr) {
			iommu_kunmap(dev_context->dsp_mmu,
				l4_peripheral_table[i].dsp_virt_addr);
			i++;
		}
		iommu_kunmap(dev_context->dsp_mmu, dev_context->sh_s.seg0_da);
		iommu_kunmap(dev_context->dsp_mmu, dev_context->sh_s.seg1_da);
		iommu_put(dev_context->dsp_mmu);
		dev_context->dsp_mmu = NULL;
	}
	/* Reset IVA IOMMU*/
	(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK,
		OMAP3430_RST2_IVA2_MASK, OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

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

	if (!status) {
		dev_context->tc_word_swap_on = drv_datap->tc_wordswapon;
		dev_context->hdev_obj = hdev_obj;
		/* Store current board state. */
		dev_context->dw_brd_state = BRD_UNKNOWN;
		dev_context->resources = resources;
		dsp_clk_enable(DSP_CLK_IVA2);
		bridge_brd_stop(dev_context);
		/* Return ptr to our device state to the DSP API for storage */
		*dev_cntxt = dev_context;
	} else {
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

/**
 * get_io_pages() - pin and get pages of io user's buffer.
 * @mm:		mm_struct Pointer of the process.
 * @uva:		Virtual user space address.
 * @pages	Pages to be pined.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 */
static int get_io_pages(struct mm_struct *mm, u32 uva, unsigned pages,
						struct page **usr_pgs)
{
	u32 pa;
	int i;
	struct page *pg;

	for (i = 0; i < pages; i++) {
		pa = user_va2_pa(mm, uva);

		if (!pfn_valid(__phys_to_pfn(pa)))
			break;

		pg = PHYS_TO_PAGE(pa);
		usr_pgs[i] = pg;
		get_page(pg);
	}
	return i;
}

/**
 * user_to_dsp_map() - maps user to dsp virtual address
 * @mmu:	Pointer to iommu handle.
 * @uva:		Virtual user space address.
 * @da		DSP address
 * @size		Buffer size to map.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 * This function maps a user space buffer into DSP virtual address.
 *
 */
u32 user_to_dsp_map(struct iommu *mmu, u32 uva, u32 da, u32 size,
				struct page **usr_pgs)
{
	int res, w;
	unsigned pages, i;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	struct sg_table *sgt;
	struct scatterlist *sg;

	if (!size || !usr_pgs)
		return -EINVAL;

	pages = size / PG_SIZE4K;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, uva);
	while (vma && (uva + size > vma->vm_end))
		vma = find_vma(mm, vma->vm_end + 1);

	if (!vma) {
		pr_err("%s: Failed to get VMA region for 0x%x (%d)\n",
						__func__, uva, size);
		up_read(&mm->mmap_sem);
		return -EINVAL;
	}
	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		w = 1;

	if (vma->vm_flags & VM_IO)
		i = get_io_pages(mm, uva, pages, usr_pgs);
	else
		i = get_user_pages(current, mm, uva, pages, w, 1,
							usr_pgs, NULL);
	up_read(&mm->mmap_sem);

	if (i < 0)
		return i;

	if (i < pages) {
		res = -EFAULT;
		goto err_pages;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		res = -ENOMEM;
		goto err_pages;
	}

	res = sg_alloc_table(sgt, pages, GFP_KERNEL);

	if (res < 0)
		goto err_sg;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		sg_set_page(sg, usr_pgs[i], PAGE_SIZE, 0);

	da = iommu_vmap(mmu, da, sgt, IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);

	if (!IS_ERR_VALUE(da))
		return da;
	res = (int)da;

	sg_free_table(sgt);
err_sg:
	kfree(sgt);
	i = pages;
err_pages:
	while (i--)
		put_page(usr_pgs[i]);
	return res;
}

/**
 * user_to_dsp_unmap() - unmaps DSP virtual buffer.
 * @mmu:	Pointer to iommu handle.
 * @da		DSP address
 *
 * This function unmaps a user space buffer into DSP virtual address.
 *
 */
int user_to_dsp_unmap(struct iommu *mmu, u32 da)
{
	unsigned i;
	struct sg_table *sgt;
	struct scatterlist *sg;

	sgt = iommu_vunmap(mmu, da);
	if (!sgt)
		return -EFAULT;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		put_page(sg_page(sg));
	sg_free_table(sgt);
	kfree(sgt);

	return 0;
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

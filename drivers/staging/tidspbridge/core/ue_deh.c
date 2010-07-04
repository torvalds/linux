/*
 * ue_deh.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements upper edge DSP exception handling (DEH) functions.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/clk.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdeh.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/dspapi.h>
#include <dspbridge/wdt.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- This */
#include "mmu_fault.h"
#include "_tiomap.h"
#include "_deh.h"
#include "_tiomap_pwr.h"
#include <dspbridge/io_sm.h>


int bridge_deh_create(struct deh_mgr **ret_deh_mgr,
		struct dev_object *hdev_obj)
{
	int status = 0;
	struct deh_mgr *deh_mgr;
	struct bridge_dev_context *hbridge_context = NULL;

	/*  Message manager will be created when a file is loaded, since
	 *  size of message buffer in shared memory is configurable in
	 *  the base image. */
	/* Get Bridge context info. */
	dev_get_bridge_context(hdev_obj, &hbridge_context);
	/* Allocate IO manager object: */
	deh_mgr = kzalloc(sizeof(struct deh_mgr), GFP_KERNEL);
	if (!deh_mgr) {
		status = -ENOMEM;
		goto err;
	}

	/* Create an NTFY object to manage notifications */
	deh_mgr->ntfy_obj = kmalloc(sizeof(struct ntfy_object), GFP_KERNEL);
	if (!deh_mgr->ntfy_obj) {
		status = -ENOMEM;
		goto err;
	}
	ntfy_init(deh_mgr->ntfy_obj);

	/* Create a MMUfault DPC */
	tasklet_init(&deh_mgr->dpc_tasklet, mmu_fault_dpc, (u32) deh_mgr);

	/* Fill in context structure */
	deh_mgr->hbridge_context = hbridge_context;

	/* Install ISR function for DSP MMU fault */
	status = request_irq(INT_DSP_MMU_IRQ, mmu_fault_isr, 0,
			"DspBridge\tiommu fault", deh_mgr);
	if (status < 0)
		goto err;

	*ret_deh_mgr = deh_mgr;
	return 0;

err:
	bridge_deh_destroy(deh_mgr);
	*ret_deh_mgr = NULL;
	return status;
}

int bridge_deh_destroy(struct deh_mgr *deh_mgr)
{
	if (!deh_mgr)
		return -EFAULT;

	/* If notification object exists, delete it */
	if (deh_mgr->ntfy_obj) {
		ntfy_delete(deh_mgr->ntfy_obj);
		kfree(deh_mgr->ntfy_obj);
	}
	/* Disable DSP MMU fault */
	free_irq(INT_DSP_MMU_IRQ, deh_mgr);

	/* Free DPC object */
	tasklet_kill(&deh_mgr->dpc_tasklet);

	/* Deallocate the DEH manager object */
	kfree(deh_mgr);

	return 0;
}

int bridge_deh_register_notify(struct deh_mgr *deh_mgr, u32 event_mask,
		u32 notify_type,
		struct dsp_notification *hnotification)
{
	if (!deh_mgr)
		return -EFAULT;

	if (event_mask)
		return ntfy_register(deh_mgr->ntfy_obj, hnotification,
				event_mask, notify_type);
	else
		return ntfy_unregister(deh_mgr->ntfy_obj, hnotification);
}

static void mmu_fault_print_stack(struct bridge_dev_context *dev_context,
		u32 fault_addr)
{
	struct cfg_hostres *resources;
	struct hw_mmu_map_attrs_t map_attrs = {
		.endianism = HW_LITTLE_ENDIAN,
		.element_size = HW_ELEM_SIZE16BIT,
		.mixed_size = HW_MMU_CPUES,
	};
	void *dummy_va_addr;

	resources = dev_context->resources;
	dummy_va_addr = (void*)__get_free_page(GFP_ATOMIC);

	/*
	 * Before acking the MMU fault, let's make sure MMU can only
	 * access entry #0. Then add a new entry so that the DSP OS
	 * can continue in order to dump the stack.
	 */
	hw_mmu_twl_disable(resources->dw_dmmu_base);
	hw_mmu_tlb_flush_all(resources->dw_dmmu_base);

	hw_mmu_tlb_add(resources->dw_dmmu_base,
			virt_to_phys(dummy_va_addr), fault_addr,
			HW_PAGE_SIZE4KB, 1,
			&map_attrs, HW_SET, HW_SET);

	dsp_clk_enable(DSP_CLK_GPT8);

	dsp_gpt_wait_overflow(DSP_CLK_GPT8, 0xfffffffe);

	/* Clear MMU interrupt */
	hw_mmu_event_ack(resources->dw_dmmu_base,
			HW_MMU_TRANSLATION_FAULT);
	dump_dsp_stack(dev_context);
	dsp_clk_disable(DSP_CLK_GPT8);

	hw_mmu_disable(resources->dw_dmmu_base);
	free_page((unsigned long)dummy_va_addr);
}

void bridge_deh_notify(struct deh_mgr *deh_mgr, u32 ulEventMask, u32 dwErrInfo)
{
	struct bridge_dev_context *dev_context;

	if (!deh_mgr)
		return;

	dev_info(bridge, "%s: device exception\n", __func__);
	dev_context = deh_mgr->hbridge_context;

	switch (ulEventMask) {
	case DSP_SYSERROR:
		dev_err(bridge, "%s: %s, err_info = 0x%x\n",
				__func__, "DSP_SYSERROR", dwErrInfo);
		dump_dl_modules(dev_context);
		dump_dsp_stack(dev_context);
		break;
	case DSP_MMUFAULT:
		dev_err(bridge, "%s: %s, err_info = 0x%x\n",
				__func__, "DSP_MMUFAULT", dwErrInfo);
		dev_info(bridge, "%s: %s, fault=0x%x\n", __func__, "DSP_MMUFAULT",
				deh_mgr->fault_addr);

		print_dsp_trace_buffer(dev_context);
		dump_dl_modules(dev_context);

		mmu_fault_print_stack(dev_context, deh_mgr->fault_addr);
		break;
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
	case DSP_PWRERROR:
		dev_err(bridge, "%s: %s, err_info = 0x%x\n",
				__func__, "DSP_PWRERROR", dwErrInfo);
		break;
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
	case DSP_WDTOVERFLOW:
		dev_err(bridge, "%s: DSP_WDTOVERFLOW\n", __func__);
		break;
	default:
		dev_dbg(bridge, "%s: Unknown Error, err_info = 0x%x\n",
				__func__, dwErrInfo);
		break;
	}

	/* Filter subsequent notifications when an error occurs */
	if (dev_context->dw_brd_state != BRD_ERROR) {
		ntfy_notify(deh_mgr->ntfy_obj, ulEventMask);
#ifdef CONFIG_BRIDGE_RECOVERY
		bridge_recover_schedule();
#endif
	}

	/* Set the Board state as ERROR */
	dev_context->dw_brd_state = BRD_ERROR;
	/* Disable all the clocks that were enabled by DSP */
	dsp_clock_disable_all(dev_context->dsp_per_clks);
	/*
	 * Avoid the subsequent WDT if it happens once,
	 * also if fatal error occurs.
	 */
	dsp_wdt_enable(false);
}

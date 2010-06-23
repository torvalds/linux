/*
 * mmu_fault.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements DSP MMU fault handling functions.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/host_os.h>
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/drv.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdeh.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- This */
#include "_deh.h"
#include <dspbridge/cfg.h>
#include "_tiomap.h"
#include "mmu_fault.h"

static u32 dmmu_event_mask;
u32 fault_addr;

static bool mmu_check_if_fault(struct bridge_dev_context *dev_context);

/*
 *  ======== mmu_fault_dpc ========
 *      Deferred procedure call to handle DSP MMU fault.
 */
void mmu_fault_dpc(IN unsigned long pRefData)
{
	struct deh_mgr *hdeh_mgr = (struct deh_mgr *)pRefData;

	if (hdeh_mgr)
		bridge_deh_notify(hdeh_mgr, DSP_MMUFAULT, 0L);

}

/*
 *  ======== mmu_fault_isr ========
 *      ISR to be triggered by a DSP MMU fault interrupt.
 */
irqreturn_t mmu_fault_isr(int irq, IN void *pRefData)
{
	struct deh_mgr *deh_mgr_obj = (struct deh_mgr *)pRefData;
	struct bridge_dev_context *dev_context;
	struct cfg_hostres *resources;

	DBC_REQUIRE(irq == INT_DSP_MMU_IRQ);
	DBC_REQUIRE(deh_mgr_obj);

	if (deh_mgr_obj) {

		dev_context =
		    (struct bridge_dev_context *)deh_mgr_obj->hbridge_context;

		resources = dev_context->resources;

		if (!resources) {
			dev_dbg(bridge, "%s: Failed to get Host Resources\n",
				__func__);
			return IRQ_HANDLED;
		}
		if (mmu_check_if_fault(dev_context)) {
			printk(KERN_INFO "***** DSPMMU FAULT ***** IRQStatus "
			       "0x%x\n", dmmu_event_mask);
			printk(KERN_INFO "***** DSPMMU FAULT ***** fault_addr "
			       "0x%x\n", fault_addr);
			/*
			 * Schedule a DPC directly. In the future, it may be
			 * necessary to check if DSP MMU fault is intended for
			 * Bridge.
			 */
			tasklet_schedule(&deh_mgr_obj->dpc_tasklet);

			/* Reset err_info structure before use. */
			deh_mgr_obj->err_info.dw_err_mask = DSP_MMUFAULT;
			deh_mgr_obj->err_info.dw_val1 = fault_addr >> 16;
			deh_mgr_obj->err_info.dw_val2 = fault_addr & 0xFFFF;
			deh_mgr_obj->err_info.dw_val3 = 0L;
			/* Disable the MMU events, else once we clear it will
			 * start to raise INTs again */
			hw_mmu_event_disable(resources->dw_dmmu_base,
					     HW_MMU_TRANSLATION_FAULT);
		} else {
			hw_mmu_event_disable(resources->dw_dmmu_base,
					     HW_MMU_ALL_INTERRUPTS);
		}
	}
	return IRQ_HANDLED;
}

/*
 *  ======== mmu_check_if_fault ========
 *      Check to see if MMU Fault is valid TLB miss from DSP
 *  Note: This function is called from an ISR
 */
static bool mmu_check_if_fault(struct bridge_dev_context *dev_context)
{

	bool ret = false;
	hw_status hw_status_obj;
	struct cfg_hostres *resources = dev_context->resources;

	if (!resources) {
		dev_dbg(bridge, "%s: Failed to get Host Resources in\n",
			__func__);
		return ret;
	}
	hw_status_obj =
	    hw_mmu_event_status(resources->dw_dmmu_base, &dmmu_event_mask);
	if (dmmu_event_mask == HW_MMU_TRANSLATION_FAULT) {
		hw_mmu_fault_addr_read(resources->dw_dmmu_base, &fault_addr);
		ret = true;
	}
	return ret;
}

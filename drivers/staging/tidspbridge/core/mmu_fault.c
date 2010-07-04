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

/*
 *  ======== mmu_fault_dpc ========
 *      Deferred procedure call to handle DSP MMU fault.
 */
void mmu_fault_dpc(IN unsigned long pRefData)
{
	struct deh_mgr *hdeh_mgr = (struct deh_mgr *)pRefData;

	if (!hdeh_mgr)
		return;

	bridge_deh_notify(hdeh_mgr, DSP_MMUFAULT, 0L);
}

/*
 *  ======== mmu_fault_isr ========
 *      ISR to be triggered by a DSP MMU fault interrupt.
 */
irqreturn_t mmu_fault_isr(int irq, IN void *pRefData)
{
	struct deh_mgr *deh_mgr_obj = pRefData;
	struct cfg_hostres *resources;
	u32 dmmu_event_mask;

	if (!deh_mgr_obj)
		return IRQ_HANDLED;

	resources = deh_mgr_obj->hbridge_context->resources;
	if (!resources) {
		dev_dbg(bridge, "%s: Failed to get Host Resources\n",
				__func__);
		return IRQ_HANDLED;
	}

	hw_mmu_event_status(resources->dw_dmmu_base, &dmmu_event_mask);
	if (dmmu_event_mask == HW_MMU_TRANSLATION_FAULT) {
		hw_mmu_fault_addr_read(resources->dw_dmmu_base, &deh_mgr_obj->fault_addr);
		dev_info(bridge, "%s: status=0x%x, fault_addr=0x%x\n", __func__,
				dmmu_event_mask, deh_mgr_obj->fault_addr);
		/*
		 * Schedule a DPC directly. In the future, it may be
		 * necessary to check if DSP MMU fault is intended for
		 * Bridge.
		 */
		tasklet_schedule(&deh_mgr_obj->dpc_tasklet);

		/* Disable the MMU events, else once we clear it will
		 * start to raise INTs again */
		hw_mmu_event_disable(resources->dw_dmmu_base,
				HW_MMU_TRANSLATION_FAULT);
	} else {
		hw_mmu_event_disable(resources->dw_dmmu_base,
				HW_MMU_ALL_INTERRUPTS);
	}
	return IRQ_HANDLED;
}

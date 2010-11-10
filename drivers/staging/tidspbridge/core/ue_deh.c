/*
 * ue_deh.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements upper edge DSP exception handling (DEH) functions.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 * Copyright (C) 2010 Felipe Contreras
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <plat/dmtimer.h>

#include <dspbridge/dbdefs.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/dev.h>
#include "_tiomap.h"
#include "_deh.h"

#include <dspbridge/io_sm.h>
#include <dspbridge/drv.h>
#include <dspbridge/wdt.h>

#define MMU_CNTL_TWL_EN		(1 << 2)

static void mmu_fault_dpc(unsigned long data)
{
	struct deh_mgr *deh = (void *)data;

	if (!deh)
		return;

	bridge_deh_notify(deh, DSP_MMUFAULT, 0);
}

int mmu_fault_isr(struct iommu *mmu)
{
	struct deh_mgr *dm;

	dev_get_deh_mgr(dev_get_first(), &dm);

	if (!dm)
		return -EPERM;

	iommu_write_reg(mmu, 0, MMU_IRQENABLE);
	tasklet_schedule(&dm->dpc_tasklet);
	return 0;
}

int bridge_deh_create(struct deh_mgr **ret_deh,
		struct dev_object *hdev_obj)
{
	int status;
	struct deh_mgr *deh;
	struct bridge_dev_context *hbridge_context = NULL;

	/*  Message manager will be created when a file is loaded, since
	 *  size of message buffer in shared memory is configurable in
	 *  the base image. */
	/* Get Bridge context info. */
	dev_get_bridge_context(hdev_obj, &hbridge_context);
	/* Allocate IO manager object: */
	deh = kzalloc(sizeof(*deh), GFP_KERNEL);
	if (!deh) {
		status = -ENOMEM;
		goto err;
	}

	/* Create an NTFY object to manage notifications */
	deh->ntfy_obj = kmalloc(sizeof(struct ntfy_object), GFP_KERNEL);
	if (!deh->ntfy_obj) {
		status = -ENOMEM;
		goto err;
	}
	ntfy_init(deh->ntfy_obj);

	/* Create a MMUfault DPC */
	tasklet_init(&deh->dpc_tasklet, mmu_fault_dpc, (u32) deh);

	/* Fill in context structure */
	deh->hbridge_context = hbridge_context;

	*ret_deh = deh;
	return 0;

err:
	bridge_deh_destroy(deh);
	*ret_deh = NULL;
	return status;
}

int bridge_deh_destroy(struct deh_mgr *deh)
{
	if (!deh)
		return -EFAULT;

	/* If notification object exists, delete it */
	if (deh->ntfy_obj) {
		ntfy_delete(deh->ntfy_obj);
		kfree(deh->ntfy_obj);
	}

	/* Free DPC object */
	tasklet_kill(&deh->dpc_tasklet);

	/* Deallocate the DEH manager object */
	kfree(deh);

	return 0;
}

int bridge_deh_register_notify(struct deh_mgr *deh, u32 event_mask,
		u32 notify_type,
		struct dsp_notification *hnotification)
{
	if (!deh)
		return -EFAULT;

	if (event_mask)
		return ntfy_register(deh->ntfy_obj, hnotification,
				event_mask, notify_type);
	else
		return ntfy_unregister(deh->ntfy_obj, hnotification);
}

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
static void mmu_fault_print_stack(struct bridge_dev_context *dev_context)
{
	void *dummy_addr;
	u32 fa, tmp;
	struct iotlb_entry e;
	struct iommu *mmu = dev_context->dsp_mmu;
	dummy_addr = (void *)__get_free_page(GFP_ATOMIC);

	/*
	 * Before acking the MMU fault, let's make sure MMU can only
	 * access entry #0. Then add a new entry so that the DSP OS
	 * can continue in order to dump the stack.
	 */
	tmp = iommu_read_reg(mmu, MMU_CNTL);
	tmp &= ~MMU_CNTL_TWL_EN;
	iommu_write_reg(mmu, tmp, MMU_CNTL);
	fa = iommu_read_reg(mmu, MMU_FAULT_AD);
	e.da = fa & PAGE_MASK;
	e.pa = virt_to_phys(dummy_addr);
	e.valid = 1;
	e.prsvd = 1;
	e.pgsz = IOVMF_PGSZ_4K & MMU_CAM_PGSZ_MASK;
	e.endian = MMU_RAM_ENDIAN_LITTLE;
	e.elsz = MMU_RAM_ELSZ_32;
	e.mixed = 0;

	load_iotlb_entry(dev_context->dsp_mmu, &e);

	dsp_clk_enable(DSP_CLK_GPT8);

	dsp_gpt_wait_overflow(DSP_CLK_GPT8, 0xfffffffe);

	/* Clear MMU interrupt */
	tmp = iommu_read_reg(mmu, MMU_IRQSTATUS);
	iommu_write_reg(mmu, tmp, MMU_IRQSTATUS);

	dump_dsp_stack(dev_context);
	dsp_clk_disable(DSP_CLK_GPT8);

	iopgtable_clear_entry(mmu, fa);
	free_page((unsigned long)dummy_addr);
}
#endif

static inline const char *event_to_string(int event)
{
	switch (event) {
	case DSP_SYSERROR: return "DSP_SYSERROR"; break;
	case DSP_MMUFAULT: return "DSP_MMUFAULT"; break;
	case DSP_PWRERROR: return "DSP_PWRERROR"; break;
	case DSP_WDTOVERFLOW: return "DSP_WDTOVERFLOW"; break;
	default: return "unkown event"; break;
	}
}

void bridge_deh_notify(struct deh_mgr *deh, int event, int info)
{
	struct bridge_dev_context *dev_context;
	const char *str = event_to_string(event);
	u32 fa;

	if (!deh)
		return;

	dev_dbg(bridge, "%s: device exception", __func__);
	dev_context = deh->hbridge_context;

	switch (event) {
	case DSP_SYSERROR:
		dev_err(bridge, "%s: %s, info=0x%x", __func__,
				str, info);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
		dump_dl_modules(dev_context);
		dump_dsp_stack(dev_context);
#endif
		break;
	case DSP_MMUFAULT:
		fa = iommu_read_reg(dev_context->dsp_mmu, MMU_FAULT_AD);
		dev_err(bridge, "%s: %s, addr=0x%x", __func__, str, fa);
#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
		print_dsp_trace_buffer(dev_context);
		dump_dl_modules(dev_context);
		mmu_fault_print_stack(dev_context);
#endif
		break;
	default:
		dev_err(bridge, "%s: %s", __func__, str);
		break;
	}

	/* Filter subsequent notifications when an error occurs */
	if (dev_context->dw_brd_state != BRD_ERROR) {
		ntfy_notify(deh->ntfy_obj, event);
#ifdef CONFIG_TIDSPBRIDGE_RECOVERY
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

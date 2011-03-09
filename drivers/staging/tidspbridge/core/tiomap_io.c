/*
 * tiomap_io.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation for the io read/write routines.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/wdt.h>

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "tiomap_io.h"

static u32 ul_ext_base;
static u32 ul_ext_end;

static u32 shm0_end;
static u32 ul_dyn_ext_base;
static u32 ul_trace_sec_beg;
static u32 ul_trace_sec_end;
static u32 ul_shm_base_virt;

bool symbols_reloaded = true;

/*
 *  ======== read_ext_dsp_data ========
 *      Copies DSP external memory buffers to the host side buffers.
 */
int read_ext_dsp_data(struct bridge_dev_context *dev_ctxt,
			     u8 *host_buff, u32 dsp_addr,
			     u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 offset;
	u32 ul_tlb_base_virt = 0;
	u32 ul_shm_offset_virt = 0;
	u32 dw_ext_prog_virt_mem;
	u32 dw_base_addr = dev_context->dw_dsp_ext_base_addr;
	bool trace_read = false;

	if (!ul_shm_base_virt) {
		status = dev_get_symbol(dev_context->hdev_obj,
					SHMBASENAME, &ul_shm_base_virt);
	}
	DBC_ASSERT(ul_shm_base_virt != 0);

	/* Check if it is a read of Trace section */
	if (!status && !ul_trace_sec_beg) {
		status = dev_get_symbol(dev_context->hdev_obj,
					DSP_TRACESEC_BEG, &ul_trace_sec_beg);
	}
	DBC_ASSERT(ul_trace_sec_beg != 0);

	if (!status && !ul_trace_sec_end) {
		status = dev_get_symbol(dev_context->hdev_obj,
					DSP_TRACESEC_END, &ul_trace_sec_end);
	}
	DBC_ASSERT(ul_trace_sec_end != 0);

	if (!status) {
		if ((dsp_addr <= ul_trace_sec_end) &&
		    (dsp_addr >= ul_trace_sec_beg))
			trace_read = true;
	}

	/* If reading from TRACE, force remap/unmap */
	if (trace_read && dw_base_addr) {
		dw_base_addr = 0;
		dev_context->dw_dsp_ext_base_addr = 0;
	}

	if (!dw_base_addr) {
		/* Initialize ul_ext_base and ul_ext_end */
		ul_ext_base = 0;
		ul_ext_end = 0;

		/* Get DYNEXT_BEG, EXT_BEG and EXT_END. */
		if (!status && !ul_dyn_ext_base) {
			status = dev_get_symbol(dev_context->hdev_obj,
						DYNEXTBASE, &ul_dyn_ext_base);
		}
		DBC_ASSERT(ul_dyn_ext_base != 0);

		if (!status) {
			status = dev_get_symbol(dev_context->hdev_obj,
						EXTBASE, &ul_ext_base);
		}
		DBC_ASSERT(ul_ext_base != 0);

		if (!status) {
			status = dev_get_symbol(dev_context->hdev_obj,
						EXTEND, &ul_ext_end);
		}
		DBC_ASSERT(ul_ext_end != 0);

		/* Trace buffer is right after the shm SEG0,
		 *  so set the base address to SHMBASE */
		if (trace_read) {
			ul_ext_base = ul_shm_base_virt;
			ul_ext_end = ul_trace_sec_end;
		}

		DBC_ASSERT(ul_ext_end != 0);
		DBC_ASSERT(ul_ext_end > ul_ext_base);

		if (ul_ext_end < ul_ext_base)
			status = -EPERM;

		if (!status) {
			ul_tlb_base_virt =
			    dev_context->atlb_entry[0].ul_dsp_va * DSPWORDSIZE;
			DBC_ASSERT(ul_tlb_base_virt <= ul_shm_base_virt);
			dw_ext_prog_virt_mem =
			    dev_context->atlb_entry[0].ul_gpp_va;

			if (!trace_read) {
				ul_shm_offset_virt =
				    ul_shm_base_virt - ul_tlb_base_virt;
				ul_shm_offset_virt +=
				    PG_ALIGN_HIGH(ul_ext_end - ul_dyn_ext_base +
						  1, HW_PAGE_SIZE64KB);
				dw_ext_prog_virt_mem -= ul_shm_offset_virt;
				dw_ext_prog_virt_mem +=
				    (ul_ext_base - ul_dyn_ext_base);
				dev_context->dw_dsp_ext_base_addr =
				    dw_ext_prog_virt_mem;

				/*
				 * This dw_dsp_ext_base_addr will get cleared
				 * only when the board is stopped.
				*/
				if (!dev_context->dw_dsp_ext_base_addr)
					status = -EPERM;
			}

			dw_base_addr = dw_ext_prog_virt_mem;
		}
	}

	if (!dw_base_addr || !ul_ext_base || !ul_ext_end)
		status = -EPERM;

	offset = dsp_addr - ul_ext_base;

	if (!status)
		memcpy(host_buff, (u8 *) dw_base_addr + offset, ul_num_bytes);

	return status;
}

/*
 *  ======== write_dsp_data ========
 *  purpose:
 *      Copies buffers to the DSP internal/external memory.
 */
int write_dsp_data(struct bridge_dev_context *dev_context,
			  u8 *host_buff, u32 dsp_addr, u32 ul_num_bytes,
			  u32 mem_type)
{
	u32 offset;
	u32 dw_base_addr = dev_context->dw_dsp_base_addr;
	struct cfg_hostres *resources = dev_context->resources;
	int status = 0;
	u32 base1, base2, base3;
	base1 = OMAP_DSP_MEM1_SIZE;
	base2 = OMAP_DSP_MEM2_BASE - OMAP_DSP_MEM1_BASE;
	base3 = OMAP_DSP_MEM3_BASE - OMAP_DSP_MEM1_BASE;

	if (!resources)
		return -EPERM;

	offset = dsp_addr - dev_context->dw_dsp_start_add;
	if (offset < base1) {
		dw_base_addr = MEM_LINEAR_ADDRESS(resources->dw_mem_base[2],
						  resources->dw_mem_length[2]);
	} else if (offset > base1 && offset < base2 + OMAP_DSP_MEM2_SIZE) {
		dw_base_addr = MEM_LINEAR_ADDRESS(resources->dw_mem_base[3],
						  resources->dw_mem_length[3]);
		offset = offset - base2;
	} else if (offset >= base2 + OMAP_DSP_MEM2_SIZE &&
		   offset < base3 + OMAP_DSP_MEM3_SIZE) {
		dw_base_addr = MEM_LINEAR_ADDRESS(resources->dw_mem_base[4],
						  resources->dw_mem_length[4]);
		offset = offset - base3;
	} else {
		return -EPERM;
	}
	if (ul_num_bytes)
		memcpy((u8 *) (dw_base_addr + offset), host_buff, ul_num_bytes);
	else
		*((u32 *) host_buff) = dw_base_addr + offset;

	return status;
}

/*
 *  ======== write_ext_dsp_data ========
 *  purpose:
 *      Copies buffers to the external memory.
 *
 */
int write_ext_dsp_data(struct bridge_dev_context *dev_context,
			      u8 *host_buff, u32 dsp_addr,
			      u32 ul_num_bytes, u32 mem_type,
			      bool dynamic_load)
{
	u32 dw_base_addr = dev_context->dw_dsp_ext_base_addr;
	u32 dw_offset = 0;
	u8 temp_byte1, temp_byte2;
	u8 remain_byte[4];
	s32 i;
	int ret = 0;
	u32 dw_ext_prog_virt_mem;
	u32 ul_tlb_base_virt = 0;
	u32 ul_shm_offset_virt = 0;
	struct cfg_hostres *host_res = dev_context->resources;
	bool trace_load = false;
	temp_byte1 = 0x0;
	temp_byte2 = 0x0;

	if (symbols_reloaded) {
		/* Check if it is a load to Trace section */
		ret = dev_get_symbol(dev_context->hdev_obj,
				     DSP_TRACESEC_BEG, &ul_trace_sec_beg);
		if (!ret)
			ret = dev_get_symbol(dev_context->hdev_obj,
					     DSP_TRACESEC_END,
					     &ul_trace_sec_end);
	}
	if (!ret) {
		if ((dsp_addr <= ul_trace_sec_end) &&
		    (dsp_addr >= ul_trace_sec_beg))
			trace_load = true;
	}

	/* If dynamic, force remap/unmap */
	if ((dynamic_load || trace_load) && dw_base_addr) {
		dw_base_addr = 0;
		MEM_UNMAP_LINEAR_ADDRESS((void *)
					 dev_context->dw_dsp_ext_base_addr);
		dev_context->dw_dsp_ext_base_addr = 0x0;
	}
	if (!dw_base_addr) {
		if (symbols_reloaded)
			/* Get SHM_BEG  EXT_BEG and EXT_END. */
			ret = dev_get_symbol(dev_context->hdev_obj,
					     SHMBASENAME, &ul_shm_base_virt);
		DBC_ASSERT(ul_shm_base_virt != 0);
		if (dynamic_load) {
			if (!ret) {
				if (symbols_reloaded)
					ret =
					    dev_get_symbol
					    (dev_context->hdev_obj, DYNEXTBASE,
					     &ul_ext_base);
			}
			DBC_ASSERT(ul_ext_base != 0);
			if (!ret) {
				/* DR  OMAPS00013235 : DLModules array may be
				 * in EXTMEM. It is expected that DYNEXTMEM and
				 * EXTMEM are contiguous, so checking for the
				 * upper bound at EXTEND should be Ok. */
				if (symbols_reloaded)
					ret =
					    dev_get_symbol
					    (dev_context->hdev_obj, EXTEND,
					     &ul_ext_end);
			}
		} else {
			if (symbols_reloaded) {
				if (!ret)
					ret =
					    dev_get_symbol
					    (dev_context->hdev_obj, EXTBASE,
					     &ul_ext_base);
				DBC_ASSERT(ul_ext_base != 0);
				if (!ret)
					ret =
					    dev_get_symbol
					    (dev_context->hdev_obj, EXTEND,
					     &ul_ext_end);
			}
		}
		/* Trace buffer it right after the shm SEG0, so set the
		 *      base address to SHMBASE */
		if (trace_load)
			ul_ext_base = ul_shm_base_virt;

		DBC_ASSERT(ul_ext_end != 0);
		DBC_ASSERT(ul_ext_end > ul_ext_base);
		if (ul_ext_end < ul_ext_base)
			ret = -EPERM;

		if (!ret) {
			ul_tlb_base_virt =
			    dev_context->atlb_entry[0].ul_dsp_va * DSPWORDSIZE;
			DBC_ASSERT(ul_tlb_base_virt <= ul_shm_base_virt);

			if (symbols_reloaded) {
				ret = dev_get_symbol
					    (dev_context->hdev_obj,
					     DSP_TRACESEC_END, &shm0_end);
				if (!ret) {
					ret =
					    dev_get_symbol
					    (dev_context->hdev_obj, DYNEXTBASE,
					     &ul_dyn_ext_base);
				}
			}
			ul_shm_offset_virt =
			    ul_shm_base_virt - ul_tlb_base_virt;
			if (trace_load) {
				dw_ext_prog_virt_mem =
				    dev_context->atlb_entry[0].ul_gpp_va;
			} else {
				dw_ext_prog_virt_mem = host_res->dw_mem_base[1];
				dw_ext_prog_virt_mem +=
				    (ul_ext_base - ul_dyn_ext_base);
			}

			dev_context->dw_dsp_ext_base_addr =
			    (u32) MEM_LINEAR_ADDRESS((void *)
						     dw_ext_prog_virt_mem,
						     ul_ext_end - ul_ext_base);
			dw_base_addr += dev_context->dw_dsp_ext_base_addr;
			/* This dw_dsp_ext_base_addr will get cleared only when
			 * the board is stopped. */
			if (!dev_context->dw_dsp_ext_base_addr)
				ret = -EPERM;
		}
	}
	if (!dw_base_addr || !ul_ext_base || !ul_ext_end)
		ret = -EPERM;

	if (!ret) {
		for (i = 0; i < 4; i++)
			remain_byte[i] = 0x0;

		dw_offset = dsp_addr - ul_ext_base;
		/* Also make sure the dsp_addr is < ul_ext_end */
		if (dsp_addr > ul_ext_end || dw_offset > dsp_addr)
			ret = -EPERM;
	}
	if (!ret) {
		if (ul_num_bytes)
			memcpy((u8 *) dw_base_addr + dw_offset, host_buff,
			       ul_num_bytes);
		else
			*((u32 *) host_buff) = dw_base_addr + dw_offset;
	}
	/* Unmap here to force remap for other Ext loads */
	if ((dynamic_load || trace_load) && dev_context->dw_dsp_ext_base_addr) {
		MEM_UNMAP_LINEAR_ADDRESS((void *)
					 dev_context->dw_dsp_ext_base_addr);
		dev_context->dw_dsp_ext_base_addr = 0x0;
	}
	symbols_reloaded = false;
	return ret;
}

int sm_interrupt_dsp(struct bridge_dev_context *dev_context, u16 mb_val)
{
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 opplevel = 0;
#endif
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;
	struct cfg_hostres *resources = dev_context->resources;
	int status = 0;
	u32 temp;

	if (!dev_context->mbox)
		return 0;

	if (!resources)
		return -EPERM;

	if (dev_context->dw_brd_state == BRD_DSP_HIBERNATION ||
	    dev_context->dw_brd_state == BRD_HIBERNATION) {
#ifdef CONFIG_TIDSPBRIDGE_DVFS
		if (pdata->dsp_get_opp)
			opplevel = (*pdata->dsp_get_opp) ();
		if (opplevel == VDD1_OPP1) {
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp) (VDD1_OPP2);
		}
#endif
		/* Restart the peripheral clocks */
		dsp_clock_enable_all(dev_context->dsp_per_clks);
		dsp_wdt_enable(true);

		/*
		 * 2:0 AUTO_IVA2_DPLL - Enabling IVA2 DPLL auto control
		 *     in CM_AUTOIDLE_PLL_IVA2 register
		 */
		(*pdata->dsp_cm_write)(1 << OMAP3430_AUTO_IVA2_DPLL_SHIFT,
				OMAP3430_IVA2_MOD, OMAP3430_CM_AUTOIDLE_PLL);

		/*
		 * 7:4 IVA2_DPLL_FREQSEL - IVA2 internal frq set to
		 *     0.75 MHz - 1.0 MHz
		 * 2:0 EN_IVA2_DPLL - Enable IVA2 DPLL in lock mode
		 */
		(*pdata->dsp_cm_rmw_bits)(OMAP3430_IVA2_DPLL_FREQSEL_MASK |
				OMAP3430_EN_IVA2_DPLL_MASK,
				0x3 << OMAP3430_IVA2_DPLL_FREQSEL_SHIFT |
				0x7 << OMAP3430_EN_IVA2_DPLL_SHIFT,
				OMAP3430_IVA2_MOD, OMAP3430_CM_CLKEN_PLL);

		/* Restore mailbox settings */
		omap_mbox_restore_ctx(dev_context->mbox);

		/* Access MMU SYS CONFIG register to generate a short wakeup */
		temp = readl(resources->dw_dmmu_base + 0x10);

		dev_context->dw_brd_state = BRD_RUNNING;
	} else if (dev_context->dw_brd_state == BRD_RETENTION) {
		/* Restart the peripheral clocks */
		dsp_clock_enable_all(dev_context->dsp_per_clks);
	}

	status = omap_mbox_msg_send(dev_context->mbox, mb_val);

	if (status) {
		pr_err("omap_mbox_msg_send Fail and status = %d\n", status);
		status = -EPERM;
	}

	return 0;
}

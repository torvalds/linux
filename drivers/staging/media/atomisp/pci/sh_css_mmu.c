// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_mmu.h"
#include "ia_css_mmu_private.h"
#include <ia_css_debug.h>
#include "sh_css_sp.h"
#include "sh_css_firmware.h"
#include "sp.h"
#include "mmu_device.h"

void
ia_css_mmu_invalidate_cache(void)
{
	const struct ia_css_fw_info *fw = &sh_css_sp_fw;
	unsigned int HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_mmu_invalidate_cache() enter\n");

	/* if the SP is not running we should not access its dmem */
	if (sh_css_sp_is_running()) {
		HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb = fw->info.sp.invalidate_tlb;

		(void)HIVE_ADDR_ia_css_dmaproxy_sp_invalidate_tlb; /* Suppres warnings in CRUN */

		sp_dmem_store_uint32(SP0_ID,
				     (unsigned int)sp_address_of(ia_css_dmaproxy_sp_invalidate_tlb),
				     true);
	}
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			    "ia_css_mmu_invalidate_cache() leave\n");
}

void
sh_css_mmu_set_page_table_base_index(hrt_data base_index)
{
	int i;

	IA_CSS_ENTER_PRIVATE("base_index=0x%08x\n", base_index);
	for (i = 0; i < N_MMU_ID; i++) {
		mmu_ID_t mmu_id = i;

		mmu_set_page_table_base_index(mmu_id, base_index);
		mmu_invalidate_cache(mmu_id);
	}
	IA_CSS_LEAVE_PRIVATE("");
}

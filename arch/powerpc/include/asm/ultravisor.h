/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ultravisor definitions
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#ifndef _ASM_POWERPC_ULTRAVISOR_H
#define _ASM_POWERPC_ULTRAVISOR_H

#include <asm/asm-prototypes.h>
#include <asm/ultravisor-api.h>
#include <asm/firmware.h>

int early_init_dt_scan_ultravisor(unsigned long node, const char *uname,
				  int depth, void *data);

/*
 * In ultravisor enabled systems, PTCR becomes ultravisor privileged only for
 * writing and an attempt to write to it will cause a Hypervisor Emulation
 * Assistance interrupt.
 */
static inline void set_ptcr_when_no_uv(u64 val)
{
	if (!firmware_has_feature(FW_FEATURE_ULTRAVISOR))
		mtspr(SPRN_PTCR, val);
}

static inline int uv_register_pate(u64 lpid, u64 dw0, u64 dw1)
{
	return ucall_norets(UV_WRITE_PATE, lpid, dw0, dw1);
}

static inline int uv_share_page(u64 pfn, u64 npages)
{
	return ucall_norets(UV_SHARE_PAGE, pfn, npages);
}

static inline int uv_unshare_page(u64 pfn, u64 npages)
{
	return ucall_norets(UV_UNSHARE_PAGE, pfn, npages);
}

static inline int uv_unshare_all_pages(void)
{
	return ucall_norets(UV_UNSHARE_ALL_PAGES);
}

#endif	/* _ASM_POWERPC_ULTRAVISOR_H */

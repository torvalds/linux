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

static inline int uv_page_in(u64 lpid, u64 src_ra, u64 dst_gpa, u64 flags,
			     u64 page_shift)
{
	return ucall_norets(UV_PAGE_IN, lpid, src_ra, dst_gpa, flags,
			    page_shift);
}

static inline int uv_page_out(u64 lpid, u64 dst_ra, u64 src_gpa, u64 flags,
			      u64 page_shift)
{
	return ucall_norets(UV_PAGE_OUT, lpid, dst_ra, src_gpa, flags,
			    page_shift);
}

static inline int uv_register_mem_slot(u64 lpid, u64 start_gpa, u64 size,
				       u64 flags, u64 slotid)
{
	return ucall_norets(UV_REGISTER_MEM_SLOT, lpid, start_gpa,
			    size, flags, slotid);
}

static inline int uv_unregister_mem_slot(u64 lpid, u64 slotid)
{
	return ucall_norets(UV_UNREGISTER_MEM_SLOT, lpid, slotid);
}

static inline int uv_page_inval(u64 lpid, u64 gpa, u64 page_shift)
{
	return ucall_norets(UV_PAGE_INVAL, lpid, gpa, page_shift);
}

static inline int uv_svm_terminate(u64 lpid)
{
	return ucall_norets(UV_SVM_TERMINATE, lpid);
}

#endif	/* _ASM_POWERPC_ULTRAVISOR_H */

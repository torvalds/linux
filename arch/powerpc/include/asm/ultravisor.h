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

int early_init_dt_scan_ultravisor(unsigned long node, const char *uname,
				  int depth, void *data);

static inline int uv_register_pate(u64 lpid, u64 dw0, u64 dw1)
{
	return ucall_norets(UV_WRITE_PATE, lpid, dw0, dw1);
}

#endif	/* _ASM_POWERPC_ULTRAVISOR_H */

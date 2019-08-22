/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ultravisor definitions
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#ifndef _ASM_POWERPC_ULTRAVISOR_H
#define _ASM_POWERPC_ULTRAVISOR_H

int early_init_dt_scan_ultravisor(unsigned long node, const char *uname,
				  int depth, void *data);

#endif	/* _ASM_POWERPC_ULTRAVISOR_H */

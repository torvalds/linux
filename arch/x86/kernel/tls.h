/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal declarations for x86 TLS implementation functions.
 *
 * Copyright (C) 2007 Red Hat, Inc.  All rights reserved.
 *
 * Red Hat Author: Roland McGrath.
 */

#ifndef _ARCH_X86_KERNEL_TLS_H

#include <linux/regset.h>

extern user_regset_active_fn regset_tls_active;
extern user_regset_get2_fn regset_tls_get;
extern user_regset_set_fn regset_tls_set;

#endif	/* _ARCH_X86_KERNEL_TLS_H */

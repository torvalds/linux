/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#include <linux/stringify.h>

#define MODULE_ARCH_VERMAGIC	"ia64" \
	"gcc-" __stringify(__GNUC__) "." __stringify(__GNUC_MINOR__)

#endif /* _ASM_VERMAGIC_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#include <linux/stringify.h>
#include <variant/core.h>

#define MODULE_ARCH_VERMAGIC "xtensa-" __stringify(XCHAL_CORE_ID) " "

#endif	/* _ASM_VERMAGIC_H */

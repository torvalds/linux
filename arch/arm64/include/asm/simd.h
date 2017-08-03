/*
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __ASM_SIMD_H
#define __ASM_SIMD_H

#include <linux/types.h>

/*
 * may_use_simd - whether it is allowable at this time to issue SIMD
 *                instructions or access the SIMD register file
 */
static __must_check inline bool may_use_simd(void)
{
	return true;
}

#endif

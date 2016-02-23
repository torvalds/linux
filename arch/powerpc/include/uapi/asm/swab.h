/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_ASM_POWERPC_SWAB_H
#define _UAPI_ASM_POWERPC_SWAB_H


#include <linux/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

#ifndef __powerpc64__
#define __SWAB_64_THRU_32__
#endif /* __powerpc64__ */


#endif /* __GNUC__ */

#endif /* _UAPI_ASM_POWERPC_SWAB_H */

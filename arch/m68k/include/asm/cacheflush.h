/* SPDX-License-Identifier: GPL-2.0 */
#ifdef __uClinux__
#include <asm/cacheflush_no.h>
#else
#include <asm/cacheflush_mm.h>
#endif

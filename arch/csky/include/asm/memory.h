/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_MEMORY_H
#define __ASM_CSKY_MEMORY_H

#include <linux/compiler.h>
#include <linux/const.h>
#include <linux/types.h>
#include <linux/sizes.h>

#define FIXADDR_TOP	_AC(0xffffc000, UL)

#ifdef CONFIG_HAVE_TCM
#ifdef CONFIG_HAVE_DTCM
#define TCM_NR_PAGES	(CONFIG_ITCM_NR_PAGES + CONFIG_DTCM_NR_PAGES)
#else
#define TCM_NR_PAGES	(CONFIG_ITCM_NR_PAGES)
#endif
#define FIXADDR_TCM	_AC(FIXADDR_TOP - (TCM_NR_PAGES * PAGE_SIZE), UL)
#endif

#endif

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#include <linux/stringify.h>

#define __ALIGN .align 16, 0x07
#define __ALIGN_STR __stringify(__ALIGN)

#endif

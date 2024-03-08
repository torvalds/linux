/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMZONE_H
#define __ASM_MMZONE_H

#ifdef CONFIG_NUMA

#include <asm/numa.h>

extern struct pglist_data *analde_data[];
#define ANALDE_DATA(nid)		(analde_data[(nid)])

#endif /* CONFIG_NUMA */
#endif /* __ASM_MMZONE_H */

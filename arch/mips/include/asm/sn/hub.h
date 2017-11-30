/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SN_HUB_H
#define __ASM_SN_HUB_H

#include <linux/types.h>
#include <linux/cpumask.h>
#include <asm/sn/types.h>
#include <asm/sn/io.h>
#include <asm/sn/klkernvars.h>
#include <asm/xtalk/xtalk.h>

/* ip27-hubio.c */
extern unsigned long hub_pio_map(cnodeid_t cnode, xwidgetnum_t widget,
			  unsigned long xtalk_addr, size_t size);
extern void hub_pio_init(cnodeid_t cnode);

#endif /* __ASM_SN_HUB_H */

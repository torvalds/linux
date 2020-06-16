// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/err.h>
#include <linux/kernel_stat.h>
#include <linux/pgtable.h>

#include <asm/kvm_book3s.h>
#include <asm/kvm_ppc.h>
#include <asm/hvcall.h>
#include <asm/xics.h>
#include <asm/debug.h>
#include <asm/synch.h>
#include <asm/cputhreads.h>
#include <asm/ppc-opcode.h>
#include <asm/pnv-pci.h>
#include <asm/opal.h>
#include <asm/smp.h>
#include <asm/asm-prototypes.h>
#include <asm/xive.h>
#include <asm/xive-regs.h>

#include "book3s_xive.h"

/* XXX */
#include <asm/udbg.h>
//#define DBG(fmt...) udbg_printf(fmt)
#define DBG(fmt...) do { } while(0)

static inline void __iomem *get_tima_phys(void)
{
	return local_paca->kvm_hstate.xive_tima_phys;
}

#undef XIVE_RUNTIME_CHECKS
#define X_PFX xive_rm_
#define X_STATIC
#define X_STAT_PFX stat_rm_
#define __x_tima		get_tima_phys()
#define __x_eoi_page(xd)	((void __iomem *)((xd)->eoi_page))
#define __x_trig_page(xd)	((void __iomem *)((xd)->trig_page))
#define __x_writeb	__raw_rm_writeb
#define __x_readw	__raw_rm_readw
#define __x_readq	__raw_rm_readq
#define __x_writeq	__raw_rm_writeq

#include "book3s_xive_template.c"

/* $Id: loadmmu.c,v 1.56 2000/02/08 20:24:21 davem Exp $
 * loadmmu.c:  This code loads up all the mm function pointers once the
 *             machine type has been determined.  It also sets the static
 *             mmu values such as PAGE_NONE, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/a.out.h>
#include <asm/mmu_context.h>
#include <asm/oplib.h>

struct ctx_list *ctx_list_pool;
struct ctx_list ctx_free;
struct ctx_list ctx_used;

unsigned int pg_iobits;

extern void ld_mmu_sun4c(void);
extern void ld_mmu_srmmu(void);

void __init load_mmu(void)
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		ld_mmu_sun4c();
		break;
	case sun4m:
	case sun4d:
		ld_mmu_srmmu();
		break;
	default:
		prom_printf("load_mmu: %d unsupported\n", (int)sparc_cpu_model);
		prom_halt();
	}
	btfixup();
}

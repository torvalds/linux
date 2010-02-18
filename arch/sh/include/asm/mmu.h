#ifndef __MMU_H
#define __MMU_H

/*
 * Privileged Space Mapping Buffer (PMB) definitions
 */
#define PMB_PASCR		0xff000070
#define PMB_IRMCR		0xff000078

#define PASCR_SE		0x80000000

#define PMB_ADDR		0xf6100000
#define PMB_DATA		0xf7100000

#define NR_PMB_ENTRIES		16

#define PMB_E_MASK		0x0000000f
#define PMB_E_SHIFT		8

#define PMB_PFN_MASK		0xff000000

#define PMB_SZ_16M		0x00000000
#define PMB_SZ_64M		0x00000010
#define PMB_SZ_128M		0x00000080
#define PMB_SZ_512M		0x00000090
#define PMB_SZ_MASK		PMB_SZ_512M
#define PMB_C			0x00000008
#define PMB_WT			0x00000001
#define PMB_UB			0x00000200
#define PMB_CACHE_MASK		(PMB_C | PMB_WT | PMB_UB)
#define PMB_V			0x00000100

#define PMB_NO_ENTRY		(-1)

#ifndef __ASSEMBLY__
#include <linux/errno.h>
#include <linux/threads.h>
#include <asm/page.h>

/* Default "unsigned long" context */
typedef unsigned long mm_context_id_t[NR_CPUS];

typedef struct {
#ifdef CONFIG_MMU
	mm_context_id_t		id;
	void			*vdso;
#else
	unsigned long		end_brk;
#endif
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long		exec_fdpic_loadmap;
	unsigned long		interp_fdpic_loadmap;
#endif
} mm_context_t;

#ifdef CONFIG_PMB
/* arch/sh/mm/pmb.c */
long pmb_remap(unsigned long virt, unsigned long phys,
	       unsigned long size, pgprot_t prot);
void pmb_unmap(unsigned long addr);
void pmb_init(void);
bool __in_29bit_mode(void);
#else
static inline long pmb_remap(unsigned long virt, unsigned long phys,
			     unsigned long size, pgprot_t prot)
{
	return -EINVAL;
}

#define pmb_unmap(addr)		do { } while (0)
#define pmb_init(addr)		do { } while (0)

#ifdef CONFIG_29BIT
#define __in_29bit_mode()	(1)
#else
#define __in_29bit_mode()	(0)
#endif

#endif /* CONFIG_PMB */
#endif /* __ASSEMBLY__ */

#endif /* __MMU_H */

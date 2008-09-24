#ifndef __MMU_H
#define __MMU_H

/* Default "unsigned long" context */
typedef unsigned long mm_context_id_t[NR_CPUS];

typedef struct {
#ifdef CONFIG_MMU
	mm_context_id_t		id;
	void			*vdso;
#else
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
#endif
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long		exec_fdpic_loadmap;
	unsigned long		interp_fdpic_loadmap;
#endif
} mm_context_t;

/*
 * Privileged Space Mapping Buffer (PMB) definitions
 */
#define PMB_PASCR		0xff000070
#define PMB_IRMCR		0xff000078

#define PMB_ADDR		0xf6100000
#define PMB_DATA		0xf7100000
#define PMB_ENTRY_MAX		16
#define PMB_E_MASK		0x0000000f
#define PMB_E_SHIFT		8

#define PMB_SZ_16M		0x00000000
#define PMB_SZ_64M		0x00000010
#define PMB_SZ_128M		0x00000080
#define PMB_SZ_512M		0x00000090
#define PMB_SZ_MASK		PMB_SZ_512M
#define PMB_C			0x00000008
#define PMB_WT			0x00000001
#define PMB_UB			0x00000200
#define PMB_V			0x00000100

#define PMB_NO_ENTRY		(-1)

struct pmb_entry;

struct pmb_entry {
	unsigned long vpn;
	unsigned long ppn;
	unsigned long flags;

	/*
	 * 0 .. NR_PMB_ENTRIES for specific entry selection, or
	 * PMB_NO_ENTRY to search for a free one
	 */
	int entry;

	struct pmb_entry *next;
	/* Adjacent entry link for contiguous multi-entry mappings */
	struct pmb_entry *link;
};

/* arch/sh/mm/pmb.c */
int __set_pmb_entry(unsigned long vpn, unsigned long ppn,
		    unsigned long flags, int *entry);
int set_pmb_entry(struct pmb_entry *pmbe);
void clear_pmb_entry(struct pmb_entry *pmbe);
struct pmb_entry *pmb_alloc(unsigned long vpn, unsigned long ppn,
			    unsigned long flags);
void pmb_free(struct pmb_entry *pmbe);
long pmb_remap(unsigned long virt, unsigned long phys,
	       unsigned long size, unsigned long flags);
void pmb_unmap(unsigned long addr);

#endif /* __MMU_H */


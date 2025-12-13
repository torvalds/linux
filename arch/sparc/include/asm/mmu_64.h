/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MMU_H
#define __MMU_H

#include <linux/const.h>
#include <asm/page.h>
#include <asm/hypervisor.h>

#define CTX_NR_BITS		13

#define TAG_CONTEXT_BITS	((_AC(1,UL) << CTX_NR_BITS) - _AC(1,UL))

/* UltraSPARC-III+ and later have a feature whereby you can
 * select what page size the various Data-TLB instances in the
 * chip.  In order to gracefully support this, we put the version
 * field in a spot outside of the areas of the context register
 * where this parameter is specified.
 */
#define CTX_VERSION_SHIFT	22
#define CTX_VERSION_MASK	((~0UL) << CTX_VERSION_SHIFT)

#define CTX_PGSZ_8KB		_AC(0x0,UL)
#define CTX_PGSZ_64KB		_AC(0x1,UL)
#define CTX_PGSZ_512KB		_AC(0x2,UL)
#define CTX_PGSZ_4MB		_AC(0x3,UL)
#define CTX_PGSZ_BITS		_AC(0x7,UL)
#define CTX_PGSZ0_NUC_SHIFT	61
#define CTX_PGSZ1_NUC_SHIFT	58
#define CTX_PGSZ0_SHIFT		16
#define CTX_PGSZ1_SHIFT		19
#define CTX_PGSZ_MASK		((CTX_PGSZ_BITS << CTX_PGSZ0_SHIFT) | \
				 (CTX_PGSZ_BITS << CTX_PGSZ1_SHIFT))

#define CTX_PGSZ_BASE	CTX_PGSZ_8KB
#define CTX_PGSZ_HUGE	CTX_PGSZ_4MB
#define CTX_PGSZ_KERN	CTX_PGSZ_4MB

/* Thus, when running on UltraSPARC-III+ and later, we use the following
 * PRIMARY_CONTEXT register values for the kernel context.
 */
#define CTX_CHEETAH_PLUS_NUC \
	((CTX_PGSZ_KERN << CTX_PGSZ0_NUC_SHIFT) | \
	 (CTX_PGSZ_BASE << CTX_PGSZ1_NUC_SHIFT))

#define CTX_CHEETAH_PLUS_CTX0 \
	((CTX_PGSZ_KERN << CTX_PGSZ0_SHIFT) | \
	 (CTX_PGSZ_BASE << CTX_PGSZ1_SHIFT))

/* If you want "the TLB context number" use CTX_NR_MASK.  If you
 * want "the bits I program into the context registers" use
 * CTX_HW_MASK.
 */
#define CTX_NR_MASK		TAG_CONTEXT_BITS
#define CTX_HW_MASK		(CTX_NR_MASK | CTX_PGSZ_MASK)

#define CTX_FIRST_VERSION	BIT(CTX_VERSION_SHIFT)
#define CTX_VALID(__ctx)	\
	 (!(((__ctx.sparc64_ctx_val) ^ tlb_context_cache) & CTX_VERSION_MASK))
#define CTX_HWBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_HW_MASK)
#define CTX_NRBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_NR_MASK)

#ifndef __ASSEMBLER__

#define TSB_ENTRY_ALIGNMENT	16

struct tsb {
	unsigned long tag;
	unsigned long pte;
} __attribute__((aligned(TSB_ENTRY_ALIGNMENT)));

void __tsb_insert(unsigned long ent, unsigned long tag, unsigned long pte);
void tsb_flush(unsigned long ent, unsigned long tag);
void tsb_init(struct tsb *tsb, unsigned long size);

struct tsb_config {
	struct tsb		*tsb;
	unsigned long		tsb_rss_limit;
	unsigned long		tsb_nentries;
	unsigned long		tsb_reg_val;
	unsigned long		tsb_map_vaddr;
	unsigned long		tsb_map_pte;
};

#define MM_TSB_BASE	0

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
#define MM_TSB_HUGE	1
#define MM_NUM_TSBS	2
#else
#define MM_NUM_TSBS	1
#endif

/* ADI tags are stored when a page is swapped out and the storage for
 * tags is allocated dynamically. There is a tag storage descriptor
 * associated with each set of tag storage pages. Tag storage descriptors
 * are allocated dynamically. Since kernel will allocate a full page for
 * each tag storage descriptor, we can store up to
 * PAGE_SIZE/sizeof(tag storage descriptor) descriptors on that page.
 */
typedef struct {
	unsigned long	start;		/* Start address for this tag storage */
	unsigned long	end;		/* Last address for tag storage */
	unsigned char	*tags;		/* Where the tags are */
	unsigned long	tag_users;	/* number of references to descriptor */
} tag_storage_desc_t;

typedef struct {
	spinlock_t		lock;
	unsigned long		sparc64_ctx_val;
	unsigned long		hugetlb_pte_count;
	unsigned long		thp_pte_count;
	struct tsb_config	tsb_block[MM_NUM_TSBS];
	struct hv_tsb_descr	tsb_descr[MM_NUM_TSBS];
	void			*vdso;
	bool			adi;
	tag_storage_desc_t	*tag_store;
	spinlock_t		tag_lock;
} mm_context_t;

#endif /* !__ASSEMBLER__ */

#define TSB_CONFIG_TSB		0x00
#define TSB_CONFIG_RSS_LIMIT	0x08
#define TSB_CONFIG_NENTRIES	0x10
#define TSB_CONFIG_REG_VAL	0x18
#define TSB_CONFIG_MAP_VADDR	0x20
#define TSB_CONFIG_MAP_PTE	0x28

#endif /* __MMU_H */

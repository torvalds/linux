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

#if defined(CONFIG_SPARC64_PAGE_SIZE_8KB)
#define CTX_PGSZ_BASE	CTX_PGSZ_8KB
#elif defined(CONFIG_SPARC64_PAGE_SIZE_64KB)
#define CTX_PGSZ_BASE	CTX_PGSZ_64KB
#else
#error No page size specified in kernel configuration
#endif

#if defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define CTX_PGSZ_HUGE		CTX_PGSZ_4MB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
#define CTX_PGSZ_HUGE		CTX_PGSZ_512KB
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define CTX_PGSZ_HUGE		CTX_PGSZ_64KB
#endif

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

#define CTX_FIRST_VERSION	((_AC(1,UL) << CTX_VERSION_SHIFT) + _AC(1,UL))
#define CTX_VALID(__ctx)	\
	 (!(((__ctx.sparc64_ctx_val) ^ tlb_context_cache) & CTX_VERSION_MASK))
#define CTX_HWBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_HW_MASK)
#define CTX_NRBITS(__ctx)	((__ctx.sparc64_ctx_val) & CTX_NR_MASK)

#ifndef __ASSEMBLY__

#define TSB_ENTRY_ALIGNMENT	16

struct tsb {
	unsigned long tag;
	unsigned long pte;
} __attribute__((aligned(TSB_ENTRY_ALIGNMENT)));

extern void __tsb_insert(unsigned long ent, unsigned long tag, unsigned long pte);
extern void tsb_flush(unsigned long ent, unsigned long tag);
extern void tsb_init(struct tsb *tsb, unsigned long size);

struct tsb_config {
	struct tsb		*tsb;
	unsigned long		tsb_rss_limit;
	unsigned long		tsb_nentries;
	unsigned long		tsb_reg_val;
	unsigned long		tsb_map_vaddr;
	unsigned long		tsb_map_pte;
};

#define MM_TSB_BASE	0

#ifdef CONFIG_HUGETLB_PAGE
#define MM_TSB_HUGE	1
#define MM_NUM_TSBS	2
#else
#define MM_NUM_TSBS	1
#endif

typedef struct {
	spinlock_t		lock;
	unsigned long		sparc64_ctx_val;
	unsigned long		huge_pte_count;
	struct tsb_config	tsb_block[MM_NUM_TSBS];
	struct hv_tsb_descr	tsb_descr[MM_NUM_TSBS];
} mm_context_t;

#endif /* !__ASSEMBLY__ */

#define TSB_CONFIG_TSB		0x00
#define TSB_CONFIG_RSS_LIMIT	0x08
#define TSB_CONFIG_NENTRIES	0x10
#define TSB_CONFIG_REG_VAL	0x18
#define TSB_CONFIG_MAP_VADDR	0x20
#define TSB_CONFIG_MAP_PTE	0x28

#endif /* __MMU_H */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>

#include <asm/kaiser.h>
#include <asm/tlbflush.h>	/* to verify its kaiser declarations */
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/desc.h>

#ifdef CONFIG_KAISER
__visible
DEFINE_PER_CPU_USER_MAPPED(unsigned long, unsafe_stack_register_backup);

/*
 * These can have bit 63 set, so we can not just use a plain "or"
 * instruction to get their value or'd into CR3.  It would take
 * another register.  So, we use a memory reference to these instead.
 *
 * This is also handy because systems that do not support PCIDs
 * just end up or'ing a 0 into their CR3, which does no harm.
 */
unsigned long x86_cr3_pcid_noflush __read_mostly;
DEFINE_PER_CPU(unsigned long, x86_cr3_pcid_user);

/*
 * At runtime, the only things we map are some things for CPU
 * hotplug, and stacks for new processes.  No two CPUs will ever
 * be populating the same addresses, so we only need to ensure
 * that we protect between two CPUs trying to allocate and
 * populate the same page table page.
 *
 * Only take this lock when doing a set_p[4um]d(), but it is not
 * needed for doing a set_pte().  We assume that only the *owner*
 * of a given allocation will be doing this for _their_
 * allocation.
 *
 * This ensures that once a system has been running for a while
 * and there have been stacks all over and these page tables
 * are fully populated, there will be no further acquisitions of
 * this lock.
 */
static DEFINE_SPINLOCK(shadow_table_allocation_lock);

/*
 * Returns -1 on error.
 */
static inline unsigned long get_pa_from_mapping(unsigned long vaddr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(vaddr);
	/*
	 * We made all the kernel PGDs present in kaiser_init().
	 * We expect them to stay that way.
	 */
	BUG_ON(pgd_none(*pgd));
	/*
	 * PGDs are either 512GB or 128TB on all x86_64
	 * configurations.  We don't handle these.
	 */
	BUG_ON(pgd_large(*pgd));

	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		WARN_ON_ONCE(1);
		return -1;
	}

	if (pud_large(*pud))
		return (pud_pfn(*pud) << PAGE_SHIFT) | (vaddr & ~PUD_PAGE_MASK);

	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		WARN_ON_ONCE(1);
		return -1;
	}

	if (pmd_large(*pmd))
		return (pmd_pfn(*pmd) << PAGE_SHIFT) | (vaddr & ~PMD_PAGE_MASK);

	pte = pte_offset_kernel(pmd, vaddr);
	if (pte_none(*pte)) {
		WARN_ON_ONCE(1);
		return -1;
	}

	return (pte_pfn(*pte) << PAGE_SHIFT) | (vaddr & ~PAGE_MASK);
}

/*
 * This is a relatively normal page table walk, except that it
 * also tries to allocate page tables pages along the way.
 *
 * Returns a pointer to a PTE on success, or NULL on failure.
 */
static pte_t *kaiser_pagetable_walk(unsigned long address, bool is_atomic)
{
	pmd_t *pmd;
	pud_t *pud;
	pgd_t *pgd = native_get_shadow_pgd(pgd_offset_k(address));
	gfp_t gfp = (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);

	if (is_atomic) {
		gfp &= ~GFP_KERNEL;
		gfp |= __GFP_HIGH | __GFP_ATOMIC;
	} else
		might_sleep();

	if (pgd_none(*pgd)) {
		WARN_ONCE(1, "All shadow pgds should have been populated");
		return NULL;
	}
	BUILD_BUG_ON(pgd_large(*pgd) != 0);

	pud = pud_offset(pgd, address);
	/* The shadow page tables do not use large mappings: */
	if (pud_large(*pud)) {
		WARN_ON(1);
		return NULL;
	}
	if (pud_none(*pud)) {
		unsigned long new_pmd_page = __get_free_page(gfp);
		if (!new_pmd_page)
			return NULL;
		spin_lock(&shadow_table_allocation_lock);
		if (pud_none(*pud)) {
			set_pud(pud, __pud(_KERNPG_TABLE | __pa(new_pmd_page)));
			__inc_zone_page_state(virt_to_page((void *)
						new_pmd_page), NR_KAISERTABLE);
		} else
			free_page(new_pmd_page);
		spin_unlock(&shadow_table_allocation_lock);
	}

	pmd = pmd_offset(pud, address);
	/* The shadow page tables do not use large mappings: */
	if (pmd_large(*pmd)) {
		WARN_ON(1);
		return NULL;
	}
	if (pmd_none(*pmd)) {
		unsigned long new_pte_page = __get_free_page(gfp);
		if (!new_pte_page)
			return NULL;
		spin_lock(&shadow_table_allocation_lock);
		if (pmd_none(*pmd)) {
			set_pmd(pmd, __pmd(_KERNPG_TABLE | __pa(new_pte_page)));
			__inc_zone_page_state(virt_to_page((void *)
						new_pte_page), NR_KAISERTABLE);
		} else
			free_page(new_pte_page);
		spin_unlock(&shadow_table_allocation_lock);
	}

	return pte_offset_kernel(pmd, address);
}

int kaiser_add_user_map(const void *__start_addr, unsigned long size,
			unsigned long flags)
{
	int ret = 0;
	pte_t *pte;
	unsigned long start_addr = (unsigned long )__start_addr;
	unsigned long address = start_addr & PAGE_MASK;
	unsigned long end_addr = PAGE_ALIGN(start_addr + size);
	unsigned long target_address;

	for (; address < end_addr; address += PAGE_SIZE) {
		target_address = get_pa_from_mapping(address);
		if (target_address == -1) {
			ret = -EIO;
			break;
		}
		pte = kaiser_pagetable_walk(address, false);
		if (!pte) {
			ret = -ENOMEM;
			break;
		}
		if (pte_none(*pte)) {
			set_pte(pte, __pte(flags | target_address));
		} else {
			pte_t tmp;
			set_pte(&tmp, __pte(flags | target_address));
			WARN_ON_ONCE(!pte_same(*pte, tmp));
		}
	}
	return ret;
}

static int kaiser_add_user_map_ptrs(const void *start, const void *end, unsigned long flags)
{
	unsigned long size = end - start;

	return kaiser_add_user_map(start, size, flags);
}

/*
 * Ensure that the top level of the (shadow) page tables are
 * entirely populated.  This ensures that all processes that get
 * forked have the same entries.  This way, we do not have to
 * ever go set up new entries in older processes.
 *
 * Note: we never free these, so there are no updates to them
 * after this.
 */
static void __init kaiser_init_all_pgds(void)
{
	pgd_t *pgd;
	int i = 0;

	pgd = native_get_shadow_pgd(pgd_offset_k((unsigned long )0));
	for (i = PTRS_PER_PGD / 2; i < PTRS_PER_PGD; i++) {
		pgd_t new_pgd;
		pud_t *pud = pud_alloc_one(&init_mm,
					   PAGE_OFFSET + i * PGDIR_SIZE);
		if (!pud) {
			WARN_ON(1);
			break;
		}
		inc_zone_page_state(virt_to_page(pud), NR_KAISERTABLE);
		new_pgd = __pgd(_KERNPG_TABLE |__pa(pud));
		/*
		 * Make sure not to stomp on some other pgd entry.
		 */
		if (!pgd_none(pgd[i])) {
			WARN_ON(1);
			continue;
		}
		set_pgd(pgd + i, new_pgd);
	}
}

#define kaiser_add_user_map_early(start, size, flags) do {	\
	int __ret = kaiser_add_user_map(start, size, flags);	\
	WARN_ON(__ret);						\
} while (0)

#define kaiser_add_user_map_ptrs_early(start, end, flags) do {		\
	int __ret = kaiser_add_user_map_ptrs(start, end, flags);	\
	WARN_ON(__ret);							\
} while (0)

/*
 * If anything in here fails, we will likely die on one of the
 * first kernel->user transitions and init will die.  But, we
 * will have most of the kernel up by then and should be able to
 * get a clean warning out of it.  If we BUG_ON() here, we run
 * the risk of being before we have good console output.
 */
void __init kaiser_init(void)
{
	int cpu;

	kaiser_init_all_pgds();

	for_each_possible_cpu(cpu) {
		void *percpu_vaddr = __per_cpu_user_mapped_start +
				     per_cpu_offset(cpu);
		unsigned long percpu_sz = __per_cpu_user_mapped_end -
					  __per_cpu_user_mapped_start;
		kaiser_add_user_map_early(percpu_vaddr, percpu_sz,
					  __PAGE_KERNEL);
	}

	/*
	 * Map the entry/exit text section, which is needed at
	 * switches from user to and from kernel.
	 */
	kaiser_add_user_map_ptrs_early(__entry_text_start, __entry_text_end,
				       __PAGE_KERNEL_RX);

#if defined(CONFIG_FUNCTION_GRAPH_TRACER) || defined(CONFIG_KASAN)
	kaiser_add_user_map_ptrs_early(__irqentry_text_start,
				       __irqentry_text_end,
				       __PAGE_KERNEL_RX);
#endif
	kaiser_add_user_map_early((void *)idt_descr.address,
				  sizeof(gate_desc) * NR_VECTORS,
				  __PAGE_KERNEL_RO);
#ifdef CONFIG_TRACING
	kaiser_add_user_map_early(&trace_idt_descr,
				  sizeof(trace_idt_descr),
				  __PAGE_KERNEL);
	kaiser_add_user_map_early(&trace_idt_table,
				  sizeof(gate_desc) * NR_VECTORS,
				  __PAGE_KERNEL);
#endif
	kaiser_add_user_map_early(&debug_idt_descr, sizeof(debug_idt_descr),
				  __PAGE_KERNEL);
	kaiser_add_user_map_early(&debug_idt_table,
				  sizeof(gate_desc) * NR_VECTORS,
				  __PAGE_KERNEL);

	kaiser_add_user_map_early(&x86_cr3_pcid_noflush,
				  sizeof(x86_cr3_pcid_noflush),
				  __PAGE_KERNEL);
}

/* Add a mapping to the shadow mapping, and synchronize the mappings */
int kaiser_add_mapping(unsigned long addr, unsigned long size, unsigned long flags)
{
	return kaiser_add_user_map((const void *)addr, size, flags);
}

void kaiser_remove_mapping(unsigned long start, unsigned long size)
{
	extern void unmap_pud_range_nofree(pgd_t *pgd,
				unsigned long start, unsigned long end);
	unsigned long end = start + size;
	unsigned long addr, next;
	pgd_t *pgd;

	pgd = native_get_shadow_pgd(pgd_offset_k(start));
	for (addr = start; addr < end; pgd++, addr = next) {
		next = pgd_addr_end(addr, end);
		unmap_pud_range_nofree(pgd, addr, next);
	}
}

/*
 * Page table pages are page-aligned.  The lower half of the top
 * level is used for userspace and the top half for the kernel.
 * This returns true for user pages that need to get copied into
 * both the user and kernel copies of the page tables, and false
 * for kernel pages that should only be in the kernel copy.
 */
static inline bool is_userspace_pgd(pgd_t *pgdp)
{
	return ((unsigned long)pgdp % PAGE_SIZE) < (PAGE_SIZE / 2);
}

pgd_t kaiser_set_shadow_pgd(pgd_t *pgdp, pgd_t pgd)
{
	/*
	 * Do we need to also populate the shadow pgd?  Check _PAGE_USER to
	 * skip cases like kexec and EFI which make temporary low mappings.
	 */
	if (pgd.pgd & _PAGE_USER) {
		if (is_userspace_pgd(pgdp)) {
			native_get_shadow_pgd(pgdp)->pgd = pgd.pgd;
			/*
			 * Even if the entry is *mapping* userspace, ensure
			 * that userspace can not use it.  This way, if we
			 * get out to userspace running on the kernel CR3,
			 * userspace will crash instead of running.
			 */
			pgd.pgd |= _PAGE_NX;
		}
	} else if (!pgd.pgd) {
		/*
		 * pgd_clear() cannot check _PAGE_USER, and is even used to
		 * clear corrupted pgd entries: so just rely on cases like
		 * kexec and EFI never to be using pgd_clear().
		 */
		if (!WARN_ON_ONCE((unsigned long)pgdp & PAGE_SIZE) &&
		    is_userspace_pgd(pgdp))
			native_get_shadow_pgd(pgdp)->pgd = pgd.pgd;
	}
	return pgd;
}

void kaiser_setup_pcid(void)
{
	unsigned long kern_cr3 = 0;
	unsigned long user_cr3 = KAISER_SHADOW_PGD_OFFSET;

	if (this_cpu_has(X86_FEATURE_PCID)) {
		kern_cr3 |= X86_CR3_PCID_KERN_NOFLUSH;
		user_cr3 |= X86_CR3_PCID_USER_NOFLUSH;
	}
	/*
	 * These variables are used by the entry/exit
	 * code to change PCID and pgd and TLB flushing.
	 */
	x86_cr3_pcid_noflush = kern_cr3;
	this_cpu_write(x86_cr3_pcid_user, user_cr3);
}

/*
 * Make a note that this cpu will need to flush USER tlb on return to user.
 * Caller checks whether this_cpu_has(X86_FEATURE_PCID) before calling:
 * if cpu does not, then the NOFLUSH bit will never have been set.
 */
void kaiser_flush_tlb_on_return_to_user(void)
{
	this_cpu_write(x86_cr3_pcid_user,
			X86_CR3_PCID_USER_FLUSH | KAISER_SHADOW_PGD_OFFSET);
}
EXPORT_SYMBOL(kaiser_flush_tlb_on_return_to_user);
#endif /* CONFIG_KAISER */

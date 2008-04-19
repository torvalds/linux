/*
 * Handle caching attributes in page tables (PAT)
 *
 * Authors: Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *          Suresh B Siddha <suresh.b.siddha@intel.com>
 *
 * Loosely based on earlier PAT patchset from Eric Biederman and Andi Kleen.
 */

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/fs.h>

#include <asm/msr.h>
#include <asm/tlbflush.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/pat.h>
#include <asm/e820.h>
#include <asm/cacheflush.h>
#include <asm/fcntl.h>
#include <asm/mtrr.h>

int pat_wc_enabled = 1;

static u64 __read_mostly boot_pat_state;

static int nopat(char *str)
{
	pat_wc_enabled = 0;
	printk(KERN_INFO "x86: PAT support disabled.\n");

	return 0;
}
early_param("nopat", nopat);

static int pat_known_cpu(void)
{
	if (!pat_wc_enabled)
		return 0;

	if (cpu_has_pat)
		return 1;

	pat_wc_enabled = 0;
	printk(KERN_INFO "CPU and/or kernel does not support PAT.\n");
	return 0;
}

enum {
	PAT_UC = 0,		/* uncached */
	PAT_WC = 1,		/* Write combining */
	PAT_WT = 4,		/* Write Through */
	PAT_WP = 5,		/* Write Protected */
	PAT_WB = 6,		/* Write Back (default) */
	PAT_UC_MINUS = 7,	/* UC, but can be overriden by MTRR */
};

#define PAT(x,y)	((u64)PAT_ ## y << ((x)*8))

void pat_init(void)
{
	u64 pat;

#ifndef CONFIG_X86_PAT
	nopat(NULL);
#endif

	/* Boot CPU enables PAT based on CPU feature */
	if (!smp_processor_id() && !pat_known_cpu())
		return;

	/* APs enable PAT iff boot CPU has enabled it before */
	if (smp_processor_id() && !pat_wc_enabled)
		return;

	/* Set PWT to Write-Combining. All other bits stay the same */
	/*
	 * PTE encoding used in Linux:
	 *      PAT
	 *      |PCD
	 *      ||PWT
	 *      |||
	 *      000 WB		_PAGE_CACHE_WB
	 *      001 WC		_PAGE_CACHE_WC
	 *      010 UC-		_PAGE_CACHE_UC_MINUS
	 *      011 UC		_PAGE_CACHE_UC
	 * PAT bit unused
	 */
	pat = PAT(0,WB) | PAT(1,WC) | PAT(2,UC_MINUS) | PAT(3,UC) |
	      PAT(4,WB) | PAT(5,WC) | PAT(6,UC_MINUS) | PAT(7,UC);

	/* Boot CPU check */
	if (!smp_processor_id()) {
		rdmsrl(MSR_IA32_CR_PAT, boot_pat_state);
	}

	wrmsrl(MSR_IA32_CR_PAT, pat);
	printk(KERN_INFO "x86 PAT enabled: cpu %d, old 0x%Lx, new 0x%Lx\n",
	       smp_processor_id(), boot_pat_state, pat);
}

#undef PAT

static char *cattr_name(unsigned long flags)
{
	switch (flags & _PAGE_CACHE_MASK) {
		case _PAGE_CACHE_UC:		return "uncached";
		case _PAGE_CACHE_UC_MINUS:	return "uncached-minus";
		case _PAGE_CACHE_WB:		return "write-back";
		case _PAGE_CACHE_WC:		return "write-combining";
		default:			return "broken";
	}
}

/*
 * The global memtype list keeps track of memory type for specific
 * physical memory areas. Conflicting memory types in different
 * mappings can cause CPU cache corruption. To avoid this we keep track.
 *
 * The list is sorted based on starting address and can contain multiple
 * entries for each address (this allows reference counting for overlapping
 * areas). All the aliases have the same cache attributes of course.
 * Zero attributes are represented as holes.
 *
 * Currently the data structure is a list because the number of mappings
 * are expected to be relatively small. If this should be a problem
 * it could be changed to a rbtree or similar.
 *
 * memtype_lock protects the whole list.
 */

struct memtype {
	u64 start;
	u64 end;
	unsigned long type;
	struct list_head nd;
};

static LIST_HEAD(memtype_list);
static DEFINE_SPINLOCK(memtype_lock); 	/* protects memtype list */

/*
 * Does intersection of PAT memory type and MTRR memory type and returns
 * the resulting memory type as PAT understands it.
 * (Type in pat and mtrr will not have same value)
 * The intersection is based on "Effective Memory Type" tables in IA-32
 * SDM vol 3a
 */
static int pat_x_mtrr_type(u64 start, u64 end, unsigned long prot,
				unsigned long *ret_prot)
{
	unsigned long pat_type;
	u8 mtrr_type;

	mtrr_type = mtrr_type_lookup(start, end);
	if (mtrr_type == 0xFF) {		/* MTRR not enabled */
		*ret_prot = prot;
		return 0;
	}
	if (mtrr_type == 0xFE) {		/* MTRR match error */
		*ret_prot = _PAGE_CACHE_UC;
		return -1;
	}
	if (mtrr_type != MTRR_TYPE_UNCACHABLE &&
	    mtrr_type != MTRR_TYPE_WRBACK &&
	    mtrr_type != MTRR_TYPE_WRCOMB) {	/* MTRR type unhandled */
		*ret_prot = _PAGE_CACHE_UC;
		return -1;
	}

	pat_type = prot & _PAGE_CACHE_MASK;
	prot &= (~_PAGE_CACHE_MASK);

	/* Currently doing intersection by hand. Optimize it later. */
	if (pat_type == _PAGE_CACHE_WC) {
		*ret_prot = prot | _PAGE_CACHE_WC;
	} else if (pat_type == _PAGE_CACHE_UC_MINUS) {
		*ret_prot = prot | _PAGE_CACHE_UC_MINUS;
	} else if (pat_type == _PAGE_CACHE_UC ||
	           mtrr_type == MTRR_TYPE_UNCACHABLE) {
		*ret_prot = prot | _PAGE_CACHE_UC;
	} else if (mtrr_type == MTRR_TYPE_WRCOMB) {
		*ret_prot = prot | _PAGE_CACHE_WC;
	} else {
		*ret_prot = prot | _PAGE_CACHE_WB;
	}

	return 0;
}

int reserve_memtype(u64 start, u64 end, unsigned long req_type,
			unsigned long *ret_type)
{
	struct memtype *new_entry = NULL;
	struct memtype *parse;
	unsigned long actual_type;
	int err = 0;

	/* Only track when pat_wc_enabled */
	if (!pat_wc_enabled) {
		if (ret_type)
			*ret_type = req_type;

		return 0;
	}

	/* Low ISA region is always mapped WB in page table. No need to track */
	if (start >= ISA_START_ADDRESS && (end - 1) <= ISA_END_ADDRESS) {
		if (ret_type)
			*ret_type = _PAGE_CACHE_WB;

		return 0;
	}

	req_type &= _PAGE_CACHE_MASK;
	err = pat_x_mtrr_type(start, end, req_type, &actual_type);
	if (err) {
		if (ret_type)
			*ret_type = actual_type;

		return -EINVAL;
	}

	new_entry  = kmalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!new_entry)
		return -ENOMEM;

	new_entry->start = start;
	new_entry->end = end;
	new_entry->type = actual_type;

	if (ret_type)
		*ret_type = actual_type;

	spin_lock(&memtype_lock);

	/* Search for existing mapping that overlaps the current range */
	list_for_each_entry(parse, &memtype_list, nd) {
		struct memtype *saved_ptr;

		if (parse->start >= end) {
			printk("New Entry\n");
			list_add(&new_entry->nd, parse->nd.prev);
			new_entry = NULL;
			break;
		}

		if (start <= parse->start && end >= parse->start) {
			if (actual_type != parse->type && ret_type) {
				actual_type = parse->type;
				*ret_type = actual_type;
				new_entry->type = actual_type;
			}

			if (actual_type != parse->type) {
				printk(
		KERN_INFO "%s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
					current->comm, current->pid,
					start, end,
					cattr_name(actual_type),
					cattr_name(parse->type));
				err = -EBUSY;
				break;
			}

			saved_ptr = parse;
			/*
			 * Check to see whether the request overlaps more
			 * than one entry in the list
			 */
			list_for_each_entry_continue(parse, &memtype_list, nd) {
				if (end <= parse->start) {
					break;
				}

				if (actual_type != parse->type) {
					printk(
		KERN_INFO "%s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
						current->comm, current->pid,
						start, end,
						cattr_name(actual_type),
						cattr_name(parse->type));
					err = -EBUSY;
					break;
				}
			}

			if (err) {
				break;
			}

			printk("Overlap at 0x%Lx-0x%Lx\n",
			       saved_ptr->start, saved_ptr->end);
			/* No conflict. Go ahead and add this new entry */
			list_add(&new_entry->nd, saved_ptr->nd.prev);
			new_entry = NULL;
			break;
		}

		if (start < parse->end) {
			if (actual_type != parse->type && ret_type) {
				actual_type = parse->type;
				*ret_type = actual_type;
				new_entry->type = actual_type;
			}

			if (actual_type != parse->type) {
				printk(
		KERN_INFO "%s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
					current->comm, current->pid,
					start, end,
					cattr_name(actual_type),
					cattr_name(parse->type));
				err = -EBUSY;
				break;
			}

			saved_ptr = parse;
			/*
			 * Check to see whether the request overlaps more
			 * than one entry in the list
			 */
			list_for_each_entry_continue(parse, &memtype_list, nd) {
				if (end <= parse->start) {
					break;
				}

				if (actual_type != parse->type) {
					printk(
		KERN_INFO "%s:%d conflicting memory types %Lx-%Lx %s<->%s\n",
						current->comm, current->pid,
						start, end,
						cattr_name(actual_type),
						cattr_name(parse->type));
					err = -EBUSY;
					break;
				}
			}

			if (err) {
				break;
			}

			printk("Overlap at 0x%Lx-0x%Lx\n",
			       saved_ptr->start, saved_ptr->end);
			/* No conflict. Go ahead and add this new entry */
			list_add(&new_entry->nd, &saved_ptr->nd);
			new_entry = NULL;
			break;
		}
	}

	if (err) {
		printk(
	"reserve_memtype failed 0x%Lx-0x%Lx, track %s, req %s\n",
			start, end, cattr_name(new_entry->type),
			cattr_name(req_type));
		kfree(new_entry);
		spin_unlock(&memtype_lock);
		return err;
	}

	if (new_entry) {
		/* No conflict. Not yet added to the list. Add to the tail */
		list_add_tail(&new_entry->nd, &memtype_list);
		printk("New Entry\n");
  	}

	if (ret_type) {
		printk(
	"reserve_memtype added 0x%Lx-0x%Lx, track %s, req %s, ret %s\n",
			start, end, cattr_name(actual_type),
			cattr_name(req_type), cattr_name(*ret_type));
	} else {
		printk(
	"reserve_memtype added 0x%Lx-0x%Lx, track %s, req %s\n",
			start, end, cattr_name(actual_type),
			cattr_name(req_type));
	}

	spin_unlock(&memtype_lock);
	return err;
}

int free_memtype(u64 start, u64 end)
{
	struct memtype *ml;
	int err = -EINVAL;

	/* Only track when pat_wc_enabled */
	if (!pat_wc_enabled) {
		return 0;
	}

	/* Low ISA region is always mapped WB. No need to track */
	if (start >= ISA_START_ADDRESS && end <= ISA_END_ADDRESS) {
		return 0;
	}

	spin_lock(&memtype_lock);
	list_for_each_entry(ml, &memtype_list, nd) {
		if (ml->start == start && ml->end == end) {
			list_del(&ml->nd);
			kfree(ml);
			err = 0;
			break;
		}
	}
	spin_unlock(&memtype_lock);

	if (err) {
		printk(KERN_DEBUG "%s:%d freeing invalid memtype %Lx-%Lx\n",
			current->comm, current->pid, start, end);
	}

	printk( "free_memtype request 0x%Lx-0x%Lx\n", start, end);
	return err;
}


/*
 * self test for change_page_attr.
 *
 * Clears the global bit on random pages in the direct mapping, then reverts
 * and compares page tables forwards and afterwards.
 */
#include <linux/bootmem.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/kdebug.h>

enum {
	NTEST			= 4000,
#ifdef CONFIG_X86_64
	LPS			= (1 << PMD_SHIFT),
#elif defined(CONFIG_X86_PAE)
	LPS			= (1 << PMD_SHIFT),
#else
	LPS			= (1 << 22),
#endif
	GPS			= (1<<30)
};

struct split_state {
	long lpg, gpg, spg, exec;
	long min_exec, max_exec;
};

static __init int print_split(struct split_state *s)
{
	long i, expected, missed = 0;
	int printed = 0;
	int err = 0;

	s->lpg = s->gpg = s->spg = s->exec = 0;
	s->min_exec = ~0UL;
	s->max_exec = 0;
	for (i = 0; i < max_pfn_mapped; ) {
		unsigned long addr = (unsigned long)__va(i << PAGE_SHIFT);
		unsigned int level;
		pte_t *pte;

		pte = lookup_address(addr, &level);
		if (!pte) {
			if (!printed) {
				dump_pagetable(addr);
				printk(KERN_INFO "CPA %lx no pte level %d\n",
					addr, level);
				printed = 1;
			}
			missed++;
			i++;
			continue;
		}

		if (level == PG_LEVEL_1G && sizeof(long) == 8) {
			s->gpg++;
			i += GPS/PAGE_SIZE;
		} else if (level == PG_LEVEL_2M) {
			if (!(pte_val(*pte) & _PAGE_PSE)) {
				printk(KERN_ERR
					"%lx level %d but not PSE %Lx\n",
					addr, level, (u64)pte_val(*pte));
				err = 1;
			}
			s->lpg++;
			i += LPS/PAGE_SIZE;
		} else {
			s->spg++;
			i++;
		}
		if (!(pte_val(*pte) & _PAGE_NX)) {
			s->exec++;
			if (addr < s->min_exec)
				s->min_exec = addr;
			if (addr > s->max_exec)
				s->max_exec = addr;
		}
	}
	printk(KERN_INFO
		"CPA mapping 4k %lu large %lu gb %lu x %lu[%lx-%lx] miss %lu\n",
		s->spg, s->lpg, s->gpg, s->exec,
		s->min_exec != ~0UL ? s->min_exec : 0, s->max_exec, missed);

	expected = (s->gpg*GPS + s->lpg*LPS)/PAGE_SIZE + s->spg + missed;
	if (expected != i) {
		printk(KERN_ERR "CPA max_pfn_mapped %lu but expected %lu\n",
			max_pfn_mapped, expected);
		return 1;
	}
	return err;
}

static unsigned long __initdata addr[NTEST];
static unsigned int __initdata len[NTEST];

/* Change the global bit on random pages in the direct mapping */
static __init int exercise_pageattr(void)
{
	struct split_state sa, sb, sc;
	unsigned long *bm;
	pte_t *pte, pte0;
	int failed = 0;
	unsigned int level;
	int i, k;
	int err;

	printk(KERN_INFO "CPA exercising pageattr\n");

	bm = vmalloc((max_pfn_mapped + 7) / 8);
	if (!bm) {
		printk(KERN_ERR "CPA Cannot vmalloc bitmap\n");
		return -ENOMEM;
	}
	memset(bm, 0, (max_pfn_mapped + 7) / 8);

	failed += print_split(&sa);
	srandom32(100);

	for (i = 0; i < NTEST; i++) {
		unsigned long pfn = random32() % max_pfn_mapped;

		addr[i] = (unsigned long)__va(pfn << PAGE_SHIFT);
		len[i] = random32() % 100;
		len[i] = min_t(unsigned long, len[i], max_pfn_mapped - pfn - 1);

		if (len[i] == 0)
			len[i] = 1;

		pte = NULL;
		pte0 = pfn_pte(0, __pgprot(0)); /* shut gcc up */

		for (k = 0; k < len[i]; k++) {
			pte = lookup_address(addr[i] + k*PAGE_SIZE, &level);
			if (!pte || pgprot_val(pte_pgprot(*pte)) == 0 ||
			    !(pte_val(*pte) & _PAGE_PRESENT)) {
				addr[i] = 0;
				break;
			}
			if (k == 0) {
				pte0 = *pte;
			} else {
				if (pgprot_val(pte_pgprot(*pte)) !=
					pgprot_val(pte_pgprot(pte0))) {
					len[i] = k;
					break;
				}
			}
			if (test_bit(pfn + k, bm)) {
				len[i] = k;
				break;
			}
			__set_bit(pfn + k, bm);
		}
		if (!addr[i] || !pte || !k) {
			addr[i] = 0;
			continue;
		}

		err = change_page_attr_clear(addr[i], len[i],
					       __pgprot(_PAGE_GLOBAL));
		if (err < 0) {
			printk(KERN_ERR "CPA %d failed %d\n", i, err);
			failed++;
		}

		pte = lookup_address(addr[i], &level);
		if (!pte || pte_global(*pte) || pte_huge(*pte)) {
			printk(KERN_ERR "CPA %lx: bad pte %Lx\n", addr[i],
				pte ? (u64)pte_val(*pte) : 0ULL);
			failed++;
		}
		if (level != PG_LEVEL_4K) {
			printk(KERN_ERR "CPA %lx: unexpected level %d\n",
				addr[i], level);
			failed++;
		}

	}
	vfree(bm);

	failed += print_split(&sb);

	printk(KERN_INFO "CPA reverting everything\n");
	for (i = 0; i < NTEST; i++) {
		if (!addr[i])
			continue;
		pte = lookup_address(addr[i], &level);
		if (!pte) {
			printk(KERN_ERR "CPA lookup of %lx failed\n", addr[i]);
			failed++;
			continue;
		}
		err = change_page_attr_set(addr[i], len[i],
					     __pgprot(_PAGE_GLOBAL));
		if (err < 0) {
			printk(KERN_ERR "CPA reverting failed: %d\n", err);
			failed++;
		}
		pte = lookup_address(addr[i], &level);
		if (!pte || !pte_global(*pte)) {
			printk(KERN_ERR "CPA %lx: bad pte after revert %Lx\n",
				addr[i], pte ? (u64)pte_val(*pte) : 0ULL);
			failed++;
		}

	}

	failed += print_split(&sc);

	if (failed) {
		printk(KERN_ERR "CPA selftests NOT PASSED. Please report.\n");
		WARN_ON(1);
	} else {
		printk(KERN_INFO "CPA selftests PASSED\n");
	}

	return 0;
}
module_init(exercise_pageattr);

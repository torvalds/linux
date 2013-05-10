/*
 * self test for change_page_attr.
 *
 * Clears the a test pte bit on random pages in the direct mapping,
 * then reverts and compares page tables forwards and afterwards.
 */
#include <linux/bootmem.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/kdebug.h>

/*
 * Only print the results of the first pass:
 */
static __read_mostly int print = 1;

enum {
	NTEST			= 400,
#ifdef CONFIG_X86_64
	LPS			= (1 << PMD_SHIFT),
#elif defined(CONFIG_X86_PAE)
	LPS			= (1 << PMD_SHIFT),
#else
	LPS			= (1 << 22),
#endif
	GPS			= (1<<30)
};

#define PAGE_CPA_TEST	__pgprot(_PAGE_CPA_TEST)

static int pte_testbit(pte_t pte)
{
	return pte_flags(pte) & _PAGE_UNUSED1;
}

struct split_state {
	long lpg, gpg, spg, exec;
	long min_exec, max_exec;
};

static int print_split(struct split_state *s)
{
	long i, expected, missed = 0;
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
	if (print) {
		printk(KERN_INFO
			" 4k %lu large %lu gb %lu x %lu[%lx-%lx] miss %lu\n",
			s->spg, s->lpg, s->gpg, s->exec,
			s->min_exec != ~0UL ? s->min_exec : 0,
			s->max_exec, missed);
	}

	expected = (s->gpg*GPS + s->lpg*LPS)/PAGE_SIZE + s->spg + missed;
	if (expected != i) {
		printk(KERN_ERR "CPA max_pfn_mapped %lu but expected %lu\n",
			max_pfn_mapped, expected);
		return 1;
	}
	return err;
}

static unsigned long addr[NTEST];
static unsigned int len[NTEST];

/* Change the global bit on random pages in the direct mapping */
static int pageattr_test(void)
{
	struct split_state sa, sb, sc;
	unsigned long *bm;
	pte_t *pte, pte0;
	int failed = 0;
	unsigned int level;
	int i, k;
	int err;
	unsigned long test_addr;

	if (print)
		printk(KERN_INFO "CPA self-test:\n");

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

		test_addr = addr[i];
		err = change_page_attr_set(&test_addr, len[i], PAGE_CPA_TEST, 0);
		if (err < 0) {
			printk(KERN_ERR "CPA %d failed %d\n", i, err);
			failed++;
		}

		pte = lookup_address(addr[i], &level);
		if (!pte || !pte_testbit(*pte) || pte_huge(*pte)) {
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

	for (i = 0; i < NTEST; i++) {
		if (!addr[i])
			continue;
		pte = lookup_address(addr[i], &level);
		if (!pte) {
			printk(KERN_ERR "CPA lookup of %lx failed\n", addr[i]);
			failed++;
			continue;
		}
		test_addr = addr[i];
		err = change_page_attr_clear(&test_addr, len[i], PAGE_CPA_TEST, 0);
		if (err < 0) {
			printk(KERN_ERR "CPA reverting failed: %d\n", err);
			failed++;
		}
		pte = lookup_address(addr[i], &level);
		if (!pte || pte_testbit(*pte)) {
			printk(KERN_ERR "CPA %lx: bad pte after revert %Lx\n",
				addr[i], pte ? (u64)pte_val(*pte) : 0ULL);
			failed++;
		}

	}

	failed += print_split(&sc);

	if (failed) {
		WARN(1, KERN_ERR "NOT PASSED. Please report.\n");
		return -EINVAL;
	} else {
		if (print)
			printk(KERN_INFO "ok.\n");
	}

	return 0;
}

static int do_pageattr_test(void *__unused)
{
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(HZ*30);
		if (pageattr_test() < 0)
			break;
		if (print)
			print--;
	}
	return 0;
}

static int start_pageattr_test(void)
{
	struct task_struct *p;

	p = kthread_create(do_pageattr_test, NULL, "pageattr-test");
	if (!IS_ERR(p))
		wake_up_process(p);
	else
		WARN_ON(1);

	return 0;
}

module_init(start_pageattr_test);

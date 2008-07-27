#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/pfn.h>

#include <asm/e820.h>

static void __init memtest(unsigned long start_phys, unsigned long size,
				 unsigned pattern)
{
	unsigned long i;
	unsigned long *start;
	unsigned long start_bad;
	unsigned long last_bad;
	unsigned long val;
	unsigned long start_phys_aligned;
	unsigned long count;
	unsigned long incr;

	switch (pattern) {
	case 0:
		val = 0UL;
		break;
	case 1:
		val = -1UL;
		break;
	case 2:
#ifdef CONFIG_X86_64
		val = 0x5555555555555555UL;
#else
		val = 0x55555555UL;
#endif
		break;
	case 3:
#ifdef CONFIG_X86_64
		val = 0xaaaaaaaaaaaaaaaaUL;
#else
		val = 0xaaaaaaaaUL;
#endif
		break;
	default:
		return;
	}

	incr = sizeof(unsigned long);
	start_phys_aligned = ALIGN(start_phys, incr);
	count = (size - (start_phys_aligned - start_phys))/incr;
	start = __va(start_phys_aligned);
	start_bad = 0;
	last_bad = 0;

	for (i = 0; i < count; i++)
		start[i] = val;
	for (i = 0; i < count; i++, start++, start_phys_aligned += incr) {
		if (*start != val) {
			if (start_phys_aligned == last_bad + incr) {
				last_bad += incr;
			} else {
				if (start_bad) {
					printk(KERN_CONT "\n  %010lx bad mem addr %010lx - %010lx reserved",
						val, start_bad, last_bad + incr);
					reserve_early(start_bad, last_bad - start_bad, "BAD RAM");
				}
				start_bad = last_bad = start_phys_aligned;
			}
		}
	}
	if (start_bad) {
		printk(KERN_CONT "\n  %016lx bad mem addr %010lx - %010lx reserved",
			val, start_bad, last_bad + incr);
		reserve_early(start_bad, last_bad - start_bad, "BAD RAM");
	}

}

/* default is disabled */
static int memtest_pattern __initdata;

static int __init parse_memtest(char *arg)
{
	if (arg)
		memtest_pattern = simple_strtoul(arg, NULL, 0);
	return 0;
}

early_param("memtest", parse_memtest);

void __init early_memtest(unsigned long start, unsigned long end)
{
	u64 t_start, t_size;
	unsigned pattern;

	if (!memtest_pattern)
		return;

	printk(KERN_INFO "early_memtest: pattern num %d", memtest_pattern);
	for (pattern = 0; pattern < memtest_pattern; pattern++) {
		t_start = start;
		t_size = 0;
		while (t_start < end) {
			t_start = find_e820_area_size(t_start, &t_size, 1);

			/* done ? */
			if (t_start >= end)
				break;
			if (t_start + t_size > end)
				t_size = end - t_start;

			printk(KERN_CONT "\n  %010llx - %010llx pattern %d",
				(unsigned long long)t_start,
				(unsigned long long)t_start + t_size, pattern);

			memtest(t_start, t_size, pattern);

			t_start += t_size;
		}
	}
	printk(KERN_CONT "\n");
}

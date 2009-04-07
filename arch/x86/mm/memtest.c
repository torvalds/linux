#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/pfn.h>

#include <asm/e820.h>

static u64 patterns[] __initdata = {
	0,
	0xffffffffffffffffULL,
	0x5555555555555555ULL,
	0xaaaaaaaaaaaaaaaaULL,
	0x1111111111111111ULL,
	0x2222222222222222ULL,
	0x4444444444444444ULL,
	0x8888888888888888ULL,
	0x3333333333333333ULL,
	0x6666666666666666ULL,
	0x9999999999999999ULL,
	0xccccccccccccccccULL,
	0x7777777777777777ULL,
	0xbbbbbbbbbbbbbbbbULL,
	0xddddddddddddddddULL,
	0xeeeeeeeeeeeeeeeeULL,
	0x7a6c7258554e494cULL, /* yeah ;-) */
};

static void __init reserve_bad_mem(u64 pattern, u64 start_bad, u64 end_bad)
{
	printk(KERN_INFO "  %016llx bad mem addr %010llx - %010llx reserved\n",
	       (unsigned long long) pattern,
	       (unsigned long long) start_bad,
	       (unsigned long long) end_bad);
	reserve_early(start_bad, end_bad, "BAD RAM");
}

static void __init memtest(u64 pattern, u64 start_phys, u64 size)
{
	u64 i, count;
	u64 *start;
	u64 start_bad, last_bad;
	u64 start_phys_aligned;
	size_t incr;

	incr = sizeof(pattern);
	start_phys_aligned = ALIGN(start_phys, incr);
	count = (size - (start_phys_aligned - start_phys))/incr;
	start = __va(start_phys_aligned);
	start_bad = 0;
	last_bad = 0;

	for (i = 0; i < count; i++)
		start[i] = pattern;
	for (i = 0; i < count; i++, start++, start_phys_aligned += incr) {
		if (*start == pattern)
			continue;
		if (start_phys_aligned == last_bad + incr) {
			last_bad += incr;
			continue;
		}
		if (start_bad)
			reserve_bad_mem(pattern, start_bad, last_bad + incr);
		start_bad = last_bad = start_phys_aligned;
	}
	if (start_bad)
		reserve_bad_mem(pattern, start_bad, last_bad + incr);
}

static void __init do_one_pass(u64 pattern, u64 start, u64 end)
{
	u64 size = 0;

	while (start < end) {
		start = find_e820_area_size(start, &size, 1);

		/* done ? */
		if (start >= end)
			break;
		if (start + size > end)
			size = end - start;

		printk(KERN_INFO "  %010llx - %010llx pattern %016llx\n",
		       (unsigned long long) start,
		       (unsigned long long) start + size,
		       (unsigned long long) cpu_to_be64(pattern));
		memtest(pattern, start, size);

		start += size;
	}
}

/* default is disabled */
static int memtest_pattern __initdata;

static int __init parse_memtest(char *arg)
{
	if (arg)
		memtest_pattern = simple_strtoul(arg, NULL, 0);
	else
		memtest_pattern = ARRAY_SIZE(patterns);

	return 0;
}

early_param("memtest", parse_memtest);

void __init early_memtest(unsigned long start, unsigned long end)
{
	unsigned int i;
	unsigned int idx = 0;

	if (!memtest_pattern)
		return;

	printk(KERN_INFO "early_memtest: # of tests: %d\n", memtest_pattern);
	for (i = 0; i < memtest_pattern; i++) {
		idx = i % ARRAY_SIZE(patterns);
		do_one_pass(patterns[idx], start, end);
	}

	if (idx > 0) {
		printk(KERN_INFO "early_memtest: wipe out "
		       "test pattern from memory\n");
		/* additional test with pattern 0 will do this */
		do_one_pass(0, start, end);
	}
}

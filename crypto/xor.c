// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xor.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000,
 * Ingo Molnar, Matti Aarnio, Jakub Jelinek, Richard Henderson.
 *
 * Dispatch optimized RAID-5 checksumming functions.
 */

#define BH_TRACE 0
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/raid/xor.h>
#include <linux/jiffies.h>
#include <linux/preempt.h>
#include <asm/xor.h>

#ifndef XOR_SELECT_TEMPLATE
#define XOR_SELECT_TEMPLATE(x) (x)
#endif

/* The xor routines to use.  */
static struct xor_block_template *active_template;

void
xor_blocks(unsigned int src_count, unsigned int bytes, void *dest, void **srcs)
{
	unsigned long *p1, *p2, *p3, *p4;

	p1 = (unsigned long *) srcs[0];
	if (src_count == 1) {
		active_template->do_2(bytes, dest, p1);
		return;
	}

	p2 = (unsigned long *) srcs[1];
	if (src_count == 2) {
		active_template->do_3(bytes, dest, p1, p2);
		return;
	}

	p3 = (unsigned long *) srcs[2];
	if (src_count == 3) {
		active_template->do_4(bytes, dest, p1, p2, p3);
		return;
	}

	p4 = (unsigned long *) srcs[3];
	active_template->do_5(bytes, dest, p1, p2, p3, p4);
}
EXPORT_SYMBOL(xor_blocks);

/* Set of all registered templates.  */
static struct xor_block_template *__initdata template_list;

#ifndef MODULE
static void __init do_xor_register(struct xor_block_template *tmpl)
{
	tmpl->next = template_list;
	template_list = tmpl;
}

static int __init register_xor_blocks(void)
{
	active_template = XOR_SELECT_TEMPLATE(NULL);

	if (!active_template) {
#define xor_speed	do_xor_register
		// register all the templates and pick the first as the default
		XOR_TRY_TEMPLATES;
#undef xor_speed
		active_template = template_list;
	}
	return 0;
}
#endif

#define BENCH_SIZE	4096
#define REPS		800U

static void __init
do_xor_speed(struct xor_block_template *tmpl, void *b1, void *b2)
{
	int speed;
	unsigned long reps;
	ktime_t min, start, t0;

	tmpl->next = template_list;
	template_list = tmpl;

	preempt_disable();

	reps = 0;
	t0 = ktime_get();
	/* delay start until time has advanced */
	while ((start = ktime_get()) == t0)
		cpu_relax();
	do {
		mb(); /* prevent loop optimization */
		tmpl->do_2(BENCH_SIZE, b1, b2);
		mb();
	} while (reps++ < REPS || (t0 = ktime_get()) == start);
	min = ktime_sub(t0, start);

	preempt_enable();

	// bytes/ns == GB/s, multiply by 1000 to get MB/s [not MiB/s]
	speed = (1000 * reps * BENCH_SIZE) / (unsigned int)ktime_to_ns(min);
	tmpl->speed = speed;

	pr_info("   %-16s: %5d MB/sec\n", tmpl->name, speed);
}

static int __init
calibrate_xor_blocks(void)
{
	void *b1, *b2;
	struct xor_block_template *f, *fastest;

	fastest = XOR_SELECT_TEMPLATE(NULL);

	if (fastest) {
		printk(KERN_INFO "xor: automatically using best "
				 "checksumming function   %-10s\n",
		       fastest->name);
		goto out;
	}

	b1 = (void *) __get_free_pages(GFP_KERNEL, 2);
	if (!b1) {
		printk(KERN_WARNING "xor: Yikes!  No memory available.\n");
		return -ENOMEM;
	}
	b2 = b1 + 2*PAGE_SIZE + BENCH_SIZE;

	/*
	 * If this arch/cpu has a short-circuited selection, don't loop through
	 * all the possible functions, just test the best one
	 */

#define xor_speed(templ)	do_xor_speed((templ), b1, b2)

	printk(KERN_INFO "xor: measuring software checksum speed\n");
	template_list = NULL;
	XOR_TRY_TEMPLATES;
	fastest = template_list;
	for (f = fastest; f; f = f->next)
		if (f->speed > fastest->speed)
			fastest = f;

	pr_info("xor: using function: %s (%d MB/sec)\n",
	       fastest->name, fastest->speed);

#undef xor_speed

	free_pages((unsigned long)b1, 2);
out:
	active_template = fastest;
	return 0;
}

static __exit void xor_exit(void) { }

MODULE_LICENSE("GPL");

#ifndef MODULE
/* when built-in xor.o must initialize before drivers/md/md.o */
core_initcall(register_xor_blocks);
#endif

module_init(calibrate_xor_blocks);
module_exit(xor_exit);

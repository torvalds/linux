/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes to Ingo Molnar, for suggesting the idea.
 * Mathieu Desnoyers, for suggesting postponing the modifications.
 * Arjan van de Ven, for keeping me straight, and explaining to me
 * the dangers of modifying code on the run.
 */

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#define CALL_BACK		5

#define JMPFWD			0x03eb

static unsigned short ftrace_jmp = JMPFWD;

struct ftrace_record {
	struct dyn_ftrace	rec;
	int			failed;
} __attribute__((packed));

struct ftrace_page {
	struct ftrace_page	*next;
	int			index;
	struct ftrace_record	records[];
} __attribute__((packed));

#define ENTRIES_PER_PAGE \
  ((PAGE_SIZE - sizeof(struct ftrace_page)) / sizeof(struct ftrace_record))

/* estimate from running different kernels */
#define NR_TO_INIT		10000

#define MCOUNT_ADDR ((long)(&mcount))

union ftrace_code_union {
	char code[5];
	struct {
		char e8;
		int offset;
	} __attribute__((packed));
};

static struct ftrace_page	*ftrace_pages_start;
static struct ftrace_page	*ftrace_pages;

notrace struct dyn_ftrace *ftrace_alloc_shutdown_node(unsigned long ip)
{
	struct ftrace_record *rec;
	unsigned short save;

	ip -= CALL_BACK;
	save = *(short *)ip;

	/* If this was already converted, skip it */
	if (save == JMPFWD)
		return NULL;

	if (ftrace_pages->index == ENTRIES_PER_PAGE) {
		if (!ftrace_pages->next)
			return NULL;
		ftrace_pages = ftrace_pages->next;
	}

	rec = &ftrace_pages->records[ftrace_pages->index++];

	return &rec->rec;
}

static int notrace
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		   unsigned char *new_code)
{
	unsigned short old = *(unsigned short *)old_code;
	unsigned short new = *(unsigned short *)new_code;
	unsigned short replaced;
	int faulted = 0;

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine.
	 */
	asm volatile (
		"1: lock\n"
		"   cmpxchg %w3, (%2)\n"
		"2:\n"
		".section .fixup, \"ax\"\n"
		"	movl $1, %0\n"
		"3:	jmp 2b\n"
		".previous\n"
		_ASM_EXTABLE(1b, 3b)
		: "=r"(faulted), "=a"(replaced)
		: "r"(ip), "r"(new), "0"(faulted), "a"(old)
		: "memory");
	sync_core();

	if (replaced != old)
		faulted = 2;

	return faulted;
}

static int notrace ftrace_calc_offset(long ip)
{
	return (int)(MCOUNT_ADDR - ip);
}

notrace void ftrace_code_disable(struct dyn_ftrace *rec)
{
	unsigned long ip;
	union ftrace_code_union save;
	struct ftrace_record *r =
		container_of(rec, struct ftrace_record, rec);

	ip = rec->ip;

	save.e8		= 0xe8;
	save.offset 	= ftrace_calc_offset(ip);

	/* move the IP back to the start of the call */
	ip -= CALL_BACK;

	r->failed = ftrace_modify_code(ip, save.code, (char *)&ftrace_jmp);
}

static void notrace ftrace_replace_code(int saved)
{
	unsigned char *new = NULL, *old = NULL;
	struct ftrace_record *rec;
	struct ftrace_page *pg;
	unsigned long ip;
	int i;

	if (saved)
		old = (char *)&ftrace_jmp;
	else
		new = (char *)&ftrace_jmp;

	for (pg = ftrace_pages_start; pg; pg = pg->next) {
		for (i = 0; i < pg->index; i++) {
			union ftrace_code_union calc;
			rec = &pg->records[i];

			/* don't modify code that has already faulted */
			if (rec->failed)
				continue;

			ip = rec->rec.ip;

			calc.e8		= 0xe8;
			calc.offset	= ftrace_calc_offset(ip);

			if (saved)
				new = calc.code;
			else
				old = calc.code;

			ip -= CALL_BACK;

			rec->failed = ftrace_modify_code(ip, old, new);
		}
	}

}

notrace void ftrace_startup_code(void)
{
	ftrace_replace_code(1);
}

notrace void ftrace_shutdown_code(void)
{
	ftrace_replace_code(0);
}

notrace void ftrace_shutdown_replenish(void)
{
	if (ftrace_pages->next)
		return;

	/* allocate another page */
	ftrace_pages->next = (void *)get_zeroed_page(GFP_KERNEL);
}

notrace int ftrace_shutdown_arch_init(void)
{
	struct ftrace_page *pg;
	int cnt;
	int i;

	/* allocate a few pages */
	ftrace_pages_start = (void *)get_zeroed_page(GFP_KERNEL);
	if (!ftrace_pages_start)
		return -1;

	/*
	 * Allocate a few more pages.
	 *
	 * TODO: have some parser search vmlinux before
	 *   final linking to find all calls to ftrace.
	 *   Then we can:
	 *    a) know how many pages to allocate.
	 *     and/or
	 *    b) set up the table then.
	 *
	 *  The dynamic code is still necessary for
	 *  modules.
	 */

	pg = ftrace_pages = ftrace_pages_start;

	cnt = NR_TO_INIT / ENTRIES_PER_PAGE;

	for (i = 0; i < cnt; i++) {
		pg->next = (void *)get_zeroed_page(GFP_KERNEL);

		/* If we fail, we'll try later anyway */
		if (!pg->next)
			break;

		pg = pg->next;
	}

	return 0;
}

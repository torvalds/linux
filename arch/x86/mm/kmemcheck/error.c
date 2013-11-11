#include <linux/interrupt.h>
#include <linux/kdebug.h>
#include <linux/kmemcheck.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/stacktrace.h>
#include <linux/string.h>

#include "error.h"
#include "shadow.h"

enum kmemcheck_error_type {
	KMEMCHECK_ERROR_INVALID_ACCESS,
	KMEMCHECK_ERROR_BUG,
};

#define SHADOW_COPY_SIZE (1 << CONFIG_KMEMCHECK_SHADOW_COPY_SHIFT)

struct kmemcheck_error {
	enum kmemcheck_error_type type;

	union {
		/* KMEMCHECK_ERROR_INVALID_ACCESS */
		struct {
			/* Kind of access that caused the error */
			enum kmemcheck_shadow state;
			/* Address and size of the erroneous read */
			unsigned long	address;
			unsigned int	size;
		};
	};

	struct pt_regs		regs;
	struct stack_trace	trace;
	unsigned long		trace_entries[32];

	/* We compress it to a char. */
	unsigned char		shadow_copy[SHADOW_COPY_SIZE];
	unsigned char		memory_copy[SHADOW_COPY_SIZE];
};

/*
 * Create a ring queue of errors to output. We can't call printk() directly
 * from the kmemcheck traps, since this may call the console drivers and
 * result in a recursive fault.
 */
static struct kmemcheck_error error_fifo[CONFIG_KMEMCHECK_QUEUE_SIZE];
static unsigned int error_count;
static unsigned int error_rd;
static unsigned int error_wr;
static unsigned int error_missed_count;

static struct kmemcheck_error *error_next_wr(void)
{
	struct kmemcheck_error *e;

	if (error_count == ARRAY_SIZE(error_fifo)) {
		++error_missed_count;
		return NULL;
	}

	e = &error_fifo[error_wr];
	if (++error_wr == ARRAY_SIZE(error_fifo))
		error_wr = 0;
	++error_count;
	return e;
}

static struct kmemcheck_error *error_next_rd(void)
{
	struct kmemcheck_error *e;

	if (error_count == 0)
		return NULL;

	e = &error_fifo[error_rd];
	if (++error_rd == ARRAY_SIZE(error_fifo))
		error_rd = 0;
	--error_count;
	return e;
}

void kmemcheck_error_recall(void)
{
	static const char *desc[] = {
		[KMEMCHECK_SHADOW_UNALLOCATED]		= "unallocated",
		[KMEMCHECK_SHADOW_UNINITIALIZED]	= "uninitialized",
		[KMEMCHECK_SHADOW_INITIALIZED]		= "initialized",
		[KMEMCHECK_SHADOW_FREED]		= "freed",
	};

	static const char short_desc[] = {
		[KMEMCHECK_SHADOW_UNALLOCATED]		= 'a',
		[KMEMCHECK_SHADOW_UNINITIALIZED]	= 'u',
		[KMEMCHECK_SHADOW_INITIALIZED]		= 'i',
		[KMEMCHECK_SHADOW_FREED]		= 'f',
	};

	struct kmemcheck_error *e;
	unsigned int i;

	e = error_next_rd();
	if (!e)
		return;

	switch (e->type) {
	case KMEMCHECK_ERROR_INVALID_ACCESS:
		printk(KERN_WARNING "WARNING: kmemcheck: Caught %d-bit read from %s memory (%p)\n",
			8 * e->size, e->state < ARRAY_SIZE(desc) ?
				desc[e->state] : "(invalid shadow state)",
			(void *) e->address);

		printk(KERN_WARNING);
		for (i = 0; i < SHADOW_COPY_SIZE; ++i)
			printk(KERN_CONT "%02x", e->memory_copy[i]);
		printk(KERN_CONT "\n");

		printk(KERN_WARNING);
		for (i = 0; i < SHADOW_COPY_SIZE; ++i) {
			if (e->shadow_copy[i] < ARRAY_SIZE(short_desc))
				printk(KERN_CONT " %c", short_desc[e->shadow_copy[i]]);
			else
				printk(KERN_CONT " ?");
		}
		printk(KERN_CONT "\n");
		printk(KERN_WARNING "%*c\n", 2 + 2
			* (int) (e->address & (SHADOW_COPY_SIZE - 1)), '^');
		break;
	case KMEMCHECK_ERROR_BUG:
		printk(KERN_EMERG "ERROR: kmemcheck: Fatal error\n");
		break;
	}

	__show_regs(&e->regs, 1);
	print_stack_trace(&e->trace, 0);
}

static void do_wakeup(unsigned long data)
{
	while (error_count > 0)
		kmemcheck_error_recall();

	if (error_missed_count > 0) {
		printk(KERN_WARNING "kmemcheck: Lost %d error reports because "
			"the queue was too small\n", error_missed_count);
		error_missed_count = 0;
	}
}

static DECLARE_TASKLET(kmemcheck_tasklet, &do_wakeup, 0);

/*
 * Save the context of an error report.
 */
void kmemcheck_error_save(enum kmemcheck_shadow state,
	unsigned long address, unsigned int size, struct pt_regs *regs)
{
	static unsigned long prev_ip;

	struct kmemcheck_error *e;
	void *shadow_copy;
	void *memory_copy;

	/* Don't report several adjacent errors from the same EIP. */
	if (regs->ip == prev_ip)
		return;
	prev_ip = regs->ip;

	e = error_next_wr();
	if (!e)
		return;

	e->type = KMEMCHECK_ERROR_INVALID_ACCESS;

	e->state = state;
	e->address = address;
	e->size = size;

	/* Save regs */
	memcpy(&e->regs, regs, sizeof(*regs));

	/* Save stack trace */
	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 0;
	save_stack_trace_regs(regs, &e->trace);

	/* Round address down to nearest 16 bytes */
	shadow_copy = kmemcheck_shadow_lookup(address
		& ~(SHADOW_COPY_SIZE - 1));
	BUG_ON(!shadow_copy);

	memcpy(e->shadow_copy, shadow_copy, SHADOW_COPY_SIZE);

	kmemcheck_show_addr(address);
	memory_copy = (void *) (address & ~(SHADOW_COPY_SIZE - 1));
	memcpy(e->memory_copy, memory_copy, SHADOW_COPY_SIZE);
	kmemcheck_hide_addr(address);

	tasklet_hi_schedule_first(&kmemcheck_tasklet);
}

/*
 * Save the context of a kmemcheck bug.
 */
void kmemcheck_error_save_bug(struct pt_regs *regs)
{
	struct kmemcheck_error *e;

	e = error_next_wr();
	if (!e)
		return;

	e->type = KMEMCHECK_ERROR_BUG;

	memcpy(&e->regs, regs, sizeof(*regs));

	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 1;
	save_stack_trace(&e->trace);

	tasklet_hi_schedule_first(&kmemcheck_tasklet);
}

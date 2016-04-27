/*
 * Perf callchain handling code.
 *
 *   Based on the ARM perf implementation.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

static bool is_valid_call(unsigned long calladdr)
{
	unsigned int callinsn;

	/* Check the possible return address is aligned. */
	if (!(calladdr & 0x3)) {
		if (!get_user(callinsn, (unsigned int *)calladdr)) {
			/* Check for CALLR or SWAP PC,D1RtP. */
			if ((callinsn & 0xff000000) == 0xab000000 ||
			    callinsn == 0xa3200aa0)
				return true;
		}
	}
	return false;
}

static struct metag_frame __user *
user_backtrace(struct metag_frame __user *user_frame,
	       struct perf_callchain_entry *entry)
{
	struct metag_frame frame;
	unsigned long calladdr;

	/* We cannot rely on having frame pointers in user code. */
	while (1) {
		/* Also check accessibility of one struct frame beyond */
		if (!access_ok(VERIFY_READ, user_frame, sizeof(frame)))
			return 0;
		if (__copy_from_user_inatomic(&frame, user_frame,
					      sizeof(frame)))
			return 0;

		--user_frame;

		calladdr = frame.lr - 4;
		if (is_valid_call(calladdr)) {
			perf_callchain_store(entry, calladdr);
			return user_frame;
		}
	}

	return 0;
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	unsigned long sp = regs->ctx.AX[0].U0;
	struct metag_frame __user *frame;

	frame = (struct metag_frame __user *)sp;

	--frame;

	while ((entry->nr < sysctl_perf_event_max_stack) && frame)
		frame = user_backtrace(frame, entry);
}

/*
 * Gets called by walk_stackframe() for every stackframe. This will be called
 * whist unwinding the stackframe and is like a subroutine return so we use
 * the PC.
 */
static int
callchain_trace(struct stackframe *fr,
		void *data)
{
	struct perf_callchain_entry *entry = data;
	perf_callchain_store(entry, fr->pc);
	return 0;
}

void
perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct stackframe fr;

	fr.fp = regs->ctx.AX[1].U0;
	fr.sp = regs->ctx.AX[0].U0;
	fr.lr = regs->ctx.DX[4].U1;
	fr.pc = regs->ctx.CurrPC;
	walk_stackframe(&fr, callchain_trace, entry);
}

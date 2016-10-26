#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/bitops.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

#define FRAME_HEADER_SIZE (sizeof(long) * 2)

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	unsigned long addr;
	unsigned long *addr_p = unwind_get_return_address_ptr(state);

	if (unwind_done(state))
		return 0;

	if (state->regs && user_mode(state->regs))
		return 0;

	addr = ftrace_graph_ret_addr(state->task, &state->graph_idx, *addr_p,
				     addr_p);

	if (!__kernel_text_address(addr)) {
		printk_deferred_once(KERN_WARNING
			"WARNING: unrecognized kernel stack return address %p at %p in %s:%d\n",
			(void *)addr, addr_p, state->task->comm,
			state->task->pid);
		return 0;
	}

	return addr;
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

static bool is_last_task_frame(struct unwind_state *state)
{
	unsigned long bp = (unsigned long)state->bp;
	unsigned long regs = (unsigned long)task_pt_regs(state->task);

	return bp == regs - FRAME_HEADER_SIZE;
}

/*
 * This determines if the frame pointer actually contains an encoded pointer to
 * pt_regs on the stack.  See ENCODE_FRAME_POINTER.
 */
static struct pt_regs *decode_frame_pointer(unsigned long *bp)
{
	unsigned long regs = (unsigned long)bp;

	if (!(regs & 0x1))
		return NULL;

	return (struct pt_regs *)(regs & ~0x1);
}

static bool update_stack_state(struct unwind_state *state, void *addr,
			       size_t len)
{
	struct stack_info *info = &state->stack_info;

	/*
	 * If addr isn't on the current stack, switch to the next one.
	 *
	 * We may have to traverse multiple stacks to deal with the possibility
	 * that 'info->next_sp' could point to an empty stack and 'addr' could
	 * be on a subsequent stack.
	 */
	while (!on_stack(info, addr, len))
		if (get_stack_info(info->next_sp, state->task, info,
				   &state->stack_mask))
			return false;

	return true;
}

bool unwind_next_frame(struct unwind_state *state)
{
	struct pt_regs *regs;
	unsigned long *next_bp, *next_frame;
	size_t next_len;

	if (unwind_done(state))
		return false;

	/* have we reached the end? */
	if (state->regs && user_mode(state->regs))
		goto the_end;

	if (is_last_task_frame(state)) {
		regs = task_pt_regs(state->task);

		/*
		 * kthreads (other than the boot CPU's idle thread) have some
		 * partial regs at the end of their stack which were placed
		 * there by copy_thread_tls().  But the regs don't have any
		 * useful information, so we can skip them.
		 *
		 * This user_mode() check is slightly broader than a PF_KTHREAD
		 * check because it also catches the awkward situation where a
		 * newly forked kthread transitions into a user task by calling
		 * do_execve(), which eventually clears PF_KTHREAD.
		 */
		if (!user_mode(regs))
			goto the_end;

		/*
		 * We're almost at the end, but not quite: there's still the
		 * syscall regs frame.  Entry code doesn't encode the regs
		 * pointer for syscalls, so we have to set it manually.
		 */
		state->regs = regs;
		state->bp = NULL;
		return true;
	}

	/* get the next frame pointer */
	if (state->regs)
		next_bp = (unsigned long *)state->regs->bp;
	else
		next_bp = (unsigned long *)*state->bp;

	/* is the next frame pointer an encoded pointer to pt_regs? */
	regs = decode_frame_pointer(next_bp);
	if (regs) {
		next_frame = (unsigned long *)regs;
		next_len = sizeof(*regs);
	} else {
		next_frame = next_bp;
		next_len = FRAME_HEADER_SIZE;
	}

	/* make sure the next frame's data is accessible */
	if (!update_stack_state(state, next_frame, next_len)) {
		/*
		 * Don't warn on bad regs->bp.  An interrupt in entry code
		 * might cause a false positive warning.
		 */
		if (state->regs)
			goto the_end;

		goto bad_address;
	}

	/* move to the next frame */
	if (regs) {
		state->regs = regs;
		state->bp = NULL;
	} else {
		state->bp = next_bp;
		state->regs = NULL;
	}

	return true;

bad_address:
	printk_deferred_once(KERN_WARNING
		"WARNING: kernel stack frame pointer at %p in %s:%d has bad value %p\n",
		state->bp, state->task->comm,
		state->task->pid, next_bp);
the_end:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame)
{
	unsigned long *bp, *frame;
	size_t len;

	memset(state, 0, sizeof(*state));
	state->task = task;

	/* don't even attempt to start from user mode regs */
	if (regs && user_mode(regs)) {
		state->stack_info.type = STACK_TYPE_UNKNOWN;
		return;
	}

	/* set up the starting stack frame */
	bp = get_frame_pointer(task, regs);
	regs = decode_frame_pointer(bp);
	if (regs) {
		state->regs = regs;
		frame = (unsigned long *)regs;
		len = sizeof(*regs);
	} else {
		state->bp = bp;
		frame = bp;
		len = FRAME_HEADER_SIZE;
	}

	/* initialize stack info and make sure the frame data is accessible */
	get_stack_info(frame, state->task, &state->stack_info,
		       &state->stack_mask);
	update_stack_state(state, frame, len);

	/*
	 * The caller can provide the address of the first frame directly
	 * (first_frame) or indirectly (regs->sp) to indicate which stack frame
	 * to start unwinding at.  Skip ahead until we reach it.
	 */
	while (!unwind_done(state) &&
	       (!on_stack(&state->stack_info, first_frame, sizeof(long)) ||
			state->bp < first_frame))
		unwind_next_frame(state);
}
EXPORT_SYMBOL_GPL(__unwind_start);

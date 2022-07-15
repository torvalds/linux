// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test module for unwind_for_each_frame
 */

#define pr_fmt(fmt) "test_unwind: " fmt
#include <asm/unwind.h>
#include <linux/completion.h>
#include <linux/kallsyms.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/wait.h>
#include <asm/irq.h>
#include <asm/delay.h>

#define BT_BUF_SIZE (PAGE_SIZE * 4)

/*
 * To avoid printk line limit split backtrace by lines
 */
static void print_backtrace(char *bt)
{
	char *p;

	while (true) {
		p = strsep(&bt, "\n");
		if (!p)
			break;
		pr_err("%s\n", p);
	}
}

/*
 * Calls unwind_for_each_frame(task, regs, sp) and verifies that the result
 * contains unwindme_func2 followed by unwindme_func1.
 */
static noinline int test_unwind(struct task_struct *task, struct pt_regs *regs,
				unsigned long sp)
{
	int frame_count, prev_is_func2, seen_func2_func1;
	const int max_frames = 128;
	struct unwind_state state;
	size_t bt_pos = 0;
	int ret = 0;
	char *bt;

	bt = kmalloc(BT_BUF_SIZE, GFP_ATOMIC);
	if (!bt) {
		pr_err("failed to allocate backtrace buffer\n");
		return -ENOMEM;
	}
	/* Unwind. */
	frame_count = 0;
	prev_is_func2 = 0;
	seen_func2_func1 = 0;
	unwind_for_each_frame(&state, task, regs, sp) {
		unsigned long addr = unwind_get_return_address(&state);
		char sym[KSYM_SYMBOL_LEN];

		if (frame_count++ == max_frames)
			break;
		if (state.reliable && !addr) {
			pr_err("unwind state reliable but addr is 0\n");
			kfree(bt);
			return -EINVAL;
		}
		sprint_symbol(sym, addr);
		if (bt_pos < BT_BUF_SIZE) {
			bt_pos += snprintf(bt + bt_pos, BT_BUF_SIZE - bt_pos,
					   state.reliable ? " [%-7s%px] %pSR\n" :
							    "([%-7s%px] %pSR)\n",
					   stack_type_name(state.stack_info.type),
					   (void *)state.sp, (void *)state.ip);
			if (bt_pos >= BT_BUF_SIZE)
				pr_err("backtrace buffer is too small\n");
		}
		frame_count += 1;
		if (prev_is_func2 && str_has_prefix(sym, "unwindme_func1"))
			seen_func2_func1 = 1;
		prev_is_func2 = str_has_prefix(sym, "unwindme_func2");
	}

	/* Check the results. */
	if (unwind_error(&state)) {
		pr_err("unwind error\n");
		ret = -EINVAL;
	}
	if (!seen_func2_func1) {
		pr_err("unwindme_func2 and unwindme_func1 not found\n");
		ret = -EINVAL;
	}
	if (frame_count == max_frames) {
		pr_err("Maximum number of frames exceeded\n");
		ret = -EINVAL;
	}
	if (ret)
		print_backtrace(bt);
	kfree(bt);
	return ret;
}

/* State of the task being unwound. */
struct unwindme {
	int flags;
	int ret;
	struct task_struct *task;
	struct completion task_ready;
	wait_queue_head_t task_wq;
	unsigned long sp;
};

static struct unwindme *unwindme;

/* Values of unwindme.flags. */
#define UWM_DEFAULT		0x0
#define UWM_THREAD		0x1	/* Unwind a separate task. */
#define UWM_REGS		0x2	/* Pass regs to test_unwind(). */
#define UWM_SP			0x4	/* Pass sp to test_unwind(). */
#define UWM_CALLER		0x8	/* Unwind starting from caller. */
#define UWM_SWITCH_STACK	0x10	/* Use CALL_ON_STACK. */
#define UWM_IRQ			0x20	/* Unwind from irq context. */
#define UWM_PGM			0x40	/* Unwind from program check handler. */

static __always_inline unsigned long get_psw_addr(void)
{
	unsigned long psw_addr;

	asm volatile(
		"basr	%[psw_addr],0\n"
		: [psw_addr] "=d" (psw_addr));
	return psw_addr;
}

#ifdef CONFIG_KPROBES
static int pgm_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct unwindme *u = unwindme;

	u->ret = test_unwind(NULL, (u->flags & UWM_REGS) ? regs : NULL,
			     (u->flags & UWM_SP) ? u->sp : 0);
	return 0;
}
#endif

/* This function may or may not appear in the backtrace. */
static noinline int unwindme_func4(struct unwindme *u)
{
	if (!(u->flags & UWM_CALLER))
		u->sp = current_frame_address();
	if (u->flags & UWM_THREAD) {
		complete(&u->task_ready);
		wait_event(u->task_wq, kthread_should_park());
		kthread_parkme();
		return 0;
#ifdef CONFIG_KPROBES
	} else if (u->flags & UWM_PGM) {
		struct kprobe kp;
		int ret;

		unwindme = u;
		memset(&kp, 0, sizeof(kp));
		kp.symbol_name = "do_report_trap";
		kp.pre_handler = pgm_pre_handler;
		ret = register_kprobe(&kp);
		if (ret < 0) {
			pr_err("register_kprobe failed %d\n", ret);
			return -EINVAL;
		}

		/*
		 * Trigger operation exception; use insn notation to bypass
		 * llvm's integrated assembler sanity checks.
		 */
		asm volatile(
			"	.insn	e,0x0000\n"	/* illegal opcode */
			"0:	nopr	%%r7\n"
			EX_TABLE(0b, 0b)
			:);

		unregister_kprobe(&kp);
		unwindme = NULL;
		return u->ret;
#endif
	} else {
		struct pt_regs regs;

		memset(&regs, 0, sizeof(regs));
		regs.psw.addr = get_psw_addr();
		regs.gprs[15] = current_stack_pointer();
		return test_unwind(NULL,
				   (u->flags & UWM_REGS) ? &regs : NULL,
				   (u->flags & UWM_SP) ? u->sp : 0);
	}
}

/* This function may or may not appear in the backtrace. */
static noinline int unwindme_func3(struct unwindme *u)
{
	u->sp = current_frame_address();
	return unwindme_func4(u);
}

/* This function must appear in the backtrace. */
static noinline int unwindme_func2(struct unwindme *u)
{
	unsigned long flags;
	int rc;

	if (u->flags & UWM_SWITCH_STACK) {
		local_irq_save(flags);
		local_mcck_disable();
		rc = CALL_ON_STACK(unwindme_func3, S390_lowcore.nodat_stack, 1, u);
		local_mcck_enable();
		local_irq_restore(flags);
		return rc;
	} else {
		return unwindme_func3(u);
	}
}

/* This function must follow unwindme_func2 in the backtrace. */
static noinline int unwindme_func1(void *u)
{
	return unwindme_func2((struct unwindme *)u);
}

static void unwindme_irq_handler(struct ext_code ext_code,
				       unsigned int param32,
				       unsigned long param64)
{
	struct unwindme *u = READ_ONCE(unwindme);

	if (u && u->task == current) {
		unwindme = NULL;
		u->task = NULL;
		u->ret = unwindme_func1(u);
	}
}

static int test_unwind_irq(struct unwindme *u)
{
	preempt_disable();
	if (register_external_irq(EXT_IRQ_CLK_COMP, unwindme_irq_handler)) {
		pr_info("Couldn't register external interrupt handler");
		return -1;
	}
	u->task = current;
	unwindme = u;
	udelay(1);
	unregister_external_irq(EXT_IRQ_CLK_COMP, unwindme_irq_handler);
	preempt_enable();
	return u->ret;
}

/* Spawns a task and passes it to test_unwind(). */
static int test_unwind_task(struct unwindme *u)
{
	struct task_struct *task;
	int ret;

	/* Initialize thread-related fields. */
	init_completion(&u->task_ready);
	init_waitqueue_head(&u->task_wq);

	/*
	 * Start the task and wait until it reaches unwindme_func4() and sleeps
	 * in (task_ready, unwind_done] range.
	 */
	task = kthread_run(unwindme_func1, u, "%s", __func__);
	if (IS_ERR(task)) {
		pr_err("kthread_run() failed\n");
		return PTR_ERR(task);
	}
	/*
	 * Make sure task reaches unwindme_func4 before parking it,
	 * we might park it before kthread function has been executed otherwise
	 */
	wait_for_completion(&u->task_ready);
	kthread_park(task);
	/* Unwind. */
	ret = test_unwind(task, NULL, (u->flags & UWM_SP) ? u->sp : 0);
	kthread_stop(task);
	return ret;
}

static int test_unwind_flags(int flags)
{
	struct unwindme u;

	u.flags = flags;
	if (u.flags & UWM_THREAD)
		return test_unwind_task(&u);
	else if (u.flags & UWM_IRQ)
		return test_unwind_irq(&u);
	else
		return unwindme_func1(&u);
}

static int test_unwind_init(void)
{
	int ret = 0;

#define TEST(flags)							\
do {									\
	pr_info("[ RUN      ] " #flags "\n");				\
	if (!test_unwind_flags((flags))) {				\
		pr_info("[       OK ] " #flags "\n");			\
	} else {							\
		pr_err("[  FAILED  ] " #flags "\n");			\
		ret = -EINVAL;						\
	}								\
} while (0)

	TEST(UWM_DEFAULT);
	TEST(UWM_SP);
	TEST(UWM_REGS);
	TEST(UWM_SWITCH_STACK);
	TEST(UWM_SP | UWM_REGS);
	TEST(UWM_CALLER | UWM_SP);
	TEST(UWM_CALLER | UWM_SP | UWM_REGS);
	TEST(UWM_CALLER | UWM_SP | UWM_REGS | UWM_SWITCH_STACK);
	TEST(UWM_THREAD);
	TEST(UWM_THREAD | UWM_SP);
	TEST(UWM_THREAD | UWM_CALLER | UWM_SP);
	TEST(UWM_IRQ);
	TEST(UWM_IRQ | UWM_SWITCH_STACK);
	TEST(UWM_IRQ | UWM_SP);
	TEST(UWM_IRQ | UWM_REGS);
	TEST(UWM_IRQ | UWM_SP | UWM_REGS);
	TEST(UWM_IRQ | UWM_CALLER | UWM_SP);
	TEST(UWM_IRQ | UWM_CALLER | UWM_SP | UWM_REGS);
	TEST(UWM_IRQ | UWM_CALLER | UWM_SP | UWM_REGS | UWM_SWITCH_STACK);
#ifdef CONFIG_KPROBES
	TEST(UWM_PGM);
	TEST(UWM_PGM | UWM_SP);
	TEST(UWM_PGM | UWM_REGS);
	TEST(UWM_PGM | UWM_SP | UWM_REGS);
#endif
#undef TEST

	return ret;
}

static void test_unwind_exit(void)
{
}

module_init(test_unwind_init);
module_exit(test_unwind_exit);
MODULE_LICENSE("GPL");

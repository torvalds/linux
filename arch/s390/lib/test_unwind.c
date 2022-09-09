// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test module for unwind_for_each_frame
 */

#include <kunit/test.h>
#include <asm/unwind.h>
#include <linux/completion.h>
#include <linux/kallsyms.h>
#include <linux/kthread.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/wait.h>
#include <asm/irq.h>

static struct kunit *current_test;

#define BT_BUF_SIZE (PAGE_SIZE * 4)

static bool force_bt;
module_param_named(backtrace, force_bt, bool, 0444);
MODULE_PARM_DESC(backtrace, "print backtraces for all tests");

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
		kunit_err(current_test, "%s\n", p);
	}
}

/*
 * Calls unwind_for_each_frame(task, regs, sp) and verifies that the result
 * contains unwindme_func2 followed by unwindme_func1.
 */
static noinline int test_unwind(struct task_struct *task, struct pt_regs *regs,
				unsigned long sp)
{
	int frame_count, prev_is_func2, seen_func2_func1, seen_kretprobe_trampoline;
	const int max_frames = 128;
	struct unwind_state state;
	size_t bt_pos = 0;
	int ret = 0;
	char *bt;

	bt = kmalloc(BT_BUF_SIZE, GFP_ATOMIC);
	if (!bt) {
		kunit_err(current_test, "failed to allocate backtrace buffer\n");
		return -ENOMEM;
	}
	/* Unwind. */
	frame_count = 0;
	prev_is_func2 = 0;
	seen_func2_func1 = 0;
	seen_kretprobe_trampoline = 0;
	unwind_for_each_frame(&state, task, regs, sp) {
		unsigned long addr = unwind_get_return_address(&state);
		char sym[KSYM_SYMBOL_LEN];

		if (frame_count++ == max_frames)
			break;
		if (state.reliable && !addr) {
			kunit_err(current_test, "unwind state reliable but addr is 0\n");
			ret = -EINVAL;
			break;
		}
		sprint_symbol(sym, addr);
		if (bt_pos < BT_BUF_SIZE) {
			bt_pos += snprintf(bt + bt_pos, BT_BUF_SIZE - bt_pos,
					   state.reliable ? " [%-7s%px] %pSR\n" :
							    "([%-7s%px] %pSR)\n",
					   stack_type_name(state.stack_info.type),
					   (void *)state.sp, (void *)state.ip);
			if (bt_pos >= BT_BUF_SIZE)
				kunit_err(current_test, "backtrace buffer is too small\n");
		}
		frame_count += 1;
		if (prev_is_func2 && str_has_prefix(sym, "unwindme_func1"))
			seen_func2_func1 = 1;
		prev_is_func2 = str_has_prefix(sym, "unwindme_func2");
		if (str_has_prefix(sym, "__kretprobe_trampoline+0x0/"))
			seen_kretprobe_trampoline = 1;
	}

	/* Check the results. */
	if (unwind_error(&state)) {
		kunit_err(current_test, "unwind error\n");
		ret = -EINVAL;
	}
	if (!seen_func2_func1) {
		kunit_err(current_test, "unwindme_func2 and unwindme_func1 not found\n");
		ret = -EINVAL;
	}
	if (frame_count == max_frames) {
		kunit_err(current_test, "Maximum number of frames exceeded\n");
		ret = -EINVAL;
	}
	if (seen_kretprobe_trampoline) {
		kunit_err(current_test, "__kretprobe_trampoline+0x0 in unwinding results\n");
		ret = -EINVAL;
	}
	if (ret || force_bt)
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
#define UWM_SWITCH_STACK	0x10	/* Use call_on_stack. */
#define UWM_IRQ			0x20	/* Unwind from irq context. */
#define UWM_PGM			0x40	/* Unwind from program check handler */
#define UWM_KPROBE_ON_FTRACE	0x80	/* Unwind from kprobe handler called via ftrace. */
#define UWM_FTRACE		0x100	/* Unwind from ftrace handler. */
#define UWM_KRETPROBE		0x200	/* Unwind through kretprobed function. */
#define UWM_KRETPROBE_HANDLER	0x400	/* Unwind from kretprobe handler. */

static __always_inline struct pt_regs fake_pt_regs(void)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.gprs[15] = current_stack_pointer;

	asm volatile(
		"basr	%[psw_addr],0\n"
		: [psw_addr] "=d" (regs.psw.addr));
	return regs;
}

static int kretprobe_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct unwindme *u = unwindme;

	if (!(u->flags & UWM_KRETPROBE_HANDLER))
		return 0;

	u->ret = test_unwind(NULL, (u->flags & UWM_REGS) ? regs : NULL,
			     (u->flags & UWM_SP) ? u->sp : 0);

	return 0;
}

static noinline notrace int test_unwind_kretprobed_func(struct unwindme *u)
{
	struct pt_regs regs;

	if (!(u->flags & UWM_KRETPROBE))
		return 0;

	regs = fake_pt_regs();
	return test_unwind(NULL, (u->flags & UWM_REGS) ? &regs : NULL,
			   (u->flags & UWM_SP) ? u->sp : 0);
}

static noinline int test_unwind_kretprobed_func_caller(struct unwindme *u)
{
	return test_unwind_kretprobed_func(u);
}

static int test_unwind_kretprobe(struct unwindme *u)
{
	int ret;
	struct kretprobe my_kretprobe;

	if (!IS_ENABLED(CONFIG_KPROBES))
		kunit_skip(current_test, "requires CONFIG_KPROBES");

	u->ret = -1; /* make sure kprobe is called */
	unwindme = u;

	memset(&my_kretprobe, 0, sizeof(my_kretprobe));
	my_kretprobe.handler = kretprobe_ret_handler;
	my_kretprobe.maxactive = 1;
	my_kretprobe.kp.addr = (kprobe_opcode_t *)test_unwind_kretprobed_func;

	ret = register_kretprobe(&my_kretprobe);

	if (ret < 0) {
		kunit_err(current_test, "register_kretprobe failed %d\n", ret);
		return -EINVAL;
	}

	ret = test_unwind_kretprobed_func_caller(u);
	unregister_kretprobe(&my_kretprobe);
	unwindme = NULL;
	if (u->flags & UWM_KRETPROBE_HANDLER)
		ret = u->ret;
	return ret;
}

static int kprobe_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct unwindme *u = unwindme;

	u->ret = test_unwind(NULL, (u->flags & UWM_REGS) ? regs : NULL,
			     (u->flags & UWM_SP) ? u->sp : 0);
	return 0;
}

extern const char test_unwind_kprobed_insn[];

static noinline void test_unwind_kprobed_func(void)
{
	asm volatile(
		"	nopr	%%r7\n"
		"test_unwind_kprobed_insn:\n"
		"	nopr	%%r7\n"
		:);
}

static int test_unwind_kprobe(struct unwindme *u)
{
	struct kprobe kp;
	int ret;

	if (!IS_ENABLED(CONFIG_KPROBES))
		kunit_skip(current_test, "requires CONFIG_KPROBES");
	if (!IS_ENABLED(CONFIG_KPROBES_ON_FTRACE) && u->flags & UWM_KPROBE_ON_FTRACE)
		kunit_skip(current_test, "requires CONFIG_KPROBES_ON_FTRACE");

	u->ret = -1; /* make sure kprobe is called */
	unwindme = u;
	memset(&kp, 0, sizeof(kp));
	kp.pre_handler = kprobe_pre_handler;
	kp.addr = u->flags & UWM_KPROBE_ON_FTRACE ?
				(kprobe_opcode_t *)test_unwind_kprobed_func :
				(kprobe_opcode_t *)test_unwind_kprobed_insn;
	ret = register_kprobe(&kp);
	if (ret < 0) {
		kunit_err(current_test, "register_kprobe failed %d\n", ret);
		return -EINVAL;
	}

	test_unwind_kprobed_func();
	unregister_kprobe(&kp);
	unwindme = NULL;
	return u->ret;
}

static void notrace __used test_unwind_ftrace_handler(unsigned long ip,
						      unsigned long parent_ip,
						      struct ftrace_ops *fops,
						      struct ftrace_regs *fregs)
{
	struct unwindme *u = (struct unwindme *)fregs->regs.gprs[2];

	u->ret = test_unwind(NULL, (u->flags & UWM_REGS) ? &fregs->regs : NULL,
			     (u->flags & UWM_SP) ? u->sp : 0);
}

static noinline int test_unwind_ftraced_func(struct unwindme *u)
{
	return READ_ONCE(u)->ret;
}

static int test_unwind_ftrace(struct unwindme *u)
{
	int ret;
#ifdef CONFIG_DYNAMIC_FTRACE
	struct ftrace_ops *fops;

	fops = kunit_kzalloc(current_test, sizeof(*fops), GFP_KERNEL);
	fops->func = test_unwind_ftrace_handler;
	fops->flags = FTRACE_OPS_FL_DYNAMIC |
		     FTRACE_OPS_FL_RECURSION |
		     FTRACE_OPS_FL_SAVE_REGS |
		     FTRACE_OPS_FL_PERMANENT;
#else
	kunit_skip(current_test, "requires CONFIG_DYNAMIC_FTRACE");
#endif

	ret = ftrace_set_filter_ip(fops, (unsigned long)test_unwind_ftraced_func, 0, 0);
	if (ret) {
		kunit_err(current_test, "failed to set ftrace filter (%d)\n", ret);
		return -1;
	}

	ret = register_ftrace_function(fops);
	if (!ret) {
		ret = test_unwind_ftraced_func(u);
		unregister_ftrace_function(fops);
	} else {
		kunit_err(current_test, "failed to register ftrace handler (%d)\n", ret);
	}

	ftrace_set_filter_ip(fops, (unsigned long)test_unwind_ftraced_func, 1, 0);
	return ret;
}

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
	} else if (u->flags & (UWM_PGM | UWM_KPROBE_ON_FTRACE)) {
		return test_unwind_kprobe(u);
	} else if (u->flags & (UWM_KRETPROBE | UWM_KRETPROBE_HANDLER)) {
		return test_unwind_kretprobe(u);
	} else if (u->flags & UWM_FTRACE) {
		return test_unwind_ftrace(u);
	} else {
		struct pt_regs regs = fake_pt_regs();

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
		rc = call_on_stack(1, S390_lowcore.nodat_stack,
				   int, unwindme_func3, struct unwindme *, u);
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

static void unwindme_timer_fn(struct timer_list *unused)
{
	struct unwindme *u = READ_ONCE(unwindme);

	if (u) {
		unwindme = NULL;
		u->task = NULL;
		u->ret = unwindme_func1(u);
		complete(&u->task_ready);
	}
}

static struct timer_list unwind_timer;

static int test_unwind_irq(struct unwindme *u)
{
	unwindme = u;
	init_completion(&u->task_ready);
	timer_setup(&unwind_timer, unwindme_timer_fn, 0);
	mod_timer(&unwind_timer, jiffies + 1);
	wait_for_completion(&u->task_ready);
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
		kunit_err(current_test, "kthread_run() failed\n");
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

struct test_params {
	int flags;
	char *name;
};

/*
 * Create required parameter list for tests
 */
#define TEST_WITH_FLAGS(f) { .flags = f, .name = #f }
static const struct test_params param_list[] = {
	TEST_WITH_FLAGS(UWM_DEFAULT),
	TEST_WITH_FLAGS(UWM_SP),
	TEST_WITH_FLAGS(UWM_REGS),
	TEST_WITH_FLAGS(UWM_SWITCH_STACK),
	TEST_WITH_FLAGS(UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_CALLER | UWM_SP),
	TEST_WITH_FLAGS(UWM_CALLER | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_CALLER | UWM_SP | UWM_REGS | UWM_SWITCH_STACK),
	TEST_WITH_FLAGS(UWM_THREAD),
	TEST_WITH_FLAGS(UWM_THREAD | UWM_SP),
	TEST_WITH_FLAGS(UWM_THREAD | UWM_CALLER | UWM_SP),
	TEST_WITH_FLAGS(UWM_IRQ),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_SWITCH_STACK),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_SP),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_REGS),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_CALLER | UWM_SP),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_CALLER | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_IRQ | UWM_CALLER | UWM_SP | UWM_REGS | UWM_SWITCH_STACK),
	TEST_WITH_FLAGS(UWM_PGM),
	TEST_WITH_FLAGS(UWM_PGM | UWM_SP),
	TEST_WITH_FLAGS(UWM_PGM | UWM_REGS),
	TEST_WITH_FLAGS(UWM_PGM | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KPROBE_ON_FTRACE),
	TEST_WITH_FLAGS(UWM_KPROBE_ON_FTRACE | UWM_SP),
	TEST_WITH_FLAGS(UWM_KPROBE_ON_FTRACE | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KPROBE_ON_FTRACE | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_FTRACE),
	TEST_WITH_FLAGS(UWM_FTRACE | UWM_SP),
	TEST_WITH_FLAGS(UWM_FTRACE | UWM_REGS),
	TEST_WITH_FLAGS(UWM_FTRACE | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KRETPROBE),
	TEST_WITH_FLAGS(UWM_KRETPROBE | UWM_SP),
	TEST_WITH_FLAGS(UWM_KRETPROBE | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KRETPROBE | UWM_SP | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KRETPROBE_HANDLER),
	TEST_WITH_FLAGS(UWM_KRETPROBE_HANDLER | UWM_SP),
	TEST_WITH_FLAGS(UWM_KRETPROBE_HANDLER | UWM_REGS),
	TEST_WITH_FLAGS(UWM_KRETPROBE_HANDLER | UWM_SP | UWM_REGS),
};

/*
 * Parameter description generator: required for KUNIT_ARRAY_PARAM()
 */
static void get_desc(const struct test_params *params, char *desc)
{
	strscpy(desc, params->name, KUNIT_PARAM_DESC_SIZE);
}

/*
 * Create test_unwind_gen_params
 */
KUNIT_ARRAY_PARAM(test_unwind, param_list, get_desc);

static void test_unwind_flags(struct kunit *test)
{
	struct unwindme u;
	const struct test_params *params;

	current_test = test;
	params = (const struct test_params *)test->param_value;
	u.flags = params->flags;
	if (u.flags & UWM_THREAD)
		KUNIT_EXPECT_EQ(test, 0, test_unwind_task(&u));
	else if (u.flags & UWM_IRQ)
		KUNIT_EXPECT_EQ(test, 0, test_unwind_irq(&u));
	else
		KUNIT_EXPECT_EQ(test, 0, unwindme_func1(&u));
}

static struct kunit_case unwind_test_cases[] = {
	KUNIT_CASE_PARAM(test_unwind_flags, test_unwind_gen_params),
	{}
};

static struct kunit_suite test_unwind_suite = {
	.name = "test_unwind",
	.test_cases = unwind_test_cases,
};

kunit_test_suites(&test_unwind_suite);

MODULE_LICENSE("GPL");

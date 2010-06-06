/*
 * arch/arm/common/fiq_debugger.c
 *
 * Serial Debugger Interface accessed through an FIQ interrupt.
 *
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/kernel_debugger.h>
#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/wakelock.h>

#include <asm/fiq_debugger.h>
#include <asm/fiq_glue.h>
#include <asm/stacktrace.h>

#include <mach/system.h>

#include <linux/uaccess.h>

#define DEBUG_MAX 64

struct fiq_debugger_state {
	struct fiq_glue_handler handler;

	int fiq;
	int signal_irq;
	int wakeup_irq;
	bool wakeup_irq_no_set_wake;
	struct clk *clk;
	struct fiq_debugger_pdata *pdata;
	struct platform_device *pdev;

	char debug_cmd[DEBUG_MAX];
	int debug_busy;
	int debug_abort;

	char debug_buf[DEBUG_MAX];
	int debug_count;

	bool no_sleep;
	bool debug_enable;
	bool ignore_next_wakeup_irq;
	struct timer_list sleep_timer;
	bool uart_clk_enabled;
	struct wake_lock debugger_wake_lock;

	unsigned int last_irqs[NR_IRQS];
};

#ifdef CONFIG_FIQ_DEBUGGER_NO_SLEEP
static bool initial_no_sleep = true;
#else
static bool initial_no_sleep;
#endif
static bool initial_debug_enable;

module_param_named(no_sleep, initial_no_sleep, bool, 0644);
module_param_named(debug_enable, initial_debug_enable, bool, 0644);

#ifdef CONFIG_FIQ_DEBUGGER_WAKEUP_IRQ_ALWAYS_ON
static inline void enable_wakeup_irq(struct fiq_debugger_state *state) {}
static inline void disable_wakeup_irq(struct fiq_debugger_state *state) {}
#else
static inline void enable_wakeup_irq(struct fiq_debugger_state *state)
{
	if (state->wakeup_irq < 0)
		return;
	enable_irq(state->wakeup_irq);
	if (!state->wakeup_irq_no_set_wake)
		enable_irq_wake(state->wakeup_irq);
}
static inline void disable_wakeup_irq(struct fiq_debugger_state *state)
{
	if (state->wakeup_irq < 0)
		return;
	disable_irq_nosync(state->wakeup_irq);
	if (!state->wakeup_irq_no_set_wake)
		disable_irq_wake(state->wakeup_irq);
}
#endif

static void debug_force_irq(struct fiq_debugger_state *state)
{
	unsigned int irq = state->signal_irq;
	if (state->pdata->force_irq)
		state->pdata->force_irq(state->pdev, irq);
	else {
		struct irq_chip *chip = get_irq_chip(irq);
		if (chip && chip->retrigger)
			chip->retrigger(irq);
	}
}

static void debug_uart_flush(struct fiq_debugger_state *state)
{
	if (state->pdata->uart_flush)
		state->pdata->uart_flush(state->pdev);
}

static void debug_puts(struct fiq_debugger_state *state, char *s)
{
	unsigned c;
	while ((c = *s++)) {
		if (c == '\n')
			state->pdata->uart_putc(state->pdev, '\r');
		state->pdata->uart_putc(state->pdev, c);
	}
}

static void debug_prompt(struct fiq_debugger_state *state)
{
	debug_puts(state, "debug> ");
}

int log_buf_copy(char *dest, int idx, int len);
static void dump_kernel_log(struct fiq_debugger_state *state)
{
	char buf[1024];
	int idx = 0;
	int ret;
	int saved_oip;

	/* setting oops_in_progress prevents log_buf_copy()
	 * from trying to take a spinlock which will make it
	 * very unhappy in some cases...
	 */
	saved_oip = oops_in_progress;
	oops_in_progress = 1;
	for (;;) {
		ret = log_buf_copy(buf, idx, 1023);
		if (ret <= 0)
			break;
		buf[ret] = 0;
		debug_puts(state, buf);
		idx += ret;
	}
	oops_in_progress = saved_oip;
}

static char *mode_name(unsigned cpsr)
{
	switch (cpsr & MODE_MASK) {
	case USR_MODE: return "USR";
	case FIQ_MODE: return "FIQ";
	case IRQ_MODE: return "IRQ";
	case SVC_MODE: return "SVC";
	case ABT_MODE: return "ABT";
	case UND_MODE: return "UND";
	case SYSTEM_MODE: return "SYS";
	default: return "???";
	}
}

static int debug_printf(void *cookie, const char *fmt, ...)
{
	struct fiq_debugger_state *state = cookie;
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	debug_puts(state, buf);
	return state->debug_abort;
}

/* Safe outside fiq context */
static int debug_printf_nfiq(void *cookie, const char *fmt, ...)
{
	struct fiq_debugger_state *state = cookie;
	char buf[256];
	va_list ap;
	unsigned long irq_flags;

	va_start(ap, fmt);
	vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	local_irq_save(irq_flags);
	debug_puts(state, buf);
	debug_uart_flush(state);
	local_irq_restore(irq_flags);
	return state->debug_abort;
}

static void dump_regs(struct fiq_debugger_state *state, unsigned *regs)
{
	debug_printf(state, " r0 %08x  r1 %08x  r2 %08x  r3 %08x\n",
			regs[0], regs[1], regs[2], regs[3]);
	debug_printf(state, " r4 %08x  r5 %08x  r6 %08x  r7 %08x\n",
			regs[4], regs[5], regs[6], regs[7]);
	debug_printf(state, " r8 %08x  r9 %08x r10 %08x r11 %08x  mode %s\n",
			regs[8], regs[9], regs[10], regs[11],
			mode_name(regs[16]));
	if ((regs[16] & MODE_MASK) == USR_MODE)
		debug_printf(state, " ip %08x  sp %08x  lr %08x  pc %08x  "
				"cpsr %08x\n", regs[12], regs[13], regs[14],
				regs[15], regs[16]);
	else
		debug_printf(state, " ip %08x  sp %08x  lr %08x  pc %08x  "
				"cpsr %08x  spsr %08x\n", regs[12], regs[13],
				regs[14], regs[15], regs[16], regs[17]);
}

struct mode_regs {
	unsigned long sp_svc;
	unsigned long lr_svc;
	unsigned long spsr_svc;

	unsigned long sp_abt;
	unsigned long lr_abt;
	unsigned long spsr_abt;

	unsigned long sp_und;
	unsigned long lr_und;
	unsigned long spsr_und;

	unsigned long sp_irq;
	unsigned long lr_irq;
	unsigned long spsr_irq;

	unsigned long r8_fiq;
	unsigned long r9_fiq;
	unsigned long r10_fiq;
	unsigned long r11_fiq;
	unsigned long r12_fiq;
	unsigned long sp_fiq;
	unsigned long lr_fiq;
	unsigned long spsr_fiq;
};

void __naked get_mode_regs(struct mode_regs *regs)
{
	asm volatile (
	"mrs	r1, cpsr\n"
	"msr	cpsr_c, #0xd3 @(SVC_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd7 @(ABT_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xdb @(UND_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd2 @(IRQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd1 @(FIQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r8 - r14}\n"
	"mrs	r2, spsr\n"
	"stmia	r0!, {r2}\n"
	"msr	cpsr_c, r1\n"
	"bx	lr\n");
}


static void dump_allregs(struct fiq_debugger_state *state, unsigned *regs)
{
	struct mode_regs mode_regs;
	dump_regs(state, regs);
	get_mode_regs(&mode_regs);
	debug_printf(state, " svc: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_svc, mode_regs.lr_svc, mode_regs.spsr_svc);
	debug_printf(state, " abt: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_abt, mode_regs.lr_abt, mode_regs.spsr_abt);
	debug_printf(state, " und: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_und, mode_regs.lr_und, mode_regs.spsr_und);
	debug_printf(state, " irq: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_irq, mode_regs.lr_irq, mode_regs.spsr_irq);
	debug_printf(state, " fiq: r8 %08x  r9 %08x  r10 %08x  r11 %08x  "
			"r12 %08x\n",
			mode_regs.r8_fiq, mode_regs.r9_fiq, mode_regs.r10_fiq,
			mode_regs.r11_fiq, mode_regs.r12_fiq);
	debug_printf(state, " fiq: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_fiq, mode_regs.lr_fiq, mode_regs.spsr_fiq);
}

static void dump_irqs(struct fiq_debugger_state *state)
{
	int n;
	debug_printf(state, "irqnr       total  since-last   status  name\n");
	for (n = 0; n < NR_IRQS; n++) {
		struct irqaction *act = irq_desc[n].action;
		if (!act && !kstat_irqs(n))
			continue;
		debug_printf(state, "%5d: %10u %11u %8x  %s\n", n,
			kstat_irqs(n),
			kstat_irqs(n) - state->last_irqs[n],
			irq_desc[n].status,
			(act && act->name) ? act->name : "???");
		state->last_irqs[n] = kstat_irqs(n);
	}
}

struct stacktrace_state {
	struct fiq_debugger_state *state;
	unsigned int depth;
};

static int report_trace(struct stackframe *frame, void *d)
{
	struct stacktrace_state *sts = d;

	if (sts->depth) {
		debug_printf(sts->state,
			"  pc: %p (%pF), lr %p (%pF), sp %p, fp %p\n",
			frame->pc, frame->pc, frame->lr, frame->lr,
			frame->sp, frame->fp);
		sts->depth--;
		return 0;
	}
	debug_printf(sts->state, "  ...\n");

	return sts->depth == 0;
}

struct frame_tail {
	struct frame_tail *fp;
	unsigned long sp;
	unsigned long lr;
} __attribute__((packed));

static struct frame_tail *user_backtrace(struct fiq_debugger_state *state,
					struct frame_tail *tail)
{
	struct frame_tail buftail[2];

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail))) {
		debug_printf(state, "  invalid frame pointer %p\n", tail);
		return NULL;
	}
	if (__copy_from_user_inatomic(buftail, tail, sizeof(buftail))) {
		debug_printf(state,
			"  failed to copy frame pointer %p\n", tail);
		return NULL;
	}

	debug_printf(state, "  %p\n", buftail[0].lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail >= buftail[0].fp)
		return NULL;

	return buftail[0].fp-1;
}

void dump_stacktrace(struct fiq_debugger_state *state,
		struct pt_regs * const regs, unsigned int depth, void *ssp)
{
	struct frame_tail *tail;
	struct thread_info *real_thread_info = (struct thread_info *)
				((unsigned long)ssp & ~(THREAD_SIZE - 1));
	struct stacktrace_state sts;

	sts.depth = depth;
	sts.state = state;
	*current_thread_info() = *real_thread_info;

	if (!current)
		debug_printf(state, "current NULL\n");
	else
		debug_printf(state, "pid: %d  comm: %s\n",
			current->pid, current->comm);
	dump_regs(state, (unsigned *)regs);

	if (!user_mode(regs)) {
		struct stackframe frame;
		frame.fp = regs->ARM_fp;
		frame.sp = regs->ARM_sp;
		frame.lr = regs->ARM_lr;
		frame.pc = regs->ARM_pc;
		debug_printf(state,
			"  pc: %p (%pF), lr %p (%pF), sp %p, fp %p\n",
			regs->ARM_pc, regs->ARM_pc, regs->ARM_lr, regs->ARM_lr,
			regs->ARM_sp, regs->ARM_fp);
		walk_stackframe(&frame, report_trace, &sts);
		return;
	}

	tail = ((struct frame_tail *) regs->ARM_fp) - 1;
	while (depth-- && tail && !((unsigned long) tail & 3))
		tail = user_backtrace(state, tail);
}

static void debug_exec(struct fiq_debugger_state *state,
			const char *cmd, unsigned *regs, void *svc_sp)
{
	if (!strcmp(cmd, "pc")) {
		debug_printf(state, " pc %08x cpsr %08x mode %s\n",
			regs[15], regs[16], mode_name(regs[16]));
	} else if (!strcmp(cmd, "regs")) {
		dump_regs(state, regs);
	} else if (!strcmp(cmd, "allregs")) {
		dump_allregs(state, regs);
	} else if (!strcmp(cmd, "bt")) {
		dump_stacktrace(state, (struct pt_regs *)regs, 100, svc_sp);
	} else if (!strcmp(cmd, "reboot")) {
		arch_reset(0, 0);
	} else if (!strcmp(cmd, "irqs")) {
		dump_irqs(state);
	} else if (!strcmp(cmd, "kmsg")) {
		dump_kernel_log(state);
	} else if (!strcmp(cmd, "version")) {
		debug_printf(state, "%s\n", linux_banner);
	} else if (!strcmp(cmd, "sleep")) {
		state->no_sleep = false;
	} else if (!strcmp(cmd, "nosleep")) {
		state->no_sleep = true;
	} else {
		if (state->debug_busy) {
			debug_printf(state,
				"command processor busy. trying to abort.\n");
			state->debug_abort = -1;
		} else {
			strcpy(state->debug_cmd, cmd);
			state->debug_busy = 1;
		}

		debug_force_irq(state);

		return;
	}
	debug_prompt(state);
}

static void sleep_timer_expired(unsigned long data)
{
	struct fiq_debugger_state *state = (struct fiq_debugger_state *)data;

	if (state->uart_clk_enabled && !state->no_sleep) {
		if (state->debug_enable) {
			state->debug_enable = false;
			debug_printf_nfiq(state, "suspending fiq debugger\n");
		}
		state->ignore_next_wakeup_irq = true;
		if (state->clk)
			clk_disable(state->clk);
		state->uart_clk_enabled = false;
		enable_wakeup_irq(state);
	}
	wake_unlock(&state->debugger_wake_lock);
}

static irqreturn_t wakeup_irq_handler(int irq, void *dev)
{
	struct fiq_debugger_state *state = dev;

	if (!state->no_sleep)
		debug_puts(state, "WAKEUP\n");
	if (state->ignore_next_wakeup_irq)
		state->ignore_next_wakeup_irq = false;
	else if (!state->uart_clk_enabled) {
		wake_lock(&state->debugger_wake_lock);
		if (state->clk)
			clk_enable(state->clk);
		state->uart_clk_enabled = true;
		disable_wakeup_irq(state);
		mod_timer(&state->sleep_timer, jiffies + HZ / 2);
	}
	return IRQ_HANDLED;
}

static irqreturn_t debug_irq(int irq, void *dev)
{
	struct fiq_debugger_state *state = dev;
	if (state->pdata->force_irq_ack)
		state->pdata->force_irq_ack(state->pdev, state->signal_irq);

	if (!state->no_sleep) {
		wake_lock(&state->debugger_wake_lock);
		mod_timer(&state->sleep_timer, jiffies + HZ * 5);
	}
	if (state->debug_busy) {
		struct kdbg_ctxt ctxt;

		ctxt.printf = debug_printf_nfiq;
		ctxt.cookie = state;
		kernel_debugger(&ctxt, state->debug_cmd);
		debug_prompt(state);

		state->debug_busy = 0;
	}
	return IRQ_HANDLED;
}

static int debug_getc(struct fiq_debugger_state *state)
{
	return state->pdata->uart_getc(state->pdev);
}

static void debug_fiq(struct fiq_glue_handler *h, void *regs, void *svc_sp)
{
	struct fiq_debugger_state *state =
		container_of(h, struct fiq_debugger_state, handler);
	int c;
	static int last_c;
	int count = 0;

	while ((c = debug_getc(state)) != FIQ_DEBUGGER_NO_CHAR) {
		count++;
		if (!state->debug_enable) {
			if ((c == 13) || (c == 10)) {
				state->debug_enable = true;
				state->debug_count = 0;
				debug_prompt(state);
			}
		} else if ((c >= ' ') && (c < 127)) {
			if (state->debug_count < (DEBUG_MAX - 1)) {
				state->debug_buf[state->debug_count++] = c;
				state->pdata->uart_putc(state->pdev, c);
			}
		} else if ((c == 8) || (c == 127)) {
			if (state->debug_count > 0) {
				state->debug_count--;
				state->pdata->uart_putc(state->pdev, 8);
				state->pdata->uart_putc(state->pdev, ' ');
				state->pdata->uart_putc(state->pdev, 8);
			}
		} else if ((c == 13) || (c == 10)) {
			if (c == '\r' || (c == '\n' && last_c != '\r')) {
				state->pdata->uart_putc(state->pdev, '\r');
				state->pdata->uart_putc(state->pdev, '\n');
			}
			if (state->debug_count) {
				state->debug_buf[state->debug_count] = 0;
				state->debug_count = 0;
				debug_exec(state, state->debug_buf,
					regs, svc_sp);
			} else {
				debug_prompt(state);
			}
		}
		last_c = c;
	}
	debug_uart_flush(state);
	if (state->pdata->fiq_ack)
		state->pdata->fiq_ack(state->pdev, state->fiq);

	/* poke sleep timer if necessary */
	if (state->debug_enable && !state->no_sleep)
		debug_force_irq(state);
}

static void debug_resume(struct fiq_glue_handler *h)
{
	struct fiq_debugger_state *state =
		container_of(h, struct fiq_debugger_state, handler);
	if (state->pdata->uart_resume)
		state->pdata->uart_resume(state->pdev);
}

static int fiq_debugger_probe(struct platform_device *pdev)
{
	int ret;
	struct fiq_debugger_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct fiq_debugger_state *state;

	if (!pdata->uart_getc || !pdata->uart_putc || !pdata->fiq_enable)
		return -EINVAL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	state->handler.fiq = debug_fiq;
	state->handler.resume = debug_resume;
	setup_timer(&state->sleep_timer, sleep_timer_expired,
		    (unsigned long)state);
	state->pdata = pdata;
	state->pdev = pdev;
	state->no_sleep = initial_no_sleep;
	state->debug_enable = initial_debug_enable;

	state->fiq = platform_get_irq_byname(pdev, "fiq");
	state->signal_irq = platform_get_irq_byname(pdev, "signal");
	state->wakeup_irq = platform_get_irq_byname(pdev, "wakeup");

	if (state->wakeup_irq < 0)
		state->no_sleep = true;
	state->ignore_next_wakeup_irq = !state->no_sleep;

	wake_lock_init(&state->debugger_wake_lock,
			WAKE_LOCK_SUSPEND, "serial-debug");

	state->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(state->clk))
		state->clk = NULL;

	if (state->clk)
		clk_enable(state->clk);

	if (pdata->uart_init) {
		ret = pdata->uart_init(pdev);
		if (ret)
			goto err_uart_init;
	}

	debug_printf_nfiq(state, "<hit enter %sto activate fiq debugger>\n",
				state->no_sleep ? "" : "twice ");

	ret = fiq_glue_register_handler(&state->handler);
	if (ret) {
		pr_err("serial_debugger: could not install fiq handler\n");
		goto err_register_fiq;
	}

	pdata->fiq_enable(pdev, state->fiq, 1);

	if (state->clk)
		clk_disable(state->clk);

	ret = request_irq(state->signal_irq, debug_irq,
			  IRQF_TRIGGER_RISING, "debug", state);
	if (ret)
		pr_err("serial_debugger: could not install signal_irq");

	if (state->wakeup_irq >= 0) {
		ret = request_irq(state->wakeup_irq, wakeup_irq_handler,
				  IRQF_TRIGGER_FALLING | IRQF_DISABLED,
				  "debug-wakeup", state);
		if (ret) {
			pr_err("serial_debugger: "
				"could not install wakeup irq\n");
			state->wakeup_irq = -1;
		} else {
			ret = enable_irq_wake(state->wakeup_irq);
			if (ret) {
				pr_err("serial_debugger: "
					"could not enable wakeup\n");
				state->wakeup_irq_no_set_wake = true;
			}
		}
	}
	if (state->no_sleep)
		wakeup_irq_handler(state->wakeup_irq, state);

	return 0;

err_register_fiq:
	if (pdata->uart_free)
		pdata->uart_free(pdev);
err_uart_init:
	kfree(state);
	if (state->clk)
		clk_put(state->clk);
	return ret;
}

static struct platform_driver fiq_debugger_driver = {
	.probe = fiq_debugger_probe,
	.driver.name = "fiq_debugger",
};

static int __init fiq_debugger_init(void)
{
	return platform_driver_register(&fiq_debugger_driver);
}

postcore_initcall(fiq_debugger_init);

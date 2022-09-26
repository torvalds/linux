// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Procedures for interfacing to the RTAS on CHRP machines.
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 */

#include <linux/stdarg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/of.h>
#include <linux/of_fdt.h>

#include <asm/interrupt.h>
#include <asm/rtas.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/delay.h>
#include <linux/uaccess.h>
#include <asm/udbg.h>
#include <asm/syscalls.h>
#include <asm/smp.h>
#include <linux/atomic.h>
#include <asm/time.h>
#include <asm/mmu.h>
#include <asm/topology.h>

/* This is here deliberately so it's only used in this file */
void enter_rtas(unsigned long);

static inline void do_enter_rtas(unsigned long args)
{
	unsigned long msr;

	/*
	 * Make sure MSR[RI] is currently enabled as it will be forced later
	 * in enter_rtas.
	 */
	msr = mfmsr();
	BUG_ON(!(msr & MSR_RI));

	BUG_ON(!irqs_disabled());

	hard_irq_disable(); /* Ensure MSR[EE] is disabled on PPC64 */

	enter_rtas(args);

	srr_regs_clobbered(); /* rtas uses SRRs, invalidate */
}

struct rtas_t rtas = {
	.lock = __ARCH_SPIN_LOCK_UNLOCKED
};
EXPORT_SYMBOL(rtas);

DEFINE_SPINLOCK(rtas_data_buf_lock);
EXPORT_SYMBOL(rtas_data_buf_lock);

char rtas_data_buf[RTAS_DATA_BUF_SIZE] __cacheline_aligned;
EXPORT_SYMBOL(rtas_data_buf);

unsigned long rtas_rmo_buf;

/*
 * If non-NULL, this gets called when the kernel terminates.
 * This is done like this so rtas_flash can be a module.
 */
void (*rtas_flash_term_hook)(int);
EXPORT_SYMBOL(rtas_flash_term_hook);

/* RTAS use home made raw locking instead of spin_lock_irqsave
 * because those can be called from within really nasty contexts
 * such as having the timebase stopped which would lockup with
 * normal locks and spinlock debugging enabled
 */
static unsigned long lock_rtas(void)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	arch_spin_lock(&rtas.lock);
	return flags;
}

static void unlock_rtas(unsigned long flags)
{
	arch_spin_unlock(&rtas.lock);
	local_irq_restore(flags);
	preempt_enable();
}

/*
 * call_rtas_display_status and call_rtas_display_status_delay
 * are designed only for very early low-level debugging, which
 * is why the token is hard-coded to 10.
 */
static void call_rtas_display_status(unsigned char c)
{
	unsigned long s;

	if (!rtas.base)
		return;

	s = lock_rtas();
	rtas_call_unlocked(&rtas.args, 10, 1, 1, NULL, c);
	unlock_rtas(s);
}

static void call_rtas_display_status_delay(char c)
{
	static int pending_newline = 0;  /* did last write end with unprinted newline? */
	static int width = 16;

	if (c == '\n') {	
		while (width-- > 0)
			call_rtas_display_status(' ');
		width = 16;
		mdelay(500);
		pending_newline = 1;
	} else {
		if (pending_newline) {
			call_rtas_display_status('\r');
			call_rtas_display_status('\n');
		} 
		pending_newline = 0;
		if (width--) {
			call_rtas_display_status(c);
			udelay(10000);
		}
	}
}

void __init udbg_init_rtas_panel(void)
{
	udbg_putc = call_rtas_display_status_delay;
}

#ifdef CONFIG_UDBG_RTAS_CONSOLE

/* If you think you're dying before early_init_dt_scan_rtas() does its
 * work, you can hard code the token values for your firmware here and
 * hardcode rtas.base/entry etc.
 */
static unsigned int rtas_putchar_token = RTAS_UNKNOWN_SERVICE;
static unsigned int rtas_getchar_token = RTAS_UNKNOWN_SERVICE;

static void udbg_rtascon_putc(char c)
{
	int tries;

	if (!rtas.base)
		return;

	/* Add CRs before LFs */
	if (c == '\n')
		udbg_rtascon_putc('\r');

	/* if there is more than one character to be displayed, wait a bit */
	for (tries = 0; tries < 16; tries++) {
		if (rtas_call(rtas_putchar_token, 1, 1, NULL, c) == 0)
			break;
		udelay(1000);
	}
}

static int udbg_rtascon_getc_poll(void)
{
	int c;

	if (!rtas.base)
		return -1;

	if (rtas_call(rtas_getchar_token, 0, 2, &c))
		return -1;

	return c;
}

static int udbg_rtascon_getc(void)
{
	int c;

	while ((c = udbg_rtascon_getc_poll()) == -1)
		;

	return c;
}


void __init udbg_init_rtas_console(void)
{
	udbg_putc = udbg_rtascon_putc;
	udbg_getc = udbg_rtascon_getc;
	udbg_getc_poll = udbg_rtascon_getc_poll;
}
#endif /* CONFIG_UDBG_RTAS_CONSOLE */

void rtas_progress(char *s, unsigned short hex)
{
	struct device_node *root;
	int width;
	const __be32 *p;
	char *os;
	static int display_character, set_indicator;
	static int display_width, display_lines, form_feed;
	static const int *row_width;
	static DEFINE_SPINLOCK(progress_lock);
	static int current_line;
	static int pending_newline = 0;  /* did last write end with unprinted newline? */

	if (!rtas.base)
		return;

	if (display_width == 0) {
		display_width = 0x10;
		if ((root = of_find_node_by_path("/rtas"))) {
			if ((p = of_get_property(root,
					"ibm,display-line-length", NULL)))
				display_width = be32_to_cpu(*p);
			if ((p = of_get_property(root,
					"ibm,form-feed", NULL)))
				form_feed = be32_to_cpu(*p);
			if ((p = of_get_property(root,
					"ibm,display-number-of-lines", NULL)))
				display_lines = be32_to_cpu(*p);
			row_width = of_get_property(root,
					"ibm,display-truncation-length", NULL);
			of_node_put(root);
		}
		display_character = rtas_token("display-character");
		set_indicator = rtas_token("set-indicator");
	}

	if (display_character == RTAS_UNKNOWN_SERVICE) {
		/* use hex display if available */
		if (set_indicator != RTAS_UNKNOWN_SERVICE)
			rtas_call(set_indicator, 3, 1, NULL, 6, 0, hex);
		return;
	}

	spin_lock(&progress_lock);

	/*
	 * Last write ended with newline, but we didn't print it since
	 * it would just clear the bottom line of output. Print it now
	 * instead.
	 *
	 * If no newline is pending and form feed is supported, clear the
	 * display with a form feed; otherwise, print a CR to start output
	 * at the beginning of the line.
	 */
	if (pending_newline) {
		rtas_call(display_character, 1, 1, NULL, '\r');
		rtas_call(display_character, 1, 1, NULL, '\n');
		pending_newline = 0;
	} else {
		current_line = 0;
		if (form_feed)
			rtas_call(display_character, 1, 1, NULL,
				  (char)form_feed);
		else
			rtas_call(display_character, 1, 1, NULL, '\r');
	}
 
	if (row_width)
		width = row_width[current_line];
	else
		width = display_width;
	os = s;
	while (*os) {
		if (*os == '\n' || *os == '\r') {
			/* If newline is the last character, save it
			 * until next call to avoid bumping up the
			 * display output.
			 */
			if (*os == '\n' && !os[1]) {
				pending_newline = 1;
				current_line++;
				if (current_line > display_lines-1)
					current_line = display_lines-1;
				spin_unlock(&progress_lock);
				return;
			}
 
			/* RTAS wants CR-LF, not just LF */
 
			if (*os == '\n') {
				rtas_call(display_character, 1, 1, NULL, '\r');
				rtas_call(display_character, 1, 1, NULL, '\n');
			} else {
				/* CR might be used to re-draw a line, so we'll
				 * leave it alone and not add LF.
				 */
				rtas_call(display_character, 1, 1, NULL, *os);
			}
 
			if (row_width)
				width = row_width[current_line];
			else
				width = display_width;
		} else {
			width--;
			rtas_call(display_character, 1, 1, NULL, *os);
		}
 
		os++;
 
		/* if we overwrite the screen length */
		if (width <= 0)
			while ((*os != 0) && (*os != '\n') && (*os != '\r'))
				os++;
	}
 
	spin_unlock(&progress_lock);
}
EXPORT_SYMBOL(rtas_progress);		/* needed by rtas_flash module */

int rtas_token(const char *service)
{
	const __be32 *tokp;
	if (rtas.dev == NULL)
		return RTAS_UNKNOWN_SERVICE;
	tokp = of_get_property(rtas.dev, service, NULL);
	return tokp ? be32_to_cpu(*tokp) : RTAS_UNKNOWN_SERVICE;
}
EXPORT_SYMBOL(rtas_token);

int rtas_service_present(const char *service)
{
	return rtas_token(service) != RTAS_UNKNOWN_SERVICE;
}
EXPORT_SYMBOL(rtas_service_present);

#ifdef CONFIG_RTAS_ERROR_LOGGING
/*
 * Return the firmware-specified size of the error log buffer
 *  for all rtas calls that require an error buffer argument.
 *  This includes 'check-exception' and 'rtas-last-error'.
 */
int rtas_get_error_log_max(void)
{
	static int rtas_error_log_max;
	if (rtas_error_log_max)
		return rtas_error_log_max;

	rtas_error_log_max = rtas_token ("rtas-error-log-max");
	if ((rtas_error_log_max == RTAS_UNKNOWN_SERVICE) ||
	    (rtas_error_log_max > RTAS_ERROR_LOG_MAX)) {
		printk (KERN_WARNING "RTAS: bad log buffer size %d\n",
			rtas_error_log_max);
		rtas_error_log_max = RTAS_ERROR_LOG_MAX;
	}
	return rtas_error_log_max;
}
EXPORT_SYMBOL(rtas_get_error_log_max);


static char rtas_err_buf[RTAS_ERROR_LOG_MAX];
static int rtas_last_error_token;

/** Return a copy of the detailed error text associated with the
 *  most recent failed call to rtas.  Because the error text
 *  might go stale if there are any other intervening rtas calls,
 *  this routine must be called atomically with whatever produced
 *  the error (i.e. with rtas.lock still held from the previous call).
 */
static char *__fetch_rtas_last_error(char *altbuf)
{
	struct rtas_args err_args, save_args;
	u32 bufsz;
	char *buf = NULL;

	if (rtas_last_error_token == -1)
		return NULL;

	bufsz = rtas_get_error_log_max();

	err_args.token = cpu_to_be32(rtas_last_error_token);
	err_args.nargs = cpu_to_be32(2);
	err_args.nret = cpu_to_be32(1);
	err_args.args[0] = cpu_to_be32(__pa(rtas_err_buf));
	err_args.args[1] = cpu_to_be32(bufsz);
	err_args.args[2] = 0;

	save_args = rtas.args;
	rtas.args = err_args;

	do_enter_rtas(__pa(&rtas.args));

	err_args = rtas.args;
	rtas.args = save_args;

	/* Log the error in the unlikely case that there was one. */
	if (unlikely(err_args.args[2] == 0)) {
		if (altbuf) {
			buf = altbuf;
		} else {
			buf = rtas_err_buf;
			if (slab_is_available())
				buf = kmalloc(RTAS_ERROR_LOG_MAX, GFP_ATOMIC);
		}
		if (buf)
			memcpy(buf, rtas_err_buf, RTAS_ERROR_LOG_MAX);
	}

	return buf;
}

#define get_errorlog_buffer()	kmalloc(RTAS_ERROR_LOG_MAX, GFP_KERNEL)

#else /* CONFIG_RTAS_ERROR_LOGGING */
#define __fetch_rtas_last_error(x)	NULL
#define get_errorlog_buffer()		NULL
#endif


static void
va_rtas_call_unlocked(struct rtas_args *args, int token, int nargs, int nret,
		      va_list list)
{
	int i;

	args->token = cpu_to_be32(token);
	args->nargs = cpu_to_be32(nargs);
	args->nret  = cpu_to_be32(nret);
	args->rets  = &(args->args[nargs]);

	for (i = 0; i < nargs; ++i)
		args->args[i] = cpu_to_be32(va_arg(list, __u32));

	for (i = 0; i < nret; ++i)
		args->rets[i] = 0;

	do_enter_rtas(__pa(args));
}

void rtas_call_unlocked(struct rtas_args *args, int token, int nargs, int nret, ...)
{
	va_list list;

	va_start(list, nret);
	va_rtas_call_unlocked(args, token, nargs, nret, list);
	va_end(list);
}

static int ibm_open_errinjct_token;
static int ibm_errinjct_token;

int rtas_call(int token, int nargs, int nret, int *outputs, ...)
{
	va_list list;
	int i;
	unsigned long s;
	struct rtas_args *rtas_args;
	char *buff_copy = NULL;
	int ret;

	if (!rtas.entry || token == RTAS_UNKNOWN_SERVICE)
		return -1;

	if (token == ibm_open_errinjct_token || token == ibm_errinjct_token) {
		/*
		 * It would be nicer to not discard the error value
		 * from security_locked_down(), but callers expect an
		 * RTAS status, not an errno.
		 */
		if (security_locked_down(LOCKDOWN_RTAS_ERROR_INJECTION))
			return -1;
	}

	if ((mfmsr() & (MSR_IR|MSR_DR)) != (MSR_IR|MSR_DR)) {
		WARN_ON_ONCE(1);
		return -1;
	}

	s = lock_rtas();

	/* We use the global rtas args buffer */
	rtas_args = &rtas.args;

	va_start(list, outputs);
	va_rtas_call_unlocked(rtas_args, token, nargs, nret, list);
	va_end(list);

	/* A -1 return code indicates that the last command couldn't
	   be completed due to a hardware error. */
	if (be32_to_cpu(rtas_args->rets[0]) == -1)
		buff_copy = __fetch_rtas_last_error(NULL);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = be32_to_cpu(rtas_args->rets[i+1]);
	ret = (nret > 0)? be32_to_cpu(rtas_args->rets[0]): 0;

	unlock_rtas(s);

	if (buff_copy) {
		log_error(buff_copy, ERR_TYPE_RTAS_LOG, 0);
		if (slab_is_available())
			kfree(buff_copy);
	}
	return ret;
}
EXPORT_SYMBOL(rtas_call);

/**
 * rtas_busy_delay_time() - From an RTAS status value, calculate the
 *                          suggested delay time in milliseconds.
 *
 * @status: a value returned from rtas_call() or similar APIs which return
 *          the status of a RTAS function call.
 *
 * Context: Any context.
 *
 * Return:
 * * 100000 - If @status is 9905.
 * * 10000  - If @status is 9904.
 * * 1000   - If @status is 9903.
 * * 100    - If @status is 9902.
 * * 10     - If @status is 9901.
 * * 1      - If @status is either 9900 or -2. This is "wrong" for -2, but
 *            some callers depend on this behavior, and the worst outcome
 *            is that they will delay for longer than necessary.
 * * 0      - If @status is not a busy or extended delay value.
 */
unsigned int rtas_busy_delay_time(int status)
{
	int order;
	unsigned int ms = 0;

	if (status == RTAS_BUSY) {
		ms = 1;
	} else if (status >= RTAS_EXTENDED_DELAY_MIN &&
		   status <= RTAS_EXTENDED_DELAY_MAX) {
		order = status - RTAS_EXTENDED_DELAY_MIN;
		for (ms = 1; order > 0; order--)
			ms *= 10;
	}

	return ms;
}
EXPORT_SYMBOL(rtas_busy_delay_time);

/**
 * rtas_busy_delay() - helper for RTAS busy and extended delay statuses
 *
 * @status: a value returned from rtas_call() or similar APIs which return
 *          the status of a RTAS function call.
 *
 * Context: Process context. May sleep or schedule.
 *
 * Return:
 * * true  - @status is RTAS_BUSY or an extended delay hint. The
 *           caller may assume that the CPU has been yielded if necessary,
 *           and that an appropriate delay for @status has elapsed.
 *           Generally the caller should reattempt the RTAS call which
 *           yielded @status.
 *
 * * false - @status is not @RTAS_BUSY nor an extended delay hint. The
 *           caller is responsible for handling @status.
 */
bool rtas_busy_delay(int status)
{
	unsigned int ms;
	bool ret;

	switch (status) {
	case RTAS_EXTENDED_DELAY_MIN...RTAS_EXTENDED_DELAY_MAX:
		ret = true;
		ms = rtas_busy_delay_time(status);
		/*
		 * The extended delay hint can be as high as 100 seconds.
		 * Surely any function returning such a status is either
		 * buggy or isn't going to be significantly slowed by us
		 * polling at 1HZ. Clamp the sleep time to one second.
		 */
		ms = clamp(ms, 1U, 1000U);
		/*
		 * The delay hint is an order-of-magnitude suggestion, not
		 * a minimum. It is fine, possibly even advantageous, for
		 * us to pause for less time than hinted. For small values,
		 * use usleep_range() to ensure we don't sleep much longer
		 * than actually needed.
		 *
		 * See Documentation/timers/timers-howto.rst for
		 * explanation of the threshold used here. In effect we use
		 * usleep_range() for 9900 and 9901, msleep() for
		 * 9902-9905.
		 */
		if (ms <= 20)
			usleep_range(ms * 100, ms * 1000);
		else
			msleep(ms);
		break;
	case RTAS_BUSY:
		ret = true;
		/*
		 * We should call again immediately if there's no other
		 * work to do.
		 */
		cond_resched();
		break;
	default:
		ret = false;
		/*
		 * Not a busy or extended delay status; the caller should
		 * handle @status itself. Ensure we warn on misuses in
		 * atomic context regardless.
		 */
		might_sleep();
		break;
	}

	return ret;
}
EXPORT_SYMBOL(rtas_busy_delay);

static int rtas_error_rc(int rtas_rc)
{
	int rc;

	switch (rtas_rc) {
		case -1: 		/* Hardware Error */
			rc = -EIO;
			break;
		case -3:		/* Bad indicator/domain/etc */
			rc = -EINVAL;
			break;
		case -9000:		/* Isolation error */
			rc = -EFAULT;
			break;
		case -9001:		/* Outstanding TCE/PTE */
			rc = -EEXIST;
			break;
		case -9002:		/* No usable slot */
			rc = -ENODEV;
			break;
		default:
			printk(KERN_ERR "%s: unexpected RTAS error %d\n",
					__func__, rtas_rc);
			rc = -ERANGE;
			break;
	}
	return rc;
}

int rtas_get_power_level(int powerdomain, int *level)
{
	int token = rtas_token("get-power-level");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	while ((rc = rtas_call(token, 1, 2, level, powerdomain)) == RTAS_BUSY)
		udelay(1);

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}
EXPORT_SYMBOL(rtas_get_power_level);

int rtas_set_power_level(int powerdomain, int level, int *setlevel)
{
	int token = rtas_token("set-power-level");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	do {
		rc = rtas_call(token, 2, 2, setlevel, powerdomain, level);
	} while (rtas_busy_delay(rc));

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}
EXPORT_SYMBOL(rtas_set_power_level);

int rtas_get_sensor(int sensor, int index, int *state)
{
	int token = rtas_token("get-sensor-state");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	do {
		rc = rtas_call(token, 2, 2, state, sensor, index);
	} while (rtas_busy_delay(rc));

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}
EXPORT_SYMBOL(rtas_get_sensor);

int rtas_get_sensor_fast(int sensor, int index, int *state)
{
	int token = rtas_token("get-sensor-state");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	rc = rtas_call(token, 2, 2, state, sensor, index);
	WARN_ON(rc == RTAS_BUSY || (rc >= RTAS_EXTENDED_DELAY_MIN &&
				    rc <= RTAS_EXTENDED_DELAY_MAX));

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}

bool rtas_indicator_present(int token, int *maxindex)
{
	int proplen, count, i;
	const struct indicator_elem {
		__be32 token;
		__be32 maxindex;
	} *indicators;

	indicators = of_get_property(rtas.dev, "rtas-indicators", &proplen);
	if (!indicators)
		return false;

	count = proplen / sizeof(struct indicator_elem);

	for (i = 0; i < count; i++) {
		if (__be32_to_cpu(indicators[i].token) != token)
			continue;
		if (maxindex)
			*maxindex = __be32_to_cpu(indicators[i].maxindex);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(rtas_indicator_present);

int rtas_set_indicator(int indicator, int index, int new_value)
{
	int token = rtas_token("set-indicator");
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	do {
		rc = rtas_call(token, 3, 1, NULL, indicator, index, new_value);
	} while (rtas_busy_delay(rc));

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}
EXPORT_SYMBOL(rtas_set_indicator);

/*
 * Ignoring RTAS extended delay
 */
int rtas_set_indicator_fast(int indicator, int index, int new_value)
{
	int rc;
	int token = rtas_token("set-indicator");

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	rc = rtas_call(token, 3, 1, NULL, indicator, index, new_value);

	WARN_ON(rc == RTAS_BUSY || (rc >= RTAS_EXTENDED_DELAY_MIN &&
				    rc <= RTAS_EXTENDED_DELAY_MAX));

	if (rc < 0)
		return rtas_error_rc(rc);

	return rc;
}

/**
 * rtas_ibm_suspend_me() - Call ibm,suspend-me to suspend the LPAR.
 *
 * @fw_status: RTAS call status will be placed here if not NULL.
 *
 * rtas_ibm_suspend_me() should be called only on a CPU which has
 * received H_CONTINUE from the H_JOIN hcall. All other active CPUs
 * should be waiting to return from H_JOIN.
 *
 * rtas_ibm_suspend_me() may suspend execution of the OS
 * indefinitely. Callers should take appropriate measures upon return, such as
 * resetting watchdog facilities.
 *
 * Callers may choose to retry this call if @fw_status is
 * %RTAS_THREADS_ACTIVE.
 *
 * Return:
 * 0          - The partition has resumed from suspend, possibly after
 *              migration to a different host.
 * -ECANCELED - The operation was aborted.
 * -EAGAIN    - There were other CPUs not in H_JOIN at the time of the call.
 * -EBUSY     - Some other condition prevented the suspend from succeeding.
 * -EIO       - Hardware/platform error.
 */
int rtas_ibm_suspend_me(int *fw_status)
{
	int fwrc;
	int ret;

	fwrc = rtas_call(rtas_token("ibm,suspend-me"), 0, 1, NULL);

	switch (fwrc) {
	case 0:
		ret = 0;
		break;
	case RTAS_SUSPEND_ABORTED:
		ret = -ECANCELED;
		break;
	case RTAS_THREADS_ACTIVE:
		ret = -EAGAIN;
		break;
	case RTAS_NOT_SUSPENDABLE:
	case RTAS_OUTSTANDING_COPROC:
		ret = -EBUSY;
		break;
	case -1:
	default:
		ret = -EIO;
		break;
	}

	if (fw_status)
		*fw_status = fwrc;

	return ret;
}

void __noreturn rtas_restart(char *cmd)
{
	if (rtas_flash_term_hook)
		rtas_flash_term_hook(SYS_RESTART);
	printk("RTAS system-reboot returned %d\n",
	       rtas_call(rtas_token("system-reboot"), 0, 1, NULL));
	for (;;);
}

void rtas_power_off(void)
{
	if (rtas_flash_term_hook)
		rtas_flash_term_hook(SYS_POWER_OFF);
	/* allow power on only with power button press */
	printk("RTAS power-off returned %d\n",
	       rtas_call(rtas_token("power-off"), 2, 1, NULL, -1, -1));
	for (;;);
}

void __noreturn rtas_halt(void)
{
	if (rtas_flash_term_hook)
		rtas_flash_term_hook(SYS_HALT);
	/* allow power on only with power button press */
	printk("RTAS power-off returned %d\n",
	       rtas_call(rtas_token("power-off"), 2, 1, NULL, -1, -1));
	for (;;);
}

/* Must be in the RMO region, so we place it here */
static char rtas_os_term_buf[2048];

void rtas_os_term(char *str)
{
	int status;

	/*
	 * Firmware with the ibm,extended-os-term property is guaranteed
	 * to always return from an ibm,os-term call. Earlier versions without
	 * this property may terminate the partition which we want to avoid
	 * since it interferes with panic_timeout.
	 */
	if (RTAS_UNKNOWN_SERVICE == rtas_token("ibm,os-term") ||
	    RTAS_UNKNOWN_SERVICE == rtas_token("ibm,extended-os-term"))
		return;

	snprintf(rtas_os_term_buf, 2048, "OS panic: %s", str);

	do {
		status = rtas_call(rtas_token("ibm,os-term"), 1, 1, NULL,
				   __pa(rtas_os_term_buf));
	} while (rtas_busy_delay(status));

	if (status != 0)
		printk(KERN_EMERG "ibm,os-term call failed %d\n", status);
}

/**
 * rtas_activate_firmware() - Activate a new version of firmware.
 *
 * Context: This function may sleep.
 *
 * Activate a new version of partition firmware. The OS must call this
 * after resuming from a partition hibernation or migration in order
 * to maintain the ability to perform live firmware updates. It's not
 * catastrophic for this method to be absent or to fail; just log the
 * condition in that case.
 */
void rtas_activate_firmware(void)
{
	int token;
	int fwrc;

	token = rtas_token("ibm,activate-firmware");
	if (token == RTAS_UNKNOWN_SERVICE) {
		pr_notice("ibm,activate-firmware method unavailable\n");
		return;
	}

	do {
		fwrc = rtas_call(token, 0, 1, NULL);
	} while (rtas_busy_delay(fwrc));

	if (fwrc)
		pr_err("ibm,activate-firmware failed (%i)\n", fwrc);
}

/**
 * get_pseries_errorlog() - Find a specific pseries error log in an RTAS
 *                          extended event log.
 * @log: RTAS error/event log
 * @section_id: two character section identifier
 *
 * Return: A pointer to the specified errorlog or NULL if not found.
 */
noinstr struct pseries_errorlog *get_pseries_errorlog(struct rtas_error_log *log,
						      uint16_t section_id)
{
	struct rtas_ext_event_log_v6 *ext_log =
		(struct rtas_ext_event_log_v6 *)log->buffer;
	struct pseries_errorlog *sect;
	unsigned char *p, *log_end;
	uint32_t ext_log_length = rtas_error_extended_log_length(log);
	uint8_t log_format = rtas_ext_event_log_format(ext_log);
	uint32_t company_id = rtas_ext_event_company_id(ext_log);

	/* Check that we understand the format */
	if (ext_log_length < sizeof(struct rtas_ext_event_log_v6) ||
	    log_format != RTAS_V6EXT_LOG_FORMAT_EVENT_LOG ||
	    company_id != RTAS_V6EXT_COMPANY_ID_IBM)
		return NULL;

	log_end = log->buffer + ext_log_length;
	p = ext_log->vendor_log;

	while (p < log_end) {
		sect = (struct pseries_errorlog *)p;
		if (pseries_errorlog_id(sect) == section_id)
			return sect;
		p += pseries_errorlog_length(sect);
	}

	return NULL;
}

#ifdef CONFIG_PPC_RTAS_FILTER

/*
 * The sys_rtas syscall, as originally designed, allows root to pass
 * arbitrary physical addresses to RTAS calls. A number of RTAS calls
 * can be abused to write to arbitrary memory and do other things that
 * are potentially harmful to system integrity, and thus should only
 * be used inside the kernel and not exposed to userspace.
 *
 * All known legitimate users of the sys_rtas syscall will only ever
 * pass addresses that fall within the RMO buffer, and use a known
 * subset of RTAS calls.
 *
 * Accordingly, we filter RTAS requests to check that the call is
 * permitted, and that provided pointers fall within the RMO buffer.
 * The rtas_filters list contains an entry for each permitted call,
 * with the indexes of the parameters which are expected to contain
 * addresses and sizes of buffers allocated inside the RMO buffer.
 */
struct rtas_filter {
	const char *name;
	int token;
	/* Indexes into the args buffer, -1 if not used */
	int buf_idx1;
	int size_idx1;
	int buf_idx2;
	int size_idx2;

	int fixed_size;
};

static struct rtas_filter rtas_filters[] __ro_after_init = {
	{ "ibm,activate-firmware", -1, -1, -1, -1, -1 },
	{ "ibm,configure-connector", -1, 0, -1, 1, -1, 4096 },	/* Special cased */
	{ "display-character", -1, -1, -1, -1, -1 },
	{ "ibm,display-message", -1, 0, -1, -1, -1 },
	{ "ibm,errinjct", -1, 2, -1, -1, -1, 1024 },
	{ "ibm,close-errinjct", -1, -1, -1, -1, -1 },
	{ "ibm,open-errinjct", -1, -1, -1, -1, -1 },
	{ "ibm,get-config-addr-info2", -1, -1, -1, -1, -1 },
	{ "ibm,get-dynamic-sensor-state", -1, 1, -1, -1, -1 },
	{ "ibm,get-indices", -1, 2, 3, -1, -1 },
	{ "get-power-level", -1, -1, -1, -1, -1 },
	{ "get-sensor-state", -1, -1, -1, -1, -1 },
	{ "ibm,get-system-parameter", -1, 1, 2, -1, -1 },
	{ "get-time-of-day", -1, -1, -1, -1, -1 },
	{ "ibm,get-vpd", -1, 0, -1, 1, 2 },
	{ "ibm,lpar-perftools", -1, 2, 3, -1, -1 },
	{ "ibm,platform-dump", -1, 4, 5, -1, -1 },		/* Special cased */
	{ "ibm,read-slot-reset-state", -1, -1, -1, -1, -1 },
	{ "ibm,scan-log-dump", -1, 0, 1, -1, -1 },
	{ "ibm,set-dynamic-indicator", -1, 2, -1, -1, -1 },
	{ "ibm,set-eeh-option", -1, -1, -1, -1, -1 },
	{ "set-indicator", -1, -1, -1, -1, -1 },
	{ "set-power-level", -1, -1, -1, -1, -1 },
	{ "set-time-for-power-on", -1, -1, -1, -1, -1 },
	{ "ibm,set-system-parameter", -1, 1, -1, -1, -1 },
	{ "set-time-of-day", -1, -1, -1, -1, -1 },
#ifdef CONFIG_CPU_BIG_ENDIAN
	{ "ibm,suspend-me", -1, -1, -1, -1, -1 },
	{ "ibm,update-nodes", -1, 0, -1, -1, -1, 4096 },
	{ "ibm,update-properties", -1, 0, -1, -1, -1, 4096 },
#endif
	{ "ibm,physical-attestation", -1, 0, 1, -1, -1 },
};

static bool in_rmo_buf(u32 base, u32 end)
{
	return base >= rtas_rmo_buf &&
		base < (rtas_rmo_buf + RTAS_USER_REGION_SIZE) &&
		base <= end &&
		end >= rtas_rmo_buf &&
		end < (rtas_rmo_buf + RTAS_USER_REGION_SIZE);
}

static bool block_rtas_call(int token, int nargs,
			    struct rtas_args *args)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rtas_filters); i++) {
		struct rtas_filter *f = &rtas_filters[i];
		u32 base, size, end;

		if (token != f->token)
			continue;

		if (f->buf_idx1 != -1) {
			base = be32_to_cpu(args->args[f->buf_idx1]);
			if (f->size_idx1 != -1)
				size = be32_to_cpu(args->args[f->size_idx1]);
			else if (f->fixed_size)
				size = f->fixed_size;
			else
				size = 1;

			end = base + size - 1;

			/*
			 * Special case for ibm,platform-dump - NULL buffer
			 * address is used to indicate end of dump processing
			 */
			if (!strcmp(f->name, "ibm,platform-dump") &&
			    base == 0)
				return false;

			if (!in_rmo_buf(base, end))
				goto err;
		}

		if (f->buf_idx2 != -1) {
			base = be32_to_cpu(args->args[f->buf_idx2]);
			if (f->size_idx2 != -1)
				size = be32_to_cpu(args->args[f->size_idx2]);
			else if (f->fixed_size)
				size = f->fixed_size;
			else
				size = 1;
			end = base + size - 1;

			/*
			 * Special case for ibm,configure-connector where the
			 * address can be 0
			 */
			if (!strcmp(f->name, "ibm,configure-connector") &&
			    base == 0)
				return false;

			if (!in_rmo_buf(base, end))
				goto err;
		}

		return false;
	}

err:
	pr_err_ratelimited("sys_rtas: RTAS call blocked - exploit attempt?\n");
	pr_err_ratelimited("sys_rtas: token=0x%x, nargs=%d (called by %s)\n",
			   token, nargs, current->comm);
	return true;
}

static void __init rtas_syscall_filter_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rtas_filters); i++)
		rtas_filters[i].token = rtas_token(rtas_filters[i].name);
}

#else

static bool block_rtas_call(int token, int nargs,
			    struct rtas_args *args)
{
	return false;
}

static void __init rtas_syscall_filter_init(void)
{
}

#endif /* CONFIG_PPC_RTAS_FILTER */

/* We assume to be passed big endian arguments */
SYSCALL_DEFINE1(rtas, struct rtas_args __user *, uargs)
{
	struct rtas_args args;
	unsigned long flags;
	char *buff_copy, *errbuf = NULL;
	int nargs, nret, token;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!rtas.entry)
		return -EINVAL;

	if (copy_from_user(&args, uargs, 3 * sizeof(u32)) != 0)
		return -EFAULT;

	nargs = be32_to_cpu(args.nargs);
	nret  = be32_to_cpu(args.nret);
	token = be32_to_cpu(args.token);

	if (nargs >= ARRAY_SIZE(args.args)
	    || nret > ARRAY_SIZE(args.args)
	    || nargs + nret > ARRAY_SIZE(args.args))
		return -EINVAL;

	/* Copy in args. */
	if (copy_from_user(args.args, uargs->args,
			   nargs * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	args.rets = &args.args[nargs];
	memset(args.rets, 0, nret * sizeof(rtas_arg_t));

	if (block_rtas_call(token, nargs, &args))
		return -EINVAL;

	if (token == ibm_open_errinjct_token || token == ibm_errinjct_token) {
		int err;

		err = security_locked_down(LOCKDOWN_RTAS_ERROR_INJECTION);
		if (err)
			return err;
	}

	/* Need to handle ibm,suspend_me call specially */
	if (token == rtas_token("ibm,suspend-me")) {

		/*
		 * rtas_ibm_suspend_me assumes the streamid handle is in cpu
		 * endian, or at least the hcall within it requires it.
		 */
		int rc = 0;
		u64 handle = ((u64)be32_to_cpu(args.args[0]) << 32)
		              | be32_to_cpu(args.args[1]);
		rc = rtas_syscall_dispatch_ibm_suspend_me(handle);
		if (rc == -EAGAIN)
			args.rets[0] = cpu_to_be32(RTAS_NOT_SUSPENDABLE);
		else if (rc == -EIO)
			args.rets[0] = cpu_to_be32(-1);
		else if (rc)
			return rc;
		goto copy_return;
	}

	buff_copy = get_errorlog_buffer();

	flags = lock_rtas();

	rtas.args = args;
	do_enter_rtas(__pa(&rtas.args));
	args = rtas.args;

	/* A -1 return code indicates that the last command couldn't
	   be completed due to a hardware error. */
	if (be32_to_cpu(args.rets[0]) == -1)
		errbuf = __fetch_rtas_last_error(buff_copy);

	unlock_rtas(flags);

	if (buff_copy) {
		if (errbuf)
			log_error(errbuf, ERR_TYPE_RTAS_LOG, 0);
		kfree(buff_copy);
	}

 copy_return:
	/* Copy out args. */
	if (copy_to_user(uargs->args + nargs,
			 args.args + nargs,
			 nret * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * Call early during boot, before mem init, to retrieve the RTAS
 * information from the device-tree and allocate the RMO buffer for userland
 * accesses.
 */
void __init rtas_initialize(void)
{
	unsigned long rtas_region = RTAS_INSTANTIATE_MAX;
	u32 base, size, entry;
	int no_base, no_size, no_entry;

	/* Get RTAS dev node and fill up our "rtas" structure with infos
	 * about it.
	 */
	rtas.dev = of_find_node_by_name(NULL, "rtas");
	if (!rtas.dev)
		return;

	no_base = of_property_read_u32(rtas.dev, "linux,rtas-base", &base);
	no_size = of_property_read_u32(rtas.dev, "rtas-size", &size);
	if (no_base || no_size) {
		of_node_put(rtas.dev);
		rtas.dev = NULL;
		return;
	}

	rtas.base = base;
	rtas.size = size;
	no_entry = of_property_read_u32(rtas.dev, "linux,rtas-entry", &entry);
	rtas.entry = no_entry ? rtas.base : entry;

	/* If RTAS was found, allocate the RMO buffer for it and look for
	 * the stop-self token if any
	 */
#ifdef CONFIG_PPC64
	if (firmware_has_feature(FW_FEATURE_LPAR))
		rtas_region = min(ppc64_rma_size, RTAS_INSTANTIATE_MAX);
#endif
	rtas_rmo_buf = memblock_phys_alloc_range(RTAS_USER_REGION_SIZE, PAGE_SIZE,
						 0, rtas_region);
	if (!rtas_rmo_buf)
		panic("ERROR: RTAS: Failed to allocate %lx bytes below %pa\n",
		      PAGE_SIZE, &rtas_region);

#ifdef CONFIG_RTAS_ERROR_LOGGING
	rtas_last_error_token = rtas_token("rtas-last-error");
#endif
	ibm_open_errinjct_token = rtas_token("ibm,open-errinjct");
	ibm_errinjct_token = rtas_token("ibm,errinjct");
	rtas_syscall_filter_init();
}

int __init early_init_dt_scan_rtas(unsigned long node,
		const char *uname, int depth, void *data)
{
	const u32 *basep, *entryp, *sizep;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	basep  = of_get_flat_dt_prop(node, "linux,rtas-base", NULL);
	entryp = of_get_flat_dt_prop(node, "linux,rtas-entry", NULL);
	sizep  = of_get_flat_dt_prop(node, "rtas-size", NULL);

#ifdef CONFIG_PPC64
	/* need this feature to decide the crashkernel offset */
	if (of_get_flat_dt_prop(node, "ibm,hypertas-functions", NULL))
		powerpc_firmware_features |= FW_FEATURE_LPAR;
#endif

	if (basep && entryp && sizep) {
		rtas.base = *basep;
		rtas.entry = *entryp;
		rtas.size = *sizep;
	}

#ifdef CONFIG_UDBG_RTAS_CONSOLE
	basep = of_get_flat_dt_prop(node, "put-term-char", NULL);
	if (basep)
		rtas_putchar_token = *basep;

	basep = of_get_flat_dt_prop(node, "get-term-char", NULL);
	if (basep)
		rtas_getchar_token = *basep;

	if (rtas_putchar_token != RTAS_UNKNOWN_SERVICE &&
	    rtas_getchar_token != RTAS_UNKNOWN_SERVICE)
		udbg_init_rtas_console();

#endif

	/* break now */
	return 1;
}

static arch_spinlock_t timebase_lock;
static u64 timebase = 0;

void rtas_give_timebase(void)
{
	unsigned long flags;

	local_irq_save(flags);
	hard_irq_disable();
	arch_spin_lock(&timebase_lock);
	rtas_call(rtas_token("freeze-time-base"), 0, 1, NULL);
	timebase = get_tb();
	arch_spin_unlock(&timebase_lock);

	while (timebase)
		barrier();
	rtas_call(rtas_token("thaw-time-base"), 0, 1, NULL);
	local_irq_restore(flags);
}

void rtas_take_timebase(void)
{
	while (!timebase)
		barrier();
	arch_spin_lock(&timebase_lock);
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	arch_spin_unlock(&timebase_lock);
}

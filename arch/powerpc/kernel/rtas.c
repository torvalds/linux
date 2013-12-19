/*
 *
 * Procedures for interfacing to the RTAS on CHRP machines.
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/reboot.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/udbg.h>
#include <asm/syscalls.h>
#include <asm/smp.h>
#include <linux/atomic.h>
#include <asm/time.h>
#include <asm/mmu.h>
#include <asm/topology.h>

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
	arch_spin_lock_flags(&rtas.lock, flags);
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
	struct rtas_args *args = &rtas.args;
	unsigned long s;

	if (!rtas.base)
		return;
	s = lock_rtas();

	args->token = cpu_to_be32(10);
	args->nargs = cpu_to_be32(1);
	args->nret  = cpu_to_be32(1);
	args->rets  = &(args->args[1]);
	args->args[0] = cpu_to_be32(c);

	enter_rtas(__pa(args));

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

	enter_rtas(__pa(&rtas.args));

	err_args = rtas.args;
	rtas.args = save_args;

	/* Log the error in the unlikely case that there was one. */
	if (unlikely(err_args.args[2] == 0)) {
		if (altbuf) {
			buf = altbuf;
		} else {
			buf = rtas_err_buf;
			if (mem_init_done)
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

	s = lock_rtas();
	rtas_args = &rtas.args;

	rtas_args->token = cpu_to_be32(token);
	rtas_args->nargs = cpu_to_be32(nargs);
	rtas_args->nret  = cpu_to_be32(nret);
	rtas_args->rets  = &(rtas_args->args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		rtas_args->args[i] = cpu_to_be32(va_arg(list, __u32));
	va_end(list);

	for (i = 0; i < nret; ++i)
		rtas_args->rets[i] = 0;

	enter_rtas(__pa(rtas_args));

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
		if (mem_init_done)
			kfree(buff_copy);
	}
	return ret;
}
EXPORT_SYMBOL(rtas_call);

/* For RTAS_BUSY (-2), delay for 1 millisecond.  For an extended busy status
 * code of 990n, perform the hinted delay of 10^n (last digit) milliseconds.
 */
unsigned int rtas_busy_delay_time(int status)
{
	int order;
	unsigned int ms = 0;

	if (status == RTAS_BUSY) {
		ms = 1;
	} else if (status >= 9900 && status <= 9905) {
		order = status - 9900;
		for (ms = 1; order > 0; order--)
			ms *= 10;
	}

	return ms;
}
EXPORT_SYMBOL(rtas_busy_delay_time);

/* For an RTAS busy status code, perform the hinted delay. */
unsigned int rtas_busy_delay(int status)
{
	unsigned int ms;

	might_sleep();
	ms = rtas_busy_delay_time(status);
	if (ms && need_resched())
		msleep(ms);

	return ms;
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

	WARN_ON(rc == -2 || (rc >= 9900 && rc <= 9905));

	if (rc < 0)
		return rtas_error_rc(rc);

	return rc;
}

void rtas_restart(char *cmd)
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

void rtas_halt(void)
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

static int ibm_suspend_me_token = RTAS_UNKNOWN_SERVICE;
#ifdef CONFIG_PPC_PSERIES
static int __rtas_suspend_last_cpu(struct rtas_suspend_me_data *data, int wake_when_done)
{
	u16 slb_size = mmu_slb_size;
	int rc = H_MULTI_THREADS_ACTIVE;
	int cpu;

	slb_set_size(SLB_MIN_SIZE);
	printk(KERN_DEBUG "calling ibm,suspend-me on cpu %i\n", smp_processor_id());

	while (rc == H_MULTI_THREADS_ACTIVE && !atomic_read(&data->done) &&
	       !atomic_read(&data->error))
		rc = rtas_call(data->token, 0, 1, NULL);

	if (rc || atomic_read(&data->error)) {
		printk(KERN_DEBUG "ibm,suspend-me returned %d\n", rc);
		slb_set_size(slb_size);
	}

	if (atomic_read(&data->error))
		rc = atomic_read(&data->error);

	atomic_set(&data->error, rc);
	pSeries_coalesce_init();

	if (wake_when_done) {
		atomic_set(&data->done, 1);

		for_each_online_cpu(cpu)
			plpar_hcall_norets(H_PROD, get_hard_smp_processor_id(cpu));
	}

	if (atomic_dec_return(&data->working) == 0)
		complete(data->complete);

	return rc;
}

int rtas_suspend_last_cpu(struct rtas_suspend_me_data *data)
{
	atomic_inc(&data->working);
	return __rtas_suspend_last_cpu(data, 0);
}

static int __rtas_suspend_cpu(struct rtas_suspend_me_data *data, int wake_when_done)
{
	long rc = H_SUCCESS;
	unsigned long msr_save;
	int cpu;

	atomic_inc(&data->working);

	/* really need to ensure MSR.EE is off for H_JOIN */
	msr_save = mfmsr();
	mtmsr(msr_save & ~(MSR_EE));

	while (rc == H_SUCCESS && !atomic_read(&data->done) && !atomic_read(&data->error))
		rc = plpar_hcall_norets(H_JOIN);

	mtmsr(msr_save);

	if (rc == H_SUCCESS) {
		/* This cpu was prodded and the suspend is complete. */
		goto out;
	} else if (rc == H_CONTINUE) {
		/* All other cpus are in H_JOIN, this cpu does
		 * the suspend.
		 */
		return __rtas_suspend_last_cpu(data, wake_when_done);
	} else {
		printk(KERN_ERR "H_JOIN on cpu %i failed with rc = %ld\n",
		       smp_processor_id(), rc);
		atomic_set(&data->error, rc);
	}

	if (wake_when_done) {
		atomic_set(&data->done, 1);

		/* This cpu did the suspend or got an error; in either case,
		 * we need to prod all other other cpus out of join state.
		 * Extra prods are harmless.
		 */
		for_each_online_cpu(cpu)
			plpar_hcall_norets(H_PROD, get_hard_smp_processor_id(cpu));
	}
out:
	if (atomic_dec_return(&data->working) == 0)
		complete(data->complete);
	return rc;
}

int rtas_suspend_cpu(struct rtas_suspend_me_data *data)
{
	return __rtas_suspend_cpu(data, 0);
}

static void rtas_percpu_suspend_me(void *info)
{
	__rtas_suspend_cpu((struct rtas_suspend_me_data *)info, 1);
}

enum rtas_cpu_state {
	DOWN,
	UP,
};

#ifndef CONFIG_SMP
static int rtas_cpu_state_change_mask(enum rtas_cpu_state state,
				cpumask_var_t cpus)
{
	if (!cpumask_empty(cpus)) {
		cpumask_clear(cpus);
		return -EINVAL;
	} else
		return 0;
}
#else
/* On return cpumask will be altered to indicate CPUs changed.
 * CPUs with states changed will be set in the mask,
 * CPUs with status unchanged will be unset in the mask. */
static int rtas_cpu_state_change_mask(enum rtas_cpu_state state,
				cpumask_var_t cpus)
{
	int cpu;
	int cpuret = 0;
	int ret = 0;

	if (cpumask_empty(cpus))
		return 0;

	for_each_cpu(cpu, cpus) {
		switch (state) {
		case DOWN:
			cpuret = cpu_down(cpu);
			break;
		case UP:
			cpuret = cpu_up(cpu);
			break;
		}
		if (cpuret) {
			pr_debug("%s: cpu_%s for cpu#%d returned %d.\n",
					__func__,
					((state == UP) ? "up" : "down"),
					cpu, cpuret);
			if (!ret)
				ret = cpuret;
			if (state == UP) {
				/* clear bits for unchanged cpus, return */
				cpumask_shift_right(cpus, cpus, cpu);
				cpumask_shift_left(cpus, cpus, cpu);
				break;
			} else {
				/* clear bit for unchanged cpu, continue */
				cpumask_clear_cpu(cpu, cpus);
			}
		}
	}

	return ret;
}
#endif

int rtas_online_cpus_mask(cpumask_var_t cpus)
{
	int ret;

	ret = rtas_cpu_state_change_mask(UP, cpus);

	if (ret) {
		cpumask_var_t tmp_mask;

		if (!alloc_cpumask_var(&tmp_mask, GFP_TEMPORARY))
			return ret;

		/* Use tmp_mask to preserve cpus mask from first failure */
		cpumask_copy(tmp_mask, cpus);
		rtas_offline_cpus_mask(tmp_mask);
		free_cpumask_var(tmp_mask);
	}

	return ret;
}
EXPORT_SYMBOL(rtas_online_cpus_mask);

int rtas_offline_cpus_mask(cpumask_var_t cpus)
{
	return rtas_cpu_state_change_mask(DOWN, cpus);
}
EXPORT_SYMBOL(rtas_offline_cpus_mask);

int rtas_ibm_suspend_me(struct rtas_args *args)
{
	long state;
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	struct rtas_suspend_me_data data;
	DECLARE_COMPLETION_ONSTACK(done);
	cpumask_var_t offline_mask;
	int cpuret;

	if (!rtas_service_present("ibm,suspend-me"))
		return -ENOSYS;

	/* Make sure the state is valid */
	rc = plpar_hcall(H_VASI_STATE, retbuf,
			 ((u64)args->args[0] << 32) | args->args[1]);

	state = retbuf[0];

	if (rc) {
		printk(KERN_ERR "rtas_ibm_suspend_me: vasi_state returned %ld\n",rc);
		return rc;
	} else if (state == H_VASI_ENABLED) {
		args->args[args->nargs] = RTAS_NOT_SUSPENDABLE;
		return 0;
	} else if (state != H_VASI_SUSPENDING) {
		printk(KERN_ERR "rtas_ibm_suspend_me: vasi_state returned state %ld\n",
		       state);
		args->args[args->nargs] = -1;
		return 0;
	}

	if (!alloc_cpumask_var(&offline_mask, GFP_TEMPORARY))
		return -ENOMEM;

	atomic_set(&data.working, 0);
	atomic_set(&data.done, 0);
	atomic_set(&data.error, 0);
	data.token = rtas_token("ibm,suspend-me");
	data.complete = &done;

	/* All present CPUs must be online */
	cpumask_andnot(offline_mask, cpu_present_mask, cpu_online_mask);
	cpuret = rtas_online_cpus_mask(offline_mask);
	if (cpuret) {
		pr_err("%s: Could not bring present CPUs online.\n", __func__);
		atomic_set(&data.error, cpuret);
		goto out;
	}

	stop_topology_update();

	/* Call function on all CPUs.  One of us will make the
	 * rtas call
	 */
	if (on_each_cpu(rtas_percpu_suspend_me, &data, 0))
		atomic_set(&data.error, -EINVAL);

	wait_for_completion(&done);

	if (atomic_read(&data.error) != 0)
		printk(KERN_ERR "Error doing global join\n");

	start_topology_update();

	/* Take down CPUs not online prior to suspend */
	cpuret = rtas_offline_cpus_mask(offline_mask);
	if (cpuret)
		pr_warn("%s: Could not restore CPUs to offline state.\n",
				__func__);

out:
	free_cpumask_var(offline_mask);
	return atomic_read(&data.error);
}
#else /* CONFIG_PPC_PSERIES */
int rtas_ibm_suspend_me(struct rtas_args *args)
{
	return -ENOSYS;
}
#endif

/**
 * Find a specific pseries error log in an RTAS extended event log.
 * @log: RTAS error/event log
 * @section_id: two character section identifier
 *
 * Returns a pointer to the specified errorlog or NULL if not found.
 */
struct pseries_errorlog *get_pseries_errorlog(struct rtas_error_log *log,
					      uint16_t section_id)
{
	struct rtas_ext_event_log_v6 *ext_log =
		(struct rtas_ext_event_log_v6 *)log->buffer;
	struct pseries_errorlog *sect;
	unsigned char *p, *log_end;

	/* Check that we understand the format */
	if (log->extended_log_length < sizeof(struct rtas_ext_event_log_v6) ||
	    ext_log->log_format != RTAS_V6EXT_LOG_FORMAT_EVENT_LOG ||
	    ext_log->company_id != RTAS_V6EXT_COMPANY_ID_IBM)
		return NULL;

	log_end = log->buffer + log->extended_log_length;
	p = ext_log->vendor_log;

	while (p < log_end) {
		sect = (struct pseries_errorlog *)p;
		if (sect->id == section_id)
			return sect;
		p += sect->length;
	}

	return NULL;
}

asmlinkage int ppc_rtas(struct rtas_args __user *uargs)
{
	struct rtas_args args;
	unsigned long flags;
	char *buff_copy, *errbuf = NULL;
	int nargs;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&args, uargs, 3 * sizeof(u32)) != 0)
		return -EFAULT;

	nargs = args.nargs;
	if (nargs > ARRAY_SIZE(args.args)
	    || args.nret > ARRAY_SIZE(args.args)
	    || nargs + args.nret > ARRAY_SIZE(args.args))
		return -EINVAL;

	/* Copy in args. */
	if (copy_from_user(args.args, uargs->args,
			   nargs * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	if (args.token == RTAS_UNKNOWN_SERVICE)
		return -EINVAL;

	args.rets = &args.args[nargs];
	memset(args.rets, 0, args.nret * sizeof(rtas_arg_t));

	/* Need to handle ibm,suspend_me call specially */
	if (args.token == ibm_suspend_me_token) {
		rc = rtas_ibm_suspend_me(&args);
		if (rc)
			return rc;
		goto copy_return;
	}

	buff_copy = get_errorlog_buffer();

	flags = lock_rtas();

	rtas.args = args;
	enter_rtas(__pa(&rtas.args));
	args = rtas.args;

	/* A -1 return code indicates that the last command couldn't
	   be completed due to a hardware error. */
	if (args.rets[0] == -1)
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
			 args.nret * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

/*
 * Call early during boot, before mem init or bootmem, to retrieve the RTAS
 * informations from the device-tree and allocate the RMO buffer for userland
 * accesses.
 */
void __init rtas_initialize(void)
{
	unsigned long rtas_region = RTAS_INSTANTIATE_MAX;

	/* Get RTAS dev node and fill up our "rtas" structure with infos
	 * about it.
	 */
	rtas.dev = of_find_node_by_name(NULL, "rtas");
	if (rtas.dev) {
		const __be32 *basep, *entryp, *sizep;

		basep = of_get_property(rtas.dev, "linux,rtas-base", NULL);
		sizep = of_get_property(rtas.dev, "rtas-size", NULL);
		if (basep != NULL && sizep != NULL) {
			rtas.base = __be32_to_cpu(*basep);
			rtas.size = __be32_to_cpu(*sizep);
			entryp = of_get_property(rtas.dev,
					"linux,rtas-entry", NULL);
			if (entryp == NULL) /* Ugh */
				rtas.entry = rtas.base;
			else
				rtas.entry = __be32_to_cpu(*entryp);
		} else
			rtas.dev = NULL;
	}
	if (!rtas.dev)
		return;

	/* If RTAS was found, allocate the RMO buffer for it and look for
	 * the stop-self token if any
	 */
#ifdef CONFIG_PPC64
	if (machine_is(pseries) && firmware_has_feature(FW_FEATURE_LPAR)) {
		rtas_region = min(ppc64_rma_size, RTAS_INSTANTIATE_MAX);
		ibm_suspend_me_token = rtas_token("ibm,suspend-me");
	}
#endif
	rtas_rmo_buf = memblock_alloc_base(RTAS_RMOBUF_MAX, PAGE_SIZE, rtas_region);

#ifdef CONFIG_RTAS_ERROR_LOGGING
	rtas_last_error_token = rtas_token("rtas-last-error");
#endif
}

int __init early_init_dt_scan_rtas(unsigned long node,
		const char *uname, int depth, void *data)
{
	u32 *basep, *entryp, *sizep;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	basep  = of_get_flat_dt_prop(node, "linux,rtas-base", NULL);
	entryp = of_get_flat_dt_prop(node, "linux,rtas-entry", NULL);
	sizep  = of_get_flat_dt_prop(node, "rtas-size", NULL);

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

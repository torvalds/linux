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
#include <linux/module.h>
#include <linux/init.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/semaphore.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/lmb.h>
#ifdef CONFIG_PPC64
#include <asm/systemcfg.h>
#endif

struct rtas_t rtas = {
	.lock = SPIN_LOCK_UNLOCKED
};

EXPORT_SYMBOL(rtas);

DEFINE_SPINLOCK(rtas_data_buf_lock);
char rtas_data_buf[RTAS_DATA_BUF_SIZE] __cacheline_aligned;
unsigned long rtas_rmo_buf;

/*
 * If non-NULL, this gets called when the kernel terminates.
 * This is done like this so rtas_flash can be a module.
 */
void (*rtas_flash_term_hook)(int);
EXPORT_SYMBOL(rtas_flash_term_hook);

/*
 * call_rtas_display_status and call_rtas_display_status_delay
 * are designed only for very early low-level debugging, which
 * is why the token is hard-coded to 10.
 */
void call_rtas_display_status(unsigned char c)
{
	struct rtas_args *args = &rtas.args;
	unsigned long s;

	if (!rtas.base)
		return;
	spin_lock_irqsave(&rtas.lock, s);

	args->token = 10;
	args->nargs = 1;
	args->nret  = 1;
	args->rets  = (rtas_arg_t *)&(args->args[1]);
	args->args[0] = (int)c;

	enter_rtas(__pa(args));

	spin_unlock_irqrestore(&rtas.lock, s);
}

void call_rtas_display_status_delay(unsigned char c)
{
	static int pending_newline = 0;  /* did last write end with unprinted newline? */
	static int width = 16;

	if (c == '\n') {	
		while (width-- > 0)
			call_rtas_display_status(' ');
		width = 16;
		udelay(500000);
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

void rtas_progress(char *s, unsigned short hex)
{
	struct device_node *root;
	int width, *p;
	char *os;
	static int display_character, set_indicator;
	static int display_width, display_lines, *row_width, form_feed;
	static DEFINE_SPINLOCK(progress_lock);
	static int current_line;
	static int pending_newline = 0;  /* did last write end with unprinted newline? */

	if (!rtas.base)
		return;

	if (display_width == 0) {
		display_width = 0x10;
		if ((root = find_path_device("/rtas"))) {
			if ((p = (unsigned int *)get_property(root,
					"ibm,display-line-length", NULL)))
				display_width = *p;
			if ((p = (unsigned int *)get_property(root,
					"ibm,form-feed", NULL)))
				form_feed = *p;
			if ((p = (unsigned int *)get_property(root,
					"ibm,display-number-of-lines", NULL)))
				display_lines = *p;
			row_width = (unsigned int *)get_property(root,
					"ibm,display-truncation-length", NULL);
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
	int *tokp;
	if (rtas.dev == NULL)
		return RTAS_UNKNOWN_SERVICE;
	tokp = (int *) get_property(rtas.dev, service, NULL);
	return tokp ? *tokp : RTAS_UNKNOWN_SERVICE;
}

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


char rtas_err_buf[RTAS_ERROR_LOG_MAX];
int rtas_last_error_token;

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

	err_args.token = rtas_last_error_token;
	err_args.nargs = 2;
	err_args.nret = 1;
	err_args.args[0] = (rtas_arg_t)__pa(rtas_err_buf);
	err_args.args[1] = bufsz;
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

	if (token == RTAS_UNKNOWN_SERVICE)
		return -1;

	/* Gotta do something different here, use global lock for now... */
	spin_lock_irqsave(&rtas.lock, s);
	rtas_args = &rtas.args;

	rtas_args->token = token;
	rtas_args->nargs = nargs;
	rtas_args->nret  = nret;
	rtas_args->rets  = (rtas_arg_t *)&(rtas_args->args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i)
		rtas_args->args[i] = va_arg(list, rtas_arg_t);
	va_end(list);

	for (i = 0; i < nret; ++i)
		rtas_args->rets[i] = 0;

	enter_rtas(__pa(rtas_args));

	/* A -1 return code indicates that the last command couldn't
	   be completed due to a hardware error. */
	if (rtas_args->rets[0] == -1)
		buff_copy = __fetch_rtas_last_error(NULL);

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = rtas_args->rets[i+1];
	ret = (nret > 0)? rtas_args->rets[0]: 0;

	/* Gotta do something different here, use global lock for now... */
	spin_unlock_irqrestore(&rtas.lock, s);

	if (buff_copy) {
		log_error(buff_copy, ERR_TYPE_RTAS_LOG, 0);
		if (mem_init_done)
			kfree(buff_copy);
	}
	return ret;
}

/* Given an RTAS status code of 990n compute the hinted delay of 10^n
 * (last digit) milliseconds.  For now we bound at n=5 (100 sec).
 */
unsigned int rtas_extended_busy_delay_time(int status)
{
	int order = status - 9900;
	unsigned long ms;

	if (order < 0)
		order = 0;	/* RTC depends on this for -2 clock busy */
	else if (order > 5)
		order = 5;	/* bound */

	/* Use microseconds for reasonable accuracy */
	for (ms = 1; order > 0; order--)
		ms *= 10;

	return ms; 
}

int rtas_error_rc(int rtas_rc)
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
					__FUNCTION__, rtas_rc);
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

int rtas_set_power_level(int powerdomain, int level, int *setlevel)
{
	int token = rtas_token("set-power-level");
	unsigned int wait_time;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	while (1) {
		rc = rtas_call(token, 2, 2, setlevel, powerdomain, level);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		} else
			break;
	}

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}

int rtas_get_sensor(int sensor, int index, int *state)
{
	int token = rtas_token("get-sensor-state");
	unsigned int wait_time;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	while (1) {
		rc = rtas_call(token, 2, 2, state, sensor, index);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		} else
			break;
	}

	if (rc < 0)
		return rtas_error_rc(rc);
	return rc;
}

int rtas_set_indicator(int indicator, int index, int new_value)
{
	int token = rtas_token("set-indicator");
	unsigned int wait_time;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return -ENOENT;

	while (1) {
		rc = rtas_call(token, 3, 1, NULL, indicator, index, new_value);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		}
		else
			break;
	}

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

	if (RTAS_UNKNOWN_SERVICE == rtas_token("ibm,os-term"))
		return;

	snprintf(rtas_os_term_buf, 2048, "OS panic: %s", str);

	do {
		status = rtas_call(rtas_token("ibm,os-term"), 1, 1, NULL,
				   __pa(rtas_os_term_buf));

		if (status == RTAS_BUSY)
			udelay(1);
		else if (status != 0)
			printk(KERN_EMERG "ibm,os-term call failed %d\n",
			       status);
	} while (status == RTAS_BUSY);
}


asmlinkage int ppc_rtas(struct rtas_args __user *uargs)
{
	struct rtas_args args;
	unsigned long flags;
	char *buff_copy, *errbuf = NULL;
	int nargs;

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

	buff_copy = get_errorlog_buffer();

	spin_lock_irqsave(&rtas.lock, flags);

	rtas.args = args;
	enter_rtas(__pa(&rtas.args));
	args = rtas.args;

	args.rets = &args.args[nargs];

	/* A -1 return code indicates that the last command couldn't
	   be completed due to a hardware error. */
	if (args.rets[0] == -1)
		errbuf = __fetch_rtas_last_error(buff_copy);

	spin_unlock_irqrestore(&rtas.lock, flags);

	if (buff_copy) {
		if (errbuf)
			log_error(errbuf, ERR_TYPE_RTAS_LOG, 0);
		kfree(buff_copy);
	}

	/* Copy out args. */
	if (copy_to_user(uargs->args + nargs,
			 args.args + nargs,
			 args.nret * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_SMP
/* This version can't take the spinlock, because it never returns */

struct rtas_args rtas_stop_self_args = {
	/* The token is initialized for real in setup_system() */
	.token = RTAS_UNKNOWN_SERVICE,
	.nargs = 0,
	.nret = 1,
	.rets = &rtas_stop_self_args.args[0],
};

void rtas_stop_self(void)
{
	struct rtas_args *rtas_args = &rtas_stop_self_args;

	local_irq_disable();

	BUG_ON(rtas_args->token == RTAS_UNKNOWN_SERVICE);

	printk("cpu %u (hwid %u) Ready to die...\n",
	       smp_processor_id(), hard_smp_processor_id());
	enter_rtas(__pa(rtas_args));

	panic("Alas, I survived.\n");
}
#endif

/*
 * Call early during boot, before mem init or bootmem, to retreive the RTAS
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
		u32 *basep, *entryp;
		u32 *sizep;

		basep = (u32 *)get_property(rtas.dev, "linux,rtas-base", NULL);
		sizep = (u32 *)get_property(rtas.dev, "rtas-size", NULL);
		if (basep != NULL && sizep != NULL) {
			rtas.base = *basep;
			rtas.size = *sizep;
			entryp = (u32 *)get_property(rtas.dev, "linux,rtas-entry", NULL);
			if (entryp == NULL) /* Ugh */
				rtas.entry = rtas.base;
			else
				rtas.entry = *entryp;
		} else
			rtas.dev = NULL;
	}
	if (!rtas.dev)
		return;

	/* If RTAS was found, allocate the RMO buffer for it and look for
	 * the stop-self token if any
	 */
#ifdef CONFIG_PPC64
	if (systemcfg->platform == PLATFORM_PSERIES_LPAR)
		rtas_region = min(lmb.rmo_size, RTAS_INSTANTIATE_MAX);
#endif
	rtas_rmo_buf = lmb_alloc_base(RTAS_RMOBUF_MAX, PAGE_SIZE, rtas_region);

#ifdef CONFIG_HOTPLUG_CPU
	rtas_stop_self_args.token = rtas_token("stop-self");
#endif /* CONFIG_HOTPLUG_CPU */
#ifdef CONFIG_RTAS_ERROR_LOGGING
	rtas_last_error_token = rtas_token("rtas-last-error");
#endif
}


EXPORT_SYMBOL(rtas_token);
EXPORT_SYMBOL(rtas_call);
EXPORT_SYMBOL(rtas_data_buf);
EXPORT_SYMBOL(rtas_data_buf_lock);
EXPORT_SYMBOL(rtas_extended_busy_delay_time);
EXPORT_SYMBOL(rtas_get_sensor);
EXPORT_SYMBOL(rtas_get_power_level);
EXPORT_SYMBOL(rtas_set_power_level);
EXPORT_SYMBOL(rtas_set_indicator);

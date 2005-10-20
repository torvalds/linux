/* -*- mode: c; c-basic-offset: 8 -*- */

/* Copyright (C) 2001
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/kernel/voyager_thread.c
 *
 * This module provides the machine status monitor thread for the
 * voyager architecture.  This allows us to monitor the machine
 * environment (temp, voltage, fan function) and the front panel and
 * internal UPS.  If a fault is detected, this thread takes corrective
 * action (usually just informing init)
 * */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/kmod.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <asm/desc.h>
#include <asm/voyager.h>
#include <asm/vic.h>
#include <asm/mtrr.h>
#include <asm/msr.h>

#define THREAD_NAME "kvoyagerd"

/* external variables */
int kvoyagerd_running = 0;
DECLARE_MUTEX_LOCKED(kvoyagerd_sem);

static int thread(void *);

static __u8 set_timeout = 0;

/* Start the machine monitor thread.  Return 1 if OK, 0 if fail */
static int __init
voyager_thread_start(void)
{
	if(kernel_thread(thread, NULL, CLONE_KERNEL) < 0) {
		/* This is serious, but not fatal */
		printk(KERN_ERR "Voyager: Failed to create system monitor thread!!!\n");
		return 1;
	}
	return 0;
}

static int
execute(const char *string)
{
	int ret;

	char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
		NULL,
	};
	char *argv[] = {
		"/bin/bash",
		"-c",
		(char *)string,
		NULL,
	};

	if ((ret = call_usermodehelper(argv[0], argv, envp, 1)) != 0) {
		printk(KERN_ERR "Voyager failed to run \"%s\": %i\n",
		       string, ret);
	}
	return ret;
}

static void
check_from_kernel(void)
{
	if(voyager_status.switch_off) {
		
		/* FIXME: This should be configureable via proc */
		execute("umask 600; echo 0 > /etc/initrunlvl; kill -HUP 1");
	} else if(voyager_status.power_fail) {
		VDEBUG(("Voyager daemon detected AC power failure\n"));
		
		/* FIXME: This should be configureable via proc */
		execute("umask 600; echo F > /etc/powerstatus; kill -PWR 1");
		set_timeout = 1;
	}
}

static void
check_continuing_condition(void)
{
	if(voyager_status.power_fail) {
		__u8 data;
		voyager_cat_psi(VOYAGER_PSI_SUBREAD, 
				VOYAGER_PSI_AC_FAIL_REG, &data);
		if((data & 0x1f) == 0) {
			/* all power restored */
			printk(KERN_NOTICE "VOYAGER AC power restored, cancelling shutdown\n");
			/* FIXME: should be user configureable */
			execute("umask 600; echo O > /etc/powerstatus; kill -PWR 1");
			set_timeout = 0;
		}
	}
}

static void
wakeup(unsigned long unused)
{
	up(&kvoyagerd_sem);
}

static int
thread(void *unused)
{
	struct timer_list wakeup_timer;

	kvoyagerd_running = 1;

	daemonize(THREAD_NAME);

	set_timeout = 0;

	init_timer(&wakeup_timer);

	sigfillset(&current->blocked);
	current->signal->tty = NULL;

	printk(KERN_NOTICE "Voyager starting monitor thread\n");

	for(;;) {
		down_interruptible(&kvoyagerd_sem);
		VDEBUG(("Voyager Daemon awoken\n"));
		if(voyager_status.request_from_kernel == 0) {
			/* probably awoken from timeout */
			check_continuing_condition();
		} else {
			check_from_kernel();
			voyager_status.request_from_kernel = 0;
		}
		if(set_timeout) {
			del_timer(&wakeup_timer);
			wakeup_timer.expires = HZ + jiffies;
			wakeup_timer.function = wakeup;
			add_timer(&wakeup_timer);
		}
	}
}

static void __exit
voyager_thread_stop(void)
{
	/* FIXME: do nothing at the moment */
}

module_init(voyager_thread_start);
//module_exit(voyager_thread_stop);

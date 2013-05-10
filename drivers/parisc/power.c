/*
 * linux/drivers/parisc/power.c
 * HP PARISC soft power switch support driver
 *
 * Copyright (c) 2001-2007 Helge Deller <deller@gmx.de>
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 *
 *  HINT:
 *  Support of the soft power switch button may be enabled or disabled at
 *  runtime through the "/proc/sys/kernel/power" procfs entry.
 */ 

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/pm.h>

#include <asm/pdc.h>
#include <asm/io.h>
#include <asm/led.h>

#define DRIVER_NAME  "powersw"
#define KTHREAD_NAME "kpowerswd"

/* how often should the power button be polled ? */
#define POWERSWITCH_POLL_PER_SEC 2

/* how long does the power button needs to be down until we react ? */
#define POWERSWITCH_DOWN_SEC 2

/* assembly code to access special registers */
/* taken from PCXL ERS page 82 */
#define DIAG_CODE(code)		(0x14000000 + ((code)<<5))

#define MFCPU_X(rDiagReg, t_ch, t_th, code) \
	(DIAG_CODE(code) + ((rDiagReg)<<21) + ((t_ch)<<16) + ((t_th)<<0) )
	
#define MTCPU(dr, gr)		MFCPU_X(dr, gr,  0, 0x12)       /* move value of gr to dr[dr] */
#define MFCPU_C(dr, gr)		MFCPU_X(dr, gr,  0, 0x30)	/* for dr0 and dr8 only ! */
#define MFCPU_T(dr, gr)		MFCPU_X(dr,  0, gr, 0xa0)	/* all dr except dr0 and dr8 */
	
#define __getDIAG(dr) ( { 			\
        register unsigned long __res asm("r28");\
	 __asm__ __volatile__ (			\
		".word %1" : "=&r" (__res) : "i" (MFCPU_T(dr,28) ) \
	);					\
	__res;					\
} )

/* local shutdown counter */
static int shutdown_timer __read_mostly;

/* check, give feedback and start shutdown after one second */
static void process_shutdown(void)
{
	if (shutdown_timer == 0)
		printk(KERN_ALERT KTHREAD_NAME ": Shutdown requested...\n");

	shutdown_timer++;
	
	/* wait until the button was pressed for 1 second */
	if (shutdown_timer == (POWERSWITCH_DOWN_SEC*POWERSWITCH_POLL_PER_SEC)) {
		static const char msg[] = "Shutting down...";
		printk(KERN_INFO KTHREAD_NAME ": %s\n", msg);
		lcd_print(msg);

		/* send kill signal */
		if (kill_cad_pid(SIGINT, 1)) {
			/* just in case killing init process failed */
			if (pm_power_off)
				pm_power_off();
		}
	}
}


/* main power switch task struct */
static struct task_struct *power_task;

/* filename in /proc which can be used to enable/disable the power switch */
#define SYSCTL_FILENAME	"sys/kernel/power"

/* soft power switch enabled/disabled */
int pwrsw_enabled __read_mostly = 1;

/* main kernel thread worker. It polls the button state */
static int kpowerswd(void *param)
{
	__set_current_state(TASK_RUNNING);

	do {
		int button_not_pressed;
		unsigned long soft_power_reg = (unsigned long) param;

		schedule_timeout_interruptible(pwrsw_enabled ? HZ : HZ/POWERSWITCH_POLL_PER_SEC);
		__set_current_state(TASK_RUNNING);

		if (unlikely(!pwrsw_enabled))
			continue;

		if (soft_power_reg) {
			/*
			 * Non-Gecko-style machines:
			 * Check the power switch status which is read from the
			 * real I/O location at soft_power_reg.
			 * Bit 31 ("the lowest bit) is the status of the power switch.
			 * This bit is "1" if the button is NOT pressed.
			 */
			button_not_pressed = (gsc_readl(soft_power_reg) & 0x1);
		} else {
			/*
			 * On gecko style machines (e.g. 712/xx and 715/xx) 
			 * the power switch status is stored in Bit 0 ("the highest bit")
			 * of CPU diagnose register 25.
			 * Warning: Some machines never reset the DIAG flag, even if
			 * the button has been released again.
			 */
			button_not_pressed = (__getDIAG(25) & 0x80000000);
		}

		if (likely(button_not_pressed)) {
			if (unlikely(shutdown_timer && /* avoid writing if not necessary */
				shutdown_timer < (POWERSWITCH_DOWN_SEC*POWERSWITCH_POLL_PER_SEC))) {
				shutdown_timer = 0;
				printk(KERN_INFO KTHREAD_NAME ": Shutdown request aborted.\n");
			}
		} else
			process_shutdown();


	} while (!kthread_should_stop());

	return 0;
}


/*
 * powerfail interruption handler (irq IRQ_FROM_REGION(CPU_IRQ_REGION)+2) 
 */
#if 0
static void powerfail_interrupt(int code, void *x)
{
	printk(KERN_CRIT "POWERFAIL INTERRUPTION !\n");
	poweroff();
}
#endif




/* parisc_panic_event() is called by the panic handler.
 * As soon as a panic occurs, our tasklets above will not be
 * executed any longer. This function then re-enables the 
 * soft-power switch and allows the user to switch off the system
 */
static int parisc_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	/* re-enable the soft-power switch */
	pdc_soft_power_button(0);
	return NOTIFY_DONE;
}

static struct notifier_block parisc_panic_block = {
	.notifier_call	= parisc_panic_event,
	.priority	= INT_MAX,
};


static int __init power_init(void)
{
	unsigned long ret;
	unsigned long soft_power_reg;

#if 0
	request_irq( IRQ_FROM_REGION(CPU_IRQ_REGION)+2, &powerfail_interrupt,
		0, "powerfail", NULL);
#endif

	/* enable the soft power switch if possible */
	ret = pdc_soft_power_info(&soft_power_reg);
	if (ret == PDC_OK)
		ret = pdc_soft_power_button(1);
	if (ret != PDC_OK)
		soft_power_reg = -1UL;
	
	switch (soft_power_reg) {
	case 0:		printk(KERN_INFO DRIVER_NAME ": Gecko-style soft power switch enabled.\n");
			break;
			
	case -1UL:	printk(KERN_INFO DRIVER_NAME ": Soft power switch support not available.\n");
			return -ENODEV;
	
	default:	printk(KERN_INFO DRIVER_NAME ": Soft power switch at 0x%08lx enabled.\n",
				soft_power_reg);
	}

	power_task = kthread_run(kpowerswd, (void*)soft_power_reg, KTHREAD_NAME);
	if (IS_ERR(power_task)) {
		printk(KERN_ERR DRIVER_NAME ": thread creation failed.  Driver not loaded.\n");
		pdc_soft_power_button(0);
		return -EIO;
	}

	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list,
			&parisc_panic_block);

	return 0;
}

static void __exit power_exit(void)
{
	kthread_stop(power_task);

	atomic_notifier_chain_unregister(&panic_notifier_list,
			&parisc_panic_block);

	pdc_soft_power_button(0);
}

arch_initcall(power_init);
module_exit(power_exit);


MODULE_AUTHOR("Helge Deller <deller@gmx.de>");
MODULE_DESCRIPTION("Soft power switch driver");
MODULE_LICENSE("Dual BSD/GPL");

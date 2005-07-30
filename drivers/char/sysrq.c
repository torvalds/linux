/* -*- linux-c -*-
 *
 *	$Id: sysrq.c,v 1.15 1998/08/23 14:56:41 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	based on ideas by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>
 *
 *	(c) 2000 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 *	overhauled to use key registration
 *	based upon discusions in irc://irc.openprojects.net/#kernelnewbies
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/kbd_kern.h>
#include <linux/quotaops.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>		/* for fsync_bdev() */
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/vt_kern.h>
#include <linux/workqueue.h>
#include <linux/kexec.h>

#include <asm/ptrace.h>

/* Whether we react on sysrq keys or just ignore them */
int sysrq_enabled = 1;

/* Loglevel sysrq handler */
static void sysrq_handle_loglevel(int key, struct pt_regs *pt_regs,
				  struct tty_struct *tty) 
{
	int i;
	i = key - '0';
	console_loglevel = 7;
	printk("Loglevel set to %d\n", i);
	console_loglevel = i;
}	
static struct sysrq_key_op sysrq_loglevel_op = {
	.handler	= sysrq_handle_loglevel,
	.help_msg	= "loglevel0-8",
	.action_msg	= "Changing Loglevel",
	.enable_mask	= SYSRQ_ENABLE_LOG,
};


/* SAK sysrq handler */
#ifdef CONFIG_VT
static void sysrq_handle_SAK(int key, struct pt_regs *pt_regs,
			     struct tty_struct *tty) 
{
	if (tty)
		do_SAK(tty);
	reset_vc(vc_cons[fg_console].d);
}
static struct sysrq_key_op sysrq_SAK_op = {
	.handler	= sysrq_handle_SAK,
	.help_msg	= "saK",
	.action_msg	= "SAK",
	.enable_mask	= SYSRQ_ENABLE_KEYBOARD,
};
#endif

#ifdef CONFIG_VT
/* unraw sysrq handler */
static void sysrq_handle_unraw(int key, struct pt_regs *pt_regs,
			       struct tty_struct *tty) 
{
	struct kbd_struct *kbd = &kbd_table[fg_console];

	if (kbd)
		kbd->kbdmode = VC_XLATE;
}
static struct sysrq_key_op sysrq_unraw_op = {
	.handler	= sysrq_handle_unraw,
	.help_msg	= "unRaw",
	.action_msg	= "Keyboard mode set to XLATE",
	.enable_mask	= SYSRQ_ENABLE_KEYBOARD,
};
#endif /* CONFIG_VT */

#ifdef CONFIG_KEXEC
/* crashdump sysrq handler */
static void sysrq_handle_crashdump(int key, struct pt_regs *pt_regs,
				struct tty_struct *tty)
{
	crash_kexec(pt_regs);
}
static struct sysrq_key_op sysrq_crashdump_op = {
	.handler	= sysrq_handle_crashdump,
	.help_msg	= "Crashdump",
	.action_msg	= "Trigger a crashdump",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};
#endif

/* reboot sysrq handler */
static void sysrq_handle_reboot(int key, struct pt_regs *pt_regs,
				struct tty_struct *tty) 
{
	local_irq_enable();
	emergency_restart();
}

static struct sysrq_key_op sysrq_reboot_op = {
	.handler	= sysrq_handle_reboot,
	.help_msg	= "reBoot",
	.action_msg	= "Resetting",
	.enable_mask	= SYSRQ_ENABLE_BOOT,
};

static void sysrq_handle_sync(int key, struct pt_regs *pt_regs,
			      struct tty_struct *tty) 
{
	emergency_sync();
}

static struct sysrq_key_op sysrq_sync_op = {
	.handler	= sysrq_handle_sync,
	.help_msg	= "Sync",
	.action_msg	= "Emergency Sync",
	.enable_mask	= SYSRQ_ENABLE_SYNC,
};

static void sysrq_handle_mountro(int key, struct pt_regs *pt_regs,
				 struct tty_struct *tty) 
{
	emergency_remount();
}

static struct sysrq_key_op sysrq_mountro_op = {
	.handler	= sysrq_handle_mountro,
	.help_msg	= "Unmount",
	.action_msg	= "Emergency Remount R/O",
	.enable_mask	= SYSRQ_ENABLE_REMOUNT,
};

/* END SYNC SYSRQ HANDLERS BLOCK */


/* SHOW SYSRQ HANDLERS BLOCK */

static void sysrq_handle_showregs(int key, struct pt_regs *pt_regs,
				  struct tty_struct *tty) 
{
	if (pt_regs)
		show_regs(pt_regs);
}
static struct sysrq_key_op sysrq_showregs_op = {
	.handler	= sysrq_handle_showregs,
	.help_msg	= "showPc",
	.action_msg	= "Show Regs",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};


static void sysrq_handle_showstate(int key, struct pt_regs *pt_regs,
				   struct tty_struct *tty) 
{
	show_state();
}
static struct sysrq_key_op sysrq_showstate_op = {
	.handler	= sysrq_handle_showstate,
	.help_msg	= "showTasks",
	.action_msg	= "Show State",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};


static void sysrq_handle_showmem(int key, struct pt_regs *pt_regs,
				 struct tty_struct *tty) 
{
	show_mem();
}
static struct sysrq_key_op sysrq_showmem_op = {
	.handler	= sysrq_handle_showmem,
	.help_msg	= "showMem",
	.action_msg	= "Show Memory",
	.enable_mask	= SYSRQ_ENABLE_DUMP,
};

/* SHOW SYSRQ HANDLERS BLOCK */


/* SIGNAL SYSRQ HANDLERS BLOCK */

/* signal sysrq helper function
 * Sends a signal to all user processes */
static void send_sig_all(int sig)
{
	struct task_struct *p;

	for_each_process(p) {
		if (p->mm && p->pid != 1)
			/* Not swapper, init nor kernel thread */
			force_sig(sig, p);
	}
}

static void sysrq_handle_term(int key, struct pt_regs *pt_regs,
			      struct tty_struct *tty) 
{
	send_sig_all(SIGTERM);
	console_loglevel = 8;
}
static struct sysrq_key_op sysrq_term_op = {
	.handler	= sysrq_handle_term,
	.help_msg	= "tErm",
	.action_msg	= "Terminate All Tasks",
	.enable_mask	= SYSRQ_ENABLE_SIGNAL,
};

static void moom_callback(void *ignored)
{
	out_of_memory(GFP_KERNEL, 0);
}

static DECLARE_WORK(moom_work, moom_callback, NULL);

static void sysrq_handle_moom(int key, struct pt_regs *pt_regs,
			      struct tty_struct *tty)
{
	schedule_work(&moom_work);
}
static struct sysrq_key_op sysrq_moom_op = {
	.handler	= sysrq_handle_moom,
	.help_msg	= "Full",
	.action_msg	= "Manual OOM execution",
};

static void sysrq_handle_kill(int key, struct pt_regs *pt_regs,
			      struct tty_struct *tty) 
{
	send_sig_all(SIGKILL);
	console_loglevel = 8;
}
static struct sysrq_key_op sysrq_kill_op = {
	.handler	= sysrq_handle_kill,
	.help_msg	= "kIll",
	.action_msg	= "Kill All Tasks",
	.enable_mask	= SYSRQ_ENABLE_SIGNAL,
};

/* END SIGNAL SYSRQ HANDLERS BLOCK */

static void sysrq_handle_unrt(int key, struct pt_regs *pt_regs,
				struct tty_struct *tty)
{
	normalize_rt_tasks();
}
static struct sysrq_key_op sysrq_unrt_op = {
	.handler	= sysrq_handle_unrt,
	.help_msg	= "Nice",
	.action_msg	= "Nice All RT Tasks",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};

/* Key Operations table and lock */
static DEFINE_SPINLOCK(sysrq_key_table_lock);
#define SYSRQ_KEY_TABLE_LENGTH 36
static struct sysrq_key_op *sysrq_key_table[SYSRQ_KEY_TABLE_LENGTH] = {
/* 0 */	&sysrq_loglevel_op,
/* 1 */	&sysrq_loglevel_op,
/* 2 */	&sysrq_loglevel_op,
/* 3 */	&sysrq_loglevel_op,
/* 4 */	&sysrq_loglevel_op,
/* 5 */	&sysrq_loglevel_op,
/* 6 */	&sysrq_loglevel_op,
/* 7 */	&sysrq_loglevel_op,
/* 8 */	&sysrq_loglevel_op,
/* 9 */	&sysrq_loglevel_op,
/* a */	NULL, /* Don't use for system provided sysrqs,
		 it is handled specially on the sparc
		 and will never arrive */
/* b */	&sysrq_reboot_op,
#ifdef CONFIG_KEXEC
/* c */ &sysrq_crashdump_op,
#else
/* c */	NULL,
#endif
/* d */ NULL,
/* e */	&sysrq_term_op,
/* f */	&sysrq_moom_op,
/* g */	NULL,
/* h */	NULL,
/* i */	&sysrq_kill_op,
/* j */	NULL,
#ifdef CONFIG_VT
/* k */	&sysrq_SAK_op,
#else
/* k */	NULL,
#endif
/* l */	NULL,
/* m */	&sysrq_showmem_op,
/* n */	&sysrq_unrt_op,
/* o */	NULL, /* This will often be registered
		 as 'Off' at init time */
/* p */	&sysrq_showregs_op,
/* q */	NULL,
#ifdef CONFIG_VT
/* r */	&sysrq_unraw_op,
#else
/* r */ NULL,
#endif
/* s */	&sysrq_sync_op,
/* t */	&sysrq_showstate_op,
/* u */	&sysrq_mountro_op,
/* v */	NULL, /* May be assigned at init time by SMP VOYAGER */
/* w */	NULL,
/* x */	NULL,
/* y */	NULL,
/* z */	NULL
};

/* key2index calculation, -1 on invalid index */
static int sysrq_key_table_key2index(int key) {
	int retval;
	if ((key >= '0') && (key <= '9')) {
		retval = key - '0';
	} else if ((key >= 'a') && (key <= 'z')) {
		retval = key + 10 - 'a';
	} else {
		retval = -1;
	}
	return retval;
}

/*
 * get and put functions for the table, exposed to modules.
 */

struct sysrq_key_op *__sysrq_get_key_op (int key) {
        struct sysrq_key_op *op_p;
        int i;
	
	i = sysrq_key_table_key2index(key);
        op_p = (i == -1) ? NULL : sysrq_key_table[i];
        return op_p;
}

void __sysrq_put_key_op (int key, struct sysrq_key_op *op_p) {
        int i;

	i = sysrq_key_table_key2index(key);
        if (i != -1)
                sysrq_key_table[i] = op_p;
}

/*
 * This is the non-locking version of handle_sysrq
 * It must/can only be called by sysrq key handlers,
 * as they are inside of the lock
 */

void __handle_sysrq(int key, struct pt_regs *pt_regs, struct tty_struct *tty, int check_mask)
{
	struct sysrq_key_op *op_p;
	int orig_log_level;
	int i, j;
	unsigned long flags;

	spin_lock_irqsave(&sysrq_key_table_lock, flags);
	orig_log_level = console_loglevel;
	console_loglevel = 7;
	printk(KERN_INFO "SysRq : ");

        op_p = __sysrq_get_key_op(key);
        if (op_p) {
		/* Should we check for enabled operations (/proc/sysrq-trigger should not)
		 * and is the invoked operation enabled? */
		if (!check_mask || sysrq_enabled == 1 ||
		    (sysrq_enabled & op_p->enable_mask)) {
			printk ("%s\n", op_p->action_msg);
			console_loglevel = orig_log_level;
			op_p->handler(key, pt_regs, tty);
		}
		else
			printk("This sysrq operation is disabled.\n");
	} else {
		printk("HELP : ");
		/* Only print the help msg once per handler */
		for (i=0; i<SYSRQ_KEY_TABLE_LENGTH; i++) 
		if (sysrq_key_table[i]) {
			for (j=0; sysrq_key_table[i] != sysrq_key_table[j]; j++);
			if (j == i)
				printk ("%s ", sysrq_key_table[i]->help_msg);
		}
		printk ("\n");
		console_loglevel = orig_log_level;
	}
	spin_unlock_irqrestore(&sysrq_key_table_lock, flags);
}

/*
 * This function is called by the keyboard handler when SysRq is pressed
 * and any other keycode arrives.
 */

void handle_sysrq(int key, struct pt_regs *pt_regs, struct tty_struct *tty)
{
	if (!sysrq_enabled)
		return;
	__handle_sysrq(key, pt_regs, tty, 1);
}

int __sysrq_swap_key_ops(int key, struct sysrq_key_op *insert_op_p,
                                struct sysrq_key_op *remove_op_p) {

	int retval;
	unsigned long flags;

	spin_lock_irqsave(&sysrq_key_table_lock, flags);
	if (__sysrq_get_key_op(key) == remove_op_p) {
		__sysrq_put_key_op(key, insert_op_p);
		retval = 0;
	} else {
		retval = -1;
	}
	spin_unlock_irqrestore(&sysrq_key_table_lock, flags);

	return retval;
}

int register_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __sysrq_swap_key_ops(key, op_p, NULL);
}

int unregister_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __sysrq_swap_key_ops(key, NULL, op_p);
}

EXPORT_SYMBOL(handle_sysrq);
EXPORT_SYMBOL(register_sysrq_key);
EXPORT_SYMBOL(unregister_sysrq_key);

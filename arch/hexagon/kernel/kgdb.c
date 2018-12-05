/*
 * arch/hexagon/kernel/kgdb.c - Hexagon KGDB Support
 *
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>

/* All registers are 4 bytes, for now */
#define GDB_SIZEOF_REG 4

/* The register names are used during printing of the regs;
 * Keep these at three letters to pretty-print. */
struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	{ " r0", GDB_SIZEOF_REG, offsetof(struct pt_regs, r00)},
	{ " r1", GDB_SIZEOF_REG, offsetof(struct pt_regs, r01)},
	{ " r2", GDB_SIZEOF_REG, offsetof(struct pt_regs, r02)},
	{ " r3", GDB_SIZEOF_REG, offsetof(struct pt_regs, r03)},
	{ " r4", GDB_SIZEOF_REG, offsetof(struct pt_regs, r04)},
	{ " r5", GDB_SIZEOF_REG, offsetof(struct pt_regs, r05)},
	{ " r6", GDB_SIZEOF_REG, offsetof(struct pt_regs, r06)},
	{ " r7", GDB_SIZEOF_REG, offsetof(struct pt_regs, r07)},
	{ " r8", GDB_SIZEOF_REG, offsetof(struct pt_regs, r08)},
	{ " r9", GDB_SIZEOF_REG, offsetof(struct pt_regs, r09)},
	{ "r10", GDB_SIZEOF_REG, offsetof(struct pt_regs, r10)},
	{ "r11", GDB_SIZEOF_REG, offsetof(struct pt_regs, r11)},
	{ "r12", GDB_SIZEOF_REG, offsetof(struct pt_regs, r12)},
	{ "r13", GDB_SIZEOF_REG, offsetof(struct pt_regs, r13)},
	{ "r14", GDB_SIZEOF_REG, offsetof(struct pt_regs, r14)},
	{ "r15", GDB_SIZEOF_REG, offsetof(struct pt_regs, r15)},
	{ "r16", GDB_SIZEOF_REG, offsetof(struct pt_regs, r16)},
	{ "r17", GDB_SIZEOF_REG, offsetof(struct pt_regs, r17)},
	{ "r18", GDB_SIZEOF_REG, offsetof(struct pt_regs, r18)},
	{ "r19", GDB_SIZEOF_REG, offsetof(struct pt_regs, r19)},
	{ "r20", GDB_SIZEOF_REG, offsetof(struct pt_regs, r20)},
	{ "r21", GDB_SIZEOF_REG, offsetof(struct pt_regs, r21)},
	{ "r22", GDB_SIZEOF_REG, offsetof(struct pt_regs, r22)},
	{ "r23", GDB_SIZEOF_REG, offsetof(struct pt_regs, r23)},
	{ "r24", GDB_SIZEOF_REG, offsetof(struct pt_regs, r24)},
	{ "r25", GDB_SIZEOF_REG, offsetof(struct pt_regs, r25)},
	{ "r26", GDB_SIZEOF_REG, offsetof(struct pt_regs, r26)},
	{ "r27", GDB_SIZEOF_REG, offsetof(struct pt_regs, r27)},
	{ "r28", GDB_SIZEOF_REG, offsetof(struct pt_regs, r28)},
	{ "r29", GDB_SIZEOF_REG, offsetof(struct pt_regs, r29)},
	{ "r30", GDB_SIZEOF_REG, offsetof(struct pt_regs, r30)},
	{ "r31", GDB_SIZEOF_REG, offsetof(struct pt_regs, r31)},

	{ "usr", GDB_SIZEOF_REG, offsetof(struct pt_regs, usr)},
	{ "preds", GDB_SIZEOF_REG, offsetof(struct pt_regs, preds)},
	{ " m0", GDB_SIZEOF_REG, offsetof(struct pt_regs, m0)},
	{ " m1", GDB_SIZEOF_REG, offsetof(struct pt_regs, m1)},
	{ "sa0", GDB_SIZEOF_REG, offsetof(struct pt_regs, sa0)},
	{ "sa1", GDB_SIZEOF_REG, offsetof(struct pt_regs, sa1)},
	{ "lc0", GDB_SIZEOF_REG, offsetof(struct pt_regs, lc0)},
	{ "lc1", GDB_SIZEOF_REG, offsetof(struct pt_regs, lc1)},
	{ " gp", GDB_SIZEOF_REG, offsetof(struct pt_regs, gp)},
	{ "ugp", GDB_SIZEOF_REG, offsetof(struct pt_regs, ugp)},
	{ "cs0", GDB_SIZEOF_REG, offsetof(struct pt_regs, cs0)},
	{ "cs1", GDB_SIZEOF_REG, offsetof(struct pt_regs, cs1)},
	{ "psp", GDB_SIZEOF_REG, offsetof(struct pt_regs, hvmer.vmpsp)},
	{ "elr", GDB_SIZEOF_REG, offsetof(struct pt_regs, hvmer.vmel)},
	{ "est", GDB_SIZEOF_REG, offsetof(struct pt_regs, hvmer.vmest)},
	{ "badva", GDB_SIZEOF_REG, offsetof(struct pt_regs, hvmer.vmbadva)},
	{ "restart_r0", GDB_SIZEOF_REG, offsetof(struct pt_regs, restart_r0)},
	{ "syscall_nr", GDB_SIZEOF_REG, offsetof(struct pt_regs, syscall_nr)},
};

struct kgdb_arch arch_kgdb_ops = {
	/* trap0(#0xDB) 0x0cdb0054 */
	.gdb_bpt_instr = {0x54, 0x00, 0xdb, 0x0c},
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	*((unsigned long *) mem) = *((unsigned long *) ((void *)regs +
		dbg_reg_def[regno].offset));

	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	*((unsigned long *) ((void *)regs + dbg_reg_def[regno].offset)) =
		*((unsigned long *) mem);

	return 0;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	instruction_pointer(regs) = pc;
}

#ifdef CONFIG_SMP

/**
 * kgdb_roundup_cpus - Get other CPUs into a holding pattern
 *
 * On SMP systems, we need to get the attention of the other CPUs
 * and get them be in a known state.  This should do what is needed
 * to get the other CPUs to call kgdb_wait(). Note that on some arches,
 * the NMI approach is not used for rounding up all the CPUs. For example,
 * in case of MIPS, smp_call_function() is used to roundup CPUs.
 *
 * On non-SMP systems, this is not called.
 */

static void hexagon_kgdb_nmi_hook(void *ignored)
{
	kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());
}

void kgdb_roundup_cpus(void)
{
	local_irq_enable();
	smp_call_function(hexagon_kgdb_nmi_hook, NULL, 0);
	local_irq_disable();
}
#endif


/*  Not yet working  */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs,
				 struct task_struct *task)
{
	struct pt_regs *thread_regs;

	if (task == NULL)
		return;

	/* Initialize to zero */
	memset(gdb_regs, 0, NUMREGBYTES);

	/* Otherwise, we have only some registers from switch_to() */
	thread_regs = task_pt_regs(task);
	gdb_regs[0] = thread_regs->r00;
}

/**
 * kgdb_arch_handle_exception - Handle architecture specific GDB packets.
 * @vector: The error vector of the exception that happened.
 * @signo: The signal number of the exception that happened.
 * @err_code: The error code of the exception that happened.
 * @remcom_in_buffer: The buffer of the packet we have read.
 * @remcom_out_buffer: The buffer of %BUFMAX bytes to write a packet into.
 * @regs: The &struct pt_regs of the current process.
 *
 * This function MUST handle the 'c' and 's' command packets,
 * as well packets to set / remove a hardware breakpoint, if used.
 * If there are additional packets which the hardware needs to handle,
 * they are handled here.  The code should return -1 if it wants to
 * process more packets, and a %0 or %1 if it wants to exit from the
 * kgdb callback.
 *
 * Not yet working.
 */
int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *linux_regs)
{
	switch (remcom_in_buffer[0]) {
	case 's':
	case 'c':
		return 0;
	}
	/* Stay in the debugger. */
	return -1;
}

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	/* cpu roundup */
	if (atomic_read(&kgdb_active) != -1) {
		kgdb_nmicallback(smp_processor_id(), args->regs);
		return NOTIFY_STOP;
	}

	if (user_mode(args->regs))
		return NOTIFY_DONE;

	if (kgdb_handle_exception(args->trapnr & 0xff, args->signr, args->err,
				    args->regs))
		return NOTIFY_DONE;

	return NOTIFY_STOP;
}

static int
kgdb_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_notify(ptr, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call = kgdb_notify,

	/*
	 * Lowest-prio notifier priority, we want to be notified last:
	 */
	.priority = -INT_MAX,
};

/**
 * kgdb_arch_init - Perform any architecture specific initialization.
 *
 * This function will handle the initialization of any architecture
 * specific callbacks.
 */
int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

/**
 * kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 * This function will handle the uninitalization of any architecture
 * specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	unregister_die_notifier(&kgdb_notifier);
}

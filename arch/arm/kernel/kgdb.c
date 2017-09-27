/*
 * arch/arm/kernel/kgdb.c
 *
 * ARM KGDB support
 *
 * Copyright (c) 2002-2004 MontaVista Software, Inc
 * Copyright (c) 2008 Wind River Systems, Inc.
 *
 * Authors:  George Davis <davis_g@mvista.com>
 *           Deepak Saxena <dsaxena@plexity.net>
 */
#include <linux/irq.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>
#include <linux/uaccess.h>

#include <asm/patch.h>
#include <asm/traps.h>

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] =
{
	{ "r0", 4, offsetof(struct pt_regs, ARM_r0)},
	{ "r1", 4, offsetof(struct pt_regs, ARM_r1)},
	{ "r2", 4, offsetof(struct pt_regs, ARM_r2)},
	{ "r3", 4, offsetof(struct pt_regs, ARM_r3)},
	{ "r4", 4, offsetof(struct pt_regs, ARM_r4)},
	{ "r5", 4, offsetof(struct pt_regs, ARM_r5)},
	{ "r6", 4, offsetof(struct pt_regs, ARM_r6)},
	{ "r7", 4, offsetof(struct pt_regs, ARM_r7)},
	{ "r8", 4, offsetof(struct pt_regs, ARM_r8)},
	{ "r9", 4, offsetof(struct pt_regs, ARM_r9)},
	{ "r10", 4, offsetof(struct pt_regs, ARM_r10)},
	{ "fp", 4, offsetof(struct pt_regs, ARM_fp)},
	{ "ip", 4, offsetof(struct pt_regs, ARM_ip)},
	{ "sp", 4, offsetof(struct pt_regs, ARM_sp)},
	{ "lr", 4, offsetof(struct pt_regs, ARM_lr)},
	{ "pc", 4, offsetof(struct pt_regs, ARM_pc)},
	{ "f0", 12, -1 },
	{ "f1", 12, -1 },
	{ "f2", 12, -1 },
	{ "f3", 12, -1 },
	{ "f4", 12, -1 },
	{ "f5", 12, -1 },
	{ "f6", 12, -1 },
	{ "f7", 12, -1 },
	{ "fps", 4, -1 },
	{ "cpsr", 4, offsetof(struct pt_regs, ARM_cpsr)},
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy(mem, (void *)regs + dbg_reg_def[regno].offset,
		       dbg_reg_def[regno].size);
	else
		memset(mem, 0, dbg_reg_def[regno].size);
	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy((void *)regs + dbg_reg_def[regno].offset, mem,
		       dbg_reg_def[regno].size);
	return 0;
}

void
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *task)
{
	struct thread_info *ti;
	int regno;

	/* Just making sure... */
	if (task == NULL)
		return;

	/* Initialize to zero */
	for (regno = 0; regno < GDB_MAX_REGS; regno++)
		gdb_regs[regno] = 0;

	/* Otherwise, we have only some registers from switch_to() */
	ti			= task_thread_info(task);
	gdb_regs[_R4]		= ti->cpu_context.r4;
	gdb_regs[_R5]		= ti->cpu_context.r5;
	gdb_regs[_R6]		= ti->cpu_context.r6;
	gdb_regs[_R7]		= ti->cpu_context.r7;
	gdb_regs[_R8]		= ti->cpu_context.r8;
	gdb_regs[_R9]		= ti->cpu_context.r9;
	gdb_regs[_R10]		= ti->cpu_context.sl;
	gdb_regs[_FP]		= ti->cpu_context.fp;
	gdb_regs[_SPT]		= ti->cpu_context.sp;
	gdb_regs[_PC]		= ti->cpu_context.pc;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->ARM_pc = pc;
}

static int compiled_break;

int kgdb_arch_handle_exception(int exception_vector, int signo,
			       int err_code, char *remcom_in_buffer,
			       char *remcom_out_buffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	char *ptr;

	switch (remcom_in_buffer[0]) {
	case 'D':
	case 'k':
	case 'c':
		/*
		 * Try to read optional parameter, pc unchanged if no parm.
		 * If this was a compiled breakpoint, we need to move
		 * to the next instruction or we will just breakpoint
		 * over and over again.
		 */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			linux_regs->ARM_pc = addr;
		else if (compiled_break == 1)
			linux_regs->ARM_pc += 4;

		compiled_break = 0;

		return 0;
	}

	return -1;
}

static int kgdb_brk_fn(struct pt_regs *regs, unsigned int instr)
{
	kgdb_handle_exception(1, SIGTRAP, 0, regs);

	return 0;
}

static int kgdb_compiled_brk_fn(struct pt_regs *regs, unsigned int instr)
{
	compiled_break = 1;
	kgdb_handle_exception(1, SIGTRAP, 0, regs);

	return 0;
}

static struct undef_hook kgdb_brkpt_hook = {
	.instr_mask		= 0xffffffff,
	.instr_val		= KGDB_BREAKINST,
	.cpsr_mask		= MODE_MASK,
	.cpsr_val		= SVC_MODE,
	.fn			= kgdb_brk_fn
};

static struct undef_hook kgdb_compiled_brkpt_hook = {
	.instr_mask		= 0xffffffff,
	.instr_val		= KGDB_COMPILED_BREAK,
	.cpsr_mask		= MODE_MASK,
	.cpsr_val		= SVC_MODE,
	.fn			= kgdb_compiled_brk_fn
};

static void kgdb_call_nmi_hook(void *ignored)
{
       kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());
}

void kgdb_roundup_cpus(unsigned long flags)
{
       local_irq_enable();
       smp_call_function(kgdb_call_nmi_hook, NULL, 0);
       local_irq_disable();
}

static int __kgdb_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	if (kgdb_handle_exception(1, args->signr, cmd, regs))
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
	.notifier_call	= kgdb_notify,
	.priority	= -INT_MAX,
};


/**
 *	kgdb_arch_init - Perform any architecture specific initalization.
 *
 *	This function will handle the initalization of any architecture
 *	specific callbacks.
 */
int kgdb_arch_init(void)
{
	int ret = register_die_notifier(&kgdb_notifier);

	if (ret != 0)
		return ret;

	register_undef_hook(&kgdb_brkpt_hook);
	register_undef_hook(&kgdb_compiled_brkpt_hook);

	return 0;
}

/**
 *	kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 *	This function will handle the uninitalization of any architecture
 *	specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	unregister_undef_hook(&kgdb_brkpt_hook);
	unregister_undef_hook(&kgdb_compiled_brkpt_hook);
	unregister_die_notifier(&kgdb_notifier);
}

int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;

	/* patch_text() only supports int-sized breakpoints */
	BUILD_BUG_ON(sizeof(int) != BREAK_INSTR_SIZE);

	err = probe_kernel_read(bpt->saved_instr, (char *)bpt->bpt_addr,
				BREAK_INSTR_SIZE);
	if (err)
		return err;

	/* Machine is already stopped, so we can use __patch_text() directly */
	__patch_text((void *)bpt->bpt_addr,
		     *(unsigned int *)arch_kgdb_ops.gdb_bpt_instr);

	return err;
}

int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	/* Machine is already stopped, so we can use __patch_text() directly */
	__patch_text((void *)bpt->bpt_addr, *(unsigned int *)bpt->saved_instr);

	return 0;
}

/*
 * Register our undef instruction hooks with ARM undef core.
 * We register a hook specifically looking for the KGB break inst
 * and we handle the normal undef case within the do_undefinstr
 * handler.
 */
struct kgdb_arch arch_kgdb_ops = {
#ifndef __ARMEB__
	.gdb_bpt_instr		= {0xfe, 0xde, 0xff, 0xe7}
#else /* ! __ARMEB__ */
	.gdb_bpt_instr		= {0xe7, 0xff, 0xde, 0xfe}
#endif
};

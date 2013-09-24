/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE-Gx KGDB support.
 */

#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <asm/cacheflush.h>

static tile_bundle_bits singlestep_insn = TILEGX_BPT_BUNDLE | DIE_SSTEPBP;
static unsigned long stepped_addr;
static tile_bundle_bits stepped_instr;

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	{ "r0", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[0])},
	{ "r1", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[1])},
	{ "r2", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[2])},
	{ "r3", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[3])},
	{ "r4", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[4])},
	{ "r5", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[5])},
	{ "r6", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[6])},
	{ "r7", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[7])},
	{ "r8", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[8])},
	{ "r9", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[9])},
	{ "r10", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[10])},
	{ "r11", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[11])},
	{ "r12", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[12])},
	{ "r13", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[13])},
	{ "r14", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[14])},
	{ "r15", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[15])},
	{ "r16", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[16])},
	{ "r17", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[17])},
	{ "r18", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[18])},
	{ "r19", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[19])},
	{ "r20", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[20])},
	{ "r21", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[21])},
	{ "r22", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[22])},
	{ "r23", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[23])},
	{ "r24", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[24])},
	{ "r25", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[25])},
	{ "r26", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[26])},
	{ "r27", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[27])},
	{ "r28", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[28])},
	{ "r29", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[29])},
	{ "r30", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[30])},
	{ "r31", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[31])},
	{ "r32", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[32])},
	{ "r33", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[33])},
	{ "r34", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[34])},
	{ "r35", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[35])},
	{ "r36", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[36])},
	{ "r37", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[37])},
	{ "r38", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[38])},
	{ "r39", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[39])},
	{ "r40", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[40])},
	{ "r41", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[41])},
	{ "r42", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[42])},
	{ "r43", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[43])},
	{ "r44", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[44])},
	{ "r45", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[45])},
	{ "r46", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[46])},
	{ "r47", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[47])},
	{ "r48", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[48])},
	{ "r49", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[49])},
	{ "r50", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[50])},
	{ "r51", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[51])},
	{ "r52", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[52])},
	{ "tp", GDB_SIZEOF_REG, offsetof(struct pt_regs, tp)},
	{ "sp", GDB_SIZEOF_REG, offsetof(struct pt_regs, sp)},
	{ "lr", GDB_SIZEOF_REG, offsetof(struct pt_regs, lr)},
	{ "sn", GDB_SIZEOF_REG, -1},
	{ "idn0", GDB_SIZEOF_REG, -1},
	{ "idn1", GDB_SIZEOF_REG, -1},
	{ "udn0", GDB_SIZEOF_REG, -1},
	{ "udn1", GDB_SIZEOF_REG, -1},
	{ "udn2", GDB_SIZEOF_REG, -1},
	{ "udn3", GDB_SIZEOF_REG, -1},
	{ "zero", GDB_SIZEOF_REG, -1},
	{ "pc", GDB_SIZEOF_REG, offsetof(struct pt_regs, pc)},
	{ "faultnum", GDB_SIZEOF_REG, offsetof(struct pt_regs, faultnum)},
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

/*
 * Similar to pt_regs_to_gdb_regs() except that process is sleeping and so
 * we may not be able to get all the info.
 */
void
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *task)
{
	int reg;
	struct pt_regs *thread_regs;
	unsigned long *ptr = gdb_regs;

	if (task == NULL)
		return;

	/* Initialize to zero. */
	memset(gdb_regs, 0, NUMREGBYTES);

	thread_regs = task_pt_regs(task);
	for (reg = 0; reg <= TREG_LAST_GPR; reg++)
		*(ptr++) = thread_regs->regs[reg];

	gdb_regs[TILEGX_PC_REGNUM] = thread_regs->pc;
	gdb_regs[TILEGX_FAULTNUM_REGNUM] = thread_regs->faultnum;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->pc = pc;
}

static void kgdb_call_nmi_hook(void *ignored)
{
	kgdb_nmicallback(raw_smp_processor_id(), NULL);
}

void kgdb_roundup_cpus(unsigned long flags)
{
	local_irq_enable();
	smp_call_function(kgdb_call_nmi_hook, NULL, 0);
	local_irq_disable();
}

/*
 * Convert a kernel address to the writable kernel text mapping.
 */
static unsigned long writable_address(unsigned long addr)
{
	unsigned long ret = 0;

	if (core_kernel_text(addr))
		ret = addr - MEM_SV_START + PAGE_OFFSET;
	else if (is_module_text_address(addr))
		ret = addr;
	else
		pr_err("Unknown virtual address 0x%lx\n", addr);

	return ret;
}

/*
 * Calculate the new address for after a step.
 */
static unsigned long get_step_address(struct pt_regs *regs)
{
	int src_reg;
	int jump_off;
	int br_off;
	unsigned long addr;
	unsigned int opcode;
	tile_bundle_bits bundle;

	/* Move to the next instruction by default. */
	addr = regs->pc + TILEGX_BUNDLE_SIZE_IN_BYTES;
	bundle = *(unsigned long *)instruction_pointer(regs);

	/* 0: X mode, Otherwise: Y mode. */
	if (bundle & TILEGX_BUNDLE_MODE_MASK) {
		if (get_Opcode_Y1(bundle) == RRR_1_OPCODE_Y1 &&
		    get_RRROpcodeExtension_Y1(bundle) ==
		    UNARY_RRR_1_OPCODE_Y1) {
			opcode = get_UnaryOpcodeExtension_Y1(bundle);

			switch (opcode) {
			case JALR_UNARY_OPCODE_Y1:
			case JALRP_UNARY_OPCODE_Y1:
			case JR_UNARY_OPCODE_Y1:
			case JRP_UNARY_OPCODE_Y1:
				src_reg = get_SrcA_Y1(bundle);
				dbg_get_reg(src_reg, &addr, regs);
				break;
			}
		}
	} else if (get_Opcode_X1(bundle) == RRR_0_OPCODE_X1) {
		if (get_RRROpcodeExtension_X1(bundle) ==
		    UNARY_RRR_0_OPCODE_X1) {
			opcode = get_UnaryOpcodeExtension_X1(bundle);

			switch (opcode) {
			case JALR_UNARY_OPCODE_X1:
			case JALRP_UNARY_OPCODE_X1:
			case JR_UNARY_OPCODE_X1:
			case JRP_UNARY_OPCODE_X1:
				src_reg = get_SrcA_X1(bundle);
				dbg_get_reg(src_reg, &addr, regs);
				break;
			}
		}
	} else if (get_Opcode_X1(bundle) == JUMP_OPCODE_X1) {
		opcode = get_JumpOpcodeExtension_X1(bundle);

		switch (opcode) {
		case JAL_JUMP_OPCODE_X1:
		case J_JUMP_OPCODE_X1:
			jump_off = sign_extend(get_JumpOff_X1(bundle), 27);
			addr = regs->pc +
				(jump_off << TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES);
			break;
		}
	} else if (get_Opcode_X1(bundle) == BRANCH_OPCODE_X1) {
		br_off = 0;
		opcode = get_BrType_X1(bundle);

		switch (opcode) {
		case BEQZT_BRANCH_OPCODE_X1:
		case BEQZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) == 0)
				br_off = get_BrOff_X1(bundle);
			break;
		case BGEZT_BRANCH_OPCODE_X1:
		case BGEZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) >= 0)
				br_off = get_BrOff_X1(bundle);
			break;
		case BGTZT_BRANCH_OPCODE_X1:
		case BGTZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) > 0)
				br_off = get_BrOff_X1(bundle);
			break;
		case BLBCT_BRANCH_OPCODE_X1:
		case BLBC_BRANCH_OPCODE_X1:
			if (!(get_SrcA_X1(bundle) & 1))
				br_off = get_BrOff_X1(bundle);
			break;
		case BLBST_BRANCH_OPCODE_X1:
		case BLBS_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) & 1)
				br_off = get_BrOff_X1(bundle);
			break;
		case BLEZT_BRANCH_OPCODE_X1:
		case BLEZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) <= 0)
				br_off = get_BrOff_X1(bundle);
			break;
		case BLTZT_BRANCH_OPCODE_X1:
		case BLTZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) < 0)
				br_off = get_BrOff_X1(bundle);
			break;
		case BNEZT_BRANCH_OPCODE_X1:
		case BNEZ_BRANCH_OPCODE_X1:
			if (get_SrcA_X1(bundle) != 0)
				br_off = get_BrOff_X1(bundle);
			break;
		}

		if (br_off != 0) {
			br_off = sign_extend(br_off, 17);
			addr = regs->pc +
				(br_off << TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES);
		}
	}

	return addr;
}

/*
 * Replace the next instruction after the current instruction with a
 * breakpoint instruction.
 */
static void do_single_step(struct pt_regs *regs)
{
	unsigned long addr_wr;

	/* Determine where the target instruction will send us to. */
	stepped_addr = get_step_address(regs);
	probe_kernel_read((char *)&stepped_instr, (char *)stepped_addr,
			  BREAK_INSTR_SIZE);

	addr_wr = writable_address(stepped_addr);
	probe_kernel_write((char *)addr_wr, (char *)&singlestep_insn,
			   BREAK_INSTR_SIZE);
	smp_wmb();
	flush_icache_range(stepped_addr, stepped_addr + BREAK_INSTR_SIZE);
}

static void undo_single_step(struct pt_regs *regs)
{
	unsigned long addr_wr;

	if (stepped_instr == 0)
		return;

	addr_wr = writable_address(stepped_addr);
	probe_kernel_write((char *)addr_wr, (char *)&stepped_instr,
			   BREAK_INSTR_SIZE);
	stepped_instr = 0;
	smp_wmb();
	flush_icache_range(stepped_addr, stepped_addr + BREAK_INSTR_SIZE);
}

/*
 * Calls linux_debug_hook before the kernel dies. If KGDB is enabled,
 * then try to fall into the debugger.
 */
static int
kgdb_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	int ret;
	unsigned long flags;
	struct die_args *args = (struct die_args *)ptr;
	struct pt_regs *regs = args->regs;

#ifdef CONFIG_KPROBES
	/*
	 * Return immediately if the kprobes fault notifier has set
	 * DIE_PAGE_FAULT.
	 */
	if (cmd == DIE_PAGE_FAULT)
		return NOTIFY_DONE;
#endif /* CONFIG_KPROBES */

	switch (cmd) {
	case DIE_BREAK:
	case DIE_COMPILED_BPT:
		break;
	case DIE_SSTEPBP:
		local_irq_save(flags);
		kgdb_handle_exception(0, SIGTRAP, 0, regs);
		local_irq_restore(flags);
		return NOTIFY_STOP;
	default:
		/* Userspace events, ignore. */
		if (user_mode(regs))
			return NOTIFY_DONE;
	}

	local_irq_save(flags);
	ret = kgdb_handle_exception(args->trapnr, args->signr, args->err, regs);
	local_irq_restore(flags);
	if (ret)
		return NOTIFY_DONE;

	return NOTIFY_STOP;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call = kgdb_notify,
};

/*
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
 * they are handled here. The code should return -1 if it wants to
 * process more packets, and a %0 or %1 if it wants to exit from the
 * kgdb callback.
 */
int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	char *ptr;
	unsigned long address;

	/* Undo any stepping we may have done. */
	undo_single_step(regs);

	switch (remcom_in_buffer[0]) {
	case 'c':
	case 's':
	case 'D':
	case 'k':
		/*
		 * Try to read optional parameter, pc unchanged if no parm.
		 * If this was a compiled-in breakpoint, we need to move
		 * to the next instruction or we will just breakpoint
		 * over and over again.
		 */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &address))
			regs->pc = address;
		else if (*(unsigned long *)regs->pc == compiled_bpt)
			regs->pc += BREAK_INSTR_SIZE;

		if (remcom_in_buffer[0] == 's') {
			do_single_step(regs);
			kgdb_single_step = 1;
			atomic_set(&kgdb_cpu_doing_single_step,
				   raw_smp_processor_id());
		} else
			atomic_set(&kgdb_cpu_doing_single_step, -1);

		return 0;
	}

	return -1; /* this means that we do not want to exit from the handler */
}

struct kgdb_arch arch_kgdb_ops;

/*
 * kgdb_arch_init - Perform any architecture specific initalization.
 *
 * This function will handle the initalization of any architecture
 * specific callbacks.
 */
int kgdb_arch_init(void)
{
	tile_bundle_bits bundle = TILEGX_BPT_BUNDLE;

	memcpy(arch_kgdb_ops.gdb_bpt_instr, &bundle, BREAK_INSTR_SIZE);
	return register_die_notifier(&kgdb_notifier);
}

/*
 * kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 * This function will handle the uninitalization of any architecture
 * specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	unregister_die_notifier(&kgdb_notifier);
}

int kgdb_arch_set_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;
	unsigned long addr_wr = writable_address(bpt->bpt_addr);

	if (addr_wr == 0)
		return -1;

	err = probe_kernel_read(bpt->saved_instr, (char *)bpt->bpt_addr,
				BREAK_INSTR_SIZE);
	if (err)
		return err;

	err = probe_kernel_write((char *)addr_wr, arch_kgdb_ops.gdb_bpt_instr,
				 BREAK_INSTR_SIZE);
	smp_wmb();
	flush_icache_range((unsigned long)bpt->bpt_addr,
			   (unsigned long)bpt->bpt_addr + BREAK_INSTR_SIZE);
	return err;
}

int kgdb_arch_remove_breakpoint(struct kgdb_bkpt *bpt)
{
	int err;
	unsigned long addr_wr = writable_address(bpt->bpt_addr);

	if (addr_wr == 0)
		return -1;

	err = probe_kernel_write((char *)addr_wr, (char *)bpt->saved_instr,
				 BREAK_INSTR_SIZE);
	smp_wmb();
	flush_icache_range((unsigned long)bpt->bpt_addr,
			   (unsigned long)bpt->bpt_addr + BREAK_INSTR_SIZE);
	return err;
}

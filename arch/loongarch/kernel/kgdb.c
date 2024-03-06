// SPDX-License-Identifier: GPL-2.0-only
/*
 * LoongArch KGDB support
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/hw_breakpoint.h>
#include <linux/kdebug.h>
#include <linux/kgdb.h>
#include <linux/processor.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/hw_breakpoint.h>
#include <asm/inst.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>

int kgdb_watch_activated;
static unsigned int stepped_opcode;
static unsigned long stepped_address;

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	{ "r0", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[0]) },
	{ "r1", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[1]) },
	{ "r2", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[2]) },
	{ "r3", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[3]) },
	{ "r4", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[4]) },
	{ "r5", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[5]) },
	{ "r6", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[6]) },
	{ "r7", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[7]) },
	{ "r8", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[8]) },
	{ "r9", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[9]) },
	{ "r10", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[10]) },
	{ "r11", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[11]) },
	{ "r12", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[12]) },
	{ "r13", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[13]) },
	{ "r14", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[14]) },
	{ "r15", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[15]) },
	{ "r16", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[16]) },
	{ "r17", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[17]) },
	{ "r18", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[18]) },
	{ "r19", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[19]) },
	{ "r20", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[20]) },
	{ "r21", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[21]) },
	{ "r22", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[22]) },
	{ "r23", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[23]) },
	{ "r24", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[24]) },
	{ "r25", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[25]) },
	{ "r26", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[26]) },
	{ "r27", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[27]) },
	{ "r28", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[28]) },
	{ "r29", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[29]) },
	{ "r30", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[30]) },
	{ "r31", GDB_SIZEOF_REG, offsetof(struct pt_regs, regs[31]) },
	{ "orig_a0", GDB_SIZEOF_REG, offsetof(struct pt_regs, orig_a0) },
	{ "pc", GDB_SIZEOF_REG, offsetof(struct pt_regs, csr_era) },
	{ "badv", GDB_SIZEOF_REG, offsetof(struct pt_regs, csr_badvaddr) },
	{ "f0", GDB_SIZEOF_REG, 0 },
	{ "f1", GDB_SIZEOF_REG, 1 },
	{ "f2", GDB_SIZEOF_REG, 2 },
	{ "f3", GDB_SIZEOF_REG, 3 },
	{ "f4", GDB_SIZEOF_REG, 4 },
	{ "f5", GDB_SIZEOF_REG, 5 },
	{ "f6", GDB_SIZEOF_REG, 6 },
	{ "f7", GDB_SIZEOF_REG, 7 },
	{ "f8", GDB_SIZEOF_REG, 8 },
	{ "f9", GDB_SIZEOF_REG, 9 },
	{ "f10", GDB_SIZEOF_REG, 10 },
	{ "f11", GDB_SIZEOF_REG, 11 },
	{ "f12", GDB_SIZEOF_REG, 12 },
	{ "f13", GDB_SIZEOF_REG, 13 },
	{ "f14", GDB_SIZEOF_REG, 14 },
	{ "f15", GDB_SIZEOF_REG, 15 },
	{ "f16", GDB_SIZEOF_REG, 16 },
	{ "f17", GDB_SIZEOF_REG, 17 },
	{ "f18", GDB_SIZEOF_REG, 18 },
	{ "f19", GDB_SIZEOF_REG, 19 },
	{ "f20", GDB_SIZEOF_REG, 20 },
	{ "f21", GDB_SIZEOF_REG, 21 },
	{ "f22", GDB_SIZEOF_REG, 22 },
	{ "f23", GDB_SIZEOF_REG, 23 },
	{ "f24", GDB_SIZEOF_REG, 24 },
	{ "f25", GDB_SIZEOF_REG, 25 },
	{ "f26", GDB_SIZEOF_REG, 26 },
	{ "f27", GDB_SIZEOF_REG, 27 },
	{ "f28", GDB_SIZEOF_REG, 28 },
	{ "f29", GDB_SIZEOF_REG, 29 },
	{ "f30", GDB_SIZEOF_REG, 30 },
	{ "f31", GDB_SIZEOF_REG, 31 },
	{ "fcc0", 1, 0 },
	{ "fcc1", 1, 1 },
	{ "fcc2", 1, 2 },
	{ "fcc3", 1, 3 },
	{ "fcc4", 1, 4 },
	{ "fcc5", 1, 5 },
	{ "fcc6", 1, 6 },
	{ "fcc7", 1, 7 },
	{ "fcsr", 4, 0 },
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	int reg_offset, reg_size;

	if (regno < 0 || regno >= DBG_MAX_REG_NUM)
		return NULL;

	reg_offset = dbg_reg_def[regno].offset;
	reg_size = dbg_reg_def[regno].size;

	if (reg_offset == -1)
		goto out;

	/* Handle general-purpose/orig_a0/pc/badv registers */
	if (regno <= DBG_PT_REGS_END) {
		memcpy(mem, (void *)regs + reg_offset, reg_size);
		goto out;
	}

	if (!(regs->csr_euen & CSR_EUEN_FPEN))
		goto out;

	save_fp(current);

	/* Handle FP registers */
	switch (regno) {
	case DBG_FCSR:				/* Process the fcsr */
		memcpy(mem, (void *)&current->thread.fpu.fcsr, reg_size);
		break;
	case DBG_FCC_BASE ... DBG_FCC_END:	/* Process the fcc */
		memcpy(mem, (void *)&current->thread.fpu.fcc + reg_offset, reg_size);
		break;
	case DBG_FPR_BASE ... DBG_FPR_END:	/* Process the fpr */
		memcpy(mem, (void *)&current->thread.fpu.fpr[reg_offset], reg_size);
		break;
	default:
		break;
	}

out:
	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	int reg_offset, reg_size;

	if (regno < 0 || regno >= DBG_MAX_REG_NUM)
		return -EINVAL;

	reg_offset = dbg_reg_def[regno].offset;
	reg_size = dbg_reg_def[regno].size;

	if (reg_offset == -1)
		return 0;

	/* Handle general-purpose/orig_a0/pc/badv registers */
	if (regno <= DBG_PT_REGS_END) {
		memcpy((void *)regs + reg_offset, mem, reg_size);
		return 0;
	}

	if (!(regs->csr_euen & CSR_EUEN_FPEN))
		return 0;

	/* Handle FP registers */
	switch (regno) {
	case DBG_FCSR:				/* Process the fcsr */
		memcpy((void *)&current->thread.fpu.fcsr, mem, reg_size);
		break;
	case DBG_FCC_BASE ... DBG_FCC_END:	/* Process the fcc */
		memcpy((void *)&current->thread.fpu.fcc + reg_offset, mem, reg_size);
		break;
	case DBG_FPR_BASE ... DBG_FPR_END:	/* Process the fpr */
		memcpy((void *)&current->thread.fpu.fpr[reg_offset], mem, reg_size);
		break;
	default:
		break;
	}

	restore_fp(current);

	return 0;
}

/*
 * Similar to regs_to_gdb_regs() except that process is sleeping and so
 * we may not be able to get all the info.
 */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	/* Initialize to zero */
	memset((char *)gdb_regs, 0, NUMREGBYTES);

	gdb_regs[DBG_LOONGARCH_RA] = p->thread.reg01;
	gdb_regs[DBG_LOONGARCH_TP] = (long)p;
	gdb_regs[DBG_LOONGARCH_SP] = p->thread.reg03;

	/* S0 - S8 */
	gdb_regs[DBG_LOONGARCH_S0] = p->thread.reg23;
	gdb_regs[DBG_LOONGARCH_S1] = p->thread.reg24;
	gdb_regs[DBG_LOONGARCH_S2] = p->thread.reg25;
	gdb_regs[DBG_LOONGARCH_S3] = p->thread.reg26;
	gdb_regs[DBG_LOONGARCH_S4] = p->thread.reg27;
	gdb_regs[DBG_LOONGARCH_S5] = p->thread.reg28;
	gdb_regs[DBG_LOONGARCH_S6] = p->thread.reg29;
	gdb_regs[DBG_LOONGARCH_S7] = p->thread.reg30;
	gdb_regs[DBG_LOONGARCH_S8] = p->thread.reg31;

	/*
	 * PC use return address (RA), i.e. the moment after return from __switch_to()
	 */
	gdb_regs[DBG_LOONGARCH_PC] = p->thread.reg01;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->csr_era = pc;
}

void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (			\
		".globl kgdb_breakinst\n\t"	\
		"nop\n"				\
		"kgdb_breakinst:\tbreak 2\n\t"); /* BRK_KDB = 2 */
}

/*
 * Calls linux_debug_hook before the kernel dies. If KGDB is enabled,
 * then try to fall into the debugger
 */
static int kgdb_loongarch_notify(struct notifier_block *self, unsigned long cmd, void *ptr)
{
	struct die_args *args = (struct die_args *)ptr;
	struct pt_regs *regs = args->regs;

	/* Userspace events, ignore. */
	if (user_mode(regs))
		return NOTIFY_DONE;

	if (!kgdb_io_module_registered)
		return NOTIFY_DONE;

	if (atomic_read(&kgdb_active) != -1)
		kgdb_nmicallback(smp_processor_id(), regs);

	if (kgdb_handle_exception(args->trapnr, args->signr, cmd, regs))
		return NOTIFY_DONE;

	if (atomic_read(&kgdb_setting_breakpoint))
		if (regs->csr_era == (unsigned long)&kgdb_breakinst)
			regs->csr_era += LOONGARCH_INSN_SIZE;

	return NOTIFY_STOP;
}

bool kgdb_breakpoint_handler(struct pt_regs *regs)
{
	struct die_args args = {
		.regs	= regs,
		.str	= "Break",
		.err	= BRK_KDB,
		.trapnr = read_csr_excode(),
		.signr	= SIGTRAP,

	};

	return (kgdb_loongarch_notify(NULL, DIE_TRAP, &args) == NOTIFY_STOP) ? true : false;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call = kgdb_loongarch_notify,
};

static inline void kgdb_arch_update_addr(struct pt_regs *regs,
					 char *remcom_in_buffer)
{
	unsigned long addr;
	char *ptr;

	ptr = &remcom_in_buffer[1];
	if (kgdb_hex2long(&ptr, &addr))
		regs->csr_era = addr;
}

/* Calculate the new address for after a step */
static int get_step_address(struct pt_regs *regs, unsigned long *next_addr)
{
	char cj_val;
	unsigned int si, si_l, si_h, rd, rj, cj;
	unsigned long pc = instruction_pointer(regs);
	union loongarch_instruction *ip = (union loongarch_instruction *)pc;

	if (pc & 3) {
		pr_warn("%s: invalid pc 0x%lx\n", __func__, pc);
		return -EINVAL;
	}

	*next_addr = pc + LOONGARCH_INSN_SIZE;

	si_h = ip->reg0i26_format.immediate_h;
	si_l = ip->reg0i26_format.immediate_l;
	switch (ip->reg0i26_format.opcode) {
	case b_op:
		*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 27);
		return 0;
	case bl_op:
		*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 27);
		regs->regs[1] = pc + LOONGARCH_INSN_SIZE;
		return 0;
	}

	rj = ip->reg1i21_format.rj;
	cj = (rj & 0x07) + DBG_FCC_BASE;
	si_l = ip->reg1i21_format.immediate_l;
	si_h = ip->reg1i21_format.immediate_h;
	dbg_get_reg(cj, &cj_val, regs);
	switch (ip->reg1i21_format.opcode) {
	case beqz_op:
		if (regs->regs[rj] == 0)
			*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 22);
		return 0;
	case bnez_op:
		if (regs->regs[rj] != 0)
			*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 22);
		return 0;
	case bceqz_op: /* bceqz_op = bcnez_op */
		if (((rj & 0x18) == 0x00) && !cj_val) /* bceqz */
			*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 22);
		if (((rj & 0x18) == 0x08) && cj_val) /* bcnez */
			*next_addr = pc + sign_extend64((si_h << 16 | si_l) << 2, 22);
		return 0;
	}

	rj = ip->reg2i16_format.rj;
	rd = ip->reg2i16_format.rd;
	si = ip->reg2i16_format.immediate;
	switch (ip->reg2i16_format.opcode) {
	case beq_op:
		if (regs->regs[rj] == regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case bne_op:
		if (regs->regs[rj] != regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case blt_op:
		if ((long)regs->regs[rj] < (long)regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case bge_op:
		if ((long)regs->regs[rj] >= (long)regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case bltu_op:
		if (regs->regs[rj] < regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case bgeu_op:
		if (regs->regs[rj] >= regs->regs[rd])
			*next_addr = pc + sign_extend64(si << 2, 17);
		return 0;
	case jirl_op:
		regs->regs[rd] = pc + LOONGARCH_INSN_SIZE;
		*next_addr = regs->regs[rj] + sign_extend64(si << 2, 17);
		return 0;
	}

	return 0;
}

static int do_single_step(struct pt_regs *regs)
{
	int error = 0;
	unsigned long addr = 0; /* Determine where the target instruction will send us to */

	error = get_step_address(regs, &addr);
	if (error)
		return error;

	/* Store the opcode in the stepped address */
	error = get_kernel_nofault(stepped_opcode, (void *)addr);
	if (error)
		return error;

	stepped_address = addr;

	/* Replace the opcode with the break instruction */
	error = copy_to_kernel_nofault((void *)stepped_address,
				       arch_kgdb_ops.gdb_bpt_instr, BREAK_INSTR_SIZE);
	flush_icache_range(addr, addr + BREAK_INSTR_SIZE);

	if (error) {
		stepped_opcode = 0;
		stepped_address = 0;
	} else {
		kgdb_single_step = 1;
		atomic_set(&kgdb_cpu_doing_single_step, raw_smp_processor_id());
	}

	return error;
}

/* Undo a single step */
static void undo_single_step(struct pt_regs *regs)
{
	if (stepped_opcode) {
		copy_to_kernel_nofault((void *)stepped_address,
				       (void *)&stepped_opcode, BREAK_INSTR_SIZE);
		flush_icache_range(stepped_address, stepped_address + BREAK_INSTR_SIZE);
	}

	stepped_opcode = 0;
	stepped_address = 0;
	kgdb_single_step = 0;
	atomic_set(&kgdb_cpu_doing_single_step, -1);
}

int kgdb_arch_handle_exception(int vector, int signo, int err_code,
			       char *remcom_in_buffer, char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	int ret = 0;

	undo_single_step(regs);
	regs->csr_prmd |= CSR_PRMD_PWE;

	switch (remcom_in_buffer[0]) {
	case 'D':
	case 'k':
		regs->csr_prmd &= ~CSR_PRMD_PWE;
		fallthrough;
	case 'c':
		kgdb_arch_update_addr(regs, remcom_in_buffer);
		break;
	case 's':
		kgdb_arch_update_addr(regs, remcom_in_buffer);
		ret = do_single_step(regs);
		break;
	default:
		ret = -1;
	}

	return ret;
}

static struct hw_breakpoint {
	unsigned int		enabled;
	unsigned long		addr;
	int			len;
	int			type;
	struct perf_event	* __percpu *pev;
} breakinfo[LOONGARCH_MAX_BRP];

static int hw_break_reserve_slot(int breakno)
{
	int cpu, cnt = 0;
	struct perf_event **pevent;

	for_each_online_cpu(cpu) {
		cnt++;
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		if (dbg_reserve_bp_slot(*pevent))
			goto fail;
	}

	return 0;

fail:
	for_each_online_cpu(cpu) {
		cnt--;
		if (!cnt)
			break;
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		dbg_release_bp_slot(*pevent);
	}

	return -1;
}

static int hw_break_release_slot(int breakno)
{
	int cpu;
	struct perf_event **pevent;

	if (dbg_is_early)
		return 0;

	for_each_online_cpu(cpu) {
		pevent = per_cpu_ptr(breakinfo[breakno].pev, cpu);
		if (dbg_release_bp_slot(*pevent))
			/*
			 * The debugger is responsible for handing the retry on
			 * remove failure.
			 */
			return -1;
	}

	return 0;
}

static int kgdb_set_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	int i;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++)
		if (!breakinfo[i].enabled)
			break;

	if (i == LOONGARCH_MAX_BRP)
		return -1;

	switch (bptype) {
	case BP_HARDWARE_BREAKPOINT:
		breakinfo[i].type = HW_BREAKPOINT_X;
		break;
	case BP_READ_WATCHPOINT:
		breakinfo[i].type = HW_BREAKPOINT_R;
		break;
	case BP_WRITE_WATCHPOINT:
		breakinfo[i].type = HW_BREAKPOINT_W;
		break;
	case BP_ACCESS_WATCHPOINT:
		breakinfo[i].type = HW_BREAKPOINT_RW;
		break;
	default:
		return -1;
	}

	switch (len) {
	case 1:
		breakinfo[i].len = HW_BREAKPOINT_LEN_1;
		break;
	case 2:
		breakinfo[i].len = HW_BREAKPOINT_LEN_2;
		break;
	case 4:
		breakinfo[i].len = HW_BREAKPOINT_LEN_4;
		break;
	case 8:
		breakinfo[i].len = HW_BREAKPOINT_LEN_8;
		break;
	default:
		return -1;
	}

	breakinfo[i].addr = addr;
	if (hw_break_reserve_slot(i)) {
		breakinfo[i].addr = 0;
		return -1;
	}
	breakinfo[i].enabled = 1;

	return 0;
}

static int kgdb_remove_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	int i;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++)
		if (breakinfo[i].addr == addr && breakinfo[i].enabled)
			break;

	if (i == LOONGARCH_MAX_BRP)
		return -1;

	if (hw_break_release_slot(i)) {
		pr_err("Cannot remove hw breakpoint at %lx\n", addr);
		return -1;
	}
	breakinfo[i].enabled = 0;

	return 0;
}

static void kgdb_disable_hw_break(struct pt_regs *regs)
{
	int i;
	int cpu = raw_smp_processor_id();
	struct perf_event *bp;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		if (!breakinfo[i].enabled)
			continue;

		bp = *per_cpu_ptr(breakinfo[i].pev, cpu);
		if (bp->attr.disabled == 1)
			continue;

		arch_uninstall_hw_breakpoint(bp);
		bp->attr.disabled = 1;
	}

	/* Disable hardware debugging while we are in kgdb */
	csr_xchg32(0, CSR_CRMD_WE, LOONGARCH_CSR_CRMD);
}

static void kgdb_remove_all_hw_break(void)
{
	int i;
	int cpu = raw_smp_processor_id();
	struct perf_event *bp;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		if (!breakinfo[i].enabled)
			continue;

		bp = *per_cpu_ptr(breakinfo[i].pev, cpu);
		if (!bp->attr.disabled) {
			arch_uninstall_hw_breakpoint(bp);
			bp->attr.disabled = 1;
			continue;
		}

		if (hw_break_release_slot(i))
			pr_err("KGDB: hw bpt remove failed %lx\n", breakinfo[i].addr);
		breakinfo[i].enabled = 0;
	}

	csr_xchg32(0, CSR_CRMD_WE, LOONGARCH_CSR_CRMD);
	kgdb_watch_activated = 0;
}

static void kgdb_correct_hw_break(void)
{
	int i, activated = 0;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		struct perf_event *bp;
		int val;
		int cpu = raw_smp_processor_id();

		if (!breakinfo[i].enabled)
			continue;

		bp = *per_cpu_ptr(breakinfo[i].pev, cpu);
		if (bp->attr.disabled != 1)
			continue;

		bp->attr.bp_addr = breakinfo[i].addr;
		bp->attr.bp_len = breakinfo[i].len;
		bp->attr.bp_type = breakinfo[i].type;

		val = hw_breakpoint_arch_parse(bp, &bp->attr, counter_arch_bp(bp));
		if (val)
			return;

		val = arch_install_hw_breakpoint(bp);
		if (!val)
			bp->attr.disabled = 0;
		activated = 1;
	}

	csr_xchg32(activated ? CSR_CRMD_WE : 0, CSR_CRMD_WE, LOONGARCH_CSR_CRMD);
	kgdb_watch_activated = activated;
}

const struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr		= {0x02, 0x00, break_op >> 1, 0x00}, /* BRK_KDB = 2 */
	.flags			= KGDB_HW_BREAKPOINT,
	.set_hw_breakpoint	= kgdb_set_hw_break,
	.remove_hw_breakpoint	= kgdb_remove_hw_break,
	.disable_hw_break	= kgdb_disable_hw_break,
	.remove_all_hw_break	= kgdb_remove_all_hw_break,
	.correct_hw_break	= kgdb_correct_hw_break,
};

int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

void kgdb_arch_late(void)
{
	int i, cpu;
	struct perf_event_attr attr;
	struct perf_event **pevent;

	hw_breakpoint_init(&attr);

	attr.bp_addr = (unsigned long)kgdb_arch_init;
	attr.bp_len = HW_BREAKPOINT_LEN_4;
	attr.bp_type = HW_BREAKPOINT_W;
	attr.disabled = 1;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		if (breakinfo[i].pev)
			continue;

		breakinfo[i].pev = register_wide_hw_breakpoint(&attr, NULL, NULL);
		if (IS_ERR((void * __force)breakinfo[i].pev)) {
			pr_err("kgdb: Could not allocate hw breakpoints.\n");
			breakinfo[i].pev = NULL;
			return;
		}

		for_each_online_cpu(cpu) {
			pevent = per_cpu_ptr(breakinfo[i].pev, cpu);
			if (pevent[0]->destroy) {
				pevent[0]->destroy = NULL;
				release_bp_slot(*pevent);
			}
		}
	}
}

void kgdb_arch_exit(void)
{
	int i;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		if (breakinfo[i].pev) {
			unregister_wide_hw_breakpoint(breakinfo[i].pev);
			breakinfo[i].pev = NULL;
		}
	}

	unregister_die_notifier(&kgdb_notifier);
}

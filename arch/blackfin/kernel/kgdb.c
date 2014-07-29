/*
 * arch/blackfin/kernel/kgdb.c - Blackfin kgdb pieces
 *
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/ptrace.h>		/* for linux pt_regs struct */
#include <linux/kgdb.h>
#include <linux/uaccess.h>
#include <asm/irq_regs.h>

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	gdb_regs[BFIN_R0] = regs->r0;
	gdb_regs[BFIN_R1] = regs->r1;
	gdb_regs[BFIN_R2] = regs->r2;
	gdb_regs[BFIN_R3] = regs->r3;
	gdb_regs[BFIN_R4] = regs->r4;
	gdb_regs[BFIN_R5] = regs->r5;
	gdb_regs[BFIN_R6] = regs->r6;
	gdb_regs[BFIN_R7] = regs->r7;
	gdb_regs[BFIN_P0] = regs->p0;
	gdb_regs[BFIN_P1] = regs->p1;
	gdb_regs[BFIN_P2] = regs->p2;
	gdb_regs[BFIN_P3] = regs->p3;
	gdb_regs[BFIN_P4] = regs->p4;
	gdb_regs[BFIN_P5] = regs->p5;
	gdb_regs[BFIN_SP] = regs->reserved;
	gdb_regs[BFIN_FP] = regs->fp;
	gdb_regs[BFIN_I0] = regs->i0;
	gdb_regs[BFIN_I1] = regs->i1;
	gdb_regs[BFIN_I2] = regs->i2;
	gdb_regs[BFIN_I3] = regs->i3;
	gdb_regs[BFIN_M0] = regs->m0;
	gdb_regs[BFIN_M1] = regs->m1;
	gdb_regs[BFIN_M2] = regs->m2;
	gdb_regs[BFIN_M3] = regs->m3;
	gdb_regs[BFIN_B0] = regs->b0;
	gdb_regs[BFIN_B1] = regs->b1;
	gdb_regs[BFIN_B2] = regs->b2;
	gdb_regs[BFIN_B3] = regs->b3;
	gdb_regs[BFIN_L0] = regs->l0;
	gdb_regs[BFIN_L1] = regs->l1;
	gdb_regs[BFIN_L2] = regs->l2;
	gdb_regs[BFIN_L3] = regs->l3;
	gdb_regs[BFIN_A0_DOT_X] = regs->a0x;
	gdb_regs[BFIN_A0_DOT_W] = regs->a0w;
	gdb_regs[BFIN_A1_DOT_X] = regs->a1x;
	gdb_regs[BFIN_A1_DOT_W] = regs->a1w;
	gdb_regs[BFIN_ASTAT] = regs->astat;
	gdb_regs[BFIN_RETS] = regs->rets;
	gdb_regs[BFIN_LC0] = regs->lc0;
	gdb_regs[BFIN_LT0] = regs->lt0;
	gdb_regs[BFIN_LB0] = regs->lb0;
	gdb_regs[BFIN_LC1] = regs->lc1;
	gdb_regs[BFIN_LT1] = regs->lt1;
	gdb_regs[BFIN_LB1] = regs->lb1;
	gdb_regs[BFIN_CYCLES] = 0;
	gdb_regs[BFIN_CYCLES2] = 0;
	gdb_regs[BFIN_USP] = regs->usp;
	gdb_regs[BFIN_SEQSTAT] = regs->seqstat;
	gdb_regs[BFIN_SYSCFG] = regs->syscfg;
	gdb_regs[BFIN_RETI] = regs->pc;
	gdb_regs[BFIN_RETX] = regs->retx;
	gdb_regs[BFIN_RETN] = regs->retn;
	gdb_regs[BFIN_RETE] = regs->rete;
	gdb_regs[BFIN_PC] = regs->pc;
	gdb_regs[BFIN_CC] = (regs->astat >> 5) & 1;
	gdb_regs[BFIN_EXTRA1] = 0;
	gdb_regs[BFIN_EXTRA2] = 0;
	gdb_regs[BFIN_EXTRA3] = 0;
	gdb_regs[BFIN_IPEND] = regs->ipend;
}

/*
 * Extracts ebp, esp and eip values understandable by gdb from the values
 * saved by switch_to.
 * thread.esp points to ebp. flags and ebp are pushed in switch_to hence esp
 * prior to entering switch_to is 8 greater than the value that is saved.
 * If switch_to changes, change following code appropriately.
 */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	gdb_regs[BFIN_SP] = p->thread.ksp;
	gdb_regs[BFIN_PC] = p->thread.pc;
	gdb_regs[BFIN_SEQSTAT] = p->thread.seqstat;
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	regs->r0 = gdb_regs[BFIN_R0];
	regs->r1 = gdb_regs[BFIN_R1];
	regs->r2 = gdb_regs[BFIN_R2];
	regs->r3 = gdb_regs[BFIN_R3];
	regs->r4 = gdb_regs[BFIN_R4];
	regs->r5 = gdb_regs[BFIN_R5];
	regs->r6 = gdb_regs[BFIN_R6];
	regs->r7 = gdb_regs[BFIN_R7];
	regs->p0 = gdb_regs[BFIN_P0];
	regs->p1 = gdb_regs[BFIN_P1];
	regs->p2 = gdb_regs[BFIN_P2];
	regs->p3 = gdb_regs[BFIN_P3];
	regs->p4 = gdb_regs[BFIN_P4];
	regs->p5 = gdb_regs[BFIN_P5];
	regs->fp = gdb_regs[BFIN_FP];
	regs->i0 = gdb_regs[BFIN_I0];
	regs->i1 = gdb_regs[BFIN_I1];
	regs->i2 = gdb_regs[BFIN_I2];
	regs->i3 = gdb_regs[BFIN_I3];
	regs->m0 = gdb_regs[BFIN_M0];
	regs->m1 = gdb_regs[BFIN_M1];
	regs->m2 = gdb_regs[BFIN_M2];
	regs->m3 = gdb_regs[BFIN_M3];
	regs->b0 = gdb_regs[BFIN_B0];
	regs->b1 = gdb_regs[BFIN_B1];
	regs->b2 = gdb_regs[BFIN_B2];
	regs->b3 = gdb_regs[BFIN_B3];
	regs->l0 = gdb_regs[BFIN_L0];
	regs->l1 = gdb_regs[BFIN_L1];
	regs->l2 = gdb_regs[BFIN_L2];
	regs->l3 = gdb_regs[BFIN_L3];
	regs->a0x = gdb_regs[BFIN_A0_DOT_X];
	regs->a0w = gdb_regs[BFIN_A0_DOT_W];
	regs->a1x = gdb_regs[BFIN_A1_DOT_X];
	regs->a1w = gdb_regs[BFIN_A1_DOT_W];
	regs->rets = gdb_regs[BFIN_RETS];
	regs->lc0 = gdb_regs[BFIN_LC0];
	regs->lt0 = gdb_regs[BFIN_LT0];
	regs->lb0 = gdb_regs[BFIN_LB0];
	regs->lc1 = gdb_regs[BFIN_LC1];
	regs->lt1 = gdb_regs[BFIN_LT1];
	regs->lb1 = gdb_regs[BFIN_LB1];
	regs->usp = gdb_regs[BFIN_USP];
	regs->syscfg = gdb_regs[BFIN_SYSCFG];
	regs->retx = gdb_regs[BFIN_RETX];
	regs->retn = gdb_regs[BFIN_RETN];
	regs->rete = gdb_regs[BFIN_RETE];
	regs->pc = gdb_regs[BFIN_PC];

#if 0				/* can't change these */
	regs->astat = gdb_regs[BFIN_ASTAT];
	regs->seqstat = gdb_regs[BFIN_SEQSTAT];
	regs->ipend = gdb_regs[BFIN_IPEND];
#endif
}

static struct hw_breakpoint {
	unsigned int occupied:1;
	unsigned int skip:1;
	unsigned int enabled:1;
	unsigned int type:1;
	unsigned int dataacc:2;
	unsigned short count;
	unsigned int addr;
} breakinfo[HW_WATCHPOINT_NUM];

static int bfin_set_hw_break(unsigned long addr, int len, enum kgdb_bptype type)
{
	int breakno;
	int bfin_type;
	int dataacc = 0;

	switch (type) {
	case BP_HARDWARE_BREAKPOINT:
		bfin_type = TYPE_INST_WATCHPOINT;
		break;
	case BP_WRITE_WATCHPOINT:
		dataacc = 1;
		bfin_type = TYPE_DATA_WATCHPOINT;
		break;
	case BP_READ_WATCHPOINT:
		dataacc = 2;
		bfin_type = TYPE_DATA_WATCHPOINT;
		break;
	case BP_ACCESS_WATCHPOINT:
		dataacc = 3;
		bfin_type = TYPE_DATA_WATCHPOINT;
		break;
	default:
		return -ENOSPC;
	}

	/* Because hardware data watchpoint impelemented in current
	 * Blackfin can not trigger an exception event as the hardware
	 * instrction watchpoint does, we ignaore all data watch point here.
	 * They can be turned on easily after future blackfin design
	 * supports this feature.
	 */
	for (breakno = 0; breakno < HW_INST_WATCHPOINT_NUM; breakno++)
		if (bfin_type == breakinfo[breakno].type
			&& !breakinfo[breakno].occupied) {
			breakinfo[breakno].occupied = 1;
			breakinfo[breakno].skip = 0;
			breakinfo[breakno].enabled = 1;
			breakinfo[breakno].addr = addr;
			breakinfo[breakno].dataacc = dataacc;
			breakinfo[breakno].count = 0;
			return 0;
		}

	return -ENOSPC;
}

static int bfin_remove_hw_break(unsigned long addr, int len, enum kgdb_bptype type)
{
	int breakno;
	int bfin_type;

	switch (type) {
	case BP_HARDWARE_BREAKPOINT:
		bfin_type = TYPE_INST_WATCHPOINT;
		break;
	case BP_WRITE_WATCHPOINT:
	case BP_READ_WATCHPOINT:
	case BP_ACCESS_WATCHPOINT:
		bfin_type = TYPE_DATA_WATCHPOINT;
		break;
	default:
		return 0;
	}
	for (breakno = 0; breakno < HW_WATCHPOINT_NUM; breakno++)
		if (bfin_type == breakinfo[breakno].type
			&& breakinfo[breakno].occupied
			&& breakinfo[breakno].addr == addr) {
			breakinfo[breakno].occupied = 0;
			breakinfo[breakno].enabled = 0;
		}

	return 0;
}

static void bfin_remove_all_hw_break(void)
{
	int breakno;

	memset(breakinfo, 0, sizeof(struct hw_breakpoint)*HW_WATCHPOINT_NUM);

	for (breakno = 0; breakno < HW_INST_WATCHPOINT_NUM; breakno++)
		breakinfo[breakno].type = TYPE_INST_WATCHPOINT;
	for (; breakno < HW_WATCHPOINT_NUM; breakno++)
		breakinfo[breakno].type = TYPE_DATA_WATCHPOINT;
}

static void bfin_correct_hw_break(void)
{
	int breakno;
	unsigned int wpiactl = 0;
	unsigned int wpdactl = 0;
	int enable_wp = 0;

	for (breakno = 0; breakno < HW_WATCHPOINT_NUM; breakno++)
		if (breakinfo[breakno].enabled) {
			enable_wp = 1;

			switch (breakno) {
			case 0:
				wpiactl |= WPIAEN0|WPICNTEN0;
				bfin_write_WPIA0(breakinfo[breakno].addr);
				bfin_write_WPIACNT0(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 1:
				wpiactl |= WPIAEN1|WPICNTEN1;
				bfin_write_WPIA1(breakinfo[breakno].addr);
				bfin_write_WPIACNT1(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 2:
				wpiactl |= WPIAEN2|WPICNTEN2;
				bfin_write_WPIA2(breakinfo[breakno].addr);
				bfin_write_WPIACNT2(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 3:
				wpiactl |= WPIAEN3|WPICNTEN3;
				bfin_write_WPIA3(breakinfo[breakno].addr);
				bfin_write_WPIACNT3(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 4:
				wpiactl |= WPIAEN4|WPICNTEN4;
				bfin_write_WPIA4(breakinfo[breakno].addr);
				bfin_write_WPIACNT4(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 5:
				wpiactl |= WPIAEN5|WPICNTEN5;
				bfin_write_WPIA5(breakinfo[breakno].addr);
				bfin_write_WPIACNT5(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 6:
				wpdactl |= WPDAEN0|WPDCNTEN0|WPDSRC0;
				wpdactl |= breakinfo[breakno].dataacc
					<< WPDACC0_OFFSET;
				bfin_write_WPDA0(breakinfo[breakno].addr);
				bfin_write_WPDACNT0(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			case 7:
				wpdactl |= WPDAEN1|WPDCNTEN1|WPDSRC1;
				wpdactl |= breakinfo[breakno].dataacc
					<< WPDACC1_OFFSET;
				bfin_write_WPDA1(breakinfo[breakno].addr);
				bfin_write_WPDACNT1(breakinfo[breakno].count
					+ breakinfo->skip);
				break;
			}
		}

	/* Should enable WPPWR bit first before set any other
	 * WPIACTL and WPDACTL bits */
	if (enable_wp) {
		bfin_write_WPIACTL(WPPWR);
		CSYNC();
		bfin_write_WPIACTL(wpiactl|WPPWR);
		bfin_write_WPDACTL(wpdactl);
		CSYNC();
	}
}

static void bfin_disable_hw_debug(struct pt_regs *regs)
{
	/* Disable hardware debugging while we are in kgdb */
	bfin_write_WPIACTL(0);
	bfin_write_WPDACTL(0);
	CSYNC();
}

#ifdef CONFIG_SMP
void kgdb_passive_cpu_callback(void *info)
{
	kgdb_nmicallback(raw_smp_processor_id(), get_irq_regs());
}

void kgdb_roundup_cpus(unsigned long flags)
{
	unsigned int cpu;

	for (cpu = cpumask_first(cpu_online_mask); cpu < nr_cpu_ids;
		cpu = cpumask_next(cpu, cpu_online_mask))
		smp_call_function_single(cpu, kgdb_passive_cpu_callback,
					 NULL, 0);
}

void kgdb_roundup_cpu(int cpu, unsigned long flags)
{
	smp_call_function_single(cpu, kgdb_passive_cpu_callback, NULL, 0);
}
#endif

#ifdef CONFIG_IPIPE
static unsigned long kgdb_arch_imask;
#endif

void kgdb_post_primary_code(struct pt_regs *regs, int e_vector, int err_code)
{
	if (kgdb_single_step)
		preempt_enable();

#ifdef CONFIG_IPIPE
	if (kgdb_arch_imask) {
		cpu_pda[raw_smp_processor_id()].ex_imask = kgdb_arch_imask;
		kgdb_arch_imask = 0;
	}
#endif
}

int kgdb_arch_handle_exception(int vector, int signo,
			       int err_code, char *remcom_in_buffer,
			       char *remcom_out_buffer,
			       struct pt_regs *regs)
{
	long addr;
	char *ptr;
	int newPC;
	int i;

	switch (remcom_in_buffer[0]) {
	case 'c':
	case 's':
		if (kgdb_contthread && kgdb_contthread != current) {
			strcpy(remcom_out_buffer, "E00");
			break;
		}

		kgdb_contthread = NULL;

		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &addr)) {
			regs->retx = addr;
		}
		newPC = regs->retx;

		/* clear the trace bit */
		regs->syscfg &= 0xfffffffe;

		/* set the trace bit if we're stepping */
		if (remcom_in_buffer[0] == 's') {
			regs->syscfg |= 0x1;
			kgdb_single_step = regs->ipend;
			kgdb_single_step >>= 6;
			for (i = 10; i > 0; i--, kgdb_single_step >>= 1)
				if (kgdb_single_step & 1)
					break;
			/* i indicate event priority of current stopped instruction
			 * user space instruction is 0, IVG15 is 1, IVTMR is 10.
			 * kgdb_single_step > 0 means in single step mode
			 */
			kgdb_single_step = i + 1;

			preempt_disable();
#ifdef CONFIG_IPIPE
			kgdb_arch_imask = cpu_pda[raw_smp_processor_id()].ex_imask;
			cpu_pda[raw_smp_processor_id()].ex_imask = 0;
#endif
		}

		bfin_correct_hw_break();

		return 0;
	}			/* switch */
	return -1;		/* this means that we do not want to exit from the handler */
}

struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0xa1},
	.flags = KGDB_HW_BREAKPOINT,
	.set_hw_breakpoint = bfin_set_hw_break,
	.remove_hw_breakpoint = bfin_remove_hw_break,
	.disable_hw_break = bfin_disable_hw_debug,
	.remove_all_hw_break = bfin_remove_all_hw_break,
	.correct_hw_break = bfin_correct_hw_break,
};

#define IN_MEM(addr, size, l1_addr, l1_size) \
({ \
	unsigned long __addr = (unsigned long)(addr); \
	(l1_size && __addr >= l1_addr && __addr + (size) <= l1_addr + l1_size); \
})
#define ASYNC_BANK_SIZE \
	(ASYNC_BANK0_SIZE + ASYNC_BANK1_SIZE + \
	 ASYNC_BANK2_SIZE + ASYNC_BANK3_SIZE)

int kgdb_validate_break_address(unsigned long addr)
{
	int cpu = raw_smp_processor_id();

	if (addr >= 0x1000 && (addr + BREAK_INSTR_SIZE) <= physical_mem_end)
		return 0;
	if (IN_MEM(addr, BREAK_INSTR_SIZE, ASYNC_BANK0_BASE, ASYNC_BANK_SIZE))
		return 0;
	if (cpu == 0 && IN_MEM(addr, BREAK_INSTR_SIZE, L1_CODE_START, L1_CODE_LENGTH))
		return 0;
#ifdef CONFIG_SMP
	else if (cpu == 1 && IN_MEM(addr, BREAK_INSTR_SIZE, COREB_L1_CODE_START, L1_CODE_LENGTH))
		return 0;
#endif
	if (IN_MEM(addr, BREAK_INSTR_SIZE, L2_START, L2_LENGTH))
		return 0;

	return -EFAULT;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->retx = ip;
}

int kgdb_arch_init(void)
{
	kgdb_single_step = 0;
#ifdef CONFIG_IPIPE
	kgdb_arch_imask = 0;
#endif

	bfin_remove_all_hw_break();
	return 0;
}

void kgdb_arch_exit(void)
{
}

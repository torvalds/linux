/*
 * arch/blackfin/kernel/kgdb.c - Blackfin kgdb pieces
 *
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ptrace.h>		/* for linux pt_regs struct */
#include <linux/kgdb.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/blackfin.h>
#include <asm/dma.h>

/* Put the error code here just in case the user cares.  */
int gdb_bfin_errcode;
/* Likewise, the vector number here (since GDB only gets the signal
   number through the usual means, and that's not very specific).  */
int gdb_bfin_vector = -1;

#if KGDB_MAX_NO_CPUS != 8
#error change the definition of slavecpulocks
#endif

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
	gdb_regs[BFIN_CC] = 0;
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
	regs->retx = gdb_regs[BFIN_PC];
	regs->retn = gdb_regs[BFIN_RETN];
	regs->rete = gdb_regs[BFIN_RETE];
	regs->pc = gdb_regs[BFIN_PC];

#if 0				/* can't change these */
	regs->astat = gdb_regs[BFIN_ASTAT];
	regs->seqstat = gdb_regs[BFIN_SEQSTAT];
	regs->ipend = gdb_regs[BFIN_IPEND];
#endif
}

struct hw_breakpoint {
	unsigned int occupied:1;
	unsigned int skip:1;
	unsigned int enabled:1;
	unsigned int type:1;
	unsigned int dataacc:2;
	unsigned short count;
	unsigned int addr;
} breakinfo[HW_WATCHPOINT_NUM];

int bfin_set_hw_break(unsigned long addr, int len, enum kgdb_bptype type)
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

	/* Becasue hardware data watchpoint impelemented in current
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

int bfin_remove_hw_break(unsigned long addr, int len, enum kgdb_bptype type)
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

void bfin_remove_all_hw_break(void)
{
	int breakno;

	memset(breakinfo, 0, sizeof(struct hw_breakpoint)*HW_WATCHPOINT_NUM);

	for (breakno = 0; breakno < HW_INST_WATCHPOINT_NUM; breakno++)
		breakinfo[breakno].type = TYPE_INST_WATCHPOINT;
	for (; breakno < HW_WATCHPOINT_NUM; breakno++)
		breakinfo[breakno].type = TYPE_DATA_WATCHPOINT;
}

void bfin_correct_hw_break(void)
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

void kgdb_disable_hw_debug(struct pt_regs *regs)
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
	smp_call_function(kgdb_passive_cpu_callback, NULL, 0);
}

void kgdb_roundup_cpu(int cpu, unsigned long flags)
{
	smp_call_function_single(cpu, kgdb_passive_cpu_callback, NULL, 0);
}
#endif

void kgdb_post_primary_code(struct pt_regs *regs, int eVector, int err_code)
{
	/* Master processor is completely in the debugger */
	gdb_bfin_vector = eVector;
	gdb_bfin_errcode = err_code;
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
		}

		bfin_correct_hw_break();

		return 0;
	}			/* switch */
	return -1;		/* this means that we do not want to exit from the handler */
}

struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0xa1},
#ifdef CONFIG_SMP
	.flags = KGDB_HW_BREAKPOINT|KGDB_THR_PROC_SWAP,
#else
	.flags = KGDB_HW_BREAKPOINT,
#endif
	.set_hw_breakpoint = bfin_set_hw_break,
	.remove_hw_breakpoint = bfin_remove_hw_break,
	.remove_all_hw_break = bfin_remove_all_hw_break,
	.correct_hw_break = bfin_correct_hw_break,
};

static int hex(char ch)
{
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	return -1;
}

static int validate_memory_access_address(unsigned long addr, int size)
{
	if (size < 0 || addr == 0)
		return -EFAULT;
	return bfin_mem_access_type(addr, size);
}

static int bfin_probe_kernel_read(char *dst, char *src, int size)
{
	unsigned long lsrc = (unsigned long)src;
	int mem_type;

	mem_type = validate_memory_access_address(lsrc, size);
	if (mem_type < 0)
		return mem_type;

	if (lsrc >= SYSMMR_BASE) {
		if (size == 2 && lsrc % 2 == 0) {
			u16 mmr = bfin_read16(src);
			memcpy(dst, &mmr, sizeof(mmr));
			return 0;
		} else if (size == 4 && lsrc % 4 == 0) {
			u32 mmr = bfin_read32(src);
			memcpy(dst, &mmr, sizeof(mmr));
			return 0;
		}
	} else {
		switch (mem_type) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				return probe_kernel_read(dst, src, size);
			/* XXX: should support IDMA here with SMP */
			case BFIN_MEM_ACCESS_DMA:
				if (dma_memcpy(dst, src, size))
					return 0;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(dst, src, size))
					return 0;
				break;
		}
	}

	return -EFAULT;
}

static int bfin_probe_kernel_write(char *dst, char *src, int size)
{
	unsigned long ldst = (unsigned long)dst;
	int mem_type;

	mem_type = validate_memory_access_address(ldst, size);
	if (mem_type < 0)
		return mem_type;

	if (ldst >= SYSMMR_BASE) {
		if (size == 2 && ldst % 2 == 0) {
			u16 mmr;
			memcpy(&mmr, src, sizeof(mmr));
			bfin_write16(dst, mmr);
			return 0;
		} else if (size == 4 && ldst % 4 == 0) {
			u32 mmr;
			memcpy(&mmr, src, sizeof(mmr));
			bfin_write32(dst, mmr);
			return 0;
		}
	} else {
		switch (mem_type) {
			case BFIN_MEM_ACCESS_CORE:
			case BFIN_MEM_ACCESS_CORE_ONLY:
				return probe_kernel_write(dst, src, size);
			/* XXX: should support IDMA here with SMP */
			case BFIN_MEM_ACCESS_DMA:
				if (dma_memcpy(dst, src, size))
					return 0;
				break;
			case BFIN_MEM_ACCESS_ITEST:
				if (isram_memcpy(dst, src, size))
					return 0;
				break;
		}
	}

	return -EFAULT;
}

/*
 * Convert the memory pointed to by mem into hex, placing result in buf.
 * Return a pointer to the last char put in buf (null). May return an error.
 */
int kgdb_mem2hex(char *mem, char *buf, int count)
{
	char *tmp;
	int err;

	/*
	 * We use the upper half of buf as an intermediate buffer for the
	 * raw memory copy.  Hex conversion will work against this one.
	 */
	tmp = buf + count;

	err = bfin_probe_kernel_read(tmp, mem, count);
	if (!err) {
		while (count > 0) {
			buf = pack_hex_byte(buf, *tmp);
			tmp++;
			count--;
		}

		*buf = 0;
	}

	return err;
}

/*
 * Copy the binary array pointed to by buf into mem.  Fix $, #, and
 * 0x7d escaped with 0x7d.  Return a pointer to the character after
 * the last byte written.
 */
int kgdb_ebin2mem(char *buf, char *mem, int count)
{
	char *tmp_old, *tmp_new;
	int size;

	tmp_old = tmp_new = buf;

	for (size = 0; size < count; ++size) {
		if (*tmp_old == 0x7d)
			*tmp_new = *(++tmp_old) ^ 0x20;
		else
			*tmp_new = *tmp_old;
		tmp_new++;
		tmp_old++;
	}

	return bfin_probe_kernel_write(mem, buf, count);
}

/*
 * Convert the hex array pointed to by buf into binary to be placed in mem.
 * Return a pointer to the character AFTER the last byte written.
 * May return an error.
 */
int kgdb_hex2mem(char *buf, char *mem, int count)
{
	char *tmp_raw, *tmp_hex;

	/*
	 * We use the upper half of buf as an intermediate buffer for the
	 * raw memory that is converted from hex.
	 */
	tmp_raw = buf + count * 2;

	tmp_hex = tmp_raw - 1;
	while (tmp_hex >= buf) {
		tmp_raw--;
		*tmp_raw = hex(*tmp_hex--);
		*tmp_raw |= hex(*tmp_hex--) << 4;
	}

	return bfin_probe_kernel_write(mem, tmp_raw, count);
}

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

int kgdb_arch_set_breakpoint(unsigned long addr, char *saved_instr)
{
	int err = bfin_probe_kernel_read(saved_instr, (char *)addr,
	                                 BREAK_INSTR_SIZE);
	if (err)
		return err;
	return bfin_probe_kernel_write((char *)addr, arch_kgdb_ops.gdb_bpt_instr,
	                               BREAK_INSTR_SIZE);
}

int kgdb_arch_remove_breakpoint(unsigned long addr, char *bundle)
{
	return bfin_probe_kernel_write((char *)addr, bundle, BREAK_INSTR_SIZE);
}

int kgdb_arch_init(void)
{
	kgdb_single_step = 0;

	bfin_remove_all_hw_break();
	return 0;
}

void kgdb_arch_exit(void)
{
}

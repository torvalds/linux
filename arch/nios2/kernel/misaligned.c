/*
 *  linux/arch/nios2/kernel/misaligned.c
 *
 *  basic emulation for mis-aligned accesses on the NIOS II cpu
 *  modelled after the version for arm in arm/alignment.c
 *
 *  Brad Parker <brad@heeltoe.com>
 *  Copyright (C) 2010 Ambient Corporation
 *  Copyright (c) 2010 Altera Corporation, San Jose, California, USA.
 *  Copyright (c) 2010 Arrow Electronics, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include <asm/traps.h>
#include <linux/unaligned.h>

/* instructions we emulate */
#define INST_LDHU	0x0b
#define INST_STH	0x0d
#define INST_LDH	0x0f
#define INST_STW	0x15
#define INST_LDW	0x17

static unsigned int ma_usermode;
#define UM_WARN		0x01
#define UM_FIXUP	0x02
#define UM_SIGNAL	0x04
#define KM_WARN		0x08

/* see arch/nios2/include/asm/ptrace.h */
static u8 sys_stack_frame_reg_offset[] = {
	/* struct pt_regs */
	8, 9, 10, 11, 12, 13, 14, 15, 1, 2, 3, 4, 5, 6, 7, 0,
	/* struct switch_stack */
	16, 17, 18, 19, 20, 21, 22, 23, 0, 0, 0, 0, 0, 0, 0, 0
};

static int reg_offsets[32];

static inline u32 get_reg_val(struct pt_regs *fp, int reg)
{
	u8 *p = ((u8 *)fp) + reg_offsets[reg];
	return *(u32 *)p;
}

static inline void put_reg_val(struct pt_regs *fp, int reg, u32 val)
{
	u8 *p = ((u8 *)fp) + reg_offsets[reg];
	*(u32 *)p = val;
}

/*
 * (mis)alignment handler
 */
asmlinkage void handle_unaligned_c(struct pt_regs *fp, int cause)
{
	u32 isn, addr, val;
	int in_kernel;
	u8 a, b, d0, d1, d2, d3;
	s16 imm16;
	unsigned int fault;

	/* back up one instruction */
	fp->ea -= 4;

	if (fixup_exception(fp)) {
		return;
	}

	in_kernel = !user_mode(fp);

	isn = *(unsigned long *)(fp->ea);

	fault = 0;

	/* do fixup if in kernel or mode turned on */
	if (in_kernel || (ma_usermode & UM_FIXUP)) {
		/* decompose instruction */
		a = (isn >> 27) & 0x1f;
		b = (isn >> 22) & 0x1f;
		imm16 = (isn >> 6) & 0xffff;
		addr = get_reg_val(fp, a) + imm16;

		/* do fixup to saved registers */
		switch (isn & 0x3f) {
		case INST_LDHU:
			fault |= __get_user(d0, (u8 *)(addr+0));
			fault |= __get_user(d1, (u8 *)(addr+1));
			val = (d1 << 8) | d0;
			put_reg_val(fp, b, val);
			break;
		case INST_STH:
			val = get_reg_val(fp, b);
			d1 = val >> 8;
			d0 = val >> 0;
			if (in_kernel) {
				*(u8 *)(addr+0) = d0;
				*(u8 *)(addr+1) = d1;
			} else {
				fault |= __put_user(d0, (u8 *)(addr+0));
				fault |= __put_user(d1, (u8 *)(addr+1));
			}
			break;
		case INST_LDH:
			fault |= __get_user(d0, (u8 *)(addr+0));
			fault |= __get_user(d1, (u8 *)(addr+1));
			val = (short)((d1 << 8) | d0);
			put_reg_val(fp, b, val);
			break;
		case INST_STW:
			val = get_reg_val(fp, b);
			d3 = val >> 24;
			d2 = val >> 16;
			d1 = val >> 8;
			d0 = val >> 0;
			if (in_kernel) {
				*(u8 *)(addr+0) = d0;
				*(u8 *)(addr+1) = d1;
				*(u8 *)(addr+2) = d2;
				*(u8 *)(addr+3) = d3;
			} else {
				fault |= __put_user(d0, (u8 *)(addr+0));
				fault |= __put_user(d1, (u8 *)(addr+1));
				fault |= __put_user(d2, (u8 *)(addr+2));
				fault |= __put_user(d3, (u8 *)(addr+3));
			}
			break;
		case INST_LDW:
			fault |= __get_user(d0, (u8 *)(addr+0));
			fault |= __get_user(d1, (u8 *)(addr+1));
			fault |= __get_user(d2, (u8 *)(addr+2));
			fault |= __get_user(d3, (u8 *)(addr+3));
			val = (d3 << 24) | (d2 << 16) | (d1 << 8) | d0;
			put_reg_val(fp, b, val);
			break;
		}
	}

	addr = RDCTL(CTL_BADADDR);
	cause >>= 2;

	if (fault) {
		if (in_kernel) {
			pr_err("fault during kernel misaligned fixup @ %#lx; addr 0x%08x; isn=0x%08x\n",
				fp->ea, (unsigned int)addr,
				(unsigned int)isn);
		} else {
			pr_err("fault during user misaligned fixup @ %#lx; isn=%08x addr=0x%08x sp=0x%08lx pid=%d\n",
				fp->ea,
				(unsigned int)isn, addr, fp->sp,
				current->pid);

			_exception(SIGSEGV, fp, SEGV_MAPERR, fp->ea);
			return;
		}
	}

	/*
	 * kernel mode -
	 *  note exception and skip bad instruction (return)
	 */
	if (in_kernel) {
		fp->ea += 4;

		if (ma_usermode & KM_WARN) {
			pr_err("kernel unaligned access @ %#lx; BADADDR 0x%08x; cause=%d, isn=0x%08x\n",
				fp->ea,
				(unsigned int)addr, cause,
				(unsigned int)isn);
			/* show_regs(fp); */
		}

		return;
	}

	/*
	 * user mode -
	 *  possibly warn,
	 *  possibly send SIGBUS signal to process
	 */
	if (ma_usermode & UM_WARN) {
		pr_err("user unaligned access @ %#lx; isn=0x%08lx ea=0x%08lx ra=0x%08lx sp=0x%08lx\n",
			(unsigned long)addr, (unsigned long)isn,
			fp->ea, fp->ra, fp->sp);
	}

	if (ma_usermode & UM_SIGNAL)
		_exception(SIGBUS, fp, BUS_ADRALN, fp->ea);
	else
		fp->ea += 4;	/* else advance */
}

static void __init misaligned_calc_reg_offsets(void)
{
	int i, r, offset;

	/* pre-calc offsets of registers on sys call stack frame */
	offset = 0;

	/* struct pt_regs */
	for (i = 0; i < 16; i++) {
		r = sys_stack_frame_reg_offset[i];
		reg_offsets[r] = offset;
		offset += 4;
	}

	/* struct switch_stack */
	offset = -sizeof(struct switch_stack);
	for (i = 16; i < 32; i++) {
		r = sys_stack_frame_reg_offset[i];
		reg_offsets[r] = offset;
		offset += 4;
	}
}


static int __init misaligned_init(void)
{
	/* default mode - silent fix */
	ma_usermode = UM_FIXUP | KM_WARN;

	misaligned_calc_reg_offsets();

	return 0;
}

fs_initcall(misaligned_init);

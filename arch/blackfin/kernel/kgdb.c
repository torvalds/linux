/*
 * File:         arch/blackfin/kernel/kgdb.c
 * Based on:
 * Author:       Sonic Zhang
 *
 * Created:
 * Description:
 *
 * Rev:          $Id: kgdb_bfin_linux-2.6.x.patch 4934 2007-02-13 09:32:11Z sonicz $
 *
 * Modified:
 *               Copyright 2005-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <linux/debugger.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/blackfin.h>

/* Put the error code here just in case the user cares.  */
int gdb_bf533errcode;
/* Likewise, the vector number here (since GDB only gets the signal
   number through the usual means, and that's not very specific).  */
int gdb_bf533vector = -1;

#if KGDB_MAX_NO_CPUS != 8
#error change the definition of slavecpulocks
#endif

void regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
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
 * prior to entering switch_to is 8 greater then the value that is saved.
 * If switch_to changes, change following code appropriately.
 */
void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	gdb_regs[BFIN_SP] = p->thread.ksp;
	gdb_regs[BFIN_PC] = p->thread.pc;
	gdb_regs[BFIN_SEQSTAT] = p->thread.seqstat;
}

void gdb_regs_to_regs(unsigned long *gdb_regs, struct pt_regs *regs)
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
} breakinfo[HW_BREAKPOINT_NUM];

int kgdb_arch_init(void)
{
	debugger_step = 0;

	kgdb_remove_all_hw_break();
	return 0;
}

int kgdb_set_hw_break(unsigned long addr)
{
	int breakno;
	for (breakno = 0; breakno < HW_BREAKPOINT_NUM; breakno++)
		if (!breakinfo[breakno].occupied) {
			breakinfo[breakno].occupied = 1;
			breakinfo[breakno].enabled = 1;
			breakinfo[breakno].type = 1;
			breakinfo[breakno].addr = addr;
			return 0;
		}

	return -ENOSPC;
}

int kgdb_remove_hw_break(unsigned long addr)
{
	int breakno;
	for (breakno = 0; breakno < HW_BREAKPOINT_NUM; breakno++)
		if (breakinfo[breakno].addr == addr)
			memset(&(breakinfo[breakno]), 0, sizeof(struct hw_breakpoint));

	return 0;
}

void kgdb_remove_all_hw_break(void)
{
	memset(breakinfo, 0, sizeof(struct hw_breakpoint)*8);
}

/*
void kgdb_show_info(void)
{
	printk(KERN_DEBUG "hwd: wpia0=0x%x, wpiacnt0=%d, wpiactl=0x%x, wpstat=0x%x\n",
		bfin_read_WPIA0(), bfin_read_WPIACNT0(),
		bfin_read_WPIACTL(), bfin_read_WPSTAT());
}
*/

void kgdb_correct_hw_break(void)
{
	int breakno;
	int correctit;
	uint32_t wpdactl = bfin_read_WPDACTL();

	correctit = 0;
	for (breakno = 0; breakno < HW_BREAKPOINT_NUM; breakno++) {
		if (breakinfo[breakno].type == 1) {
			switch (breakno) {
			case 0:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN0)) {
					correctit = 1;
					wpdactl &= ~(WPIREN01|EMUSW0);
					wpdactl |= WPIAEN0|WPICNTEN0;
					bfin_write_WPIA0(breakinfo[breakno].addr);
					bfin_write_WPIACNT0(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN0)) {
					correctit = 1;
					wpdactl &= ~WPIAEN0;
				}
				break;

			case 1:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN1)) {
					correctit = 1;
					wpdactl &= ~(WPIREN01|EMUSW1);
					wpdactl |= WPIAEN1|WPICNTEN1;
					bfin_write_WPIA1(breakinfo[breakno].addr);
					bfin_write_WPIACNT1(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN1)) {
					correctit = 1;
					wpdactl &= ~WPIAEN1;
				}
				break;

			case 2:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN2)) {
					correctit = 1;
					wpdactl &= ~(WPIREN23|EMUSW2);
					wpdactl |= WPIAEN2|WPICNTEN2;
					bfin_write_WPIA2(breakinfo[breakno].addr);
					bfin_write_WPIACNT2(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN2)) {
					correctit = 1;
					wpdactl &= ~WPIAEN2;
				}
				break;

			case 3:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN3)) {
					correctit = 1;
					wpdactl &= ~(WPIREN23|EMUSW3);
					wpdactl |= WPIAEN3|WPICNTEN3;
					bfin_write_WPIA3(breakinfo[breakno].addr);
					bfin_write_WPIACNT3(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN3)) {
					correctit = 1;
					wpdactl &= ~WPIAEN3;
				}
				break;
			case 4:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN4)) {
					correctit = 1;
					wpdactl &= ~(WPIREN45|EMUSW4);
					wpdactl |= WPIAEN4|WPICNTEN4;
					bfin_write_WPIA4(breakinfo[breakno].addr);
					bfin_write_WPIACNT4(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN4)) {
					correctit = 1;
					wpdactl &= ~WPIAEN4;
				}
				break;
			case 5:
				if (breakinfo[breakno].enabled && !(wpdactl & WPIAEN5)) {
					correctit = 1;
					wpdactl &= ~(WPIREN45|EMUSW5);
					wpdactl |= WPIAEN5|WPICNTEN5;
					bfin_write_WPIA5(breakinfo[breakno].addr);
					bfin_write_WPIACNT5(breakinfo[breakno].skip);
				} else if (!breakinfo[breakno].enabled && (wpdactl & WPIAEN5)) {
					correctit = 1;
					wpdactl &= ~WPIAEN5;
				}
				break;
			}
		}
	}
	if (correctit) {
		wpdactl &= ~WPAND;
		wpdactl |= WPPWR;
		/*printk("correct_hw_break: wpdactl=0x%x\n", wpdactl);*/
		bfin_write_WPDACTL(wpdactl);
		CSYNC();
		/*kgdb_show_info();*/
	}
}

void kgdb_disable_hw_debug(struct pt_regs *regs)
{
	/* Disable hardware debugging while we are in kgdb */
	bfin_write_WPIACTL(bfin_read_WPIACTL() & ~0x1);
	CSYNC();
}

void kgdb_post_master_code(struct pt_regs *regs, int eVector, int err_code)
{
	/* Master processor is completely in the debugger */
	gdb_bf533vector = eVector;
	gdb_bf533errcode = err_code;
}

int kgdb_arch_handle_exception(int exceptionVector, int signo,
			       int err_code, char *remcom_in_buffer,
			       char *remcom_out_buffer,
			       struct pt_regs *linux_regs)
{
	long addr;
	long breakno;
	char *ptr;
	int newPC;
	int wp_status;
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
			linux_regs->retx = addr;
		}
		newPC = linux_regs->retx;

		/* clear the trace bit */
		linux_regs->syscfg &= 0xfffffffe;

		/* set the trace bit if we're stepping */
		if (remcom_in_buffer[0] == 's') {
			linux_regs->syscfg |= 0x1;
			debugger_step = linux_regs->ipend;
			debugger_step >>= 6;
			for (i = 10; i > 0; i--, debugger_step >>= 1)
				if (debugger_step & 1)
					break;
			/* i indicate event priority of current stopped instruction
			 * user space instruction is 0, IVG15 is 1, IVTMR is 10.
			 * debugger_step > 0 means in single step mode
			 */
			debugger_step = i + 1;
		} else {
			debugger_step = 0;
		}

		wp_status = bfin_read_WPSTAT();
		CSYNC();

		if (exceptionVector == VEC_WATCH) {
			for (breakno = 0; breakno < 6; ++breakno) {
				if (wp_status & (1 << breakno)) {
					breakinfo->skip = 1;
					break;
				}
			}
		}
		kgdb_correct_hw_break();

		bfin_write_WPSTAT(0);

		return 0;
	}			/* switch */
	return -1;		/* this means that we do not want to exit from the handler */
}

struct kgdb_arch arch_kgdb_ops = {
	.gdb_bpt_instr = {0xa1},
	.flags = KGDB_HW_BREAKPOINT,
};

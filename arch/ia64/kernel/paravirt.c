/******************************************************************************
 * arch/ia64/kernel/paravirt.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *     Yaozu (Eddie) Dong <eddie.dong@intel.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>

#include <linux/compiler.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/iosapic.h>
#include <asm/paravirt.h>

/***************************************************************************
 * general info
 */
struct pv_info pv_info = {
	.kernel_rpl = 0,
	.paravirt_enabled = 0,
	.name = "bare hardware"
};

/***************************************************************************
 * pv_init_ops
 * initialization hooks.
 */

struct pv_init_ops pv_init_ops;

/***************************************************************************
 * pv_cpu_ops
 * intrinsics hooks.
 */

/* ia64_native_xxx are macros so that we have to make them real functions */

#define DEFINE_VOID_FUNC1(name)					\
	static void						\
	ia64_native_ ## name ## _func(unsigned long arg)	\
	{							\
		ia64_native_ ## name(arg);			\
	}							\

#define DEFINE_VOID_FUNC2(name)					\
	static void						\
	ia64_native_ ## name ## _func(unsigned long arg0,	\
				      unsigned long arg1)	\
	{							\
		ia64_native_ ## name(arg0, arg1);		\
	}							\

#define DEFINE_FUNC0(name)			\
	static unsigned long			\
	ia64_native_ ## name ## _func(void)	\
	{					\
		return ia64_native_ ## name();	\
	}

#define DEFINE_FUNC1(name, type)			\
	static unsigned long				\
	ia64_native_ ## name ## _func(type arg)		\
	{						\
		return ia64_native_ ## name(arg);	\
	}						\

DEFINE_VOID_FUNC1(fc);
DEFINE_VOID_FUNC1(intrin_local_irq_restore);

DEFINE_VOID_FUNC2(ptcga);
DEFINE_VOID_FUNC2(set_rr);

DEFINE_FUNC0(get_psr_i);

DEFINE_FUNC1(thash, unsigned long);
DEFINE_FUNC1(get_cpuid, int);
DEFINE_FUNC1(get_pmd, int);
DEFINE_FUNC1(get_rr, unsigned long);

static void
ia64_native_ssm_i_func(void)
{
	ia64_native_ssm(IA64_PSR_I);
}

static void
ia64_native_rsm_i_func(void)
{
	ia64_native_rsm(IA64_PSR_I);
}

static void
ia64_native_set_rr0_to_rr4_func(unsigned long val0, unsigned long val1,
				unsigned long val2, unsigned long val3,
				unsigned long val4)
{
	ia64_native_set_rr0_to_rr4(val0, val1, val2, val3, val4);
}

#define CASE_GET_REG(id)				\
	case _IA64_REG_ ## id:				\
	res = ia64_native_getreg(_IA64_REG_ ## id);	\
	break;
#define CASE_GET_AR(id) CASE_GET_REG(AR_ ## id)
#define CASE_GET_CR(id) CASE_GET_REG(CR_ ## id)

unsigned long
ia64_native_getreg_func(int regnum)
{
	unsigned long res = -1;
	switch (regnum) {
	CASE_GET_REG(GP);
	/*CASE_GET_REG(IP);*/ /* returned ip value shouldn't be constant */
	CASE_GET_REG(PSR);
	CASE_GET_REG(TP);
	CASE_GET_REG(SP);

	CASE_GET_AR(KR0);
	CASE_GET_AR(KR1);
	CASE_GET_AR(KR2);
	CASE_GET_AR(KR3);
	CASE_GET_AR(KR4);
	CASE_GET_AR(KR5);
	CASE_GET_AR(KR6);
	CASE_GET_AR(KR7);
	CASE_GET_AR(RSC);
	CASE_GET_AR(BSP);
	CASE_GET_AR(BSPSTORE);
	CASE_GET_AR(RNAT);
	CASE_GET_AR(FCR);
	CASE_GET_AR(EFLAG);
	CASE_GET_AR(CSD);
	CASE_GET_AR(SSD);
	CASE_GET_AR(CFLAG);
	CASE_GET_AR(FSR);
	CASE_GET_AR(FIR);
	CASE_GET_AR(FDR);
	CASE_GET_AR(CCV);
	CASE_GET_AR(UNAT);
	CASE_GET_AR(FPSR);
	CASE_GET_AR(ITC);
	CASE_GET_AR(PFS);
	CASE_GET_AR(LC);
	CASE_GET_AR(EC);

	CASE_GET_CR(DCR);
	CASE_GET_CR(ITM);
	CASE_GET_CR(IVA);
	CASE_GET_CR(PTA);
	CASE_GET_CR(IPSR);
	CASE_GET_CR(ISR);
	CASE_GET_CR(IIP);
	CASE_GET_CR(IFA);
	CASE_GET_CR(ITIR);
	CASE_GET_CR(IIPA);
	CASE_GET_CR(IFS);
	CASE_GET_CR(IIM);
	CASE_GET_CR(IHA);
	CASE_GET_CR(LID);
	CASE_GET_CR(IVR);
	CASE_GET_CR(TPR);
	CASE_GET_CR(EOI);
	CASE_GET_CR(IRR0);
	CASE_GET_CR(IRR1);
	CASE_GET_CR(IRR2);
	CASE_GET_CR(IRR3);
	CASE_GET_CR(ITV);
	CASE_GET_CR(PMV);
	CASE_GET_CR(CMCV);
	CASE_GET_CR(LRR0);
	CASE_GET_CR(LRR1);

	default:
		printk(KERN_CRIT "wrong_getreg %d\n", regnum);
		break;
	}
	return res;
}

#define CASE_SET_REG(id)				\
	case _IA64_REG_ ## id:				\
	ia64_native_setreg(_IA64_REG_ ## id, val);	\
	break;
#define CASE_SET_AR(id) CASE_SET_REG(AR_ ## id)
#define CASE_SET_CR(id) CASE_SET_REG(CR_ ## id)

void
ia64_native_setreg_func(int regnum, unsigned long val)
{
	switch (regnum) {
	case _IA64_REG_PSR_L:
		ia64_native_setreg(_IA64_REG_PSR_L, val);
		ia64_dv_serialize_data();
		break;
	CASE_SET_REG(SP);
	CASE_SET_REG(GP);

	CASE_SET_AR(KR0);
	CASE_SET_AR(KR1);
	CASE_SET_AR(KR2);
	CASE_SET_AR(KR3);
	CASE_SET_AR(KR4);
	CASE_SET_AR(KR5);
	CASE_SET_AR(KR6);
	CASE_SET_AR(KR7);
	CASE_SET_AR(RSC);
	CASE_SET_AR(BSP);
	CASE_SET_AR(BSPSTORE);
	CASE_SET_AR(RNAT);
	CASE_SET_AR(FCR);
	CASE_SET_AR(EFLAG);
	CASE_SET_AR(CSD);
	CASE_SET_AR(SSD);
	CASE_SET_AR(CFLAG);
	CASE_SET_AR(FSR);
	CASE_SET_AR(FIR);
	CASE_SET_AR(FDR);
	CASE_SET_AR(CCV);
	CASE_SET_AR(UNAT);
	CASE_SET_AR(FPSR);
	CASE_SET_AR(ITC);
	CASE_SET_AR(PFS);
	CASE_SET_AR(LC);
	CASE_SET_AR(EC);

	CASE_SET_CR(DCR);
	CASE_SET_CR(ITM);
	CASE_SET_CR(IVA);
	CASE_SET_CR(PTA);
	CASE_SET_CR(IPSR);
	CASE_SET_CR(ISR);
	CASE_SET_CR(IIP);
	CASE_SET_CR(IFA);
	CASE_SET_CR(ITIR);
	CASE_SET_CR(IIPA);
	CASE_SET_CR(IFS);
	CASE_SET_CR(IIM);
	CASE_SET_CR(IHA);
	CASE_SET_CR(LID);
	CASE_SET_CR(IVR);
	CASE_SET_CR(TPR);
	CASE_SET_CR(EOI);
	CASE_SET_CR(IRR0);
	CASE_SET_CR(IRR1);
	CASE_SET_CR(IRR2);
	CASE_SET_CR(IRR3);
	CASE_SET_CR(ITV);
	CASE_SET_CR(PMV);
	CASE_SET_CR(CMCV);
	CASE_SET_CR(LRR0);
	CASE_SET_CR(LRR1);
	default:
		printk(KERN_CRIT "wrong setreg %d\n", regnum);
		break;
	}
}

struct pv_cpu_ops pv_cpu_ops = {
	.fc		= ia64_native_fc_func,
	.thash		= ia64_native_thash_func,
	.get_cpuid	= ia64_native_get_cpuid_func,
	.get_pmd	= ia64_native_get_pmd_func,
	.ptcga		= ia64_native_ptcga_func,
	.get_rr		= ia64_native_get_rr_func,
	.set_rr		= ia64_native_set_rr_func,
	.set_rr0_to_rr4	= ia64_native_set_rr0_to_rr4_func,
	.ssm_i		= ia64_native_ssm_i_func,
	.getreg		= ia64_native_getreg_func,
	.setreg		= ia64_native_setreg_func,
	.rsm_i		= ia64_native_rsm_i_func,
	.get_psr_i	= ia64_native_get_psr_i_func,
	.intrin_local_irq_restore
			= ia64_native_intrin_local_irq_restore_func,
};
EXPORT_SYMBOL(pv_cpu_ops);

/******************************************************************************
 * replacement of hand written assembly codes.
 */

void
paravirt_cpu_asm_init(const struct pv_cpu_asm_switch *cpu_asm_switch)
{
	extern unsigned long paravirt_switch_to_targ;
	extern unsigned long paravirt_leave_syscall_targ;
	extern unsigned long paravirt_work_processed_syscall_targ;
	extern unsigned long paravirt_leave_kernel_targ;

	paravirt_switch_to_targ = cpu_asm_switch->switch_to;
	paravirt_leave_syscall_targ = cpu_asm_switch->leave_syscall;
	paravirt_work_processed_syscall_targ =
		cpu_asm_switch->work_processed_syscall;
	paravirt_leave_kernel_targ = cpu_asm_switch->leave_kernel;
}

/***************************************************************************
 * pv_iosapic_ops
 * iosapic read/write hooks.
 */

static unsigned int
ia64_native_iosapic_read(char __iomem *iosapic, unsigned int reg)
{
	return __ia64_native_iosapic_read(iosapic, reg);
}

static void
ia64_native_iosapic_write(char __iomem *iosapic, unsigned int reg, u32 val)
{
	__ia64_native_iosapic_write(iosapic, reg, val);
}

struct pv_iosapic_ops pv_iosapic_ops = {
	.pcat_compat_init = ia64_native_iosapic_pcat_compat_init,
	.__get_irq_chip = ia64_native_iosapic_get_irq_chip,

	.__read = ia64_native_iosapic_read,
	.__write = ia64_native_iosapic_write,
};

/***************************************************************************
 * pv_irq_ops
 * irq operations
 */

struct pv_irq_ops pv_irq_ops = {
	.register_ipi = ia64_native_register_ipi,

	.assign_irq_vector = ia64_native_assign_irq_vector,
	.free_irq_vector = ia64_native_free_irq_vector,
	.register_percpu_irq = ia64_native_register_percpu_irq,

	.resend_irq = ia64_native_resend_irq,
};

/***************************************************************************
 * pv_time_ops
 * time operations
 */

static int
ia64_native_do_steal_accounting(unsigned long *new_itm)
{
	return 0;
}

struct pv_time_ops pv_time_ops = {
	.do_steal_accounting = ia64_native_do_steal_accounting,
};

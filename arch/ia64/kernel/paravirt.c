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

static void __init
ia64_native_patch_branch(unsigned long tag, unsigned long type);

struct pv_init_ops pv_init_ops =
{
#ifdef ASM_SUPPORTED
	.patch_bundle = ia64_native_patch_bundle,
#endif
	.patch_branch = ia64_native_patch_branch,
};

/***************************************************************************
 * pv_cpu_ops
 * intrinsics hooks.
 */

#ifndef ASM_SUPPORTED
/* ia64_native_xxx are macros so that we have to make them real functions */

#define DEFINE_VOID_FUNC1(name)					\
	static void						\
	ia64_native_ ## name ## _func(unsigned long arg)	\
	{							\
		ia64_native_ ## name(arg);			\
	}

#define DEFINE_VOID_FUNC1_VOID(name)				\
	static void						\
	ia64_native_ ## name ## _func(void *arg)		\
	{							\
		ia64_native_ ## name(arg);			\
	}

#define DEFINE_VOID_FUNC2(name)					\
	static void						\
	ia64_native_ ## name ## _func(unsigned long arg0,	\
				      unsigned long arg1)	\
	{							\
		ia64_native_ ## name(arg0, arg1);		\
	}

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

DEFINE_VOID_FUNC1_VOID(fc);
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
#else

#define __DEFINE_FUNC(name, code)					\
	extern const char ia64_native_ ## name ## _direct_start[];	\
	extern const char ia64_native_ ## name ## _direct_end[];	\
	asm (".align 32\n"						\
	     ".proc ia64_native_" #name "_func\n"			\
	     "ia64_native_" #name "_func:\n"				\
	     "ia64_native_" #name "_direct_start:\n"			\
	     code							\
	     "ia64_native_" #name "_direct_end:\n"			\
	     "br.cond.sptk.many b6\n"					\
	     ".endp ia64_native_" #name "_func\n")

#define DEFINE_VOID_FUNC0(name, code)				\
	extern void						\
	ia64_native_ ## name ## _func(void);			\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC1(name, code)				\
	extern void						\
	ia64_native_ ## name ## _func(unsigned long arg);	\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC1_VOID(name, code)			\
	extern void						\
	ia64_native_ ## name ## _func(void *arg);		\
	__DEFINE_FUNC(name, code)

#define DEFINE_VOID_FUNC2(name, code)				\
	extern void						\
	ia64_native_ ## name ## _func(unsigned long arg0,	\
				      unsigned long arg1);	\
	__DEFINE_FUNC(name, code)

#define DEFINE_FUNC0(name, code)		\
	extern unsigned long			\
	ia64_native_ ## name ## _func(void);	\
	__DEFINE_FUNC(name, code)

#define DEFINE_FUNC1(name, type, code)			\
	extern unsigned long				\
	ia64_native_ ## name ## _func(type arg);	\
	__DEFINE_FUNC(name, code)

DEFINE_VOID_FUNC1_VOID(fc,
		       "fc r8\n");
DEFINE_VOID_FUNC1(intrin_local_irq_restore,
		  ";;\n"
		  "     cmp.ne p6, p7 = r8, r0\n"
		  ";;\n"
		  "(p6) ssm psr.i\n"
		  "(p7) rsm psr.i\n"
		  ";;\n"
		  "(p6) srlz.d\n");

DEFINE_VOID_FUNC2(ptcga,
		  "ptc.ga r8, r9\n");
DEFINE_VOID_FUNC2(set_rr,
		  "mov rr[r8] = r9\n");

/* ia64_native_getreg(_IA64_REG_PSR) & IA64_PSR_I */
DEFINE_FUNC0(get_psr_i,
	     "mov r2 = " __stringify(1 << IA64_PSR_I_BIT) "\n"
	     "mov r8 = psr\n"
	     ";;\n"
	     "and r8 = r2, r8\n");

DEFINE_FUNC1(thash, unsigned long,
	     "thash r8 = r8\n");
DEFINE_FUNC1(get_cpuid, int,
	     "mov r8 = cpuid[r8]\n");
DEFINE_FUNC1(get_pmd, int,
	     "mov r8 = pmd[r8]\n");
DEFINE_FUNC1(get_rr, unsigned long,
	     "mov r8 = rr[r8]\n");

DEFINE_VOID_FUNC0(ssm_i,
		  "ssm psr.i\n");
DEFINE_VOID_FUNC0(rsm_i,
		  "rsm psr.i\n");

extern void
ia64_native_set_rr0_to_rr4_func(unsigned long val0, unsigned long val1,
				unsigned long val2, unsigned long val3,
				unsigned long val4);
__DEFINE_FUNC(set_rr0_to_rr4,
	      "mov rr[r0] = r8\n"
	      "movl r2 = 0x2000000000000000\n"
	      ";;\n"
	      "mov rr[r2] = r9\n"
	      "shl r3 = r2, 1\n"	/* movl r3 = 0x4000000000000000 */
	      ";;\n"
	      "add r2 = r2, r3\n"	/* movl r2 = 0x6000000000000000 */
	      "mov rr[r3] = r10\n"
	      ";;\n"
	      "mov rr[r2] = r11\n"
	      "shl r3 = r3, 1\n"	/* movl r3 = 0x8000000000000000 */
	      ";;\n"
	      "mov rr[r3] = r14\n");

extern unsigned long ia64_native_getreg_func(int regnum);
asm(".global ia64_native_getreg_func\n");
#define __DEFINE_GET_REG(id, reg)			\
	"mov r2 = " __stringify(_IA64_REG_ ## id) "\n"	\
	";;\n"						\
	"cmp.eq p6, p0 = r2, r8\n"			\
	";;\n"						\
	"(p6) mov r8 = " #reg "\n"			\
	"(p6) br.cond.sptk.many b6\n"			\
	";;\n"
#define __DEFINE_GET_AR(id, reg)	__DEFINE_GET_REG(AR_ ## id, ar.reg)
#define __DEFINE_GET_CR(id, reg)	__DEFINE_GET_REG(CR_ ## id, cr.reg)

__DEFINE_FUNC(getreg,
	      __DEFINE_GET_REG(GP, gp)
	      /*__DEFINE_GET_REG(IP, ip)*/ /* returned ip value shouldn't be constant */
	      __DEFINE_GET_REG(PSR, psr)
	      __DEFINE_GET_REG(TP, tp)
	      __DEFINE_GET_REG(SP, sp)

	      __DEFINE_GET_REG(AR_KR0, ar0)
	      __DEFINE_GET_REG(AR_KR1, ar1)
	      __DEFINE_GET_REG(AR_KR2, ar2)
	      __DEFINE_GET_REG(AR_KR3, ar3)
	      __DEFINE_GET_REG(AR_KR4, ar4)
	      __DEFINE_GET_REG(AR_KR5, ar5)
	      __DEFINE_GET_REG(AR_KR6, ar6)
	      __DEFINE_GET_REG(AR_KR7, ar7)
	      __DEFINE_GET_AR(RSC, rsc)
	      __DEFINE_GET_AR(BSP, bsp)
	      __DEFINE_GET_AR(BSPSTORE, bspstore)
	      __DEFINE_GET_AR(RNAT, rnat)
	      __DEFINE_GET_AR(FCR, fcr)
	      __DEFINE_GET_AR(EFLAG, eflag)
	      __DEFINE_GET_AR(CSD, csd)
	      __DEFINE_GET_AR(SSD, ssd)
	      __DEFINE_GET_REG(AR_CFLAG, ar27)
	      __DEFINE_GET_AR(FSR, fsr)
	      __DEFINE_GET_AR(FIR, fir)
	      __DEFINE_GET_AR(FDR, fdr)
	      __DEFINE_GET_AR(CCV, ccv)
	      __DEFINE_GET_AR(UNAT, unat)
	      __DEFINE_GET_AR(FPSR, fpsr)
	      __DEFINE_GET_AR(ITC, itc)
	      __DEFINE_GET_AR(PFS, pfs)
	      __DEFINE_GET_AR(LC, lc)
	      __DEFINE_GET_AR(EC, ec)

	      __DEFINE_GET_CR(DCR, dcr)
	      __DEFINE_GET_CR(ITM, itm)
	      __DEFINE_GET_CR(IVA, iva)
	      __DEFINE_GET_CR(PTA, pta)
	      __DEFINE_GET_CR(IPSR, ipsr)
	      __DEFINE_GET_CR(ISR, isr)
	      __DEFINE_GET_CR(IIP, iip)
	      __DEFINE_GET_CR(IFA, ifa)
	      __DEFINE_GET_CR(ITIR, itir)
	      __DEFINE_GET_CR(IIPA, iipa)
	      __DEFINE_GET_CR(IFS, ifs)
	      __DEFINE_GET_CR(IIM, iim)
	      __DEFINE_GET_CR(IHA, iha)
	      __DEFINE_GET_CR(LID, lid)
	      __DEFINE_GET_CR(IVR, ivr)
	      __DEFINE_GET_CR(TPR, tpr)
	      __DEFINE_GET_CR(EOI, eoi)
	      __DEFINE_GET_CR(IRR0, irr0)
	      __DEFINE_GET_CR(IRR1, irr1)
	      __DEFINE_GET_CR(IRR2, irr2)
	      __DEFINE_GET_CR(IRR3, irr3)
	      __DEFINE_GET_CR(ITV, itv)
	      __DEFINE_GET_CR(PMV, pmv)
	      __DEFINE_GET_CR(CMCV, cmcv)
	      __DEFINE_GET_CR(LRR0, lrr0)
	      __DEFINE_GET_CR(LRR1, lrr1)

	      "mov r8 = -1\n"	/* unsupported case */
	);

extern void ia64_native_setreg_func(int regnum, unsigned long val);
asm(".global ia64_native_setreg_func\n");
#define __DEFINE_SET_REG(id, reg)			\
	"mov r2 = " __stringify(_IA64_REG_ ## id) "\n"	\
	";;\n"						\
	"cmp.eq p6, p0 = r2, r9\n"			\
	";;\n"						\
	"(p6) mov " #reg " = r8\n"			\
	"(p6) br.cond.sptk.many b6\n"			\
	";;\n"
#define __DEFINE_SET_AR(id, reg)	__DEFINE_SET_REG(AR_ ## id, ar.reg)
#define __DEFINE_SET_CR(id, reg)	__DEFINE_SET_REG(CR_ ## id, cr.reg)
__DEFINE_FUNC(setreg,
	      "mov r2 = " __stringify(_IA64_REG_PSR_L) "\n"
	      ";;\n"
	      "cmp.eq p6, p0 = r2, r9\n"
	      ";;\n"
	      "(p6) mov psr.l = r8\n"
#ifdef HAVE_SERIALIZE_DIRECTIVE
	      ".serialize.data\n"
#endif
	      "(p6) br.cond.sptk.many b6\n"
	      __DEFINE_SET_REG(GP, gp)
	      __DEFINE_SET_REG(SP, sp)

	      __DEFINE_SET_REG(AR_KR0, ar0)
	      __DEFINE_SET_REG(AR_KR1, ar1)
	      __DEFINE_SET_REG(AR_KR2, ar2)
	      __DEFINE_SET_REG(AR_KR3, ar3)
	      __DEFINE_SET_REG(AR_KR4, ar4)
	      __DEFINE_SET_REG(AR_KR5, ar5)
	      __DEFINE_SET_REG(AR_KR6, ar6)
	      __DEFINE_SET_REG(AR_KR7, ar7)
	      __DEFINE_SET_AR(RSC, rsc)
	      __DEFINE_SET_AR(BSP, bsp)
	      __DEFINE_SET_AR(BSPSTORE, bspstore)
	      __DEFINE_SET_AR(RNAT, rnat)
	      __DEFINE_SET_AR(FCR, fcr)
	      __DEFINE_SET_AR(EFLAG, eflag)
	      __DEFINE_SET_AR(CSD, csd)
	      __DEFINE_SET_AR(SSD, ssd)
	      __DEFINE_SET_REG(AR_CFLAG, ar27)
	      __DEFINE_SET_AR(FSR, fsr)
	      __DEFINE_SET_AR(FIR, fir)
	      __DEFINE_SET_AR(FDR, fdr)
	      __DEFINE_SET_AR(CCV, ccv)
	      __DEFINE_SET_AR(UNAT, unat)
	      __DEFINE_SET_AR(FPSR, fpsr)
	      __DEFINE_SET_AR(ITC, itc)
	      __DEFINE_SET_AR(PFS, pfs)
	      __DEFINE_SET_AR(LC, lc)
	      __DEFINE_SET_AR(EC, ec)

	      __DEFINE_SET_CR(DCR, dcr)
	      __DEFINE_SET_CR(ITM, itm)
	      __DEFINE_SET_CR(IVA, iva)
	      __DEFINE_SET_CR(PTA, pta)
	      __DEFINE_SET_CR(IPSR, ipsr)
	      __DEFINE_SET_CR(ISR, isr)
	      __DEFINE_SET_CR(IIP, iip)
	      __DEFINE_SET_CR(IFA, ifa)
	      __DEFINE_SET_CR(ITIR, itir)
	      __DEFINE_SET_CR(IIPA, iipa)
	      __DEFINE_SET_CR(IFS, ifs)
	      __DEFINE_SET_CR(IIM, iim)
	      __DEFINE_SET_CR(IHA, iha)
	      __DEFINE_SET_CR(LID, lid)
	      __DEFINE_SET_CR(IVR, ivr)
	      __DEFINE_SET_CR(TPR, tpr)
	      __DEFINE_SET_CR(EOI, eoi)
	      __DEFINE_SET_CR(IRR0, irr0)
	      __DEFINE_SET_CR(IRR1, irr1)
	      __DEFINE_SET_CR(IRR2, irr2)
	      __DEFINE_SET_CR(IRR3, irr3)
	      __DEFINE_SET_CR(ITV, itv)
	      __DEFINE_SET_CR(PMV, pmv)
	      __DEFINE_SET_CR(CMCV, cmcv)
	      __DEFINE_SET_CR(LRR0, lrr0)
	      __DEFINE_SET_CR(LRR1, lrr1)
	);
#endif

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
struct jump_label_key paravirt_steal_enabled;
struct jump_label_key paravirt_steal_rq_enabled;

static int
ia64_native_do_steal_accounting(unsigned long *new_itm)
{
	return 0;
}

struct pv_time_ops pv_time_ops = {
	.do_steal_accounting = ia64_native_do_steal_accounting,
	.sched_clock = ia64_native_sched_clock,
};

/***************************************************************************
 * binary pacthing
 * pv_init_ops.patch_bundle
 */

#ifdef ASM_SUPPORTED
#define IA64_NATIVE_PATCH_DEFINE_GET_REG(name, reg)	\
	__DEFINE_FUNC(get_ ## name,			\
		      ";;\n"				\
		      "mov r8 = " #reg "\n"		\
		      ";;\n")

#define IA64_NATIVE_PATCH_DEFINE_SET_REG(name, reg)	\
	__DEFINE_FUNC(set_ ## name,			\
		      ";;\n"				\
		      "mov " #reg " = r8\n"		\
		      ";;\n")

#define IA64_NATIVE_PATCH_DEFINE_REG(name, reg)		\
	IA64_NATIVE_PATCH_DEFINE_GET_REG(name, reg);	\
	IA64_NATIVE_PATCH_DEFINE_SET_REG(name, reg)	\

#define IA64_NATIVE_PATCH_DEFINE_AR(name, reg)			\
	IA64_NATIVE_PATCH_DEFINE_REG(ar_ ## name, ar.reg)

#define IA64_NATIVE_PATCH_DEFINE_CR(name, reg)			\
	IA64_NATIVE_PATCH_DEFINE_REG(cr_ ## name, cr.reg)


IA64_NATIVE_PATCH_DEFINE_GET_REG(psr, psr);
IA64_NATIVE_PATCH_DEFINE_GET_REG(tp, tp);

/* IA64_NATIVE_PATCH_DEFINE_SET_REG(psr_l, psr.l); */
__DEFINE_FUNC(set_psr_l,
	      ";;\n"
	      "mov psr.l = r8\n"
#ifdef HAVE_SERIALIZE_DIRECTIVE
	      ".serialize.data\n"
#endif
	      ";;\n");

IA64_NATIVE_PATCH_DEFINE_REG(gp, gp);
IA64_NATIVE_PATCH_DEFINE_REG(sp, sp);

IA64_NATIVE_PATCH_DEFINE_REG(kr0, ar0);
IA64_NATIVE_PATCH_DEFINE_REG(kr1, ar1);
IA64_NATIVE_PATCH_DEFINE_REG(kr2, ar2);
IA64_NATIVE_PATCH_DEFINE_REG(kr3, ar3);
IA64_NATIVE_PATCH_DEFINE_REG(kr4, ar4);
IA64_NATIVE_PATCH_DEFINE_REG(kr5, ar5);
IA64_NATIVE_PATCH_DEFINE_REG(kr6, ar6);
IA64_NATIVE_PATCH_DEFINE_REG(kr7, ar7);

IA64_NATIVE_PATCH_DEFINE_AR(rsc, rsc);
IA64_NATIVE_PATCH_DEFINE_AR(bsp, bsp);
IA64_NATIVE_PATCH_DEFINE_AR(bspstore, bspstore);
IA64_NATIVE_PATCH_DEFINE_AR(rnat, rnat);
IA64_NATIVE_PATCH_DEFINE_AR(fcr, fcr);
IA64_NATIVE_PATCH_DEFINE_AR(eflag, eflag);
IA64_NATIVE_PATCH_DEFINE_AR(csd, csd);
IA64_NATIVE_PATCH_DEFINE_AR(ssd, ssd);
IA64_NATIVE_PATCH_DEFINE_REG(ar27, ar27);
IA64_NATIVE_PATCH_DEFINE_AR(fsr, fsr);
IA64_NATIVE_PATCH_DEFINE_AR(fir, fir);
IA64_NATIVE_PATCH_DEFINE_AR(fdr, fdr);
IA64_NATIVE_PATCH_DEFINE_AR(ccv, ccv);
IA64_NATIVE_PATCH_DEFINE_AR(unat, unat);
IA64_NATIVE_PATCH_DEFINE_AR(fpsr, fpsr);
IA64_NATIVE_PATCH_DEFINE_AR(itc, itc);
IA64_NATIVE_PATCH_DEFINE_AR(pfs, pfs);
IA64_NATIVE_PATCH_DEFINE_AR(lc, lc);
IA64_NATIVE_PATCH_DEFINE_AR(ec, ec);

IA64_NATIVE_PATCH_DEFINE_CR(dcr, dcr);
IA64_NATIVE_PATCH_DEFINE_CR(itm, itm);
IA64_NATIVE_PATCH_DEFINE_CR(iva, iva);
IA64_NATIVE_PATCH_DEFINE_CR(pta, pta);
IA64_NATIVE_PATCH_DEFINE_CR(ipsr, ipsr);
IA64_NATIVE_PATCH_DEFINE_CR(isr, isr);
IA64_NATIVE_PATCH_DEFINE_CR(iip, iip);
IA64_NATIVE_PATCH_DEFINE_CR(ifa, ifa);
IA64_NATIVE_PATCH_DEFINE_CR(itir, itir);
IA64_NATIVE_PATCH_DEFINE_CR(iipa, iipa);
IA64_NATIVE_PATCH_DEFINE_CR(ifs, ifs);
IA64_NATIVE_PATCH_DEFINE_CR(iim, iim);
IA64_NATIVE_PATCH_DEFINE_CR(iha, iha);
IA64_NATIVE_PATCH_DEFINE_CR(lid, lid);
IA64_NATIVE_PATCH_DEFINE_CR(ivr, ivr);
IA64_NATIVE_PATCH_DEFINE_CR(tpr, tpr);
IA64_NATIVE_PATCH_DEFINE_CR(eoi, eoi);
IA64_NATIVE_PATCH_DEFINE_CR(irr0, irr0);
IA64_NATIVE_PATCH_DEFINE_CR(irr1, irr1);
IA64_NATIVE_PATCH_DEFINE_CR(irr2, irr2);
IA64_NATIVE_PATCH_DEFINE_CR(irr3, irr3);
IA64_NATIVE_PATCH_DEFINE_CR(itv, itv);
IA64_NATIVE_PATCH_DEFINE_CR(pmv, pmv);
IA64_NATIVE_PATCH_DEFINE_CR(cmcv, cmcv);
IA64_NATIVE_PATCH_DEFINE_CR(lrr0, lrr0);
IA64_NATIVE_PATCH_DEFINE_CR(lrr1, lrr1);

static const struct paravirt_patch_bundle_elem ia64_native_patch_bundle_elems[]
__initdata_or_module =
{
#define IA64_NATIVE_PATCH_BUNDLE_ELEM(name, type)		\
	{							\
		(void*)ia64_native_ ## name ## _direct_start,	\
		(void*)ia64_native_ ## name ## _direct_end,	\
		PARAVIRT_PATCH_TYPE_ ## type,			\
	}

	IA64_NATIVE_PATCH_BUNDLE_ELEM(fc, FC),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(thash, THASH),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(get_cpuid, GET_CPUID),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(get_pmd, GET_PMD),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(ptcga, PTCGA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(get_rr, GET_RR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(set_rr, SET_RR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(set_rr0_to_rr4, SET_RR0_TO_RR4),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(ssm_i, SSM_I),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(rsm_i, RSM_I),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(get_psr_i, GET_PSR_I),
	IA64_NATIVE_PATCH_BUNDLE_ELEM(intrin_local_irq_restore,
				      INTRIN_LOCAL_IRQ_RESTORE),

#define IA64_NATIVE_PATCH_BUNDLE_ELEM_GETREG(name, reg)			\
	{								\
		(void*)ia64_native_get_ ## name ## _direct_start,	\
		(void*)ia64_native_get_ ## name ## _direct_end,		\
		PARAVIRT_PATCH_TYPE_GETREG + _IA64_REG_ ## reg,		\
	}

#define IA64_NATIVE_PATCH_BUNDLE_ELEM_SETREG(name, reg)			\
	{								\
		(void*)ia64_native_set_ ## name ## _direct_start,	\
		(void*)ia64_native_set_ ## name ## _direct_end,		\
		PARAVIRT_PATCH_TYPE_SETREG + _IA64_REG_ ## reg,		\
	}

#define IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(name, reg)		\
	IA64_NATIVE_PATCH_BUNDLE_ELEM_GETREG(name, reg),	\
	IA64_NATIVE_PATCH_BUNDLE_ELEM_SETREG(name, reg)		\

#define IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(name, reg)		\
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(ar_ ## name, AR_ ## reg)

#define IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(name, reg)		\
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(cr_ ## name, CR_ ## reg)

	IA64_NATIVE_PATCH_BUNDLE_ELEM_GETREG(psr, PSR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_GETREG(tp, TP),

	IA64_NATIVE_PATCH_BUNDLE_ELEM_SETREG(psr_l, PSR_L),

	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(gp, GP),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(sp, SP),

	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr0, AR_KR0),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr1, AR_KR1),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr2, AR_KR2),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr3, AR_KR3),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr4, AR_KR4),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr5, AR_KR5),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr6, AR_KR6),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(kr7, AR_KR7),

	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(rsc, RSC),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(bsp, BSP),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(bspstore, BSPSTORE),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(rnat, RNAT),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(fcr, FCR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(eflag, EFLAG),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(csd, CSD),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(ssd, SSD),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_REG(ar27, AR_CFLAG),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(fsr, FSR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(fir, FIR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(fdr, FDR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(ccv, CCV),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(unat, UNAT),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(fpsr, FPSR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(itc, ITC),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(pfs, PFS),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(lc, LC),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_AR(ec, EC),

	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(dcr, DCR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(itm, ITM),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(iva, IVA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(pta, PTA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(ipsr, IPSR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(isr, ISR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(iip, IIP),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(ifa, IFA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(itir, ITIR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(iipa, IIPA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(ifs, IFS),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(iim, IIM),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(iha, IHA),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(lid, LID),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(ivr, IVR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(tpr, TPR),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(eoi, EOI),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(irr0, IRR0),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(irr1, IRR1),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(irr2, IRR2),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(irr3, IRR3),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(itv, ITV),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(pmv, PMV),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(cmcv, CMCV),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(lrr0, LRR0),
	IA64_NATIVE_PATCH_BUNDLE_ELEM_CR(lrr1, LRR1),
};

unsigned long __init_or_module
ia64_native_patch_bundle(void *sbundle, void *ebundle, unsigned long type)
{
	const unsigned long nelems = sizeof(ia64_native_patch_bundle_elems) /
		sizeof(ia64_native_patch_bundle_elems[0]);

	return __paravirt_patch_apply_bundle(sbundle, ebundle, type,
					      ia64_native_patch_bundle_elems,
					      nelems, NULL);
}
#endif /* ASM_SUPPOTED */

extern const char ia64_native_switch_to[];
extern const char ia64_native_leave_syscall[];
extern const char ia64_native_work_processed_syscall[];
extern const char ia64_native_leave_kernel[];

const struct paravirt_patch_branch_target ia64_native_branch_target[]
__initconst = {
#define PARAVIRT_BR_TARGET(name, type)			\
	{						\
		ia64_native_ ## name,			\
		PARAVIRT_PATCH_TYPE_BR_ ## type,	\
	}
	PARAVIRT_BR_TARGET(switch_to, SWITCH_TO),
	PARAVIRT_BR_TARGET(leave_syscall, LEAVE_SYSCALL),
	PARAVIRT_BR_TARGET(work_processed_syscall, WORK_PROCESSED_SYSCALL),
	PARAVIRT_BR_TARGET(leave_kernel, LEAVE_KERNEL),
};

static void __init
ia64_native_patch_branch(unsigned long tag, unsigned long type)
{
	const unsigned long nelem =
		sizeof(ia64_native_branch_target) /
		sizeof(ia64_native_branch_target[0]);
	__paravirt_patch_apply_branch(tag, type,
				      ia64_native_branch_target, nelem);
}

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGARCH_KVM_CSR_H__
#define __ASM_LOONGARCH_KVM_CSR_H__

#include <linux/uaccess.h>
#include <linux/kvm_host.h>
#include <asm/loongarch.h>
#include <asm/kvm_vcpu.h>

#define gcsr_read(csr)						\
({								\
	register unsigned long __v;				\
	__asm__ __volatile__(					\
		" gcsrrd %[val], %[reg]\n\t"			\
		: [val] "=r" (__v)				\
		: [reg] "i" (csr)				\
		: "memory");					\
	__v;							\
})

#define gcsr_write(v, csr)					\
({								\
	register unsigned long __v = v;				\
	__asm__ __volatile__ (					\
		" gcsrwr %[val], %[reg]\n\t"			\
		: [val] "+r" (__v)				\
		: [reg] "i" (csr)				\
		: "memory");					\
	__v;							\
})

#define gcsr_xchg(v, m, csr)					\
({								\
	register unsigned long __v = v;				\
	__asm__ __volatile__(					\
		" gcsrxchg %[val], %[mask], %[reg]\n\t"		\
		: [val] "+r" (__v)				\
		: [mask] "r" (m), [reg] "i" (csr)		\
		: "memory");					\
	__v;							\
})

/* Guest CSRS read and write */
#define read_gcsr_crmd()		gcsr_read(LOONGARCH_CSR_CRMD)
#define write_gcsr_crmd(val)		gcsr_write(val, LOONGARCH_CSR_CRMD)
#define read_gcsr_prmd()		gcsr_read(LOONGARCH_CSR_PRMD)
#define write_gcsr_prmd(val)		gcsr_write(val, LOONGARCH_CSR_PRMD)
#define read_gcsr_euen()		gcsr_read(LOONGARCH_CSR_EUEN)
#define write_gcsr_euen(val)		gcsr_write(val, LOONGARCH_CSR_EUEN)
#define read_gcsr_misc()		gcsr_read(LOONGARCH_CSR_MISC)
#define write_gcsr_misc(val)		gcsr_write(val, LOONGARCH_CSR_MISC)
#define read_gcsr_ecfg()		gcsr_read(LOONGARCH_CSR_ECFG)
#define write_gcsr_ecfg(val)		gcsr_write(val, LOONGARCH_CSR_ECFG)
#define read_gcsr_estat()		gcsr_read(LOONGARCH_CSR_ESTAT)
#define write_gcsr_estat(val)		gcsr_write(val, LOONGARCH_CSR_ESTAT)
#define read_gcsr_era()			gcsr_read(LOONGARCH_CSR_ERA)
#define write_gcsr_era(val)		gcsr_write(val, LOONGARCH_CSR_ERA)
#define read_gcsr_badv()		gcsr_read(LOONGARCH_CSR_BADV)
#define write_gcsr_badv(val)		gcsr_write(val, LOONGARCH_CSR_BADV)
#define read_gcsr_badi()		gcsr_read(LOONGARCH_CSR_BADI)
#define write_gcsr_badi(val)		gcsr_write(val, LOONGARCH_CSR_BADI)
#define read_gcsr_eentry()		gcsr_read(LOONGARCH_CSR_EENTRY)
#define write_gcsr_eentry(val)		gcsr_write(val, LOONGARCH_CSR_EENTRY)

#define read_gcsr_asid()		gcsr_read(LOONGARCH_CSR_ASID)
#define write_gcsr_asid(val)		gcsr_write(val, LOONGARCH_CSR_ASID)
#define read_gcsr_pgdl()		gcsr_read(LOONGARCH_CSR_PGDL)
#define write_gcsr_pgdl(val)		gcsr_write(val, LOONGARCH_CSR_PGDL)
#define read_gcsr_pgdh()		gcsr_read(LOONGARCH_CSR_PGDH)
#define write_gcsr_pgdh(val)		gcsr_write(val, LOONGARCH_CSR_PGDH)
#define write_gcsr_pgd(val)		gcsr_write(val, LOONGARCH_CSR_PGD)
#define read_gcsr_pgd()			gcsr_read(LOONGARCH_CSR_PGD)
#define read_gcsr_pwctl0()		gcsr_read(LOONGARCH_CSR_PWCTL0)
#define write_gcsr_pwctl0(val)		gcsr_write(val, LOONGARCH_CSR_PWCTL0)
#define read_gcsr_pwctl1()		gcsr_read(LOONGARCH_CSR_PWCTL1)
#define write_gcsr_pwctl1(val)		gcsr_write(val, LOONGARCH_CSR_PWCTL1)
#define read_gcsr_stlbpgsize()		gcsr_read(LOONGARCH_CSR_STLBPGSIZE)
#define write_gcsr_stlbpgsize(val)	gcsr_write(val, LOONGARCH_CSR_STLBPGSIZE)
#define read_gcsr_rvacfg()		gcsr_read(LOONGARCH_CSR_RVACFG)
#define write_gcsr_rvacfg(val)		gcsr_write(val, LOONGARCH_CSR_RVACFG)

#define read_gcsr_cpuid()		gcsr_read(LOONGARCH_CSR_CPUID)
#define write_gcsr_cpuid(val)		gcsr_write(val, LOONGARCH_CSR_CPUID)
#define read_gcsr_prcfg1()		gcsr_read(LOONGARCH_CSR_PRCFG1)
#define write_gcsr_prcfg1(val)		gcsr_write(val, LOONGARCH_CSR_PRCFG1)
#define read_gcsr_prcfg2()		gcsr_read(LOONGARCH_CSR_PRCFG2)
#define write_gcsr_prcfg2(val)		gcsr_write(val, LOONGARCH_CSR_PRCFG2)
#define read_gcsr_prcfg3()		gcsr_read(LOONGARCH_CSR_PRCFG3)
#define write_gcsr_prcfg3(val)		gcsr_write(val, LOONGARCH_CSR_PRCFG3)

#define read_gcsr_kscratch0()		gcsr_read(LOONGARCH_CSR_KS0)
#define write_gcsr_kscratch0(val)	gcsr_write(val, LOONGARCH_CSR_KS0)
#define read_gcsr_kscratch1()		gcsr_read(LOONGARCH_CSR_KS1)
#define write_gcsr_kscratch1(val)	gcsr_write(val, LOONGARCH_CSR_KS1)
#define read_gcsr_kscratch2()		gcsr_read(LOONGARCH_CSR_KS2)
#define write_gcsr_kscratch2(val)	gcsr_write(val, LOONGARCH_CSR_KS2)
#define read_gcsr_kscratch3()		gcsr_read(LOONGARCH_CSR_KS3)
#define write_gcsr_kscratch3(val)	gcsr_write(val, LOONGARCH_CSR_KS3)
#define read_gcsr_kscratch4()		gcsr_read(LOONGARCH_CSR_KS4)
#define write_gcsr_kscratch4(val)	gcsr_write(val, LOONGARCH_CSR_KS4)
#define read_gcsr_kscratch5()		gcsr_read(LOONGARCH_CSR_KS5)
#define write_gcsr_kscratch5(val)	gcsr_write(val, LOONGARCH_CSR_KS5)
#define read_gcsr_kscratch6()		gcsr_read(LOONGARCH_CSR_KS6)
#define write_gcsr_kscratch6(val)	gcsr_write(val, LOONGARCH_CSR_KS6)
#define read_gcsr_kscratch7()		gcsr_read(LOONGARCH_CSR_KS7)
#define write_gcsr_kscratch7(val)	gcsr_write(val, LOONGARCH_CSR_KS7)

#define read_gcsr_timerid()		gcsr_read(LOONGARCH_CSR_TMID)
#define write_gcsr_timerid(val)		gcsr_write(val, LOONGARCH_CSR_TMID)
#define read_gcsr_timercfg()		gcsr_read(LOONGARCH_CSR_TCFG)
#define write_gcsr_timercfg(val)	gcsr_write(val, LOONGARCH_CSR_TCFG)
#define read_gcsr_timertick()		gcsr_read(LOONGARCH_CSR_TVAL)
#define write_gcsr_timertick(val)	gcsr_write(val, LOONGARCH_CSR_TVAL)
#define read_gcsr_timeroffset()		gcsr_read(LOONGARCH_CSR_CNTC)
#define write_gcsr_timeroffset(val)	gcsr_write(val, LOONGARCH_CSR_CNTC)

#define read_gcsr_llbctl()		gcsr_read(LOONGARCH_CSR_LLBCTL)
#define write_gcsr_llbctl(val)		gcsr_write(val, LOONGARCH_CSR_LLBCTL)

#define read_gcsr_tlbidx()		gcsr_read(LOONGARCH_CSR_TLBIDX)
#define write_gcsr_tlbidx(val)		gcsr_write(val, LOONGARCH_CSR_TLBIDX)
#define read_gcsr_tlbrentry()		gcsr_read(LOONGARCH_CSR_TLBRENTRY)
#define write_gcsr_tlbrentry(val)	gcsr_write(val, LOONGARCH_CSR_TLBRENTRY)
#define read_gcsr_tlbrbadv()		gcsr_read(LOONGARCH_CSR_TLBRBADV)
#define write_gcsr_tlbrbadv(val)	gcsr_write(val, LOONGARCH_CSR_TLBRBADV)
#define read_gcsr_tlbrera()		gcsr_read(LOONGARCH_CSR_TLBRERA)
#define write_gcsr_tlbrera(val)		gcsr_write(val, LOONGARCH_CSR_TLBRERA)
#define read_gcsr_tlbrsave()		gcsr_read(LOONGARCH_CSR_TLBRSAVE)
#define write_gcsr_tlbrsave(val)	gcsr_write(val, LOONGARCH_CSR_TLBRSAVE)
#define read_gcsr_tlbrelo0()		gcsr_read(LOONGARCH_CSR_TLBRELO0)
#define write_gcsr_tlbrelo0(val)	gcsr_write(val, LOONGARCH_CSR_TLBRELO0)
#define read_gcsr_tlbrelo1()		gcsr_read(LOONGARCH_CSR_TLBRELO1)
#define write_gcsr_tlbrelo1(val)	gcsr_write(val, LOONGARCH_CSR_TLBRELO1)
#define read_gcsr_tlbrehi()		gcsr_read(LOONGARCH_CSR_TLBREHI)
#define write_gcsr_tlbrehi(val)		gcsr_write(val, LOONGARCH_CSR_TLBREHI)
#define read_gcsr_tlbrprmd()		gcsr_read(LOONGARCH_CSR_TLBRPRMD)
#define write_gcsr_tlbrprmd(val)	gcsr_write(val, LOONGARCH_CSR_TLBRPRMD)

#define read_gcsr_directwin0()		gcsr_read(LOONGARCH_CSR_DMWIN0)
#define write_gcsr_directwin0(val)	gcsr_write(val, LOONGARCH_CSR_DMWIN0)
#define read_gcsr_directwin1()		gcsr_read(LOONGARCH_CSR_DMWIN1)
#define write_gcsr_directwin1(val)	gcsr_write(val, LOONGARCH_CSR_DMWIN1)
#define read_gcsr_directwin2()		gcsr_read(LOONGARCH_CSR_DMWIN2)
#define write_gcsr_directwin2(val)	gcsr_write(val, LOONGARCH_CSR_DMWIN2)
#define read_gcsr_directwin3()		gcsr_read(LOONGARCH_CSR_DMWIN3)
#define write_gcsr_directwin3(val)	gcsr_write(val, LOONGARCH_CSR_DMWIN3)

/* Guest related CSRs */
#define read_csr_gtlbc()		csr_read64(LOONGARCH_CSR_GTLBC)
#define write_csr_gtlbc(val)		csr_write64(val, LOONGARCH_CSR_GTLBC)
#define read_csr_trgp()			csr_read64(LOONGARCH_CSR_TRGP)
#define read_csr_gcfg()			csr_read64(LOONGARCH_CSR_GCFG)
#define write_csr_gcfg(val)		csr_write64(val, LOONGARCH_CSR_GCFG)
#define read_csr_gstat()		csr_read64(LOONGARCH_CSR_GSTAT)
#define write_csr_gstat(val)		csr_write64(val, LOONGARCH_CSR_GSTAT)
#define read_csr_gintc()		csr_read64(LOONGARCH_CSR_GINTC)
#define write_csr_gintc(val)		csr_write64(val, LOONGARCH_CSR_GINTC)
#define read_csr_gcntc()		csr_read64(LOONGARCH_CSR_GCNTC)
#define write_csr_gcntc(val)		csr_write64(val, LOONGARCH_CSR_GCNTC)

#define __BUILD_GCSR_OP(name)		__BUILD_CSR_COMMON(gcsr_##name)

__BUILD_CSR_OP(gcfg)
__BUILD_CSR_OP(gstat)
__BUILD_CSR_OP(gtlbc)
__BUILD_CSR_OP(gintc)
__BUILD_GCSR_OP(llbctl)
__BUILD_GCSR_OP(tlbidx)

#define set_gcsr_estat(val)	\
	gcsr_xchg(val, val, LOONGARCH_CSR_ESTAT)
#define clear_gcsr_estat(val)	\
	gcsr_xchg(~(val), val, LOONGARCH_CSR_ESTAT)

#define kvm_read_hw_gcsr(id)		gcsr_read(id)
#define kvm_write_hw_gcsr(id, val)	gcsr_write(val, id)

#define kvm_save_hw_gcsr(csr, gid)	(csr->csrs[gid] = gcsr_read(gid))
#define kvm_restore_hw_gcsr(csr, gid)	(gcsr_write(csr->csrs[gid], gid))

#define kvm_read_clear_hw_gcsr(csr, gid)	(csr->csrs[gid] = gcsr_write(0, gid))

int kvm_emu_iocsr(larch_inst inst, struct kvm_run *run, struct kvm_vcpu *vcpu);

static __always_inline unsigned long kvm_read_sw_gcsr(struct loongarch_csrs *csr, int gid)
{
	return csr->csrs[gid];
}

static __always_inline void kvm_write_sw_gcsr(struct loongarch_csrs *csr, int gid, unsigned long val)
{
	csr->csrs[gid] = val;
}

static __always_inline void kvm_set_sw_gcsr(struct loongarch_csrs *csr,
					    int gid, unsigned long val)
{
	csr->csrs[gid] |= val;
}

static __always_inline void kvm_change_sw_gcsr(struct loongarch_csrs *csr,
					       int gid, unsigned long mask, unsigned long val)
{
	unsigned long _mask = mask;

	csr->csrs[gid] &= ~_mask;
	csr->csrs[gid] |= val & _mask;
}

#define KVM_PMU_EVENT_ENABLED	(CSR_PERFCTRL_PLV0 | CSR_PERFCTRL_PLV1 | \
					CSR_PERFCTRL_PLV2 | CSR_PERFCTRL_PLV3)

#endif	/* __ASM_LOONGARCH_KVM_CSR_H__ */

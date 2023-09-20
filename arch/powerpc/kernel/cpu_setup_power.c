// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020, Jordan Niethe, IBM Corporation.
 *
 * This file contains low level CPU setup functions.
 * Originally written in assembly by Benjamin Herrenschmidt & various other
 * authors.
 */

#include <asm/reg.h>
#include <asm/synch.h>
#include <linux/bitops.h>
#include <asm/cputable.h>
#include <asm/cpu_setup.h>

/* Disable CPU_FTR_HVMODE and return false if MSR:HV is not set */
static bool init_hvmode_206(struct cpu_spec *t)
{
	u64 msr;

	msr = mfmsr();
	if (msr & MSR_HV)
		return true;

	t->cpu_features &= ~(CPU_FTR_HVMODE | CPU_FTR_P9_TM_HV_ASSIST);
	return false;
}

static void init_LPCR_ISA300(u64 lpcr, u64 lpes)
{
	/* POWER9 has no VRMASD */
	lpcr |= (lpes << LPCR_LPES_SH) & LPCR_LPES;
	lpcr |= LPCR_PECE0|LPCR_PECE1|LPCR_PECE2;
	lpcr |= (4ull << LPCR_DPFD_SH) & LPCR_DPFD;
	lpcr &= ~LPCR_HDICE;	/* clear HDICE */
	lpcr |= (4ull << LPCR_VC_SH);
	mtspr(SPRN_LPCR, lpcr);
	isync();
}

/*
 * Setup a sane LPCR:
 *   Called with initial LPCR and desired LPES 2-bit value
 *
 *   LPES = 0b01 (HSRR0/1 used for 0x500)
 *   PECE = 0b111
 *   DPFD = 4
 *   HDICE = 0
 *   VC = 0b100 (VPM0=1, VPM1=0, ISL=0)
 *   VRMASD = 0b10000 (L=1, LP=00)
 *
 * Other bits untouched for now
 */
static void init_LPCR_ISA206(u64 lpcr, u64 lpes)
{
	lpcr |= (0x10ull << LPCR_VRMASD_SH) & LPCR_VRMASD;
	init_LPCR_ISA300(lpcr, lpes);
}

static void init_FSCR(void)
{
	u64 fscr;

	fscr = mfspr(SPRN_FSCR);
	fscr |= FSCR_TAR|FSCR_EBB;
	mtspr(SPRN_FSCR, fscr);
}

static void init_FSCR_power9(void)
{
	u64 fscr;

	fscr = mfspr(SPRN_FSCR);
	fscr |= FSCR_SCV;
	mtspr(SPRN_FSCR, fscr);
	init_FSCR();
}

static void init_FSCR_power10(void)
{
	u64 fscr;

	fscr = mfspr(SPRN_FSCR);
	fscr |= FSCR_PREFIX;
	mtspr(SPRN_FSCR, fscr);
	init_FSCR_power9();
}

static void init_HFSCR(void)
{
	u64 hfscr;

	hfscr = mfspr(SPRN_HFSCR);
	hfscr |= HFSCR_TAR|HFSCR_TM|HFSCR_BHRB|HFSCR_PM|HFSCR_DSCR|\
		 HFSCR_VECVSX|HFSCR_FP|HFSCR_EBB|HFSCR_MSGP;
	mtspr(SPRN_HFSCR, hfscr);
}

static void init_PMU_HV(void)
{
	mtspr(SPRN_MMCRC, 0);
}

static void init_PMU_HV_ISA207(void)
{
	mtspr(SPRN_MMCRH, 0);
}

static void init_PMU(void)
{
	mtspr(SPRN_MMCRA, 0);
	mtspr(SPRN_MMCR0, MMCR0_FC);
	mtspr(SPRN_MMCR1, 0);
	mtspr(SPRN_MMCR2, 0);
}

static void init_PMU_ISA207(void)
{
	mtspr(SPRN_MMCRS, 0);
}

static void init_PMU_ISA31(void)
{
	mtspr(SPRN_MMCR3, 0);
	mtspr(SPRN_MMCRA, MMCRA_BHRB_DISABLE);
	mtspr(SPRN_MMCR0, MMCR0_FC | MMCR0_PMCCEXT);
}

static void init_DEXCR(void)
{
	mtspr(SPRN_DEXCR, DEXCR_INIT);
	mtspr(SPRN_HASHKEYR, 0);
}

/*
 * Note that we can be called twice of pseudo-PVRs.
 * The parameter offset is not used.
 */

void __setup_cpu_power7(unsigned long offset, struct cpu_spec *t)
{
	if (!init_hvmode_206(t))
		return;

	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA206(mfspr(SPRN_LPCR), LPCR_LPES1 >> LPCR_LPES_SH);
}

void __restore_cpu_power7(void)
{
	u64 msr;

	msr = mfmsr();
	if (!(msr & MSR_HV))
		return;

	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA206(mfspr(SPRN_LPCR), LPCR_LPES1 >> LPCR_LPES_SH);
}

void __setup_cpu_power8(unsigned long offset, struct cpu_spec *t)
{
	init_FSCR();
	init_PMU();
	init_PMU_ISA207();

	if (!init_hvmode_206(t))
		return;

	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA206(mfspr(SPRN_LPCR) | LPCR_PECEDH, 0); /* LPES = 0 */
	init_HFSCR();
	init_PMU_HV();
	init_PMU_HV_ISA207();
}

void __restore_cpu_power8(void)
{
	u64 msr;

	init_FSCR();
	init_PMU();
	init_PMU_ISA207();

	msr = mfmsr();
	if (!(msr & MSR_HV))
		return;

	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA206(mfspr(SPRN_LPCR) | LPCR_PECEDH, 0); /* LPES = 0 */
	init_HFSCR();
	init_PMU_HV();
	init_PMU_HV_ISA207();
}

void __setup_cpu_power9(unsigned long offset, struct cpu_spec *t)
{
	init_FSCR_power9();
	init_PMU();

	if (!init_hvmode_206(t))
		return;

	mtspr(SPRN_PSSCR, 0);
	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_PID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA300((mfspr(SPRN_LPCR) | LPCR_PECEDH | LPCR_PECE_HVEE |\
			 LPCR_HVICE | LPCR_HEIC) & ~(LPCR_UPRT | LPCR_HR), 0);
	init_HFSCR();
	init_PMU_HV();
}

void __restore_cpu_power9(void)
{
	u64 msr;

	init_FSCR_power9();
	init_PMU();

	msr = mfmsr();
	if (!(msr & MSR_HV))
		return;

	mtspr(SPRN_PSSCR, 0);
	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_PID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA300((mfspr(SPRN_LPCR) | LPCR_PECEDH | LPCR_PECE_HVEE |\
			 LPCR_HVICE | LPCR_HEIC) & ~(LPCR_UPRT | LPCR_HR), 0);
	init_HFSCR();
	init_PMU_HV();
}

void __setup_cpu_power10(unsigned long offset, struct cpu_spec *t)
{
	init_FSCR_power10();
	init_PMU();
	init_PMU_ISA31();
	init_DEXCR();

	if (!init_hvmode_206(t))
		return;

	mtspr(SPRN_PSSCR, 0);
	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_PID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA300((mfspr(SPRN_LPCR) | LPCR_PECEDH | LPCR_PECE_HVEE |\
			 LPCR_HVICE | LPCR_HEIC) & ~(LPCR_UPRT | LPCR_HR), 0);
	init_HFSCR();
	init_PMU_HV();
}

void __restore_cpu_power10(void)
{
	u64 msr;

	init_FSCR_power10();
	init_PMU();
	init_PMU_ISA31();
	init_DEXCR();

	msr = mfmsr();
	if (!(msr & MSR_HV))
		return;

	mtspr(SPRN_PSSCR, 0);
	mtspr(SPRN_LPID, 0);
	mtspr(SPRN_PID, 0);
	mtspr(SPRN_AMOR, ~0);
	mtspr(SPRN_PCR, PCR_MASK);
	init_LPCR_ISA300((mfspr(SPRN_LPCR) | LPCR_PECEDH | LPCR_PECE_HVEE |\
			 LPCR_HVICE | LPCR_HEIC) & ~(LPCR_UPRT | LPCR_HR), 0);
	init_HFSCR();
	init_PMU_HV();
}

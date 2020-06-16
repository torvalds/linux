// SPDX-License-Identifier: GPL-2.0

#include <linux/smp.h>
#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/elf.h>

#include <loongson_regs.h>
#include <cpucfg-emul.h>

static bool is_loongson(struct cpuinfo_mips *c)
{
	switch (c->processor_id & PRID_COMP_MASK) {
	case PRID_COMP_LEGACY:
		return ((c->processor_id & PRID_IMP_MASK) ==
			PRID_IMP_LOONGSON_64C);

	case PRID_COMP_LOONGSON:
		return true;

	default:
		return false;
	}
}

static u32 get_loongson_fprev(struct cpuinfo_mips *c)
{
	return c->fpu_id & LOONGSON_FPREV_MASK;
}

static bool cpu_has_uca(void)
{
	u32 diag = read_c0_diag();
	u32 new_diag;

	if (diag & LOONGSON_DIAG_UCAC)
		/* UCA is already enabled. */
		return true;

	/* See if UCAC bit can be flipped on. This should be safe. */
	new_diag = diag | LOONGSON_DIAG_UCAC;
	write_c0_diag(new_diag);
	new_diag = read_c0_diag();
	write_c0_diag(diag);

	return (new_diag & LOONGSON_DIAG_UCAC) != 0;
}

static void probe_uca(struct cpuinfo_mips *c)
{
	if (cpu_has_uca())
		c->loongson3_cpucfg_data[0] |= LOONGSON_CFG1_LSUCA;
}

static void decode_loongson_config6(struct cpuinfo_mips *c)
{
	u32 config6 = read_c0_config6();

	if (config6 & MIPS_CONF6_LOONGSON_SFBEN)
		c->loongson3_cpucfg_data[0] |= LOONGSON_CFG1_SFBP;
	if (config6 & MIPS_CONF6_LOONGSON_LLEXC)
		c->loongson3_cpucfg_data[0] |= LOONGSON_CFG1_LLEXC;
	if (config6 & MIPS_CONF6_LOONGSON_SCRAND)
		c->loongson3_cpucfg_data[0] |= LOONGSON_CFG1_SCRAND;
}

static void patch_cpucfg_sel1(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u64 options = c->options;
	u32 data = c->loongson3_cpucfg_data[0];

	if (options & MIPS_CPU_FPU) {
		data |= LOONGSON_CFG1_FP;
		data |= get_loongson_fprev(c) << LOONGSON_CFG1_FPREV_OFFSET;
	}
	if (ases & MIPS_ASE_LOONGSON_MMI)
		data |= LOONGSON_CFG1_MMI;
	if (ases & MIPS_ASE_MSA)
		data |= LOONGSON_CFG1_MSA1;

	c->loongson3_cpucfg_data[0] = data;
}

static void patch_cpucfg_sel2(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u64 options = c->options;
	u32 data = c->loongson3_cpucfg_data[1];

	if (ases & MIPS_ASE_LOONGSON_EXT)
		data |= LOONGSON_CFG2_LEXT1;
	if (ases & MIPS_ASE_LOONGSON_EXT2)
		data |= LOONGSON_CFG2_LEXT2;
	if (options & MIPS_CPU_LDPTE)
		data |= LOONGSON_CFG2_LSPW;

	if (ases & MIPS_ASE_VZ)
		data |= LOONGSON_CFG2_LVZP;
	else
		data &= ~LOONGSON_CFG2_LVZREV;

	c->loongson3_cpucfg_data[1] = data;
}

static void patch_cpucfg_sel3(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u32 data = c->loongson3_cpucfg_data[2];

	if (ases & MIPS_ASE_LOONGSON_CAM) {
		data |= LOONGSON_CFG3_LCAMP;
	} else {
		data &= ~LOONGSON_CFG3_LCAMREV;
		data &= ~LOONGSON_CFG3_LCAMNUM;
		data &= ~LOONGSON_CFG3_LCAMKW;
		data &= ~LOONGSON_CFG3_LCAMVW;
	}

	c->loongson3_cpucfg_data[2] = data;
}

void loongson3_cpucfg_synthesize_data(struct cpuinfo_mips *c)
{
	/* Only engage the logic on Loongson processors. */
	if (!is_loongson(c))
		return;

	/* CPUs with CPUCFG support don't need to synthesize anything. */
	if (cpu_has_cfg())
		goto have_cpucfg_now;

	c->loongson3_cpucfg_data[0] = 0;
	c->loongson3_cpucfg_data[1] = 0;
	c->loongson3_cpucfg_data[2] = 0;

	/* Add CPUCFG features non-discoverable otherwise. */
	switch (c->processor_id & (PRID_IMP_MASK | PRID_REV_MASK)) {
	case PRID_IMP_LOONGSON_64R | PRID_REV_LOONGSON2K_R1_0:
	case PRID_IMP_LOONGSON_64R | PRID_REV_LOONGSON2K_R1_1:
	case PRID_IMP_LOONGSON_64R | PRID_REV_LOONGSON2K_R1_2:
	case PRID_IMP_LOONGSON_64R | PRID_REV_LOONGSON2K_R1_3:
		decode_loongson_config6(c);
		probe_uca(c);

		c->loongson3_cpucfg_data[0] |= (LOONGSON_CFG1_LSLDR0 |
			LOONGSON_CFG1_LSSYNCI | LOONGSON_CFG1_LLSYNC |
			LOONGSON_CFG1_TGTSYNC);
		c->loongson3_cpucfg_data[1] |= (LOONGSON_CFG2_LBT1 |
			LOONGSON_CFG2_LBT2 | LOONGSON_CFG2_LPMP |
			LOONGSON_CFG2_LPM_REV2);
		c->loongson3_cpucfg_data[2] = 0;
		break;

	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R1:
		c->loongson3_cpucfg_data[0] |= (LOONGSON_CFG1_LSLDR0 |
			LOONGSON_CFG1_LSSYNCI | LOONGSON_CFG1_LSUCA |
			LOONGSON_CFG1_LLSYNC | LOONGSON_CFG1_TGTSYNC);
		c->loongson3_cpucfg_data[1] |= (LOONGSON_CFG2_LBT1 |
			LOONGSON_CFG2_LPMP | LOONGSON_CFG2_LPM_REV1);
		c->loongson3_cpucfg_data[2] |= (
			LOONGSON_CFG3_LCAM_REV1 |
			LOONGSON_CFG3_LCAMNUM_REV1 |
			LOONGSON_CFG3_LCAMKW_REV1 |
			LOONGSON_CFG3_LCAMVW_REV1);
		break;

	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3B_R1:
	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3B_R2:
		c->loongson3_cpucfg_data[0] |= (LOONGSON_CFG1_LSLDR0 |
			LOONGSON_CFG1_LSSYNCI | LOONGSON_CFG1_LSUCA |
			LOONGSON_CFG1_LLSYNC | LOONGSON_CFG1_TGTSYNC);
		c->loongson3_cpucfg_data[1] |= (LOONGSON_CFG2_LBT1 |
			LOONGSON_CFG2_LPMP | LOONGSON_CFG2_LPM_REV1);
		c->loongson3_cpucfg_data[2] |= (
			LOONGSON_CFG3_LCAM_REV1 |
			LOONGSON_CFG3_LCAMNUM_REV1 |
			LOONGSON_CFG3_LCAMKW_REV1 |
			LOONGSON_CFG3_LCAMVW_REV1);
		break;

	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_0:
	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R2_1:
	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R3_0:
	case PRID_IMP_LOONGSON_64C | PRID_REV_LOONGSON3A_R3_1:
		decode_loongson_config6(c);
		probe_uca(c);

		c->loongson3_cpucfg_data[0] |= (LOONGSON_CFG1_CNT64 |
			LOONGSON_CFG1_LSLDR0 | LOONGSON_CFG1_LSPREF |
			LOONGSON_CFG1_LSPREFX | LOONGSON_CFG1_LSSYNCI |
			LOONGSON_CFG1_LLSYNC | LOONGSON_CFG1_TGTSYNC);
		c->loongson3_cpucfg_data[1] |= (LOONGSON_CFG2_LBT1 |
			LOONGSON_CFG2_LBT2 | LOONGSON_CFG2_LBTMMU |
			LOONGSON_CFG2_LPMP | LOONGSON_CFG2_LPM_REV1 |
			LOONGSON_CFG2_LVZ_REV1);
		c->loongson3_cpucfg_data[2] |= (LOONGSON_CFG3_LCAM_REV1 |
			LOONGSON_CFG3_LCAMNUM_REV1 |
			LOONGSON_CFG3_LCAMKW_REV1 |
			LOONGSON_CFG3_LCAMVW_REV1);
		break;

	default:
		/* It is possible that some future Loongson cores still do
		 * not have CPUCFG, so do not emulate anything for these
		 * cores.
		 */
		return;
	}

	/* This feature is set by firmware, but all known Loongson-64 systems
	 * are configured this way.
	 */
	c->loongson3_cpucfg_data[0] |= LOONGSON_CFG1_CDMAP;

	/* Patch in dynamically probed bits. */
	patch_cpucfg_sel1(c);
	patch_cpucfg_sel2(c);
	patch_cpucfg_sel3(c);

have_cpucfg_now:
	/* We have usable CPUCFG now, emulated or not.
	 * Announce CPUCFG availability to userspace via hwcap.
	 */
	elf_hwcap |= HWCAP_LOONGSON_CPUCFG;
}

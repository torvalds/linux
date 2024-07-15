// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_tcc.c - Library for Intel TCC (thermal control circuitry) MSR access
 * Copyright (c) 2022, Intel Corporation.
 */

#include <linux/errno.h>
#include <linux/intel_tcc.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>

/**
 * struct temp_masks - Bitmasks for temperature readings
 * @tcc_offset:			TCC offset in MSR_TEMPERATURE_TARGET
 * @digital_readout:		Digital readout in MSR_IA32_THERM_STATUS
 * @pkg_digital_readout:	Digital readout in MSR_IA32_PACKAGE_THERM_STATUS
 *
 * Bitmasks to extract the fields of the MSR_TEMPERATURE and IA32_[PACKAGE]_
 * THERM_STATUS registers for different processor models.
 *
 * The bitmask of TjMax is not included in this structure. It is always 0xff.
 */
struct temp_masks {
	u32 tcc_offset;
	u32 digital_readout;
	u32 pkg_digital_readout;
};

#define TCC_MODEL_TEMP_MASKS(model, _tcc_offset, _digital_readout,	\
			     _pkg_digital_readout)			\
	static const struct temp_masks temp_##model __initconst = {	\
		.tcc_offset = _tcc_offset,				\
		.digital_readout = _digital_readout,			\
		.pkg_digital_readout = _pkg_digital_readout		\
	}

TCC_MODEL_TEMP_MASKS(nehalem, 0, 0x7f, 0x7f);
TCC_MODEL_TEMP_MASKS(haswell_x, 0xf, 0x7f, 0x7f);
TCC_MODEL_TEMP_MASKS(broadwell, 0x3f, 0x7f, 0x7f);
TCC_MODEL_TEMP_MASKS(goldmont, 0x7f, 0x7f, 0x7f);
TCC_MODEL_TEMP_MASKS(tigerlake, 0x3f, 0xff, 0xff);
TCC_MODEL_TEMP_MASKS(sapphirerapids, 0x3f, 0x7f, 0xff);

/* Use these masks for processors not included in @tcc_cpu_ids. */
static struct temp_masks intel_tcc_temp_masks __ro_after_init = {
	.tcc_offset = 0x7f,
	.digital_readout = 0xff,
	.pkg_digital_readout = 0xff,
};

static const struct x86_cpu_id intel_tcc_cpu_ids[] __initconst = {
	X86_MATCH_VFM(INTEL_CORE_YONAH,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_CORE2_MEROM,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_CORE2_MEROM_L,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_CORE2_PENRYN,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_CORE2_DUNNINGTON,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_NEHALEM,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_NEHALEM_G,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_NEHALEM_EP,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_NEHALEM_EX,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_WESTMERE,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_WESTMERE_EP,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_WESTMERE_EX,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_SANDYBRIDGE_X,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_IVYBRIDGE,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_IVYBRIDGE_X,		&temp_haswell_x),
	X86_MATCH_VFM(INTEL_HASWELL,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_HASWELL_X,			&temp_haswell_x),
	X86_MATCH_VFM(INTEL_HASWELL_L,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_HASWELL_G,			&temp_nehalem),
	X86_MATCH_VFM(INTEL_BROADWELL,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_BROADWELL_G,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_BROADWELL_X,		&temp_haswell_x),
	X86_MATCH_VFM(INTEL_BROADWELL_D,		&temp_haswell_x),
	X86_MATCH_VFM(INTEL_SKYLAKE_L,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_SKYLAKE,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_SKYLAKE_X,			&temp_haswell_x),
	X86_MATCH_VFM(INTEL_KABYLAKE_L,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_KABYLAKE,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_COMETLAKE,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_COMETLAKE_L,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_CANNONLAKE_L,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ICELAKE_X,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_ICELAKE_D,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_ICELAKE,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_ICELAKE_L,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_ICELAKE_NNPI,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ROCKETLAKE,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L,		&temp_tigerlake),
	X86_MATCH_VFM(INTEL_TIGERLAKE,			&temp_tigerlake),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X,		&temp_sapphirerapids),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X,		&temp_sapphirerapids),
	X86_MATCH_VFM(INTEL_LAKEFIELD,			&temp_broadwell),
	X86_MATCH_VFM(INTEL_ALDERLAKE,			&temp_tigerlake),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,		&temp_tigerlake),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,			&temp_tigerlake),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,		&temp_tigerlake),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,		&temp_tigerlake),
	X86_MATCH_VFM(INTEL_ATOM_BONNELL,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_ATOM_BONNELL_MID,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_ATOM_SALTWELL,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_ATOM_SALTWELL_MID,		&temp_nehalem),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT_D,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_SILVERMONT_MID,	&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_AIRMONT,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_AIRMONT_MID,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_AIRMONT_NP,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT,		&temp_goldmont),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_D,		&temp_goldmont),
	X86_MATCH_VFM(INTEL_ATOM_GOLDMONT_PLUS,		&temp_goldmont),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_D,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_TREMONT_L,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_ATOM_GRACEMONT,		&temp_tigerlake),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNL,		&temp_broadwell),
	X86_MATCH_VFM(INTEL_XEON_PHI_KNM,		&temp_broadwell),
	{}
};

static int __init intel_tcc_init(void)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(intel_tcc_cpu_ids);
	if (id)
		memcpy(&intel_tcc_temp_masks, (const void *)id->driver_data,
		       sizeof(intel_tcc_temp_masks));

	return 0;
}
/*
 * Use subsys_initcall to ensure temperature bitmasks are initialized before
 * the drivers that use this library.
 */
subsys_initcall(intel_tcc_init);

/**
 * intel_tcc_get_offset_mask() - Returns the bitmask to read TCC offset
 *
 * Get the model-specific bitmask to extract TCC_OFFSET from the MSR
 * TEMPERATURE_TARGET register. If the mask is 0, it means the processor does
 * not support TCC offset.
 *
 * Return: The model-specific bitmask for TCC offset.
 */
u32 intel_tcc_get_offset_mask(void)
{
	return intel_tcc_temp_masks.tcc_offset;
}
EXPORT_SYMBOL_NS(intel_tcc_get_offset_mask, INTEL_TCC);

/**
 * get_temp_mask() - Returns the model-specific bitmask for temperature
 *
 * @pkg: true: Package Thermal Sensor. false: Core Thermal Sensor.
 *
 * Get the model-specific bitmask to extract the temperature reading from the
 * MSR_IA32_[PACKAGE]_THERM_STATUS register.
 *
 * Callers must check if the thermal status registers are supported.
 *
 * Return: The model-specific bitmask for temperature reading
 */
static u32 get_temp_mask(bool pkg)
{
	return pkg ? intel_tcc_temp_masks.pkg_digital_readout :
	       intel_tcc_temp_masks.digital_readout;
}

/**
 * intel_tcc_get_tjmax() - returns the default TCC activation Temperature
 * @cpu: cpu that the MSR should be run on, nagative value means any cpu.
 *
 * Get the TjMax value, which is the default thermal throttling or TCC
 * activation temperature in degrees C.
 *
 * Return: Tjmax value in degrees C on success, negative error code otherwise.
 */
int intel_tcc_get_tjmax(int cpu)
{
	u32 low, high;
	int val, err;

	if (cpu < 0)
		err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	else
		err = rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	if (err)
		return err;

	val = (low >> 16) & 0xff;

	return val ? val : -ENODATA;
}
EXPORT_SYMBOL_NS_GPL(intel_tcc_get_tjmax, INTEL_TCC);

/**
 * intel_tcc_get_offset() - returns the TCC Offset value to Tjmax
 * @cpu: cpu that the MSR should be run on, nagative value means any cpu.
 *
 * Get the TCC offset value to Tjmax. The effective thermal throttling or TCC
 * activation temperature equals "Tjmax" - "TCC Offset", in degrees C.
 *
 * Return: Tcc offset value in degrees C on success, negative error code otherwise.
 */
int intel_tcc_get_offset(int cpu)
{
	u32 low, high;
	int err;

	if (cpu < 0)
		err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	else
		err = rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	if (err)
		return err;

	return (low >> 24) & intel_tcc_temp_masks.tcc_offset;
}
EXPORT_SYMBOL_NS_GPL(intel_tcc_get_offset, INTEL_TCC);

/**
 * intel_tcc_set_offset() - set the TCC offset value to Tjmax
 * @cpu: cpu that the MSR should be run on, nagative value means any cpu.
 * @offset: TCC offset value in degree C
 *
 * Set the TCC Offset value to Tjmax. The effective thermal throttling or TCC
 * activation temperature equals "Tjmax" - "TCC Offset", in degree C.
 *
 * Return: On success returns 0, negative error code otherwise.
 */

int intel_tcc_set_offset(int cpu, int offset)
{
	u32 low, high;
	int err;

	if (!intel_tcc_temp_masks.tcc_offset)
		return -ENODEV;

	if (offset < 0 || offset > intel_tcc_temp_masks.tcc_offset)
		return -EINVAL;

	if (cpu < 0)
		err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	else
		err = rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &low, &high);
	if (err)
		return err;

	/* MSR Locked */
	if (low & BIT(31))
		return -EPERM;

	low &= ~(intel_tcc_temp_masks.tcc_offset << 24);
	low |= offset << 24;

	if (cpu < 0)
		return wrmsr_safe(MSR_IA32_TEMPERATURE_TARGET, low, high);
	else
		return wrmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, low, high);
}
EXPORT_SYMBOL_NS_GPL(intel_tcc_set_offset, INTEL_TCC);

/**
 * intel_tcc_get_temp() - returns the current temperature
 * @cpu: cpu that the MSR should be run on, nagative value means any cpu.
 * @temp: pointer to the memory for saving cpu temperature.
 * @pkg: true: Package Thermal Sensor. false: Core Thermal Sensor.
 *
 * Get the current temperature returned by the CPU core/package level
 * thermal sensor, in degrees C.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int intel_tcc_get_temp(int cpu, int *temp, bool pkg)
{
	u32 msr = pkg ? MSR_IA32_PACKAGE_THERM_STATUS : MSR_IA32_THERM_STATUS;
	u32 low, high, mask;
	int tjmax, err;

	tjmax = intel_tcc_get_tjmax(cpu);
	if (tjmax < 0)
		return tjmax;

	if (cpu < 0)
		err = rdmsr_safe(msr, &low, &high);
	else
		err = rdmsr_safe_on_cpu(cpu, msr, &low, &high);
	if (err)
		return err;

	/* Temperature is beyond the valid thermal sensor range */
	if (!(low & BIT(31)))
		return -ENODATA;

	mask = get_temp_mask(pkg);

	*temp = tjmax - ((low >> 16) & mask);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(intel_tcc_get_temp, INTEL_TCC);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_tcc.c - Library for Intel TCC (thermal control circuitry) MSR access
 * Copyright (c) 2022, Intel Corporation.
 */

#include <linux/errno.h>
#include <linux/intel_tcc.h>
#include <asm/msr.h>

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

	return (low >> 24) & 0x3f;
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

	if (offset < 0 || offset > 0x3f)
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

	low &= ~(0x3f << 24);
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
 * @pkg: true: Package Thermal Sensor. false: Core Thermal Sensor.
 *
 * Get the current temperature returned by the CPU core/package level
 * thermal sensor, in degrees C.
 *
 * Return: Temperature in degrees C on success, negative error code otherwise.
 */
int intel_tcc_get_temp(int cpu, bool pkg)
{
	u32 low, high;
	u32 msr = pkg ? MSR_IA32_PACKAGE_THERM_STATUS : MSR_IA32_THERM_STATUS;
	int tjmax, temp, err;

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

	temp = tjmax - ((low >> 16) & 0x7f);

	/* Do not allow negative CPU temperature */
	return temp >= 0 ? temp : -ENODATA;
}
EXPORT_SYMBOL_NS_GPL(intel_tcc_get_temp, INTEL_TCC);

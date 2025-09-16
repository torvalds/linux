// SPDX-License-Identifier: GPL-2.0

/*
 * Core routines for interacting with Microsoft's Hyper-V hypervisor,
 * including hypervisor initialization.
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Michael Kelley <mikelley@microsoft.com>
 */

#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/cpuhotplug.h>
#include <asm/mshyperv.h>

static bool hyperv_initialized;

int hv_get_hypervisor_version(union hv_hypervisor_version_info *info)
{
	hv_get_vpreg_128(HV_REGISTER_HYPERVISOR_VERSION,
			 (struct hv_get_vp_registers_output *)info);

	return 0;
}
EXPORT_SYMBOL_GPL(hv_get_hypervisor_version);

#ifdef CONFIG_ACPI

static bool __init hyperv_detect_via_acpi(void)
{
	if (acpi_disabled)
		return false;
	/*
	 * Hypervisor ID is only available in ACPI v6+, and the
	 * structure layout was extended in v6 to accommodate that
	 * new field.
	 *
	 * At the very minimum, this check makes sure not to read
	 * past the FADT structure.
	 *
	 * It is also needed to catch running in some unknown
	 * non-Hyper-V environment that has ACPI 5.x or less.
	 * In such a case, it can't be Hyper-V.
	 */
	if (acpi_gbl_FADT.header.revision < 6)
		return false;
	return strncmp((char *)&acpi_gbl_FADT.hypervisor_id, "MsHyperV", 8) == 0;
}

#else

static bool __init hyperv_detect_via_acpi(void)
{
	return false;
}

#endif

static bool __init hyperv_detect_via_smccc(void)
{
	uuid_t hyperv_uuid = UUID_INIT(
		0x58ba324d, 0x6447, 0x24cd,
		0x75, 0x6c, 0xef, 0x8e,
		0x24, 0x70, 0x59, 0x16);

	return arm_smccc_hypervisor_has_uuid(&hyperv_uuid);
}

static int __init hyperv_init(void)
{
	struct hv_get_vp_registers_output	result;
	u64	guest_id;
	int	ret;

	/*
	 * Allow for a kernel built with CONFIG_HYPERV to be running in
	 * a non-Hyper-V environment.
	 *
	 * In such cases, do nothing and return success.
	 */
	if (!hyperv_detect_via_acpi() && !hyperv_detect_via_smccc())
		return 0;

	/* Setup the guest ID */
	guest_id = hv_generate_guest_id(LINUX_VERSION_CODE);
	hv_set_vpreg(HV_REGISTER_GUEST_OS_ID, guest_id);

	/* Get the features and hints from Hyper-V */
	hv_get_vpreg_128(HV_REGISTER_PRIVILEGES_AND_FEATURES_INFO, &result);
	ms_hyperv.features = result.as32.a;
	ms_hyperv.priv_high = result.as32.b;
	ms_hyperv.misc_features = result.as32.c;

	hv_get_vpreg_128(HV_REGISTER_FEATURES_INFO, &result);
	ms_hyperv.hints = result.as32.a;

	pr_info("Hyper-V: privilege flags low 0x%x, high 0x%x, hints 0x%x, misc 0x%x\n",
		ms_hyperv.features, ms_hyperv.priv_high, ms_hyperv.hints,
		ms_hyperv.misc_features);

	hv_identify_partition_type();

	ret = hv_common_init();
	if (ret)
		return ret;

	ret = cpuhp_setup_state(CPUHP_AP_HYPERV_ONLINE, "arm64/hyperv_init:online",
				hv_common_cpu_init, hv_common_cpu_die);
	if (ret < 0) {
		hv_common_free();
		return ret;
	}

	if (ms_hyperv.priv_high & HV_ACCESS_PARTITION_ID)
		hv_get_partition_id();
	ms_hyperv.vtl = get_vtl();
	if (ms_hyperv.vtl > 0) /* non default VTL */
		pr_info("Linux runs in Hyper-V Virtual Trust Level %d\n", ms_hyperv.vtl);

	ms_hyperv_late_init();

	hyperv_initialized = true;
	return 0;
}

early_initcall(hyperv_init);

bool hv_is_hyperv_initialized(void)
{
	return hyperv_initialized;
}
EXPORT_SYMBOL_GPL(hv_is_hyperv_initialized);

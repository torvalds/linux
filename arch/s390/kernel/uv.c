// SPDX-License-Identifier: GPL-2.0
/*
 * Common Ultravisor functions and initialization
 *
 * Copyright IBM Corp. 2019, 2020
 */
#define KMSG_COMPONENT "prot_virt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/bitmap.h>
#include <linux/memblock.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/uv.h>

/* the bootdata_preserved fields come from ones in arch/s390/boot/uv.c */
#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
int __bootdata_preserved(prot_virt_guest);
#endif

#if IS_ENABLED(CONFIG_KVM)
int prot_virt_host;
EXPORT_SYMBOL(prot_virt_host);
struct uv_info __bootdata_preserved(uv_info);
EXPORT_SYMBOL(uv_info);

static int __init prot_virt_setup(char *val)
{
	bool enabled;
	int rc;

	rc = kstrtobool(val, &enabled);
	if (!rc && enabled)
		prot_virt_host = 1;

	if (is_prot_virt_guest() && prot_virt_host) {
		prot_virt_host = 0;
		pr_warn("Protected virtualization not available in protected guests.");
	}

	if (prot_virt_host && !test_facility(158)) {
		prot_virt_host = 0;
		pr_warn("Protected virtualization not supported by the hardware.");
	}

	return rc;
}
early_param("prot_virt", prot_virt_setup);
#endif

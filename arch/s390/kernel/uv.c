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

static int __init uv_init(unsigned long stor_base, unsigned long stor_len)
{
	struct uv_cb_init uvcb = {
		.header.cmd = UVC_CMD_INIT_UV,
		.header.len = sizeof(uvcb),
		.stor_origin = stor_base,
		.stor_len = stor_len,
	};

	if (uv_call(0, (uint64_t)&uvcb)) {
		pr_err("Ultravisor init failed with rc: 0x%x rrc: 0%x\n",
		       uvcb.header.rc, uvcb.header.rrc);
		return -1;
	}
	return 0;
}

void __init setup_uv(void)
{
	unsigned long uv_stor_base;

	uv_stor_base = (unsigned long)memblock_alloc_try_nid(
		uv_info.uv_base_stor_len, SZ_1M, SZ_2G,
		MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
	if (!uv_stor_base) {
		pr_warn("Failed to reserve %lu bytes for ultravisor base storage\n",
			uv_info.uv_base_stor_len);
		goto fail;
	}

	if (uv_init(uv_stor_base, uv_info.uv_base_stor_len)) {
		memblock_free(uv_stor_base, uv_info.uv_base_stor_len);
		goto fail;
	}

	pr_info("Reserving %luMB as ultravisor base storage\n",
		uv_info.uv_base_stor_len >> 20);
	return;
fail:
	pr_info("Disabling support for protected virtualization");
	prot_virt_host = 0;
}

void adjust_to_uv_max(unsigned long *vmax)
{
	*vmax = min_t(unsigned long, *vmax, uv_info.max_sec_stor_addr);
}
#endif

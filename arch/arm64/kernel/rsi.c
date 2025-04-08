// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Ltd.
 */

#include <linux/jump_label.h>
#include <linux/memblock.h>
#include <linux/psci.h>
#include <linux/swiotlb.h>
#include <linux/cc_platform.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/mem_encrypt.h>
#include <asm/rsi.h>

static struct realm_config config;

unsigned long prot_ns_shared;
EXPORT_SYMBOL(prot_ns_shared);

DEFINE_STATIC_KEY_FALSE_RO(rsi_present);
EXPORT_SYMBOL(rsi_present);

bool cc_platform_has(enum cc_attr attr)
{
	switch (attr) {
	case CC_ATTR_MEM_ENCRYPT:
		return is_realm_world();
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(cc_platform_has);

static bool rsi_version_matches(void)
{
	unsigned long ver_lower, ver_higher;
	unsigned long ret = rsi_request_version(RSI_ABI_VERSION,
						&ver_lower,
						&ver_higher);

	if (ret == SMCCC_RET_NOT_SUPPORTED)
		return false;

	if (ret != RSI_SUCCESS) {
		pr_err("RME: RMM doesn't support RSI version %lu.%lu. Supported range: %lu.%lu-%lu.%lu\n",
		       RSI_ABI_VERSION_MAJOR, RSI_ABI_VERSION_MINOR,
		       RSI_ABI_VERSION_GET_MAJOR(ver_lower),
		       RSI_ABI_VERSION_GET_MINOR(ver_lower),
		       RSI_ABI_VERSION_GET_MAJOR(ver_higher),
		       RSI_ABI_VERSION_GET_MINOR(ver_higher));
		return false;
	}

	pr_info("RME: Using RSI version %lu.%lu\n",
		RSI_ABI_VERSION_GET_MAJOR(ver_lower),
		RSI_ABI_VERSION_GET_MINOR(ver_lower));

	return true;
}

static void __init arm64_rsi_setup_memory(void)
{
	u64 i;
	phys_addr_t start, end;

	/*
	 * Iterate over the available memory ranges and convert the state to
	 * protected memory. We should take extra care to ensure that we DO NOT
	 * permit any "DESTROYED" pages to be converted to "RAM".
	 *
	 * panic() is used because if the attempt to switch the memory to
	 * protected has failed here, then future accesses to the memory are
	 * simply going to be reflected as a SEA (Synchronous External Abort)
	 * which we can't handle.  Bailing out early prevents the guest limping
	 * on and dying later.
	 */
	for_each_mem_range(i, &start, &end) {
		if (rsi_set_memory_range_protected_safe(start, end)) {
			panic("Failed to set memory range to protected: %pa-%pa",
			      &start, &end);
		}
	}
}

bool __arm64_is_protected_mmio(phys_addr_t base, size_t size)
{
	enum ripas ripas;
	phys_addr_t end, top;

	/* Overflow ? */
	if (WARN_ON(base + size <= base))
		return false;

	end = ALIGN(base + size, RSI_GRANULE_SIZE);
	base = ALIGN_DOWN(base, RSI_GRANULE_SIZE);

	while (base < end) {
		if (WARN_ON(rsi_ipa_state_get(base, end, &ripas, &top)))
			break;
		if (WARN_ON(top <= base))
			break;
		if (ripas != RSI_RIPAS_DEV)
			break;
		base = top;
	}

	return base >= end;
}
EXPORT_SYMBOL(__arm64_is_protected_mmio);

static int realm_ioremap_hook(phys_addr_t phys, size_t size, pgprot_t *prot)
{
	if (__arm64_is_protected_mmio(phys, size))
		*prot = pgprot_encrypted(*prot);
	else
		*prot = pgprot_decrypted(*prot);

	return 0;
}

void __init arm64_rsi_init(void)
{
	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_SMC)
		return;
	if (!rsi_version_matches())
		return;
	if (WARN_ON(rsi_get_realm_config(&config)))
		return;
	prot_ns_shared = BIT(config.ipa_bits - 1);

	if (arm64_ioremap_prot_hook_register(realm_ioremap_hook))
		return;

	if (realm_register_memory_enc_ops())
		return;

	arm64_rsi_setup_memory();

	static_branch_enable(&rsi_present);
}

static struct platform_device rsi_dev = {
	.name = RSI_PDEV_NAME,
	.id = PLATFORM_DEVID_NONE
};

static int __init arm64_create_dummy_rsi_dev(void)
{
	if (is_realm_world() &&
	    platform_device_register(&rsi_dev))
		pr_err("failed to register rsi platform device\n");
	return 0;
}

arch_initcall(arm64_create_dummy_rsi_dev)

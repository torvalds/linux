// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for the hypercall interface exposed to protected guests by
 * pKVM.
 *
 * Author: Will Deacon <will@kernel.org>
 * Copyright (C) 2024 Google LLC
 */

#include <linux/arm-smccc.h>
#include <linux/array_size.h>
#include <linux/io.h>
#include <linux/mem_encrypt.h>
#include <linux/mm.h>
#include <linux/pgtable.h>

#include <asm/hypervisor.h>

static size_t pkvm_granule;

static int arm_smccc_do_one_page(u32 func_id, phys_addr_t phys)
{
	phys_addr_t end = phys + PAGE_SIZE;

	while (phys < end) {
		struct arm_smccc_res res;

		arm_smccc_1_1_invoke(func_id, phys, 0, 0, &res);
		if (res.a0 != SMCCC_RET_SUCCESS)
			return -EPERM;

		phys += pkvm_granule;
	}

	return 0;
}

static int __set_memory_range(u32 func_id, unsigned long start, int numpages)
{
	void *addr = (void *)start, *end = addr + numpages * PAGE_SIZE;

	while (addr < end) {
		int err;

		err = arm_smccc_do_one_page(func_id, virt_to_phys(addr));
		if (err)
			return err;

		addr += PAGE_SIZE;
	}

	return 0;
}

static int pkvm_set_memory_encrypted(unsigned long addr, int numpages)
{
	return __set_memory_range(ARM_SMCCC_VENDOR_HYP_KVM_MEM_UNSHARE_FUNC_ID,
				  addr, numpages);
}

static int pkvm_set_memory_decrypted(unsigned long addr, int numpages)
{
	return __set_memory_range(ARM_SMCCC_VENDOR_HYP_KVM_MEM_SHARE_FUNC_ID,
				  addr, numpages);
}

static const struct arm64_mem_crypt_ops pkvm_crypt_ops = {
	.encrypt	= pkvm_set_memory_encrypted,
	.decrypt	= pkvm_set_memory_decrypted,
};

static int mmio_guard_ioremap_hook(phys_addr_t phys, size_t size,
				   pgprot_t *prot)
{
	phys_addr_t end;
	pteval_t protval = pgprot_val(*prot);

	/*
	 * We only expect MMIO emulation for regions mapped with device
	 * attributes.
	 */
	if (protval != PROT_DEVICE_nGnRE && protval != PROT_DEVICE_nGnRnE)
		return 0;

	phys = PAGE_ALIGN_DOWN(phys);
	end = phys + PAGE_ALIGN(size);

	while (phys < end) {
		const int func_id = ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_FUNC_ID;

		WARN_ON_ONCE(arm_smccc_do_one_page(func_id, phys));
		phys += PAGE_SIZE;
	}

	return 0;
}

void pkvm_init_hyp_services(void)
{
	int i;
	struct arm_smccc_res res;
	const u32 funcs[] = {
		ARM_SMCCC_KVM_FUNC_HYP_MEMINFO,
		ARM_SMCCC_KVM_FUNC_MEM_SHARE,
		ARM_SMCCC_KVM_FUNC_MEM_UNSHARE,
	};

	for (i = 0; i < ARRAY_SIZE(funcs); ++i) {
		if (!kvm_arm_hyp_service_available(funcs[i]))
			return;
	}

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID,
			     0, 0, 0, &res);
	if (res.a0 > PAGE_SIZE) /* Includes error codes */
		return;

	pkvm_granule = res.a0;
	arm64_mem_crypt_ops_register(&pkvm_crypt_ops);

	if (kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MMIO_GUARD))
		arm64_ioremap_prot_hook_register(&mmio_guard_ioremap_hook);
}

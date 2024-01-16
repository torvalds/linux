/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of the memory encryption/decryption API.
 *
 * Amusingly, no crypto is actually performed. Rather, we call into the
 * hypervisor component of KVM to expose pages selectively to the host
 * for virtio "DMA" operations. In other words, "encrypted" pages are
 * not accessible to the host, whereas "decrypted" pages are.
 *
 * Author: Will Deacon <will@kernel.org>
 */
#include <linux/arm-smccc.h>
#include <linux/mem_encrypt.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/set_memory.h>
#include <linux/types.h>

#include <asm/hypervisor.h>

#ifndef ARM_SMCCC_KVM_FUNC_HYP_MEMINFO
#define ARM_SMCCC_KVM_FUNC_HYP_MEMINFO	2

#define ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_HYP_MEMINFO)
#endif	/* ARM_SMCCC_KVM_FUNC_HYP_MEMINFO */

#ifndef ARM_SMCCC_KVM_FUNC_MEM_SHARE
#define ARM_SMCCC_KVM_FUNC_MEM_SHARE	3

#define ARM_SMCCC_VENDOR_HYP_KVM_MEM_SHARE_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MEM_SHARE)
#endif	/* ARM_SMCCC_KVM_FUNC_MEM_SHARE */

#ifndef ARM_SMCCC_KVM_FUNC_MEM_UNSHARE
#define ARM_SMCCC_KVM_FUNC_MEM_UNSHARE	4

#define ARM_SMCCC_VENDOR_HYP_KVM_MEM_UNSHARE_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MEM_UNSHARE)
#endif	/* ARM_SMCCC_KVM_FUNC_MEM_UNSHARE */

static unsigned long memshare_granule_sz;

bool mem_encrypt_active(void)
{
	return memshare_granule_sz;
}
EXPORT_SYMBOL(mem_encrypt_active);

void kvm_init_memshare_services(void)
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

	memshare_granule_sz = res.a0;
}

static int arm_smccc_share_unshare_page(u32 func_id, phys_addr_t phys)
{
	phys_addr_t end = phys + PAGE_SIZE;

	while (phys < end) {
		struct arm_smccc_res res;

		arm_smccc_1_1_invoke(func_id, phys, 0, 0, &res);
		if (res.a0 != SMCCC_RET_SUCCESS)
			return -EPERM;

		phys += memshare_granule_sz;
	}

	return 0;
}

static int set_memory_xcrypted(u32 func_id, unsigned long start, int numpages)
{
	void *addr = (void *)start, *end = addr + numpages * PAGE_SIZE;

	while (addr < end) {
		int err;

		err = arm_smccc_share_unshare_page(func_id, virt_to_phys(addr));
		if (err)
			return err;

		addr += PAGE_SIZE;
	}

	return 0;
}

int set_memory_encrypted(unsigned long addr, int numpages)
{
	if (!memshare_granule_sz || WARN_ON(!PAGE_ALIGNED(addr)))
		return 0;

	return set_memory_xcrypted(ARM_SMCCC_VENDOR_HYP_KVM_MEM_UNSHARE_FUNC_ID,
				   addr, numpages);
}
EXPORT_SYMBOL_GPL(set_memory_encrypted);

int set_memory_decrypted(unsigned long addr, int numpages)
{
	if (!memshare_granule_sz || WARN_ON(!PAGE_ALIGNED(addr)))
		return 0;

	return set_memory_xcrypted(ARM_SMCCC_VENDOR_HYP_KVM_MEM_SHARE_FUNC_ID,
				   addr, numpages);
}
EXPORT_SYMBOL_GPL(set_memory_decrypted);

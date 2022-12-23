/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_PKVM_MODULE_H__
#define __ARM64_KVM_PKVM_MODULE_H__

#include <asm/kvm_pgtable.h>
#include <linux/export.h>

typedef void (*dyn_hcall_t)(struct kvm_cpu_context *);

enum pkvm_psci_notification {
	PKVM_PSCI_CPU_SUSPEND,
	PKVM_PSCI_SYSTEM_SUSPEND,
	PKVM_PSCI_CPU_ENTRY,
};

struct pkvm_module_ops {
	int (*create_private_mapping)(phys_addr_t phys, size_t size,
				      enum kvm_pgtable_prot prot,
				      unsigned long *haddr);
	int (*register_serial_driver)(void (*hyp_putc_cb)(char));
	void (*puts)(const char *str);
	void (*putx64)(u64 num);
	void *(*fixmap_map)(phys_addr_t phys);
	void (*fixmap_unmap)(void);
	void *(*linear_map_early)(phys_addr_t phys, size_t size, enum kvm_pgtable_prot prot);
	void (*linear_unmap_early)(void *addr, size_t size);
	void (*flush_dcache_to_poc)(void *addr, size_t size);
	int (*register_host_perm_fault_handler)(int (*cb)(struct kvm_cpu_context *ctxt, u64 esr, u64 addr));
	int (*protect_host_page)(u64 pfn, enum kvm_pgtable_prot prot);
	int (*register_host_smc_handler)(bool (*cb)(struct kvm_cpu_context *));
	int (*register_default_trap_handler)(bool (*cb)(struct kvm_cpu_context *));
	int (*register_illegal_abt_notifier)(void (*cb)(struct kvm_cpu_context *));
	int (*register_psci_notifier)(void (*cb)(enum pkvm_psci_notification, struct kvm_cpu_context *));
	int (*register_hyp_panic_notifier)(void (*cb)(struct kvm_cpu_context *host_ctxt));
};

struct pkvm_module_section {
	void *start;
	void *end;
};

typedef s32 kvm_nvhe_reloc_t;

struct pkvm_el2_module {
	struct pkvm_module_section text;
	struct pkvm_module_section bss;
	struct pkvm_module_section rodata;
	struct pkvm_module_section data;
	kvm_nvhe_reloc_t *relocs;
	unsigned int nr_relocs;
	int (*init)(const struct pkvm_module_ops *ops);
};

int __pkvm_load_el2_module(struct pkvm_el2_module *mod, struct module *this,
			   unsigned long *token);

int __pkvm_register_el2_call(dyn_hcall_t hfn, unsigned long token,
			     unsigned long hyp_text_kern_va);
#ifdef MODULE
#define pkvm_load_el2_module(init_fn, token)				\
({									\
	extern char __kvm_nvhe___hypmod_text_start[];			\
	extern char __kvm_nvhe___hypmod_text_end[];			\
	extern char __kvm_nvhe___hypmod_bss_start[];			\
	extern char __kvm_nvhe___hypmod_bss_end[];			\
	extern char __kvm_nvhe___hypmod_rodata_start[];			\
	extern char __kvm_nvhe___hypmod_rodata_end[];			\
	extern char __kvm_nvhe___hypmod_data_start[];			\
	extern char __kvm_nvhe___hypmod_data_end[];			\
	extern char __kvm_nvhe___hyprel_start[];			\
	extern char __kvm_nvhe___hyprel_end[];				\
	struct pkvm_el2_module mod;					\
									\
	mod.text.start		= __kvm_nvhe___hypmod_text_start;	\
	mod.text.end		= __kvm_nvhe___hypmod_text_end;		\
	mod.bss.start		= __kvm_nvhe___hypmod_bss_start;	\
	mod.bss.end		= __kvm_nvhe___hypmod_bss_end;		\
	mod.rodata.start	= __kvm_nvhe___hypmod_rodata_start;	\
	mod.rodata.end		= __kvm_nvhe___hypmod_rodata_end;	\
	mod.data.start		= __kvm_nvhe___hypmod_data_start;	\
	mod.data.end		= __kvm_nvhe___hypmod_data_end;		\
	mod.relocs		= (kvm_nvhe_reloc_t *)__kvm_nvhe___hyprel_start; \
	mod.nr_relocs		= (__kvm_nvhe___hyprel_end - __kvm_nvhe___hyprel_start) / \
				  sizeof(*mod.relocs);			\
	mod.init = init_fn;						\
									\
	__pkvm_load_el2_module(&mod, THIS_MODULE, token);		\
})

#define pkvm_register_el2_mod_call(hfn, token)				\
({									\
	extern char __kvm_nvhe___hypmod_text_start[];			\
	unsigned long hyp_text_kern_va = 				\
		(unsigned long)__kvm_nvhe___hypmod_text_start;		\
	__pkvm_register_el2_call(hfn, token,				\
				 hyp_text_kern_va);			\
})

#define pkvm_el2_mod_call(id, ...)					\
	({								\
		struct arm_smccc_res res;				\
									\
		arm_smccc_1_1_hvc(KVM_HOST_SMCCC_ID(id),		\
				  ##__VA_ARGS__, &res);			\
		WARN_ON(res.a0 != SMCCC_RET_SUCCESS);			\
									\
		res.a1;							\
	})
#endif
#endif

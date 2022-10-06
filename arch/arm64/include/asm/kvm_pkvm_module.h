/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_PKVM_MODULE_H__
#define __ARM64_KVM_PKVM_MODULE_H__

#include <linux/export.h>

struct pkvm_module_ops {
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

#ifdef MODULE
int __pkvm_load_el2_module(struct pkvm_el2_module *mod, struct module *this);

#define pkvm_load_el2_module(init_fn)					\
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
	__pkvm_load_el2_module(&mod, THIS_MODULE);			\
})
#endif
#endif

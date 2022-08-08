/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google LLC.
 * Written by David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_HYP_IMAGE_H__
#define __ARM64_HYP_IMAGE_H__

#define __HYP_CONCAT(a, b)	a ## b
#define HYP_CONCAT(a, b)	__HYP_CONCAT(a, b)

/*
 * KVM nVHE code has its own symbol namespace prefixed with __kvm_nvhe_,
 * to separate it from the kernel proper.
 */
#define kvm_nvhe_sym(sym)	__kvm_nvhe_##sym

#ifdef LINKER_SCRIPT

/*
 * KVM nVHE ELF section names are prefixed with .hyp, to separate them
 * from the kernel proper.
 */
#define HYP_SECTION_NAME(NAME)	.hyp##NAME

/* Symbol defined at the beginning of each hyp section. */
#define HYP_SECTION_SYMBOL_NAME(NAME) \
	HYP_CONCAT(__hyp_section_, HYP_SECTION_NAME(NAME))

/*
 * Helper to generate linker script statements starting a hyp section.
 *
 * A symbol with a well-known name is defined at the first byte. This
 * is used as a base for hyp relocations (see gen-hyprel.c). It must
 * be defined inside the section so the linker of `vmlinux` cannot
 * separate it from the section data.
 */
#define BEGIN_HYP_SECTION(NAME)				\
	HYP_SECTION_NAME(NAME) : {			\
		HYP_SECTION_SYMBOL_NAME(NAME) = .;

/* Helper to generate linker script statements ending a hyp section. */
#define END_HYP_SECTION					\
	}

/* Defines an ELF hyp section from input section @NAME and its subsections. */
#define HYP_SECTION(NAME)			\
	BEGIN_HYP_SECTION(NAME)			\
		*(NAME NAME##.*)		\
	END_HYP_SECTION

/*
 * Defines a linker script alias of a kernel-proper symbol referenced by
 * KVM nVHE hyp code.
 */
#define KVM_NVHE_ALIAS(sym)	kvm_nvhe_sym(sym) = sym;

#endif /* LINKER_SCRIPT */

#endif /* __ARM64_HYP_IMAGE_H__ */

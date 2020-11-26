/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google LLC.
 * Written by David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_HYP_IMAGE_H__
#define __ARM64_HYP_IMAGE_H__

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

/* Defines an ELF hyp section from input section @NAME and its subsections. */
#define HYP_SECTION(NAME) \
	HYP_SECTION_NAME(NAME) : { *(NAME NAME##.*) }

/*
 * Defines a linker script alias of a kernel-proper symbol referenced by
 * KVM nVHE hyp code.
 */
#define KVM_NVHE_ALIAS(sym)	kvm_nvhe_sym(sym) = sym;

#endif /* LINKER_SCRIPT */

#endif /* __ARM64_HYP_IMAGE_H__ */

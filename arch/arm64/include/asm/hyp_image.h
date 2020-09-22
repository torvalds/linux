/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google LLC.
 * Written by David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_HYP_IMAGE_H__
#define __ARM64_HYP_IMAGE_H__

#ifdef LINKER_SCRIPT

/*
 * KVM nVHE ELF section names are prefixed with .hyp, to separate them
 * from the kernel proper.
 */
#define HYP_SECTION_NAME(NAME)	.hyp##NAME

/* Defines an ELF hyp section from input section @NAME and its subsections. */
#define HYP_SECTION(NAME) \
	HYP_SECTION_NAME(NAME) : { *(NAME NAME##.*) }

#endif /* LINKER_SCRIPT */

#endif /* __ARM64_HYP_IMAGE_H__ */

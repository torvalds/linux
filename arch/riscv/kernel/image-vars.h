/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 * Linker script variables to be set after section resolution, as
 * ld.lld does not like variables assigned before SECTIONS is processed.
 * Based on arch/arm64/kernel/image-vars.h
 */
#ifndef __RISCV_KERNEL_IMAGE_VARS_H
#define __RISCV_KERNEL_IMAGE_VARS_H

#ifndef LINKER_SCRIPT
#error This file should only be included in vmlinux.lds.S
#endif

#ifdef CONFIG_EFI

/*
 * The EFI stub has its own symbol namespace prefixed by __efistub_, to
 * isolate it from the kernel proper. The following symbols are legally
 * accessed by the stub, so provide some aliases to make them accessible.
 * Only include data symbols here, or text symbols of functions that are
 * guaranteed to be safe when executed at another offset than they were
 * linked at. The routines below are all implemented in assembler in a
 * position independent manner
 */
__efistub_memcmp		= memcmp;
__efistub_memchr		= memchr;
__efistub_memcpy		= memcpy;
__efistub_memmove		= memmove;
__efistub_memset		= memset;
__efistub_strlen		= strlen;
__efistub_strnlen		= strnlen;
__efistub_strcmp		= strcmp;
__efistub_strncmp		= strncmp;
__efistub_strrchr		= strrchr;

#ifdef CONFIG_KASAN
__efistub___memcpy		= memcpy;
__efistub___memmove		= memmove;
__efistub___memset		= memset;
#endif

__efistub__start		= _start;
__efistub__start_kernel		= _start_kernel;
__efistub__end			= _end;
__efistub__edata		= _edata;
__efistub_screen_info		= screen_info;

#endif

#endif /* __RISCV_KERNEL_IMAGE_VARS_H */

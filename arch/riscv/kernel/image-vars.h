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
__efistub__start		= _start;
__efistub__start_kernel		= _start_kernel;
__efistub__end			= _end;
__efistub__edata		= _edata;
__efistub___init_text_end	= __init_text_end;
#if defined(CONFIG_EFI_EARLYCON) || defined(CONFIG_SYSFB)
__efistub_sysfb_primary_display	= sysfb_primary_display;
#endif

/*
 * These double-word integer shifts are used by the library code, and
 * the first two of them are required to link EFI stub. Note __ashrdi3()
 * is not actually used by the stub but this may change in the future.
 */
PROVIDE(__efistub___lshrdi3	= __lshrdi3);
PROVIDE(__efistub___ashldi3	= __ashldi3);
PROVIDE(__efistub___ashrdi3	= __ashrdi3);

#endif

#endif /* __RISCV_KERNEL_IMAGE_VARS_H */

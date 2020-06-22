/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_ISYS_IRQ_H__
#define __IA_CSS_ISYS_IRQ_H__

#include <type_support.h>
#include <system_local.h>

#if defined(USE_INPUT_SYSTEM_VERSION_2401)

#ifndef __INLINE_ISYS2401_IRQ__

#define STORAGE_CLASS_ISYS2401_IRQ_H extern
#define STORAGE_CLASS_ISYS2401_IRQ_C extern
#include "isys_irq_public.h"

#else  /* __INLINE_ISYS2401_IRQ__ */

#define STORAGE_CLASS_ISYS2401_IRQ_H static inline
#define STORAGE_CLASS_ISYS2401_IRQ_C static inline
#include "isys_irq_private.h"

#endif /* __INLINE_ISYS2401_IRQ__ */

#endif /* defined(USE_INPUT_SYSTEM_VERSION_2401) */

#endif	/* __IA_CSS_ISYS_IRQ_H__ */

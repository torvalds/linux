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

#ifndef __SP_H_INCLUDED__
#define __SP_H_INCLUDED__

/*
 * This file is included on every cell {SP,ISP,host} and on every system
 * that uses the SP cell. It defines the API to DLI bridge
 *
 * System and cell specific interfaces and inline code are included
 * conditionally through Makefile path settings.
 *
 *  - .        system and cell agnostic interfaces, constants and identifiers
 *	- public:  system agnostic, cell specific interfaces
 *	- private: system dependent, cell specific interfaces & inline implementations
 *	- global:  system specific constants and identifiers
 *	- local:   system and cell specific constants and identifiers
 */

#include "storage_class.h"

#include "system_local.h"
#include "sp_local.h"

#ifndef __INLINE_SP__
#define STORAGE_CLASS_SP_H STORAGE_CLASS_EXTERN
#define STORAGE_CLASS_SP_C 
#include "sp_public.h"
#else  /* __INLINE_SP__ */
#define STORAGE_CLASS_SP_H STORAGE_CLASS_INLINE
#define STORAGE_CLASS_SP_C STORAGE_CLASS_INLINE
#include "sp_private.h"
#endif /* __INLINE_SP__ */

#endif /* __SP_H_INCLUDED__ */

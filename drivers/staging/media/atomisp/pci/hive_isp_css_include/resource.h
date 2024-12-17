/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __RESOURCE_H_INCLUDED__
#define __RESOURCE_H_INCLUDED__

/*
 * This file is included on every cell {SP,ISP,host} and on every system
 * that uses a RESOURCE manager. It defines the API to DLI bridge
 *
 * System and cell specific interfaces and inline code are included
 * conditionally through Makefile path settings.
 *
 *  - .        system and cell agnostic interfaces, constants and identifiers
 *	- public:  system agnostic, cell specific interfaces
 *	- private: system dependent, cell specific interfaces & inline implementations
 *	- global:  system specific constants and identifiers
 *	- local:   system and cell specific constants and identifiers
 *
 */

#include "system_local.h"
#include "resource_local.h"

#ifndef __INLINE_RESOURCE__
#define STORAGE_CLASS_RESOURCE_H extern
#define STORAGE_CLASS_RESOURCE_C
#include "resource_public.h"
#else  /* __INLINE_RESOURCE__ */
#define STORAGE_CLASS_RESOURCE_H static inline
#define STORAGE_CLASS_RESOURCE_C static inline
#include "resource_private.h"
#endif /* __INLINE_RESOURCE__ */

#endif /* __RESOURCE_H_INCLUDED__ */

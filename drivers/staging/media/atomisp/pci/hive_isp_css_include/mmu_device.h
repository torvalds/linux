/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __MMU_DEVICE_H_INCLUDED__
#define __MMU_DEVICE_H_INCLUDED__

/* The file mmu.h already exists */

/*
 * This file is included on every cell {SP,ISP,host} and on every system
 * that uses the MMU device. It defines the API to DLI bridge
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

#include "system_local.h"
#include "mmu_local.h"

#include "mmu_public.h"

#endif /* __MMU_DEVICE_H_INCLUDED__ */

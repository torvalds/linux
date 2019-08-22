/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ultravisor API.
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#ifndef _ASM_POWERPC_ULTRAVISOR_API_H
#define _ASM_POWERPC_ULTRAVISOR_API_H

#include <asm/hvcall.h>

/* Return codes */
#define U_FUNCTION		H_FUNCTION
#define U_NOT_AVAILABLE		H_NOT_AVAILABLE
#define U_P2			H_P2
#define U_P3			H_P3
#define U_P4			H_P4
#define U_P5			H_P5
#define U_PARAMETER		H_PARAMETER
#define U_SUCCESS		H_SUCCESS

#endif /* _ASM_POWERPC_ULTRAVISOR_API_H */

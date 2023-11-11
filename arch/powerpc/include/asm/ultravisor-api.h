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
#define U_BUSY			H_BUSY
#define U_FUNCTION		H_FUNCTION
#define U_NOT_AVAILABLE		H_NOT_AVAILABLE
#define U_P2			H_P2
#define U_P3			H_P3
#define U_P4			H_P4
#define U_P5			H_P5
#define U_PARAMETER		H_PARAMETER
#define U_PERMISSION		H_PERMISSION
#define U_SUCCESS		H_SUCCESS

/* opcodes */
#define UV_WRITE_PATE			0xF104
#define UV_RETURN			0xF11C
#define UV_ESM				0xF110
#define UV_REGISTER_MEM_SLOT		0xF120
#define UV_UNREGISTER_MEM_SLOT		0xF124
#define UV_PAGE_IN			0xF128
#define UV_PAGE_OUT			0xF12C
#define UV_SHARE_PAGE			0xF130
#define UV_UNSHARE_PAGE			0xF134
#define UV_UNSHARE_ALL_PAGES		0xF140
#define UV_PAGE_INVAL			0xF138
#define UV_SVM_TERMINATE		0xF13C

#endif /* _ASM_POWERPC_ULTRAVISOR_API_H */

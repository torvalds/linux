// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef __DML2_DEBUG_H__
#define __DML2_DEBUG_H__

#ifdef _DEBUG
#define DML2_ASSERT(condition) dml2_assert(condition)
#else
#define DML2_ASSERT(condition)
#endif

int dml2_printf(const char *format, ...);
void dml2_assert(int condition);

#endif
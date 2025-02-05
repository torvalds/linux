// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_debug.h"

int dml2_log_internal(const char *format, ...)
{
	return 0;
}

int dml2_printf(const char *format, ...)
{
#ifdef _DEBUG
#ifdef _DEBUG_PRINTS
	int result;
	va_list args;
	va_start(args, format);

	result = vprintf(format, args);

	va_end(args);

	return result;
#else
	return 0;
#endif
#else
	return 0;
#endif
}

void dml2_assert(int condition)
{
	//ASSERT(condition);
}

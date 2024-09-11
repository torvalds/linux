/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef SPL_DEBUG_H
#define SPL_DEBUG_H

#ifdef SPL_ASSERT
#undef SPL_ASSERT
#endif
#define SPL_ASSERT(b)

#define SPL_ASSERT_CRITICAL(expr)  do {if (expr)/* Do nothing */; } while (0)

#ifdef SPL_DALMSG
#undef SPL_DALMSG
#endif
#define SPL_DALMSG(b)

#ifdef SPL_DAL_ASSERT_MSG
#undef SPL_DAL_ASSERT_MSG
#endif
#define SPL_DAL_ASSERT_MSG(b, m)

#endif  // SPL_DEBUG_H

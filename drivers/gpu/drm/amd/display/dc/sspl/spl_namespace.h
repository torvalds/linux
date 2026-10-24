/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef _SPL_NAMESPACE_H_
#define _SPL_NAMESPACE_H_

/* SPL namespace macros */
#ifndef SPL_PFX_
#define SPL_PFX_
#endif

#define SPL_EXPAND2(a, b)         a##b
#define SPL_EXPAND(a, b)          SPL_EXPAND2(a, b)
#define SPL_NAMESPACE(symbol)     SPL_EXPAND(SPL_PFX_, symbol)

#endif /* _SPL_NAMESPACE_H_ */

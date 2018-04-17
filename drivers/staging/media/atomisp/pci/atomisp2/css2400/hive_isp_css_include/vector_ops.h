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

#ifndef __VECTOR_OPS_H_INCLUDED__
#define __VECTOR_OPS_H_INCLUDED__


#include "vector_ops_local.h"

#ifndef __INLINE_VECTOR_OPS__
#define STORAGE_CLASS_VECTOR_OPS_H extern
#define STORAGE_CLASS_VECTOR_OPS_C
#include "vector_ops_public.h"
#else  /* __INLINE_VECTOR_OPS__ */
#define STORAGE_CLASS_VECTOR_OPS_H static inline
#define STORAGE_CLASS_VECTOR_OPS_C static inline
#include "vector_ops_private.h"
#endif /* __INLINE_VECTOR_OPS__ */

#endif /* __VECTOR_OPS_H_INCLUDED__ */

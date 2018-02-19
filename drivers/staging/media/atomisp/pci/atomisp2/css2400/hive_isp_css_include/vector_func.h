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

#ifndef __VECTOR_FUNC_H_INCLUDED__
#define __VECTOR_FUNC_H_INCLUDED__


/* TODO: Later filters will be moved to types directory,
 * and we should only include matrix_MxN types */
#include "filters/filters_1.0/filter_2x2.h"
#include "filters/filters_1.0/filter_3x3.h"
#include "filters/filters_1.0/filter_4x4.h"
#include "filters/filters_1.0/filter_5x5.h"

#include "vector_func_local.h"

#ifndef __INLINE_VECTOR_FUNC__
#define STORAGE_CLASS_VECTOR_FUNC_H extern
#define STORAGE_CLASS_VECTOR_FUNC_C 
#include "vector_func_public.h"
#else  /* __INLINE_VECTOR_FUNC__ */
#define STORAGE_CLASS_VECTOR_FUNC_H static inline
#define STORAGE_CLASS_VECTOR_FUNC_C static inline
#include "vector_func_private.h"
#endif /* __INLINE_VECTOR_FUNC__ */

#endif /* __VECTOR_FUNC_H_INCLUDED__ */

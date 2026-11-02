/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef _SPL_OS_TYPES_H_
#define _SPL_OS_TYPES_H_

#include "spl_debug.h"

#include <linux/slab.h>
#include <linux/kgdb.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mm.h>

#ifndef spl_min
#define spl_min(a, b)    (((a) < (b)) ? (a):(b))
#endif

#include "spl_namespace.h"

#endif /* _SPL_OS_TYPES_H_ */

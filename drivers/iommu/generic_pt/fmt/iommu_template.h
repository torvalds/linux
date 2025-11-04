/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES
 *
 * Template to build the iommu module and kunit from the format and
 * implementation headers.
 *
 * The format should have:
 *  #define PT_FMT <name>
 *  #define PT_SUPPORTED_FEATURES (BIT(PT_FEAT_xx) | BIT(PT_FEAT_yy))
 * And optionally:
 *  #define PT_FORCE_ENABLED_FEATURES ..
 *  #define PT_FMT_VARIANT <suffix>
 */
#include <linux/args.h>
#include <linux/stringify.h>

#ifdef PT_FMT_VARIANT
#define PTPFX_RAW \
	CONCATENATE(CONCATENATE(PT_FMT, _), PT_FMT_VARIANT)
#else
#define PTPFX_RAW PT_FMT
#endif

#define PTPFX CONCATENATE(PTPFX_RAW, _)

#define _PT_FMT_H PT_FMT.h
#define PT_FMT_H __stringify(_PT_FMT_H)

#define _PT_DEFS_H CONCATENATE(defs_, _PT_FMT_H)
#define PT_DEFS_H __stringify(_PT_DEFS_H)

#include <linux/generic_pt/common.h>
#include PT_DEFS_H
#include "../pt_defs.h"
#include PT_FMT_H
#include "../pt_common.h"

#ifndef GENERIC_PT_KUNIT
#include "../iommu_pt.h"
#else
/*
 * The makefile will compile the .c file twice, once with GENERIC_PT_KUNIT set
 * which means we are building the kunit modle.
 */
#include "../kunit_generic_pt.h"
#include "../kunit_iommu_pt.h"
#endif

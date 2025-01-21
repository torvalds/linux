/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __VMEM_GLOBAL_H_INCLUDED__
#define __VMEM_GLOBAL_H_INCLUDED__

#include "isp.h"

#define VMEM_SIZE	ISP_VMEM_DEPTH
#define VMEM_ELEMBITS	ISP_VMEM_ELEMBITS
#define VMEM_ALIGN	ISP_VMEM_ALIGN

#ifndef PIPE_GENERATION
typedef tvector *pvector;
#endif

#endif /* __VMEM_GLOBAL_H_INCLUDED__ */

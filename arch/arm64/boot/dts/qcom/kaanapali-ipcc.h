/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DTS_KAANAPALI_MAILBOX_IPCC_H
#define __DTS_KAANAPALI_MAILBOX_IPCC_H

/* Physical client IDs */
#define IPCC_MPROC_AOP			0
#define IPCC_MPROC_TZ			1
#define IPCC_MPROC_MPSS			2
#define IPCC_MPROC_LPASS		3
#define IPCC_MPROC_SDC			4
#define IPCC_MPROC_CDSP			5
#define IPCC_MPROC_APSS			6
#define IPCC_MPROC_SOCCP		13
#define IPCC_MPROC_DCP			14
#define IPCC_MPROC_SPSS			15
#define IPCC_MPROC_TME			16
#define IPCC_MPROC_WPSS			17

#define IPCC_COMPUTE_L0_CDSP		2
#define IPCC_COMPUTE_L0_APSS		3
#define IPCC_COMPUTE_L0_GPU		4
#define IPCC_COMPUTE_L0_CVP		8
#define IPCC_COMPUTE_L0_CAM		9
#define IPCC_COMPUTE_L0_CAM1		10
#define IPCC_COMPUTE_L0_DCP		11
#define IPCC_COMPUTE_L0_VPU		12
#define IPCC_COMPUTE_L0_SOCCP		16

#define IPCC_COMPUTE_L1_CDSP		2
#define IPCC_COMPUTE_L1_APSS		3
#define IPCC_COMPUTE_L1_GPU		4
#define IPCC_COMPUTE_L1_CVP		8
#define IPCC_COMPUTE_L1_CAM		9
#define IPCC_COMPUTE_L1_CAM1		10
#define IPCC_COMPUTE_L1_DCP		11
#define IPCC_COMPUTE_L1_VPU		12
#define IPCC_COMPUTE_L1_SOCCP		16

#define IPCC_PERIPH_CDSP		2
#define IPCC_PERIPH_APSS		3
#define IPCC_PERIPH_PCIE0		4
#define IPCC_PERIPH_PCIE1		5

#define IPCC_FENCE_CDSP			2
#define IPCC_FENCE_APSS			3
#define IPCC_FENCE_GPU			4
#define IPCC_FENCE_CVP			8
#define IPCC_FENCE_CAM			8
#define IPCC_FENCE_CAM1			10
#define IPCC_FENCE_DCP			11
#define IPCC_FENCE_VPU			20
#define IPCC_FENCE_SOCCP		24

#endif

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef __INTEL_DIP_REGS_H__
#define __INTEL_DIP_REGS_H__

#include "intel_display_reg_defs.h"

/* EMP (Extended Metadata Packet) AS (Adaptive Sync) SDP Transmission Line */
#define _EMP_AS_SDP_TL_A			0x60204
#define EMP_AS_SDP_TL(display, trans)		_MMIO_TRANS2((display), (trans), _EMP_AS_SDP_TL_A)
#define   EMP_AS_SDP_DB_TL_MASK			REG_GENMASK(12, 0)
#define   EMP_AS_SDP_DB_TL(db_transmit_line)	REG_FIELD_PREP(EMP_AS_SDP_DB_TL_MASK, \
							       (db_transmit_line))

/* COMMON SDP TRANSMISSION LINE */
#define _CMN_SDP_TL_A			0x6020c
#define CMN_SDP_TL(display, trans)	_MMIO_TRANS2(display, (trans), _CMN_SDP_TL_A)
#define  TRANSMISSION_LINE_ENABLE	REG_BIT(31)
#define  BASE_TRANSMISSION_LINE_MASK	REG_GENMASK(12, 0)
#define  BASE_TRANSMISSION_LINE(x)	REG_FIELD_PREP(BASE_TRANSMISSION_LINE_MASK, x)

#define _CMN_SDP_TL_STGR_CTL_A			0x60214
#define CMN_SDP_TL_STGR_CTL(display, trans)	_MMIO_TRANS2(display, (trans), \
							     _CMN_SDP_TL_STGR_CTL_A)
#define  VSC_EXT_STAGGER_MASK			REG_GENMASK(11, 8)
#define  VSC_EXT_STAGGER(x)			REG_FIELD_PREP(VSC_EXT_STAGGER_MASK, x)
#define  VSC_EXT_STAGGER_DEFAULT		0x2
#define  PPS_STAGGER_MASK			REG_GENMASK(7, 4)
#define  PPS_STAGGER(x)				REG_FIELD_PREP(PPS_STAGGER_MASK, x)
#define  PPS_STAGGER_DEFAULT			0x1
#define  GMP_STAGGER_MASK			REG_GENMASK(3, 0)
#define  GMP_STAGGER(x)				REG_FIELD_PREP(GMP_STAGGER_MASK, x)
#define  GMP_STAGGER_DEFAULT			0x0

#endif /* __INTEL_DIP_REGS_H__ */

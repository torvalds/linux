/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DSB_REGS_H__
#define __INTEL_DSB_REGS_H__

#include "intel_display_reg_defs.h"

/* This register controls the Display State Buffer (DSB) engines. */
#define _DSBSL_INSTANCE_BASE		0x70B00
#define DSBSL_INSTANCE(pipe, id)	(_DSBSL_INSTANCE_BASE + \
					 (pipe) * 0x1000 + (id) * 0x100)
#define DSB_HEAD(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x0)
#define DSB_TAIL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x4)
#define DSB_CTRL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x8)
#define   DSB_ENABLE			REG_BIT(31)
#define   DSB_BUF_REITERATE		REG_BIT(29)
#define   DSB_WAIT_FOR_VBLANK		REG_BIT(28)
#define   DSB_WAIT_FOR_LINE_IN		REG_BIT(27)
#define   DSB_HALT			REG_BIT(16)
#define   DSB_NON_POSTED		REG_BIT(8)
#define   DSB_STATUS_BUSY		REG_BIT(0)
#define DSB_MMIOCTRL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0xc)
#define   DSB_MMIO_DEAD_CLOCKS_ENABLE	REG_BIT(31)
#define   DSB_MMIO_DEAD_CLOCKS_COUNT_MASK	REG_GENMASK(15, 8)
#define   DSB_MMIO_DEAD_CLOCKS_COUNT(x)	REG_FIELD_PREP(DSB_MMIO_DEAD_CLOCK_COUNT_MASK, (x))
#define   DSB_MMIO_CYCLES_MASK		REG_GENMASK(7, 0)
#define   DSB_MMIO_CYCLES(x)		REG_FIELD_PREP(DSB_MMIO_CYCLES_MASK, (x))
#define DSB_POLLFUNC(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x10)
#define   DSB_POLL_ENABLE		REG_BIT(31)
#define   DSB_POLL_WAIT_MASK		REG_GENMASK(30, 23)
#define   DSB_POLL_WAIT(x)		REG_FIELD_PREP(DSB_POLL_WAIT_MASK, (x)) /* usec */
#define   DSB_POLL_COUNT_MASK		REG_GENMASK(22, 15)
#define   DSB_POLL_COUNT(x)		REG_FIELD_PREP(DSB_POLL_COUNT_MASK, (x))
#define DSB_DEBUG(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x14)
#define DSB_POLLMASK(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x1c)
#define DSB_STATUS(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x24)
#define DSB_INTERRUPT(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x28)
#define   DSB_ATS_FAULT_INT_EN		REG_BIT(20)
#define   DSB_GTT_FAULT_INT_EN		REG_BIT(19)
#define   DSB_RSPTIMEOUT_INT_EN		REG_BIT(18)
#define   DSB_POLL_ERR_INT_EN		REG_BIT(17)
#define   DSB_PROG_INT_EN		REG_BIT(16)
#define   DSB_ATS_FAULT_INT_STATUS	REG_BIT(4)
#define   DSB_GTT_FAULT_INT_STATUS	REG_BIT(3)
#define   DSB_RSPTIMEOUT_INT_STATUS	REG_BIT(2)
#define   DSB_POLL_ERR_INT_STATUS	REG_BIT(1)
#define   DSB_PROG_INT_STATUS		REG_BIT(0)
#define DSB_CURRENT_HEAD(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x2c)
#define DSB_RM_TIMEOUT(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x30)
#define   DSB_RM_CLAIM_TIMEOUT		REG_BIT(31)
#define   DSB_RM_READY_TIMEOUT		REG_BIT(30)
#define   DSB_RM_CLAIM_TIMEOUT_COUNT_MASK	REG_GENMASK(23, 16)
#define   DSB_RM_CLAIM_TIMEOUT_COUNT(x)	REG_FIELD_PREP(DSB_RM_CLAIM_TIMEOUT_COUNT_MASK, (x)) /* clocks */
#define   DSB_RM_READY_TIMEOUT_VALUE_MASK	REG_GENMASK(15, 0)
#define   DSB_RM_READY_TIMEOUT_VALUE(x)	REG_FIELD_PREP(DSB_RM_READY_TIMEOUT_VALUE, (x)) /* usec */
#define DSB_RMTIMEOUTREG_CAPTURE(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x34)
#define DSB_PMCTRL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x38)
#define DSB_PMCTRL_2(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x3c)
#define DSB_PF_LN_LOWER(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x40)
#define DSB_PF_LN_UPPER(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x44)
#define DSB_BUFRPT_CNT(pipe, id)	_MMIO(DSBSL_INSTANCE(pipe, id) + 0x48)
#define DSB_CHICKEN(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0xf0)

#endif /* __INTEL_DSB_REGS_H__ */

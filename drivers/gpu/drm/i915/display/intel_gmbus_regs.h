/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GMBUS_REGS_H__
#define __INTEL_GMBUS_REGS_H__

#include "i915_reg_defs.h"

#define GMBUS_MMIO_BASE(__i915) ((__i915)->display.gmbus.mmio_base)

#define GPIO(__i915, gpio)	_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5010 + 4 * (gpio))
#define   GPIO_CLOCK_DIR_MASK		(1 << 0)
#define   GPIO_CLOCK_DIR_IN		(0 << 1)
#define   GPIO_CLOCK_DIR_OUT		(1 << 1)
#define   GPIO_CLOCK_VAL_MASK		(1 << 2)
#define   GPIO_CLOCK_VAL_OUT		(1 << 3)
#define   GPIO_CLOCK_VAL_IN		(1 << 4)
#define   GPIO_CLOCK_PULLUP_DISABLE	(1 << 5)
#define   GPIO_DATA_DIR_MASK		(1 << 8)
#define   GPIO_DATA_DIR_IN		(0 << 9)
#define   GPIO_DATA_DIR_OUT		(1 << 9)
#define   GPIO_DATA_VAL_MASK		(1 << 10)
#define   GPIO_DATA_VAL_OUT		(1 << 11)
#define   GPIO_DATA_VAL_IN		(1 << 12)
#define   GPIO_DATA_PULLUP_DISABLE	(1 << 13)

/* clock/port select */
#define GMBUS0(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5100)
#define   GMBUS_AKSV_SELECT		(1 << 11)
#define   GMBUS_RATE_100KHZ		(0 << 8)
#define   GMBUS_RATE_50KHZ		(1 << 8)
#define   GMBUS_RATE_400KHZ		(2 << 8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ		(3 << 8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT		(1 << 7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_BYTE_CNT_OVERRIDE	(1 << 6)

/* command/status */
#define GMBUS1(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5104)
#define   GMBUS_SW_CLR_INT		(1 << 31)
#define   GMBUS_SW_RDY			(1 << 30)
#define   GMBUS_ENT			(1 << 29) /* enable timeout */
#define   GMBUS_CYCLE_NONE		(0 << 25)
#define   GMBUS_CYCLE_WAIT		(1 << 25)
#define   GMBUS_CYCLE_INDEX		(2 << 25)
#define   GMBUS_CYCLE_STOP		(4 << 25)
#define   GMBUS_BYTE_COUNT_SHIFT	16
#define   GMBUS_BYTE_COUNT_MAX		256U
#define   GEN9_GMBUS_BYTE_COUNT_MAX	511U
#define   GMBUS_SLAVE_INDEX_SHIFT	8
#define   GMBUS_SLAVE_ADDR_SHIFT	1
#define   GMBUS_SLAVE_READ		(1 << 0)
#define   GMBUS_SLAVE_WRITE		(0 << 0)

/* status */
#define GMBUS2(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5108)
#define   GMBUS_INUSE			(1 << 15)
#define   GMBUS_HW_WAIT_PHASE		(1 << 14)
#define   GMBUS_STALL_TIMEOUT		(1 << 13)
#define   GMBUS_INT			(1 << 12)
#define   GMBUS_HW_RDY			(1 << 11)
#define   GMBUS_SATOER			(1 << 10)
#define   GMBUS_ACTIVE			(1 << 9)

/* data buffer bytes 3-0 */
#define GMBUS3(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x510c)

/* interrupt mask (Pineview+) */
#define GMBUS4(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5110)
#define   GMBUS_SLAVE_TIMEOUT_EN	(1 << 4)
#define   GMBUS_NAK_EN			(1 << 3)
#define   GMBUS_IDLE_EN			(1 << 2)
#define   GMBUS_HW_WAIT_EN		(1 << 1)
#define   GMBUS_HW_RDY_EN		(1 << 0)

/* byte index */
#define GMBUS5(__i915)		_MMIO(GMBUS_MMIO_BASE(__i915) + 0x5120)
#define   GMBUS_2BYTE_INDEX_EN		(1 << 31)

#endif /* __INTEL_GMBUS_REGS_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip KSZ PTP register definitions
 * Copyright (C) 2022 Microchip Technology Inc.
 */

#ifndef __KSZ_PTP_REGS_H
#define __KSZ_PTP_REGS_H

/* 5 - PTP Clock */
#define REG_PTP_CLK_CTRL		0x0500

#define PTP_STEP_ADJ			BIT(6)
#define PTP_STEP_DIR			BIT(5)
#define PTP_READ_TIME			BIT(4)
#define PTP_LOAD_TIME			BIT(3)
#define PTP_CLK_ADJ_ENABLE		BIT(2)
#define PTP_CLK_ENABLE			BIT(1)
#define PTP_CLK_RESET			BIT(0)

#define REG_PTP_RTC_SUB_NANOSEC__2	0x0502

#define PTP_RTC_SUB_NANOSEC_M		0x0007
#define PTP_RTC_0NS			0x00

#define REG_PTP_RTC_NANOSEC		0x0504

#define REG_PTP_RTC_SEC			0x0508

#define REG_PTP_SUBNANOSEC_RATE		0x050C

#define PTP_SUBNANOSEC_M		0x3FFFFFFF
#define PTP_RATE_DIR			BIT(31)
#define PTP_TMP_RATE_ENABLE		BIT(30)

#define REG_PTP_SUBNANOSEC_RATE_L	0x050E

#define REG_PTP_RATE_DURATION		0x0510
#define REG_PTP_RATE_DURATION_H		0x0510
#define REG_PTP_RATE_DURATION_L		0x0512

#define REG_PTP_MSG_CONF1		0x0514

#define PTP_802_1AS			BIT(7)
#define PTP_ENABLE			BIT(6)
#define PTP_ETH_ENABLE			BIT(5)
#define PTP_IPV4_UDP_ENABLE		BIT(4)
#define PTP_IPV6_UDP_ENABLE		BIT(3)
#define PTP_TC_P2P			BIT(2)
#define PTP_MASTER			BIT(1)
#define PTP_1STEP			BIT(0)

#endif

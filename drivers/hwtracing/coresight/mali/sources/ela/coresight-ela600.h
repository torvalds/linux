/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _CORESIGHT_ELA600_H
#define _CORESIGHT_ELA600_H

#include <linux/bits.h>

#define ELA_CTRL 0x000
#define ELA_TIMECTRL 0x004
#define ELA_TSSR 0x008
#define ELA_ATBCTRL 0x00C
#define ELA_PTACTION 0x010
#define ELA_AUXCTRL 0x014
#define ELA_CNTSEL 0x018

#define ELA_CTSR 0x020
#define ELA_CCVR 0x024
#define ELA_CAVR 0x028
#define ELA_RDCAPTID 0x02C
#define ELA_RDCAPTIDEXT 0x030

#define ELA_RRAR 0x040
#define ELA_RRDR 0x044
#define ELA_RWAR 0x048
#define ELA_RWDR 0x04C

#define ELA_SIGSEL(x) (0x100 + 0x100 * (x))
#define ELA_TRIGCTRL(x) (ELA_SIGSEL(x) + 0x004)
#define ELA_NEXTSTATE(x) (ELA_SIGSEL(x) + 0x008)
#define ELA_ACTION(x) (ELA_SIGSEL(x) + 0x00C)
#define ELA_ALTNEXTSTATE(x) (ELA_SIGSEL(x) + 0x010)
#define ELA_ALTACTION(x) (ELA_SIGSEL(x) + 0x014)
#define ELA_COMPCTRL(x) (ELA_SIGSEL(x) + 0x018)
#define ELA_ALTCOMPCTRL(x) (ELA_SIGSEL(x) + 0x01C)
#define ELA_COUNTCOMP(x) (ELA_SIGSEL(x) + 0x020)
#define ELA_TWBSEL(x) (ELA_SIGSEL(x) + 0x028)
#define ELA_EXTMASK(x) (ELA_SIGSEL(x) + 0x030)
#define ELA_EXTCOMP(x) (ELA_SIGSEL(x) + 0x034)
#define ELA_QUALMASK(x) (ELA_SIGSEL(x) + 0x038)
#define ELA_QUALCOMP(x) (ELA_SIGSEL(x) + 0x03C)
#define ELA_SIGMASK(x, y) (ELA_SIGSEL(x) + 0x040 + 4 * (y))
#define ELA_SIGCOMP(x, y) (ELA_SIGSEL(x) + 0x080 + 4 * (y))

#define ELA_ITTRIGOUT 0xEE8
#define ELA_ITATBDATA 0xEEC
#define ELA_ITATBCTR1 0xEF0
#define ELA_ITATBCTR0 0xEF4
#define ELA_ITTRIGIN 0xEF8
#define ELA_ITCTRL 0xF00

#define ELA_AUTHSTATUS 0xFB8

#define ELA_DEVARCH 0xFBC
#define ELA_DEVID2 0xFC0
#define ELA_DEVID1 0xFC4
#define ELA_DEVID 0xFC8
#define ELA_DEVTYPE 0xFCC

#define ELA_PIDR4 0xFD0
#define ELA_PIDR5 0xFD4
#define ELA_PIDR6 0xFD8
#define ELA_PIDR7 0xFDC
#define ELA_PIDR0 0xFE0
#define ELA_PIDR1 0xFE4
#define ELA_PIDR2 0xFE8
#define ELA_PIDR3 0xFEC
#define ELA_CIDR0 0xFF0
#define ELA_CIDR1 0xFF4
#define ELA_CIDR2 0xFF8
#define ELA_CIDR3 0xFFC

/* REGISTER MASKS */
#define ELA_CTRL_RUN BIT(0)
#define ELA_CTRL_TRACE_BUSY BIT(1)

#define ELA_TIMECTRL_TSEN BIT(16)
#define ELA_TIMECTRL_TSINT GEN_MASK(15, 12)
#define ELA_TIMECTRL_TCSEL1 GEN_MASK(7, 4)
#define ELA_TIMECTRL_TCSEL0 GEN_MASK(3, 0)

#define ELA_ATBCTRL_PREDICT BIT(31)
#define ELA_ATBCTRL_ATID_TRIG_EN BIT(15)
#define ELA_ATBCTRL_ATID_VALUE GEN_MASK(14, 8)
#define ELA_ATBCTRL_ASYNC_INTERVAL GEN_MASK(7, 0)

#define ELA_ACTION_ELAOUTPUT GEN_MASK(7, 4)
#define ELA_ACTION_TRACE BIT(3)
#define ELA_ACTION_STOPCLOCK BIT(2)
#define ELA_ACTION_CTTRIGOUT GEN_MASK(1, 0)

#define ELA_AUXCTRL_FLUSH_DIS BIT(0)

#define ELA_SIGSEL_JCN_REQUEST BIT(0)
#define ELA_SIGSEL_JCN_RESPONSE BIT(1)
#define ELA_SIGSEL_CEU_EXECUTION BIT(2)
#define ELA_SIGSEL_MCU_AHBP BIT(3)
#define ELA_SIGSEL_HOST_AXI BIT(4)

#define ELA_TRIGCTRL_ALTCOMPSEL BIT(15)
#define ELA_TRIGCTRL_ALTCOMP GEN_MASK(14, 12)
#define ELA_TRIGCTRL_CAPTID GEN_MASK(11, 10)
#define ELA_TRIGCTRL_COUNTBRK BIT(9)
#define ELA_TRIGCTRL_COUNTCLR BIT(8)
#define ELA_TRIGCTRL_TRACE GEN_MASK(7, 6)
#define ELA_TRIGCTRL_COUNTSRC BIT(5)
#define ELA_TRIGCTRL_WATCHRST BIT(4)
#define ELA_TRIGCTRL_COMPSEL BIT(3)
#define ELA_TRIGCTRL_COMP GEN_MASK(2, 0)

#endif /* _CORESIGHT_ELA600_H */

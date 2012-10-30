/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

#ifndef __CVMX_CIU2_DEFS_H__
#define __CVMX_CIU2_DEFS_H__

#define CVMX_CIU2_ACK_IOX_INT(block_id) (CVMX_ADD_IO_SEG(0x00010701080C0800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_ACK_PPX_IP2(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_ACK_PPX_IP3(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_ACK_PPX_IP4(block_id) (CVMX_ADD_IO_SEG(0x00010701000C0400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108097800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B7800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A7800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108094800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B4800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A4800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070108098800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B8800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A8800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108095800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B5800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A5800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108093800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B3800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A3800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108096800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B6800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A6800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108092800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B2800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A2800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108091800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B1800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A1800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108090800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701080B0800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_IOX_INT_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701080A0800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP2_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP3_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100097400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_GPIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B7400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_GPIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A7400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100094400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_IO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B4400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_IO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A4400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100098400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MBOX_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B8400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MBOX_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A8400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100095400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MEM_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B5400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MEM_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A5400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100093400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MIO_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B3400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_MIO_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A3400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100096400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_PKT_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B6400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_PKT_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A6400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100092400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_RML_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B2400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_RML_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A2400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100091400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WDOG_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B1400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WDOG_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A1400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100090400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WRKQ_W1C(block_id) (CVMX_ADD_IO_SEG(0x00010701000B0400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_EN_PPX_IP4_WRKQ_W1S(block_id) (CVMX_ADD_IO_SEG(0x00010701000A0400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_INTR_CIU_READY (CVMX_ADD_IO_SEG(0x0001070100102008ull))
#define CVMX_CIU2_INTR_RAM_ECC_CTL (CVMX_ADD_IO_SEG(0x0001070100102010ull))
#define CVMX_CIU2_INTR_RAM_ECC_ST (CVMX_ADD_IO_SEG(0x0001070100102018ull))
#define CVMX_CIU2_INTR_SLOWDOWN (CVMX_ADD_IO_SEG(0x0001070100102000ull))
#define CVMX_CIU2_MSIRED_PPX_IP2(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_MSIRED_PPX_IP3(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_MSIRED_PPX_IP4(block_id) (CVMX_ADD_IO_SEG(0x00010701000C1400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_MSI_RCVX(offset) (CVMX_ADD_IO_SEG(0x00010701000C2000ull) + ((offset) & 255) * 8)
#define CVMX_CIU2_MSI_SELX(offset) (CVMX_ADD_IO_SEG(0x00010701000C3000ull) + ((offset) & 255) * 8)
#define CVMX_CIU2_RAW_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108047800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108044800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108045800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108043800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108046800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108042800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108041800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108040800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100047400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100044400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100045400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100043400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100046400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100042400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100041400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_RAW_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100040400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108087800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070108084800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070108088800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070108085800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070108083800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070108086800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070108082800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070108081800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_IOX_INT_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070108080800ull) + ((block_id) & 1) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP2_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080000ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP3_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080200ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_GPIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100087400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_IO(block_id) (CVMX_ADD_IO_SEG(0x0001070100084400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_MBOX(block_id) (CVMX_ADD_IO_SEG(0x0001070100088400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_MEM(block_id) (CVMX_ADD_IO_SEG(0x0001070100085400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_MIO(block_id) (CVMX_ADD_IO_SEG(0x0001070100083400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_PKT(block_id) (CVMX_ADD_IO_SEG(0x0001070100086400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_RML(block_id) (CVMX_ADD_IO_SEG(0x0001070100082400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_WDOG(block_id) (CVMX_ADD_IO_SEG(0x0001070100081400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SRC_PPX_IP4_WRKQ(block_id) (CVMX_ADD_IO_SEG(0x0001070100080400ull) + ((block_id) & 31) * 0x200000ull)
#define CVMX_CIU2_SUM_IOX_INT(offset) (CVMX_ADD_IO_SEG(0x0001070100000800ull) + ((offset) & 1) * 8)
#define CVMX_CIU2_SUM_PPX_IP2(offset) (CVMX_ADD_IO_SEG(0x0001070100000000ull) + ((offset) & 31) * 8)
#define CVMX_CIU2_SUM_PPX_IP3(offset) (CVMX_ADD_IO_SEG(0x0001070100000200ull) + ((offset) & 31) * 8)
#define CVMX_CIU2_SUM_PPX_IP4(offset) (CVMX_ADD_IO_SEG(0x0001070100000400ull) + ((offset) & 31) * 8)

union cvmx_ciu2_ack_iox_int {
	uint64_t u64;
	struct cvmx_ciu2_ack_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ack:1;
#else
		uint64_t ack:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_ack_iox_int_s cn68xx;
	struct cvmx_ciu2_ack_iox_int_s cn68xxp1;
};

union cvmx_ciu2_ack_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ack:1;
#else
		uint64_t ack:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip2_s cn68xx;
	struct cvmx_ciu2_ack_ppx_ip2_s cn68xxp1;
};

union cvmx_ciu2_ack_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ack:1;
#else
		uint64_t ack:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip3_s cn68xx;
	struct cvmx_ciu2_ack_ppx_ip3_s cn68xxp1;
};

union cvmx_ciu2_ack_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_ack_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ack:1;
#else
		uint64_t ack:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_ack_ppx_ip4_s cn68xx;
	struct cvmx_ciu2_ack_ppx_ip4_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_s cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_gpio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_s cn68xx;
	struct cvmx_ciu2_en_iox_int_io_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_io_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_io_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_io_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mbox_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mem_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_mio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_s cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_s cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wdog_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_iox_int_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_iox_int_wrkq_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_gpio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_io_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mbox_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mem_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_mio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wdog_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip2_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip2_wrkq_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_gpio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_io_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mbox_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mem_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_mio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wdog_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip3_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip3_wrkq_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_gpio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_gpio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_gpio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_io_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_io_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_io_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mbox {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mbox_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mbox_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mbox_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mem_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mem_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mem_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mio_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_mio_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_mio_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_pkt_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_pkt_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_pkt_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_rml_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1c_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_rml_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_rml_w1s_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wdog_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wdog_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wdog_w1s_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wrkq_w1c {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1c_s cn68xxp1;
};

union cvmx_ciu2_en_ppx_ip4_wrkq_w1s {
	uint64_t u64;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s cn68xx;
	struct cvmx_ciu2_en_ppx_ip4_wrkq_w1s_s cn68xxp1;
};

union cvmx_ciu2_intr_ciu_ready {
	uint64_t u64;
	struct cvmx_ciu2_intr_ciu_ready_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t ready:1;
#else
		uint64_t ready:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_intr_ciu_ready_s cn68xx;
	struct cvmx_ciu2_intr_ciu_ready_s cn68xxp1;
};

union cvmx_ciu2_intr_ram_ecc_ctl {
	uint64_t u64;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t flip_synd:2;
		uint64_t ecc_ena:1;
#else
		uint64_t ecc_ena:1;
		uint64_t flip_synd:2;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s cn68xx;
	struct cvmx_ciu2_intr_ram_ecc_ctl_s cn68xxp1;
};

union cvmx_ciu2_intr_ram_ecc_st {
	uint64_t u64;
	struct cvmx_ciu2_intr_ram_ecc_st_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_23_63:41;
		uint64_t addr:7;
		uint64_t reserved_13_15:3;
		uint64_t syndrom:9;
		uint64_t reserved_2_3:2;
		uint64_t dbe:1;
		uint64_t sbe:1;
#else
		uint64_t sbe:1;
		uint64_t dbe:1;
		uint64_t reserved_2_3:2;
		uint64_t syndrom:9;
		uint64_t reserved_13_15:3;
		uint64_t addr:7;
		uint64_t reserved_23_63:41;
#endif
	} s;
	struct cvmx_ciu2_intr_ram_ecc_st_s cn68xx;
	struct cvmx_ciu2_intr_ram_ecc_st_s cn68xxp1;
};

union cvmx_ciu2_intr_slowdown {
	uint64_t u64;
	struct cvmx_ciu2_intr_slowdown_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t ctl:3;
#else
		uint64_t ctl:3;
		uint64_t reserved_3_63:61;
#endif
	} s;
	struct cvmx_ciu2_intr_slowdown_s cn68xx;
	struct cvmx_ciu2_intr_slowdown_s cn68xxp1;
};

union cvmx_ciu2_msi_rcvx {
	uint64_t u64;
	struct cvmx_ciu2_msi_rcvx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t msi_rcv:1;
#else
		uint64_t msi_rcv:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_ciu2_msi_rcvx_s cn68xx;
	struct cvmx_ciu2_msi_rcvx_s cn68xxp1;
};

union cvmx_ciu2_msi_selx {
	uint64_t u64;
	struct cvmx_ciu2_msi_selx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_13_63:51;
		uint64_t pp_num:5;
		uint64_t reserved_6_7:2;
		uint64_t ip_num:2;
		uint64_t reserved_1_3:3;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_3:3;
		uint64_t ip_num:2;
		uint64_t reserved_6_7:2;
		uint64_t pp_num:5;
		uint64_t reserved_13_63:51;
#endif
	} s;
	struct cvmx_ciu2_msi_selx_s cn68xx;
	struct cvmx_ciu2_msi_selx_s cn68xxp1;
};

union cvmx_ciu2_msired_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t intr:1;
		uint64_t reserved_17_19:3;
		uint64_t newint:1;
		uint64_t reserved_8_15:8;
		uint64_t msi_num:8;
#else
		uint64_t msi_num:8;
		uint64_t reserved_8_15:8;
		uint64_t newint:1;
		uint64_t reserved_17_19:3;
		uint64_t intr:1;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip2_s cn68xx;
	struct cvmx_ciu2_msired_ppx_ip2_s cn68xxp1;
};

union cvmx_ciu2_msired_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t intr:1;
		uint64_t reserved_17_19:3;
		uint64_t newint:1;
		uint64_t reserved_8_15:8;
		uint64_t msi_num:8;
#else
		uint64_t msi_num:8;
		uint64_t reserved_8_15:8;
		uint64_t newint:1;
		uint64_t reserved_17_19:3;
		uint64_t intr:1;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip3_s cn68xx;
	struct cvmx_ciu2_msired_ppx_ip3_s cn68xxp1;
};

union cvmx_ciu2_msired_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_msired_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t intr:1;
		uint64_t reserved_17_19:3;
		uint64_t newint:1;
		uint64_t reserved_8_15:8;
		uint64_t msi_num:8;
#else
		uint64_t msi_num:8;
		uint64_t reserved_8_15:8;
		uint64_t newint:1;
		uint64_t reserved_17_19:3;
		uint64_t intr:1;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_ciu2_msired_ppx_ip4_s cn68xx;
	struct cvmx_ciu2_msired_ppx_ip4_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_gpio_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_gpio_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_io_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_io_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_mem_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_mem_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_mio_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_mio_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_pkt_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_rml_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_wdog_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_wdog_s cn68xxp1;
};

union cvmx_ciu2_raw_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_raw_iox_int_wrkq_s cn68xx;
	struct cvmx_ciu2_raw_iox_int_wrkq_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_gpio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_io_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_io_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_mem_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_mio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_rml_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_wdog_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip2_wrkq_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_gpio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_io_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_io_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_mem_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_mio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_rml_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_wdog_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip3_wrkq_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_gpio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_io_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_io_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_mem_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_mio_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_rml_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_wdog_s cn68xxp1;
};

union cvmx_ciu2_raw_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s cn68xx;
	struct cvmx_ciu2_raw_ppx_ip4_wrkq_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_gpio_s cn68xx;
	struct cvmx_ciu2_src_iox_int_gpio_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_io {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_io_s cn68xx;
	struct cvmx_ciu2_src_iox_int_io_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mbox_s cn68xx;
	struct cvmx_ciu2_src_iox_int_mbox_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mem_s cn68xx;
	struct cvmx_ciu2_src_iox_int_mem_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_mio_s cn68xx;
	struct cvmx_ciu2_src_iox_int_mio_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_pkt_s cn68xx;
	struct cvmx_ciu2_src_iox_int_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_iox_int_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_rml_s cn68xx;
	struct cvmx_ciu2_src_iox_int_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_iox_int_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_wdog_s cn68xx;
	struct cvmx_ciu2_src_iox_int_wdog_s cn68xxp1;
};

union cvmx_ciu2_src_iox_int_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_iox_int_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_src_iox_int_wrkq_s cn68xx;
	struct cvmx_ciu2_src_iox_int_wrkq_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_gpio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_io_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_io_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mbox_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mem_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mem_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_mio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_mio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_pkt_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_rml_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_wdog_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip2_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip2_wrkq_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_gpio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_io_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_io_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mbox_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mem_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mem_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_mio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_mio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_pkt_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_rml_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_wdog_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip3_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip3_wrkq_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_gpio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t gpio:16;
#else
		uint64_t gpio:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_gpio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_io {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_io_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_34_63:30;
		uint64_t pem:2;
		uint64_t reserved_18_31:14;
		uint64_t pci_inta:2;
		uint64_t reserved_13_15:3;
		uint64_t msired:1;
		uint64_t pci_msi:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_intr:4;
#else
		uint64_t pci_intr:4;
		uint64_t reserved_4_7:4;
		uint64_t pci_msi:4;
		uint64_t msired:1;
		uint64_t reserved_13_15:3;
		uint64_t pci_inta:2;
		uint64_t reserved_18_31:14;
		uint64_t pem:2;
		uint64_t reserved_34_63:30;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_io_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_io_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_mbox {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t mbox:4;
#else
		uint64_t mbox:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mbox_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_mem {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mem_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t lmc:4;
#else
		uint64_t lmc:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mem_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mem_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_mio {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_mio_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t rst:1;
		uint64_t reserved_49_62:14;
		uint64_t ptp:1;
		uint64_t reserved_45_47:3;
		uint64_t usb_hci:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_uctl:1;
		uint64_t reserved_38_39:2;
		uint64_t uart:2;
		uint64_t reserved_34_35:2;
		uint64_t twsi:2;
		uint64_t reserved_19_31:13;
		uint64_t bootdma:1;
		uint64_t mio:1;
		uint64_t nand:1;
		uint64_t reserved_12_15:4;
		uint64_t timer:4;
		uint64_t reserved_3_7:5;
		uint64_t ipd_drp:1;
		uint64_t ssoiq:1;
		uint64_t ipdppthr:1;
#else
		uint64_t ipdppthr:1;
		uint64_t ssoiq:1;
		uint64_t ipd_drp:1;
		uint64_t reserved_3_7:5;
		uint64_t timer:4;
		uint64_t reserved_12_15:4;
		uint64_t nand:1;
		uint64_t mio:1;
		uint64_t bootdma:1;
		uint64_t reserved_19_31:13;
		uint64_t twsi:2;
		uint64_t reserved_34_35:2;
		uint64_t uart:2;
		uint64_t reserved_38_39:2;
		uint64_t usb_uctl:1;
		uint64_t reserved_41_43:3;
		uint64_t usb_hci:1;
		uint64_t reserved_45_47:3;
		uint64_t ptp:1;
		uint64_t reserved_49_62:14;
		uint64_t rst:1;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_mio_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_mio_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_pkt {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_54_63:10;
		uint64_t ilk_drp:2;
		uint64_t reserved_49_51:3;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_51:3;
		uint64_t ilk_drp:2;
		uint64_t reserved_54_63:10;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_pkt_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_pkt_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_49_63:15;
		uint64_t ilk:1;
		uint64_t reserved_41_47:7;
		uint64_t mii:1;
		uint64_t reserved_33_39:7;
		uint64_t agl:1;
		uint64_t reserved_13_31:19;
		uint64_t gmx_drp:5;
		uint64_t reserved_5_7:3;
		uint64_t agx:5;
#else
		uint64_t agx:5;
		uint64_t reserved_5_7:3;
		uint64_t gmx_drp:5;
		uint64_t reserved_13_31:19;
		uint64_t agl:1;
		uint64_t reserved_33_39:7;
		uint64_t mii:1;
		uint64_t reserved_41_47:7;
		uint64_t ilk:1;
		uint64_t reserved_49_63:15;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_rml {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_rml_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_37_39:3;
		uint64_t dpi_dma:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_35:2;
		uint64_t dpi_dma:1;
		uint64_t reserved_37_39:3;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_rml_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_rml_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_56_63:8;
		uint64_t trace:4;
		uint64_t reserved_49_51:3;
		uint64_t l2c:1;
		uint64_t reserved_41_47:7;
		uint64_t dfa:1;
		uint64_t reserved_34_39:6;
		uint64_t dpi:1;
		uint64_t sli:1;
		uint64_t reserved_31_31:1;
		uint64_t key:1;
		uint64_t rad:1;
		uint64_t tim:1;
		uint64_t reserved_25_27:3;
		uint64_t zip:1;
		uint64_t reserved_17_23:7;
		uint64_t sso:1;
		uint64_t reserved_8_15:8;
		uint64_t pko:1;
		uint64_t pip:1;
		uint64_t ipd:1;
		uint64_t fpa:1;
		uint64_t reserved_1_3:3;
		uint64_t iob:1;
#else
		uint64_t iob:1;
		uint64_t reserved_1_3:3;
		uint64_t fpa:1;
		uint64_t ipd:1;
		uint64_t pip:1;
		uint64_t pko:1;
		uint64_t reserved_8_15:8;
		uint64_t sso:1;
		uint64_t reserved_17_23:7;
		uint64_t zip:1;
		uint64_t reserved_25_27:3;
		uint64_t tim:1;
		uint64_t rad:1;
		uint64_t key:1;
		uint64_t reserved_31_31:1;
		uint64_t sli:1;
		uint64_t dpi:1;
		uint64_t reserved_34_39:6;
		uint64_t dfa:1;
		uint64_t reserved_41_47:7;
		uint64_t l2c:1;
		uint64_t reserved_49_51:3;
		uint64_t trace:4;
		uint64_t reserved_56_63:8;
#endif
	} cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_wdog {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t wdog:32;
#else
		uint64_t wdog:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_wdog_s cn68xxp1;
};

union cvmx_ciu2_src_ppx_ip4_wrkq {
	uint64_t u64;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t workq:64;
#else
		uint64_t workq:64;
#endif
	} s;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s cn68xx;
	struct cvmx_ciu2_src_ppx_ip4_wrkq_s cn68xxp1;
};

union cvmx_ciu2_sum_iox_int {
	uint64_t u64;
	struct cvmx_ciu2_sum_iox_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mbox:4;
		uint64_t reserved_8_59:52;
		uint64_t gpio:1;
		uint64_t pkt:1;
		uint64_t mem:1;
		uint64_t io:1;
		uint64_t mio:1;
		uint64_t rml:1;
		uint64_t wdog:1;
		uint64_t workq:1;
#else
		uint64_t workq:1;
		uint64_t wdog:1;
		uint64_t rml:1;
		uint64_t mio:1;
		uint64_t io:1;
		uint64_t mem:1;
		uint64_t pkt:1;
		uint64_t gpio:1;
		uint64_t reserved_8_59:52;
		uint64_t mbox:4;
#endif
	} s;
	struct cvmx_ciu2_sum_iox_int_s cn68xx;
	struct cvmx_ciu2_sum_iox_int_s cn68xxp1;
};

union cvmx_ciu2_sum_ppx_ip2 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip2_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mbox:4;
		uint64_t reserved_8_59:52;
		uint64_t gpio:1;
		uint64_t pkt:1;
		uint64_t mem:1;
		uint64_t io:1;
		uint64_t mio:1;
		uint64_t rml:1;
		uint64_t wdog:1;
		uint64_t workq:1;
#else
		uint64_t workq:1;
		uint64_t wdog:1;
		uint64_t rml:1;
		uint64_t mio:1;
		uint64_t io:1;
		uint64_t mem:1;
		uint64_t pkt:1;
		uint64_t gpio:1;
		uint64_t reserved_8_59:52;
		uint64_t mbox:4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip2_s cn68xx;
	struct cvmx_ciu2_sum_ppx_ip2_s cn68xxp1;
};

union cvmx_ciu2_sum_ppx_ip3 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip3_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mbox:4;
		uint64_t reserved_8_59:52;
		uint64_t gpio:1;
		uint64_t pkt:1;
		uint64_t mem:1;
		uint64_t io:1;
		uint64_t mio:1;
		uint64_t rml:1;
		uint64_t wdog:1;
		uint64_t workq:1;
#else
		uint64_t workq:1;
		uint64_t wdog:1;
		uint64_t rml:1;
		uint64_t mio:1;
		uint64_t io:1;
		uint64_t mem:1;
		uint64_t pkt:1;
		uint64_t gpio:1;
		uint64_t reserved_8_59:52;
		uint64_t mbox:4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip3_s cn68xx;
	struct cvmx_ciu2_sum_ppx_ip3_s cn68xxp1;
};

union cvmx_ciu2_sum_ppx_ip4 {
	uint64_t u64;
	struct cvmx_ciu2_sum_ppx_ip4_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t mbox:4;
		uint64_t reserved_8_59:52;
		uint64_t gpio:1;
		uint64_t pkt:1;
		uint64_t mem:1;
		uint64_t io:1;
		uint64_t mio:1;
		uint64_t rml:1;
		uint64_t wdog:1;
		uint64_t workq:1;
#else
		uint64_t workq:1;
		uint64_t wdog:1;
		uint64_t rml:1;
		uint64_t mio:1;
		uint64_t io:1;
		uint64_t mem:1;
		uint64_t pkt:1;
		uint64_t gpio:1;
		uint64_t reserved_8_59:52;
		uint64_t mbox:4;
#endif
	} s;
	struct cvmx_ciu2_sum_ppx_ip4_s cn68xx;
	struct cvmx_ciu2_sum_ppx_ip4_s cn68xxp1;
};

#endif

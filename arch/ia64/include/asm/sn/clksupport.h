/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * This file contains definitions for accessing a platform supported high resolution
 * clock. The clock is monitonically increasing and can be accessed from any node
 * in the system. The clock is synchronized across nodes - all nodes see the
 * same value.
 * 
 *	RTC_COUNTER_ADDR - contains the address of the counter 
 *
 */

#ifndef _ASM_IA64_SN_CLKSUPPORT_H
#define _ASM_IA64_SN_CLKSUPPORT_H

extern unsigned long sn_rtc_cycles_per_second;

#define RTC_COUNTER_ADDR	((long *)LOCAL_MMR_ADDR(SH_RTC))

#define rtc_time()		(*RTC_COUNTER_ADDR)

#endif /* _ASM_IA64_SN_CLKSUPPORT_H */

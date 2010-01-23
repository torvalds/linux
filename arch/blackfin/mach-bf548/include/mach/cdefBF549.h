/*
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _CDEF_BF549_H
#define _CDEF_BF549_H

/* include all Core registers and bit definitions */
#include "defBF549.h"

/* include core sbfin_read_()ecific register pointer definitions */
#include <asm/cdef_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF549 */

/* include cdefBF54x_base.h for the set of #defines that are common to all ADSP-BF54x bfin_read_()rocessors */
#include "cdefBF54x_base.h"

/* The BF549 is like the BF544, but has MXVR */
#include "cdefBF547.h"

/* MXVR Registers */

#define bfin_read_MXVR_CONFIG()			bfin_read16(MXVR_CONFIG)
#define bfin_write_MXVR_CONFIG(val)		bfin_write16(MXVR_CONFIG, val)
#define bfin_read_MXVR_STATE_0()		bfin_read32(MXVR_STATE_0)
#define bfin_write_MXVR_STATE_0(val)		bfin_write32(MXVR_STATE_0, val)
#define bfin_read_MXVR_STATE_1()		bfin_read32(MXVR_STATE_1)
#define bfin_write_MXVR_STATE_1(val)		bfin_write32(MXVR_STATE_1, val)
#define bfin_read_MXVR_INT_STAT_0()		bfin_read32(MXVR_INT_STAT_0)
#define bfin_write_MXVR_INT_STAT_0(val)		bfin_write32(MXVR_INT_STAT_0, val)
#define bfin_read_MXVR_INT_STAT_1()		bfin_read32(MXVR_INT_STAT_1)
#define bfin_write_MXVR_INT_STAT_1(val)		bfin_write32(MXVR_INT_STAT_1, val)
#define bfin_read_MXVR_INT_EN_0()		bfin_read32(MXVR_INT_EN_0)
#define bfin_write_MXVR_INT_EN_0(val)		bfin_write32(MXVR_INT_EN_0, val)
#define bfin_read_MXVR_INT_EN_1()		bfin_read32(MXVR_INT_EN_1)
#define bfin_write_MXVR_INT_EN_1(val)		bfin_write32(MXVR_INT_EN_1, val)
#define bfin_read_MXVR_POSITION()		bfin_read16(MXVR_POSITION)
#define bfin_write_MXVR_POSITION(val)		bfin_write16(MXVR_POSITION, val)
#define bfin_read_MXVR_MAX_POSITION()		bfin_read16(MXVR_MAX_POSITION)
#define bfin_write_MXVR_MAX_POSITION(val)	bfin_write16(MXVR_MAX_POSITION, val)
#define bfin_read_MXVR_DELAY()			bfin_read16(MXVR_DELAY)
#define bfin_write_MXVR_DELAY(val)		bfin_write16(MXVR_DELAY, val)
#define bfin_read_MXVR_MAX_DELAY()		bfin_read16(MXVR_MAX_DELAY)
#define bfin_write_MXVR_MAX_DELAY(val)		bfin_write16(MXVR_MAX_DELAY, val)
#define bfin_read_MXVR_LADDR()			bfin_read32(MXVR_LADDR)
#define bfin_write_MXVR_LADDR(val)		bfin_write32(MXVR_LADDR, val)
#define bfin_read_MXVR_GADDR()			bfin_read16(MXVR_GADDR)
#define bfin_write_MXVR_GADDR(val)		bfin_write16(MXVR_GADDR, val)
#define bfin_read_MXVR_AADDR()			bfin_read32(MXVR_AADDR)
#define bfin_write_MXVR_AADDR(val)		bfin_write32(MXVR_AADDR, val)

/* MXVR Allocation Table Registers */

#define bfin_read_MXVR_ALLOC_0()		bfin_read32(MXVR_ALLOC_0)
#define bfin_write_MXVR_ALLOC_0(val)		bfin_write32(MXVR_ALLOC_0, val)
#define bfin_read_MXVR_ALLOC_1()		bfin_read32(MXVR_ALLOC_1)
#define bfin_write_MXVR_ALLOC_1(val)		bfin_write32(MXVR_ALLOC_1, val)
#define bfin_read_MXVR_ALLOC_2()		bfin_read32(MXVR_ALLOC_2)
#define bfin_write_MXVR_ALLOC_2(val)		bfin_write32(MXVR_ALLOC_2, val)
#define bfin_read_MXVR_ALLOC_3()		bfin_read32(MXVR_ALLOC_3)
#define bfin_write_MXVR_ALLOC_3(val)		bfin_write32(MXVR_ALLOC_3, val)
#define bfin_read_MXVR_ALLOC_4()		bfin_read32(MXVR_ALLOC_4)
#define bfin_write_MXVR_ALLOC_4(val)		bfin_write32(MXVR_ALLOC_4, val)
#define bfin_read_MXVR_ALLOC_5()		bfin_read32(MXVR_ALLOC_5)
#define bfin_write_MXVR_ALLOC_5(val)		bfin_write32(MXVR_ALLOC_5, val)
#define bfin_read_MXVR_ALLOC_6()		bfin_read32(MXVR_ALLOC_6)
#define bfin_write_MXVR_ALLOC_6(val)		bfin_write32(MXVR_ALLOC_6, val)
#define bfin_read_MXVR_ALLOC_7()		bfin_read32(MXVR_ALLOC_7)
#define bfin_write_MXVR_ALLOC_7(val)		bfin_write32(MXVR_ALLOC_7, val)
#define bfin_read_MXVR_ALLOC_8()		bfin_read32(MXVR_ALLOC_8)
#define bfin_write_MXVR_ALLOC_8(val)		bfin_write32(MXVR_ALLOC_8, val)
#define bfin_read_MXVR_ALLOC_9()		bfin_read32(MXVR_ALLOC_9)
#define bfin_write_MXVR_ALLOC_9(val)		bfin_write32(MXVR_ALLOC_9, val)
#define bfin_read_MXVR_ALLOC_10()		bfin_read32(MXVR_ALLOC_10)
#define bfin_write_MXVR_ALLOC_10(val)		bfin_write32(MXVR_ALLOC_10, val)
#define bfin_read_MXVR_ALLOC_11()		bfin_read32(MXVR_ALLOC_11)
#define bfin_write_MXVR_ALLOC_11(val)		bfin_write32(MXVR_ALLOC_11, val)
#define bfin_read_MXVR_ALLOC_12()		bfin_read32(MXVR_ALLOC_12)
#define bfin_write_MXVR_ALLOC_12(val)		bfin_write32(MXVR_ALLOC_12, val)
#define bfin_read_MXVR_ALLOC_13()		bfin_read32(MXVR_ALLOC_13)
#define bfin_write_MXVR_ALLOC_13(val)		bfin_write32(MXVR_ALLOC_13, val)
#define bfin_read_MXVR_ALLOC_14()		bfin_read32(MXVR_ALLOC_14)
#define bfin_write_MXVR_ALLOC_14(val)		bfin_write32(MXVR_ALLOC_14, val)

/* MXVR Channel Assign Registers */

#define bfin_read_MXVR_SYNC_LCHAN_0()		bfin_read32(MXVR_SYNC_LCHAN_0)
#define bfin_write_MXVR_SYNC_LCHAN_0(val)	bfin_write32(MXVR_SYNC_LCHAN_0, val)
#define bfin_read_MXVR_SYNC_LCHAN_1()		bfin_read32(MXVR_SYNC_LCHAN_1)
#define bfin_write_MXVR_SYNC_LCHAN_1(val)	bfin_write32(MXVR_SYNC_LCHAN_1, val)
#define bfin_read_MXVR_SYNC_LCHAN_2()		bfin_read32(MXVR_SYNC_LCHAN_2)
#define bfin_write_MXVR_SYNC_LCHAN_2(val)	bfin_write32(MXVR_SYNC_LCHAN_2, val)
#define bfin_read_MXVR_SYNC_LCHAN_3()		bfin_read32(MXVR_SYNC_LCHAN_3)
#define bfin_write_MXVR_SYNC_LCHAN_3(val)	bfin_write32(MXVR_SYNC_LCHAN_3, val)
#define bfin_read_MXVR_SYNC_LCHAN_4()		bfin_read32(MXVR_SYNC_LCHAN_4)
#define bfin_write_MXVR_SYNC_LCHAN_4(val)	bfin_write32(MXVR_SYNC_LCHAN_4, val)
#define bfin_read_MXVR_SYNC_LCHAN_5()		bfin_read32(MXVR_SYNC_LCHAN_5)
#define bfin_write_MXVR_SYNC_LCHAN_5(val)	bfin_write32(MXVR_SYNC_LCHAN_5, val)
#define bfin_read_MXVR_SYNC_LCHAN_6()		bfin_read32(MXVR_SYNC_LCHAN_6)
#define bfin_write_MXVR_SYNC_LCHAN_6(val)	bfin_write32(MXVR_SYNC_LCHAN_6, val)
#define bfin_read_MXVR_SYNC_LCHAN_7()		bfin_read32(MXVR_SYNC_LCHAN_7)
#define bfin_write_MXVR_SYNC_LCHAN_7(val)	bfin_write32(MXVR_SYNC_LCHAN_7, val)

/* MXVR DMA0 Registers */

#define bfin_read_MXVR_DMA0_CONFIG()		bfin_read32(MXVR_DMA0_CONFIG)
#define bfin_write_MXVR_DMA0_CONFIG(val)	bfin_write32(MXVR_DMA0_CONFIG, val)
#define bfin_read_MXVR_DMA0_START_ADDR()	bfin_read32(MXVR_DMA0_START_ADDR)
#define bfin_write_MXVR_DMA0_START_ADDR(val)	bfin_write32(MXVR_DMA0_START_ADDR)
#define bfin_read_MXVR_DMA0_COUNT()		bfin_read16(MXVR_DMA0_COUNT)
#define bfin_write_MXVR_DMA0_COUNT(val)		bfin_write16(MXVR_DMA0_COUNT, val)
#define bfin_read_MXVR_DMA0_CURR_ADDR()		bfin_read32(MXVR_DMA0_CURR_ADDR)
#define bfin_write_MXVR_DMA0_CURR_ADDR(val)	bfin_write32(MXVR_DMA0_CURR_ADDR)
#define bfin_read_MXVR_DMA0_CURR_COUNT()	bfin_read16(MXVR_DMA0_CURR_COUNT)
#define bfin_write_MXVR_DMA0_CURR_COUNT(val)	bfin_write16(MXVR_DMA0_CURR_COUNT, val)

/* MXVR DMA1 Registers */

#define bfin_read_MXVR_DMA1_CONFIG()		bfin_read32(MXVR_DMA1_CONFIG)
#define bfin_write_MXVR_DMA1_CONFIG(val)	bfin_write32(MXVR_DMA1_CONFIG, val)
#define bfin_read_MXVR_DMA1_START_ADDR()	bfin_read32(MXVR_DMA1_START_ADDR)
#define bfin_write_MXVR_DMA1_START_ADDR(val)	bfin_write32(MXVR_DMA1_START_ADDR)
#define bfin_read_MXVR_DMA1_COUNT()		bfin_read16(MXVR_DMA1_COUNT)
#define bfin_write_MXVR_DMA1_COUNT(val)		bfin_write16(MXVR_DMA1_COUNT, val)
#define bfin_read_MXVR_DMA1_CURR_ADDR()		bfin_read32(MXVR_DMA1_CURR_ADDR)
#define bfin_write_MXVR_DMA1_CURR_ADDR(val)	bfin_write32(MXVR_DMA1_CURR_ADDR)
#define bfin_read_MXVR_DMA1_CURR_COUNT()	bfin_read16(MXVR_DMA1_CURR_COUNT)
#define bfin_write_MXVR_DMA1_CURR_COUNT(val)	bfin_write16(MXVR_DMA1_CURR_COUNT, val)

/* MXVR DMA2 Registers */

#define bfin_read_MXVR_DMA2_CONFIG()		bfin_read32(MXVR_DMA2_CONFIG)
#define bfin_write_MXVR_DMA2_CONFIG(val)	bfin_write32(MXVR_DMA2_CONFIG, val)
#define bfin_read_MXVR_DMA2_START_ADDR() 	bfin_read32(MXVR_DMA2_START_ADDR)
#define bfin_write_MXVR_DMA2_START_ADDR(val) 	bfin_write32(MXVR_DMA2_START_ADDR)
#define bfin_read_MXVR_DMA2_COUNT()		bfin_read16(MXVR_DMA2_COUNT)
#define bfin_write_MXVR_DMA2_COUNT(val)		bfin_write16(MXVR_DMA2_COUNT, val)
#define bfin_read_MXVR_DMA2_CURR_ADDR() 	bfin_read32(MXVR_DMA2_CURR_ADDR)
#define bfin_write_MXVR_DMA2_CURR_ADDR(val) 	bfin_write32(MXVR_DMA2_CURR_ADDR)
#define bfin_read_MXVR_DMA2_CURR_COUNT()	bfin_read16(MXVR_DMA2_CURR_COUNT)
#define bfin_write_MXVR_DMA2_CURR_COUNT(val)	bfin_write16(MXVR_DMA2_CURR_COUNT, val)

/* MXVR DMA3 Registers */

#define bfin_read_MXVR_DMA3_CONFIG()		bfin_read32(MXVR_DMA3_CONFIG)
#define bfin_write_MXVR_DMA3_CONFIG(val)	bfin_write32(MXVR_DMA3_CONFIG, val)
#define bfin_read_MXVR_DMA3_START_ADDR() 	bfin_read32(MXVR_DMA3_START_ADDR)
#define bfin_write_MXVR_DMA3_START_ADDR(val) 	bfin_write32(MXVR_DMA3_START_ADDR)
#define bfin_read_MXVR_DMA3_COUNT()		bfin_read16(MXVR_DMA3_COUNT)
#define bfin_write_MXVR_DMA3_COUNT(val)		bfin_write16(MXVR_DMA3_COUNT, val)
#define bfin_read_MXVR_DMA3_CURR_ADDR() 	bfin_read32(MXVR_DMA3_CURR_ADDR)
#define bfin_write_MXVR_DMA3_CURR_ADDR(val) 	bfin_write32(MXVR_DMA3_CURR_ADDR)
#define bfin_read_MXVR_DMA3_CURR_COUNT()	bfin_read16(MXVR_DMA3_CURR_COUNT)
#define bfin_write_MXVR_DMA3_CURR_COUNT(val)	bfin_write16(MXVR_DMA3_CURR_COUNT, val)

/* MXVR DMA4 Registers */

#define bfin_read_MXVR_DMA4_CONFIG()		bfin_read32(MXVR_DMA4_CONFIG)
#define bfin_write_MXVR_DMA4_CONFIG(val)	bfin_write32(MXVR_DMA4_CONFIG, val)
#define bfin_read_MXVR_DMA4_START_ADDR() 	bfin_read32(MXVR_DMA4_START_ADDR)
#define bfin_write_MXVR_DMA4_START_ADDR(val) 	bfin_write32(MXVR_DMA4_START_ADDR)
#define bfin_read_MXVR_DMA4_COUNT()		bfin_read16(MXVR_DMA4_COUNT)
#define bfin_write_MXVR_DMA4_COUNT(val)		bfin_write16(MXVR_DMA4_COUNT, val)
#define bfin_read_MXVR_DMA4_CURR_ADDR() 	bfin_read32(MXVR_DMA4_CURR_ADDR)
#define bfin_write_MXVR_DMA4_CURR_ADDR(val) 	bfin_write32(MXVR_DMA4_CURR_ADDR)
#define bfin_read_MXVR_DMA4_CURR_COUNT()	bfin_read16(MXVR_DMA4_CURR_COUNT)
#define bfin_write_MXVR_DMA4_CURR_COUNT(val)	bfin_write16(MXVR_DMA4_CURR_COUNT, val)

/* MXVR DMA5 Registers */

#define bfin_read_MXVR_DMA5_CONFIG()		bfin_read32(MXVR_DMA5_CONFIG)
#define bfin_write_MXVR_DMA5_CONFIG(val)	bfin_write32(MXVR_DMA5_CONFIG, val)
#define bfin_read_MXVR_DMA5_START_ADDR() 	bfin_read32(MXVR_DMA5_START_ADDR)
#define bfin_write_MXVR_DMA5_START_ADDR(val) 	bfin_write32(MXVR_DMA5_START_ADDR)
#define bfin_read_MXVR_DMA5_COUNT()		bfin_read16(MXVR_DMA5_COUNT)
#define bfin_write_MXVR_DMA5_COUNT(val)		bfin_write16(MXVR_DMA5_COUNT, val)
#define bfin_read_MXVR_DMA5_CURR_ADDR() 	bfin_read32(MXVR_DMA5_CURR_ADDR)
#define bfin_write_MXVR_DMA5_CURR_ADDR(val) 	bfin_write32(MXVR_DMA5_CURR_ADDR)
#define bfin_read_MXVR_DMA5_CURR_COUNT()	bfin_read16(MXVR_DMA5_CURR_COUNT)
#define bfin_write_MXVR_DMA5_CURR_COUNT(val)	bfin_write16(MXVR_DMA5_CURR_COUNT, val)

/* MXVR DMA6 Registers */

#define bfin_read_MXVR_DMA6_CONFIG()		bfin_read32(MXVR_DMA6_CONFIG)
#define bfin_write_MXVR_DMA6_CONFIG(val)	bfin_write32(MXVR_DMA6_CONFIG, val)
#define bfin_read_MXVR_DMA6_START_ADDR() 	bfin_read32(MXVR_DMA6_START_ADDR)
#define bfin_write_MXVR_DMA6_START_ADDR(val) 	bfin_write32(MXVR_DMA6_START_ADDR)
#define bfin_read_MXVR_DMA6_COUNT()		bfin_read16(MXVR_DMA6_COUNT)
#define bfin_write_MXVR_DMA6_COUNT(val)		bfin_write16(MXVR_DMA6_COUNT, val)
#define bfin_read_MXVR_DMA6_CURR_ADDR() 	bfin_read32(MXVR_DMA6_CURR_ADDR)
#define bfin_write_MXVR_DMA6_CURR_ADDR(val) 	bfin_write32(MXVR_DMA6_CURR_ADDR)
#define bfin_read_MXVR_DMA6_CURR_COUNT()	bfin_read16(MXVR_DMA6_CURR_COUNT)
#define bfin_write_MXVR_DMA6_CURR_COUNT(val)	bfin_write16(MXVR_DMA6_CURR_COUNT, val)

/* MXVR DMA7 Registers */

#define bfin_read_MXVR_DMA7_CONFIG()		bfin_read32(MXVR_DMA7_CONFIG)
#define bfin_write_MXVR_DMA7_CONFIG(val)	bfin_write32(MXVR_DMA7_CONFIG, val)
#define bfin_read_MXVR_DMA7_START_ADDR() 	bfin_read32(MXVR_DMA7_START_ADDR)
#define bfin_write_MXVR_DMA7_START_ADDR(val) 	bfin_write32(MXVR_DMA7_START_ADDR)
#define bfin_read_MXVR_DMA7_COUNT()		bfin_read16(MXVR_DMA7_COUNT)
#define bfin_write_MXVR_DMA7_COUNT(val)		bfin_write16(MXVR_DMA7_COUNT, val)
#define bfin_read_MXVR_DMA7_CURR_ADDR() 	bfin_read32(MXVR_DMA7_CURR_ADDR)
#define bfin_write_MXVR_DMA7_CURR_ADDR(val) 	bfin_write32(MXVR_DMA7_CURR_ADDR)
#define bfin_read_MXVR_DMA7_CURR_COUNT()	bfin_read16(MXVR_DMA7_CURR_COUNT)
#define bfin_write_MXVR_DMA7_CURR_COUNT(val)	bfin_write16(MXVR_DMA7_CURR_COUNT, val)

/* MXVR Asynch Packet Registers */

#define bfin_read_MXVR_AP_CTL()			bfin_read16(MXVR_AP_CTL)
#define bfin_write_MXVR_AP_CTL(val)		bfin_write16(MXVR_AP_CTL, val)
#define bfin_read_MXVR_APRB_START_ADDR() 	bfin_read32(MXVR_APRB_START_ADDR)
#define bfin_write_MXVR_APRB_START_ADDR(val) 	bfin_write32(MXVR_APRB_START_ADDR)
#define bfin_read_MXVR_APRB_CURR_ADDR() 	bfin_read32(MXVR_APRB_CURR_ADDR)
#define bfin_write_MXVR_APRB_CURR_ADDR(val) 	bfin_write32(MXVR_APRB_CURR_ADDR)
#define bfin_read_MXVR_APTB_START_ADDR() 	bfin_read32(MXVR_APTB_START_ADDR)
#define bfin_write_MXVR_APTB_START_ADDR(val) 	bfin_write32(MXVR_APTB_START_ADDR)
#define bfin_read_MXVR_APTB_CURR_ADDR() 	bfin_read32(MXVR_APTB_CURR_ADDR)
#define bfin_write_MXVR_APTB_CURR_ADDR(val) 	bfin_write32(MXVR_APTB_CURR_ADDR)

/* MXVR Control Message Registers */

#define bfin_read_MXVR_CM_CTL()			bfin_read32(MXVR_CM_CTL)
#define bfin_write_MXVR_CM_CTL(val)		bfin_write32(MXVR_CM_CTL, val)
#define bfin_read_MXVR_CMRB_START_ADDR() 	bfin_read32(MXVR_CMRB_START_ADDR)
#define bfin_write_MXVR_CMRB_START_ADDR(val) 	bfin_write32(MXVR_CMRB_START_ADDR)
#define bfin_read_MXVR_CMRB_CURR_ADDR() 	bfin_read32(MXVR_CMRB_CURR_ADDR)
#define bfin_write_MXVR_CMRB_CURR_ADDR(val) 	bfin_write32(MXVR_CMRB_CURR_ADDR)
#define bfin_read_MXVR_CMTB_START_ADDR() 	bfin_read32(MXVR_CMTB_START_ADDR)
#define bfin_write_MXVR_CMTB_START_ADDR(val) 	bfin_write32(MXVR_CMTB_START_ADDR)
#define bfin_read_MXVR_CMTB_CURR_ADDR() 	bfin_read32(MXVR_CMTB_CURR_ADDR)
#define bfin_write_MXVR_CMTB_CURR_ADDR(val) 	bfin_write32(MXVR_CMTB_CURR_ADDR)

/* MXVR Remote Read Registers */

#define bfin_read_MXVR_RRDB_START_ADDR() 	bfin_read32(MXVR_RRDB_START_ADDR)
#define bfin_write_MXVR_RRDB_START_ADDR(val) 	bfin_write32(MXVR_RRDB_START_ADDR)
#define bfin_read_MXVR_RRDB_CURR_ADDR() 	bfin_read32(MXVR_RRDB_CURR_ADDR)
#define bfin_write_MXVR_RRDB_CURR_ADDR(val) 	bfin_write32(MXVR_RRDB_CURR_ADDR)

/* MXVR Pattern Data Registers */

#define bfin_read_MXVR_PAT_DATA_0()		bfin_read32(MXVR_PAT_DATA_0)
#define bfin_write_MXVR_PAT_DATA_0(val)		bfin_write32(MXVR_PAT_DATA_0, val)
#define bfin_read_MXVR_PAT_EN_0()		bfin_read32(MXVR_PAT_EN_0)
#define bfin_write_MXVR_PAT_EN_0(val)		bfin_write32(MXVR_PAT_EN_0, val)
#define bfin_read_MXVR_PAT_DATA_1()		bfin_read32(MXVR_PAT_DATA_1)
#define bfin_write_MXVR_PAT_DATA_1(val)		bfin_write32(MXVR_PAT_DATA_1, val)
#define bfin_read_MXVR_PAT_EN_1()		bfin_read32(MXVR_PAT_EN_1)
#define bfin_write_MXVR_PAT_EN_1(val)		bfin_write32(MXVR_PAT_EN_1, val)

/* MXVR Frame Counter Registers */

#define bfin_read_MXVR_FRAME_CNT_0()		bfin_read16(MXVR_FRAME_CNT_0)
#define bfin_write_MXVR_FRAME_CNT_0(val)	bfin_write16(MXVR_FRAME_CNT_0, val)
#define bfin_read_MXVR_FRAME_CNT_1()		bfin_read16(MXVR_FRAME_CNT_1)
#define bfin_write_MXVR_FRAME_CNT_1(val)	bfin_write16(MXVR_FRAME_CNT_1, val)

/* MXVR Routing Table Registers */

#define bfin_read_MXVR_ROUTING_0()		bfin_read32(MXVR_ROUTING_0)
#define bfin_write_MXVR_ROUTING_0(val)		bfin_write32(MXVR_ROUTING_0, val)
#define bfin_read_MXVR_ROUTING_1()		bfin_read32(MXVR_ROUTING_1)
#define bfin_write_MXVR_ROUTING_1(val)		bfin_write32(MXVR_ROUTING_1, val)
#define bfin_read_MXVR_ROUTING_2()		bfin_read32(MXVR_ROUTING_2)
#define bfin_write_MXVR_ROUTING_2(val)		bfin_write32(MXVR_ROUTING_2, val)
#define bfin_read_MXVR_ROUTING_3()		bfin_read32(MXVR_ROUTING_3)
#define bfin_write_MXVR_ROUTING_3(val)		bfin_write32(MXVR_ROUTING_3, val)
#define bfin_read_MXVR_ROUTING_4()		bfin_read32(MXVR_ROUTING_4)
#define bfin_write_MXVR_ROUTING_4(val)		bfin_write32(MXVR_ROUTING_4, val)
#define bfin_read_MXVR_ROUTING_5()		bfin_read32(MXVR_ROUTING_5)
#define bfin_write_MXVR_ROUTING_5(val)		bfin_write32(MXVR_ROUTING_5, val)
#define bfin_read_MXVR_ROUTING_6()		bfin_read32(MXVR_ROUTING_6)
#define bfin_write_MXVR_ROUTING_6(val)		bfin_write32(MXVR_ROUTING_6, val)
#define bfin_read_MXVR_ROUTING_7()		bfin_read32(MXVR_ROUTING_7)
#define bfin_write_MXVR_ROUTING_7(val)		bfin_write32(MXVR_ROUTING_7, val)
#define bfin_read_MXVR_ROUTING_8()		bfin_read32(MXVR_ROUTING_8)
#define bfin_write_MXVR_ROUTING_8(val)		bfin_write32(MXVR_ROUTING_8, val)
#define bfin_read_MXVR_ROUTING_9()		bfin_read32(MXVR_ROUTING_9)
#define bfin_write_MXVR_ROUTING_9(val)		bfin_write32(MXVR_ROUTING_9, val)
#define bfin_read_MXVR_ROUTING_10()		bfin_read32(MXVR_ROUTING_10)
#define bfin_write_MXVR_ROUTING_10(val)		bfin_write32(MXVR_ROUTING_10, val)
#define bfin_read_MXVR_ROUTING_11()		bfin_read32(MXVR_ROUTING_11)
#define bfin_write_MXVR_ROUTING_11(val)		bfin_write32(MXVR_ROUTING_11, val)
#define bfin_read_MXVR_ROUTING_12()		bfin_read32(MXVR_ROUTING_12)
#define bfin_write_MXVR_ROUTING_12(val)		bfin_write32(MXVR_ROUTING_12, val)
#define bfin_read_MXVR_ROUTING_13()		bfin_read32(MXVR_ROUTING_13)
#define bfin_write_MXVR_ROUTING_13(val)		bfin_write32(MXVR_ROUTING_13, val)
#define bfin_read_MXVR_ROUTING_14()		bfin_read32(MXVR_ROUTING_14)
#define bfin_write_MXVR_ROUTING_14(val)		bfin_write32(MXVR_ROUTING_14, val)

/* MXVR Counter-Clock-Control Registers */

#define bfin_read_MXVR_BLOCK_CNT()		bfin_read16(MXVR_BLOCK_CNT)
#define bfin_write_MXVR_BLOCK_CNT(val)		bfin_write16(MXVR_BLOCK_CNT, val)
#define bfin_read_MXVR_CLK_CTL()		bfin_read32(MXVR_CLK_CTL)
#define bfin_write_MXVR_CLK_CTL(val)		bfin_write32(MXVR_CLK_CTL, val)
#define bfin_read_MXVR_CDRPLL_CTL()		bfin_read32(MXVR_CDRPLL_CTL)
#define bfin_write_MXVR_CDRPLL_CTL(val)		bfin_write32(MXVR_CDRPLL_CTL, val)
#define bfin_read_MXVR_FMPLL_CTL()		bfin_read32(MXVR_FMPLL_CTL)
#define bfin_write_MXVR_FMPLL_CTL(val)		bfin_write32(MXVR_FMPLL_CTL, val)
#define bfin_read_MXVR_PIN_CTL()		bfin_read16(MXVR_PIN_CTL)
#define bfin_write_MXVR_PIN_CTL(val)		bfin_write16(MXVR_PIN_CTL, val)
#define bfin_read_MXVR_SCLK_CNT()		bfin_read16(MXVR_SCLK_CNT)
#define bfin_write_MXVR_SCLK_CNT(val)		bfin_write16(MXVR_SCLK_CNT, val)

#endif /* _CDEF_BF549_H */

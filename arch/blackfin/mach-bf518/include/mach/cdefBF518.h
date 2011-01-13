/*
 * Copyright 2008-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _CDEF_BF518_H
#define _CDEF_BF518_H

/* BF518 is BF516 + IEEE-1588 */
#include "cdefBF516.h"

/* PTP TSYNC Registers */

#define bfin_read_EMAC_PTP_CTL()                bfin_read16(EMAC_PTP_CTL)
#define bfin_write_EMAC_PTP_CTL(val)            bfin_write16(EMAC_PTP_CTL, val)
#define bfin_read_EMAC_PTP_IE()                 bfin_read16(EMAC_PTP_IE)
#define bfin_write_EMAC_PTP_IE(val)             bfin_write16(EMAC_PTP_IE, val)
#define bfin_read_EMAC_PTP_ISTAT()              bfin_read16(EMAC_PTP_ISTAT)
#define bfin_write_EMAC_PTP_ISTAT(val)          bfin_write16(EMAC_PTP_ISTAT, val)
#define bfin_read_EMAC_PTP_FOFF()               bfin_read32(EMAC_PTP_FOFF)
#define bfin_write_EMAC_PTP_FOFF(val)           bfin_write32(EMAC_PTP_FOFF, val)
#define bfin_read_EMAC_PTP_FV1()                bfin_read32(EMAC_PTP_FV1)
#define bfin_write_EMAC_PTP_FV1(val)            bfin_write32(EMAC_PTP_FV1, val)
#define bfin_read_EMAC_PTP_FV2()                bfin_read32(EMAC_PTP_FV2)
#define bfin_write_EMAC_PTP_FV2(val)            bfin_write32(EMAC_PTP_FV2, val)
#define bfin_read_EMAC_PTP_FV3()                bfin_read32(EMAC_PTP_FV3)
#define bfin_write_EMAC_PTP_FV3(val)            bfin_write32(EMAC_PTP_FV3, val)
#define bfin_read_EMAC_PTP_ADDEND()             bfin_read32(EMAC_PTP_ADDEND)
#define bfin_write_EMAC_PTP_ADDEND(val)         bfin_write32(EMAC_PTP_ADDEND, val)
#define bfin_read_EMAC_PTP_ACCR()               bfin_read32(EMAC_PTP_ACCR)
#define bfin_write_EMAC_PTP_ACCR(val)           bfin_write32(EMAC_PTP_ACCR, val)
#define bfin_read_EMAC_PTP_OFFSET()             bfin_read32(EMAC_PTP_OFFSET)
#define bfin_write_EMAC_PTP_OFFSET(val)         bfin_write32(EMAC_PTP_OFFSET, val)
#define bfin_read_EMAC_PTP_TIMELO()             bfin_read32(EMAC_PTP_TIMELO)
#define bfin_write_EMAC_PTP_TIMELO(val)         bfin_write32(EMAC_PTP_TIMELO, val)
#define bfin_read_EMAC_PTP_TIMEHI()             bfin_read32(EMAC_PTP_TIMEHI)
#define bfin_write_EMAC_PTP_TIMEHI(val)         bfin_write32(EMAC_PTP_TIMEHI, val)
#define bfin_read_EMAC_PTP_RXSNAPLO()           bfin_read32(EMAC_PTP_RXSNAPLO)
#define bfin_read_EMAC_PTP_RXSNAPHI()           bfin_read32(EMAC_PTP_RXSNAPHI)
#define bfin_read_EMAC_PTP_TXSNAPLO()           bfin_read32(EMAC_PTP_TXSNAPLO)
#define bfin_read_EMAC_PTP_TXSNAPHI()           bfin_read32(EMAC_PTP_TXSNAPHI)
#define bfin_read_EMAC_PTP_ALARMLO()            bfin_read32(EMAC_PTP_ALARMLO)
#define bfin_write_EMAC_PTP_ALARMLO(val)        bfin_write32(EMAC_PTP_ALARMLO, val)
#define bfin_read_EMAC_PTP_ALARMHI()            bfin_read32(EMAC_PTP_ALARMHI)
#define bfin_write_EMAC_PTP_ALARMHI(val)        bfin_write32(EMAC_PTP_ALARMHI, val)
#define bfin_read_EMAC_PTP_ID_OFF()             bfin_read16(EMAC_PTP_ID_OFF)
#define bfin_write_EMAC_PTP_ID_OFF(val)         bfin_write16(EMAC_PTP_ID_OFF, val)
#define bfin_read_EMAC_PTP_ID_SNAP()            bfin_read32(EMAC_PTP_ID_SNAP)
#define bfin_write_EMAC_PTP_ID_SNAP(val)        bfin_write32(EMAC_PTP_ID_SNAP, val)
#define bfin_read_EMAC_PTP_PPS_STARTHI()        bfin_read32(EMAC_PTP_PPS_STARTHI)
#define bfin_write_EMAC_PTP_PPS_STARTHI(val)    bfin_write32(EMAC_PTP_PPS_STARTHI, val)
#define bfin_read_EMAC_PTP_PPS_PERIOD()         bfin_read32(EMAC_PTP_PPS_PERIOD)
#define bfin_write_EMAC_PTP_PPS_PERIOD(val)     bfin_write32(EMAC_PTP_PPS_PERIOD, val)

#endif /* _CDEF_BF518_H */

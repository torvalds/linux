/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_SYS_H_
#define _MSCC_OCELOT_SYS_H_

#define SYS_COUNT_RX_OCTETS_RSZ                           0x4

#define SYS_COUNT_TX_OCTETS_RSZ                           0x4

#define SYS_PORT_MODE_RSZ                                 0x4

#define SYS_PORT_MODE_DATA_WO_TS(x)                       (((x) << 5) & GENMASK(6, 5))
#define SYS_PORT_MODE_DATA_WO_TS_M                        GENMASK(6, 5)
#define SYS_PORT_MODE_DATA_WO_TS_X(x)                     (((x) & GENMASK(6, 5)) >> 5)
#define SYS_PORT_MODE_INCL_INJ_HDR(x)                     (((x) << 3) & GENMASK(4, 3))
#define SYS_PORT_MODE_INCL_INJ_HDR_M                      GENMASK(4, 3)
#define SYS_PORT_MODE_INCL_INJ_HDR_X(x)                   (((x) & GENMASK(4, 3)) >> 3)
#define SYS_PORT_MODE_INCL_XTR_HDR(x)                     (((x) << 1) & GENMASK(2, 1))
#define SYS_PORT_MODE_INCL_XTR_HDR_M                      GENMASK(2, 1)
#define SYS_PORT_MODE_INCL_XTR_HDR_X(x)                   (((x) & GENMASK(2, 1)) >> 1)
#define SYS_PORT_MODE_INJ_HDR_ERR                         BIT(0)

#define SYS_FRONT_PORT_MODE_RSZ                           0x4

#define SYS_FRONT_PORT_MODE_HDX_MODE                      BIT(0)

#define SYS_FRM_AGING_AGE_TX_ENA                          BIT(20)
#define SYS_FRM_AGING_MAX_AGE(x)                          ((x) & GENMASK(19, 0))
#define SYS_FRM_AGING_MAX_AGE_M                           GENMASK(19, 0)

#define SYS_STAT_CFG_STAT_CLEAR_SHOT(x)                   (((x) << 10) & GENMASK(16, 10))
#define SYS_STAT_CFG_STAT_CLEAR_SHOT_M                    GENMASK(16, 10)
#define SYS_STAT_CFG_STAT_CLEAR_SHOT_X(x)                 (((x) & GENMASK(16, 10)) >> 10)
#define SYS_STAT_CFG_STAT_VIEW(x)                         ((x) & GENMASK(9, 0))
#define SYS_STAT_CFG_STAT_VIEW_M                          GENMASK(9, 0)

#define SYS_SW_STATUS_RSZ                                 0x4

#define SYS_SW_STATUS_PORT_RX_PAUSED                      BIT(0)

#define SYS_MISC_CFG_PTP_RSRV_CLR                         BIT(1)
#define SYS_MISC_CFG_PTP_DIS_NEG_RO                       BIT(0)

#define SYS_REW_MAC_HIGH_CFG_RSZ                          0x4

#define SYS_REW_MAC_LOW_CFG_RSZ                           0x4

#define SYS_TIMESTAMP_OFFSET_ETH_TYPE_CFG(x)              (((x) << 6) & GENMASK(21, 6))
#define SYS_TIMESTAMP_OFFSET_ETH_TYPE_CFG_M               GENMASK(21, 6)
#define SYS_TIMESTAMP_OFFSET_ETH_TYPE_CFG_X(x)            (((x) & GENMASK(21, 6)) >> 6)
#define SYS_TIMESTAMP_OFFSET_TIMESTAMP_OFFSET(x)          ((x) & GENMASK(5, 0))
#define SYS_TIMESTAMP_OFFSET_TIMESTAMP_OFFSET_M           GENMASK(5, 0)

#define SYS_PAUSE_CFG_RSZ                                 0x4

#define SYS_PAUSE_CFG_PAUSE_START(x)                      (((x) << 10) & GENMASK(18, 10))
#define SYS_PAUSE_CFG_PAUSE_START_M                       GENMASK(18, 10)
#define SYS_PAUSE_CFG_PAUSE_START_X(x)                    (((x) & GENMASK(18, 10)) >> 10)
#define SYS_PAUSE_CFG_PAUSE_STOP(x)                       (((x) << 1) & GENMASK(9, 1))
#define SYS_PAUSE_CFG_PAUSE_STOP_M                        GENMASK(9, 1)
#define SYS_PAUSE_CFG_PAUSE_STOP_X(x)                     (((x) & GENMASK(9, 1)) >> 1)
#define SYS_PAUSE_CFG_PAUSE_ENA                           BIT(0)

#define SYS_PAUSE_TOT_CFG_PAUSE_TOT_START(x)              (((x) << 9) & GENMASK(17, 9))
#define SYS_PAUSE_TOT_CFG_PAUSE_TOT_START_M               GENMASK(17, 9)
#define SYS_PAUSE_TOT_CFG_PAUSE_TOT_START_X(x)            (((x) & GENMASK(17, 9)) >> 9)
#define SYS_PAUSE_TOT_CFG_PAUSE_TOT_STOP(x)               ((x) & GENMASK(8, 0))
#define SYS_PAUSE_TOT_CFG_PAUSE_TOT_STOP_M                GENMASK(8, 0)

#define SYS_ATOP_RSZ                                      0x4

#define SYS_MAC_FC_CFG_RSZ                                0x4

#define SYS_MAC_FC_CFG_FC_LINK_SPEED(x)                   (((x) << 26) & GENMASK(27, 26))
#define SYS_MAC_FC_CFG_FC_LINK_SPEED_M                    GENMASK(27, 26)
#define SYS_MAC_FC_CFG_FC_LINK_SPEED_X(x)                 (((x) & GENMASK(27, 26)) >> 26)
#define SYS_MAC_FC_CFG_FC_LATENCY_CFG(x)                  (((x) << 20) & GENMASK(25, 20))
#define SYS_MAC_FC_CFG_FC_LATENCY_CFG_M                   GENMASK(25, 20)
#define SYS_MAC_FC_CFG_FC_LATENCY_CFG_X(x)                (((x) & GENMASK(25, 20)) >> 20)
#define SYS_MAC_FC_CFG_ZERO_PAUSE_ENA                     BIT(18)
#define SYS_MAC_FC_CFG_TX_FC_ENA                          BIT(17)
#define SYS_MAC_FC_CFG_RX_FC_ENA                          BIT(16)
#define SYS_MAC_FC_CFG_PAUSE_VAL_CFG(x)                   ((x) & GENMASK(15, 0))
#define SYS_MAC_FC_CFG_PAUSE_VAL_CFG_M                    GENMASK(15, 0)

#define SYS_MMGT_RELCNT(x)                                (((x) << 16) & GENMASK(31, 16))
#define SYS_MMGT_RELCNT_M                                 GENMASK(31, 16)
#define SYS_MMGT_RELCNT_X(x)                              (((x) & GENMASK(31, 16)) >> 16)
#define SYS_MMGT_FREECNT(x)                               ((x) & GENMASK(15, 0))
#define SYS_MMGT_FREECNT_M                                GENMASK(15, 0)

#define SYS_MMGT_FAST_FREEVLD(x)                          (((x) << 4) & GENMASK(7, 4))
#define SYS_MMGT_FAST_FREEVLD_M                           GENMASK(7, 4)
#define SYS_MMGT_FAST_FREEVLD_X(x)                        (((x) & GENMASK(7, 4)) >> 4)
#define SYS_MMGT_FAST_RELVLD(x)                           ((x) & GENMASK(3, 0))
#define SYS_MMGT_FAST_RELVLD_M                            GENMASK(3, 0)

#define SYS_EVENTS_DIF_RSZ                                0x4

#define SYS_EVENTS_DIF_EV_DRX(x)                          (((x) << 6) & GENMASK(8, 6))
#define SYS_EVENTS_DIF_EV_DRX_M                           GENMASK(8, 6)
#define SYS_EVENTS_DIF_EV_DRX_X(x)                        (((x) & GENMASK(8, 6)) >> 6)
#define SYS_EVENTS_DIF_EV_DTX(x)                          ((x) & GENMASK(5, 0))
#define SYS_EVENTS_DIF_EV_DTX_M                           GENMASK(5, 0)

#define SYS_EVENTS_CORE_EV_FWR                            BIT(2)
#define SYS_EVENTS_CORE_EV_ANA(x)                         ((x) & GENMASK(1, 0))
#define SYS_EVENTS_CORE_EV_ANA_M                          GENMASK(1, 0)

#define SYS_CNT_GSZ                                       0x4

#define SYS_PTP_STATUS_PTP_TXSTAMP_OAM                    BIT(29)
#define SYS_PTP_STATUS_PTP_OVFL                           BIT(28)
#define SYS_PTP_STATUS_PTP_MESS_VLD                       BIT(27)
#define SYS_PTP_STATUS_PTP_MESS_ID(x)                     (((x) << 21) & GENMASK(26, 21))
#define SYS_PTP_STATUS_PTP_MESS_ID_M                      GENMASK(26, 21)
#define SYS_PTP_STATUS_PTP_MESS_ID_X(x)                   (((x) & GENMASK(26, 21)) >> 21)
#define SYS_PTP_STATUS_PTP_MESS_TXPORT(x)                 (((x) << 16) & GENMASK(20, 16))
#define SYS_PTP_STATUS_PTP_MESS_TXPORT_M                  GENMASK(20, 16)
#define SYS_PTP_STATUS_PTP_MESS_TXPORT_X(x)               (((x) & GENMASK(20, 16)) >> 16)
#define SYS_PTP_STATUS_PTP_MESS_SEQ_ID(x)                 ((x) & GENMASK(15, 0))
#define SYS_PTP_STATUS_PTP_MESS_SEQ_ID_M                  GENMASK(15, 0)

#define SYS_PTP_TXSTAMP_PTP_TXSTAMP(x)                    ((x) & GENMASK(29, 0))
#define SYS_PTP_TXSTAMP_PTP_TXSTAMP_M                     GENMASK(29, 0)
#define SYS_PTP_TXSTAMP_PTP_TXSTAMP_SEC                   BIT(31)

#define SYS_PTP_NXT_PTP_NXT                               BIT(0)

#define SYS_PTP_CFG_PTP_STAMP_WID(x)                      (((x) << 2) & GENMASK(7, 2))
#define SYS_PTP_CFG_PTP_STAMP_WID_M                       GENMASK(7, 2)
#define SYS_PTP_CFG_PTP_STAMP_WID_X(x)                    (((x) & GENMASK(7, 2)) >> 2)
#define SYS_PTP_CFG_PTP_CF_ROLL_MODE(x)                   ((x) & GENMASK(1, 0))
#define SYS_PTP_CFG_PTP_CF_ROLL_MODE_M                    GENMASK(1, 0)

#define SYS_RAM_INIT_RAM_INIT                             BIT(1)
#define SYS_RAM_INIT_RAM_CFG_HOOK                         BIT(0)

#endif

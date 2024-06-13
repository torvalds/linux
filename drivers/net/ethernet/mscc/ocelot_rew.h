/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_REW_H_
#define _MSCC_OCELOT_REW_H_

#define REW_PORT_VLAN_CFG_GSZ                             0x80

#define REW_PORT_VLAN_CFG_PORT_TPID(x)                    (((x) << 16) & GENMASK(31, 16))
#define REW_PORT_VLAN_CFG_PORT_TPID_M                     GENMASK(31, 16)
#define REW_PORT_VLAN_CFG_PORT_TPID_X(x)                  (((x) & GENMASK(31, 16)) >> 16)
#define REW_PORT_VLAN_CFG_PORT_DEI                        BIT(15)
#define REW_PORT_VLAN_CFG_PORT_PCP(x)                     (((x) << 12) & GENMASK(14, 12))
#define REW_PORT_VLAN_CFG_PORT_PCP_M                      GENMASK(14, 12)
#define REW_PORT_VLAN_CFG_PORT_PCP_X(x)                   (((x) & GENMASK(14, 12)) >> 12)
#define REW_PORT_VLAN_CFG_PORT_VID(x)                     ((x) & GENMASK(11, 0))
#define REW_PORT_VLAN_CFG_PORT_VID_M                      GENMASK(11, 0)

#define REW_TAG_CFG_GSZ                                   0x80

#define REW_TAG_CFG_TAG_CFG(x)                            (((x) << 7) & GENMASK(8, 7))
#define REW_TAG_CFG_TAG_CFG_M                             GENMASK(8, 7)
#define REW_TAG_CFG_TAG_CFG_X(x)                          (((x) & GENMASK(8, 7)) >> 7)
#define REW_TAG_CFG_TAG_TPID_CFG(x)                       (((x) << 5) & GENMASK(6, 5))
#define REW_TAG_CFG_TAG_TPID_CFG_M                        GENMASK(6, 5)
#define REW_TAG_CFG_TAG_TPID_CFG_X(x)                     (((x) & GENMASK(6, 5)) >> 5)
#define REW_TAG_CFG_TAG_VID_CFG                           BIT(4)
#define REW_TAG_CFG_TAG_PCP_CFG(x)                        (((x) << 2) & GENMASK(3, 2))
#define REW_TAG_CFG_TAG_PCP_CFG_M                         GENMASK(3, 2)
#define REW_TAG_CFG_TAG_PCP_CFG_X(x)                      (((x) & GENMASK(3, 2)) >> 2)
#define REW_TAG_CFG_TAG_DEI_CFG(x)                        ((x) & GENMASK(1, 0))
#define REW_TAG_CFG_TAG_DEI_CFG_M                         GENMASK(1, 0)

#define REW_PORT_CFG_GSZ                                  0x80

#define REW_PORT_CFG_ES0_EN                               BIT(5)
#define REW_PORT_CFG_FCS_UPDATE_NONCPU_CFG(x)             (((x) << 3) & GENMASK(4, 3))
#define REW_PORT_CFG_FCS_UPDATE_NONCPU_CFG_M              GENMASK(4, 3)
#define REW_PORT_CFG_FCS_UPDATE_NONCPU_CFG_X(x)           (((x) & GENMASK(4, 3)) >> 3)
#define REW_PORT_CFG_FCS_UPDATE_CPU_ENA                   BIT(2)
#define REW_PORT_CFG_FLUSH_ENA                            BIT(1)
#define REW_PORT_CFG_AGE_DIS                              BIT(0)

#define REW_DSCP_CFG_GSZ                                  0x80

#define REW_PCP_DEI_QOS_MAP_CFG_GSZ                       0x80
#define REW_PCP_DEI_QOS_MAP_CFG_RSZ                       0x4

#define REW_PCP_DEI_QOS_MAP_CFG_DEI_QOS_VAL               BIT(3)
#define REW_PCP_DEI_QOS_MAP_CFG_PCP_QOS_VAL(x)            ((x) & GENMASK(2, 0))
#define REW_PCP_DEI_QOS_MAP_CFG_PCP_QOS_VAL_M             GENMASK(2, 0)

#define REW_PTP_CFG_GSZ                                   0x80

#define REW_PTP_CFG_PTP_BACKPLANE_MODE                    BIT(7)
#define REW_PTP_CFG_GP_CFG_UNUSED(x)                      (((x) << 3) & GENMASK(6, 3))
#define REW_PTP_CFG_GP_CFG_UNUSED_M                       GENMASK(6, 3)
#define REW_PTP_CFG_GP_CFG_UNUSED_X(x)                    (((x) & GENMASK(6, 3)) >> 3)
#define REW_PTP_CFG_PTP_1STEP_DIS                         BIT(2)
#define REW_PTP_CFG_PTP_2STEP_DIS                         BIT(1)
#define REW_PTP_CFG_PTP_UDP_KEEP                          BIT(0)

#define REW_PTP_DLY1_CFG_GSZ                              0x80

#define REW_RED_TAG_CFG_GSZ                               0x80

#define REW_RED_TAG_CFG_RED_TAG_CFG                       BIT(0)

#define REW_DSCP_REMAP_DP1_CFG_RSZ                        0x4

#define REW_DSCP_REMAP_CFG_RSZ                            0x4

#define REW_REW_STICKY_ES0_TAGB_PUSH_FAILED               BIT(0)

#define REW_PPT_RSZ                                       0x4

#endif

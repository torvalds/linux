/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2018 Microsemi Corporation
 */

#ifndef _OCELOT_S2_CORE_H_
#define _OCELOT_S2_CORE_H_

#define S2_CORE_UPDATE_CTRL_UPDATE_CMD(x)      (((x) << 22) & GENMASK(24, 22))
#define S2_CORE_UPDATE_CTRL_UPDATE_CMD_M       GENMASK(24, 22)
#define S2_CORE_UPDATE_CTRL_UPDATE_CMD_X(x)    (((x) & GENMASK(24, 22)) >> 22)
#define S2_CORE_UPDATE_CTRL_UPDATE_ENTRY_DIS   BIT(21)
#define S2_CORE_UPDATE_CTRL_UPDATE_ACTION_DIS  BIT(20)
#define S2_CORE_UPDATE_CTRL_UPDATE_CNT_DIS     BIT(19)
#define S2_CORE_UPDATE_CTRL_UPDATE_ADDR(x)     (((x) << 3) & GENMASK(18, 3))
#define S2_CORE_UPDATE_CTRL_UPDATE_ADDR_M      GENMASK(18, 3)
#define S2_CORE_UPDATE_CTRL_UPDATE_ADDR_X(x)   (((x) & GENMASK(18, 3)) >> 3)
#define S2_CORE_UPDATE_CTRL_UPDATE_SHOT        BIT(2)
#define S2_CORE_UPDATE_CTRL_CLEAR_CACHE        BIT(1)
#define S2_CORE_UPDATE_CTRL_MV_TRAFFIC_IGN     BIT(0)

#define S2_CORE_MV_CFG_MV_NUM_POS(x)           (((x) << 16) & GENMASK(31, 16))
#define S2_CORE_MV_CFG_MV_NUM_POS_M            GENMASK(31, 16)
#define S2_CORE_MV_CFG_MV_NUM_POS_X(x)         (((x) & GENMASK(31, 16)) >> 16)
#define S2_CORE_MV_CFG_MV_SIZE(x)              ((x) & GENMASK(15, 0))
#define S2_CORE_MV_CFG_MV_SIZE_M               GENMASK(15, 0)

#define S2_CACHE_ENTRY_DAT_RSZ                 0x4

#define S2_CACHE_MASK_DAT_RSZ                  0x4

#define S2_CACHE_ACTION_DAT_RSZ                0x4

#define S2_CACHE_CNT_DAT_RSZ                   0x4

#define S2_STICKY_VCAP_ROW_DELETED_STICKY      BIT(0)

#define S2_BIST_CTRL_TCAM_BIST                 BIT(1)
#define S2_BIST_CTRL_TCAM_INIT                 BIT(0)

#define S2_BIST_CFG_TCAM_BIST_SOE_ENA          BIT(8)
#define S2_BIST_CFG_TCAM_HCG_DIS               BIT(7)
#define S2_BIST_CFG_TCAM_CG_DIS                BIT(6)
#define S2_BIST_CFG_TCAM_BIAS(x)               ((x) & GENMASK(5, 0))
#define S2_BIST_CFG_TCAM_BIAS_M                GENMASK(5, 0)

#define S2_BIST_STAT_BIST_RT_ERR               BIT(15)
#define S2_BIST_STAT_BIST_PENC_ERR             BIT(14)
#define S2_BIST_STAT_BIST_COMP_ERR             BIT(13)
#define S2_BIST_STAT_BIST_ADDR_ERR             BIT(12)
#define S2_BIST_STAT_BIST_BL1E_ERR             BIT(11)
#define S2_BIST_STAT_BIST_BL1_ERR              BIT(10)
#define S2_BIST_STAT_BIST_BL0E_ERR             BIT(9)
#define S2_BIST_STAT_BIST_BL0_ERR              BIT(8)
#define S2_BIST_STAT_BIST_PH1_ERR              BIT(7)
#define S2_BIST_STAT_BIST_PH0_ERR              BIT(6)
#define S2_BIST_STAT_BIST_PV1_ERR              BIT(5)
#define S2_BIST_STAT_BIST_PV0_ERR              BIT(4)
#define S2_BIST_STAT_BIST_RUN                  BIT(3)
#define S2_BIST_STAT_BIST_ERR                  BIT(2)
#define S2_BIST_STAT_BIST_BUSY                 BIT(1)
#define S2_BIST_STAT_TCAM_RDY                  BIT(0)

#endif /* _OCELOT_S2_CORE_H_ */

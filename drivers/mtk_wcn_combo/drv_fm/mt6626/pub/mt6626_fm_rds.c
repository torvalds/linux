/* mt6626_rds.c
 *
 * (C) Copyright 2009
 * MediaTek <www.MediaTek.com>
 * hongcheng <hongcheng.xia@MediaTek.com>
 *
 * mt6626 FM Radio Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_interface.h"
#include "fm_stdlib.h"
#include "fm_rds.h"

#include "mt6626_fm_reg.h"


static fm_bool bRDS_FirstIn = fm_false;
static fm_u32 gBLER_CHK_INTERVAL = 5000;
static fm_u16 GOOD_BLK_CNT = 0, BAD_BLK_CNT = 0;
static fm_u8 BAD_BLK_RATIO = 0;

static struct fm_callback *fm_cb = NULL;
static struct fm_basic_interface *fm_bi = NULL;


static fm_bool mt6626_RDS_support(void);
static fm_s32 mt6626_RDS_enable(void);
static fm_s32 mt6626_RDS_disable(void);
static fm_u16 mt6626_RDS_Get_GoodBlock_Counter(void);
static fm_u16 mt6626_RDS_Get_BadBlock_Counter(void);
static fm_u8 mt6626_RDS_Get_BadBlock_Ratio(void);
static fm_u32 mt6626_RDS_Get_BlerCheck_Interval(void);
static void mt6626_RDS_GetData(fm_u16 *data, fm_u16 datalen);
static void mt6626_RDS_Init_Data(rds_t *pstRDSData);



static fm_bool mt6626_RDS_support(void)
{
    return fm_true;
}

static fm_s32 mt6626_RDS_enable(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | RDSC, "rds enable\n");
    fm_bi->read(FM_RDS_CFG0, &dataRead);
    fm_bi->write(FM_RDS_CFG0, 6); //set buf_start_th
    fm_bi->read(FM_MAIN_CTRL, &dataRead);
    fm_bi->write(FM_MAIN_CTRL, dataRead | (RDS_MASK));

    return 0;
}

static fm_s32 mt6626_RDS_disable(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | RDSC, "rds disable\n");
    fm_bi->read(FM_MAIN_CTRL, &dataRead);
    fm_bi->write(FM_MAIN_CTRL, dataRead&(~RDS_MASK));

    return 0;
}

static fm_u16 mt6626_RDS_Get_GoodBlock_Counter(void)
{
    fm_u16 tmp_reg;

    fm_bi->read(FM_RDS_GOODBK_CNT, &tmp_reg);
    GOOD_BLK_CNT = tmp_reg;
    WCN_DBG(FM_DBG | RDSC, "get good block cnt:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_u16 mt6626_RDS_Get_BadBlock_Counter(void)
{
    fm_u16 tmp_reg;

    fm_bi->read(FM_RDS_BADBK_CNT, &tmp_reg);
    BAD_BLK_CNT = tmp_reg;
    WCN_DBG(FM_DBG | RDSC, "get bad block cnt:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_u8 mt6626_RDS_Get_BadBlock_Ratio(void)
{
    fm_u16 tmp_reg;
    fm_u16 gbc;
    fm_u16 bbc;

    gbc = mt6626_RDS_Get_GoodBlock_Counter();
    bbc = mt6626_RDS_Get_BadBlock_Counter();

    if ((gbc + bbc) > 0) {
        tmp_reg = (fm_u8)(bbc * 100 / (gbc + bbc));
    } else {
        tmp_reg = 0;
    }

    BAD_BLK_RATIO = tmp_reg;
    WCN_DBG(FM_DBG | RDSC, "get badblock ratio:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_s32 mt6626_RDS_BlockCounter_Reset(void)
{
    mt6626_RDS_disable();
    mt6626_RDS_enable();

    return 0;
}

static fm_u32 mt6626_RDS_Get_BlerCheck_Interval(void)
{
    return gBLER_CHK_INTERVAL;
}

static fm_s32 mt6626_RDS_BlerCheck(rds_t *dst)
{
    return -1;
}

static void RDS_Recovery_Handler(void)
{
    fm_u16 tempData = 0;

    do {
        fm_bi->read(FM_RDS_DATA_REG, &tempData);
        fm_bi->read(FM_RDS_POINTER, &tempData);
    } while (tempData & 0x3);
}

static void mt6626_RDS_GetData(fm_u16 *data, fm_u16 datalen)
{
#define RDS_GROUP_DIFF_OFS          0x007C
#define RDS_FIFO_DIFF               0x007F
#define RDS_CRC_BLK_ADJ             0x0020
#define RDS_CRC_CORR_CNT            0x001E
#define RDS_CRC_INFO                0x0001

    fm_u16 CRC = 0, i = 0, RDS_adj = 0, RDSDataCount = 0, FM_WARorrCnt = 0;
    fm_u16 temp = 0, OutputPofm_s32 = 0;

    WCN_DBG(FM_DBG | RDSC, "get data\n");
    fm_bi->read(FM_RDS_FIFO_STATUS0, &temp);
    RDSDataCount = ((RDS_GROUP_DIFF_OFS & temp) << 2);

    if ((temp & RDS_FIFO_DIFF) >= 4) {
        //block A data and info handling
        fm_bi->read(FM_RDS_INFO, &temp);
        RDS_adj |= (temp & RDS_CRC_BLK_ADJ) << 10;
        CRC |= (temp & RDS_CRC_INFO) << 3;
        FM_WARorrCnt |= ((temp & RDS_CRC_CORR_CNT) << 11);
        fm_bi->read(FM_RDS_DATA_REG, &data[0]);

        //block B data and info handling
        fm_bi->read(FM_RDS_INFO, &temp);
        RDS_adj |= (temp & RDS_CRC_BLK_ADJ) << 9;
        CRC |= (temp & RDS_CRC_INFO) << 2;
        FM_WARorrCnt |= ((temp & RDS_CRC_CORR_CNT) << 7);
        fm_bi->read(FM_RDS_DATA_REG, &data[1]);

        //block C data and info handling
        fm_bi->read(FM_RDS_INFO, &temp);
        RDS_adj |= (temp & RDS_CRC_BLK_ADJ) << 8;
        CRC |= (temp & RDS_CRC_INFO) << 1;
        FM_WARorrCnt |= ((temp & RDS_CRC_CORR_CNT) << 3);
        fm_bi->read(FM_RDS_DATA_REG, &data[2]);

        //block D data and info handling
        fm_bi->read(FM_RDS_INFO, &temp);
        RDS_adj |= (temp & RDS_CRC_BLK_ADJ) << 7;
        CRC |= (temp & RDS_CRC_INFO);
        FM_WARorrCnt |= ((temp & RDS_CRC_CORR_CNT) >> 1);
        fm_bi->read(FM_RDS_DATA_REG, &data[3]);

        data[4] = FM_WARorrCnt;     // CBC
        data[5] = (CRC | RDS_adj | RDSDataCount);   //CRC

        fm_bi->read(FM_RDS_PWDI, &data[6]);
        fm_bi->read(FM_RDS_PWDQ, &data[7]);

        fm_bi->read(FM_RDS_POINTER, &OutputPofm_s32);

        //Go fm_s32o RDS recovery handler while RDS output pofm_s32 doesn't align to 4 in numeric
        if (OutputPofm_s32 & 0x3) {
            RDS_Recovery_Handler();
        }

    } else {
        for (; i < 8; i++)
            data[i] = 0;
    }
}

static void mt6626_RDS_Init_Data(rds_t *pstRDSData)
{
    fm_memset(pstRDSData, 0, sizeof(rds_t));
    bRDS_FirstIn = fm_true;

    fm_memset(pstRDSData->RT_Data.TextData, 0x20, sizeof(pstRDSData->RT_Data.TextData));
    fm_memset(pstRDSData->PS_Data.PS, '\0', sizeof(pstRDSData->PS_Data.PS));
    fm_memset(pstRDSData->PS_ON, 0x20, sizeof(pstRDSData->PS_ON));
}

fm_bool mt6626_RDS_OnOff(rds_t *dst, fm_bool bFlag)
{
    if (mt6626_RDS_support() == fm_false) {
        WCN_DBG(FM_ALT | RDSC, "mt6626_RDS_OnOff failed, RDS not support\n");
        return fm_false;
    }

    if (bFlag) {
        mt6626_RDS_Init_Data(dst);
        mt6626_RDS_enable();
    } else {
        mt6626_RDS_disable();
    }

    return fm_true;
}

DEFINE_RDSLOG(rds_log);

/* mt6626_RDS_Efm_s32_Handler    -    response FM RDS interrupt
 * @fm - main data structure of FM driver
 * This function first get RDS raw data, then call RDS spec parser
 */
static fm_s32 mt6626_rds_parser(rds_t *rds_dst, struct rds_rx_t *rds_raw, fm_s32 rds_size, fm_u16(*getfreq)(void))
{
    struct rds_rx_t raw;
    fm_u16 fifo_offset;

    do {
        mt6626_RDS_GetData(&raw.data[0].blkA, sizeof(rds_packet_t) + 2);
        fifo_offset = (raw.data[0].crc & FM_RDS_DCO_FIFO_OFST) >> 5; //FM_RDS_DATA_CRC_FFOST
        WCN_DBG(FM_DBG | RDSC, "RDS fifo_offset:%d\n", fifo_offset);
        rds_log.log_in(&rds_log, &raw, sizeof(rds_packet_t) + 2*sizeof(fm_u16));
        rds_parser(rds_dst, &raw, sizeof(rds_packet_t) + 2*sizeof(fm_u16), getfreq);
    } while (fifo_offset > 1);

    return 0;
}

static fm_s32 mt6626_rds_log_get(struct rds_rx_t *dst, fm_s32 *dst_len)
{
    return rds_log.log_out(&rds_log, dst, dst_len);
}

static fm_s32 mt6626_rds_gc_get(struct rds_group_cnt_t *dst, rds_t *rdsp)
{
    return rds_grp_counter_get(dst, &rdsp->gc);
}

static fm_s32 mt6626_rds_gc_reset(rds_t *rdsp)
{
    return rds_grp_counter_reset(&rdsp->gc);
}

fm_s32 fm_rds_ops_register(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;

    FMR_ASSERT(ops);
    FMR_ASSERT(ops->bi.write);
    FMR_ASSERT(ops->bi.read);
    FMR_ASSERT(ops->bi.setbits);
    FMR_ASSERT(ops->bi.usdelay);
    fm_bi = &ops->bi;

    FMR_ASSERT(ops->cb.cur_freq_get);
    FMR_ASSERT(ops->cb.cur_freq_set);
    fm_cb = &ops->cb;

    ops->ri.rds_blercheck = mt6626_RDS_BlerCheck;
    ops->ri.rds_onoff = mt6626_RDS_OnOff;
    ops->ri.rds_parser = mt6626_rds_parser;
    ops->ri.rds_gbc_get = mt6626_RDS_Get_GoodBlock_Counter;
    ops->ri.rds_bbc_get = mt6626_RDS_Get_BadBlock_Counter;
    ops->ri.rds_bbr_get = mt6626_RDS_Get_BadBlock_Ratio;
    ops->ri.rds_bc_reset = mt6626_RDS_BlockCounter_Reset;
    ops->ri.rds_bci_get = mt6626_RDS_Get_BlerCheck_Interval;
    ops->ri.rds_log_get = mt6626_rds_log_get;
    ops->ri.rds_gc_get = mt6626_rds_gc_get;
    ops->ri.rds_gc_reset = mt6626_rds_gc_reset;
    return ret;
}

fm_s32 fm_rds_ops_unregister(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;

    FMR_ASSERT(ops);

    fm_bi = NULL;
    fm_memset(&ops->ri, 0, sizeof(struct fm_rds_interface));
    return ret;
}


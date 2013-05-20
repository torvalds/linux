/* mt6620_rds.c
 *
 * (C) Copyright 2011
 * MediaTek <www.MediaTek.com>
 * hongcheng <hongcheng.xia@MediaTek.com>
 *
 * mt6620 FM Radio Driver
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
#include "mt6620_fm_reg.h"


#define MT6620_RDS_BLER_TH1 90
#define MT6620_RDS_BLER_TH2 60
#define MT6620_RDS_BLER_C1  12
#define MT6620_RDS_BLER_C2  6
#define MT6620_RDS_BLER_T1  5000
#define MT6620_RDS_BLER_T2  5000

static fm_bool bRDS_FirstIn = fm_false;
static fm_u32 gBLER_CHK_INTERVAL = 5000;
static fm_u16 GOOD_BLK_CNT = 0, BAD_BLK_CNT = 0;
static fm_u8 BAD_BLK_RATIO = 0;

static struct fm_callback *fm_cb = NULL;
static struct fm_basic_interface *fm_bi = NULL;

//static fm_bool mt6620_RDS_support(void);
static fm_s32 mt6620_RDS_enable(void);
static fm_s32 mt6620_RDS_disable(void);
static fm_u16 mt6620_RDS_Get_GoodBlock_Counter(void);
static fm_u16 mt6620_RDS_Get_BadBlock_Counter(void);
static fm_u8 mt6620_RDS_Get_BadBlock_Ratio(void);
static fm_u32 mt6620_RDS_Get_BlerCheck_Interval(void);
static void mt6620_RDS_Init_Data(rds_t *pstRDSData);

#if 0
static fm_bool mt6620_RDS_support(void)
{
    return fm_true;
}
#endif
static fm_s32 mt6620_RDS_enable(void)
{
    fm_u16 page;

    WCN_DBG(FM_DBG | RDSC, "rds enable\n");
    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003); //sleect page3

    fm_bi->write(0xCB, 0xE016);

    fm_bi->write(FM_MAIN_PGSEL, 0x0000); //sleect page0

    fm_bi->write(0x63, 0x0491);
    fm_bi->setbits(0x6B, 0x2000, 0xFFFF);

    fm_bi->write(FM_MAIN_PGSEL, page); //recover page
    return 0;
}

static fm_s32 mt6620_RDS_disable(void)
{
    WCN_DBG(FM_DBG | RDSC, "rds disable\n");
    fm_bi->setbits(0x6B, 0x0000, 0xDFFF);
    fm_bi->write(0x63, 0x0481);

    return 0;
}

static fm_u16 mt6620_RDS_Get_GoodBlock_Counter(void)
{
    fm_u16 tmp_reg;
    fm_u16 page;

    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003);

    fm_bi->read(FM_RDS_GOODBK_CNT, &tmp_reg);
    GOOD_BLK_CNT = tmp_reg;

    fm_bi->write(FM_MAIN_PGSEL, page);
    WCN_DBG(FM_DBG | RDSC, "get good block cnt:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_u16 mt6620_RDS_Get_BadBlock_Counter(void)
{
    fm_u16 tmp_reg;
    fm_u16 page;

    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003);

    fm_bi->read(FM_RDS_BADBK_CNT, &tmp_reg);
    BAD_BLK_CNT = tmp_reg;

    fm_bi->write(FM_MAIN_PGSEL, page);
    WCN_DBG(FM_DBG | RDSC, "get bad block cnt:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_u8 mt6620_RDS_Get_BadBlock_Ratio(void)
{
    fm_u16 tmp_reg;
    fm_u16 gbc;
    fm_u16 bbc;

    gbc = mt6620_RDS_Get_GoodBlock_Counter();
    bbc = mt6620_RDS_Get_BadBlock_Counter();

    if ((gbc + bbc) > 0) {
        tmp_reg = (fm_u8)(bbc * 100 / (gbc + bbc));
    } else {
        tmp_reg = 0;
    }

    BAD_BLK_RATIO = tmp_reg;
    WCN_DBG(FM_DBG | RDSC, "get badblock ratio:%d\n", (fm_s32)tmp_reg);

    return tmp_reg;
}

static fm_s32 mt6620_RDS_BlockCounter_Reset(void)
{
    fm_u16 page;

    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003);

    fm_bi->write(0xC8, 0x0001);
    fm_bi->write(0xC8, 0x0002);

    fm_bi->write(FM_MAIN_PGSEL, page);

    return 0;
}

static fm_u32 mt6620_RDS_Get_BlerCheck_Interval(void)
{
    return gBLER_CHK_INTERVAL;
}

static fm_s32 mt6620_RDS_Reset(void)
{
    fm_u16 page;

    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003);

    fm_bi->write(0xB0, 0x0001);

    fm_bi->write(FM_MAIN_PGSEL, page);

    return 0;
}

static fm_s32 mt6620_RDS_Reset_Block(void)
{
    fm_u16 page;

    fm_bi->read(FM_MAIN_PGSEL, &page);
    fm_bi->write(FM_MAIN_PGSEL, 0x0003);

    fm_bi->write(0xDD, 0x0001);

    fm_bi->write(FM_MAIN_PGSEL, page);

    return 0;
}

static fm_s32 mt6620_RDS_BlerCheck(rds_t *dst)
{
    fm_s32 ret = 0;
    fm_u16 TOTAL_CNT;
    static fm_u16 RDS_Sync_Cnt;
    static fm_u16 RDS_Block_Reset_Cnt;
#if 0
    if (dst->AF_Data.Addr_Cnt == 0xFF) {
        //AF List Finished
        dst->event_status |= RDS_EVENT_AF;  //Need notfiy application
        //loop dst->event_status then act
        if (dst->event_status != 0) {
            //fm->RDS_Data_ready = true;
            //wake_up_interruptible(&fm->read_wait);
            //FIXME
            WCN_DBG(FM_DBG | RDSC, "RDS_EVENT_AF, trigger read\n");
        }
    }
#endif
    gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T1;
    GOOD_BLK_CNT = mt6620_RDS_Get_GoodBlock_Counter();
    BAD_BLK_CNT = mt6620_RDS_Get_BadBlock_Counter();
    TOTAL_CNT = GOOD_BLK_CNT + BAD_BLK_CNT;

    mt6620_RDS_BlockCounter_Reset();

    if ((GOOD_BLK_CNT == 0) && (BAD_BLK_CNT == 0)) {
        BAD_BLK_RATIO = 0;
    } else {
        BAD_BLK_RATIO = (BAD_BLK_CNT * 100) / TOTAL_CNT;
    }

    if ((BAD_BLK_RATIO < MT6620_RDS_BLER_TH2) && (RDS_Sync_Cnt > MT6620_RDS_BLER_C1)) {
        gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T2;

        if (RDS_Block_Reset_Cnt > 1)
            RDS_Block_Reset_Cnt--;
    } else {
        if (BAD_BLK_RATIO > MT6620_RDS_BLER_TH1) {
            //>90%
            mt6620_RDS_BlockCounter_Reset();
            RDS_Sync_Cnt = 0;   //need clear or not, Question, LCH.
            RDS_Block_Reset_Cnt++;

            if ((RDS_Block_Reset_Cnt > MT6620_RDS_BLER_C2) || bRDS_FirstIn) {
                if (bRDS_FirstIn)
                    bRDS_FirstIn = false;

                if ((ret = mt6620_RDS_Reset()))
                    return ret;

                RDS_Block_Reset_Cnt = 0;
                WCN_DBG(FM_DBG | RDSC, "RDS Reset, blk_cnt:%d, RDS_FirstIn:%d\n", RDS_Block_Reset_Cnt, bRDS_FirstIn);
            } else if (TOTAL_CNT > 12) {
                //LCH question 2, why 12???
                WCN_DBG(FM_DBG | RDSC, "RDS Block Reset: %x\n", RDS_Block_Reset_Cnt);

                if ((ret = mt6620_RDS_Reset_Block()))
                    return ret;
            }
        } else {
            RDS_Sync_Cnt++; //(60%-90%)
            WCN_DBG(FM_DBG | RDSC, "RDS Sync Cnt: %d\n", RDS_Block_Reset_Cnt);

            if (RDS_Block_Reset_Cnt > 1)
                RDS_Block_Reset_Cnt--;

            if (RDS_Sync_Cnt > MT6620_RDS_BLER_C1) {
                gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T2;
            }
        }
    }

    return ret;
}

static void mt6620_RDS_Init_Data(rds_t *pstRDSData)
{
    fm_memset(pstRDSData, 0 , sizeof(rds_t));
    bRDS_FirstIn = fm_true;

    fm_memset(pstRDSData->RT_Data.TextData, 0x20, sizeof(pstRDSData->RT_Data.TextData));
    fm_memset(pstRDSData->PS_Data.PS, '\0', sizeof(pstRDSData->PS_Data.PS));
    fm_memset(pstRDSData->PS_ON, 0x20, sizeof(pstRDSData->PS_ON));
}

fm_bool mt6620_RDS_OnOff(rds_t *dst, fm_bool bFlag)
{
#if 0
    if (mt6620_RDS_support() == fm_false) {
        WCN_DBG(FM_ALT | RDSC, "mt6620_RDS_OnOff failed, RDS not support\n");
        return fm_false;
    }
#endif
    if (bFlag) {
        mt6620_RDS_Init_Data(dst);
        mt6620_RDS_enable();
    } else {
        mt6620_RDS_disable();
    }

    return fm_true;
}

DEFINE_RDSLOG(mt6620_rds_log);

/* mt6620_RDS_Efm_s32_Handler    -    response FM RDS interrupt
 * @fm - main data structure of FM driver
 * This function first get RDS raw data, then call RDS spec parser
 */
static fm_s32 mt6620_rds_parser(rds_t *rds_dst, struct rds_rx_t *rds_raw, fm_s32 rds_size, fm_u16(*getfreq)(void))
{
    mt6620_rds_log.log_in(&mt6620_rds_log, rds_raw, rds_size);
    return rds_parser(rds_dst, rds_raw, rds_size, getfreq);
}

static fm_s32 mt6620_rds_log_get(struct rds_rx_t *dst, fm_s32 *dst_len)
{
    return mt6620_rds_log.log_out(&mt6620_rds_log, dst, dst_len);
}

static fm_s32 mt6620_rds_gc_get(struct rds_group_cnt_t *dst, rds_t *rdsp)
{
    return rds_grp_counter_get(dst, &rdsp->gc);
}

static fm_s32 mt6620_rds_gc_reset(rds_t *rdsp)
{
    return rds_grp_counter_reset(&rdsp->gc);
}

fm_s32 MT6620fm_rds_ops_register(struct fm_lowlevel_ops *ops)
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

    ops->ri.rds_blercheck = mt6620_RDS_BlerCheck;
    ops->ri.rds_onoff = mt6620_RDS_OnOff;
    ops->ri.rds_parser = mt6620_rds_parser;
    ops->ri.rds_gbc_get = mt6620_RDS_Get_GoodBlock_Counter;
    ops->ri.rds_bbc_get = mt6620_RDS_Get_BadBlock_Counter;
    ops->ri.rds_bbr_get = mt6620_RDS_Get_BadBlock_Ratio;
    ops->ri.rds_bc_reset = mt6620_RDS_BlockCounter_Reset;
    ops->ri.rds_bci_get = mt6620_RDS_Get_BlerCheck_Interval;
    ops->ri.rds_log_get = mt6620_rds_log_get;
    ops->ri.rds_gc_get = mt6620_rds_gc_get;
    ops->ri.rds_gc_reset = mt6620_rds_gc_reset;
    return ret;
}

fm_s32 MT6620fm_rds_ops_unregister(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;

    FMR_ASSERT(ops);

    fm_bi = NULL;
    fm_memset(&ops->ri, 0, sizeof(struct fm_rds_interface));
    return ret;
}


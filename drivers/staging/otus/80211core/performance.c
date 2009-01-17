/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : performance.c                                         */
/*                                                                      */
/*  Abstract                                                            */
/*      This module performance evaluation functions.                   */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#ifdef ZM_ENABLE_PERFORMANCE_EVALUATION

#define ZM_TP_SIZE 50
struct zsSummary zm_summary;
struct zsVariation zm_var;
struct zsThroughput zm_tp;

void zfiPerformanceInit(zdev_t* dev)
{
    u16_t   i;

    zmw_get_wlan_dev(dev);

    zm_summary.tick_base = wd->tick;
    zm_summary.tx_msdu_count = 0;
    zm_summary.tx_mpdu_count = 0;
    zm_summary.rx_msdu_count = 0;
    zm_summary.rx_mpdu_count = 0;
    zm_summary.rx_broken_seq = 0;
    zm_summary.rx_broken_sum = 0;
    zm_summary.rx_seq_base = 0;
    zm_summary.rx_broken_seq_dis = 0;
    zm_summary.rx_duplicate_seq = 0;
    zm_summary.rx_old_seq = 0;
    zm_summary.reset_count = 0;
    zm_summary.reset_sum = 0;
    zm_summary.rx_lost_sum = 0;
    zm_summary.rx_duplicate_error = 0;
    zm_summary.rx_free = 0;
    zm_summary.rx_amsdu_len = 0;
    zm_summary.rx_flush = 0;
    zm_summary.rx_clear = 0;
    zm_summary.rx_reorder = 0;

    for (i=0; i<100; i++)
    {
        zm_var.tx_msdu_tick[i] = zm_var.tx_mpdu_tick[i] = 0;
        zm_var.rx_msdu_tick[i] = zm_var.rx_mpdu_tick[i] = 0;
    }

    zfTimerSchedule(dev, ZM_EVENT_TIMEOUT_PERFORMANCE, 100);

    zm_tp.size = ZM_TP_SIZE;
    zm_tp.head = zm_tp.size - 1;
    zm_tp.tail = 0;
    for (i=0; i<zm_tp.size; i++)
    {
        zm_tp.tx[i]=0;
        zm_tp.rx[i]=0;
    }
}

void zfiPerformanceGraph(zdev_t* dev)
{
    s16_t   i,j;
    u8_t    s[ZM_TP_SIZE+5];
    zmw_get_wlan_dev(dev);

    for (i=0; i<(zm_tp.size-1); i++)
    {
        zm_tp.tx[i] = zm_tp.tx[i+1];
        zm_tp.rx[i] = zm_tp.rx[i+1];
    }
    zm_tp.tx[zm_tp.size-1] = zm_summary.tx_mpdu_count*1500*8/1000000;
    zm_tp.rx[zm_tp.size-1] = zm_summary.rx_msdu_count*1500*8/1000000;

    for (i=15; i>0; i--)
    {
        s[0] = (i/10) + '0';
        s[1] = (i%10) + '0';
        s[2] = '0';
        s[3] = '|';
        for (j=0; j<zm_tp.size; j++)
        {
            if ((zm_tp.tx[j]/10 == i) && (zm_tp.rx[j]/10 == i))
            {
                s[4+j] = 'X';
            }
            else if (zm_tp.tx[j]/10 == i)
            {
                s[4+j] = 'T';
            }
            else if (zm_tp.rx[j]/10 == i)
            {
                s[4+j] = 'R';
            }
            else
            {
                s[4+j] = ' ';
            }
        }
        s[zm_tp.size+4] = '\0';
        DbgPrint("%s",s);
    }
    DbgPrint("000|__________________________________________________");

}


void zfiPerformanceRefresh(zdev_t* dev)
{
    u16_t   i;

    zmw_get_wlan_dev(dev);

    zfiDbgReadReg(dev, 0x11772c);

    zm_var.tx_msdu_mean = zm_summary.tx_msdu_count / 100;
    zm_var.tx_mpdu_mean = zm_summary.tx_mpdu_count / 100;
    zm_var.rx_msdu_mean = zm_summary.rx_msdu_count / 100;
    zm_var.rx_mpdu_mean = zm_summary.rx_mpdu_count / 100;

    zm_var.tx_msdu_sum = zm_var.tx_mpdu_sum = 0;
    zm_var.rx_msdu_sum = zm_var.rx_mpdu_sum = 0;
    zm_summary.tx_idle_count = zm_summary.rx_idle_count = 0;
    for (i=0; i<100; i++)
    {
        zm_var.tx_msdu_sum += (zm_var.tx_msdu_tick[i] * zm_var.tx_msdu_tick[i]);
        zm_var.tx_mpdu_sum += (zm_var.tx_mpdu_tick[i] * zm_var.tx_mpdu_tick[i]);
        zm_var.rx_msdu_sum += (zm_var.rx_msdu_tick[i] * zm_var.rx_msdu_tick[i]);
        zm_var.rx_mpdu_sum += (zm_var.rx_mpdu_tick[i] * zm_var.rx_mpdu_tick[i]);

        if (!zm_var.tx_mpdu_tick[i]) zm_summary.tx_idle_count++;
        if (!zm_var.rx_mpdu_tick[i]) zm_summary.rx_idle_count++;
    }
    zm_var.tx_msdu_var = (zm_var.tx_msdu_sum / 100) - (zm_var.tx_msdu_mean * zm_var.tx_msdu_mean);
    zm_var.tx_mpdu_var = (zm_var.tx_mpdu_sum / 100) - (zm_var.tx_mpdu_mean * zm_var.tx_mpdu_mean);
    zm_var.rx_msdu_var = (zm_var.rx_msdu_sum / 100) - (zm_var.rx_msdu_mean * zm_var.rx_msdu_mean);
    zm_var.rx_mpdu_var = (zm_var.rx_mpdu_sum / 100) - (zm_var.rx_mpdu_mean * zm_var.rx_mpdu_mean);

    zm_summary.tick_base = wd->tick;
    zm_summary.rx_broken_sum += zm_summary.rx_broken_seq;
    zm_summary.rx_lost_sum += (zm_summary.rx_broken_seq - zm_summary.rx_duplicate_seq - zm_summary.rx_old_seq);

    zfiPerformanceGraph(dev);

    DbgPrint("******************************************************\n");
    DbgPrint("* TX: MSDU=%5d, VAR=%5d; MPDU=%5d, VAR=%5d\n", zm_summary.tx_msdu_count,
        zm_var.tx_msdu_var, zm_summary.tx_mpdu_count, zm_var.tx_mpdu_var);
    DbgPrint("* TX: idle=%5d,TxRate=%3d,  PER=%5d\n", zm_summary.tx_idle_count,
        wd->CurrentTxRateKbps/1000,
        (u16_t)wd->PER[wd->sta.oppositeInfo[0].rcCell.currentRate]);
    DbgPrint("* RX: MSDU=%5d, VAR=%5d; MPDU=%5d, VAR=%5d\n", zm_summary.rx_msdu_count,
        zm_var.rx_msdu_var, zm_summary.rx_mpdu_count, zm_var.rx_mpdu_var);
    DbgPrint("* RX: idle=%5d,RxRate=%3d,AMSDU=%5d\n", zm_summary.rx_idle_count,
        wd->CurrentRxRateKbps/1000, zm_summary.rx_amsdu_len);
    DbgPrint("* RX broken seq=%4d, distances=%4d, duplicates=%4d\n", zm_summary.rx_broken_seq,
        zm_summary.rx_broken_seq_dis, zm_summary.rx_duplicate_seq);
    DbgPrint("* RX    old seq=%4d,      lost=%4d, broken sum=%4d\n", zm_summary.rx_old_seq,
        (zm_summary.rx_broken_seq - zm_summary.rx_duplicate_seq - zm_summary.rx_old_seq),
        zm_summary.rx_broken_sum);
    DbgPrint("* Rx   lost sum=%4d,dup. error=%4d, free count=%4d\n", zm_summary.rx_lost_sum,
        zm_summary.rx_duplicate_error, zm_summary.rx_free);
    DbgPrint("* Rx  flush sum=%4d, clear sum=%4d, reorder=%7d\n", zm_summary.rx_flush,
        zm_summary.rx_clear, zm_summary.rx_reorder);
    DbgPrint("* Firmware reset=%3d, reset sum=%4d\n", zm_summary.reset_count,
        zm_summary.reset_sum);
    DbgPrint("******************************************************\n\n");
    //reset count 11772c
    zm_summary.tx_msdu_count = 0;
    zm_summary.tx_mpdu_count = 0;
    zm_summary.rx_msdu_count = 0;
    zm_summary.rx_mpdu_count = 0;
    zm_summary.rx_broken_seq = 0;
    zm_summary.rx_broken_seq_dis = 0;
    zm_summary.rx_duplicate_seq = 0;
    zm_summary.rx_old_seq = 0;
    zm_summary.reset_count = 0;
    zm_summary.rx_amsdu_len = 0;

    for (i=0; i<100; i++)
    {
        zm_var.tx_msdu_tick[i] = zm_var.tx_mpdu_tick[i] = 0;
        zm_var.rx_msdu_tick[i] = zm_var.rx_mpdu_tick[i] = 0;
    }

    zfTimerSchedule(dev, ZM_EVENT_TIMEOUT_PERFORMANCE, 100);
}

void zfiTxPerformanceMSDU(zdev_t* dev, u32_t tick)
{
    u32_t   index;
    zm_summary.tx_msdu_count++;

    index = tick - zm_summary.tick_base;

    if (index < 100)
    {
        zm_var.tx_msdu_tick[index]++;
    }
    else
    {
        //DbgPrint("wd->tick exceeded tick_base+100!\n");
    }
}

void zfiRxPerformanceMSDU(zdev_t* dev, u32_t tick)
{
    u32_t   index;
    zm_summary.rx_msdu_count++;

    index = tick - zm_summary.tick_base;

    if (index < 100)
    {
        zm_var.rx_msdu_tick[index]++;
    }
    else
    {
        //DbgPrint("wd->tick exceeded tick_base+100!\n");
    }
}

void zfiTxPerformanceMPDU(zdev_t* dev, u32_t tick)
{
    u32_t   index;
    zm_summary.tx_mpdu_count++;

    index = tick - zm_summary.tick_base;

    if (index < 100)
    {
        zm_var.tx_mpdu_tick[index]++;
    }
    else
    {
        //DbgPrint("wd->tick exceeded tick_base+100!\n");
    }
}

#ifndef ZM_INT_USE_EP2_HEADER_SIZE
#define ZM_INT_USE_EP2_HEADER_SIZE   12
#endif
void zfiRxPerformanceMPDU(zdev_t* dev, zbuf_t* buf)
{
    u32_t   index;
    u16_t   frameType;
    u16_t   frameCtrl;
    u8_t    mpduInd;
    u16_t   plcpHdrLen;
    u16_t   len;

    zmw_get_wlan_dev(dev);

    len = zfwBufGetSize(dev, buf);
    mpduInd = zmw_rx_buf_readb(dev, buf, len-1);
    /* First MPDU or Single MPDU */
    if(((mpduInd & 0x30) == 0x00) || ((mpduInd & 0x30) == 0x20))
    //if ((mpduInd & 0x10) == 0x00)
    {
        plcpHdrLen = 12;        // PLCP header length
    }
    else
    {
        if (zmw_rx_buf_readh(dev, buf, 4) == wd->macAddr[0] &&
            zmw_rx_buf_readh(dev, buf, 6) == wd->macAddr[1] &&
            zmw_rx_buf_readh(dev, buf, 8) == wd->macAddr[2]) {
            plcpHdrLen = 0;
        }
        else if (zmw_rx_buf_readh(dev, buf, 16) == wd->macAddr[0] &&
                 zmw_rx_buf_readh(dev, buf, 18) == wd->macAddr[1] &&
                 zmw_rx_buf_readh(dev, buf, 20) == wd->macAddr[2]){
            plcpHdrLen = 12;
        }
        else {
            plcpHdrLen = 0;
        }
    }

    frameCtrl = zmw_rx_buf_readb(dev, buf, plcpHdrLen + 0);
    frameType = frameCtrl & 0xf;

    if (frameType != ZM_WLAN_DATA_FRAME)
    {
        return;
    }

    zm_summary.rx_mpdu_count++;

    index = wd->tick - zm_summary.tick_base;

    if (index < 100)
    {
        zm_var.rx_mpdu_tick[index]++;
    }
    else
    {
        //DbgPrint("wd->tick exceeded tick_base+100!\n");
    }
}

void zfiRxPerformanceSeq(zdev_t* dev, zbuf_t* buf)
{
    u16_t   seq_no;
    u16_t   offset = 0;
    u16_t   old_dis = zm_summary.rx_broken_seq_dis;
    //sys_time = KeQueryPerformanceCounter(&freq);

    seq_no = zmw_rx_buf_readh(dev, buf, offset+22) >> 4;

    ZM_SEQ_DEBUG("Out   %5d\n", seq_no);

    if (seq_no < zm_summary.rx_seq_base)
    {
        if (seq_no == 0)
        {
            if (zm_summary.rx_seq_base != 4095)
            {
                zm_summary.rx_broken_seq++;
                ZM_SEQ_DEBUG("Broken seq");
                zm_summary.rx_broken_seq_dis+=(4096 - zm_summary.rx_seq_base);
            }
        }
        else if ((seq_no < 300) && (zm_summary.rx_seq_base > 3800))
        {
            zm_summary.rx_broken_seq++;
            ZM_SEQ_DEBUG("Broken seq");
            zm_summary.rx_broken_seq_dis+=(4096 - zm_summary.rx_seq_base + seq_no);
        }
        else
        {
            zm_summary.rx_broken_seq++;
            ZM_SEQ_DEBUG("Broken seq");
            zm_summary.rx_broken_seq_dis+=(zm_summary.rx_seq_base - seq_no);
            zm_summary.rx_old_seq++;
        }
    }
    else
    {
        if (seq_no != (zm_summary.rx_seq_base + 1))
        {
            if ((seq_no > 3800) && (zm_summary.rx_seq_base < 300))
            {
                zm_summary.rx_broken_seq++;
                ZM_SEQ_DEBUG("Broken seq");
                zm_summary.rx_broken_seq_dis+=(4096 - seq_no + zm_summary.rx_seq_base);
                zm_summary.rx_old_seq++;
            }
            else
            {
                zm_summary.rx_broken_seq++;
                ZM_SEQ_DEBUG("Broken seq");
                zm_summary.rx_broken_seq_dis+=(seq_no - zm_summary.rx_seq_base);
            }
        }
    }
    if (seq_no == zm_summary.rx_seq_base)
    {
        zm_summary.rx_duplicate_seq++;
    }

    if ((zm_summary.rx_broken_seq_dis - old_dis) > 100)
    {
        DbgPrint("* seq_no=%4d, base_seq=%4d, dis_diff=%4d", seq_no,
            zm_summary.rx_seq_base, zm_summary.rx_broken_seq_dis - old_dis);
    }
    zm_summary.rx_seq_base = seq_no;
}

void zfiRxPerformanceReg(zdev_t* dev, u32_t reg, u32_t rsp)
{
    zm_summary.reset_count = (u16_t)rsp - zm_summary.reset_sum;
    zm_summary.reset_sum = (u16_t)rsp;
}

void zfiRxPerformanceDup(zdev_t* dev, zbuf_t* buf1, zbuf_t* buf2)
{
    u16_t   seq_no1, seq_no2;

    seq_no1 = zmw_rx_buf_readh(dev, buf1, 22) >> 4;
    seq_no2 = zmw_rx_buf_readh(dev, buf2, 22) >> 4;
    if (seq_no1 != seq_no2)
    {
        zm_summary.rx_duplicate_error++;
    }
}

void zfiRxPerformanceFree(zdev_t* dev, zbuf_t* buf)
{
    zm_summary.rx_free++;
}

void zfiRxPerformanceAMSDU(zdev_t* dev, zbuf_t* buf, u16_t len)
{
    if (zm_summary.rx_amsdu_len < len)
    {
        zm_summary.rx_amsdu_len = len;
    }
}
void zfiRxPerformanceFlush(zdev_t* dev)
{
    zm_summary.rx_flush++;
}

void zfiRxPerformanceClear(zdev_t* dev)
{
    zm_summary.rx_clear++;
    ZM_SEQ_DEBUG("RxClear");
}

void zfiRxPerformanceReorder(zdev_t* dev)
{
    zm_summary.rx_reorder++;
}
#endif /* end of ZM_ENABLE_PERFORMANCE_EVALUATION */

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
/*  Module Name : wrap_dbg.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for debug functions       */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
#endif

void zfwDumpBuf(zdev_t* dev, zbuf_t* buf)
{
    u16_t i;

    for (i=0; i<buf->len; i++)
    {
        printk("%02x ", *(((u8_t*)buf->data)+i));
        if ((i&0xf)==0xf)
        {
            printk("\n");
        }
    }
    printk("\n");
}


void zfwDbgReadRegDone(zdev_t* dev, u32_t addr, u32_t val)
{
    printk("Read addr:%x = %x\n", addr, val);
}

void zfwDbgWriteRegDone(zdev_t* dev, u32_t addr, u32_t val)
{
    printk("Write addr:%x = %x\n", addr, val);
}

void zfwDbgReadTallyDone(zdev_t* dev)
{
    //printk("Read Tall Done\n");
}

void zfwDbgWriteEepromDone(zdev_t* dev, u32_t addr, u32_t val)
{
}

void zfwDbgQueryHwTxBusyDone(zdev_t* dev, u32_t val)
{
}

//For Evl ++
void zfwDbgReadFlashDone(zdev_t* dev, u32_t addr, u32_t* rspdata, u32_t datalen)
{
    printk("Read Flash addr:%x length:%x\n", addr, datalen);
}

void zfwDbgProgrameFlashDone(zdev_t* dev)
{
    printk("Program Flash Done\n");
}

void zfwDbgProgrameFlashChkDone(zdev_t* dev)
{
    printk("Program Flash Done\n");
}

void zfwDbgGetFlashChkSumDone(zdev_t* dev, u32_t* rspdata)
{
    printk("Get Flash ChkSum Done\n");
}

void zfwDbgDownloadFwInitDone(zdev_t* dev)
{
    printk("Download FW Init Done\n");
}
//For Evl --

/* Leave an empty line below to remove warning message on some compiler */

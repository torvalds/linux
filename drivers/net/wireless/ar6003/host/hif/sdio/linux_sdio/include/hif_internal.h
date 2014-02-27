//------------------------------------------------------------------------------
// <copyright file="hif_internal.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// internal header file for hif layer
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HIF_INTERNAL_H_
#define _HIF_INTERNAL_H_

#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#include "hif.h"
#include "../../../common/hif_sdio_common.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)   
#include <linux/scatterlist.h>
//#define HIF_LINUX_MMC_SCATTER_SUPPORT
#endif

#define BUS_REQUEST_MAX_NUM                64

#define SDIO_CLOCK_FREQUENCY_DEFAULT       25000000
#define SDWLAN_ENABLE_DISABLE_TIMEOUT      20
#define FLAGS_CARD_ENAB                    0x02
#define FLAGS_CARD_IRQ_UNMSK               0x04

#define HIF_MBOX_BLOCK_SIZE                HIF_DEFAULT_IO_BLOCK_SIZE
#define HIF_MBOX0_BLOCK_SIZE               1
#define HIF_MBOX1_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE
#define HIF_MBOX2_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE
#define HIF_MBOX3_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE

struct _HIF_SCATTER_REQ_PRIV;

typedef struct bus_request {
    struct bus_request *next;       /* link list of available requests */
    struct bus_request *inusenext;  /* link list of in use requests */
    struct semaphore sem_req;
    A_UINT32 address;               /* request data */
    A_UCHAR *buffer;
    A_UINT32 length;
    A_UINT32 request;
    void *context;
    A_STATUS status;
    struct _HIF_SCATTER_REQ_PRIV *pScatterReq;      /* this request is a scatter request */
} BUS_REQUEST;

struct hif_device {
    struct sdio_func *func;
    spinlock_t asynclock;
    struct task_struct* async_task;             /* task to handle async commands */
    struct semaphore sem_async;                 /* wake up for async task */
    int    async_shutdown;                      /* stop the async task */
    struct completion async_completion;          /* thread completion */
    BUS_REQUEST   *asyncreq;                    /* request for async tasklet */
    BUS_REQUEST *taskreq;                       /*  async tasklet data */
    spinlock_t lock;
    BUS_REQUEST *s_busRequestFreeQueue;         /* free list */
    BUS_REQUEST busRequest[BUS_REQUEST_MAX_NUM]; /* available bus requests */
    void     *claimedContext;
    HTC_CALLBACKS htcCallbacks;
    A_UINT8     *dma_buffer;
    DL_LIST      ScatterReqHead;                /* scatter request list head */
    A_BOOL       scatter_enabled;               /* scatter enabled flag */
    A_BOOL   is_suspend;
    A_BOOL   is_disabled;
    atomic_t   irqHandling;
    HIF_DEVICE_POWER_CHANGE_TYPE powerConfig;
    const struct sdio_device_id *id;
    struct mmc_host *host;
};

#define HIF_DMA_BUFFER_SIZE (32 * 1024)
#define CMD53_FIXED_ADDRESS 1
#define CMD53_INCR_ADDRESS  2

BUS_REQUEST *hifAllocateBusRequest(HIF_DEVICE *device);
void hifFreeBusRequest(HIF_DEVICE *device, BUS_REQUEST *busrequest);
void AddToAsyncList(HIF_DEVICE *device, BUS_REQUEST *busrequest);

#ifdef HIF_LINUX_MMC_SCATTER_SUPPORT

#define MAX_SCATTER_REQUESTS             4
#define MAX_SCATTER_ENTRIES_PER_REQ      16
#define MAX_SCATTER_REQ_TRANSFER_SIZE    32*1024

typedef struct _HIF_SCATTER_REQ_PRIV {
    HIF_SCATTER_REQ     *pHifScatterReq;  /* HIF scatter request with allocated entries */   
    HIF_DEVICE          *device;          /* this device */
    BUS_REQUEST         *busrequest;      /* request associated with request */
        /* scatter list for linux */    
    struct scatterlist  sgentries[MAX_SCATTER_ENTRIES_PER_REQ];   
} HIF_SCATTER_REQ_PRIV;

#define ATH_DEBUG_SCATTER  ATH_DEBUG_MAKE_MODULE_MASK(0)

A_STATUS SetupHIFScatterSupport(HIF_DEVICE *device, HIF_DEVICE_SCATTER_SUPPORT_INFO *pInfo);
void CleanupHIFScatterResources(HIF_DEVICE *device);
A_STATUS DoHifReadWriteScatter(HIF_DEVICE *device, BUS_REQUEST *busrequest);

#else  // HIF_LINUX_MMC_SCATTER_SUPPORT

static inline A_STATUS SetupHIFScatterSupport(HIF_DEVICE *device, HIF_DEVICE_SCATTER_SUPPORT_INFO *pInfo) 
{
    return A_ENOTSUP;
}

static inline A_STATUS DoHifReadWriteScatter(HIF_DEVICE *device, BUS_REQUEST *busrequest) 
{
    return A_ENOTSUP;
}

#define CleanupHIFScatterResources(d) { }

#endif // HIF_LINUX_MMC_SCATTER_SUPPORT

#endif // _HIF_INTERNAL_H_


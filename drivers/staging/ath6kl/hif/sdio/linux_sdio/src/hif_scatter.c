//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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
// HIF scatter implementation
//
// Author(s): ="Atheros"
//==============================================================================

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/kthread.h>
#include "hif_internal.h"
#define ATH_MODULE_NAME hif
#include "a_debug.h"

#ifdef HIF_LINUX_MMC_SCATTER_SUPPORT

#define _CMD53_ARG_READ          0
#define _CMD53_ARG_WRITE         1
#define _CMD53_ARG_BLOCK_BASIS   1 
#define _CMD53_ARG_FIXED_ADDRESS 0
#define _CMD53_ARG_INCR_ADDRESS  1

#define SDIO_SET_CMD53_ARG(arg,rw,func,mode,opcode,address,bytes_blocks) \
    (arg) = (((rw) & 1) << 31)                  | \
            (((func) & 0x7) << 28)              | \
            (((mode) & 1) << 27)                | \
            (((opcode) & 1) << 26)              | \
            (((address) & 0x1FFFF) << 9)        | \
            ((bytes_blocks) & 0x1FF)
            
static void FreeScatterReq(struct hif_device *device, struct hif_scatter_req *pReq)
{   
    unsigned long flag;

    spin_lock_irqsave(&device->lock, flag);

    DL_ListInsertTail(&device->ScatterReqHead, &pReq->ListLink);
    
    spin_unlock_irqrestore(&device->lock, flag);
        
}

static struct hif_scatter_req *AllocScatterReq(struct hif_device *device) 
{
    struct dl_list       *pItem; 
    unsigned long flag;

    spin_lock_irqsave(&device->lock, flag);
    
    pItem = DL_ListRemoveItemFromHead(&device->ScatterReqHead);
    
    spin_unlock_irqrestore(&device->lock, flag);
    
    if (pItem != NULL) {
        return A_CONTAINING_STRUCT(pItem, struct hif_scatter_req, ListLink);
    }
    
    return NULL;   
}

    /* called by async task to perform the operation synchronously using direct MMC APIs  */
int DoHifReadWriteScatter(struct hif_device *device, BUS_REQUEST *busrequest)
{
    int                     i;
    u8 rw;
    u8 opcode;
    struct mmc_request      mmcreq;
    struct mmc_command      cmd;
    struct mmc_data         data;
    struct hif_scatter_req_priv   *pReqPriv;   
    struct hif_scatter_req        *pReq;       
    int                status = 0;
    struct                  scatterlist *pSg;
    
    pReqPriv = busrequest->pScatterReq;
    
    A_ASSERT(pReqPriv != NULL);
    
    pReq = pReqPriv->pHifScatterReq;
    
    memset(&mmcreq, 0, sizeof(struct mmc_request));
    memset(&cmd, 0, sizeof(struct mmc_command));
    memset(&data, 0, sizeof(struct mmc_data));
       
    data.blksz = HIF_MBOX_BLOCK_SIZE;
    data.blocks = pReq->TotalLength / HIF_MBOX_BLOCK_SIZE;
                        
    AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER: (%s) Address: 0x%X, (BlockLen: %d, BlockCount: %d) , (tot:%d,sg:%d)\n",
              (pReq->Request & HIF_WRITE) ? "WRITE":"READ", pReq->Address, data.blksz, data.blocks,
              pReq->TotalLength,pReq->ValidScatterEntries));
         
    if (pReq->Request  & HIF_WRITE) {
        rw = _CMD53_ARG_WRITE;
        data.flags = MMC_DATA_WRITE;
    } else {
        rw = _CMD53_ARG_READ;
        data.flags = MMC_DATA_READ;
    }

    if (pReq->Request & HIF_FIXED_ADDRESS) {
        opcode = _CMD53_ARG_FIXED_ADDRESS;
    } else {
        opcode = _CMD53_ARG_INCR_ADDRESS;
    }
    
        /* fill SG entries */
    pSg = pReqPriv->sgentries;   
    sg_init_table(pSg, pReq->ValidScatterEntries); 
          
        /* assemble SG list */   
    for (i = 0 ; i < pReq->ValidScatterEntries ; i++, pSg++) {
            /* setup each sg entry */
        if ((unsigned long)pReq->ScatterList[i].pBuffer & 0x3) {
                /* note some scatter engines can handle unaligned buffers, print this
                 * as informational only */
            AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER,
                            ("HIF: (%s) Scatter Buffer is unaligned 0x%lx\n",
                            pReq->Request & HIF_WRITE ? "WRITE":"READ",
                            (unsigned long)pReq->ScatterList[i].pBuffer)); 
        }
        
        AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("  %d:  Addr:0x%lX, Len:%d \n",
            i,(unsigned long)pReq->ScatterList[i].pBuffer,pReq->ScatterList[i].Length));
            
        sg_set_buf(pSg, pReq->ScatterList[i].pBuffer, pReq->ScatterList[i].Length);
    }
        /* set scatter-gather table for request */
    data.sg = pReqPriv->sgentries;
    data.sg_len = pReq->ValidScatterEntries;
        /* set command argument */    
    SDIO_SET_CMD53_ARG(cmd.arg, 
                       rw, 
                       device->func->num, 
                       _CMD53_ARG_BLOCK_BASIS, 
                       opcode,  
                       pReq->Address,
                       data.blocks);  
                       
    cmd.opcode = SD_IO_RW_EXTENDED;
    cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;
    
    mmcreq.cmd = &cmd;
    mmcreq.data = &data;
    
    mmc_set_data_timeout(&data, device->func->card);    
        /* synchronous call to process request */
    mmc_wait_for_req(device->func->card->host, &mmcreq);
 
    if (cmd.error) {
        status = A_ERROR;   
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR, ("HIF-SCATTER: cmd error: %d \n",cmd.error));
    }
               
    if (data.error) {
        status = A_ERROR;
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR, ("HIF-SCATTER: data error: %d \n",data.error));   
    }

    if (status) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR, ("HIF-SCATTER: FAILED!!! (%s) Address: 0x%X, Block mode (BlockLen: %d, BlockCount: %d)\n",
              (pReq->Request & HIF_WRITE) ? "WRITE":"READ",pReq->Address, data.blksz, data.blocks));        
    }
    
        /* set completion status, fail or success */
    pReq->CompletionStatus = status;
    
    if (pReq->Request & HIF_ASYNCHRONOUS) {
        AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER: async_task completion routine req: 0x%lX (%d)\n",(unsigned long)busrequest, status));
            /* complete the request */
        A_ASSERT(pReq->CompletionRoutine != NULL);
        pReq->CompletionRoutine(pReq);
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER async_task upping busrequest : 0x%lX (%d)\n", (unsigned long)busrequest,status));
            /* signal wait */
        up(&busrequest->sem_req);
    }
                                                               
    return status;   
}

    /* callback to issue a read-write scatter request */
static int HifReadWriteScatter(struct hif_device *device, struct hif_scatter_req *pReq)
{
    int             status = A_EINVAL;
    u32 request = pReq->Request;
    struct hif_scatter_req_priv *pReqPriv = (struct hif_scatter_req_priv *)pReq->HIFPrivate[0];
    
    do {
        
        A_ASSERT(pReqPriv != NULL);
        
        AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER: total len: %d Scatter Entries: %d\n", 
                            pReq->TotalLength, pReq->ValidScatterEntries));
        
        if (!(request & HIF_EXTENDED_IO)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("HIF-SCATTER: Invalid command type: 0x%08x\n", request));
            break;
        }
        
        if (!(request & (HIF_SYNCHRONOUS | HIF_ASYNCHRONOUS))) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("HIF-SCATTER: Invalid execution mode: 0x%08x\n", request));
            break;
        }
        
        if (!(request & HIF_BLOCK_BASIS)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("HIF-SCATTER: Invalid data mode: 0x%08x\n", request));
            break;   
        }
        
        if (pReq->TotalLength > MAX_SCATTER_REQ_TRANSFER_SIZE) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("HIF-SCATTER: Invalid length: %d \n", pReq->TotalLength));
            break;          
        }
        
        if (pReq->TotalLength == 0) {
            A_ASSERT(false);
            break;    
        }
        
            /* add bus request to the async list for the async I/O thread to process */
        AddToAsyncList(device, pReqPriv->busrequest);

        if (request & HIF_SYNCHRONOUS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER: queued sync req: 0x%lX\n", (unsigned long)pReqPriv->busrequest));
            /* signal thread and wait */
            up(&device->sem_async);
            if (down_interruptible(&pReqPriv->busrequest->sem_req) != 0) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,("HIF-SCATTER: interrupted! \n"));
                /* interrupted, exit */
                status = A_ERROR;
                break;
            } else {
                status = pReq->CompletionStatus;
            }
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_SCATTER, ("HIF-SCATTER: queued async req: 0x%lX\n", (unsigned long)pReqPriv->busrequest));
                /* wake thread, it will process and then take care of the async callback */
            up(&device->sem_async);
            status = 0;
        }           
       
    } while (false);

    if (status && (request & HIF_ASYNCHRONOUS)) {
        pReq->CompletionStatus = status;
        pReq->CompletionRoutine(pReq);
        status = 0;
    }
        
    return status;  
}

    /* setup of HIF scatter resources */
int SetupHIFScatterSupport(struct hif_device *device, struct hif_device_scatter_support_info *pInfo)
{
    int              status = A_ERROR;
    int                   i;
    struct hif_scatter_req_priv *pReqPriv;
    BUS_REQUEST          *busrequest;
        
    do {
        
            /* check if host supports scatter requests and it meets our requirements */
        if (device->func->card->host->max_segs < MAX_SCATTER_ENTRIES_PER_REQ) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("HIF-SCATTER : host only supports scatter of : %d entries, need: %d \n",
                    device->func->card->host->max_segs, MAX_SCATTER_ENTRIES_PER_REQ));
            status = A_ENOTSUP;
            break;    
        }
                    
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("HIF-SCATTER Enabled: max scatter req : %d entries: %d \n",
                MAX_SCATTER_REQUESTS, MAX_SCATTER_ENTRIES_PER_REQ)); 
        
        for (i = 0; i < MAX_SCATTER_REQUESTS; i++) {    
                /* allocate the private request blob */
            pReqPriv = (struct hif_scatter_req_priv *)A_MALLOC(sizeof(struct hif_scatter_req_priv));
            if (NULL == pReqPriv) {
                break;    
            }
            A_MEMZERO(pReqPriv, sizeof(struct hif_scatter_req_priv));
                /* save the device instance*/
            pReqPriv->device = device;      
                /* allocate the scatter request */
            pReqPriv->pHifScatterReq = (struct hif_scatter_req *)A_MALLOC(sizeof(struct hif_scatter_req) + 
                                         (MAX_SCATTER_ENTRIES_PER_REQ - 1) * (sizeof(struct hif_scatter_item))); 
           
            if (NULL == pReqPriv->pHifScatterReq) {
                A_FREE(pReqPriv);
                break;      
            }           
                /* just zero the main part of the scatter request */
            A_MEMZERO(pReqPriv->pHifScatterReq, sizeof(struct hif_scatter_req));
                /* back pointer to the private struct */
            pReqPriv->pHifScatterReq->HIFPrivate[0] = pReqPriv;
                /* allocate a bus request for this scatter request */
            busrequest = hifAllocateBusRequest(device);
            if (NULL == busrequest) {
                A_FREE(pReqPriv->pHifScatterReq);
                A_FREE(pReqPriv);
                break;    
            }
                /* assign the scatter request to this bus request */
            busrequest->pScatterReq = pReqPriv;
                /* point back to the request */
            pReqPriv->busrequest = busrequest;                           
                /* add it to the scatter pool */
            FreeScatterReq(device,pReqPriv->pHifScatterReq);
        }
        
        if (i != MAX_SCATTER_REQUESTS) {
            status = A_NO_MEMORY;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("HIF-SCATTER : failed to alloc scatter resources !\n"));
            break;    
        }
        
            /* set scatter function pointers */
        pInfo->pAllocateReqFunc = AllocScatterReq;
        pInfo->pFreeReqFunc = FreeScatterReq;
        pInfo->pReadWriteScatterFunc = HifReadWriteScatter;   
        pInfo->MaxScatterEntries = MAX_SCATTER_ENTRIES_PER_REQ;
        pInfo->MaxTransferSizePerScatterReq = MAX_SCATTER_REQ_TRANSFER_SIZE;
     
        status = 0;
        
    } while (false);
    
    if (status) {
        CleanupHIFScatterResources(device);   
    }
    
    return status;
}

    /* clean up scatter support */
void CleanupHIFScatterResources(struct hif_device *device)
{
    struct hif_scatter_req_priv    *pReqPriv;
    struct hif_scatter_req         *pReq;
    
        /* empty the free list */
        
    while (1) {
        
        pReq = AllocScatterReq(device);
                
        if (NULL == pReq) {
            break;    
        }   
        
        pReqPriv = (struct hif_scatter_req_priv *)pReq->HIFPrivate[0];
        A_ASSERT(pReqPriv != NULL);
        
        if (pReqPriv->busrequest != NULL) {
            pReqPriv->busrequest->pScatterReq = NULL;
                /* free bus request */
            hifFreeBusRequest(device, pReqPriv->busrequest);
            pReqPriv->busrequest = NULL;
        }
        
        if (pReqPriv->pHifScatterReq != NULL) {
            A_FREE(pReqPriv->pHifScatterReq);   
            pReqPriv->pHifScatterReq = NULL; 
        }
                
        A_FREE(pReqPriv);       
    }
}

#endif // HIF_LINUX_MMC_SCATTER_SUPPORT

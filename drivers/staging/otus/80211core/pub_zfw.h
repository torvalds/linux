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

#ifndef _PUB_ZFW_H
#define _PUB_ZFW_H

#include "../oal_dt.h"


/* Buffer management */
#ifdef ZM_ENABLE_BUFFER_DEBUG
extern zbuf_t* zfwBufAllocateWithContext(zdev_t* dev, u16_t len, u8_t *functionName, ULONG line);
#define zfwBufAllocate(dev, len)  zfwBufAllocateWithContext(dev, len, (u8_t *)__func__, __LINE__)
#else
extern zbuf_t* zfwBufAllocate(zdev_t* dev, u16_t len);
#endif
extern void zfwBufFree(zdev_t* dev, zbuf_t* buf, u16_t errCode);
extern u16_t zfwBufChain(zdev_t* dev, zbuf_t** head, zbuf_t* tail);
extern u16_t zfwBufCopy(zdev_t* dev, zbuf_t* dst, zbuf_t* src);
extern u16_t zfwBufSetSize(zdev_t* dev, zbuf_t* buf, u16_t size);
extern u16_t zfwBufRemoveHead(zdev_t* dev, zbuf_t* buf, u16_t size);
extern u16_t zfwBufGetSize(zdev_t* dev, zbuf_t* buf);
extern void  zfwCopyBufContext(zdev_t* dev, zbuf_t* source, zbuf_t* dest);

/* Memory management */
extern void* zfwMemAllocate(zdev_t* dev, u32_t size);
extern void zfwMemFree(zdev_t* dev, void* mem, u32_t size);
extern void zfwMemoryCopy(u8_t* dst, u8_t* src, u16_t length);
extern void zfwMemoryMove(u8_t* dst, u8_t* src, u16_t length);
extern void zfwZeroMemory(u8_t* va, u16_t length);
extern u8_t zfwMemoryIsEqual(u8_t* m1, u8_t* m2, u16_t length);

/* Others */
extern void zfwSleep(zdev_t* dev, u32_t ms);
extern u16_t zfwGetVapId(zdev_t* dev);
extern u16_t zfwStaAddIeWpaRsn(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t frameType);
extern u32_t zfwWaitForEvent(zdev_t *dev, u32_t event, u32_t timeout);
extern void zfwSendEvent(zdev_t* dev);
extern void zfwGetActiveScanDur(zdev_t* dev, u8_t* Dur );
extern void zfwGetShowZeroLengthSSID(zdev_t* dev, u8_t* Dur );
/* For debugging */
extern void zfwDumpBuf(zdev_t* dev, zbuf_t* buf);
extern void zfwDbgReadRegDone(zdev_t* dev, u32_t addr, u32_t val);
/* For Evl */
extern void zfwDbgDownloadFwInitDone(zdev_t* dev);
extern void zfwDbgReadFlashDone(zdev_t* dev, u32_t addr, u32_t* rspdata, u32_t datalen);
extern void zfwDbgGetFlashChkSumDone(zdev_t* dev, u32_t* rspdata);
extern void zfwDbgProgrameFlashDone(zdev_t* dev);
extern void zfwDbgProgrameFlashChkDone(zdev_t* dev);
extern void zfwDbgWriteRegDone(zdev_t* dev, u32_t addr, u32_t val);
extern void zfwDbgWriteEepromDone(zdev_t* dev, u32_t addr, u32_t val);
extern void zfwDbgReadTallyDone(zdev_t* dev);
extern void zfwWlanReadRegDone(zdev_t* dev, u32_t addr, u32_t val);
extern void zfwWlanWriteRegDone(zdev_t* dev, u32_t addr, u32_t val);
extern void zfwWlanReadTallyDone(zdev_t* dev);
extern void zfwDbgQueryHwTxBusyDone(zdev_t* dev, u32_t val);
extern u32_t zfwReadReg(zdev_t* dev, u32_t offset);
extern u32_t zfwReadEeprom(zdev_t* dev, u32_t addr);

/* Reserved for Vista, please return 0 */
extern u8_t zfwGetPktEncExemptionActionType(zdev_t* dev, zbuf_t* buf);

#ifdef ZM_ENABLE_CENC
/* Reserved for CENC, please return 0 */
extern u8_t zfwCencHandleBeaconProbrespon(zdev_t* dev, u8_t *pWIEc,
        u8_t *pPeerSSIDc, u8_t *pPeerAddrc);
#endif //ZM_ENABLE_CENC

#ifdef ZM_HALPLUS_LOCK
extern asmlinkage struct zsWlanDev *zfwGetWlanDev(zdev_t* dev);
extern asmlinkage void zfwEnterCriticalSection(zdev_t* dev);
extern asmlinkage void zfwLeaveCriticalSection(zdev_t* dev);
extern asmlinkage u8_t zfwBufReadByte(zdev_t* dev, zbuf_t* buf, u16_t offset);
extern asmlinkage u16_t zfwBufReadHalfWord(zdev_t* dev, zbuf_t* buf, u16_t offset);
extern asmlinkage void zfwBufWriteByte(zdev_t* dev, zbuf_t* buf, u16_t offset, u8_t value);
extern asmlinkage void zfwBufWriteHalfWord(zdev_t* dev, zbuf_t* buf, u16_t offset, u16_t value);
extern asmlinkage u8_t *zfwGetBuffer(zdev_t* dev, zbuf_t* buf);
#endif

#endif //_PUB_ZFW_H

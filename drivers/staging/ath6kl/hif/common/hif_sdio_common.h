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
// common header file for HIF modules designed for SDIO
//
// Author(s): ="Atheros"
//==============================================================================

#ifndef HIF_SDIO_COMMON_H_
#define HIF_SDIO_COMMON_H_

    /* SDIO manufacturer ID and Codes */
#define MANUFACTURER_ID_AR6002_BASE        0x200
#define MANUFACTURER_ID_AR6003_BASE        0x300
#define MANUFACTURER_ID_AR6K_BASE_MASK     0xFF00
#define FUNCTION_CLASS                     0x0
#define MANUFACTURER_CODE                  0x271    /* Atheros */

    /* Mailbox address in SDIO address space */
#define HIF_MBOX_BASE_ADDR                 0x800
#define HIF_MBOX_WIDTH                     0x800
#define HIF_MBOX_START_ADDR(mbox)               \
   ( HIF_MBOX_BASE_ADDR + mbox * HIF_MBOX_WIDTH)

#define HIF_MBOX_END_ADDR(mbox)                 \
    (HIF_MBOX_START_ADDR(mbox) + HIF_MBOX_WIDTH - 1)

    /* extended MBOX address for larger MBOX writes to MBOX 0*/
#define HIF_MBOX0_EXTENDED_BASE_ADDR       0x2800
#define HIF_MBOX0_EXTENDED_WIDTH_AR6002    (6*1024)           
#define HIF_MBOX0_EXTENDED_WIDTH_AR6003    (18*1024)   

    /* version 1 of the chip has only a 12K extended mbox range */
#define HIF_MBOX0_EXTENDED_BASE_ADDR_AR6003_V1  0x4000
#define HIF_MBOX0_EXTENDED_WIDTH_AR6003_V1      (12*1024)  

    /* GMBOX addresses */
#define HIF_GMBOX_BASE_ADDR                0x7000
#define HIF_GMBOX_WIDTH                    0x4000

    /* for SDIO we recommend a 128-byte block size */
#define HIF_DEFAULT_IO_BLOCK_SIZE          128

    /* set extended MBOX window information for SDIO interconnects */
static INLINE void SetExtendedMboxWindowInfo(u16 Manfid, struct hif_device_mbox_info *pInfo)
{
    switch (Manfid & MANUFACTURER_ID_AR6K_BASE_MASK) {                   
        case MANUFACTURER_ID_AR6002_BASE :
                /* MBOX 0 has an extended range */
            pInfo->MboxProp[0].ExtendedAddress = HIF_MBOX0_EXTENDED_BASE_ADDR;             
            pInfo->MboxProp[0].ExtendedSize = HIF_MBOX0_EXTENDED_WIDTH_AR6002;
            break;
        case MANUFACTURER_ID_AR6003_BASE :
                /* MBOX 0 has an extended range */
            pInfo->MboxProp[0].ExtendedAddress = HIF_MBOX0_EXTENDED_BASE_ADDR_AR6003_V1;             
            pInfo->MboxProp[0].ExtendedSize = HIF_MBOX0_EXTENDED_WIDTH_AR6003_V1;
            pInfo->GMboxAddress = HIF_GMBOX_BASE_ADDR;
            pInfo->GMboxSize = HIF_GMBOX_WIDTH;
            break;
        default:
            A_ASSERT(false);
            break;
    }
}
             
/* special CCCR (func 0) registers */

#define CCCR_SDIO_IRQ_MODE_REG         0xF0        /* interrupt mode register */
#define SDIO_IRQ_MODE_ASYNC_4BIT_IRQ   (1 << 0)    /* mode to enable special 4-bit interrupt assertion without clock*/ 
                        
#endif /*HIF_SDIO_COMMON_H_*/

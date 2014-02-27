
/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/hif/sdio/include/hif_sdio.h#1 $
*/

/*! \file   "hif_sdio.h"
    \brief


*/

/*
** $Log: $
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 05 02 2012 terry.wu
 * NULL
 * Fix build error if HAVE_AEE_FEATURE = n
 *
 * 04 12 2012 terry.wu
 * NULL
 * Add AEE message support
 * 1) Show AEE warning(red screen) if SDIO access error occurs

 *
 * 03 02 2012 terry.wu
 * NULL
 * Enable CFG80211 Support.
 *
 * 08 18 2010 jeffrey.chang
 * NULL
 * support multi-function sdio
 *
 * 07 25 2010 george.kuo
 *
 * Move hif_sdio driver to linux directory.
 *
 * 07 23 2010 george.kuo
 *
 * Add MT6620 driver source tree
 * , including char device driver (wmt, bt, gps), stp driver, interface driver (tty ldisc and hif_sdio), and bt hci driver.
**
**
*/

#ifndef _HIF_SDIO_H
#define _HIF_SDIO_H
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define HIF_SDIO_DEBUG  (0) /* 0:trun off debug msg and assert, 1:trun off debug msg and assert */

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "mtk_porting.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define CFG_CLIENT_COUNT  (8)

#define HIF_DEFAULT_BLK_SIZE  (256)
#define HIF_DEFAULT_VENDOR    (0x037A)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* Function info provided by client driver */
typedef struct _MTK_WCN_HIF_SDIO_FUNCINFO MTK_WCN_HIF_SDIO_FUNCINFO;

/* Client context provided by hif_sdio driver for the following function call */
typedef UINT32 MTK_WCN_HIF_SDIO_CLTCTX;

/* Callback functions provided by client driver */
typedef INT32 (*MTK_WCN_HIF_SDIO_PROBE)(MTK_WCN_HIF_SDIO_CLTCTX, const MTK_WCN_HIF_SDIO_FUNCINFO *);
typedef INT32 (*MTK_WCN_HIF_SDIO_REMOVE)(MTK_WCN_HIF_SDIO_CLTCTX);
typedef INT32 (*MTK_WCN_HIF_SDIO_IRQ)(MTK_WCN_HIF_SDIO_CLTCTX);

/* Function info provided by client driver */
struct _MTK_WCN_HIF_SDIO_FUNCINFO {
    UINT16 manf_id;    /* TPLMID_MANF: manufacturer ID */
    UINT16 card_id;    /* TPLMID_CARD: card ID */
    UINT16 func_num;    /* Function Number */
    UINT16 blk_sz;    /* Function block size */
};

/* Client info provided by client driver */
typedef struct _MTK_WCN_HIF_SDIO_CLTINFO {
    const MTK_WCN_HIF_SDIO_FUNCINFO *func_tbl; /* supported function info table */
    UINT32 func_tbl_size; /* supported function table info element number */
    MTK_WCN_HIF_SDIO_PROBE hif_clt_probe; /* callback function for probing */
    MTK_WCN_HIF_SDIO_REMOVE hif_clt_remove; /* callback function for removing */
    MTK_WCN_HIF_SDIO_IRQ hif_clt_irq; /* callback function for interrupt handling */
} MTK_WCN_HIF_SDIO_CLTINFO;

/* function info provided by registed function */
typedef struct _MTK_WCN_HIF_SDIO_REGISTINFO {
    const MTK_WCN_HIF_SDIO_CLTINFO *sdio_cltinfo; /* client's MTK_WCN_HIF_SDIO_CLTINFO pointer */
    const MTK_WCN_HIF_SDIO_FUNCINFO *func_info; /* supported function info pointer */
} MTK_WCN_HIF_SDIO_REGISTINFO;

/* Card info provided by probed function */
typedef struct _MTK_WCN_HIF_SDIO_PROBEINFO {
    struct sdio_func* func;  /* probed sdio function pointer */
    INT8 clt_idx;   /* registered function table info element number (initial value is -1) */
    MTK_WCN_BOOL interrupted;   /* TRUE: interrupted, FALSE: not interrupted */
    void* private_data_p;  /* clt's private data pointer */
} MTK_WCN_HIF_SDIO_PROBEINFO;

/* work queue info needed by worker */
typedef struct _MTK_WCN_HIF_SDIO_CLT_PROBE_WORKERINFO {
    struct work_struct probe_work;   /* work queue structure */
    MTK_WCN_HIF_SDIO_REGISTINFO *registinfo_p;  /* MTK_WCN_HIF_SDIO_REGISTINFO pointer of the client */
    INT8 probe_idx;   /* probed function table info element number (initial value is -1) */
} MTK_WCN_HIF_SDIO_CLT_PROBE_WORKERINFO;

/* error code returned by hif_sdio driver (use NEGATIVE number) */
typedef enum {
    HIF_SDIO_ERR_SUCCESS = 0,
    HIF_SDIO_ERR_FAIL, /* generic error */
    HIF_SDIO_ERR_INVALID_PARAM,
    HIF_SDIO_ERR_DUPLICATED,
    HIF_SDIO_ERR_UNSUP_MANF_ID,
    HIF_SDIO_ERR_UNSUP_CARD_ID,
    HIF_SDIO_ERR_INVALID_FUNC_NUM,
    HIF_SDIO_ERR_INVALID_BLK_SZ,
} MTK_WCN_HIF_SDIO_ERR ;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*!
 * \brief A macro used to describe an SDIO function
 *
 * Fill an MTK_WCN_HIF_SDIO_FUNCINFO structure with function-specific information
 *
 * \param manf      the 16 bit manufacturer id
 * \param card      the 16 bit card id
 * \param func      the 16 bit function number
 * \param b_sz    the 16 bit function block size
 */
#define MTK_WCN_HIF_SDIO_FUNC(manf, card, func, b_sz) \
	.manf_id = (manf), .card_id = (card), .func_num = (func), .blk_sz = (b_sz)

/*!
 * \brief A debug print used to print debug messages while compiler flag HIF_SDIO_DEBUG on.
 *
 */
#if HIF_SDIO_DEBUG
#define DPRINTK(fmt, args...) printk("%s: " fmt, __FUNCTION__, ## args)
#else
#define DPRINTK(fmt, args...)
#endif

/*!
 * \brief ASSERT function definition.
 *
 */
#if HIF_SDIO_DEBUG
#define ASSERT(expr)    if ( !(expr) ) { \
                            printk("assertion failed! %s[%d]: %s\n",\
                                __FUNCTION__, __LINE__, #expr); \
                            BUG_ON( !(expr) );\
                        }
#else
#define ASSERT(expr)    do {} while(0)
#endif

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*!
 * \brief MTK hif sdio client registration function
 *
 * Client uses this function to do hif sdio registration
 *
 * \param pinfo     a pointer of client's information
 *
 * \retval 0    register successfully
 * \retval < 0  error code
 */
extern INT32 mtk_wcn_hif_sdio_client_reg (
    const MTK_WCN_HIF_SDIO_CLTINFO *pinfo
    );

extern INT32 mtk_wcn_hif_sdio_client_unreg (
    const MTK_WCN_HIF_SDIO_CLTINFO *pinfo
    );

extern INT32 mtk_wcn_hif_sdio_readb (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    PUINT8 pvb
    );

extern INT32 mtk_wcn_hif_sdio_writeb (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    UINT8 vb
    );

extern INT32 mtk_wcn_hif_sdio_readl (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    PUINT32 pvl
    );

extern INT32 mtk_wcn_hif_sdio_writel (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    UINT32 vl
    );

extern INT32 mtk_wcn_hif_sdio_read_buf (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    PUINT32 pbuf,
    UINT32 len
    );

extern INT32 mtk_wcn_hif_sdio_write_buf (
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    UINT32 offset,
    PUINT32 pbuf,
    UINT32 len
    );

extern void mtk_wcn_hif_sdio_set_drvdata(
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    void* private_data_p
    );

extern void* mtk_wcn_hif_sdio_get_drvdata(
    MTK_WCN_HIF_SDIO_CLTCTX ctx
    );

extern void mtk_wcn_hif_sdio_get_dev(
    MTK_WCN_HIF_SDIO_CLTCTX ctx,
    struct device **dev
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _HIF_SDIO_H */




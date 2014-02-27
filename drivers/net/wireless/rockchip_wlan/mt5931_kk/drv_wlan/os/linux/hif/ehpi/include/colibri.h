/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/hif/ehpi/include/colibri.h#1 $
*/

/*! \file   "colibri.h"
    \brief  This file contains colibri BSP configuration based on eHPI interface

    N/A
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: colibri.h $
 *
 * 04 06 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. do not check for pvData inside wlanNetCreate() due to it is NULL for eHPI  port
 * 2. update perm_addr as well for MAC address
 * 3. not calling check_mem_region() anymore for eHPI
 * 4. correct MSC_CS macro for 0-based notation
 *
 * 04 01 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. simplify config.h due to aggregation options could be also applied for eHPI/SPI interface
 * 2. use spin-lock instead of semaphore for protecting eHPI access because of possible access from ISR
 * 3. request_irq() API has some changes between linux kernel 2.6.12 and 2.6.26
 *
 * 03 23 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * apply multi-queue operation only for linux kernel > 2.6.26
 *
 * 03 11 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * add porting layer for eHPI.
**
*/
#ifndef _COLIBRI_H
#define _COLIBRI_H
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/arch/system.h>
#include <asm/arch/pxa2xx-gpio.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/


#define WLAN_STA_IRQ_GPIO   23  /* use SSP_EXTCLK as interrupt source */
#define WLAN_STA_IRQ IRQ_GPIO(WLAN_STA_IRQ_GPIO)

#define MSC_CS(cs,val) ((val)<<(((cs)&1)<<4))

#define MSC_RBUFF_SHIFT 15
#define MSC_RBUFF(x) ((x)<<MSC_RBUFF_SHIFT)
#define MSC_RBUFF_SLOW  MSC_RBUFF(0)
#define MSC_RBUFF_FAST  MSC_RBUFF(1)

#define MSC_RRR_SHIFT 12
#define MSC_RRR_MASK    0x7UL
#define MSC_RRR(x) (((x) & MSC_RRR_MASK) << MSC_RRR_SHIFT)

#define MSC_RDN_SHIFT 8
#define MSC_RDN_MASK    0xFUL
#define MSC_RDN(x) (((x) & MSC_RDN_MASK ) << MSC_RDN_SHIFT)

#define MSC_RDF_SHIFT 4
#define MSC_RDF_MASK    0xFUL
#define MSC_RDF(x) (((x) & MSC_RDF_MASK) << MSC_RDF_SHIFT)

#define MSC_RBW_SHIFT 3
#define MSC_RBW_MASK    0x1UL
#define MSC_RBW(x) (((x) & MSC_RBW_MASK) << MSC_RBW_SHIFT)
#define MSC_RBW_16  MSC_RBW(1)
#define MSC_RBW_32  MSC_RBW(0)

#define MSC_RT_SHIFT  0
#define MSC_RT_MASK    0x7UL
#define MSC_RT(x) (((x) & MSC_RT_MASK) << MSC_RT_SHIFT)
#define MSC_RT_TYPE_0   MSC_RT(0)
#define MSC_RT_SRAM   MSC_RT(1)
#define MSC_RT_TYPE_2   MSC_RT(2)
#define MSC_RT_TYPE_3   MSC_RT(3)
#define MSC_RT_VLIO   MSC_RT(4)

#define EHPI_OFFSET_ADDR (8UL)  /* connect host a3 to evb a0 */
#define EHPI_OFFSET_DATA (0UL)

/* PXA270 specific part -- start */
#ifndef __PXA270__
#define __PXA270__

#define CS0_BASE    0x00000000
#define CS1_BASE    0x04000000
#define CS2_BASE    0x08000000
#define CS3_BASE    0x0C000000
#define CS4_BASE    0x10000000
#define CS5_BASE    0x14000000

#define MEM_MAPPED_ADDR     CS4_BASE
#define MEM_MAPPED_LEN      0x80

// other register definitions come from include/asm/arch/pxa-regs.h

#endif
/* PXA270 specific part -- end */



/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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
#define set_GPIO_mode pxa_gpio_mode

#ifndef SA_SHIRQ
    #define SA_SHIRQ        0x04000000
#endif

#ifndef GPIO_OUT
    #define GPIO_OUT        0x080
#endif

#ifndef GPIO80_nCS_4_MD
    #ifndef GPIO_ALT_FN_2_OUT
        #define GPIO_ALT_FN_2_OUT   0x280
    #endif

    #define GPIO80_nCS_4_MD     (80 | GPIO_ALT_FN_2_OUT)
#endif

#ifndef MSC2
    #define MSC2        __REG(0x48000010)  /* Static Memory Control Register 2 */
#endif


/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


#endif /* _COLIBRI_H */


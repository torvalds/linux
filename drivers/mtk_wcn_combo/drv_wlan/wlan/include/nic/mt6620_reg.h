/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/nic/mt6620_reg.h#1 $
*/

/*! \file   "mt6620_reg.h"
    \brief  The common register definition of mt6620

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
** $Log: mt6620_reg.h $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 01 31 2011 terry.wu
 * [WCXRP00000412] [MT6620 Wi-Fi][FW/Driver] Dump firmware assert info at android kernel log
 * Print firmware ASSERT info at Android kernel log, driver side
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * 1) add ACPI D0/D3 state switching support
 *  *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-12-10 16:44:18 GMT mtk02752
**  remove 5921 definitions
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-09 22:56:32 GMT mtk01084
**  modify HW register definitions
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-04 14:11:04 GMT mtk01084
**  modify default IER bits
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-10-29 19:52:32 GMT mtk01084
**  modify data struture
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-10-23 16:08:20 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-10-13 21:58:53 GMT mtk01084
**  update for new HW architecture design
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-09-09 17:26:11 GMT mtk01084
**  add CFG_TEST_WITH_MT5921
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-05-18 20:59:57 GMT mtk01426
**  Update WHIER_DEFAULT value
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-05-07 16:57:36 GMT mtk01426
**  Update CHIP ID to 0x6620, and WHLPCR bit definition
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-28 10:34:57 GMT mtk01461
**  Add read WTSR and fix RX STATUS is DW align for SDIO_STATUS_ENHANCE mode
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-24 09:46:52 GMT mtk01084
**  fix LINT error
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-23 00:32:24 GMT mtk01461
**  Define constants for TX PATH
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-18 20:54:10 GMT mtk01426
**  Add WHCR_MAX_HIF_RX_AGG_LEN_OFFSET definition
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:29 GMT mtk01426
**  Init for develop
**
*/

#ifndef _MT6620_REG_H
#define _MT6620_REG_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

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

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

//1 MT6620 MCR Definition

//2 Host Interface

//4 CHIP ID Register
#define MCR_WCIR                            0x0000

//4 HIF Low Power Control  Register
#define MCR_WHLPCR                          0x0004
//#define MCR_WHLPCR_BYTE1                    0x0005


//4 Control  Status Register
#define MCR_WSDIOCSR                        0x0008
#define MCR_WSPICSR                         0x0008

//4 HIF Control Register
#define MCR_WHCR                            0x000C

//4 HIF Interrupt Status  Register
#define MCR_WHISR                           0x0010

//4 HIF Interrupt Enable  Register
#define MCR_WHIER                           0x0014

//4 Abnormal Status Register
#define MCR_WASR                            0x0018

//4 WLAN Software Interrupt Control Register
#define MCR_WSICR                           0x001C

//4 WLAN TX Status Register
#define MCR_WTSR0                           0x0020

//4 WLAN TX Status Register
#define MCR_WTSR1                           0x0024

//4 WLAN TX Data Register 0
#define MCR_WTDR0                           0x0028

//4 WLAN TX Data Register 1
#define MCR_WTDR1                           0x002C

//4 WLAN RX Data Register 0
#define MCR_WRDR0                           0x0030

//4 WLAN RX Data Register 1
#define MCR_WRDR1                           0x0034

//4 Host to Device Send Mailbox 0 Register
#define MCR_H2DSM0R                         0x0038

//4 Host to Device Send Mailbox 1 Register
#define MCR_H2DSM1R                         0x003c

//4 Device to Host Receive Mailbox 0 Register
#define MCR_D2HRM0R                         0x0040

//4 Device to Host Receive Mailbox 1 Register
#define MCR_D2HRM1R                         0x0044

//4 WLAN RX Packet Length Register
#define MCR_WRPLR                           0x0048




//temp //#if CFG_SDIO_INTR_ENHANCE
typedef struct _ENHANCE_MODE_DATA_STRUCT_T {
    UINT_32             u4WHISR;
    union {
        struct {
            UINT_8              ucTQ0Cnt;
            UINT_8              ucTQ1Cnt;
            UINT_8              ucTQ2Cnt;
            UINT_8              ucTQ3Cnt;
            UINT_8              ucTQ4Cnt;
            UINT_8              ucTQ5Cnt;
            UINT_16             u2Rsrv;
        } u;
        UINT_32                 au4WTSR[2];
    } rTxInfo;
    union {
        struct {
            UINT_16             u2NumValidRx0Len;
            UINT_16             u2NumValidRx1Len;
            UINT_16             au2Rx0Len[16];
            UINT_16             au2Rx1Len[16];
        } u;
        UINT_32                 au4RxStatusRaw[17];
    } rRxInfo;
    UINT_32                     u4RcvMailbox0;
    UINT_32                     u4RcvMailbox1;
} ENHANCE_MODE_DATA_STRUCT_T, *P_ENHANCE_MODE_DATA_STRUCT_T;
// #endif /* ENHANCE_MODE_DATA_STRUCT_T */


//2 Definition in each register
//3 WCIR 0x0000
#define WCIR_WLAN_READY                  BIT(21)
#define WCIR_POR_INDICATOR               BIT(20)
#define WCIR_REVISION_ID                 BITS(16,19)
#define WCIR_CHIP_ID                     BITS(0,15)

#define MTK_CHIP_REV                     0x00006620
#define MTK_CHIP_MP_REVERSION_ID         0x0

//3 WHLPCR 0x0004
#define WHLPCR_FW_OWN_REQ_CLR            BIT(9)
#define WHLPCR_FW_OWN_REQ_SET            BIT(8)
#define WHLPCR_IS_DRIVER_OWN             BIT(8)
#define WHLPCR_INT_EN_CLR                BIT(1)
#define WHLPCR_INT_EN_SET                BIT(0)

//3 WSDIOCSR 0x0008
#define WSDIOCSR_SDIO_RE_INIT_EN         BIT(0)

//3 WSPICSR 0x0008
#define WCSR_SPI_MODE_SEL                BITS(3,4)
#define WCSR_SPI_ENDIAN_BIG              BIT(2)
#define WCSR_SPI_INT_OUT_MODE            BIT(1)
#define WCSR_SPI_DATA_OUT_MODE           BIT(0)

//3 WHCR 0x000C
#define WHCR_RX_ENHANCE_MODE_EN         BIT(16)
#define WHCR_MAX_HIF_RX_LEN_NUM         BITS(4,7)
#define WHCR_W_MAILBOX_RD_CLR_EN        BIT(2)
#define WHCR_W_INT_CLR_CTRL             BIT(1)
#define WHCR_MCU_DBG_EN                 BIT(0)
#define WHCR_OFFSET_MAX_HIF_RX_LEN_NUM  4

//3 WHISR 0x0010
#define WHISR_D2H_SW_INT                BITS(8,31)
#define WHISR_D2H_SW_ASSERT_INFO_INT    BIT(31)
#define WHISR_FW_INT_INDICATOR          BIT(7)
#define WHISR_FW_OWN_BACK_INT           BIT(4)
#define WHISR_ABNORMAL_INT              BIT(3)
#define WHISR_RX1_DONE_INT              BIT(2)
#define WHISR_RX0_DONE_INT              BIT(1)
#define WHISR_TX_DONE_INT               BIT(0)


//3 WHIER 0x0014
#define WHIER_D2H_SW_INT                BITS(8,31)
#define WHIER_FW_INT_INDICATOR_EN       BIT(7)
#define WHIER_FW_OWN_BACK_INT_EN        BIT(4)
#define WHIER_ABNORMAL_INT_EN           BIT(3)
#define WHIER_RX1_DONE_INT_EN           BIT(2)
#define WHIER_RX0_DONE_INT_EN           BIT(1)
#define WHIER_TX_DONE_INT_EN            BIT(0)
#define WHIER_DEFAULT                   (WHIER_RX0_DONE_INT_EN    | \
                                         WHIER_RX1_DONE_INT_EN    | \
                                         WHIER_TX_DONE_INT_EN     | \
                                         WHIER_ABNORMAL_INT_EN    | \
                                         WHIER_D2H_SW_INT           \
                                         )


//3 WASR 0x0018
#define WASR_FW_OWN_INVALID_ACCESS      BIT(4)
#define WASR_RX1_UNDER_FLOW             BIT(3)
#define WASR_RX0_UNDER_FLOW             BIT(2)
#define WASR_TX1_OVER_FLOW              BIT(1)
#define WASR_TX0_OVER_FLOW              BIT(0)


//3 WSICR 0x001C
#define WSICR_H2D_SW_INT_SET            BITS(16,31)


//3 WTSR0 0x0044
#define WRPLR_RX1_PACKET_LENGTH         BITS(16,31)
#define WRPLR_RX0_PACKET_LENGTH         BITS(0,15)

#endif /* _MT6620_REG_H */


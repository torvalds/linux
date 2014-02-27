/*
** $Id: //Department/DaVinci/TRUNK/MT6620_5931_WiFi_Driver/include/nic/mt6628_reg.h#2 $
*/

/*! \file   "mt6628_reg.h"
    \brief  The common register definition of mt5931

    N/A
*/



/*
** $Log: mt6628_reg.h $
** 
** 09 12 2012 wcpadmin
** [ALPS00276400] Remove MTK copyright and legal header on GPL/LGPL related packages
** .
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 07 13 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add initial version for MT6628 driver support.
 *
*/

#ifndef _MT6628_REG_H
#define _MT6628_REG_H

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

//1 MT6628 MCR Definition

//2 Host Interface

//4 CHIP ID Register
#define MCR_WCIR                            0x0000

//4 HIF Low Power Control  Register
#define MCR_WHLPCR                          0x0004

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

//4 Device to Host Receive Mailbox 2 Register
#define MCR_D2HRM2R                         0x0048

//4 WLAN RX Packet Length Register
#define MCR_WRPLR                           0x0050


//#if CFG_SDIO_INTR_ENHANCE
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

#define MTK_CHIP_REV                     0x00006628
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
#define WHISR_FW_OWN_BACK_INT           BIT(4)
#define WHISR_ABNORMAL_INT              BIT(3)
#define WHISR_RX1_DONE_INT              BIT(2)
#define WHISR_RX0_DONE_INT              BIT(1)
#define WHISR_TX_DONE_INT               BIT(0)


//3 WHIER 0x0014
#define WHIER_D2H_SW_INT                BITS(8,31)
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


//3 WRPLR 0x0050
#define WRPLR_RX1_PACKET_LENGTH         BITS(16,31)
#define WRPLR_RX0_PACKET_LENGTH         BITS(0,15)


#endif /* _MT6628_REG_H */


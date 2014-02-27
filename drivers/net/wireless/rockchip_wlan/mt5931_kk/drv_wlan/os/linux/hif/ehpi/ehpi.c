/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/hif/ehpi/ehpi.c#1 $
*/

/*! \file   "ehpi.c"
    \brief  Brief description.

    Detail description.
*/

/******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
*******************************************************************************
*/

/******************************************************************************
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
*******************************************************************************
*/

/*
** $Log: ehpi.c $
 *
 * 04 25 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * change eHPI-8/eHPI-16 selection to config.mk.
 *
 * 04 01 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. simplify config.h due to aggregation options could be also applied for eHPI/SPI interface
 * 2. use spin-lock instead of semaphore for protecting eHPI access because of possible access from ISR
 * 3. request_irq() API has some changes between linux kernel 2.6.12 and 2.6.26
 *
 * 03 11 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * add porting layer for eHPI.
*/

/******************************************************************************
*                         C O M P I L E R   F L A G S
*******************************************************************************
*/
#if !defined(MCR_EHTCR)
#define MCR_EHTCR                           0x0054
#endif

/*******************************************************************************
*                E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_os.h"
#include "colibri.h"
#include "wlan_lib.h"

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                        P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                       P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                             M A C R O S
********************************************************************************
*/

/*******************************************************************************
*              F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static BOOL
kalDevRegRead_impl(
    IN  P_GLUE_INFO_T  prGlueInfo,
    IN  UINT_32        u4Register,
    OUT PUINT_32       pu4Value
    );

static BOOL
kalDevRegWrite_impl(
    IN  P_GLUE_INFO_T  prGlueInfo,
    IN  UINT_32        u4Register,
    IN  UINT_32        u4Value
    );


/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to read a 32 bit register value from device.
*
* \param[in] prGlueInfo     Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register     The register offset.
* \param[out] pu4Value      Pointer to the 32-bit value of the register been read.
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevRegRead(
    IN  P_GLUE_INFO_T  prGlueInfo,
    IN  UINT_32        u4Register,
    OUT PUINT_32       pu4Value
    )
{
    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);
    ASSERT(pu4Value);

    /* 0. acquire spinlock */
    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    /* 1. I/O stuff */
    kalDevRegRead_impl(prGlueInfo, u4Register, pu4Value);

    /* 2. release spin lock */
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to write a 32 bit register value to device.
*
* \param[in] prGlueInfo     Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register     The register offset.
* \param[out] u4Value       The 32-bit value of the register to be written.
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevRegWrite(
    P_GLUE_INFO_T  prGlueInfo,
    IN UINT_32     u4Register,
    IN UINT_32     u4Value
    )
{
    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);

    /* 0. acquire spinlock */
    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    /* 1. I/O stuff */
    kalDevRegWrite_impl(prGlueInfo, u4Register, u4Value);

    /* 2. release spin lock */
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to read port data from device in unit of byte.
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             The register offset.
* \param[in] u2Len              The number of byte to be read.
* \param[out] pucBuf            Pointer to data buffer for read
* \param[in] u2ValidOutBufSize  Length of the buffer valid to be accessed
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortRead(
    IN  P_GLUE_INFO_T   prGlueInfo,
    IN  UINT_16         u2Port,
    IN  UINT_16         u2Len,
    OUT PUINT_8         pucBuf,
    IN  UINT_16         u2ValidOutBufSize
    )
{
    UINT_32 i;
    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);

    /* 0. acquire spinlock */
    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    /* 1. indicate correct length to HIFSYS if larger than 4-bytes */
    if(u2Len > 4) {
        kalDevRegWrite_impl(prGlueInfo, MCR_EHTCR, ALIGN_4(u2Len));
    }

    /* 2. address cycle */
#if EHPI16
    writew(u2Port, prGlueInfo->rHifInfo.mcr_addr_base);
#elif EHPI8
    writew((u2Port & 0xFF), prGlueInfo->rHifInfo.mcr_addr_base);
    writew(((u2Port & 0xFF00) >> 8), prGlueInfo->rHifInfo.mcr_addr_base);
#endif

    /* 3. data cycle */
    for(i = 0 ; i < ALIGN_4(u2Len) ; i += 4) {
#if EHPI16
        *((PUINT_16)&(pucBuf[i]))      = (UINT_16) (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFFFF);
        *((PUINT_16)&(pucBuf[i+2]))    = (UINT_16) (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFFFF);
#elif EHPI8
        *((PUINT_8)&(pucBuf[i]))       = (UINT_8)  (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF);
        *((PUINT_8)&(pucBuf[i+1]))     = (UINT_8)  (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF);
        *((PUINT_8)&(pucBuf[i+2]))     = (UINT_8)  (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF);
        *((PUINT_8)&(pucBuf[i+3]))     = (UINT_8)  (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF);
#endif
    }

    /* 4. restore length to 4 if necessary */
    if(u2Len > 4) {
        kalDevRegWrite_impl(prGlueInfo, MCR_EHTCR, 4);
    }

    /* 5. release spin lock */
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to write port data to device in unit of byte.
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             The register offset.
* \param[in] u2Len              The number of byte to be write.
* \param[out] pucBuf            Pointer to data buffer for write
* \param[in] u2ValidOutBufSize  Length of the buffer valid to be accessed
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortWrite(
    P_GLUE_INFO_T  prGlueInfo,
    IN UINT_16     u2Port,
    IN UINT_16     u2Len,
    IN PUINT_8     pucBuf,
    IN UINT_16     u2ValidInBufSize
    )
{
    UINT_32 i;
    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);

    /* 0. acquire spinlock */
    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    /* 1. indicate correct length to HIFSYS if larger than 4-bytes */
    if(u2Len > 4) {
        kalDevRegWrite_impl(prGlueInfo, MCR_EHTCR, ALIGN_4(u2Len));
    }

    /* 2. address cycle */
#if EHPI16
    writew(u2Port, prGlueInfo->rHifInfo.mcr_addr_base);
#elif EHPI8
    writew((u2Port & 0xFF), prGlueInfo->rHifInfo.mcr_addr_base);
    writew(((u2Port & 0xFF00) >> 8), prGlueInfo->rHifInfo.mcr_addr_base);
#endif

    /* 3. data cycle */
    for(i = 0 ; i < ALIGN_4(u2Len) ; i += 4) {
#if EHPI16
        writew((UINT_32)(*((PUINT_16) &(pucBuf[i]))), prGlueInfo->rHifInfo.mcr_data_base);
        writew((UINT_32)(*((PUINT_16) &(pucBuf[i+2]))), prGlueInfo->rHifInfo.mcr_data_base);
#elif EHPI8
        writew((UINT_32)(*((PUINT_8)  &(pucBuf[i]))), prGlueInfo->rHifInfo.mcr_data_base);
        writew((UINT_32)(*((PUINT_8)  &(pucBuf[i+1]))), prGlueInfo->rHifInfo.mcr_data_base);
        writew((UINT_32)(*((PUINT_8)  &(pucBuf[i+2]))), prGlueInfo->rHifInfo.mcr_data_base);
        writew((UINT_32)(*((PUINT_8)  &(pucBuf[i+3]))), prGlueInfo->rHifInfo.mcr_data_base);
#endif
    }
 
    /* 4. restore length to 4 if necessary */
    if(u2Len > 4) {
        kalDevRegWrite_impl(prGlueInfo, MCR_EHTCR, 4);
    }

    /* 5. release spin lock */
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Write device I/O port with single byte (for SDIO compatibility)
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u4Addr             I/O port offset
* \param[in] ucData             single byte of data to be written
* \param[in] u4ValidInBufSize   Length of the buffer valid to be accessed
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevWriteWithSdioCmd52 (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_32          u4Addr,
    IN UINT_8           ucData
    )
{
    UINT_32 u4RegValue;
    BOOLEAN bRet;
    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);

    /* 0. acquire spinlock */
    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    /* 1. there is no single byte access support for eHPI, use 4-bytes write-after-read approach instead */
    if(kalDevRegRead_impl(prGlueInfo, u4Addr, &u4RegValue) == TRUE) {
        u4RegValue &= 0x00;
        u4RegValue |= ucData;

        bRet = kalDevRegWrite_impl(prGlueInfo, u4Addr, u4RegValue);
    }
    else {
        bRet = FALSE;
    }

    /* 2. release spin lock */
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_EHPI_BUS);

    return bRet;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to read a 32 bit register value from device 
*        without spin lock protection and dedicated for internal use
*
* \param[in] prGlueInfo     Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register     The register offset.
* \param[out] pu4Value      Pointer to the 32-bit value of the register been read.
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
static BOOL
kalDevRegRead_impl(
    IN  P_GLUE_INFO_T  prGlueInfo,
    IN  UINT_32        u4Register,
    OUT PUINT_32       pu4Value
    )
{
    ASSERT(prGlueInfo);

    /* 1. address cycle */
#if EHPI16
    writew(u4Register, prGlueInfo->rHifInfo.mcr_addr_base);
#elif EHPI8
    writew((u4Register & 0xFF), prGlueInfo->rHifInfo.mcr_addr_base);
    writew(((u4Register & 0xFF00) >> 8), prGlueInfo->rHifInfo.mcr_addr_base);
#endif

    /* 2. data cycle */
#if EHPI16
    *pu4Value   = (readw(prGlueInfo->rHifInfo.mcr_data_base)& 0xFFFF);
    *pu4Value   |= ((readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFFFF) << 16);
#elif EHPI8
    *pu4Value   =   (readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF);
    *pu4Value   |= ((readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF) << 8);
    *pu4Value   |= ((readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF) << 16);
    *pu4Value   |= ((readw(prGlueInfo->rHifInfo.mcr_data_base) & 0xFF) << 24);
#endif

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to write a 32 bit register value to device.
*        without spin lock protection and dedicated for internal use
*
* \param[in] prGlueInfo     Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register     The register offset.
* \param[out] u4Value       The 32-bit value of the register to be written.
*
* \retval TRUE
* \retval FALSE
*/
/*----------------------------------------------------------------------------*/
static BOOL
kalDevRegWrite_impl(
    IN  P_GLUE_INFO_T  prGlueInfo,
    IN  UINT_32        u4Register,
    IN  UINT_32        u4Value
    )
{
    ASSERT(prGlueInfo);

    /* 1. address cycle */
#if EHPI16
    writew(u4Register, prGlueInfo->rHifInfo.mcr_addr_base);
#elif EHPI8
    writew((u4Register & 0xFF), prGlueInfo->rHifInfo.mcr_addr_base);
    writew(((u4Register & 0xFF00) >> 8), prGlueInfo->rHifInfo.mcr_addr_base);
#endif

    /* 2. data cycle */
#if EHPI16
    writew(u4Value, prGlueInfo->rHifInfo.mcr_data_base);
    writew((u4Value & 0xFFFF0000) >> 16, prGlueInfo->rHifInfo.mcr_data_base);
#elif EHPI8
    writew((u4Value & 0x000000FF), prGlueInfo->rHifInfo.mcr_data_base);
    writew((u4Value & 0x0000FF00) >> 8, prGlueInfo->rHifInfo.mcr_data_base);
    writew((u4Value & 0x00FF0000) >> 16, prGlueInfo->rHifInfo.mcr_data_base);
    writew((u4Value & 0xFF000000) >> 24, prGlueInfo->rHifInfo.mcr_data_base);
#endif

    return TRUE;
}



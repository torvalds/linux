/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/hif/ehpi/arm.c#1 $
*/

/*! \file   "colibri.c"
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
** $Log: colibri.c $
 *
 * 04 08 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. correction: RX aggregation is not limited to SDIO but for all host interface options
 * 2. add forward declarations for DBG-only symbols
 *
 * 04 06 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * correcting ISR behaviour for EHPI
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
 * 03 31 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. always reset GPIO mode to INPUT mode
 * 2. SA_SHIRQ has been deprecated in later linux kernel
 *
 * 03 23 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * duplicate colibri.c to arm.c for Colibri BSP 5.0.
 *
 * 03 14 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * remove unused variables.
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
static void __iomem *mt5931_mcr_base;


/*******************************************************************************
*                             M A C R O S
********************************************************************************
*/
#if CFG_EHPI_FASTER_BUS_TIMING
    #define EHPI_CONFIG     MSC_CS(4, MSC_RBUFF_SLOW | \
            MSC_RRR(4) | \
            MSC_RDN(8) | \
            MSC_RDF(7) | \
            MSC_RBW_16 | \
            MSC_RT_VLIO)
#else
    #define EHPI_CONFIG     MSC_CS(4, MSC_RBUFF_SLOW | \
            MSC_RRR(7) | \
            MSC_RDN(13) | \
            MSC_RDF(12) | \
            MSC_RBW_16 | \
            MSC_RT_VLIO)
#endif /* CFG_EHPI_FASTER_BUS_TIMING */

/*******************************************************************************
*              F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static VOID
collibri_ehpi_reg_init(
    VOID
    );

static VOID
collibri_ehpi_reg_uninit(
    VOID
    );

static VOID
mt5931_ehpi_reg_init(
    VOID
    );

static VOID
mt5931_ehpi_reg_uninit(
    VOID
    );

static void
busSetIrq(
	void
	);

static void
busFreeIrq(
	void
	);

static irqreturn_t 
glEhpiInterruptHandler(
    int irq, 
    void *dev_id
    );

#if DBG
static void
initTrig(
    void
    );
#endif

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will register sdio bus to the os
*
* \param[in] pfProbe    Function pointer to detect card
* \param[in] pfRemove   Function pointer to remove card
*
* \return The result of registering sdio bus
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
glRegisterBus (
    probe_card pfProbe,
    remove_card pfRemove
    )
{    

    ASSERT(pfProbe);
    ASSERT(pfRemove);

    printk(KERN_INFO "mtk_sdio: MediaTek eHPI WLAN driver\n");
    printk(KERN_INFO "mtk_sdio: Copyright MediaTek Inc.\n");

    if(pfProbe(NULL) != WLAN_STATUS_SUCCESS){
        pfRemove();
        return WLAN_STATUS_FAILURE;
    }
 
    return WLAN_STATUS_SUCCESS;
} /* end of glRegisterBus() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will unregister sdio bus to the os
*
* \param[in] pfRemove   Function pointer to remove card
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glUnregisterBus(
    remove_card pfRemove
    )
{
    ASSERT(pfRemove);
    pfRemove();

    /* TODO: eHPI uninitialization */

    return;
} /* end of glUnregisterBus() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function stores hif related info, which is initialized before.
*
* \param[in] prGlueInfo Pointer to glue info structure
* \param[in] u4Cookie   Pointer to UINT_32 memory base variable for _HIF_HPI
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glSetHifInfo (
    P_GLUE_INFO_T prGlueInfo,
    UINT_32 u4Cookie
    )
{
    P_GL_HIF_INFO_T prHif = NULL;
    ASSERT(prGlueInfo);

    prHif = &prGlueInfo->rHifInfo;
    
    /* fill some buffered information into prHif */
    prHif->mcr_addr_base = mt5931_mcr_base + EHPI_OFFSET_ADDR;
    prHif->mcr_data_base = mt5931_mcr_base + EHPI_OFFSET_DATA;

    return;
} /* end of glSetHifInfo() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function clears hif related info.
*
* \param[in] prGlueInfo Pointer to glue info structure
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glClearHifInfo (
    P_GLUE_INFO_T prGlueInfo
    )
{
    P_GL_HIF_INFO_T prHif = NULL;
    ASSERT(prGlueInfo);
    
    prHif = &prGlueInfo->rHifInfo;

    /* do something */
    prHif->mcr_addr_base = 0;
    prHif->mcr_data_base = 0;

    return;
} /* end of glClearHifInfo() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Initialize bus operation and hif related information, request resources.
*
* \param[out] pvData    A pointer to HIF-specific data type buffer.
*                       For eHPI, pvData is a pointer to UINT_32 type and stores a
*                       mapped base address.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOL
glBusInit (
    PVOID pvData
    )
{
#if DBG
    initTrig();
#endif

    /* 1. initialize eHPI control registers */
    collibri_ehpi_reg_init();

    /* 2. memory remapping for MT5931 */
    mt5931_ehpi_reg_init();

    return TRUE;
};


/*----------------------------------------------------------------------------*/
/*!
* \brief Stop bus operation and release resources.
*
* \param[in] pvData A pointer to struct net_device.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glBusRelease (
    PVOID pvData
    )
{
    /* 1. memory unmapping for MT5931 */
    mt5931_ehpi_reg_uninit();

    /* 2. uninitialize eHPI control registers */
    collibri_ehpi_reg_uninit();

    return;
} /* end of glBusRelease() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Setup bus interrupt operation and interrupt handler for os.
*
* \param[in] pvData     A pointer to struct net_device.
* \param[in] pfnIsr     A pointer to interrupt handler function.
* \param[in] pvCookie   Private data for pfnIsr function.
*
* \retval WLAN_STATUS_SUCCESS   if success
*         NEGATIVE_VALUE   if fail
*/
/*----------------------------------------------------------------------------*/
INT_32
glBusSetIrq (
    PVOID pvData,
    PVOID pfnIsr,
    PVOID pvCookie
    )
{
    struct net_device *pDev = (struct net_device *)pvData;
    int i4Status = 0;

    /* 1. enable GPIO pin as IRQ */
    busSetIrq();

    /* 2. Specify IRQ number into net_device */
    pDev->irq = WLAN_STA_IRQ; 

    /* 3. register ISR callback */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 12)
    i4Status = request_irq(pDev->irq,
            glEhpiInterruptHandler,
            SA_SHIRQ,
            pDev->name,
            pvCookie);
#else
    i4Status = request_irq(pDev->irq,
            glEhpiInterruptHandler,
            IRQF_DISABLED | IRQF_SHARED | IRQF_TRIGGER_FALLING,
            pDev->name,
            pvCookie);
#endif

    if(i4Status < 0) {
        printk("request_irq(%d) failed\n", pDev->irq);
    }
    else {
        printk(KERN_INFO "request_irq(%d) success with dev_id(%x)\n", pDev->irq, (unsigned int)pvCookie);
    }
    
    return i4Status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Stop bus interrupt operation and disable interrupt handling for os.
*
* \param[in] pvData     A pointer to struct net_device.
* \param[in] pvCookie   Private data for pfnIsr function.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glBusFreeIrq (
    PVOID pvData,
    PVOID pvCookie
    )
{
    struct net_device *prDev = (struct net_device *)pvData;
    
    if(!prDev) {
        printk( KERN_INFO "Invalid net_device context.\n");
        return;
    }

    if (prDev->irq) {
        disable_irq(prDev->irq);
        free_irq(prDev->irq, pvCookie);
        prDev->irq = 0;
    }

    busFreeIrq();

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Set power state
*
* \param[in] pvGlueInfo     A pointer to GLUE_INFO_T
* \param[in] ePowerMode     Power Mode Setting
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
glSetPowerState (
    IN P_GLUE_INFO_T  prGlueInfo,
    IN UINT_32 ePowerMode
    )
{
    return;
}


#if DBG
/*----------------------------------------------------------------------------*/
/*!
* \brief Setup the GPIO pin.
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
void
setTrig (void)
{
    GPSR1 = (0x1UL << 8);
} /* end of setTrig() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Clear the GPIO pin.
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
void
clearTrig (void)
{
    GPCR1 = (0x1UL << 8);
} /* end of clearTrig() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Set a specified GPIO pin to H or L.
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static void
initTrig (void)
{
    set_GPIO_mode(GPIO40_FFDTR | GPIO_OUT);
    clearTrig();
}/* end of initTrig() */
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief This function congifure platform-dependent interupt triger type.
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
void
busSetIrq(void)
{
#if defined(WLAN_STA_IRQ_GPIO)
    pxa_gpio_mode(WLAN_STA_IRQ_GPIO | GPIO_IN);
    set_irq_type(WLAN_STA_IRQ, IRQT_FALLING);
#endif
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function should restore settings changed by busSetIrq().
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
void
busFreeIrq(void)
{
#if defined(WLAN_STA_IRQ_GPIO)
    pxa_gpio_mode(WLAN_STA_IRQ_GPIO | GPIO_OUT);
#endif
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function configures colibri memory controller registers 
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static VOID
collibri_ehpi_reg_init(
    VOID
    )
{
    UINT_32 u4RegValue;

    /* 1. enable nCS as memory controller */
    pxa_gpio_mode(GPIO80_nCS_4_MD);

    /* 2. nCS<4> configuration */
    u4RegValue = MSC2;
    u4RegValue &= ~MSC_CS(4, 0xFFFF);
    u4RegValue |= EHPI_CONFIG;
    MSC2 = u4RegValue;

    printk(KERN_INFO "EHPI new MSC2:0x%08x\n", MSC2);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function restores colibri memory controller registers 
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static VOID
collibri_ehpi_reg_uninit(
    VOID
    )
{
    UINT_32 u4RegValue;

    /* 1. restore nCS<4> configuration */
    u4RegValue = MSC2;
    u4RegValue &= ~MSC_CS(4, 0xFFFF);
    MSC2 = u4RegValue;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function configures MT5931 mapped registers on colibri
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static VOID
mt5931_ehpi_reg_init(
    VOID
    )
{
    struct resource *reso = NULL;

    /* 1. request memory regioin */
    reso = request_mem_region((unsigned long)MEM_MAPPED_ADDR, (unsigned long)MEM_MAPPED_LEN, (char *)MODULE_PREFIX);
    if(!reso) {
        printk(KERN_ERR "request_mem_region(0x%08X) failed.\n", MEM_MAPPED_ADDR);
        return;
    }

    /* 2. memory regioin remapping */
    mt5931_mcr_base = ioremap_nocache(MEM_MAPPED_ADDR, MEM_MAPPED_LEN);
    if(!(mt5931_mcr_base)) {
        release_mem_region(MEM_MAPPED_ADDR, MEM_MAPPED_LEN);
        printk(KERN_ERR "ioremap_nocache(0x%08X) failed.\n", MEM_MAPPED_ADDR);
        return;
    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function releases MT5931 mapped registers on colibri
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static VOID
mt5931_ehpi_reg_uninit(
    VOID
    )
{
    iounmap(mt5931_mcr_base);
    mt5931_mcr_base = NULL;

    release_mem_region(MEM_MAPPED_ADDR, MEM_MAPPED_LEN);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Callback for interrupt coming from device
*
* \return N/A
*/
/*----------------------------------------------------------------------------*/
static irqreturn_t 
glEhpiInterruptHandler(
    int irq, 
    void *dev_id
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T) dev_id;

    ASSERT(prGlueInfo);

    if (!prGlueInfo) {
        return IRQ_HANDLED;
    }

    /* 1. Running for ISR */
    wlanISR(prGlueInfo->prAdapter, TRUE);

    /* 1.1 Halt flag Checking */
    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        return IRQ_HANDLED;
    }

    /* 2. Flag marking for interrupt */
    set_bit (GLUE_FLAG_INT_BIT, &prGlueInfo->u4Flag);

    /* 3. wake up tx service thread */
    wake_up_interruptible(&prGlueInfo->waitq);

    return IRQ_HANDLED;
}



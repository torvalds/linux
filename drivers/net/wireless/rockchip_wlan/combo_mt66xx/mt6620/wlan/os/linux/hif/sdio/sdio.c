/******************************************************************************
*[File]             sdio.c
*[Version]          v1.0
*[Revision Date]    2010-03-01
*[Author]
*[Description]
*    The program provides SDIO HIF driver
*[Copyright]
*    Copyright (C) 2010 MediaTek Incorporation. All Rights Reserved.
******************************************************************************/



/*
** $Log: sdio.c $
 *
 * 04 12 2012 terry.wu
 * NULL
 * Add AEE message support
 * 1) Show AEE warning(red screen) if SDIO access error occurs

 *
 * 02 14 2012 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * include correct header file upon setting.
 *
 * 11 10 2011 cp.wu
 * [WCXRP00001098] [MT6620 Wi-Fi][Driver] Replace printk by DBG LOG macros in linux porting layer
 * 1. eliminaite direct calls to printk in porting layer.
 * 2. replaced by DBGLOG, which would be XLOG on ALPS platforms.
 *
 * 09 20 2011 cp.wu
 * [WCXRP00000994] [MT6620 Wi-Fi][Driver] dump message for bus error and reset bus error flag while re-initialized
 * 1. always show error message for SDIO bus errors.
 * 2. reset bus error flag when re-initialization
 *
 * 08 17 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628 related definitions for Linux/Android driver.
 *
 * 05 18 2011 cp.wu
 * [WCXRP00000702] [MT5931][Driver] Modify initialization sequence for E1 ASIC
 * add device ID for MT5931.
 *
 * 04 08 2011 pat.lu
 * [WCXRP00000623] [MT6620 Wi-Fi][Driver] use ARCH define to distinguish PC Linux driver
 * Use CONFIG_X86 instead of PC_LINUX_DRIVER_USE option to have proper compile settting for PC Linux driver
 *
 * 03 22 2011 pat.lu
 * [WCXRP00000592] [MT6620 Wi-Fi][Driver] Support PC Linux Environment Driver Build
 * Add a compiler option "PC_LINUX_DRIVER_USE" for building driver in PC Linux environment.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK.
 *
 * 03 15 2011 cp.wu
 * [WCXRP00000559] [MT6620 Wi-Fi][Driver] Combine TX/RX DMA buffers into a single one to reduce physically continuous memory consumption
 * 1. deprecate CFG_HANDLE_IST_IN_SDIO_CALLBACK
 * 2. Use common coalescing buffer for both TX/RX directions
 *
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 11 15 2010 jeffrey.chang
 * [WCXRP00000181] [MT6620 Wi-Fi][Driver] fix the driver message "GLUE_FLAG_HALT skip INT" during unloading
 * Fix GLUE_FALG_HALT message which cause driver to hang
 *
 * 11 08 2010 cp.wu
 * [WCXRP00000166] [MT6620 Wi-Fi][Driver] use SDIO CMD52 for enabling/disabling interrupt to reduce transaction period
 * correct typo
 *
 * 11 08 2010 cp.wu
 * [WCXRP00000166] [MT6620 Wi-Fi][Driver] use SDIO CMD52 for enabling/disabling interrupt to reduce transaction period
 * change to use CMD52 for enabling/disabling interrupt to reduce SDIO transaction time
 *
 * 11 01 2010 yarco.yang
 * [WCXRP00000149] [MT6620 WI-Fi][Driver]Fine tune performance on MT6516 platform
 * Add code to run WlanIST in SDIO callback.
 *
 * 10 19 2010 cp.wu
 * [WCXRP00000122] [MT6620 Wi-Fi][Driver] Preparation for YuSu source tree integration
 * remove HIF_SDIO_ONE flags because the settings could be merged for runtime detection instead of compile-time.
 *
 * 10 19 2010 jeffrey.chang
 * [WCXRP00000120] [MT6620 Wi-Fi][Driver] Refine linux kernel module to the license of MTK propietary and enable MTK HIF by default
 * Refine linux kernel module to the license of MTK and enable MTK HIF
 *
 * 08 21 2010 jeffrey.chang
 * NULL
 * 1) add sdio two setting
 * 2) bug fix of sdio glue
 *
 * 08 18 2010 jeffrey.chang
 * NULL
 * support multi-function sdio
 *
 * 08 18 2010 cp.wu
 * NULL
 * #if defined(__X86__) is not working, change to use #ifdef CONFIG_X86.
 *
 * 08 17 2010 cp.wu
 * NULL
 * add ENE SDIO host workaround for x86 linux platform.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 07 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Fix hotplug bug
 *
 * 03 28 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * clear sdio interrupt
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "gl_os.h"

#if MTK_WCN_HIF_SDIO
#include "hif_sdio.h"
#else
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h> /* sdio_readl(), etc */
#include <linux/mmc/sdio_ids.h>
#endif

#include <linux/mm.h>
#ifdef CONFIG_ARM
#include <asm/memory.h>
#endif

#if defined(MT6620)
    #include "mt6620_reg.h"
#elif defined(MT5931)
    #include "mt5931_reg.h"
#elif defined(MT6628)
    #include "mt6628_reg.h"
#endif

#if CFG_DBG_GPIO_PINS/* FIXME: move to platform or custom header */
#include <mach/mt6516_gpio.h>
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define HIF_SDIO_ERR_TITLE_STR              "["CHIP_NAME"] SDIO Access Error!"
#define HIF_SDIO_ERR_DESC_STR               "**SDIO Access Error**\n"

#if MTK_WCN_HIF_SDIO


/*
 * function prototypes
 *
 */

static INT32
mtk_sdio_probe(MTK_WCN_HIF_SDIO_CLTCTX, const MTK_WCN_HIF_SDIO_FUNCINFO *);

static INT32
mtk_sdio_remove(MTK_WCN_HIF_SDIO_CLTCTX);
static INT32 mtk_sdio_interrupt(MTK_WCN_HIF_SDIO_CLTCTX);

/*
 * sdio function info table
 */

static MTK_WCN_HIF_SDIO_FUNCINFO funcInfo[] = {
#if defined(MT6620)
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x020a, 0x1, 512) },
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x020c, 0x2, 512) },
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x018a, 0x1, 512) },
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x018c, 0x2, 512) },
#elif defined(MT5931)
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x5931, 0x1, 512) },
#elif defined(MT6628)
    { MTK_WCN_HIF_SDIO_FUNC(0x037a, 0x6628, 0x1, 512) },
#endif
};


static MTK_WCN_HIF_SDIO_CLTINFO cltInfo = {
    .func_tbl = funcInfo,
    .func_tbl_size = sizeof(funcInfo)/sizeof(MTK_WCN_HIF_SDIO_FUNCINFO),
    .hif_clt_probe = mtk_sdio_probe,
    .hif_clt_remove = mtk_sdio_remove,
    .hif_clt_irq = mtk_sdio_interrupt,
};

#else

static const struct sdio_device_id mtk_sdio_ids[] = {
#if defined(MT6620)
	{ SDIO_DEVICE(0x037a, 0x020a) }, /* Not an SDIO standard class device */
	{ SDIO_DEVICE(0x037a, 0x020b) }, /* Not an SDIO standard class device */
	{ SDIO_DEVICE(0x037a, 0x020c) }, /* Not an SDIO standard class device */
#elif defined(MT5931)
	{ SDIO_DEVICE(0x037a, 0x5931) }, /* Not an SDIO standard class device */
#elif defined(MT6628)
	{ SDIO_DEVICE(0x037a, 0x6628) }, /* Not an SDIO standard class device */
#endif
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(sdio, mtk_sdio_ids);

#endif

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
static probe_card pfWlanProbe = NULL;
static remove_card pfWlanRemove = NULL;


#if (MTK_WCN_HIF_SDIO == 0)
static struct sdio_driver mtk_sdio_driver = {
	.name		= "wlan", /* "MTK SDIO WLAN Driver" */
	.id_table	= mtk_sdio_ids,
	.probe		= NULL,
	.remove		= NULL,
};
#endif


#if CFG_DBG_GPIO_PINS

/* debug pins */
UINT_32 dbgPinSTP[] = {
    GPIO_PLATFORM(33)/* CMFLASH, IDX_ERR J613 */
    , GPIO_PLATFORM(62)/* EINT3, IDX_TX_THREAD */
    , GPIO_PLATFORM(80)/* SPI_CS_N, IDX_TX_REQ J613 */
    , GPIO_PLATFORM(81)/* SPI_SCK, IDX_TX_PORT_WRITE J613 */
    , GPIO_PLATFORM(17) /* CMRST, IDX_STP_MTX_BT J618 */
    , GPIO_PLATFORM(18) /* CMPDN, IDX_STP_MTX_FM J613 */
    , GPIO_PLATFORM(19) /* CMVREF,IDX_STP_MTX_GPS J613 */
    , GPIO_INVALID /* REMOVED, IDX_STP_MTX_WIFI */
    , GPIO_INVALID /* REMOVED, IDX_STP_MTX_WMT */
    , GPIO_PLATFORM(135) /* SCL2, IDX_LOOP_CNT  J616 */
    , GPIO_PLATFORM(136) /* SDA2, IDX_NO_BUF J616 */
    , GPIO_PLATFORM(30) /* CAM_MECHSH0, IDX_BT_TX, J613 low-active */
    , GPIO_PLATFORM(31) /* CAM_MECHSH1, IDX_BT_RX, J613 low-active */
    , GPIO_PLATFORM(124) /* GPS_PWR_EN, ThreadDSPIn [GPS] */
    , GPIO_PLATFORM(125) /* GPS_SYNC, mtk_sys_msg_recv [GPS] */
    , GPIO_PLATFORM(21) /* GPS_EINT8, dump_nmea_data [GPS] */
    , GPIO_PLATFORM(29) /* CAM_STROBE, IDX_GPS_TX, J613 low-active */
    , GPIO_PLATFORM(20) /*CMHREF, J613 UNUSED */
//    , GPIO_6516(64) /* EINT5, REMOVED!!! for MT6620-Wi-Fi Int */
//    , GPIO_6516(122) /* BT_PWR_EN, REMOVED!!! for MT6620-PMU_EN */
//    , GPIO_6516(123) /* BT_RESET, REMOVED!!! for MT6620-RST */
};
#endif
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
#if CFG_DBG_GPIO_PINS
void debug_gpio_init(void)
{
    int i;

    for (i = 0; i < sizeof(dbgPinSTP)/sizeof(dbgPinSTP[0]); ++i) {
        if (GPIO_INVALID == dbgPinSTP[i]) {
            continue;
        }
        //printk(KERN_INFO "[%s] %ld \n", __FUNCTION__, dbgPinSTP[i]);
        mt_set_gpio_pull_enable(dbgPinSTP[i], 0); /* disable pull */
        mt_set_gpio_dir(dbgPinSTP[i], GPIO_DIR_OUT); /* set output */
        mt_set_gpio_mode(dbgPinSTP[i], GPIO_MODE_00); /* set gpio mode */

        /* toggle twice to check if ok: */
        mt_set_gpio_out(dbgPinSTP[i], GPIO_OUT_ZERO); /* tie low */
        mt_set_gpio_out(dbgPinSTP[i], GPIO_OUT_ONE); /* tie high*/
        mt_set_gpio_out(dbgPinSTP[i], GPIO_OUT_ZERO); /* tie low */
        mt_set_gpio_out(dbgPinSTP[i], GPIO_OUT_ONE); /* tie high*/
    }
    //printk(KERN_INFO "[%s] initialization ok \n", __FUNCTION__);
}

void debug_gpio_deinit(void)
{
    int i;
    for (i = 0; i < sizeof(dbgPinSTP)/sizeof(dbgPinSTP[0]); ++i) {
        if (GPIO_INVALID == dbgPinSTP[i]) {
            continue;
        }
        //printk(KERN_INFO "[%s] %ld \n", __FUNCTION__, dbgPinSTP[i]);
        mt_set_gpio_dir(dbgPinSTP[i], GPIO_DIR_IN);
    }

    //printk(KERN_INFO "[%s] k\n", __FUNCTION__);
}

void mtk_wcn_stp_debug_gpio_assert(UINT_32 dwIndex, UINT_32 dwMethod)
{
    unsigned int i;

    if (dwIndex >= (sizeof(dbgPinSTP)/sizeof(dbgPinSTP[0]))) {
        //printk(KERN_INFO "[%s] invalid dwIndex(%ld) \n", __FUNCTION__, dwIndex);
        return;
    }

    if (dwIndex > IDX_STP_MAX) {
        //printk(KERN_INFO "[%s] dwIndex(%ld) > IDX_STP_MAX(%d) \n", __FUNCTION__, dwIndex, IDX_STP_MAX);
    }

    if (GPIO_INVALID == dbgPinSTP[dwIndex]) {
        return;
    }

    if (dwMethod & DBG_TIE_DIR) {
        if (dwMethod & DBG_HIGH) {
            mt_set_gpio_out(dbgPinSTP[dwIndex], GPIO_OUT_ONE);
        }
        else {
            mt_set_gpio_out(dbgPinSTP[dwIndex], GPIO_OUT_ZERO);
        }
        return;
    }

    if (dwMethod & DBG_TOGGLE(0)) {
        for (i = 0; i < DBG_TOGGLE_NUM(dwMethod); ++i) {
            mt_set_gpio_out(dbgPinSTP[dwIndex], GPIO_OUT_ZERO);
            mt_set_gpio_out(dbgPinSTP[dwIndex], GPIO_OUT_ONE);
        }
        return;
    }

    return;
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a SDIO interrupt callback function
*
* \param[in] func  pointer to SDIO handle
*
* \return void
*/
/*----------------------------------------------------------------------------*/

#if MTK_WCN_HIF_SDIO

static INT32 mtk_sdio_interrupt(MTK_WCN_HIF_SDIO_CLTCTX cltCtx)
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    INT32 ret = 0;

    prGlueInfo = mtk_wcn_hif_sdio_get_drvdata(cltCtx);

    ASSERT(prGlueInfo);

    if (!prGlueInfo) {
        //printk(KERN_INFO DRV_NAME"No glue info in mtk_sdio_interrupt()\n");
        return (-HIF_SDIO_ERR_FAIL);
    }

    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        //printk(KERN_INFO DRV_NAME"GLUE_FLAG_HALT skip INT\n");
        ret = mtk_wcn_hif_sdio_writel(cltCtx, MCR_WHLPCR, WHLPCR_INT_EN_CLR);
        return ret;
    }

    ret = mtk_wcn_hif_sdio_writel(cltCtx, MCR_WHLPCR, WHLPCR_INT_EN_CLR);

    set_bit (GLUE_FLAG_INT_BIT, &prGlueInfo->u4Flag);

    /* when we got sdio interrupt, we wake up the tx servie thread*/
    wake_up_interruptible(&prGlueInfo->waitq);

    return ret;
}

#else

static unsigned int in_interrupt = 0;

static void mtk_sdio_interrupt(struct sdio_func *func)
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    int ret = 0;

    prGlueInfo = sdio_get_drvdata(func);
    ASSERT(prGlueInfo);

    if (!prGlueInfo) {
        //printk(KERN_INFO DRV_NAME"No glue info in mtk_sdio_interrupt()\n");
        return;
    }

    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        sdio_writel(prGlueInfo->rHifInfo.func, WHLPCR_INT_EN_CLR, MCR_WHLPCR, &ret);
        //printk(KERN_INFO DRV_NAME"GLUE_FLAG_HALT skip INT\n");
        return;
    }

    sdio_writel(prGlueInfo->rHifInfo.func, WHLPCR_INT_EN_CLR, MCR_WHLPCR, &ret);

    #if 0
    wlanISR(prGlueInfo->prAdapter, TRUE);

    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        /* Should stop now... skip pending interrupt */
        //printk(KERN_INFO DRV_NAME"ignore pending interrupt\n");
    }
    else {
        wlanIST(prGlueInfo->prAdapter);
    }
    #endif

    set_bit (GLUE_FLAG_INT_BIT, &prGlueInfo->u4Flag);

    /* when we got sdio interrupt, we wake up the tx servie thread*/
    wake_up_interruptible(&prGlueInfo->waitq);
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a SDIO probe function
*
* \param[in] func   pointer to SDIO handle
* \param[in] id     pointer to SDIO device id table
*
* \return void
*/
/*----------------------------------------------------------------------------*/

#if MTK_WCN_HIF_SDIO

// FIXME: global variable
static const MTK_WCN_HIF_SDIO_FUNCINFO *prFunc;


static INT32
mtk_sdio_probe(MTK_WCN_HIF_SDIO_CLTCTX cltCtx, const MTK_WCN_HIF_SDIO_FUNCINFO *prFuncInfo)
{
    INT32 ret = HIF_SDIO_ERR_SUCCESS;

    prFunc = prFuncInfo;

    if (pfWlanProbe((PVOID) &cltCtx) != WLAN_STATUS_SUCCESS) {
        //printk(KERN_WARNING DRV_NAME"pfWlanProbe fail!call pfWlanRemove()\n");
        pfWlanRemove();
        ret = -(HIF_SDIO_ERR_FAIL);
    } else {
        //printk(KERN_INFO DRV_NAME"mtk_wifi_sdio_probe() done(%d)\n", ret);
    }
    return ret;
}
#else
static int mtk_sdio_probe (
    struct sdio_func *func,
    const struct sdio_device_id *id
    )
{
    int ret = 0;
    int i = 0;

    //printk(KERN_INFO DRV_NAME "mtk_sdio_probe()\n");

    ASSERT(func);
    ASSERT(id);

    //printk(KERN_INFO DRV_NAME "Basic struct size checking...\n");
    //printk(KERN_INFO DRV_NAME "sizeof(struct device) = %d\n", sizeof(struct device));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_host) = %d\n", sizeof(struct mmc_host));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_card) = %d\n", sizeof(struct mmc_card));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_driver) = %d\n", sizeof(struct mmc_driver));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_data) = %d\n", sizeof(struct mmc_data));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_command) = %d\n", sizeof(struct mmc_command));
    //printk(KERN_INFO DRV_NAME "sizeof(struct mmc_request) = %d\n", sizeof(struct mmc_request));
    //printk(KERN_INFO DRV_NAME "sizeof(struct sdio_func) = %d\n", sizeof(struct sdio_func));

    //printk(KERN_INFO DRV_NAME "Card information checking...\n");
    //printk(KERN_INFO DRV_NAME "func = 0x%p\n", func);
    //printk(KERN_INFO DRV_NAME "Number of info = %d:\n", func->card->num_info);

    for (i = 0; i < func->card->num_info; i++) {
        //printk(KERN_INFO DRV_NAME "info[%d]: %s\n", i, func->card->info[i]);
    }

    sdio_claim_host(func);
    ret = sdio_enable_func(func);
    sdio_release_host(func);

    if (ret) {
        //printk(KERN_INFO DRV_NAME"sdio_enable_func failed!\n");
        goto out;
    }
    //printk(KERN_INFO DRV_NAME"sdio_enable_func done!\n");

    if (pfWlanProbe((PVOID)func) != WLAN_STATUS_SUCCESS) {
        //printk(KERN_WARNING DRV_NAME"pfWlanProbe fail!call pfWlanRemove()\n");
        pfWlanRemove();
        ret = -1;
    }
    else {
#if CFG_DBG_GPIO_PINS
    //printk(KERN_INFO "[%s] init debug gpio, 20100815 \n", __FUNCTION__);
    /* Debug pins initialization */
    debug_gpio_init();
#endif
    }

out:
    //printk(KERN_INFO DRV_NAME"mtk_sdio_probe() done(%d)\n", ret);
    return ret;
}
#endif


#if MTK_WCN_HIF_SDIO
static INT32
mtk_sdio_remove(MTK_WCN_HIF_SDIO_CLTCTX cltCtx)
{
    INT32 ret = HIF_SDIO_ERR_SUCCESS;
    //printk(KERN_INFO DRV_NAME"pfWlanRemove done\n");
    pfWlanRemove();

    return ret;
}
#else
static void
mtk_sdio_remove (
    struct sdio_func *func
    )
{
    //printk(KERN_INFO DRV_NAME"mtk_sdio_remove()\n");

#if CFG_DBG_GPIO_PINS
    //printk(KERN_INFO "[%s] deinit debug gpio \n", __FUNCTION__);
    debug_gpio_deinit();
#endif

    ASSERT(func);
    //printk(KERN_INFO DRV_NAME"pfWlanRemove done\n");
    pfWlanRemove();

    sdio_claim_host(func);
    sdio_disable_func(func);
    //printk(KERN_INFO DRV_NAME"sdio_disable_func() done\n");
    sdio_release_host(func);

    //printk(KERN_INFO DRV_NAME"mtk_sdio_remove() done\n");
}
#endif

#if (MTK_WCN_HIF_SDIO == 0)
static int
mtk_sdio_suspend (
    struct device * pDev,
    pm_message_t state
    )
{
    //printk(KERN_INFO "mtk_sdio: mtk_sdio_suspend dev(0x%p)\n", pDev);
    //printk(KERN_INFO "mtk_sdio: MediaTek SDIO WLAN driver\n");

    return 0;
}

int mtk_sdio_resume (
    struct device * pDev
    )
{
    //printk(KERN_INFO "mtk_sdio: mtk_sdio_resume dev(0x%p)\n", pDev);

    return 0;
}
#endif


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
    int ret = 0;

    ASSERT(pfProbe);
    ASSERT(pfRemove);

    //printk(KERN_INFO "mtk_sdio: MediaTek SDIO WLAN driver\n");
    //printk(KERN_INFO "mtk_sdio: Copyright MediaTek Inc.\n");

    pfWlanProbe = pfProbe;
    pfWlanRemove = pfRemove;

#if MTK_WCN_HIF_SDIO
    /* register MTK sdio client */
    ret = ((mtk_wcn_hif_sdio_client_reg(&cltInfo) == HIF_SDIO_ERR_SUCCESS) ? WLAN_STATUS_SUCCESS : WLAN_STATUS_FAILURE);
#else
    mtk_sdio_driver.probe = mtk_sdio_probe;
    mtk_sdio_driver.remove = mtk_sdio_remove;

    mtk_sdio_driver.drv.suspend = mtk_sdio_suspend;
    mtk_sdio_driver.drv.resume = mtk_sdio_resume;

    ret = (sdio_register_driver(&mtk_sdio_driver) == 0) ? WLAN_STATUS_SUCCESS : WLAN_STATUS_FAILURE;
#endif

    return ret;
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

#if MTK_WCN_HIF_SDIO
    /* unregister MTK sdio client */
    mtk_wcn_hif_sdio_client_unreg(&cltInfo);
#else
    sdio_unregister_driver(&mtk_sdio_driver);
#endif

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

    prHif = &prGlueInfo->rHifInfo;

#if MTK_WCN_HIF_SDIO
    //prHif->prFuncInfo = ((MTK_WCN_HIF_SDIO_FUNCINFO *) u4Cookie);
    prHif->prFuncInfo = prFunc;
    prHif->cltCtx = *((MTK_WCN_HIF_SDIO_CLTCTX *) u4Cookie);
    mtk_wcn_hif_sdio_set_drvdata(prHif->cltCtx, prGlueInfo);

#else
    prHif->func = (struct sdio_func *) u4Cookie;

    //printk(KERN_INFO DRV_NAME"prHif->func->dev = 0x%p\n", &prHif->func->dev);
    //printk(KERN_INFO DRV_NAME"prHif->func->vendor = 0x%04X\n", prHif->func->vendor);
    //printk(KERN_INFO DRV_NAME"prHif->func->device = 0x%04X\n", prHif->func->device);
    //printk(KERN_INFO DRV_NAME"prHif->func->func = 0x%04X\n", prHif->func->num);

    sdio_set_drvdata(prHif->func, prGlueInfo);

    SET_NETDEV_DEV(prGlueInfo->prDevHandler, &prHif->func->dev);
#endif

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
    //P_GL_HIF_INFO_T prHif = NULL;
    //ASSERT(prGlueInfo);
    //prHif = &prGlueInfo->rHifInfo;

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
#if (MTK_WCN_HIF_SDIO == 0)
    int ret = 0;
    struct sdio_func *func = NULL;

    ASSERT(pvData);

    func = (struct sdio_func *) pvData;

    sdio_claim_host(func);
    ret = sdio_set_block_size(func, 512);
    sdio_release_host(func);

    if (ret) {
        //printk(KERN_INFO DRV_NAME"sdio_set_block_size 512 failed!\n");
    }
    else {
        //printk(KERN_INFO DRV_NAME"sdio_set_block_size 512 done!\n");
    }

    //printk(KERN_INFO DRV_NAME"param: func->cur_blksize(%d)\n", func->cur_blksize);
    //printk(KERN_INFO DRV_NAME"param: func->max_blksize(%d)\n", func->max_blksize);
    //printk(KERN_INFO DRV_NAME"param: func->card->host->max_blk_size(%d)\n", func->card->host->max_blk_size);
    //printk(KERN_INFO DRV_NAME"param: func->card->host->max_blk_count(%d)\n", func->card->host->max_blk_count);
#endif
    return TRUE;
} /* end of glBusInit() */


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
    int ret = 0;

#if (MTK_WCN_HIF_SDIO == 0)
    struct net_device *prNetDevice = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_GL_HIF_INFO_T prHifInfo = NULL;

    ASSERT(pvData);
    if (!pvData) {
        return -1;
    }
    prNetDevice = (struct net_device *) pvData;
    prGlueInfo = (P_GLUE_INFO_T) pvCookie;
    ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        return -1;
    }

    prHifInfo = &prGlueInfo->rHifInfo;

    sdio_claim_host(prHifInfo->func);
    ret = sdio_claim_irq(prHifInfo->func, mtk_sdio_interrupt);
    sdio_release_host(prHifInfo->func);
#else
		/* hif_sdio case */
		struct net_device *prNetDevice = NULL;
		P_GLUE_INFO_T prGlueInfo = NULL;
	
		ASSERT(pvData);
		if (!pvData) {
			return -1;
		}
		prNetDevice = (struct net_device *) pvData;
	
		prGlueInfo = (P_GLUE_INFO_T) pvCookie;
		ASSERT(prGlueInfo);
		if (!prGlueInfo) {
			return -1;
		}
	
		mtk_wcn_hif_sdio_enable_irq(prGlueInfo->rHifInfo.cltCtx, MTK_WCN_BOOL_TRUE);

#endif
    return ret;
} /* end of glBusSetIrq() */


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
#if (MTK_WCN_HIF_SDIO == 0)
    struct net_device *prNetDevice = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_GL_HIF_INFO_T prHifInfo = NULL;
    int ret = 0;

    ASSERT(pvData);
    if (!pvData) {
        //printk(KERN_INFO DRV_NAME"%s null pvData\n", __FUNCTION__);
        return;
    }
    prNetDevice = (struct net_device *) pvData;
    prGlueInfo = (P_GLUE_INFO_T) pvCookie;
    ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        //printk(KERN_INFO DRV_NAME"%s no glue info\n", __FUNCTION__);
        return;
    }

    prHifInfo = &prGlueInfo->rHifInfo;

    sdio_claim_host(prHifInfo->func);
    ret = sdio_release_irq(prHifInfo->func);
    sdio_release_host(prHifInfo->func);
#else
		/* hif_sdio case */
		struct net_device *prNetDevice = NULL;
		P_GLUE_INFO_T prGlueInfo = NULL;
	
		ASSERT(pvData);
		if (!pvData) {
			//printk(KERN_INFO DRV_NAME"%s null pvData\n", __FUNCTION__);
			return;
		}
		prNetDevice = (struct net_device *) pvData;
	
		prGlueInfo = (P_GLUE_INFO_T) pvCookie;
		ASSERT(prGlueInfo);
		if (!prGlueInfo) {
			//printk(KERN_INFO DRV_NAME"%s no glue info\n", __FUNCTION__);
			return;
		}
	
		mtk_wcn_hif_sdio_enable_irq(prGlueInfo->rHifInfo.cltCtx, MTK_WCN_BOOL_FALSE);

#endif

    return;
} /* end of glBusreeIrq() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Read a 32-bit device register
*
* \param[in] prGlueInfo Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register Register offset
* \param[in] pu4Value   Pointer to variable used to store read value
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevRegRead (
    IN  P_GLUE_INFO_T   prGlueInfo,
    IN  UINT_32         u4Register,
    OUT PUINT_32        pu4Value
    )
{
    int ret = 0;

    ASSERT(prGlueInfo);
    ASSERT(pu4Value);

#if MTK_WCN_HIF_SDIO
    ret = mtk_wcn_hif_sdio_readl(prGlueInfo->rHifInfo.cltCtx, u4Register, (PUINT32) pu4Value);
#else
    if (!in_interrupt) {
        sdio_claim_host(prGlueInfo->rHifInfo.func);
    }

    *pu4Value = sdio_readl(prGlueInfo->rHifInfo.func, u4Register, &ret);

    if (!in_interrupt) {
        sdio_release_host(prGlueInfo->rHifInfo.func);
    }
#endif

    if (ret) {
        kalSendAeeWarning(HIF_SDIO_ERR_TITLE_STR, HIF_SDIO_ERR_DESC_STR "sdio_readl() reports error: %x", ret);
        DBGLOG(HAL, ERROR, ("sdio_readl() reports error: %x", ret));
    }

    return (ret) ? FALSE : TRUE;
} /* end of kalDevRegRead() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Write a 32-bit device register
*
* \param[in] prGlueInfo Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register Register offset
* \param[in] u4Value    Value to be written
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevRegWrite (
    IN P_GLUE_INFO_T  prGlueInfo,
    IN UINT_32        u4Register,
    IN UINT_32        u4Value
    )
{
    int ret = 0;

    ASSERT(prGlueInfo);

#if MTK_WCN_HIF_SDIO
    ret = mtk_wcn_hif_sdio_writel(prGlueInfo->rHifInfo.cltCtx, u4Register, u4Value);
#else
    if (!in_interrupt) {
        sdio_claim_host(prGlueInfo->rHifInfo.func);
    }

    sdio_writel(prGlueInfo->rHifInfo.func, u4Value, u4Register, &ret);

    if (!in_interrupt) {
        sdio_release_host(prGlueInfo->rHifInfo.func);
    }
#endif

    if (ret) {
        kalSendAeeWarning(HIF_SDIO_ERR_TITLE_STR, HIF_SDIO_ERR_DESC_STR "sdio_writel() reports error: %x", ret);
        DBGLOG(HAL, ERROR, ("sdio_writel() reports error: %x", ret));
    }

    return (ret) ? FALSE : TRUE;
} /* end of kalDevRegWrite() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Read device I/O port
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             I/O port offset
* \param[in] u2Len              Length to be read
* \param[out] pucBuf            Pointer to read buffer
* \param[in] u2ValidOutBufSize  Length of the buffer valid to be accessed
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortRead (
    IN  P_GLUE_INFO_T   prGlueInfo,
    IN  UINT_16         u2Port,
    IN  UINT_16         u2Len,
    OUT PUINT_8         pucBuf,
    IN  UINT_16         u2ValidOutBufSize
    )
{
    P_GL_HIF_INFO_T prHifInfo = NULL;
    PUINT_8 pucDst = NULL;
    int count = u2Len;
    int ret = 0;
    int bNum = 0;

#if (MTK_WCN_HIF_SDIO == 0)
    struct sdio_func *prSdioFunc = NULL;
#endif

    #if DBG
    //printk(KERN_INFO DRV_NAME"++kalDevPortRead++ buf:0x%p, port:0x%x, length:%d\n", pucBuf, u2Port, u2Len);
    #endif

    ASSERT(prGlueInfo);
    prHifInfo = &prGlueInfo->rHifInfo;

    ASSERT(pucBuf);
    pucDst = pucBuf;

    ASSERT(u2Len <= u2ValidOutBufSize);

#if (MTK_WCN_HIF_SDIO == 0)
    prSdioFunc = prHifInfo->func;

    ASSERT(prSdioFunc->cur_blksize > 0);

    if (!in_interrupt) {
        sdio_claim_host(prSdioFunc);
    }

    /* Split buffer into multiple single block to workaround hifsys */
    while (count >= prSdioFunc->cur_blksize) {
        count -= prSdioFunc->cur_blksize;
        bNum++;
    }
    if (count > 0 && bNum > 0) {
        bNum++;
    }

    if (bNum > 0) {
        ret = sdio_readsb(prSdioFunc, pucDst, u2Port, prSdioFunc->cur_blksize * bNum);

#ifdef CONFIG_X86
        /* ENE workaround */
        {
            int tmp;
            sdio_writel(prSdioFunc, 0x0, SDIO_X86_WORKAROUND_WRITE_MCR, &tmp);
        }
#endif

    }
    else {
        ret = sdio_readsb(prSdioFunc, pucDst, u2Port, count);
    }

    if (!in_interrupt) {
        sdio_release_host(prSdioFunc);
    }
#else

    /* Split buffer into multiple single block to workaround hifsys */
    while (count >= (prGlueInfo->rHifInfo).prFuncInfo->blk_sz) {
        count -= ((prGlueInfo->rHifInfo).prFuncInfo->blk_sz);
        bNum++;
    }
    if (count > 0 && bNum > 0) {
        bNum++;
    }

    if (bNum > 0) {
        ret = mtk_wcn_hif_sdio_read_buf(prGlueInfo->rHifInfo.cltCtx, u2Port, (PUINT32) pucDst,
                 ((prGlueInfo->rHifInfo).prFuncInfo->blk_sz) * bNum);
    }
    else {
        ret = mtk_wcn_hif_sdio_read_buf(prGlueInfo->rHifInfo.cltCtx, u2Port, (PUINT32) pucDst, (count+3)&~0x3U);
    }
#endif

    if (ret) {
        kalSendAeeWarning(HIF_SDIO_ERR_TITLE_STR, HIF_SDIO_ERR_DESC_STR "sdio_readsb() reports error: %x", ret);
        DBGLOG(HAL, ERROR, ("sdio_readsb() reports error: %x", ret));
    }

    return (ret) ? FALSE : TRUE;
} /* end of kalDevPortRead() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Write device I/O port
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             I/O port offset
* \param[in] u2Len              Length to be write
* \param[in] pucBuf             Pointer to write buffer
* \param[in] u2ValidInBufSize   Length of the buffer valid to be accessed
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortWrite (
    IN P_GLUE_INFO_T  prGlueInfo,
    IN UINT_16        u2Port,
    IN UINT_16        u2Len,
    IN PUINT_8        pucBuf,
    IN UINT_16        u2ValidInBufSize
    )
{
    P_GL_HIF_INFO_T prHifInfo = NULL;
    PUINT_8 pucSrc = NULL;
    int count = u2Len;
    int ret = 0;
    int bNum = 0;

#if (MTK_WCN_HIF_SDIO == 0)
    struct sdio_func *prSdioFunc = NULL;
#endif

    #if DBG
    //printk(KERN_INFO DRV_NAME"++kalDevPortWrite++ buf:0x%p, port:0x%x, length:%d\n", pucBuf, u2Port, u2Len);
    #endif

    ASSERT(prGlueInfo);
    prHifInfo = &prGlueInfo->rHifInfo;

    ASSERT(pucBuf);
    pucSrc = pucBuf;

    ASSERT(u2Len <= u2ValidInBufSize);

#if (MTK_WCN_HIF_SDIO == 0)
    prSdioFunc = prHifInfo->func;
    ASSERT(prSdioFunc->cur_blksize > 0);

    if (!in_interrupt) {
        sdio_claim_host(prSdioFunc);
    }

    /* Split buffer into multiple single block to workaround hifsys */
    while (count >= prSdioFunc->cur_blksize) {
        count -= prSdioFunc->cur_blksize;
        bNum++;
    }
    if (count > 0 && bNum > 0) {
        bNum++;
    }

    if (bNum > 0) { // block mode
        ret = sdio_writesb(prSdioFunc, u2Port, pucSrc, prSdioFunc->cur_blksize * bNum);

#ifdef CONFIG_X86
        /* ENE workaround */
        {
            int tmp;
            sdio_writel(prSdioFunc, 0x0, SDIO_X86_WORKAROUND_WRITE_MCR, &tmp);
        }
#endif

    }
    else {  // byte mode

        ret = sdio_writesb(prSdioFunc, u2Port, pucSrc, count);
    }

    if (!in_interrupt) {
        sdio_release_host(prSdioFunc);
    }
#else
    /* Split buffer into multiple single block to workaround hifsys */
    while (count >= ((prGlueInfo->rHifInfo).prFuncInfo->blk_sz)) {
        count -= ((prGlueInfo->rHifInfo).prFuncInfo->blk_sz);
        bNum++;
    }
    if (count > 0 && bNum > 0) {
        bNum++;
    }

    if (bNum > 0) { // block mode
        ret = mtk_wcn_hif_sdio_write_buf(prGlueInfo->rHifInfo.cltCtx, u2Port, (PUINT32) pucSrc,
                             ((prGlueInfo->rHifInfo).prFuncInfo->blk_sz) * bNum);
    }
    else {  // byte mode
        ret = mtk_wcn_hif_sdio_write_buf(prGlueInfo->rHifInfo.cltCtx, u2Port, (PUINT32) pucSrc, (count+3)&~0x3U);
    }
#endif

    if (ret) {
        kalSendAeeWarning(HIF_SDIO_ERR_TITLE_STR, HIF_SDIO_ERR_DESC_STR "sdio_writesb() reports error: %x", ret);
        DBGLOG(HAL, ERROR, ("sdio_writesb() reports error: %x", ret));
    }

    return (ret) ? FALSE : TRUE;
} /* end of kalDevPortWrite() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Write device I/O port in byte with CMD52
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u4Addr             I/O port offset
* \param[in] ucData             Single byte of data to be written
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
    int ret = 0;

#if (MTK_WCN_HIF_SDIO == 0)
    if (!in_interrupt) {
        sdio_claim_host(prGlueInfo->rHifInfo.func);
    }

    sdio_writeb(prGlueInfo->rHifInfo.func, ucData, u4Addr, &ret);

    if (!in_interrupt) {
        sdio_release_host(prGlueInfo->rHifInfo.func);
    }
#else
    ret = mtk_wcn_hif_sdio_writeb(prGlueInfo->rHifInfo.cltCtx, u4Addr, ucData);
#endif

    if (ret) {
        kalSendAeeWarning(HIF_SDIO_ERR_TITLE_STR, HIF_SDIO_ERR_DESC_STR "sdio_writeb() reports error: %x", ret);
        DBGLOG(HAL, ERROR, ("sdio_writeb() reports error: %x", ret));
    }

    return (ret) ? FALSE : TRUE;

} /* end of kalDevWriteWithSdioCmd52() */


VOID
glSetPowerState (
    IN P_GLUE_INFO_T  prGlueInfo,
    IN UINT_32 ePowerMode
    )
{
    return;
}


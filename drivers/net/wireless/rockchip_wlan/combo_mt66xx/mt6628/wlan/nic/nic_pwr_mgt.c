/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/nic/nic_pwr_mgt.c#1 $
*/

/*! \file   "nic_pwr_mgt.c"
    \brief  In this file we define the STATE and EVENT for Power Management FSM.

    The SCAN FSM is responsible for performing SCAN behavior when the Arbiter enter
    ARB_STATE_SCAN. The STATE and EVENT for SCAN FSM are defined here with detail
    description.
*/



/*
** $Log: nic_pwr_mgt.c $
 *
 * 11 28 2011 cp.wu
 * [WCXRP00001125] [MT6620 Wi-Fi][Firmware] Strengthen Wi-Fi power off sequence to have a clearroom environment when returining to ROM code
 * 1. Due to firmware now stops HIF DMA for powering off, do not try to receive any packet from firmware
 * 2. Take use of prAdapter->fgIsEnterD3ReqIssued for tracking whether it is powering off or not
 *
 * 10 03 2011 cp.wu
 * [WCXRP00001022] [MT6628 Driver][Firmware Download] Add multi section independent download functionality
 * add firmware download path in divided scatters.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * reuse firmware download logic of MT6620 for MT6628.
 *
 * 05 11 2011 cp.wu
 * [WCXRP00000718] [MT6620 Wi-Fi] modify the behavior of setting tx power
 * ACPI APIs migrate to wlan_lib.c for glue layer to invoke.
 *
 * 04 29 2011 cp.wu
 * [WCXRP00000636] [WHQL][MT5931 Driver] 2c_PMHibernate (hang on 2h)
 * fix for compilation error when applied with FW_DOWNLOAD = 0
 *
 * 04 18 2011 cp.wu
 * [WCXRP00000636] [WHQL][MT5931 Driver] 2c_PMHibernate (hang on 2h)
 * 1) add API for glue layer to query ACPI state
 * 2) Windows glue should not access to hardware after switched into D3 state
 *
 * 04 13 2011 cp.wu
 * [WCXRP00000639] [WHQL][MT5931 Driver] 2c_PMStandby test item can not complete
 * refine for MT5931/MT6620 logic separation.
 *
 * 04 13 2011 cp.wu
 * [WCXRP00000639] [WHQL][MT5931 Driver] 2c_PMStandby test item can not complete
 * bugfix: firmware download procedure for ACPI state transition is not complete.
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
 * 12 31 2010 cp.wu
 * [WCXRP00000335] [MT6620 Wi-Fi][Driver] change to use milliseconds sleep instead of delay to avoid blocking to system scheduling
 * change to use msleep() and shorten waiting interval to reduce blocking to other task while Wi-Fi driver is being loaded
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * check success or failure for setting fw-own
 *
 * 12 30 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * host driver not to set FW-own when there is still pending interrupts
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 30 2010 cp.wu
 * NULL
 * reset ACPI power state before waking up MT6620 Wi-Fi firmware.
 *
 * 08 12 2010 cp.wu
 * NULL
 * [AIS-FSM] honor registry setting for adhoc running mode. (A/B/G)
 *
 * 08 03 2010 cp.wu
 * NULL
 * Centralize mgmt/system service procedures into independent calls.
 *
 * 07 22 2010 cp.wu
 *
 * 1) refine AIS-FSM indent.
 * 2) when entering RF Test mode, flush 802.1X frames as well
 * 3) when entering D3 state, flush 802.1X frames as well
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change MAC address updating logic.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) when acquiring LP-own, write for clr-own with lower frequency compared to read poll
 * 2) correct address list parsing
 *
 * 05 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * sleepy notify is only used for sleepy state,
 * while wake-up state is automatically set when host needs to access device
 *
 * 05 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct hibernation problem.
 *
 * 04 26 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) surpress compiler warning
 * 2) when acqruing LP-own, keep writing WHLPCR whenever OWN is not acquired yet
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * when acquiring driver-own, wait for up to 8 seconds.
 *
 * 04 21 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add for private ioctl support
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) remove redundant firmware image unloading
 *  * 2) use compile-time macros to separate logic related to accquiring own
 *
 * 04 16 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * treat BUS access failure as kind of card removal.
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * accessing to firmware load/start address, and access to OID handling information
 *  * are now handled in glue layer
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * ePowerCtrl is not necessary as a glue variable.
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add KAL API: kalFlushPendingTxPackets(), and take use of the API
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access for prGlueInfo->fgIsCardRemoved in non-glue layer
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 03 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * always send CMD_NIC_POWER_CTRL packet when nic is being halted
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * correct typo.
 *
 * 03 19 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add ACPI D0/D3 state switching support
 *  *  *  *  *  *  *  * 2) use more formal way to handle interrupt when the status is retrieved from enhanced RX response
 *
 * 03 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) add another spin-lock to protect MsduInfoList due to it might be accessed by different thread.
 *  * 2) change own-back acquiring procedure to wait for up to 16.67 seconds
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-10-13 21:59:15 GMT mtk01084
**  update for new HW design
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-09-09 17:26:36 GMT mtk01084
**  remove CMD52 access
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-05-18 14:50:29 GMT mtk01084
**  modify lines in nicpmSetDriverOwn()
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-23 16:55:37 GMT mtk01084
**  modify nicpmSetDriverOwn()
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-19 18:33:00 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\1 2009-03-19 15:05:32 GMT mtk01084
**  Initial version
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
#include "precomp.h"

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
*/extern BOOLEAN fgIsResetting;

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

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to process the POWER ON procedure.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicpmSetFWOwn (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN     fgEnableGlobalInt
    )
{
    UINT_32 u4RegValue;

    ASSERT(prAdapter);

    if(prAdapter->fgIsFwOwn == TRUE) {
        return;
    }
    else {
        if(nicProcessIST(prAdapter) != WLAN_STATUS_NOT_INDICATING) {
            // pending interrupts
            return;
        }
    }

    if (fgEnableGlobalInt) {
        prAdapter->fgIsIntEnableWithLPOwnSet = TRUE;
    }
    else {
        HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_SET);

        HAL_MCR_RD(prAdapter, MCR_WHLPCR, &u4RegValue);
        if(u4RegValue & WHLPCR_FW_OWN_REQ_SET) {
            // if set firmware own not successful (possibly pending interrupts),
            // indicate an own clear event
            HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_CLR);

            return;
        }

        prAdapter->fgIsFwOwn = TRUE;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to process the POWER OFF procedure.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
nicpmSetDriverOwn (
    IN P_ADAPTER_T prAdapter
    )
{
#define LP_OWN_BACK_TOTAL_DELAY_MS      8192    //exponential of 2
#define LP_OWN_BACK_LOOP_DELAY_MS       1       //exponential of 2
#define LP_OWN_BACK_CLR_OWN_ITERATION   256     //exponential of 2

    BOOLEAN fgStatus = TRUE;
    UINT_32 i, u4CurrTick, u4RegValue = 0;

    ASSERT(prAdapter);

    if(prAdapter->fgIsFwOwn == FALSE)
        return fgStatus;

    u4CurrTick = kalGetTimeTick();
    i = 0;
    while(1) {
        HAL_MCR_RD(prAdapter, MCR_WHLPCR, &u4RegValue);

        if (u4RegValue & WHLPCR_FW_OWN_REQ_SET) {
            prAdapter->fgIsFwOwn = FALSE;
            break;
        }
        else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                || fgIsBusAccessFailed == TRUE
                || (kalGetTimeTick() - u4CurrTick) > LP_OWN_BACK_TOTAL_DELAY_MS
                ||fgIsResetting ==TRUE) {
            //ERRORLOG(("LP cannot be own back (for %ld ms)", kalGetTimeTick() - u4CurrTick));
            fgStatus = FALSE;
            break;
        }
        else {
            if((i & (LP_OWN_BACK_CLR_OWN_ITERATION - 1)) == 0) {
                /* Software get LP ownership - per 256 iterations */
                HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_CLR);
            }

            /* Delay for LP engine to complete its operation. */
            kalMsleep(LP_OWN_BACK_LOOP_DELAY_MS);
            i++;
        }
    }

    return fgStatus;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set ACPI power mode to D0.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
nicpmSetAcpiPowerD0 (
    IN P_ADAPTER_T prAdapter
    )
{
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    UINT_32     u4Value = 0, u4WHISR = 0;
    UINT_8      aucTxCount[8];
    UINT_32     i;
#if CFG_ENABLE_FW_DOWNLOAD
    UINT_32     u4FwImgLength, u4FwLoadAddr, u4ImgSecSize;
    PVOID       prFwMappingHandle;
    PVOID       pvFwImageMapFile = NULL;
    #if CFG_ENABLE_FW_DIVIDED_DOWNLOAD
    UINT_32     j;
    P_FIRMWARE_DIVIDED_DOWNLOAD_T prFwHead;
    BOOLEAN fgValidHead;
    const UINT_32 u4CRCOffset = offsetof(FIRMWARE_DIVIDED_DOWNLOAD_T, u4NumOfEntries);
    #endif
#endif

    DEBUGFUNC("nicpmSetAcpiPowerD0");

    ASSERT(prAdapter);

    do {
        /* 0. Reset variables in ADAPTER_T */
        prAdapter->fgIsFwOwn = TRUE;
        prAdapter->fgWiFiInSleepyState = FALSE;
        prAdapter->rAcpiState = ACPI_STATE_D0;
        prAdapter->fgIsEnterD3ReqIssued = FALSE;

#if defined(MT6620) || defined(MT6628)
        /* 1. Request Ownership to enter F/W download state */
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);
    #if !CFG_ENABLE_FULL_PM
        nicpmSetDriverOwn(prAdapter);
    #endif

        /* 2. Initialize the Adapter */
        if ( (u4Status = nicInitializeAdapter(prAdapter)) != WLAN_STATUS_SUCCESS ) {
            DBGLOG(INIT, ERROR, ("nicInitializeAdapter failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
#endif

    #if CFG_ENABLE_FW_DOWNLOAD
        prFwMappingHandle = kalFirmwareImageMapping(prAdapter->prGlueInfo, &pvFwImageMapFile, &u4FwImgLength);
        if(!prFwMappingHandle) {
            DBGLOG(INIT, ERROR,("Fail to load FW image from file!\n"));
            pvFwImageMapFile = NULL;
        }

        #if defined(MT6620) || defined(MT6628)
        if (pvFwImageMapFile) {
            /* 3.1 disable interrupt, download is done by polling mode only */
            nicDisableInterrupt(prAdapter);

            /* 3.2 Initialize Tx Resource to fw download state */
            nicTxInitResetResource(prAdapter);

            /* 3.3 FW download here */
            u4FwLoadAddr = kalGetFwLoadAddress(prAdapter->prGlueInfo);

            #if CFG_ENABLE_FW_DIVIDED_DOWNLOAD
            // 3a. parse file header for decision of divided firmware download or not
            prFwHead = (P_FIRMWARE_DIVIDED_DOWNLOAD_T)pvFwImageMapFile;

            if(prFwHead->u4Signature == MTK_WIFI_SIGNATURE &&
                    prFwHead->u4CRC == wlanCRC32((PUINT_8)pvFwImageMapFile + u4CRCOffset, u4FwImgLength - u4CRCOffset)) {
                fgValidHead = TRUE;
            }
            else {
                fgValidHead = FALSE;
            }

            /* 3b. engage divided firmware downloading */
            if(fgValidHead == TRUE) {
                for(i = 0 ; i < prFwHead->u4NumOfEntries ; i++) {
                #if CFG_ENABLE_FW_DOWNLOAD_AGGREGATION
                    if(wlanImageSectionDownloadAggregated(prAdapter,
                                prFwHead->arSection[i].u4DestAddr,
                                prFwHead->arSection[i].u4Length,
                                (PUINT_8)pvFwImageMapFile + prFwHead->arSection[i].u4Offset) != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                    }
                #else
                    for(j = 0 ; j < prFwHead->arSection[i].u4Length ; j += CMD_PKT_SIZE_FOR_IMAGE) {
                        if(j + CMD_PKT_SIZE_FOR_IMAGE < prFwHead->arSection[i].u4Length)
                            u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                        else
                            u4ImgSecSize = prFwHead->arSection[i].u4Length - j;
                        
                        if(wlanImageSectionDownload(prAdapter,
                                    prFwHead->arSection[i].u4DestAddr + j,
                                    u4ImgSecSize,
                                    (PUINT_8)pvFwImageMapFile + prFwHead->arSection[i].u4Offset + j) != WLAN_STATUS_SUCCESS) {
                            DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                            u4Status = WLAN_STATUS_FAILURE;
                            break;
                        }
                    }
                #endif
                    /* escape from loop if any pending error occurs */
                    if(u4Status == WLAN_STATUS_FAILURE) {
                            break;
                    }
                }
            }
            else
            #endif
            #if CFG_ENABLE_FW_DOWNLOAD_AGGREGATION
            if(wlanImageSectionDownloadAggregated(prAdapter,
                        u4FwLoadAddr,
                        u4FwImgLength,
                        (PUINT_8)pvFwImageMapFile) != WLAN_STATUS_SUCCESS) {
                DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                u4Status = WLAN_STATUS_FAILURE;
            }
            #else
            for (i = 0; i < u4FwImgLength ; i += CMD_PKT_SIZE_FOR_IMAGE) {
                if(i + CMD_PKT_SIZE_FOR_IMAGE < u4FwImgLength)
                    u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                else
                    u4ImgSecSize = u4FwImgLength - i;

                if(wlanImageSectionDownload(prAdapter,
                        u4FwLoadAddr + i,
                        u4ImgSecSize,
                        (PUINT_8)pvFwImageMapFile + i) != WLAN_STATUS_SUCCESS) {
                    DBGLOG(INIT, ERROR, ("wlanImageSectionDownload failed!\n"));
                    u4Status = WLAN_STATUS_FAILURE;
                    break;
                }
            }
            #endif

            if(u4Status != WLAN_STATUS_SUCCESS) {
                kalFirmwareImageUnmapping(prAdapter->prGlueInfo, prFwMappingHandle, pvFwImageMapFile);
                break;
            }

            #if !CFG_ENABLE_FW_DOWNLOAD_ACK
            // Send INIT_CMD_ID_QUERY_PENDING_ERROR command and wait for response
            if(wlanImageQueryStatus(prAdapter) != WLAN_STATUS_SUCCESS) {
                kalFirmwareImageUnmapping(prAdapter->prGlueInfo, prFwMappingHandle, pvFwImageMapFile);
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
            #endif

            kalFirmwareImageUnmapping(prAdapter->prGlueInfo, prFwMappingHandle, pvFwImageMapFile);
        }
        else {
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        /* 4. send Wi-Fi Start command */
            #if CFG_OVERRIDE_FW_START_ADDRESS
        wlanConfigWifiFunc(prAdapter,
                TRUE,
                kalGetFwStartAddress(prAdapter->prGlueInfo));
            #else
        wlanConfigWifiFunc(prAdapter,
                FALSE,
                0);
            #endif

        #elif defined(MT5931)
        if (pvFwImageMapFile) {
            DBGLOG(INIT, TRACE, ("Download Address: 0x%08X\n", kalGetFwLoadAddress(prAdapter->prGlueInfo)));
            DBGLOG(INIT, TRACE, ("Firmware Length:  0x%08X\n", u4FwImgLength));

            do {
                /* 1.0 whole-chip reset except HIFSYS */
                HAL_MCR_WR(prAdapter, MCR_WMCSR, WMCSR_CHIP_RST);
                HAL_MCR_WR(prAdapter, MCR_WMCSR, 0);

                /* 1.1 wait for INIT_RDY */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_INI_RDY) {
                        DBGLOG(INIT, TRACE, ("INIT-RDY detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE) {
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for Init Ready bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                /* 1.2 set KSEL/FLEN */
                HAL_MCR_WR(prAdapter, MCR_FWCFG, u4FwImgLength >> 6);

                /* 1.3 enable FWDL_EN */
                HAL_MCR_WR(prAdapter, MCR_WMCSR, WMCSR_FWDLEN);

                /* 1.4 wait for PLL_RDY */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_PLLRDY) {
                        DBGLOG(INIT, TRACE, ("PLL-RDY detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE) {
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for PLL Ready bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                /* 2.1 turn on HIFSYS firmware download mode */
                HAL_MCR_WR(prAdapter, MCR_FWDLSR, FWDLSR_FWDL_MODE);

                /* 2.2 set starting address */
                u4FwLoadAddr = kalGetFwLoadAddress(prAdapter->prGlueInfo);
                HAL_MCR_WR(prAdapter, MCR_FWDLDSAR, u4FwLoadAddr);

                /* 3. upload firmware */
                for (i = 0; i < u4FwImgLength ; i += CMD_PKT_SIZE_FOR_IMAGE) {
                    if(i + CMD_PKT_SIZE_FOR_IMAGE < u4FwImgLength)
                        u4ImgSecSize = CMD_PKT_SIZE_FOR_IMAGE;
                    else
                        u4ImgSecSize = u4FwImgLength - i;

                    if(wlanImageSectionDownload(prAdapter,
                                u4FwLoadAddr + i,
                                u4ImgSecSize,
                                (PUINT_8)pvFwImageMapFile + i) != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, ERROR, ("Firmware scatter download failed!\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                }

                /* 4.1 poll FWDL_OK & FWDL_FAIL bits */
                i = 0;
                while(1) {
                    HAL_MCR_RD(prAdapter, MCR_WMCSR, &u4Value);

                    if (u4Value & WMCSR_DL_OK) {
                        DBGLOG(INIT, TRACE, ("DL_OK detected\n"));
                        break;
                    }
                    else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                            || fgIsBusAccessFailed == TRUE
                            || (u4Value & WMCSR_DL_FAIL)) {
                        DBGLOG(INIT, ERROR, ("DL_FAIL detected: 0x%08X\n", u4Value));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                        DBGLOG(INIT, ERROR, ("Waiting for DL_OK/DL_FAIL bit: Timeout\n"));
                        u4Status = WLAN_STATUS_FAILURE;
                        break;
                    }
                    else {
                        i++;
                        kalMsleep(10);
                    }
                }

                /* 4.2 turn off HIFSYS download mode */
                HAL_MCR_WR(prAdapter, MCR_FWDLSR, 0);

            } while (FALSE);
        }
        else {
            DBGLOG(INIT, ERROR, ("No Firmware found!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }

        #endif
    #endif

        /* 5. check Wi-Fi FW asserts ready bit */
        DBGLOG(INIT, TRACE, ("wlanAdapterStart(): Waiting for Ready bit..\n"));
        i = 0;
        while(1) {
            HAL_MCR_RD(prAdapter, MCR_WCIR, &u4Value);

            if (u4Value & WCIR_WLAN_READY) {
                DBGLOG(INIT, TRACE, ("Ready bit asserted\n"));
                break;
            }
            else if(kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE
                    || fgIsBusAccessFailed == TRUE) {
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
            else if(i >= CFG_RESPONSE_POLLING_TIMEOUT) {
                DBGLOG(INIT, ERROR, ("Waiting for Ready bit: Timeout\n"));
                u4Status = WLAN_STATUS_FAILURE;
                break;
            }
            else {
                i++;
                kalMsleep(10);
            }
        }

#if defined(MT5931)
        // Acquire LP-OWN
        ACQUIRE_POWER_CONTROL_FROM_PM(prAdapter);
    #if !CFG_ENABLE_FULL_PM
        nicpmSetDriverOwn(prAdapter);
    #endif

        /* 2. Initialize the Adapter */
        if ( (u4Status = nicInitializeAdapter(prAdapter)) != WLAN_STATUS_SUCCESS ) {
            DBGLOG(INIT, ERROR, ("nicInitializeAdapter failed!\n"));
            u4Status = WLAN_STATUS_FAILURE;
            break;
        }
#endif

        if(u4Status == WLAN_STATUS_SUCCESS) {
            // 6.1 reset interrupt status
            HAL_READ_INTR_STATUS(prAdapter, 4, (PUINT_8)&u4WHISR);
            if(HAL_IS_TX_DONE_INTR(u4WHISR)) {
                HAL_READ_TX_RELEASED_COUNT(prAdapter, aucTxCount);
            }

            /* 6.2 reset TX Resource for normal operation */
            nicTxResetResource(prAdapter);

            /* 6.3 Enable interrupt */
            nicEnableInterrupt(prAdapter);

            /* 6.4 Override network address */
            wlanUpdateNetworkAddress(prAdapter);

            /* 6.5 indicate disconnection as default status */
            kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                    WLAN_STATUS_MEDIA_DISCONNECT,
                    NULL,
                    0);
        }

        RECLAIM_POWER_CONTROL_TO_PM(prAdapter, FALSE);

        /* MGMT Initialization */
        nicInitMGMT(prAdapter, NULL);

    } while(FALSE);

    if(u4Status != WLAN_STATUS_SUCCESS) {
        return FALSE;
    }
    else {
        return TRUE;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This routine is used to set ACPI power mode to D3.
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
nicpmSetAcpiPowerD3 (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32 i;

    ASSERT(prAdapter);

    /* 1. MGMT - unitialization */
    nicUninitMGMT(prAdapter);

    /* 2. Disable Interrupt */
    nicDisableInterrupt(prAdapter);

    /* 3. emit CMD_NIC_POWER_CTRL command packet */
    wlanSendNicPowerCtrlCmd(prAdapter, 1);

    /* 4. Clear Interrupt Status */
    i = 0;
    while(i < CFG_IST_LOOP_COUNT && nicProcessIST(prAdapter) != WLAN_STATUS_NOT_INDICATING) {
        i++;
    };

    /* 5. Remove pending TX */
    nicTxRelease(prAdapter);

    // 5.1 clear pending Security / Management Frames
    kalClearSecurityFrames(prAdapter->prGlueInfo);
    kalClearMgmtFrames(prAdapter->prGlueInfo);

    // 5.2 clear pending TX packet queued in glue layer
    kalFlushPendingTxPackets(prAdapter->prGlueInfo);

    /* 6. Set Onwership to F/W */
    nicpmSetFWOwn(prAdapter, FALSE);

    /* 7. Set variables */
    prAdapter->rAcpiState = ACPI_STATE_D3;

    return TRUE;
}


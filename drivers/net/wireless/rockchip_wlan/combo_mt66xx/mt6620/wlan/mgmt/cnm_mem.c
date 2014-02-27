/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/cnm_mem.c#2 $
*/

/*! \file   "cnm_mem.c"
    \brief  This file contain the management function of packet buffers and
            generic memory alloc/free functioin for mailbox message.

            A data packet has a fixed size of buffer, but a management
            packet can be equipped with a variable size of buffer.
*/



/*
** $Log: cnm_mem.c $
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 03 14 2012 wh.su
 * [WCXRP00001173] [MT6620 Wi-Fi][Driver] Adding the ICS Tethering WPA2-PSK supporting
 * Add code from 2.2
 *
 * 11 17 2011 tsaiyuan.hsu
 * [WCXRP00001115] [MT6620 Wi-Fi][DRV] avoid deactivating staRec when changing state 3 to 3.
 * initialize fgNeedResp.
 *
 * 11 17 2011 tsaiyuan.hsu
 * [WCXRP00001115] [MT6620 Wi-Fi][DRV] avoid deactivating staRec when changing state 3 to 3.
 * avoid deactivating staRec when changing state from 3 to 3.
 *
 * 02 01 2011 cm.chang
 * [WCXRP00000415] [MT6620 Wi-Fi][Driver] Check if any memory leakage happens when uninitializing in DGB mode
 * .
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * Allocate system RAM if fixed message or mgmt buffer is not available
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * .
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 12 13 2010 cp.wu
 * [WCXRP00000260] [MT6620 Wi-Fi][Driver][Firmware] Create V1.1 branch for both firmware and driver
 * create branch for Wi-Fi driver v1.1
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 11 29 2010 cm.chang
 * [WCXRP00000210] [MT6620 Wi-Fi][Driver][FW] Set RCPI value in STA_REC for initial TX rate selection of auto-rate algorithm
 * Sync RCPI of STA_REC to FW as reference of initial TX rate
 *
 * 11 25 2010 yuche.tsai
 * NULL
 * Update SLT Function for QoS Support and not be affected by fixed rate function.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * 1. remove redundant variables in STA_REC structure
 * 2. add STA-REC uninitialization routine for clearing pending events
 *
 * 10 13 2010 cm.chang
 * [WCXRP00000094] [MT6620 Wi-Fi][Driver] Connect to 2.4GHz AP, Driver crash.
 * Add exception handle when cmd buffer is not available
 *
 * 10 12 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * add HT (802.11n) fixed rate support.
 *
 * 10 08 2010 cp.wu
 * [WCXRP00000084] [MT6620 Wi-Fi][Driver][FW] Add fixed rate support for distance test
 * adding fixed rate support for distance test. (from registry setting)
 *
 * 09 24 2010 wh.su
 * NULL
 * [WCXRP00005002][MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning.
 *
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 09 16 2010 cm.chang
 * NULL
 * Change conditional compiling options for BOW
 *
 * 09 10 2010 cm.chang
 * NULL
 * Always update Beacon content if FW sync OBSS info
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 08 19 2010 wh.su
 * NULL
 * adding the tx pkt call back handle for countermeasure.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Check draft RLM code for HT cap
 *
 * 07 07 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support state of STA record change from 1 to 1
 *
 * 07 05 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Fix correct structure size in cnmStaSendDeactivateCmd()
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) ignore RSN checking when RSN is not turned on.
 * 2) set STA-REC deactivation callback as NULL
 * 3) add variable initialization API based on PHY configuration
 *
 * 07 02 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * spin lock target revised
 *
 * 07 02 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change inner loop index from i to k.
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Support sync command of STA_REC
 *
 * 06 23 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Merge g_arStaRec[] into adapter->arStaRec[]
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 05 31 2010 yarco.yang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add RX TSF Log Feature and ADDBA Rsp with DECLINE handling
 *
 * 05 28 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support checking  of duplicated buffer free
 *
 * 05 28 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * fixed the ad-hoc wpa-none send non-encrypted frame issue.
 *
 * 05 28 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Move define of STA_REC_NUM to config.h and rename to CFG_STA_REC_NUM
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 28 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Modified some MQM-related data structures (SN counter, TX/RX BA table)
 *
 * 04 27 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Added new TX/RX BA tables in STA_REC
 *
 * 04 27 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Notify MQM, TXM, and RXM upon disconnection .
 *
 * 04 26 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Call mqm, txm, rxm functions upon disconnection
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft code to support protection in AP mode
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Beacon Timeout Support
 *  *  *  *  *  *  *  *  *  *  and will send Null frame to diagnose connection
 *
 * 04 09 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * [BORA00000644] WiFi phase 4 integration
 *  * Added per-TID SN cache in STA_REC
 *
 * 04 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Different invoking order for WTBL entry of associated AP
 *
 * 03 29 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * move the wlan table alloc / free to change state function.
 *
 * 03 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support power control
 *
 * 03 03 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Initialize StaRec->arStaWaitQueue
 *
 * 03 03 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add debug message when no available pkt buffer
 *
 * 03 01 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Fixed STA_REC initialization bug: prStaRec->au2CachedSeqCtrl[k]
 *
 * 02 26 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Added fgIsWmmSupported in STA_RECORD_T.
 *
 * 02 26 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Added fgIsUapsdSupported in STA_RECORD_T
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * add support of Driver STA_RECORD_T activation
 *
 * 02 13 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Added arTspecTable in STA_REC for TSPEC management
 *
 * 02 12 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Enable mgmt buffer debug by default
 *
 * 02 12 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Added BUFFER_SOURCE_BCN
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add Deauth and Disassoc Handler
 *
 * 01 08 2010 cp.wu
 * [BORA00000368]Integrate HIF part into BORA
 * 1) separate wifi_var_emu.c/.h from wifi_var.c/.h
 *  *  *  *  *  *  *  *  * 2) eliminate HIF_EMULATION code sections appeared in wifi_var/cnm_mem
 *  *  *  *  *  *  *  *  * 3) use cnmMemAlloc() instead to allocate SRAM buffer
 *
 * 12 25 2009 tehuang.liu
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Integrated modifications for 1st connection (mainly on FW modules MQM, TXM, and RXM)
 *  *  *  *  *  *  * MQM: BA handling
 *  *  *  *  *  *  * TXM: Macros updates
 *  *  *  *  *  *  * RXM: Macros/Duplicate Removal updates
 *
 * 12 24 2009 yarco.yang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * 12 21 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support several data buffer banks.
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .For new FPGA memory size
 *
 * Dec 9 2009 MTK02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Removed DBGPRINT
 *
 * Dec 9 2009 mtk02752
 * [BORA00000368] Integrate HIF part into BORA
 * add cnmDataPktFree() for emulation loopback purpose
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix warning of null pointer
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add cnmGetStaRecByAddress() and add fgIsInUse flag in STA_RECORD_T
 *
 * Nov 23 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Assign ucBufferSource in function cnmMgtPktAlloc()
 *
 * Nov 23 2009 mtk02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Added packet redispatch function calls
 *
 * Nov 13 2009 mtk01084
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * enable packet re-usable in current emulation driver
 *
 * Nov 12 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * 1. Add new function cnmGetStaRecByIndex()
 * 2. Rename STA_REC_T to STA_RECORD_T
 *
 * Nov 9 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Call cnmDataPktDispatch() in cnmPktFree()
 *
 * Nov 2 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Remove definition of pragma section code
 *
 * Oct 28 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
 *
 * Oct 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix lint warning
 *
 * Oct 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix typo
 *
 * Oct 12 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
 *
 * Oct 8 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
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
static VOID
cnmStaRecHandleEventPkt (
    P_ADAPTER_T     prAdapter,
    P_CMD_INFO_T    prCmdInfo,
    PUINT_8         pucEventBuf
    );

static VOID
cnmStaSendUpdateCmd (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec,
    BOOLEAN         fgNeedResp
    );

static VOID
cnmStaSendRemoveCmd (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
P_MSDU_INFO_T
cnmMgtPktAlloc (
    P_ADAPTER_T     prAdapter,
    UINT_32         u4Length
    )
{
    P_MSDU_INFO_T   prMsduInfo;
    P_QUE_T         prQueList;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    prQueList = &prAdapter->rTxCtrl.rFreeMsduInfoList;

    /* Get a free MSDU_INFO_T */
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
    QUEUE_REMOVE_HEAD(prQueList, prMsduInfo, P_MSDU_INFO_T);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);

    if (prMsduInfo) {
        prMsduInfo->prPacket = cnmMemAlloc(prAdapter, RAM_TYPE_BUF, u4Length);
        prMsduInfo->eSrc = TX_PACKET_MGMT;

        if (prMsduInfo->prPacket == NULL) {
            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
            QUEUE_INSERT_TAIL(prQueList, &prMsduInfo->rQueEntry);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
            prMsduInfo = NULL;
        }
    }

#if DBG
    if (prMsduInfo == NULL) {
        DBGLOG(MEM, WARN, ("\n"));
        DBGLOG(MEM, WARN, ("MgtDesc#=%ld\n", prQueList->u4NumElem));

#if CFG_DBG_MGT_BUF
        DBGLOG(MEM, WARN, ("rMgtBufInfo: alloc#=%ld, free#=%ld, null#=%ld\n",
            prAdapter->rMgtBufInfo.u4AllocCount,
            prAdapter->rMgtBufInfo.u4FreeCount,
            prAdapter->rMgtBufInfo.u4AllocNullCount));
#endif

        DBGLOG(MEM, WARN, ("\n"));
    }
#endif

    return prMsduInfo;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
cnmMgtPktFree (
    P_ADAPTER_T     prAdapter,
    P_MSDU_INFO_T   prMsduInfo
    )
{
    P_QUE_T         prQueList;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prMsduInfo);

    prQueList = &prAdapter->rTxCtrl.rFreeMsduInfoList;

    ASSERT(prMsduInfo->prPacket);
    if (prMsduInfo->prPacket) {
        cnmMemFree(prAdapter, prMsduInfo->prPacket);
        prMsduInfo->prPacket = NULL;
    }

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
    QUEUE_INSERT_TAIL(prQueList, &prMsduInfo->rQueEntry)
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is used to initial the MGMT/MSG memory pool.
*
* \param (none)
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmMemInit (
    P_ADAPTER_T     prAdapter
    )
{
    P_BUF_INFO_T    prBufInfo;

    /* Initialize Management buffer pool */
    prBufInfo = &prAdapter->rMgtBufInfo;
    kalMemZero(prBufInfo, sizeof(prAdapter->rMgtBufInfo));
    prBufInfo->pucBuf = prAdapter->pucMgtBufCached;

    /* Setup available memory blocks. 1 indicates FREE */
    prBufInfo->rFreeBlocksBitmap =
        (BUF_BITMAP) BITS(0, MAX_NUM_OF_BUF_BLOCKS - 1);


    /* Initialize Message buffer pool */
    prBufInfo = &prAdapter->rMsgBufInfo;
    kalMemZero(prBufInfo, sizeof(prAdapter->rMsgBufInfo));
    prBufInfo->pucBuf = &prAdapter->aucMsgBuf[0];

    /* Setup available memory blocks. 1 indicates FREE */
    prBufInfo->rFreeBlocksBitmap =
        (BUF_BITMAP) BITS(0, MAX_NUM_OF_BUF_BLOCKS - 1);

    return;

} /* end of cnmMemInit() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Allocate MGMT/MSG memory pool.
*
* \param[in] eRamType       Target RAM type.
*                           TCM blk_sz= 16bytes, BUF blk_sz= 256bytes
* \param[in] u4Length       Length of the buffer to allocate.
*
* \retval !NULL    Pointer to the start address of allocated memory.
* \retval NULL     Fail to allocat memory
*/
/*----------------------------------------------------------------------------*/
PVOID
cnmMemAlloc (
    IN P_ADAPTER_T      prAdapter,
    IN ENUM_RAM_TYPE_T  eRamType,
    IN UINT_32          u4Length
    )
{
    P_BUF_INFO_T        prBufInfo;
    BUF_BITMAP          rRequiredBitmap;
    UINT_32             u4BlockNum;
    UINT_32             i, u4BlkSzInPower;
    PVOID               pvMemory;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(u4Length);

    if (eRamType == RAM_TYPE_MSG && u4Length <= 256) {
        prBufInfo = &prAdapter->rMsgBufInfo;
        u4BlkSzInPower = MSG_BUF_BLOCK_SIZE_IN_POWER_OF_2;

        u4Length += (MSG_BUF_BLOCK_SIZE - 1);
        u4BlockNum = u4Length >> MSG_BUF_BLOCK_SIZE_IN_POWER_OF_2;

        ASSERT(u4BlockNum <= MAX_NUM_OF_BUF_BLOCKS);
    }
    else {
        eRamType = RAM_TYPE_BUF;

        prBufInfo = &prAdapter->rMgtBufInfo;
        u4BlkSzInPower = MGT_BUF_BLOCK_SIZE_IN_POWER_OF_2;

        u4Length += (MGT_BUF_BLOCK_SIZE - 1);
        u4BlockNum = u4Length >> MGT_BUF_BLOCK_SIZE_IN_POWER_OF_2;

        ASSERT(u4BlockNum <= MAX_NUM_OF_BUF_BLOCKS);
    }

#if CFG_DBG_MGT_BUF
    prBufInfo->u4AllocCount++;
#endif

    KAL_ACQUIRE_SPIN_LOCK(prAdapter,
        eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

    if ((u4BlockNum > 0) && (u4BlockNum <= MAX_NUM_OF_BUF_BLOCKS)) {

        /* Convert number of block into bit cluster */
        rRequiredBitmap = BITS(0, u4BlockNum-1);

        for (i = 0; i <= (MAX_NUM_OF_BUF_BLOCKS - u4BlockNum); i++) {

            /* Have available memory blocks */
            if ((prBufInfo->rFreeBlocksBitmap & rRequiredBitmap)
                == rRequiredBitmap) {

                /* Clear corresponding bits of allocated memory blocks */
                prBufInfo->rFreeBlocksBitmap &= ~rRequiredBitmap;

                /* Store how many blocks be allocated */
                prBufInfo->aucAllocatedBlockNum[i] = (UINT_8) u4BlockNum;

                KAL_RELEASE_SPIN_LOCK(prAdapter,
                        eRamType == RAM_TYPE_MSG ?
                        SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

                /* Return the start address of allocated memory */
                return (PVOID)(prBufInfo->pucBuf + (i << u4BlkSzInPower));

            }

            rRequiredBitmap <<= 1;
        }
    }

	KAL_RELEASE_SPIN_LOCK(prAdapter,
        eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

#ifdef LINUX
    pvMemory = (PVOID)kalMemAlloc(u4Length, VIR_MEM_TYPE);
#else
    pvMemory = (PVOID)NULL;
#endif

KAL_ACQUIRE_SPIN_LOCK(prAdapter,
	eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

#if CFG_DBG_MGT_BUF
    prBufInfo->u4AllocNullCount++;

    if (pvMemory) {
        prAdapter->u4MemAllocDynamicCount++;
    }
#endif
KAL_RELEASE_SPIN_LOCK(prAdapter,
        eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

    return pvMemory;

} /* end of cnmMemAlloc() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Release memory to MGT/MSG memory pool.
*
* \param pucMemory  Start address of previous allocated memory
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmMemFree (
    IN P_ADAPTER_T      prAdapter,
    IN PVOID            pvMemory
    )
{
    P_BUF_INFO_T        prBufInfo;
    UINT_32             u4BlockIndex;
    BUF_BITMAP          rAllocatedBlocksBitmap;
    ENUM_RAM_TYPE_T     eRamType;
    KAL_SPIN_LOCK_DECLARATION();


    ASSERT(prAdapter);
    ASSERT(pvMemory);
    if (!pvMemory) {
        return;
    }

    /* Judge it belongs to which RAM type */
    if ( ((UINT_32)pvMemory >= (UINT_32)&prAdapter->aucMsgBuf[0]) &&
         ((UINT_32)pvMemory <= (UINT_32)&prAdapter->aucMsgBuf[MSG_BUFFER_SIZE-1])) {

        prBufInfo = &prAdapter->rMsgBufInfo;
        u4BlockIndex = ((UINT_32)pvMemory - (UINT_32)prBufInfo->pucBuf)
                       >> MSG_BUF_BLOCK_SIZE_IN_POWER_OF_2;
        ASSERT(u4BlockIndex < MAX_NUM_OF_BUF_BLOCKS);
        eRamType = RAM_TYPE_MSG;
    }
    else if ( ((UINT_32)pvMemory >= (UINT_32)prAdapter->pucMgtBufCached) &&
              ((UINT_32)pvMemory <= ((UINT_32)prAdapter->pucMgtBufCached + MGT_BUFFER_SIZE -1))) {
        prBufInfo = &prAdapter->rMgtBufInfo;
        u4BlockIndex = ((UINT_32)pvMemory - (UINT_32)prBufInfo->pucBuf)
                       >> MGT_BUF_BLOCK_SIZE_IN_POWER_OF_2;
        ASSERT(u4BlockIndex < MAX_NUM_OF_BUF_BLOCKS);
        eRamType = RAM_TYPE_BUF;
    }
    else {
    #ifdef LINUX
        /* For Linux, it is supported because size is not needed */
        kalMemFree(pvMemory, VIR_MEM_TYPE, 0);
    #else
        /* For Windows, it is not supported because of no size argument */
        ASSERT(0);
    #endif

    #if CFG_DBG_MGT_BUF
        prAdapter->u4MemFreeDynamicCount++;
    #endif
        return;
    }

    KAL_ACQUIRE_SPIN_LOCK(prAdapter,
        eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

#if CFG_DBG_MGT_BUF
    prBufInfo->u4FreeCount++;
#endif

    /* Convert number of block into bit cluster */
    ASSERT(prBufInfo->aucAllocatedBlockNum[u4BlockIndex] > 0);

    rAllocatedBlocksBitmap =
        BITS(0, prBufInfo->aucAllocatedBlockNum[u4BlockIndex] - 1);
    rAllocatedBlocksBitmap <<= u4BlockIndex;

    /* Clear saved block count for this memory segment */
    prBufInfo->aucAllocatedBlockNum[u4BlockIndex] = 0;

    /* Set corresponding bit of released memory block */
    prBufInfo->rFreeBlocksBitmap |= rAllocatedBlocksBitmap;

    KAL_RELEASE_SPIN_LOCK(prAdapter,
        eRamType == RAM_TYPE_MSG ? SPIN_LOCK_MSG_BUF : SPIN_LOCK_MGT_BUF);

    return;

} /* end of cnmMemFree() */


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaRecInit (
    P_ADAPTER_T     prAdapter
    )
{
    P_STA_RECORD_T  prStaRec;
    UINT_16         i;

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = &prAdapter->arStaRec[i];

        prStaRec->ucIndex = (UINT_8) i;
        prStaRec->fgIsInUse = FALSE;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaRecUninit (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_STA_RECORD_T  prStaRec;
    UINT_16         i;

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = &prAdapter->arStaRec[i];

        if (prStaRec->fgIsInUse) {
            cnmStaRecFree(prAdapter, prStaRec, FALSE);
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
P_STA_RECORD_T
cnmStaRecAlloc (
    P_ADAPTER_T     prAdapter,
    UINT_8          ucNetTypeIndex
    )
{
    P_STA_RECORD_T  prStaRec;
    UINT_16         i, k;

    ASSERT(prAdapter);

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = &prAdapter->arStaRec[i];

        if (!prStaRec->fgIsInUse) {
            /*---- Initialize STA_REC_T here ----*/
            kalMemZero(prStaRec, sizeof(STA_RECORD_T));
            prStaRec->ucIndex = (UINT_8) i;
            prStaRec->ucNetTypeIndex = ucNetTypeIndex;
            prStaRec->fgIsInUse = TRUE;

            if (prStaRec->pucAssocReqIe) {
                kalMemFree(prStaRec->pucAssocReqIe, VIR_MEM_TYPE, prStaRec->u2AssocReqIeLen);
                prStaRec->pucAssocReqIe = NULL;
                prStaRec->u2AssocReqIeLen = 0;
            }

            /* Initialize the SN caches for duplicate detection */
            for (k = 0; k < TID_NUM + 1; k++) {
                prStaRec->au2CachedSeqCtrl[k] = 0xFFFF;
            }

            /* Initialize SW TX queues in STA_REC */
            for (k = 0; k < STA_WAIT_QUEUE_NUM; k++) {
                LINK_INITIALIZE(&prStaRec->arStaWaitQueue[k]);
            }

            /* Default enable TX/RX AMPDU */
            prStaRec->fgTxAmpduEn = TRUE;
            prStaRec->fgRxAmpduEn = TRUE;

            for (k = 0; k < NUM_OF_PER_STA_TX_QUEUES; k++) {
                QUEUE_INITIALIZE(&prStaRec->arTxQueue[k]);
            }

            break;
        }
    }

    return (i < CFG_STA_REC_NUM) ? prStaRec : NULL;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaRecFree (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec,
    BOOLEAN         fgSyncToChip
    )
{
    ASSERT(prAdapter);
    ASSERT(prStaRec);

    /* To do: free related resources, e.g. timers, buffers, etc */
    cnmTimerStopTimer(prAdapter, &prStaRec->rTxReqDoneOrRxRespTimer);
    prStaRec->fgTransmitKeyExist = FALSE;
    prStaRec->fgSetPwrMgtBit = FALSE;

    if (prStaRec->pucAssocReqIe) {
        kalMemFree(prStaRec->pucAssocReqIe, VIR_MEM_TYPE, prStaRec->u2AssocReqIeLen);
        prStaRec->pucAssocReqIe = NULL;
        prStaRec->u2AssocReqIeLen = 0;
    }

    qmDeactivateStaRec(prAdapter, prStaRec->ucIndex);

    if (fgSyncToChip) {
        cnmStaSendRemoveCmd(prAdapter, prStaRec);
    }

    prStaRec->fgIsInUse = FALSE;

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaFreeAllStaByNetType (
    P_ADAPTER_T                 prAdapter,
    ENUM_NETWORK_TYPE_INDEX_T   eNetTypeIndex,
    BOOLEAN                     fgSyncToChip
    )
{
    P_STA_RECORD_T  prStaRec;
    UINT_16         i;

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = (P_STA_RECORD_T) &prAdapter->arStaRec[i];

        if (prStaRec->fgIsInUse &&
            prStaRec->ucNetTypeIndex == (UINT_8) eNetTypeIndex) {

            cnmStaRecFree(prAdapter, prStaRec, fgSyncToChip);
        }
    } /* end of for loop */
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
P_STA_RECORD_T
cnmGetStaRecByIndex (
    P_ADAPTER_T     prAdapter,
    UINT_8          ucIndex
    )
{
    P_STA_RECORD_T  prStaRec;

    ASSERT(prAdapter);

    prStaRec = (ucIndex < CFG_STA_REC_NUM) ?
               &prAdapter->arStaRec[ucIndex] : NULL;

    if (prStaRec && prStaRec->fgIsInUse == FALSE) {
        prStaRec = NULL;
    }

    return prStaRec;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Get STA_RECORD_T by Peer MAC Address(Usually TA).
*
* @param[in] pucPeerMacAddr      Given Peer MAC Address.
*
* @retval   Pointer to STA_RECORD_T, if found. NULL, if not found
*/
/*----------------------------------------------------------------------------*/
P_STA_RECORD_T
cnmGetStaRecByAddress (
    P_ADAPTER_T     prAdapter,
    UINT_8          ucNetTypeIndex,
    PUINT_8         pucPeerMacAddr
    )
{
    P_STA_RECORD_T  prStaRec;
    UINT_16         i;

    ASSERT(prAdapter);
    ASSERT(pucPeerMacAddr);

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = &prAdapter->arStaRec[i];

        if (prStaRec->fgIsInUse &&
            prStaRec->ucNetTypeIndex == ucNetTypeIndex &&
            EQUAL_MAC_ADDR(prStaRec->aucMacAddr, pucPeerMacAddr)) {
            break;
        }
    }

    return (i < CFG_STA_REC_NUM) ? prStaRec : NULL;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Reset the Status and Reason Code Field to 0 of all Station Records for
*        the specified Network Type
*
* @param[in] eNetType       Specify Network Type
*
* @return   (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaRecResetStatus (
    P_ADAPTER_T                 prAdapter,
    ENUM_NETWORK_TYPE_INDEX_T   eNetTypeIndex
    )
{
    cnmStaFreeAllStaByNetType(prAdapter, eNetTypeIndex, FALSE);

#if 0
    P_STA_RECORD_T  prStaRec;
    UINT_16         i;

    ASSERT(prAdapter);

    for (i = 0; i < CFG_STA_REC_NUM; i++) {
        prStaRec = &prAdapter->arStaRec[i];

        if (prStaRec->fgIsInUse) {
            if ((NETWORK_TYPE_AIS_INDEX == eNetTypeIndex) &&
                IS_STA_IN_AIS(prStaRec->eStaType)) {

                prStaRec->u2StatusCode = STATUS_CODE_SUCCESSFUL;
                prStaRec->u2ReasonCode = REASON_CODE_RESERVED;
                prStaRec->ucJoinFailureCount = 0;
                prStaRec->fgTransmitKeyExist = FALSE;

                prStaRec->fgSetPwrMgtBit = FALSE;
            }

            /* TODO(Kevin): For P2P and BOW */
        }
    }

    return;
#endif
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function will change the ucStaState of STA_RECORD_T and also do
*        event indication to HOST to sync the STA_RECORD_T in driver.
*
* @param[in] prStaRec       Pointer to the STA_RECORD_T
* @param[in] u4NewState     New STATE to change.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
cnmStaRecChangeState (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec,
    UINT_8          ucNewState
    )
{
    BOOLEAN         fgNeedResp;

    ASSERT(prAdapter);
    ASSERT(prStaRec);
    ASSERT(prStaRec->fgIsInUse);

    /* Do nothing when following state transitions happen,
     * other 6 conditions should be sync to FW, including 1-->1, 3-->3
     */
    if ((ucNewState == STA_STATE_2 && prStaRec->ucStaState != STA_STATE_3) ||
        (ucNewState == STA_STATE_1 && prStaRec->ucStaState == STA_STATE_2)) {
        prStaRec->ucStaState = ucNewState;
        return;
    }

    fgNeedResp = FALSE;
    if (ucNewState == STA_STATE_3) {
        secFsmEventStart(prAdapter, prStaRec);
        if (ucNewState != prStaRec->ucStaState) {
            fgNeedResp = TRUE;
        }
    }
    else {
        if (ucNewState != prStaRec->ucStaState &&
            prStaRec->ucStaState == STA_STATE_3) {
            qmDeactivateStaRec(prAdapter, prStaRec->ucIndex);
        }
        fgNeedResp = FALSE;
    }
    prStaRec->ucStaState = ucNewState;

    cnmStaSendUpdateCmd(prAdapter, prStaRec, fgNeedResp);

#if CFG_ENABLE_WIFI_DIRECT
    /* To do: Confirm if it is invoked here or other location, but it should
     *        be invoked after state sync of STA_REC
     * Update system operation parameters for AP mode
     */
    if (prAdapter->fgIsP2PRegistered && (IS_STA_IN_P2P(prStaRec))) {
        P_BSS_INFO_T    prBssInfo;

        prBssInfo = &prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex];

        if (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {
            rlmUpdateParamsForAP(prAdapter, prBssInfo, FALSE);
        }
    }
#endif
    return;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param[in]
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
static VOID
cnmStaRecHandleEventPkt (
    P_ADAPTER_T     prAdapter,
    P_CMD_INFO_T    prCmdInfo,
    PUINT_8         pucEventBuf
    )
{
    P_EVENT_ACTIVATE_STA_REC_T  prEventContent;
    P_STA_RECORD_T              prStaRec;

    prEventContent = (P_EVENT_ACTIVATE_STA_REC_T) pucEventBuf;
    prStaRec = cnmGetStaRecByIndex(prAdapter, prEventContent->ucStaRecIdx);

    if (prStaRec && prStaRec->ucStaState == STA_STATE_3 &&
        !kalMemCmp(&prStaRec->aucMacAddr[0], &prEventContent->aucMacAddr[0],
                    MAC_ADDR_LEN)) {

        qmActivateStaRec(prAdapter, prStaRec);
    }

}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param[in]
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
static VOID
cnmStaSendUpdateCmd (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec,
    BOOLEAN         fgNeedResp
    )
{
    P_CMD_UPDATE_STA_RECORD_T   prCmdContent;
    WLAN_STATUS                 rStatus;

    ASSERT(prAdapter);
    ASSERT(prStaRec);
    ASSERT(prStaRec->fgIsInUse);

    /* To do: come out a mechanism to limit one STA_REC sync once for AP mode
     *        to avoid buffer empty case when many STAs are associated
     *        simultaneously.
     */

    /* To do: how to avoid 2 times of allocated memory. Use Stack?
     *        One is here, the other is in wlanSendQueryCmd()
     */
    prCmdContent = cnmMemAlloc(prAdapter,
                        RAM_TYPE_BUF, sizeof(CMD_UPDATE_STA_RECORD_T));
    ASSERT(prCmdContent);

    /* To do: exception handle */
    if (!prCmdContent) {
        return;
    }

    prCmdContent->ucIndex = prStaRec->ucIndex;
    prCmdContent->ucStaType = (UINT_8) prStaRec->eStaType;
    kalMemCopy(&prCmdContent->aucMacAddr[0], &prStaRec->aucMacAddr[0],
               MAC_ADDR_LEN);
    prCmdContent->u2AssocId = prStaRec->u2AssocId;
    prCmdContent->u2ListenInterval = prStaRec->u2ListenInterval;
    prCmdContent->ucNetTypeIndex = prStaRec->ucNetTypeIndex;

    prCmdContent->ucDesiredPhyTypeSet = prStaRec->ucDesiredPhyTypeSet;
    prCmdContent->u2DesiredNonHTRateSet = prStaRec->u2DesiredNonHTRateSet;
    prCmdContent->u2BSSBasicRateSet = prStaRec->u2BSSBasicRateSet;
    prCmdContent->ucMcsSet = prStaRec->ucMcsSet;
    prCmdContent->ucSupMcs32 = (UINT_8) prStaRec->fgSupMcs32;
    prCmdContent->u2HtCapInfo = prStaRec->u2HtCapInfo;
    prCmdContent->ucNeedResp = (UINT_8) fgNeedResp;

#if !CFG_SLT_SUPPORT
    if(prAdapter->rWifiVar.eRateSetting != FIXED_RATE_NONE) {
        /* override rate configuration */
        nicUpdateRateParams(prAdapter,
                prAdapter->rWifiVar.eRateSetting,
                &(prCmdContent->ucDesiredPhyTypeSet),
                &(prCmdContent->u2DesiredNonHTRateSet),
                &(prCmdContent->u2BSSBasicRateSet),
                &(prCmdContent->ucMcsSet),
                &(prCmdContent->ucSupMcs32),
                &(prCmdContent->u2HtCapInfo));
    }
#endif

    prCmdContent->ucIsQoS = prStaRec->fgIsQoS;
    prCmdContent->ucIsUapsdSupported = prStaRec->fgIsUapsdSupported;
    prCmdContent->ucStaState = prStaRec->ucStaState;

    prCmdContent->ucAmpduParam = prStaRec->ucAmpduParam;
    prCmdContent->u2HtExtendedCap = prStaRec->u2HtExtendedCap;
    prCmdContent->u4TxBeamformingCap = prStaRec->u4TxBeamformingCap;
    prCmdContent->ucAselCap = prStaRec->ucAselCap;
    prCmdContent->ucRCPI = prStaRec->ucRCPI;

    prCmdContent->ucUapsdAc = prStaRec->ucBmpTriggerAC | (prStaRec->ucBmpDeliveryAC << 4);
    prCmdContent->ucUapsdSp = prStaRec->ucUapsdSp;

    rStatus = wlanSendSetQueryCmd (
                prAdapter,                  /* prAdapter */
                CMD_ID_UPDATE_STA_RECORD,   /* ucCID */
                TRUE,                       /* fgSetQuery */
                fgNeedResp,                /* fgNeedResp */
                FALSE,                      /* fgIsOid */
                fgNeedResp? cnmStaRecHandleEventPkt : NULL,
                NULL,                       /* pfCmdTimeoutHandler */
                sizeof(CMD_UPDATE_STA_RECORD_T),    /* u4SetQueryInfoLen */
                (PUINT_8) prCmdContent,     /* pucInfoBuffer */
                NULL,                       /* pvSetQueryBuffer */
                0                           /* u4SetQueryBufferLen */
                );

    ASSERT(rStatus == WLAN_STATUS_PENDING);

    cnmMemFree(prAdapter, prCmdContent);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param[in]
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
static VOID
cnmStaSendRemoveCmd (
    P_ADAPTER_T     prAdapter,
    P_STA_RECORD_T  prStaRec
    )
{
    CMD_REMOVE_STA_RECORD_T     rCmdContent;
    WLAN_STATUS                 rStatus;

    ASSERT(prAdapter);
    ASSERT(prStaRec);

    rCmdContent.ucIndex = prStaRec->ucIndex;
    kalMemCopy(&rCmdContent.aucMacAddr[0], &prStaRec->aucMacAddr[0],
               MAC_ADDR_LEN);

    rStatus = wlanSendSetQueryCmd (
                prAdapter,                  /* prAdapter */
                CMD_ID_REMOVE_STA_RECORD,   /* ucCID */
                TRUE,                       /* fgSetQuery */
                FALSE,                      /* fgNeedResp */
                FALSE,                      /* fgIsOid */
                NULL,                       /* pfCmdDoneHandler */
                NULL,                       /* pfCmdTimeoutHandler */
                sizeof(CMD_REMOVE_STA_RECORD_T),    /* u4SetQueryInfoLen */
                (PUINT_8) &rCmdContent,     /* pucInfoBuffer */
                NULL,                       /* pvSetQueryBuffer */
                0                           /* u4SetQueryBufferLen */
                );

    ASSERT(rStatus == WLAN_STATUS_PENDING);
}



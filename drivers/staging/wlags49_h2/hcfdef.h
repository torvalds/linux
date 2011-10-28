#ifndef HCFDEFC_H
#define HCFDEFC_H 1

/*************************************************************************************************
 *
 * FILE   : HCFDEF.H
 *
 * DATE   : $Date: 2004/08/05 11:47:10 $   $Revision: 1.8 $
 * Original: 2004/05/28 14:05:35    Revision: 1.59      Tag: hcf7_t20040602_01
 * Original: 2004/05/13 15:31:45    Revision: 1.53      Tag: hcf7_t7_20040513_01
 * Original: 2004/04/15 09:24:42    Revision: 1.44      Tag: hcf7_t7_20040415_01
 * Original: 2004/04/13 14:22:45    Revision: 1.43      Tag: t7_20040413_01
 * Original: 2004/04/01 15:32:55    Revision: 1.40      Tag: t7_20040401_01
 * Original: 2004/03/10 15:39:28    Revision: 1.36      Tag: t20040310_01
 * Original: 2004/03/03 14:10:12    Revision: 1.34      Tag: t20040304_01
 * Original: 2004/03/02 09:27:12    Revision: 1.32      Tag: t20040302_03
 * Original: 2004/02/24 13:00:29    Revision: 1.29      Tag: t20040224_01
 * Original: 2004/02/18 17:13:57    Revision: 1.26      Tag: t20040219_01
 *
 * AUTHOR : Nico Valster
 *
 * SPECIFICATION: ...........
 *
 * DESC   : Definitions and Prototypes for HCF only
 *
 **************************************************************************************************
 *
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * COPYRIGHT © 1994 - 1995   by AT&T.                All Rights Reserved
 * COPYRIGHT © 1996 - 2000 by Lucent Technologies.   All Rights Reserved
 * COPYRIGHT © 2001 - 2004   by Agere Systems Inc.   All Rights Reserved
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 *
 *************************************************************************************************/


/************************************************************************************************/
/*********************************  P R E F I X E S  ********************************************/
/************************************************************************************************/
//IFB_      Interface Block
//HCMD_     Hermes Command
//HFS_      Hermes (Transmit/Receive) Frame Structure
//HREG_     Hermes Register

/*************************************************************************************************/

/************************************************************************************************/
/********************************* GENERAL EQUATES **********************************************/
/************************************************************************************************/


#define HCF_MAGIC               0x7D37  // "}7" Handle validation

#define PLUG_DATA_OFFSET        0x00000800  //needed by some test tool on top of H-II NDIS driver

#define INI_TICK_INI            0x00040000L

#define IO_IN                   0       //hcfio_in_string
#define IO_OUT                  1       //hcfio_out_string

//DO_ASSERT, create an artificial FALSE to force an ASSERT without the nasty compiler warning
#define DO_ASSERT               ( assert_ifbp->IFB_Magic != HCF_MAGIC && assert_ifbp->IFB_Magic == HCF_MAGIC )
#define NT_ASSERT               0x0000      //, NEVER_TESTED
#define NEVER_TESTED            MERGE_2( 0xEFFE, 0xFEEF )
#define SE_ASSERT               0x5EFF      /* Side Effect, HCFASSERT invokation which are only called for the
                                             * side effect and which should never trigger */
#define DHF_FILE_NAME_OFFSET    10000       //to distinguish DHF from HCF asserts by means of line number
#define MMD_FILE_NAME_OFFSET    20000       //to distinguish MMD from HCF asserts by means of line number

// trace codes used to
// 1: profile execution times via HCF_TRACE and HCF_TRACE_VALUE
// 2: hierarchical flow information via HCFLOGENTRY / HCFLOGEXIT

//#define HCF_TRACE_CONNECT     useless
//#define HCF_TRACE_DISCONNECT  useless
#define HCF_TRACE_ACTION        0x0000  // 0x0001
#define HCF_TRACE_CNTL          0x0001  // 0x0002
#define HCF_TRACE_DMA_RX_GET    0x0002  // 0x0004
#define HCF_TRACE_DMA_RX_PUT    0x0003  // 0x0008
#define HCF_TRACE_DMA_TX_GET    0x0004  // 0x0010
#define HCF_TRACE_DMA_TX_PUT    0x0005  // 0x0020
#define HCF_TRACE_GET_INFO      0x0006  // 0x0040
#define HCF_TRACE_PUT_INFO      0x0007  // 0x0080
#define HCF_TRACE_RCV_MSG       0x0008  // 0x0100
#define HCF_TRACE_SEND_MSG      0x0009  // 0x0200
#define HCF_TRACE_SERVICE_NIC   0x000A  // 0x0400
// #define HCF_TRACE_           0x000C  // 0x1000
// #define HCF_TRACE_           0x000D  // 0x2000
// #define HCF_TRACE_           0x000E  // 0x4000
// #define HCF_TRACE_           0x000F  // 0x8000
//  ============================================ HCF_TRACE_... codes below 0x0010 are asserted on re-entry
#define HCF_TRACE_ACTION_KLUDGE 0x0010  /* once you start introducing kludges there is no end to it
                                         * this is an escape to do not assert on re-entrancy problem caused
                                         * by HCF_ACT_INT_FORCE_ON used to get Microsofts NDIS drivers going
                                         */
#define HCF_TRACE_STRIO         0x0020
#define HCF_TRACE_ALLOC         0X0021
#define HCF_TRACE_DL            0X0023
#define HCF_TRACE_ISR_INFO      0X0024
#define HCF_TRACE_CALIBRATE     0x0026

#define HCF_TRACE_CMD_CPL       0x0040
#define HCF_TRACE_CMD_EXE       0x0041
#define HCF_TRACE_GET_FID       0x0042
#define HCF_TRACE_GET_FRAG      0x0043
#define HCF_TRACE_INIT          0x0044
#define HCF_TRACE_PUT_FRAG      0x0045
#define HCF_TRACE_SETUP_BAP     0x0046

#define HCF_TRACE_EXIT          0x8000  // Keil C warns "long constant truncated to int"

//#define BAP_0                 HREG_DATA_0     //Used by DMA controller to access NIC RAM
#define BAP_1                   HREG_DATA_1     //Used by HCF to access NIC RAM


//************************* Hermes Receive/Transmit Frame Structures
//HFS_STAT
//see MMD.H for HFS_STAT_ERR
#define     HFS_STAT_MSG_TYPE   0xE000  //Hermes reported Message Type
#define     HFS_STAT_MIC_KEY_ID 0x1800  //MIC key used (if any)
#define     HFS_STAT_1042       0x2000  //RFC1042 Encoded
#define     HFS_STAT_TUNNEL     0x4000  //Bridge-Tunnel Encoded
#define     HFS_STAT_WMP_MSG    0x6000  //WaveLAN-II Management Protocol Frame
#if (HCF_TYPE) & HCF_TYPE_WPA
#define     HFS_STAT_MIC        0x0010  //Frame contains MIC  //;? re-instate when F/W ready
#endif

//************************* Hermes Register Offsets and Command bits
#define HREG_IO_RANGE           0x80        //I/O Range used by Hermes


//************************* Command/Status
#define HREG_CMD                0x00        //
#define     HCMD_CMD_CODE           0x3F
#define HREG_PARAM_0            0x02        //
#define HREG_PARAM_1            0x04        //
#define HREG_PARAM_2            0x06        //
#define HREG_STAT               0x08        //
#define     HREG_STAT_CMD_CODE      0x003F  //
#define     HREG_STAT_DIAG_ERR      0x0100
#define     HREG_STAT_INQUIRE_ERR   0x0500
#define     HREG_STAT_CMD_RESULT    0x7F00  //
#define HREG_RESP_0             0x0A        //
#define HREG_RESP_1             0x0C        //
#define HREG_RESP_2             0x0E        //


//************************* FID Management
#define HREG_INFO_FID           0x10        //
#define HREG_RX_FID             0x20        //
#define HREG_ALLOC_FID          0x22        //
#define HREG_TX_COMPL_FID       0x24        //


//************************* BAP
//20031030 HWi Inserted this again because the dongle code uses this (GPIF.C)
//#define HREG_SELECT_0         0x18        //
//#define HREG_OFFSET_0         0x1C        //
//#define HREG_DATA_0           0x36        //

//#define   HREG_OFFSET_BUSY        0x8000  // use HCMD_BUSY
#define     HREG_OFFSET_ERR         0x4000  //
//rsrvd #define     HREG_OFFSET_DATA_OFFSET 0x0FFF  //

#define HREG_SELECT_1           0x1A        //
#define HREG_OFFSET_1           0x1E        //
#define HREG_DATA_1             0x38        //


//************************* Event
#define HREG_EV_STAT            0x30        //
#define HREG_INT_EN             0x32        //
#define HREG_EV_ACK             0x34        //

#define    HREG_EV_TICK             0x8000  //Auxiliary Timer Tick
//#define  HREG_EV_RES              0x4000  //H-I only: H/W error (Wait Time-out)
#define    HREG_EV_INFO_DROP        0x2000  //WMAC did not have sufficient RAM to build Unsollicited Frame
#if (HCF_TYPE) & HCF_TYPE_HII5
#define    HREG_EV_ACK_REG_READY    0x0000
#else
#define    HREG_EV_ACK_REG_READY    0x1000  //Workaround Kludge bit for H-II (not H-II.5)
#endif // HCF_TYPE_HII5
#if (HCF_SLEEP) & ( HCF_CDS | HCF_DDS )
#define    HREG_EV_SLEEP_REQ        0x0800
#else
#define    HREG_EV_SLEEP_REQ        0x0000
#endif // HCF_CDS / HCF_DDS
#if HCF_DMA
//#define    HREG_EV_LPESC          0x0400 // firmware sets this bit and clears it, not for host usage.
#define    HREG_EV_RDMAD            0x0200 // rx frame in host memory
#define    HREG_EV_TDMAD            0x0100 // tx frame in host memory processed
//#define    HREG_EV_RXDMA          0x0040 // firmware kicks off DMA engine (bit is not for host usage)
//#define    HREG_EV_TXDMA          0x0020 // firmware kicks off DMA engine (bit is not for host usage)
#define    HREG_EV_FW_DMA           0x0460 // firmware / DMA engine I/F (bits are not for host usage)
#else
#define    HREG_EV_FW_DMA           0x0000
#endif // HCF_DMA
#define    HREG_EV_INFO             0x0080  // Asynchronous Information Frame
#define    HREG_EV_CMD              0x0010  // Command completed, Status and Response available
#define    HREG_EV_ALLOC            0x0008  // Asynchronous part of Allocation/Reclaim completed
#define    HREG_EV_TX_EXC           0x0004  // Asynchronous Transmission unsuccessful completed
#define    HREG_EV_TX               0x0002  // Asynchronous Transmission successful completed
#define    HREG_EV_RX               0x0001  // Asynchronous Receive Frame

#define    HREG_EV_TX_EXT           ( (HCF_EXT) & (HCF_EXT_INT_TX_EX | HCF_EXT_INT_TICK ) )
/* HREG_EV_TX_EXT := 0x0000 or HREG_EV_TX_EXC and/or HREG_EV_TICK
 * could be extended with HREG_EV_TX */
#if HCF_EXT_INT_TX_EX != HREG_EV_TX_EXC
err: these values should match;
#endif // HCF_EXT_INT_TX_EX / HREG_EV_TX_EXC

#if HCF_EXT_INT_TICK != HREG_EV_TICK
err: these values should match;
#endif // HCF_EXT_INT_TICK / HREG_EV_TICK

//************************* Host Software
#define HREG_SW_0               0x28        //
#define HREG_SW_1               0x2A        //
#define HREG_SW_2               0x2C        //
//rsrvd #define HREG_SW_3       0x2E        //
//************************* Control and Auxiliary Port

#define HREG_IO                 0x12
#define     HREG_IO_SRESET          0x0001
#define     HREG_IO_WAKEUP_ASYNC    0x0002
#define     HREG_IO_WOKEN_UP        0x0004
#define HREG_CNTL               0x14        //
//#define       HREG_CNTL_WAKEUP_SYNC   0x0001
#define     HREG_CNTL_AUX_ENA_STAT  0xC000
#define     HREG_CNTL_AUX_DIS_STAT  0x0000
#define     HREG_CNTL_AUX_ENA_CNTL  0x8000
#define     HREG_CNTL_AUX_DIS_CNTL  0x4000
#define     HREG_CNTL_AUX_DSD       0x2000
#define     HREG_CNTL_AUX_ENA       (HREG_CNTL_AUX_ENA_CNTL | HREG_CNTL_AUX_DIS_CNTL )
#define HREG_SPARE              0x16        //
#define HREG_AUX_PAGE           0x3A        //
#define HREG_AUX_OFFSET         0x3C        //
#define HREG_AUX_DATA           0x3E        //

#if HCF_DMA
//************************* DMA (bus mastering)
// Be careful to use these registers only at a genuine 32 bits NIC
// On 16 bits NICs, these addresses are mapped into the range 0x00 through 0x3F with all consequences
// thereof, e.g.  HREG_DMA_CTRL register maps to HREG_CMD.
#define HREG_DMA_CTRL                       0x0040
#define HREG_TXDMA_PTR32                    0x0044
#define HREG_TXDMA_PRIO_PTR32               0x0048
#define HREG_TXDMA_HIPRIO_PTR32             0x004C
#define HREG_RXDMA_PTR32                    0x0050
#define HREG_CARDDETECT_1                   0x007C // contains 7D37
#define HREG_CARDDETECT_2                   0x007E // contains 7DE7
#define HREG_FREETIMER                      0x0058
#define HREG_DMA_RX_CNT                     0x0026

/******************************************************************************
 * Defines for the bits in the DmaControl register (@40h)
 ******************************************************************************/
#define HREG_DMA_CTRL_RXHWEN                0x80000000 // high word enable bit
#define HREG_DMA_CTRL_RXRESET               0x40000000 // tx dma init bit
#define HREG_DMA_CTRL_RXBAP1                BIT29
#define HREG_DMA_CTRL_RX_STALLED            BIT28
#define HREG_DMA_CTRL_RXAUTOACK_DMADONE     BIT27 // no host involvement req. for TDMADONE event
#define HREG_DMA_CTRL_RXAUTOACK_INFO        BIT26 // no host involvement req. for alloc event
#define HREG_DMA_CTRL_RXAUTOACK_DMAEN       0x02000000 // no host involvement req. for TxDMAen event
#define HREG_DMA_CTRL_RXAUTOACK_RX          0x01000000 // no host involvement req. for tx event
#define HREG_DMA_CTRL_RX_BUSY               BIT23 // read only bit
//#define HREG_DMA_CTRL_RX_RBUFCONT_PLAIN       0     // bits 21..20
//#define HREG_DMA_CTRL_RX_MODE_PLAIN_DMA       0     // mode 0
#define HREG_DMA_CTRL_RX_MODE_SINGLE_PACKET 0x00010000 // mode 1
#define HREG_DMA_CTRL_RX_MODE_MULTI_PACKET  0x00020000 // mode 2
//#define HREG_DMA_CTRL_RX_MODE_DISABLE     (0x00020000|0x00010000) // disable tx dma engine
#define HREG_DMA_CTRL_TXHWEN                0x8000 // low word enable bit
#define HREG_DMA_CTRL_TXRESET               0x4000 // rx dma init bit
#define HREG_DMA_CTRL_TXBAP1                BIT13
#define HREG_DMA_CTRL_TXAUTOACK_DMADONE     BIT11 // no host involvement req. for RxDMADONE event
#define HREG_DMA_CTRL_TXAUTOACK_DMAEN       0x00000400 // no host involvement req. for RxDMAen event
#define HREG_DMA_CTRL_TXAUTOACK_DMAALLOC    0x00000200  // no host involvement req. for info event
#define HREG_DMA_CTRL_TXAUTOACK_TX          0x00000100  // no host involvement req. for rx event
#define HREG_DMA_CTRL_TX_BUSY               BIT7  // read only bit
//#define HREG_DMA_CTRL_TX_TBUFCONT_PLAIN       0     // bits 6..5
//#define HREG_DMA_CTRL_TX_MODE_PLAIN_DMA       0     // mode 0
#define HREG_DMA_CTRL_TX_MODE_SINGLE_PACKET BIT0 // mode 1
#define HREG_DMA_CTRL_TX_MODE_MULTI_PACKET  0x00000002 // mode 2
//#define HREG_DMA_CTRL_TX_MODE_DISABLE     (0x00000001|0x00000002) // disable tx dma engine

//configuration DWORD to configure DMA for mode2 operation, using BAP0 as the DMA BAP.
#define DMA_CTRLSTAT_GO (HREG_DMA_CTRL_RXHWEN | HREG_DMA_CTRL_RX_MODE_MULTI_PACKET | \
                         HREG_DMA_CTRL_RXAUTOACK_DMAEN | HREG_DMA_CTRL_RXAUTOACK_RX | \
                         HREG_DMA_CTRL_TXHWEN | /*;?HREG_DMA_CTRL_TX_TBUFCONT_PLAIN |*/ \
                         HREG_DMA_CTRL_TX_MODE_MULTI_PACKET | HREG_DMA_CTRL_TXAUTOACK_DMAEN | \
                         HREG_DMA_CTRL_TXAUTOACK_DMAALLOC)

//configuration DWORD to reset both the Tx and Rx DMA engines
#define DMA_CTRLSTAT_RESET (HREG_DMA_CTRL_RXHWEN | HREG_DMA_CTRL_RXRESET | HREG_DMA_CTRL_TXHWEN | HREG_DMA_CTRL_TXRESET)

//#define DESC_DMA_OWNED            0x80000000                  // BIT31
#define DESC_DMA_OWNED          0x8000                      // BIT31
#define DESC_SOP                0x8000                      // BIT15
#define DESC_EOP                0x4000                      // BIT14

#define DMA_RX              0
#define DMA_TX              1

// #define IFB_RxFirstDesc      IFB_FirstDesc[DMA_RX]
// #define IFB_TxFirstDesc      IFB_FirstDesc[DMA_TX]
// #define IFB_RxLastDesc       IFB_LastDesc[DMA_RX]
// #define IFB_TxLastDesc       IFB_LastDesc[DMA_TX]

#endif // HCF_DMA
//
/************************************************************************************************/
/**********************************  EQUATES  ***************************************************/
/************************************************************************************************/


// Hermes Command Codes and Qualifier bits
#define     HCMD_BUSY           0x8000  // Busy bit, applicable for all commands
#define HCMD_INI                0x0000  //
#define HCMD_ENABLE             HCF_CNTL_ENABLE     // 0x0001
#define HCMD_DISABLE            HCF_CNTL_DISABLE    // 0x0002
#define HCMD_CONNECT            HCF_CNTL_CONNECT    // 0x0003
#define HCMD_EXECUTE            0x0004  //
#define HCMD_DISCONNECT         HCF_CNTL_DISCONNECT // 0x0005
#define HCMD_SLEEP              0x0006  //
#define HCMD_CONTINUE           HCF_CNTL_CONTINUE   // 0x0007
#define     HCMD_RETRY          0x0100  // Retry bit
#define HCMD_ALLOC              0x000A  //
#define HCMD_TX                 0x000B  //
#define     HCMD_RECL           0x0100  // Reclaim bit, applicable for Tx and Inquire
#define HCMD_INQUIRE            0x0011  //
#define HCMD_ACCESS             0x0021  //
#define     HCMD_ACCESS_WRITE   0x0100  // Write bit
#define HCMD_PROGRAM            0x0022  //
#define HCMD_READ_MIF           0x0030
#define HCMD_WRITE_MIF          0x0031
#define HCMD_THESEUS            0x0038
#define     HCMD_STARTPREAMBLE  0x0E00  // Start continuous preamble Tx
#define     HCMD_STOP           0x0F00  // Stop Theseus test mode


//Configuration Management
//

#define CFG_DRV_ACT_RANGES_PRI_3_BOTTOM 1   // Default Bottom Compatibility for Primary Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_PRI_3_TOP    1   // Default Top    Compatibility for Primary Firmware - driver I/F

#define CFG_DRV_ACT_RANGES_HSI_4_BOTTOM 1   // Default Bottom Compatibility for H/W - driver I/F
#define CFG_DRV_ACT_RANGES_HSI_4_TOP    1   // Default Top    Compatibility for H/W - driver I/F

#define CFG_DRV_ACT_RANGES_HSI_5_BOTTOM 1   // Default Bottom Compatibility for H/W - driver I/F
#define CFG_DRV_ACT_RANGES_HSI_5_TOP    1   // Default Top    Compatibility for H/W - driver I/F

#if (HCF_TYPE) & HCF_TYPE_WPA
#define CFG_DRV_ACT_RANGES_APF_1_BOTTOM 16  // Default Bottom Compatibility for AP Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_APF_1_TOP    16  // Default Top    Compatibility for AP Firmware - driver I/F
#else  //;? is this REALLY O.K.
#define CFG_DRV_ACT_RANGES_APF_1_BOTTOM 1   // Default Bottom Compatibility for AP Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_APF_1_TOP    1   // Default Top    Compatibility for AP Firmware - driver I/F
#endif // HCF_TYPE_WPA

#define CFG_DRV_ACT_RANGES_APF_2_BOTTOM 2   // Default Bottom Compatibility for AP Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_APF_2_TOP    2   // Default Top    Compatibility for AP Firmware - driver I/F

#define CFG_DRV_ACT_RANGES_APF_3_BOTTOM 1   // Default Bottom Compatibility for AP Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_APF_3_TOP    1   // Default Top    Compatibility for AP Firmware - driver I/F

#define CFG_DRV_ACT_RANGES_APF_4_BOTTOM 1   // Default Bottom Compatibility for AP Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_APF_4_TOP    1   // Default Top    Compatibility for AP Firmware - driver I/F

#if (HCF_TYPE) & HCF_TYPE_HII5
#define CFG_DRV_ACT_RANGES_STA_2_BOTTOM 6   // Default Bottom Compatibility for Station Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_STA_2_TOP    6   // Default Top    Compatibility for Station Firmware - driver I/F
#else // (HCF_TYPE) & HCF_TYPE_HII5
#define CFG_DRV_ACT_RANGES_STA_2_BOTTOM 1   // Default Bottom Compatibility for Station Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_STA_2_TOP    2   // Default Top    Compatibility for Station Firmware - driver I/F
#endif // (HCF_TYPE) & HCF_TYPE_HII5

#define CFG_DRV_ACT_RANGES_STA_3_BOTTOM 1   // Default Bottom Compatibility for Station Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_STA_3_TOP    1   // Default Top    Compatibility for Station Firmware - driver I/F

#define CFG_DRV_ACT_RANGES_STA_4_BOTTOM 1   // Default Bottom Compatibility for Station Firmware - driver I/F
#define CFG_DRV_ACT_RANGES_STA_4_TOP    1   // Default Top    Compatibility for Station Firmware - driver I/F

//---------------------------------------------------------------------------------------------------------------------
#if defined HCF_CFG_PRI_1_TOP || defined HCF_CFG_PRI_1_BOTTOM
err: PRI_1 not supported for H-I;   // Compatibility for Primary Firmware - driver I/F
#endif // HCF_CFG_PRI_1_TOP / HCF_CFG_PRI_1_BOTTOM

#if defined HCF_CFG_PRI_2_TOP || defined HCF_CFG_PRI_2_BOTTOM
err: PRI_2 not supported for H-I;   // Compatibility for Primary Firmware - driver I/F
#endif // HCF_CFG_PRI_2_TOP / HCF_CFG_PRI_2_BOTTOM

#ifdef HCF_CFG_PRI_3_TOP                                    // Top Compatibility for Primary Firmware - driver I/F
#if HCF_CFG_PRI_3_TOP == 0 ||						\
	CFG_DRV_ACT_RANGES_PRI_3_BOTTOM <= HCF_CFG_PRI_3_TOP && HCF_CFG_PRI_3_TOP <= CFG_DRV_ACT_RANGES_PRI_3_TOP
#undef CFG_DRV_ACT_RANGES_PRI_3_TOP
#define CFG_DRV_ACT_RANGES_PRI_3_TOP    HCF_CFG_PRI_3_TOP
#else
err: ;
#endif
#endif // HCF_CFG_PRI_3_TOP

#ifdef HCF_CFG_PRI_3_BOTTOM                                 // Bottom Compatibility for Primary Firmware - driver I/F
#if CFG_DRV_ACT_RANGES_PRI_3_BOTTOM <= HCF_CFG_PRI_3_BOTTOM && HCF_CFG_PRI_3_BOTTOM <= CFG_DRV_ACT_RANGES_PRI_3_TOP
#undef CFG_DRV_ACT_RANGES_PRI_3_BOTTOM
#define CFG_DRV_ACT_RANGES_PRI_3_BOTTOM HCF_CFG_PRI_3_BOTTOM
#else
err: ;
#endif
#endif // HCF_CFG_PRI_3_BOTTOM


//---------------------------------------------------------------------------------------------------------------------
#if defined HCF_CFG_HSI_0_TOP || defined HCF_CFG_HSI_0_BOTTOM
err: HSI_0 not supported for H-I;   // Compatibility for HSI I/F
#endif // HCF_CFG_HSI_0_TOP / HCF_CFG_HSI_0_BOTTOM

#if defined HCF_CFG_HSI_1_TOP || defined HCF_CFG_HSI_1_BOTTOM
err: HSI_1 not supported for H-I;   // Compatibility for HSI I/F
#endif // HCF_CFG_HSI_1_TOP / HCF_CFG_HSI_1_BOTTOM

#if defined HCF_CFG_HSI_2_TOP || defined HCF_CFG_HSI_2_BOTTOM
err: HSI_2 not supported for H-I;   // Compatibility for HSI I/F
#endif // HCF_CFG_HSI_2_TOP / HCF_CFG_HSI_2_BOTTOM

#if defined HCF_CFG_HSI_3_TOP || defined HCF_CFG_HSI_3_BOTTOM
err: HSI_3 not supported for H-I;   // Compatibility for HSI I/F
#endif // HCF_CFG_HSI_3_TOP / HCF_CFG_HSI_3_BOTTOM

#ifdef HCF_CFG_HSI_4_TOP                                    // Top Compatibility for HSI I/F
#if HCF_CFG_HSI_4_TOP == 0 ||						\
	CFG_DRV_ACT_RANGES_HSI_4_BOTTOM <= CF_CFG_HSI_4_TOP && HCF_CFG_HSI_4_TOP <= CFG_DRV_ACT_RANGES_HSI_4_TOP
#undef CFG_DRV_ACT_RANGES_HSI_4_TOP
#define CFG_DRV_ACT_RANGES_HSI_4_TOP    HCF_CFG_HSI_4_TOP
#else
err: ;
#endif
#endif // HCF_CFG_HSI_4_TOP

#ifdef HCF_CFG_HSI_4_BOTTOM                             // Bottom Compatibility for HSI I/F
#if CFG_DRV_ACT_RANGES_HSI_4_BOTTOM <= HCF_CFG_HSI_4_BOTTOM && HCF_CFG_HSI_4_BOTTOM <= CFG_DRV_ACT_RANGES_HSI_4_TOP
#undef CFG_DRV_ACT_RANGES_HSI_4_BOTTOM
#define CFG_DRV_ACT_RANGES_HSI_4_BOTTOM HCF_CFG_HSI_4_BOTTOM
#else
err: ;
#endif
#endif // HCF_CFG_HSI_4_BOTTOM

#ifdef HCF_CFG_HSI_5_TOP                                    // Top Compatibility for HSI I/F
#if HCF_CFG_HSI_5_TOP == 0 ||						\
	CFG_DRV_ACT_RANGES_HSI_5_BOTTOM <= CF_CFG_HSI_5_TOP && HCF_CFG_HSI_5_TOP <= CFG_DRV_ACT_RANGES_HSI_5_TOP
#undef CFG_DRV_ACT_RANGES_HSI_5_TOP
#define CFG_DRV_ACT_RANGES_HSI_5_TOP    HCF_CFG_HSI_5_TOP
#else
err: ;
#endif
#endif // HCF_CFG_HSI_5_TOP

#ifdef HCF_CFG_HSI_5_BOTTOM                             // Bottom Compatibility for HSI I/F
#if CFG_DRV_ACT_RANGES_HSI_5_BOTTOM <= HCF_CFG_HSI_5_BOTTOM && HCF_CFG_HSI_5_BOTTOM <= CFG_DRV_ACT_RANGES_HSI_5_TOP
#undef CFG_DRV_ACT_RANGES_HSI_5_BOTTOM
#define CFG_DRV_ACT_RANGES_HSI_5_BOTTOM HCF_CFG_HSI_5_BOTTOM
#else
err: ;
#endif
#endif // HCF_CFG_HSI_5_BOTTOM
//---------------------------------------------------------------------------------------------------------------------
#if defined HCF_CFG_APF_1_TOP || defined HCF_CFG_APF_1_BOTTOM
err: APF_1 not supported for H-I;   // Compatibility for AP Firmware - driver I/F
#endif // HCF_CFG_APF_1_TOP / HCF_CFG_APF_1_BOTTOM

#ifdef HCF_CFG_APF_2_TOP                                    // Top Compatibility for AP Firmware - driver I/F
#if HCF_CFG_APF_2_TOP == 0 ||						\
	CFG_DRV_ACT_RANGES_APF_2_BOTTOM <= HCF_CFG_APF_2_TOP && HCF_CFG_APF_2_TOP <= CFG_DRV_ACT_RANGES_APF_2_TOP
#undef CFG_DRV_ACT_RANGES_APF_2_TOP
#define CFG_DRV_ACT_RANGES_APF_2_TOP    HCF_CFG_APF_2_TOP
#else
err: ;
#endif
#endif // HCF_CFG_APF_TOP

#ifdef HCF_CFG_APF_2_BOTTOM                                 // Bottom Compatibility for AP Firmware - driver I/F
#if CFG_DRV_ACT_RANGES_APF_2_BOTTOM <= HCF_CFG_APF_2_BOTTOM && HCF_CFG_APF_2_BOTTOM <= CFG_DRV_ACT_RANGES_APF_2_TOP
#undef CFG_DRV_ACT_RANGES_APF_2_BOTTOM
#define CFG_DRV_ACT_RANGES_APF_2_BOTTOM HCF_CFG_APF_2_BOTTOM
#else
err: ;
#endif
#endif // HCF_CFG_APF_BOTTOM

//---------------------------------------------------------------------------------------------------------------------
#if defined HCF_CFG_STA_1_TOP || defined HCF_CFG_STA_1_BOTTOM
err: STA_1 not supported for H-I;   // Compatibility for Station Firmware - driver I/F
#endif // HCF_CFG_STA_1_TOP / HCF_CFG_STA_1_BOTTOM

#ifdef HCF_CFG_STA_2_TOP                                    // Top Compatibility for Station Firmware - driver I/F
#if HCF_CFG_STA_2_TOP == 0 ||						\
	CFG_DRV_ACT_RANGES_STA_2_BOTTOM <= HCF_CFG_STA_2_TOP && HCF_CFG_STA_2_TOP <= CFG_DRV_ACT_RANGES_STA_2_TOP
#undef CFG_DRV_ACT_RANGES_STA_2_TOP
#define CFG_DRV_ACT_RANGES_STA_2_TOP    HCF_CFG_STA_2_TOP
#else
err: ;
#endif
#endif // HCF_CFG_STA_TOP

#ifdef HCF_CFG_STA_2_BOTTOM                                 // Bottom Compatibility for Station Firmware - driver I/F
#if CFG_DRV_ACT_RANGES_STA_2_BOTTOM <= HCF_CFG_STA_2_BOTTOM && HCF_CFG_STA_2_BOTTOM <= CFG_DRV_ACT_RANGES_STA_2_TOP
#undef CFG_DRV_ACT_RANGES_STA_2_BOTTOM
#define CFG_DRV_ACT_RANGES_STA_2_BOTTOM HCF_CFG_STA_2_BOTTOM
#else
err: ;
#endif
#endif // HCF_CFG_STA_BOTTOM


/************************************************************************************************/
/**************************************  MACROS  ************************************************/
/************************************************************************************************/

#ifdef HCF_SLEEP
#define MSF_WAIT(x) do {						\
		PROT_CNT_INI;						\
		HCF_WAIT_WHILE((IPW(HREG_IO) & HREG_IO_WOKEN_UP) == 0); \
		HCFASSERT( prot_cnt, IPW( HREG_IO ) );			\
	} while (0)
#else
#define MSF_WAIT(x) do { } while (0)
#endif // HCF_SLEEP

#define LOF(x)          (sizeof(x)/sizeof(hcf_16)-1)

//resolve problems on for some 16 bits compilers to create 32 bit values
#define MERGE_2( hw, lw )   ( ( ((hcf_32)(hw)) << 16 ) | ((hcf_16)(lw)) )

#if ! defined HCF_STATIC
#define       HCF_STATIC    static
#endif //     HCF_STATIC

#if ( (HCF_TYPE) & HCF_TYPE_HII5 ) == 0
#define DAWA_ACK( mask) do {						\
		OPW( HREG_EV_ACK, mask | HREG_EV_ACK_REG_READY );	\
		OPW( HREG_EV_ACK, (mask & ~HREG_EV_ALLOC) | HREG_EV_ACK_REG_READY ); \
	} while (0)
#define DAWA_ZERO_FID(reg) OPW( reg, 0 )
#else
#define DAWA_ACK( mask)   OPW( HREG_EV_ACK, mask )
#define DAWA_ZERO_FID(reg) do { } while (0)
#endif // HCF_TYPE_HII5

#if (HCF_TYPE) & HCF_TYPE_WPA
#define CALC_RX_MIC( p, len ) calc_mic_rx_frag( ifbp, p, len )
#define CALC_TX_MIC( p, len ) calc_mic_tx_frag( ifbp, p, len )
#else
#define CALC_RX_MIC( p, len )
#define CALC_TX_MIC( p, len )
#define MIC_RX_RTN( mic, dw )
#define MIC_TX_RTN( mic, dw )
#endif // HCF_TYPE_WPA

#if HCF_TALLIES & HCF_TALLIES_HCF       //HCF tally support
#define IF_TALLY(x) do { x; } while (0)
#else
#define IF_TALLY(x) do { } while (0)
#endif // HCF_TALLIES_HCF


#if HCF_DMA
#define IF_DMA(x)           do { x; } while(0)
#define IF_NOT_DMA(x)       do { } while(0)
#define IF_USE_DMA(x)       if (   ifbp->IFB_CntlOpt & USE_DMA  ) { x; }
#define IF_NOT_USE_DMA(x)   if ( !(ifbp->IFB_CntlOpt & USE_DMA) ) { x; }
#else
#define IF_DMA(x)           do { } while(0)
#define IF_NOT_DMA(x)       do { x; } while(0)
#define IF_USE_DMA(x)       do { } while(0)
#define IF_NOT_USE_DMA(x)   do { x; } while(0)
#endif // HCF_DMA


#define IPW(x) ((hcf_16)IN_PORT_WORD( ifbp->IFB_IOBase + (x) ) )
#define OPW(x, y) OUT_PORT_WORD( ifbp->IFB_IOBase + (x), y )
/* make sure the implementation of HCF_WAIT_WHILE is such that there may be multiple HCF_WAIT_WHILE calls
 * in a row and that when one fails all subsequent fail immediately without reinitialization of prot_cnt
 */
#if HCF_PROT_TIME == 0
#define PROT_CNT_INI    do { } while(0)
#define IF_PROT_TIME(x) do { } while(0)
#if defined HCF_YIELD
#define HCF_WAIT_WHILE( x ) do { } while( (x) && (HCF_YIELD) )
#else
#define HCF_WAIT_WHILE( x ) do { } while ( x )
#endif // HCF_YIELD
#else
#define PROT_CNT_INI    hcf_32 prot_cnt = ifbp->IFB_TickIni
#define IF_PROT_TIME(x) do { x; } while(0)
#if defined HCF_YIELD
#define HCF_WAIT_WHILE( x ) while ( prot_cnt && (x) && (HCF_YIELD) ) prot_cnt--;
#else
#include <linux/delay.h>
#define HCF_WAIT_WHILE( x ) while ( prot_cnt && (x) ) { udelay(2); prot_cnt--; }
#endif // HCF_YIELD
#endif // HCF_PROT_TIME

#if defined HCF_EX_INT
//#if HCF_EX_INT & ~( HCF_EX_INT_TX_EX | HCF_EX_INT_TX_OK | HCF_EX_INT_TICK )
;? out dated checking
err: you used an invalid bitmask;
// #endif // HCF_EX_INT validation
// #else
// #define HCF_EX_INT 0x000
#endif // HCF_EX_INT

#if 0 //get compiler going
#if HCF_EX_INT_TICK != HREG_EV_TICK
;? out dated checking
err: someone redefined these macros while the implemenation assumes they are equal;
#endif
#if HCF_EX_INT_TX_OK != HFS_TX_CNTL_TX_OK || HFS_TX_CNTL_TX_OK != HREG_EV_TX_OK
;? out dated checking
err: someone redefined these macros while the implemenation assumes they are equal;
#endif
#if HCF_EX_INT_TX_EX != HFS_TX_CNTL_TX_EX || HFS_TX_CNTL_TX_EX != HREG_EV_TX_EX
;? out dated checking
err: someone redefined these macros while the implemenation assumes they are equal;
#endif
#endif // 0 get compiler going


/* The assert in HCFLOGENTRY checks against re-entrancy. Re-entrancy could be caused by MSF logic at
 * task-level calling hcf_functions without shielding with HCF_ACT_ON/_OFF. When an interrupt occurs,
 * the ISR could (either directly or indirectly) cause re-entering of the interrupted HCF-routine.
 *
 * The "(ifbp->IFB_AssertWhere = where)" test in HCFLOGENTRY services ALSO as a statement to get around:
 * #pragma warning: conditional expression is constant
 * on the if-statement
 */
#if HCF_ASSERT
#define HCFASSERT(x,q) do { if (!(x)) {mdd_assert(ifbp, __LINE__, q );} } while(0)
#define MMDASSERT(x,q) {if (!(x)) {mdd_assert( assert_ifbp, __LINE__ + FILE_NAME_OFFSET, q );}}

#define HCFLOGENTRY( where, what ) do {					\
		if ( (ifbp->IFB_AssertWhere = where) <= 15 ) {		\
			HCFASSERT( (ifbp->IFB_AssertTrace & 1<<((where)&0xF)) == 0, ifbp->IFB_AssertTrace ); \
			ifbp->IFB_AssertTrace |= 1<<((where)&0xF);	\
		}							\
		HCFTRACE(ifbp, where );					\
		HCFTRACEVALUE(ifbp, what );				\
	} while (0)

#define HCFLOGEXIT( where ) do {					\
		if ( (ifbp->IFB_AssertWhere = where) <= 15 ) {		\
			ifbp->IFB_AssertTrace &= ~(1<<((where)&0xF));	\
		}							\
		HCFTRACE(ifbp, (where)|HCF_TRACE_EXIT );		\
	} while (0)

#else // HCF_ASSERT
#define HCFASSERT( x, q ) do { } while(0)
#define MMDASSERT( x, q )
#define HCFLOGENTRY( where, what ) do { } while(0)
#define HCFLOGEXIT( where )        do { } while(0)
#endif // HCF_ASSERT

#if HCF_INT_ON
/* ;? HCFASSERT_INT
 * #if (HCF_SLEEP) & HCF_DDS
 * #define HCFASSERT_INT HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFF && ifbp->IFB_IntOffCnt != 0xFFFE, \
 *                               ifbp->IFB_IntOffCnt )
 * #else
 */
#define HCFASSERT_INT HCFASSERT( ifbp->IFB_IntOffCnt != 0xFFFF, ifbp->IFB_IntOffCnt )
// #endif // HCF_DDS
#else
#define HCFASSERT_INT
#endif // HCF_INT_ON


#if defined HCF_TRACE
#define HCFTRACE(ifbp, where )     do {OPW( HREG_SW_1, where );} while(0)
//#define HCFTRACE(ifbp, where )       {HCFASSERT( DO_ASSERT, where );}
#define HCFTRACEVALUE(ifbp, what ) do {OPW( HREG_SW_2, what  );} while (0)
//#define HCFTRACEVALUE(ifbp, what ) {HCFASSERT( DO_ASSERT, what  );}
#else
#define HCFTRACE(ifbp, where )     do { } while(0)
#define HCFTRACEVALUE(ifbp, what ) do { } while(0)
#endif // HCF_TRACE


#if HCF_BIG_ENDIAN
#define BE_PAR(x)               ,x
#else
#define BE_PAR(x)
#endif // HCF_BIG_ENDIAN

/************************************************************************************************/
/**************************************  END OF MACROS  *****************************************/
/************************************************************************************************/

/************************************************************************************************/
/***************************************  PROTOTYPES  *******************************************/
/************************************************************************************************/

#if HCF_ASSERT
extern IFBP BASED assert_ifbp;          //to make asserts easily work under MMD and DHF
EXTERN_C void        mdd_assert         (IFBP ifbp, unsigned int line_number, hcf_32 q );
#endif //HCF_ASSERT

#if ! ( (HCF_IO) & HCF_IO_32BITS )              // defined 16 bits only
#undef OUT_PORT_STRING_32
#undef IN_PORT_STRING_32
#endif // HCF_IO
#endif  //HCFDEFC_H


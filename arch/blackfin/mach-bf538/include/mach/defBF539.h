/*
 * Copyright 2008-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF539_H
#define _DEF_BF539_H

#include "defBF538.h"

/* Media Transceiver (MXVR)   (0xFFC02700 - 0xFFC028FF) */

#define	MXVR_CONFIG	      0xFFC02700  /* MXVR Configuration	Register */
#define	MXVR_PLL_CTL_0	      0xFFC02704  /* MXVR Phase	Lock Loop Control Register 0 */

#define	MXVR_STATE_0	      0xFFC02708  /* MXVR State	Register 0 */
#define	MXVR_STATE_1	      0xFFC0270C  /* MXVR State	Register 1 */

#define	MXVR_INT_STAT_0	      0xFFC02710  /* MXVR Interrupt Status Register 0 */
#define	MXVR_INT_STAT_1	      0xFFC02714  /* MXVR Interrupt Status Register 1 */

#define	MXVR_INT_EN_0	      0xFFC02718  /* MXVR Interrupt Enable Register 0 */
#define	MXVR_INT_EN_1	      0xFFC0271C  /* MXVR Interrupt Enable Register 1 */

#define	MXVR_POSITION	      0xFFC02720  /* MXVR Node Position	Register */
#define	MXVR_MAX_POSITION     0xFFC02724  /* MXVR Maximum Node Position	Register */

#define	MXVR_DELAY	      0xFFC02728  /* MXVR Node Frame Delay Register */
#define	MXVR_MAX_DELAY	      0xFFC0272C  /* MXVR Maximum Node Frame Delay Register */

#define	MXVR_LADDR	      0xFFC02730  /* MXVR Logical Address Register */
#define	MXVR_GADDR	      0xFFC02734  /* MXVR Group	Address	Register */
#define	MXVR_AADDR	      0xFFC02738  /* MXVR Alternate Address Register */

#define	MXVR_ALLOC_0	      0xFFC0273C  /* MXVR Allocation Table Register 0 */
#define	MXVR_ALLOC_1	      0xFFC02740  /* MXVR Allocation Table Register 1 */
#define	MXVR_ALLOC_2	      0xFFC02744  /* MXVR Allocation Table Register 2 */
#define	MXVR_ALLOC_3	      0xFFC02748  /* MXVR Allocation Table Register 3 */
#define	MXVR_ALLOC_4	      0xFFC0274C  /* MXVR Allocation Table Register 4 */
#define	MXVR_ALLOC_5	      0xFFC02750  /* MXVR Allocation Table Register 5 */
#define	MXVR_ALLOC_6	      0xFFC02754  /* MXVR Allocation Table Register 6 */
#define	MXVR_ALLOC_7	      0xFFC02758  /* MXVR Allocation Table Register 7 */
#define	MXVR_ALLOC_8	      0xFFC0275C  /* MXVR Allocation Table Register 8 */
#define	MXVR_ALLOC_9	      0xFFC02760  /* MXVR Allocation Table Register 9 */
#define	MXVR_ALLOC_10	      0xFFC02764  /* MXVR Allocation Table Register 10 */
#define	MXVR_ALLOC_11	      0xFFC02768  /* MXVR Allocation Table Register 11 */
#define	MXVR_ALLOC_12	      0xFFC0276C  /* MXVR Allocation Table Register 12 */
#define	MXVR_ALLOC_13	      0xFFC02770  /* MXVR Allocation Table Register 13 */
#define	MXVR_ALLOC_14	      0xFFC02774  /* MXVR Allocation Table Register 14 */

#define	MXVR_SYNC_LCHAN_0     0xFFC02778  /* MXVR Sync Data Logical Channel Assign Register 0 */
#define	MXVR_SYNC_LCHAN_1     0xFFC0277C  /* MXVR Sync Data Logical Channel Assign Register 1 */
#define	MXVR_SYNC_LCHAN_2     0xFFC02780  /* MXVR Sync Data Logical Channel Assign Register 2 */
#define	MXVR_SYNC_LCHAN_3     0xFFC02784  /* MXVR Sync Data Logical Channel Assign Register 3 */
#define	MXVR_SYNC_LCHAN_4     0xFFC02788  /* MXVR Sync Data Logical Channel Assign Register 4 */
#define	MXVR_SYNC_LCHAN_5     0xFFC0278C  /* MXVR Sync Data Logical Channel Assign Register 5 */
#define	MXVR_SYNC_LCHAN_6     0xFFC02790  /* MXVR Sync Data Logical Channel Assign Register 6 */
#define	MXVR_SYNC_LCHAN_7     0xFFC02794  /* MXVR Sync Data Logical Channel Assign Register 7 */

#define	MXVR_DMA0_CONFIG      0xFFC02798  /* MXVR Sync Data DMA0 Config	Register */
#define	MXVR_DMA0_START_ADDR  0xFFC0279C  /* MXVR Sync Data DMA0 Start Address Register */
#define	MXVR_DMA0_COUNT	      0xFFC027A0  /* MXVR Sync Data DMA0 Loop Count Register */
#define	MXVR_DMA0_CURR_ADDR   0xFFC027A4  /* MXVR Sync Data DMA0 Current Address Register */
#define	MXVR_DMA0_CURR_COUNT  0xFFC027A8  /* MXVR Sync Data DMA0 Current Loop Count Register */

#define	MXVR_DMA1_CONFIG      0xFFC027AC  /* MXVR Sync Data DMA1 Config	Register */
#define	MXVR_DMA1_START_ADDR  0xFFC027B0  /* MXVR Sync Data DMA1 Start Address Register */
#define	MXVR_DMA1_COUNT	      0xFFC027B4  /* MXVR Sync Data DMA1 Loop Count Register */
#define	MXVR_DMA1_CURR_ADDR   0xFFC027B8  /* MXVR Sync Data DMA1 Current Address Register */
#define	MXVR_DMA1_CURR_COUNT  0xFFC027BC  /* MXVR Sync Data DMA1 Current Loop Count Register */

#define	MXVR_DMA2_CONFIG      0xFFC027C0  /* MXVR Sync Data DMA2 Config	Register */
#define	MXVR_DMA2_START_ADDR  0xFFC027C4  /* MXVR Sync Data DMA2 Start Address Register */
#define	MXVR_DMA2_COUNT	      0xFFC027C8  /* MXVR Sync Data DMA2 Loop Count Register */
#define	MXVR_DMA2_CURR_ADDR   0xFFC027CC  /* MXVR Sync Data DMA2 Current Address Register */
#define	MXVR_DMA2_CURR_COUNT  0xFFC027D0  /* MXVR Sync Data DMA2 Current Loop Count Register */

#define	MXVR_DMA3_CONFIG      0xFFC027D4  /* MXVR Sync Data DMA3 Config	Register */
#define	MXVR_DMA3_START_ADDR  0xFFC027D8  /* MXVR Sync Data DMA3 Start Address Register */
#define	MXVR_DMA3_COUNT	      0xFFC027DC  /* MXVR Sync Data DMA3 Loop Count Register */
#define	MXVR_DMA3_CURR_ADDR   0xFFC027E0  /* MXVR Sync Data DMA3 Current Address Register */
#define	MXVR_DMA3_CURR_COUNT  0xFFC027E4  /* MXVR Sync Data DMA3 Current Loop Count Register */

#define	MXVR_DMA4_CONFIG      0xFFC027E8  /* MXVR Sync Data DMA4 Config	Register */
#define	MXVR_DMA4_START_ADDR  0xFFC027EC  /* MXVR Sync Data DMA4 Start Address Register */
#define	MXVR_DMA4_COUNT	      0xFFC027F0  /* MXVR Sync Data DMA4 Loop Count Register */
#define	MXVR_DMA4_CURR_ADDR   0xFFC027F4  /* MXVR Sync Data DMA4 Current Address Register */
#define	MXVR_DMA4_CURR_COUNT  0xFFC027F8  /* MXVR Sync Data DMA4 Current Loop Count Register */

#define	MXVR_DMA5_CONFIG      0xFFC027FC  /* MXVR Sync Data DMA5 Config	Register */
#define	MXVR_DMA5_START_ADDR  0xFFC02800  /* MXVR Sync Data DMA5 Start Address Register */
#define	MXVR_DMA5_COUNT	      0xFFC02804  /* MXVR Sync Data DMA5 Loop Count Register */
#define	MXVR_DMA5_CURR_ADDR   0xFFC02808  /* MXVR Sync Data DMA5 Current Address Register */
#define	MXVR_DMA5_CURR_COUNT  0xFFC0280C  /* MXVR Sync Data DMA5 Current Loop Count Register */

#define	MXVR_DMA6_CONFIG      0xFFC02810  /* MXVR Sync Data DMA6 Config	Register */
#define	MXVR_DMA6_START_ADDR  0xFFC02814  /* MXVR Sync Data DMA6 Start Address Register */
#define	MXVR_DMA6_COUNT	      0xFFC02818  /* MXVR Sync Data DMA6 Loop Count Register */
#define	MXVR_DMA6_CURR_ADDR   0xFFC0281C  /* MXVR Sync Data DMA6 Current Address Register */
#define	MXVR_DMA6_CURR_COUNT  0xFFC02820  /* MXVR Sync Data DMA6 Current Loop Count Register */

#define	MXVR_DMA7_CONFIG      0xFFC02824  /* MXVR Sync Data DMA7 Config	Register */
#define	MXVR_DMA7_START_ADDR  0xFFC02828  /* MXVR Sync Data DMA7 Start Address Register */
#define	MXVR_DMA7_COUNT	      0xFFC0282C  /* MXVR Sync Data DMA7 Loop Count Register */
#define	MXVR_DMA7_CURR_ADDR   0xFFC02830  /* MXVR Sync Data DMA7 Current Address Register */
#define	MXVR_DMA7_CURR_COUNT  0xFFC02834  /* MXVR Sync Data DMA7 Current Loop Count Register */

#define	MXVR_AP_CTL	      0xFFC02838  /* MXVR Async	Packet Control Register */
#define	MXVR_APRB_START_ADDR  0xFFC0283C  /* MXVR Async	Packet RX Buffer Start Addr Register */
#define	MXVR_APRB_CURR_ADDR   0xFFC02840  /* MXVR Async	Packet RX Buffer Current Addr Register */
#define	MXVR_APTB_START_ADDR  0xFFC02844  /* MXVR Async	Packet TX Buffer Start Addr Register */
#define	MXVR_APTB_CURR_ADDR   0xFFC02848  /* MXVR Async	Packet TX Buffer Current Addr Register */

#define	MXVR_CM_CTL	      0xFFC0284C  /* MXVR Control Message Control Register */
#define	MXVR_CMRB_START_ADDR  0xFFC02850  /* MXVR Control Message RX Buffer Start Addr Register */
#define	MXVR_CMRB_CURR_ADDR   0xFFC02854  /* MXVR Control Message RX Buffer Current Address */
#define	MXVR_CMTB_START_ADDR  0xFFC02858  /* MXVR Control Message TX Buffer Start Addr Register */
#define	MXVR_CMTB_CURR_ADDR   0xFFC0285C  /* MXVR Control Message TX Buffer Current Address */

#define	MXVR_RRDB_START_ADDR  0xFFC02860  /* MXVR Remote Read Buffer Start Addr	Register */
#define	MXVR_RRDB_CURR_ADDR   0xFFC02864  /* MXVR Remote Read Buffer Current Addr Register */

#define	MXVR_PAT_DATA_0	      0xFFC02868  /* MXVR Pattern Data Register	0 */
#define	MXVR_PAT_EN_0	      0xFFC0286C  /* MXVR Pattern Enable Register 0 */
#define	MXVR_PAT_DATA_1	      0xFFC02870  /* MXVR Pattern Data Register	1 */
#define	MXVR_PAT_EN_1	      0xFFC02874  /* MXVR Pattern Enable Register 1 */

#define	MXVR_FRAME_CNT_0      0xFFC02878  /* MXVR Frame	Counter	0 */
#define	MXVR_FRAME_CNT_1      0xFFC0287C  /* MXVR Frame	Counter	1 */

#define	MXVR_ROUTING_0	      0xFFC02880  /* MXVR Routing Table	Register 0 */
#define	MXVR_ROUTING_1	      0xFFC02884  /* MXVR Routing Table	Register 1 */
#define	MXVR_ROUTING_2	      0xFFC02888  /* MXVR Routing Table	Register 2 */
#define	MXVR_ROUTING_3	      0xFFC0288C  /* MXVR Routing Table	Register 3 */
#define	MXVR_ROUTING_4	      0xFFC02890  /* MXVR Routing Table	Register 4 */
#define	MXVR_ROUTING_5	      0xFFC02894  /* MXVR Routing Table	Register 5 */
#define	MXVR_ROUTING_6	      0xFFC02898  /* MXVR Routing Table	Register 6 */
#define	MXVR_ROUTING_7	      0xFFC0289C  /* MXVR Routing Table	Register 7 */
#define	MXVR_ROUTING_8	      0xFFC028A0  /* MXVR Routing Table	Register 8 */
#define	MXVR_ROUTING_9	      0xFFC028A4  /* MXVR Routing Table	Register 9 */
#define	MXVR_ROUTING_10	      0xFFC028A8  /* MXVR Routing Table	Register 10 */
#define	MXVR_ROUTING_11	      0xFFC028AC  /* MXVR Routing Table	Register 11 */
#define	MXVR_ROUTING_12	      0xFFC028B0  /* MXVR Routing Table	Register 12 */
#define	MXVR_ROUTING_13	      0xFFC028B4  /* MXVR Routing Table	Register 13 */
#define	MXVR_ROUTING_14	      0xFFC028B8  /* MXVR Routing Table	Register 14 */

#define	MXVR_PLL_CTL_1	      0xFFC028BC  /* MXVR Phase	Lock Loop Control Register 1 */
#define	MXVR_BLOCK_CNT	      0xFFC028C0  /* MXVR Block	Counter */
#define	MXVR_PLL_CTL_2	      0xFFC028C4  /* MXVR Phase	Lock Loop Control Register 2 */

#endif /* _DEF_BF539_H */

/*
 * Copyright 2007-2010 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF549_H
#define _DEF_BF549_H

/* Include defBF54x_base.h for the set of #defines that are common to all ADSP-BF54x processors */
#include "defBF54x_base.h"

/* The BF549 is like the BF544, but has MXVR */
#include "defBF547.h"

/* MXVR Registers */

#define                      MXVR_CONFIG  0xffc02700   /* MXVR Configuration Register */
#define                     MXVR_STATE_0  0xffc02708   /* MXVR State Register 0 */
#define                     MXVR_STATE_1  0xffc0270c   /* MXVR State Register 1 */
#define                  MXVR_INT_STAT_0  0xffc02710   /* MXVR Interrupt Status Register 0 */
#define                  MXVR_INT_STAT_1  0xffc02714   /* MXVR Interrupt Status Register 1 */
#define                    MXVR_INT_EN_0  0xffc02718   /* MXVR Interrupt Enable Register 0 */
#define                    MXVR_INT_EN_1  0xffc0271c   /* MXVR Interrupt Enable Register 1 */
#define                    MXVR_POSITION  0xffc02720   /* MXVR Node Position Register */
#define                MXVR_MAX_POSITION  0xffc02724   /* MXVR Maximum Node Position Register */
#define                       MXVR_DELAY  0xffc02728   /* MXVR Node Frame Delay Register */
#define                   MXVR_MAX_DELAY  0xffc0272c   /* MXVR Maximum Node Frame Delay Register */
#define                       MXVR_LADDR  0xffc02730   /* MXVR Logical Address Register */
#define                       MXVR_GADDR  0xffc02734   /* MXVR Group Address Register */
#define                       MXVR_AADDR  0xffc02738   /* MXVR Alternate Address Register */

/* MXVR Allocation Table Registers */

#define                     MXVR_ALLOC_0  0xffc0273c   /* MXVR Allocation Table Register 0 */
#define                     MXVR_ALLOC_1  0xffc02740   /* MXVR Allocation Table Register 1 */
#define                     MXVR_ALLOC_2  0xffc02744   /* MXVR Allocation Table Register 2 */
#define                     MXVR_ALLOC_3  0xffc02748   /* MXVR Allocation Table Register 3 */
#define                     MXVR_ALLOC_4  0xffc0274c   /* MXVR Allocation Table Register 4 */
#define                     MXVR_ALLOC_5  0xffc02750   /* MXVR Allocation Table Register 5 */
#define                     MXVR_ALLOC_6  0xffc02754   /* MXVR Allocation Table Register 6 */
#define                     MXVR_ALLOC_7  0xffc02758   /* MXVR Allocation Table Register 7 */
#define                     MXVR_ALLOC_8  0xffc0275c   /* MXVR Allocation Table Register 8 */
#define                     MXVR_ALLOC_9  0xffc02760   /* MXVR Allocation Table Register 9 */
#define                    MXVR_ALLOC_10  0xffc02764   /* MXVR Allocation Table Register 10 */
#define                    MXVR_ALLOC_11  0xffc02768   /* MXVR Allocation Table Register 11 */
#define                    MXVR_ALLOC_12  0xffc0276c   /* MXVR Allocation Table Register 12 */
#define                    MXVR_ALLOC_13  0xffc02770   /* MXVR Allocation Table Register 13 */
#define                    MXVR_ALLOC_14  0xffc02774   /* MXVR Allocation Table Register 14 */

/* MXVR Channel Assign Registers */

#define                MXVR_SYNC_LCHAN_0  0xffc02778   /* MXVR Sync Data Logical Channel Assign Register 0 */
#define                MXVR_SYNC_LCHAN_1  0xffc0277c   /* MXVR Sync Data Logical Channel Assign Register 1 */
#define                MXVR_SYNC_LCHAN_2  0xffc02780   /* MXVR Sync Data Logical Channel Assign Register 2 */
#define                MXVR_SYNC_LCHAN_3  0xffc02784   /* MXVR Sync Data Logical Channel Assign Register 3 */
#define                MXVR_SYNC_LCHAN_4  0xffc02788   /* MXVR Sync Data Logical Channel Assign Register 4 */
#define                MXVR_SYNC_LCHAN_5  0xffc0278c   /* MXVR Sync Data Logical Channel Assign Register 5 */
#define                MXVR_SYNC_LCHAN_6  0xffc02790   /* MXVR Sync Data Logical Channel Assign Register 6 */
#define                MXVR_SYNC_LCHAN_7  0xffc02794   /* MXVR Sync Data Logical Channel Assign Register 7 */

/* MXVR DMA0 Registers */

#define                 MXVR_DMA0_CONFIG  0xffc02798   /* MXVR Sync Data DMA0 Config Register */
#define             MXVR_DMA0_START_ADDR  0xffc0279c   /* MXVR Sync Data DMA0 Start Address */
#define                  MXVR_DMA0_COUNT  0xffc027a0   /* MXVR Sync Data DMA0 Loop Count Register */
#define              MXVR_DMA0_CURR_ADDR  0xffc027a4   /* MXVR Sync Data DMA0 Current Address */
#define             MXVR_DMA0_CURR_COUNT  0xffc027a8   /* MXVR Sync Data DMA0 Current Loop Count */

/* MXVR DMA1 Registers */

#define                 MXVR_DMA1_CONFIG  0xffc027ac   /* MXVR Sync Data DMA1 Config Register */
#define             MXVR_DMA1_START_ADDR  0xffc027b0   /* MXVR Sync Data DMA1 Start Address */
#define                  MXVR_DMA1_COUNT  0xffc027b4   /* MXVR Sync Data DMA1 Loop Count Register */
#define              MXVR_DMA1_CURR_ADDR  0xffc027b8   /* MXVR Sync Data DMA1 Current Address */
#define             MXVR_DMA1_CURR_COUNT  0xffc027bc   /* MXVR Sync Data DMA1 Current Loop Count */

/* MXVR DMA2 Registers */

#define                 MXVR_DMA2_CONFIG  0xffc027c0   /* MXVR Sync Data DMA2 Config Register */
#define             MXVR_DMA2_START_ADDR  0xffc027c4   /* MXVR Sync Data DMA2 Start Address */
#define                  MXVR_DMA2_COUNT  0xffc027c8   /* MXVR Sync Data DMA2 Loop Count Register */
#define              MXVR_DMA2_CURR_ADDR  0xffc027cc   /* MXVR Sync Data DMA2 Current Address */
#define             MXVR_DMA2_CURR_COUNT  0xffc027d0   /* MXVR Sync Data DMA2 Current Loop Count */

/* MXVR DMA3 Registers */

#define                 MXVR_DMA3_CONFIG  0xffc027d4   /* MXVR Sync Data DMA3 Config Register */
#define             MXVR_DMA3_START_ADDR  0xffc027d8   /* MXVR Sync Data DMA3 Start Address */
#define                  MXVR_DMA3_COUNT  0xffc027dc   /* MXVR Sync Data DMA3 Loop Count Register */
#define              MXVR_DMA3_CURR_ADDR  0xffc027e0   /* MXVR Sync Data DMA3 Current Address */
#define             MXVR_DMA3_CURR_COUNT  0xffc027e4   /* MXVR Sync Data DMA3 Current Loop Count */

/* MXVR DMA4 Registers */

#define                 MXVR_DMA4_CONFIG  0xffc027e8   /* MXVR Sync Data DMA4 Config Register */
#define             MXVR_DMA4_START_ADDR  0xffc027ec   /* MXVR Sync Data DMA4 Start Address */
#define                  MXVR_DMA4_COUNT  0xffc027f0   /* MXVR Sync Data DMA4 Loop Count Register */
#define              MXVR_DMA4_CURR_ADDR  0xffc027f4   /* MXVR Sync Data DMA4 Current Address */
#define             MXVR_DMA4_CURR_COUNT  0xffc027f8   /* MXVR Sync Data DMA4 Current Loop Count */

/* MXVR DMA5 Registers */

#define                 MXVR_DMA5_CONFIG  0xffc027fc   /* MXVR Sync Data DMA5 Config Register */
#define             MXVR_DMA5_START_ADDR  0xffc02800   /* MXVR Sync Data DMA5 Start Address */
#define                  MXVR_DMA5_COUNT  0xffc02804   /* MXVR Sync Data DMA5 Loop Count Register */
#define              MXVR_DMA5_CURR_ADDR  0xffc02808   /* MXVR Sync Data DMA5 Current Address */
#define             MXVR_DMA5_CURR_COUNT  0xffc0280c   /* MXVR Sync Data DMA5 Current Loop Count */

/* MXVR DMA6 Registers */

#define                 MXVR_DMA6_CONFIG  0xffc02810   /* MXVR Sync Data DMA6 Config Register */
#define             MXVR_DMA6_START_ADDR  0xffc02814   /* MXVR Sync Data DMA6 Start Address */
#define                  MXVR_DMA6_COUNT  0xffc02818   /* MXVR Sync Data DMA6 Loop Count Register */
#define              MXVR_DMA6_CURR_ADDR  0xffc0281c   /* MXVR Sync Data DMA6 Current Address */
#define             MXVR_DMA6_CURR_COUNT  0xffc02820   /* MXVR Sync Data DMA6 Current Loop Count */

/* MXVR DMA7 Registers */

#define                 MXVR_DMA7_CONFIG  0xffc02824   /* MXVR Sync Data DMA7 Config Register */
#define             MXVR_DMA7_START_ADDR  0xffc02828   /* MXVR Sync Data DMA7 Start Address */
#define                  MXVR_DMA7_COUNT  0xffc0282c   /* MXVR Sync Data DMA7 Loop Count Register */
#define              MXVR_DMA7_CURR_ADDR  0xffc02830   /* MXVR Sync Data DMA7 Current Address */
#define             MXVR_DMA7_CURR_COUNT  0xffc02834   /* MXVR Sync Data DMA7 Current Loop Count */

/* MXVR Asynch Packet Registers */

#define                      MXVR_AP_CTL  0xffc02838   /* MXVR Async Packet Control Register */
#define             MXVR_APRB_START_ADDR  0xffc0283c   /* MXVR Async Packet RX Buffer Start Addr Register */
#define              MXVR_APRB_CURR_ADDR  0xffc02840   /* MXVR Async Packet RX Buffer Current Addr Register */
#define             MXVR_APTB_START_ADDR  0xffc02844   /* MXVR Async Packet TX Buffer Start Addr Register */
#define              MXVR_APTB_CURR_ADDR  0xffc02848   /* MXVR Async Packet TX Buffer Current Addr Register */

/* MXVR Control Message Registers */

#define                      MXVR_CM_CTL  0xffc0284c   /* MXVR Control Message Control Register */
#define             MXVR_CMRB_START_ADDR  0xffc02850   /* MXVR Control Message RX Buffer Start Addr Register */
#define              MXVR_CMRB_CURR_ADDR  0xffc02854   /* MXVR Control Message RX Buffer Current Address */
#define             MXVR_CMTB_START_ADDR  0xffc02858   /* MXVR Control Message TX Buffer Start Addr Register */
#define              MXVR_CMTB_CURR_ADDR  0xffc0285c   /* MXVR Control Message TX Buffer Current Address */

/* MXVR Remote Read Registers */

#define             MXVR_RRDB_START_ADDR  0xffc02860   /* MXVR Remote Read Buffer Start Addr Register */
#define              MXVR_RRDB_CURR_ADDR  0xffc02864   /* MXVR Remote Read Buffer Current Addr Register */

/* MXVR Pattern Data Registers */

#define                  MXVR_PAT_DATA_0  0xffc02868   /* MXVR Pattern Data Register 0 */
#define                    MXVR_PAT_EN_0  0xffc0286c   /* MXVR Pattern Enable Register 0 */
#define                  MXVR_PAT_DATA_1  0xffc02870   /* MXVR Pattern Data Register 1 */
#define                    MXVR_PAT_EN_1  0xffc02874   /* MXVR Pattern Enable Register 1 */

/* MXVR Frame Counter Registers */

#define                 MXVR_FRAME_CNT_0  0xffc02878   /* MXVR Frame Counter 0 */
#define                 MXVR_FRAME_CNT_1  0xffc0287c   /* MXVR Frame Counter 1 */

/* MXVR Routing Table Registers */

#define                   MXVR_ROUTING_0  0xffc02880   /* MXVR Routing Table Register 0 */
#define                   MXVR_ROUTING_1  0xffc02884   /* MXVR Routing Table Register 1 */
#define                   MXVR_ROUTING_2  0xffc02888   /* MXVR Routing Table Register 2 */
#define                   MXVR_ROUTING_3  0xffc0288c   /* MXVR Routing Table Register 3 */
#define                   MXVR_ROUTING_4  0xffc02890   /* MXVR Routing Table Register 4 */
#define                   MXVR_ROUTING_5  0xffc02894   /* MXVR Routing Table Register 5 */
#define                   MXVR_ROUTING_6  0xffc02898   /* MXVR Routing Table Register 6 */
#define                   MXVR_ROUTING_7  0xffc0289c   /* MXVR Routing Table Register 7 */
#define                   MXVR_ROUTING_8  0xffc028a0   /* MXVR Routing Table Register 8 */
#define                   MXVR_ROUTING_9  0xffc028a4   /* MXVR Routing Table Register 9 */
#define                  MXVR_ROUTING_10  0xffc028a8   /* MXVR Routing Table Register 10 */
#define                  MXVR_ROUTING_11  0xffc028ac   /* MXVR Routing Table Register 11 */
#define                  MXVR_ROUTING_12  0xffc028b0   /* MXVR Routing Table Register 12 */
#define                  MXVR_ROUTING_13  0xffc028b4   /* MXVR Routing Table Register 13 */
#define                  MXVR_ROUTING_14  0xffc028b8   /* MXVR Routing Table Register 14 */

/* MXVR Counter-Clock-Control Registers */

#define                   MXVR_BLOCK_CNT  0xffc028c0   /* MXVR Block Counter */
#define                     MXVR_CLK_CTL  0xffc028d0   /* MXVR Clock Control Register */
#define                  MXVR_CDRPLL_CTL  0xffc028d4   /* MXVR Clock/Data Recovery PLL Control Register */
#define                   MXVR_FMPLL_CTL  0xffc028d8   /* MXVR Frequency Multiply PLL Control Register */
#define                     MXVR_PIN_CTL  0xffc028dc   /* MXVR Pin Control Register */
#define                    MXVR_SCLK_CNT  0xffc028e0   /* MXVR System Clock Counter Register */

#endif /* _DEF_BF549_H */

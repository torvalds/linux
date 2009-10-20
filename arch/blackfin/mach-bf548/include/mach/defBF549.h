/*
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF549_H
#define _DEF_BF549_H

/* Include all Core registers and bit definitions */
#include <asm/def_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF549 */

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

/* Bit masks for MXVR_CONFIG */

#define                    MXVREN  0x1        /* MXVR Enable */
#define                      MMSM  0x2        /* MXVR Master/Slave Mode Select */
#define                    ACTIVE  0x4        /* Active Mode */
#define                    SDELAY  0x8        /* Synchronous Data Delay */
#define                   NCMRXEN  0x10       /* Normal Control Message Receive Enable */
#define                   RWRRXEN  0x20       /* Remote Write Receive Enable */
#define                     MTXEN  0x40       /* MXVR Transmit Data Enable */
#define                    MTXONB  0x80       /* MXVR Phy Transmitter On */
#define                   EPARITY  0x100      /* Even Parity Select */
#define                       MSB  0x1e00     /* Master Synchronous Boundary */
#define                    APRXEN  0x2000     /* Asynchronous Packet Receive Enable */
#define                    WAKEUP  0x4000     /* Wake-Up */
#define                     LMECH  0x8000     /* Lock Mechanism Select */

/* Bit masks for MXVR_STATE_0 */

#define                      NACT  0x1        /* Network Activity */
#define                    SBLOCK  0x2        /* Super Block Lock */
#define                   FMPLLST  0xc        /* Frequency Multiply PLL SM State */
#define                  CDRPLLST  0xe0       /* Clock/Data Recovery PLL SM State */
#define                     APBSY  0x100      /* Asynchronous Packet Transmit Buffer Busy */
#define                     APARB  0x200      /* Asynchronous Packet Arbitrating */
#define                      APTX  0x400      /* Asynchronous Packet Transmitting */
#define                      APRX  0x800      /* Receiving Asynchronous Packet */
#define                     CMBSY  0x1000     /* Control Message Transmit Buffer Busy */
#define                     CMARB  0x2000     /* Control Message Arbitrating */
#define                      CMTX  0x4000     /* Control Message Transmitting */
#define                      CMRX  0x8000     /* Receiving Control Message */
#define                    MRXONB  0x10000    /* MRXONB Pin State */
#define                     RGSIP  0x20000    /* Remote Get Source In Progress */
#define                     DALIP  0x40000    /* Resource Deallocate In Progress */
#define                      ALIP  0x80000    /* Resource Allocate In Progress */
#define                     RRDIP  0x100000   /* Remote Read In Progress */
#define                     RWRIP  0x200000   /* Remote Write In Progress */
#define                     FLOCK  0x400000   /* Frame Lock */
#define                     BLOCK  0x800000   /* Block Lock */
#define                       RSB  0xf000000  /* Received Synchronous Boundary */
#define                   DERRNUM  0xf0000000 /* DMA Error Channel Number */

/* Bit masks for MXVR_STATE_1 */

#define                   SRXNUMB  0xf        /* Synchronous Receive FIFO Number of Bytes */
#define                   STXNUMB  0xf0       /* Synchronous Transmit FIFO Number of Bytes */
#define                    APCONT  0x100      /* Asynchronous Packet Continuation */
#define                  OBERRNUM  0xe00      /* DMA Out of Bounds Error Channel Number */
#define                DMAACTIVE0  0x10000    /* DMA0 Active */
#define                DMAACTIVE1  0x20000    /* DMA1 Active */
#define                DMAACTIVE2  0x40000    /* DMA2 Active */
#define                DMAACTIVE3  0x80000    /* DMA3 Active */
#define                DMAACTIVE4  0x100000   /* DMA4 Active */
#define                DMAACTIVE5  0x200000   /* DMA5 Active */
#define                DMAACTIVE6  0x400000   /* DMA6 Active */
#define                DMAACTIVE7  0x800000   /* DMA7 Active */
#define                  DMAPMEN0  0x1000000  /* DMA0 Pattern Matching Enabled */
#define                  DMAPMEN1  0x2000000  /* DMA1 Pattern Matching Enabled */
#define                  DMAPMEN2  0x4000000  /* DMA2 Pattern Matching Enabled */
#define                  DMAPMEN3  0x8000000  /* DMA3 Pattern Matching Enabled */
#define                  DMAPMEN4  0x10000000 /* DMA4 Pattern Matching Enabled */
#define                  DMAPMEN5  0x20000000 /* DMA5 Pattern Matching Enabled */
#define                  DMAPMEN6  0x40000000 /* DMA6 Pattern Matching Enabled */
#define                  DMAPMEN7  0x80000000 /* DMA7 Pattern Matching Enabled */

/* Bit masks for MXVR_INT_STAT_0 */

#define                      NI2A  0x1        /* Network Inactive to Active */
#define                      NA2I  0x2        /* Network Active to Inactive */
#define                     SBU2L  0x4        /* Super Block Unlock to Lock */
#define                     SBL2U  0x8        /* Super Block Lock to Unlock */
#define                       PRU  0x10       /* Position Register Updated */
#define                      MPRU  0x20       /* Maximum Position Register Updated */
#define                       DRU  0x40       /* Delay Register Updated */
#define                      MDRU  0x80       /* Maximum Delay Register Updated */
#define                       SBU  0x100      /* Synchronous Boundary Updated */
#define                       ATU  0x200      /* Allocation Table Updated */
#define                      FCZ0  0x400      /* Frame Counter 0 Zero */
#define                      FCZ1  0x800      /* Frame Counter 1 Zero */
#define                      PERR  0x1000     /* Parity Error */
#define                      MH2L  0x2000     /* MRXONB High to Low */
#define                      ML2H  0x4000     /* MRXONB Low to High */
#define                       WUP  0x8000     /* Wake-Up Preamble Received */
#define                      FU2L  0x10000    /* Frame Unlock to Lock */
#define                      FL2U  0x20000    /* Frame Lock to Unlock */
#define                      BU2L  0x40000    /* Block Unlock to Lock */
#define                      BL2U  0x80000    /* Block Lock to Unlock */
#define                     OBERR  0x100000   /* DMA Out of Bounds Error */
#define                       PFL  0x200000   /* PLL Frequency Locked */
#define                       SCZ  0x400000   /* System Clock Counter Zero */
#define                      FERR  0x800000   /* FIFO Error */
#define                       CMR  0x1000000  /* Control Message Received */
#define                     CMROF  0x2000000  /* Control Message Receive Buffer Overflow */
#define                      CMTS  0x4000000  /* Control Message Transmit Buffer Successfully Sent */
#define                      CMTC  0x8000000  /* Control Message Transmit Buffer Successfully Cancelled */
#define                      RWRC  0x10000000 /* Remote Write Control Message Completed */
#define                       BCZ  0x20000000 /* Block Counter Zero */
#define                     BMERR  0x40000000 /* Biphase Mark Coding Error */
#define                      DERR  0x80000000 /* DMA Error */

/* Bit masks for MXVR_INT_STAT_1 */

#define                    HDONE0  0x1        /* DMA0 Half Done */
#define                     DONE0  0x2        /* DMA0 Done */
#define                       APR  0x4        /* Asynchronous Packet Received */
#define                     APROF  0x8        /* Asynchronous Packet Receive Buffer Overflow */
#define                    HDONE1  0x10       /* DMA1 Half Done */
#define                     DONE1  0x20       /* DMA1 Done */
#define                      APTS  0x40       /* Asynchronous Packet Transmit Buffer Successfully Sent */
#define                      APTC  0x80       /* Asynchronous Packet Transmit Buffer Successfully Cancelled */
#define                    HDONE2  0x100      /* DMA2 Half Done */
#define                     DONE2  0x200      /* DMA2 Done */
#define                     APRCE  0x400      /* Asynchronous Packet Receive CRC Error */
#define                     APRPE  0x800      /* Asynchronous Packet Receive Packet Error */
#define                    HDONE3  0x1000     /* DMA3 Half Done */
#define                     DONE3  0x2000     /* DMA3 Done */
#define                    HDONE4  0x10000    /* DMA4 Half Done */
#define                     DONE4  0x20000    /* DMA4 Done */
#define                    HDONE5  0x100000   /* DMA5 Half Done */
#define                     DONE5  0x200000   /* DMA5 Done */
#define                    HDONE6  0x1000000  /* DMA6 Half Done */
#define                     DONE6  0x2000000  /* DMA6 Done */
#define                    HDONE7  0x10000000 /* DMA7 Half Done */
#define                     DONE7  0x20000000 /* DMA7 Done */

/* Bit masks for MXVR_INT_EN_0 */

#define                    NI2AEN  0x1        /* Network Inactive to Active Interrupt Enable */
#define                    NA2IEN  0x2        /* Network Active to Inactive Interrupt Enable */
#define                   SBU2LEN  0x4        /* Super Block Unlock to Lock Interrupt Enable */
#define                   SBL2UEN  0x8        /* Super Block Lock to Unlock Interrupt Enable */
#define                     PRUEN  0x10       /* Position Register Updated Interrupt Enable */
#define                    MPRUEN  0x20       /* Maximum Position Register Updated Interrupt Enable */
#define                     DRUEN  0x40       /* Delay Register Updated Interrupt Enable */
#define                    MDRUEN  0x80       /* Maximum Delay Register Updated Interrupt Enable */
#define                     SBUEN  0x100      /* Synchronous Boundary Updated Interrupt Enable */
#define                     ATUEN  0x200      /* Allocation Table Updated Interrupt Enable */
#define                    FCZ0EN  0x400      /* Frame Counter 0 Zero Interrupt Enable */
#define                    FCZ1EN  0x800      /* Frame Counter 1 Zero Interrupt Enable */
#define                    PERREN  0x1000     /* Parity Error Interrupt Enable */
#define                    MH2LEN  0x2000     /* MRXONB High to Low Interrupt Enable */
#define                    ML2HEN  0x4000     /* MRXONB Low to High Interrupt Enable */
#define                     WUPEN  0x8000     /* Wake-Up Preamble Received Interrupt Enable */
#define                    FU2LEN  0x10000    /* Frame Unlock to Lock Interrupt Enable */
#define                    FL2UEN  0x20000    /* Frame Lock to Unlock Interrupt Enable */
#define                    BU2LEN  0x40000    /* Block Unlock to Lock Interrupt Enable */
#define                    BL2UEN  0x80000    /* Block Lock to Unlock Interrupt Enable */
#define                   OBERREN  0x100000   /* DMA Out of Bounds Error Interrupt Enable */
#define                     PFLEN  0x200000   /* PLL Frequency Locked Interrupt Enable */
#define                     SCZEN  0x400000   /* System Clock Counter Zero Interrupt Enable */
#define                    FERREN  0x800000   /* FIFO Error Interrupt Enable */
#define                     CMREN  0x1000000  /* Control Message Received Interrupt Enable */
#define                   CMROFEN  0x2000000  /* Control Message Receive Buffer Overflow Interrupt Enable */
#define                    CMTSEN  0x4000000  /* Control Message Transmit Buffer Successfully Sent Interrupt Enable */
#define                    CMTCEN  0x8000000  /* Control Message Transmit Buffer Successfully Cancelled Interrupt Enable */
#define                    RWRCEN  0x10000000 /* Remote Write Control Message Completed Interrupt Enable */
#define                     BCZEN  0x20000000 /* Block Counter Zero Interrupt Enable */
#define                   BMERREN  0x40000000 /* Biphase Mark Coding Error Interrupt Enable */
#define                    DERREN  0x80000000 /* DMA Error Interrupt Enable */

/* Bit masks for MXVR_INT_EN_1 */

#define                  HDONEEN0  0x1        /* DMA0 Half Done Interrupt Enable */
#define                   DONEEN0  0x2        /* DMA0 Done Interrupt Enable */
#define                     APREN  0x4        /* Asynchronous Packet Received Interrupt Enable */
#define                   APROFEN  0x8        /* Asynchronous Packet Receive Buffer Overflow Interrupt Enable */
#define                  HDONEEN1  0x10       /* DMA1 Half Done Interrupt Enable */
#define                   DONEEN1  0x20       /* DMA1 Done Interrupt Enable */
#define                    APTSEN  0x40       /* Asynchronous Packet Transmit Buffer Successfully Sent Interrupt Enable */
#define                    APTCEN  0x80       /* Asynchronous Packet Transmit Buffer Successfully Cancelled Interrupt Enable */
#define                  HDONEEN2  0x100      /* DMA2 Half Done Interrupt Enable */
#define                   DONEEN2  0x200      /* DMA2 Done Interrupt Enable */
#define                   APRCEEN  0x400      /* Asynchronous Packet Receive CRC Error Interrupt Enable */
#define                   APRPEEN  0x800      /* Asynchronous Packet Receive Packet Error Interrupt Enable */
#define                  HDONEEN3  0x1000     /* DMA3 Half Done Interrupt Enable */
#define                   DONEEN3  0x2000     /* DMA3 Done Interrupt Enable */
#define                  HDONEEN4  0x10000    /* DMA4 Half Done Interrupt Enable */
#define                   DONEEN4  0x20000    /* DMA4 Done Interrupt Enable */
#define                  HDONEEN5  0x100000   /* DMA5 Half Done Interrupt Enable */
#define                   DONEEN5  0x200000   /* DMA5 Done Interrupt Enable */
#define                  HDONEEN6  0x1000000  /* DMA6 Half Done Interrupt Enable */
#define                   DONEEN6  0x2000000  /* DMA6 Done Interrupt Enable */
#define                  HDONEEN7  0x10000000 /* DMA7 Half Done Interrupt Enable */
#define                   DONEEN7  0x20000000 /* DMA7 Done Interrupt Enable */

/* Bit masks for MXVR_POSITION */

#define                  POSITION  0x3f       /* Node Position */
#define                    PVALID  0x8000     /* Node Position Valid */

/* Bit masks for MXVR_MAX_POSITION */

#define                 MPOSITION  0x3f       /* Maximum Node Position */
#define                   MPVALID  0x8000     /* Maximum Node Position Valid */

/* Bit masks for MXVR_DELAY */

#define                     DELAY  0x3f       /* Node Frame Delay */
#define                    DVALID  0x8000     /* Node Frame Delay Valid */

/* Bit masks for MXVR_MAX_DELAY */

#define                    MDELAY  0x3f       /* Maximum Node Frame Delay */
#define                   MDVALID  0x8000     /* Maximum Node Frame Delay Valid */

/* Bit masks for MXVR_LADDR */

#define                     LADDR  0xffff     /* Logical Address */
#define                    LVALID  0x80000000 /* Logical Address Valid */

/* Bit masks for MXVR_GADDR */

#define                    GADDRL  0xff       /* Group Address Lower Byte */
#define                    GVALID  0x8000     /* Group Address Valid */

/* Bit masks for MXVR_AADDR */

#define                     AADDR  0xffff     /* Alternate Address */
#define                    AVALID  0x80000000 /* Alternate Address Valid */

/* Bit masks for MXVR_ALLOC_0 */

#define                       CL0  0x7f       /* Channel 0 Connection Label */
#define                      CIU0  0x80       /* Channel 0 In Use */
#define                       CL1  0x7f00     /* Channel 0 Connection Label */
#define                      CIU1  0x8000     /* Channel 0 In Use */
#define                       CL2  0x7f0000   /* Channel 0 Connection Label */
#define                      CIU2  0x800000   /* Channel 0 In Use */
#define                       CL3  0x7f000000 /* Channel 0 Connection Label */
#define                      CIU3  0x80000000 /* Channel 0 In Use */

/* Bit masks for MXVR_ALLOC_1 */

#define                       CL4  0x7f       /* Channel 4 Connection Label */
#define                      CIU4  0x80       /* Channel 4 In Use */
#define                       CL5  0x7f00     /* Channel 5 Connection Label */
#define                      CIU5  0x8000     /* Channel 5 In Use */
#define                       CL6  0x7f0000   /* Channel 6 Connection Label */
#define                      CIU6  0x800000   /* Channel 6 In Use */
#define                       CL7  0x7f000000 /* Channel 7 Connection Label */
#define                      CIU7  0x80000000 /* Channel 7 In Use */

/* Bit masks for MXVR_ALLOC_2 */

#define                       CL8  0x7f       /* Channel 8 Connection Label */
#define                      CIU8  0x80       /* Channel 8 In Use */
#define                       CL9  0x7f00     /* Channel 9 Connection Label */
#define                      CIU9  0x8000     /* Channel 9 In Use */
#define                      CL10  0x7f0000   /* Channel 10 Connection Label */
#define                     CIU10  0x800000   /* Channel 10 In Use */
#define                      CL11  0x7f000000 /* Channel 11 Connection Label */
#define                     CIU11  0x80000000 /* Channel 11 In Use */

/* Bit masks for MXVR_ALLOC_3 */

#define                      CL12  0x7f       /* Channel 12 Connection Label */
#define                     CIU12  0x80       /* Channel 12 In Use */
#define                      CL13  0x7f00     /* Channel 13 Connection Label */
#define                     CIU13  0x8000     /* Channel 13 In Use */
#define                      CL14  0x7f0000   /* Channel 14 Connection Label */
#define                     CIU14  0x800000   /* Channel 14 In Use */
#define                      CL15  0x7f000000 /* Channel 15 Connection Label */
#define                     CIU15  0x80000000 /* Channel 15 In Use */

/* Bit masks for MXVR_ALLOC_4 */

#define                      CL16  0x7f       /* Channel 16 Connection Label */
#define                     CIU16  0x80       /* Channel 16 In Use */
#define                      CL17  0x7f00     /* Channel 17 Connection Label */
#define                     CIU17  0x8000     /* Channel 17 In Use */
#define                      CL18  0x7f0000   /* Channel 18 Connection Label */
#define                     CIU18  0x800000   /* Channel 18 In Use */
#define                      CL19  0x7f000000 /* Channel 19 Connection Label */
#define                     CIU19  0x80000000 /* Channel 19 In Use */

/* Bit masks for MXVR_ALLOC_5 */

#define                      CL20  0x7f       /* Channel 20 Connection Label */
#define                     CIU20  0x80       /* Channel 20 In Use */
#define                      CL21  0x7f00     /* Channel 21 Connection Label */
#define                     CIU21  0x8000     /* Channel 21 In Use */
#define                      CL22  0x7f0000   /* Channel 22 Connection Label */
#define                     CIU22  0x800000   /* Channel 22 In Use */
#define                      CL23  0x7f000000 /* Channel 23 Connection Label */
#define                     CIU23  0x80000000 /* Channel 23 In Use */

/* Bit masks for MXVR_ALLOC_6 */

#define                      CL24  0x7f       /* Channel 24 Connection Label */
#define                     CIU24  0x80       /* Channel 24 In Use */
#define                      CL25  0x7f00     /* Channel 25 Connection Label */
#define                     CIU25  0x8000     /* Channel 25 In Use */
#define                      CL26  0x7f0000   /* Channel 26 Connection Label */
#define                     CIU26  0x800000   /* Channel 26 In Use */
#define                      CL27  0x7f000000 /* Channel 27 Connection Label */
#define                     CIU27  0x80000000 /* Channel 27 In Use */

/* Bit masks for MXVR_ALLOC_7 */

#define                      CL28  0x7f       /* Channel 28 Connection Label */
#define                     CIU28  0x80       /* Channel 28 In Use */
#define                      CL29  0x7f00     /* Channel 29 Connection Label */
#define                     CIU29  0x8000     /* Channel 29 In Use */
#define                      CL30  0x7f0000   /* Channel 30 Connection Label */
#define                     CIU30  0x800000   /* Channel 30 In Use */
#define                      CL31  0x7f000000 /* Channel 31 Connection Label */
#define                     CIU31  0x80000000 /* Channel 31 In Use */

/* Bit masks for MXVR_ALLOC_8 */

#define                      CL32  0x7f       /* Channel 32 Connection Label */
#define                     CIU32  0x80       /* Channel 32 In Use */
#define                      CL33  0x7f00     /* Channel 33 Connection Label */
#define                     CIU33  0x8000     /* Channel 33 In Use */
#define                      CL34  0x7f0000   /* Channel 34 Connection Label */
#define                     CIU34  0x800000   /* Channel 34 In Use */
#define                      CL35  0x7f000000 /* Channel 35 Connection Label */
#define                     CIU35  0x80000000 /* Channel 35 In Use */

/* Bit masks for MXVR_ALLOC_9 */

#define                      CL36  0x7f       /* Channel 36 Connection Label */
#define                     CIU36  0x80       /* Channel 36 In Use */
#define                      CL37  0x7f00     /* Channel 37 Connection Label */
#define                     CIU37  0x8000     /* Channel 37 In Use */
#define                      CL38  0x7f0000   /* Channel 38 Connection Label */
#define                     CIU38  0x800000   /* Channel 38 In Use */
#define                      CL39  0x7f000000 /* Channel 39 Connection Label */
#define                     CIU39  0x80000000 /* Channel 39 In Use */

/* Bit masks for MXVR_ALLOC_10 */

#define                      CL40  0x7f       /* Channel 40 Connection Label */
#define                     CIU40  0x80       /* Channel 40 In Use */
#define                      CL41  0x7f00     /* Channel 41 Connection Label */
#define                     CIU41  0x8000     /* Channel 41 In Use */
#define                      CL42  0x7f0000   /* Channel 42 Connection Label */
#define                     CIU42  0x800000   /* Channel 42 In Use */
#define                      CL43  0x7f000000 /* Channel 43 Connection Label */
#define                     CIU43  0x80000000 /* Channel 43 In Use */

/* Bit masks for MXVR_ALLOC_11 */

#define                      CL44  0x7f       /* Channel 44 Connection Label */
#define                     CIU44  0x80       /* Channel 44 In Use */
#define                      CL45  0x7f00     /* Channel 45 Connection Label */
#define                     CIU45  0x8000     /* Channel 45 In Use */
#define                      CL46  0x7f0000   /* Channel 46 Connection Label */
#define                     CIU46  0x800000   /* Channel 46 In Use */
#define                      CL47  0x7f000000 /* Channel 47 Connection Label */
#define                     CIU47  0x80000000 /* Channel 47 In Use */

/* Bit masks for MXVR_ALLOC_12 */

#define                      CL48  0x7f       /* Channel 48 Connection Label */
#define                     CIU48  0x80       /* Channel 48 In Use */
#define                      CL49  0x7f00     /* Channel 49 Connection Label */
#define                     CIU49  0x8000     /* Channel 49 In Use */
#define                      CL50  0x7f0000   /* Channel 50 Connection Label */
#define                     CIU50  0x800000   /* Channel 50 In Use */
#define                      CL51  0x7f000000 /* Channel 51 Connection Label */
#define                     CIU51  0x80000000 /* Channel 51 In Use */

/* Bit masks for MXVR_ALLOC_13 */

#define                      CL52  0x7f       /* Channel 52 Connection Label */
#define                     CIU52  0x80       /* Channel 52 In Use */
#define                      CL53  0x7f00     /* Channel 53 Connection Label */
#define                     CIU53  0x8000     /* Channel 53 In Use */
#define                      CL54  0x7f0000   /* Channel 54 Connection Label */
#define                     CIU54  0x800000   /* Channel 54 In Use */
#define                      CL55  0x7f000000 /* Channel 55 Connection Label */
#define                     CIU55  0x80000000 /* Channel 55 In Use */

/* Bit masks for MXVR_ALLOC_14 */

#define                      CL56  0x7f       /* Channel 56 Connection Label */
#define                     CIU56  0x80       /* Channel 56 In Use */
#define                      CL57  0x7f00     /* Channel 57 Connection Label */
#define                     CIU57  0x8000     /* Channel 57 In Use */
#define                      CL58  0x7f0000   /* Channel 58 Connection Label */
#define                     CIU58  0x800000   /* Channel 58 In Use */
#define                      CL59  0x7f000000 /* Channel 59 Connection Label */
#define                     CIU59  0x80000000 /* Channel 59 In Use */

/* MXVR_SYNC_LCHAN_0 Masks */

#define LCHANPC0     0x0000000Flu
#define LCHANPC1     0x000000F0lu
#define LCHANPC2     0x00000F00lu
#define LCHANPC3     0x0000F000lu
#define LCHANPC4     0x000F0000lu
#define LCHANPC5     0x00F00000lu
#define LCHANPC6     0x0F000000lu
#define LCHANPC7     0xF0000000lu


/* MXVR_SYNC_LCHAN_1 Masks */

#define LCHANPC8     0x0000000Flu
#define LCHANPC9     0x000000F0lu
#define LCHANPC10    0x00000F00lu
#define LCHANPC11    0x0000F000lu
#define LCHANPC12    0x000F0000lu
#define LCHANPC13    0x00F00000lu
#define LCHANPC14    0x0F000000lu
#define LCHANPC15    0xF0000000lu


/* MXVR_SYNC_LCHAN_2 Masks */

#define LCHANPC16    0x0000000Flu
#define LCHANPC17    0x000000F0lu
#define LCHANPC18    0x00000F00lu
#define LCHANPC19    0x0000F000lu
#define LCHANPC20    0x000F0000lu
#define LCHANPC21    0x00F00000lu
#define LCHANPC22    0x0F000000lu
#define LCHANPC23    0xF0000000lu


/* MXVR_SYNC_LCHAN_3 Masks */

#define LCHANPC24    0x0000000Flu
#define LCHANPC25    0x000000F0lu
#define LCHANPC26    0x00000F00lu
#define LCHANPC27    0x0000F000lu
#define LCHANPC28    0x000F0000lu
#define LCHANPC29    0x00F00000lu
#define LCHANPC30    0x0F000000lu
#define LCHANPC31    0xF0000000lu


/* MXVR_SYNC_LCHAN_4 Masks */

#define LCHANPC32    0x0000000Flu
#define LCHANPC33    0x000000F0lu
#define LCHANPC34    0x00000F00lu
#define LCHANPC35    0x0000F000lu
#define LCHANPC36    0x000F0000lu
#define LCHANPC37    0x00F00000lu
#define LCHANPC38    0x0F000000lu
#define LCHANPC39    0xF0000000lu


/* MXVR_SYNC_LCHAN_5 Masks */

#define LCHANPC40    0x0000000Flu
#define LCHANPC41    0x000000F0lu
#define LCHANPC42    0x00000F00lu
#define LCHANPC43    0x0000F000lu
#define LCHANPC44    0x000F0000lu
#define LCHANPC45    0x00F00000lu
#define LCHANPC46    0x0F000000lu
#define LCHANPC47    0xF0000000lu


/* MXVR_SYNC_LCHAN_6 Masks */

#define LCHANPC48    0x0000000Flu
#define LCHANPC49    0x000000F0lu
#define LCHANPC50    0x00000F00lu
#define LCHANPC51    0x0000F000lu
#define LCHANPC52    0x000F0000lu
#define LCHANPC53    0x00F00000lu
#define LCHANPC54    0x0F000000lu
#define LCHANPC55    0xF0000000lu


/* MXVR_SYNC_LCHAN_7 Masks */

#define LCHANPC56    0x0000000Flu
#define LCHANPC57    0x000000F0lu
#define LCHANPC58    0x00000F00lu
#define LCHANPC59    0x0000F000lu

/* Bit masks for MXVR_DMAx_CONFIG */

#define                    MDMAEN  0x1        /* DMA Channel Enable */
#define                     DMADD  0x2        /* DMA Channel Direction */
#define                 BY4SWAPEN  0x20       /* DMA Channel Four Byte Swap Enable */
#define                     LCHAN  0x3c0      /* DMA Channel Logical Channel */
#define                 BITSWAPEN  0x400      /* DMA Channel Bit Swap Enable */
#define                 BY2SWAPEN  0x800      /* DMA Channel Two Byte Swap Enable */
#define                     MFLOW  0x7000     /* DMA Channel Operation Flow */
#define                   FIXEDPM  0x80000    /* DMA Channel Fixed Pattern Matching Select */
#define                  STARTPAT  0x300000   /* DMA Channel Start Pattern Select */
#define                   STOPPAT  0xc00000   /* DMA Channel Stop Pattern Select */
#define                  COUNTPOS  0x1c000000 /* DMA Channel Count Position */

/* Bit masks for MXVR_AP_CTL */

#define                   STARTAP  0x1        /* Start Asynchronous Packet Transmission */
#define                  CANCELAP  0x2        /* Cancel Asynchronous Packet Transmission */
#define                   RESETAP  0x4        /* Reset Asynchronous Packet Arbitration */
#define                    APRBE0  0x4000     /* Asynchronous Packet Receive Buffer Entry 0 */
#define                    APRBE1  0x8000     /* Asynchronous Packet Receive Buffer Entry 1 */

/* Bit masks for MXVR_APRB_START_ADDR */

#define      MXVR_APRB_START_ADDR_MASK  0x1fffffe  /* Asynchronous Packet Receive Buffer Start Address */

/* Bit masks for MXVR_APRB_CURR_ADDR */

#define       MXVR_APRB_CURR_ADDR_MASK  0xffffffff /* Asynchronous Packet Receive Buffer Current Address */

/* Bit masks for MXVR_APTB_START_ADDR */

#define       MXVR_APTB_START_ADDR_MASK  0x1fffffe  /* Asynchronous Packet Transmit Buffer Start Address */

/* Bit masks for MXVR_APTB_CURR_ADDR */

#define        MXVR_APTB_CURR_ADDR_MASK  0xffffffff /* Asynchronous Packet Transmit Buffer Current Address */

/* Bit masks for MXVR_CM_CTL */

#define                   STARTCM  0x1        /* Start Control Message Transmission */
#define                  CANCELCM  0x2        /* Cancel Control Message Transmission */
#define                    CMRBE0  0x10000    /* Control Message Receive Buffer Entry 0 */
#define                    CMRBE1  0x20000    /* Control Message Receive Buffer Entry 1 */
#define                    CMRBE2  0x40000    /* Control Message Receive Buffer Entry 2 */
#define                    CMRBE3  0x80000    /* Control Message Receive Buffer Entry 3 */
#define                    CMRBE4  0x100000   /* Control Message Receive Buffer Entry 4 */
#define                    CMRBE5  0x200000   /* Control Message Receive Buffer Entry 5 */
#define                    CMRBE6  0x400000   /* Control Message Receive Buffer Entry 6 */
#define                    CMRBE7  0x800000   /* Control Message Receive Buffer Entry 7 */
#define                    CMRBE8  0x1000000  /* Control Message Receive Buffer Entry 8 */
#define                    CMRBE9  0x2000000  /* Control Message Receive Buffer Entry 9 */
#define                   CMRBE10  0x4000000  /* Control Message Receive Buffer Entry 10 */
#define                   CMRBE11  0x8000000  /* Control Message Receive Buffer Entry 11 */
#define                   CMRBE12  0x10000000 /* Control Message Receive Buffer Entry 12 */
#define                   CMRBE13  0x20000000 /* Control Message Receive Buffer Entry 13 */
#define                   CMRBE14  0x40000000 /* Control Message Receive Buffer Entry 14 */
#define                   CMRBE15  0x80000000 /* Control Message Receive Buffer Entry 15 */

/* Bit masks for MXVR_CMRB_START_ADDR */

#define      MXVR_CMRB_START_ADDR_MASK  0x1fffffe  /* Control Message Receive Buffer Start Address */

/* Bit masks for MXVR_CMRB_CURR_ADDR */

#define       MXVR_CMRB_CURR_ADDR_MASK  0xffffffff /* Control Message Receive Buffer Current Address */

/* Bit masks for MXVR_CMTB_START_ADDR */

#define      MXVR_CMTB_START_ADDR_MASK  0x1fffffe  /* Control Message Transmit Buffer Start Address */

/* Bit masks for MXVR_CMTB_CURR_ADDR */

#define       MXVR_CMTB_CURR_ADDR_MASK  0xffffffff /* Control Message Transmit Buffer Current Address */

/* Bit masks for MXVR_RRDB_START_ADDR */

#define      MXVR_RRDB_START_ADDR_MASK  0x1fffffe  /* Remote Read Buffer Start Address */

/* Bit masks for MXVR_RRDB_CURR_ADDR */

#define       MXVR_RRDB_CURR_ADDR_MASK  0xffffffff /* Remote Read Buffer Current Address */

/* Bit masks for MXVR_PAT_DATAx */

#define              MATCH_DATA_0  0xff       /* Pattern Match Data Byte 0 */
#define              MATCH_DATA_1  0xff00     /* Pattern Match Data Byte 1 */
#define              MATCH_DATA_2  0xff0000   /* Pattern Match Data Byte 2 */
#define              MATCH_DATA_3  0xff000000 /* Pattern Match Data Byte 3 */

/* Bit masks for MXVR_PAT_EN_0 */

#define              MATCH_EN_0_0  0x1        /* Pattern Match Enable Byte 0 Bit 0 */
#define              MATCH_EN_0_1  0x2        /* Pattern Match Enable Byte 0 Bit 1 */
#define              MATCH_EN_0_2  0x4        /* Pattern Match Enable Byte 0 Bit 2 */
#define              MATCH_EN_0_3  0x8        /* Pattern Match Enable Byte 0 Bit 3 */
#define              MATCH_EN_0_4  0x10       /* Pattern Match Enable Byte 0 Bit 4 */
#define              MATCH_EN_0_5  0x20       /* Pattern Match Enable Byte 0 Bit 5 */
#define              MATCH_EN_0_6  0x40       /* Pattern Match Enable Byte 0 Bit 6 */
#define              MATCH_EN_0_7  0x80       /* Pattern Match Enable Byte 0 Bit 7 */
#define              MATCH_EN_1_0  0x100      /* Pattern Match Enable Byte 1 Bit 0 */
#define              MATCH_EN_1_1  0x200      /* Pattern Match Enable Byte 1 Bit 1 */
#define              MATCH_EN_1_2  0x400      /* Pattern Match Enable Byte 1 Bit 2 */
#define              MATCH_EN_1_3  0x800      /* Pattern Match Enable Byte 1 Bit 3 */
#define              MATCH_EN_1_4  0x1000     /* Pattern Match Enable Byte 1 Bit 4 */
#define              MATCH_EN_1_5  0x2000     /* Pattern Match Enable Byte 1 Bit 5 */
#define              MATCH_EN_1_6  0x4000     /* Pattern Match Enable Byte 1 Bit 6 */
#define              MATCH_EN_1_7  0x8000     /* Pattern Match Enable Byte 1 Bit 7 */
#define              MATCH_EN_2_0  0x10000    /* Pattern Match Enable Byte 2 Bit 0 */
#define              MATCH_EN_2_1  0x20000    /* Pattern Match Enable Byte 2 Bit 1 */
#define              MATCH_EN_2_2  0x40000    /* Pattern Match Enable Byte 2 Bit 2 */
#define              MATCH_EN_2_3  0x80000    /* Pattern Match Enable Byte 2 Bit 3 */
#define              MATCH_EN_2_4  0x100000   /* Pattern Match Enable Byte 2 Bit 4 */
#define              MATCH_EN_2_5  0x200000   /* Pattern Match Enable Byte 2 Bit 5 */
#define              MATCH_EN_2_6  0x400000   /* Pattern Match Enable Byte 2 Bit 6 */
#define              MATCH_EN_2_7  0x800000   /* Pattern Match Enable Byte 2 Bit 7 */
#define              MATCH_EN_3_0  0x1000000  /* Pattern Match Enable Byte 3 Bit 0 */
#define              MATCH_EN_3_1  0x2000000  /* Pattern Match Enable Byte 3 Bit 1 */
#define              MATCH_EN_3_2  0x4000000  /* Pattern Match Enable Byte 3 Bit 2 */
#define              MATCH_EN_3_3  0x8000000  /* Pattern Match Enable Byte 3 Bit 3 */
#define              MATCH_EN_3_4  0x10000000 /* Pattern Match Enable Byte 3 Bit 4 */
#define              MATCH_EN_3_5  0x20000000 /* Pattern Match Enable Byte 3 Bit 5 */
#define              MATCH_EN_3_6  0x40000000 /* Pattern Match Enable Byte 3 Bit 6 */
#define              MATCH_EN_3_7  0x80000000 /* Pattern Match Enable Byte 3 Bit 7 */

/* Bit masks for MXVR_PAT_EN_1 */

#define              MATCH_EN_0_0  0x1        /* Pattern Match Enable Byte 0 Bit 0 */
#define              MATCH_EN_0_1  0x2        /* Pattern Match Enable Byte 0 Bit 1 */
#define              MATCH_EN_0_2  0x4        /* Pattern Match Enable Byte 0 Bit 2 */
#define              MATCH_EN_0_3  0x8        /* Pattern Match Enable Byte 0 Bit 3 */
#define              MATCH_EN_0_4  0x10       /* Pattern Match Enable Byte 0 Bit 4 */
#define              MATCH_EN_0_5  0x20       /* Pattern Match Enable Byte 0 Bit 5 */
#define              MATCH_EN_0_6  0x40       /* Pattern Match Enable Byte 0 Bit 6 */
#define              MATCH_EN_0_7  0x80       /* Pattern Match Enable Byte 0 Bit 7 */
#define              MATCH_EN_1_0  0x100      /* Pattern Match Enable Byte 1 Bit 0 */
#define              MATCH_EN_1_1  0x200      /* Pattern Match Enable Byte 1 Bit 1 */
#define              MATCH_EN_1_2  0x400      /* Pattern Match Enable Byte 1 Bit 2 */
#define              MATCH_EN_1_3  0x800      /* Pattern Match Enable Byte 1 Bit 3 */
#define              MATCH_EN_1_4  0x1000     /* Pattern Match Enable Byte 1 Bit 4 */
#define              MATCH_EN_1_5  0x2000     /* Pattern Match Enable Byte 1 Bit 5 */
#define              MATCH_EN_1_6  0x4000     /* Pattern Match Enable Byte 1 Bit 6 */
#define              MATCH_EN_1_7  0x8000     /* Pattern Match Enable Byte 1 Bit 7 */
#define              MATCH_EN_2_0  0x10000    /* Pattern Match Enable Byte 2 Bit 0 */
#define              MATCH_EN_2_1  0x20000    /* Pattern Match Enable Byte 2 Bit 1 */
#define              MATCH_EN_2_2  0x40000    /* Pattern Match Enable Byte 2 Bit 2 */
#define              MATCH_EN_2_3  0x80000    /* Pattern Match Enable Byte 2 Bit 3 */
#define              MATCH_EN_2_4  0x100000   /* Pattern Match Enable Byte 2 Bit 4 */
#define              MATCH_EN_2_5  0x200000   /* Pattern Match Enable Byte 2 Bit 5 */
#define              MATCH_EN_2_6  0x400000   /* Pattern Match Enable Byte 2 Bit 6 */
#define              MATCH_EN_2_7  0x800000   /* Pattern Match Enable Byte 2 Bit 7 */
#define              MATCH_EN_3_0  0x1000000  /* Pattern Match Enable Byte 3 Bit 0 */
#define              MATCH_EN_3_1  0x2000000  /* Pattern Match Enable Byte 3 Bit 1 */
#define              MATCH_EN_3_2  0x4000000  /* Pattern Match Enable Byte 3 Bit 2 */
#define              MATCH_EN_3_3  0x8000000  /* Pattern Match Enable Byte 3 Bit 3 */
#define              MATCH_EN_3_4  0x10000000 /* Pattern Match Enable Byte 3 Bit 4 */
#define              MATCH_EN_3_5  0x20000000 /* Pattern Match Enable Byte 3 Bit 5 */
#define              MATCH_EN_3_6  0x40000000 /* Pattern Match Enable Byte 3 Bit 6 */
#define              MATCH_EN_3_7  0x80000000 /* Pattern Match Enable Byte 3 Bit 7 */

/* Bit masks for MXVR_FRAME_CNT_0 */

#define                      FCNT  0xffff     /* Frame Count */

/* Bit masks for MXVR_FRAME_CNT_1 */

#define                      FCNT  0xffff     /* Frame Count */

/* Bit masks for MXVR_ROUTING_0 */

#define                    TX_CH0  0x3f       /* Transmit Channel 0 */
#define                  MUTE_CH0  0x80       /* Mute Channel 0 */
#define                    TX_CH1  0x3f00     /* Transmit Channel 0 */
#define                  MUTE_CH1  0x8000     /* Mute Channel 0 */
#define                    TX_CH2  0x3f0000   /* Transmit Channel 0 */
#define                  MUTE_CH2  0x800000   /* Mute Channel 0 */
#define                    TX_CH3  0x3f000000 /* Transmit Channel 0 */
#define                  MUTE_CH3  0x80000000 /* Mute Channel 0 */

/* Bit masks for MXVR_ROUTING_1 */

#define                    TX_CH4  0x3f       /* Transmit Channel 4 */
#define                  MUTE_CH4  0x80       /* Mute Channel 4 */
#define                    TX_CH5  0x3f00     /* Transmit Channel 5 */
#define                  MUTE_CH5  0x8000     /* Mute Channel 5 */
#define                    TX_CH6  0x3f0000   /* Transmit Channel 6 */
#define                  MUTE_CH6  0x800000   /* Mute Channel 6 */
#define                    TX_CH7  0x3f000000 /* Transmit Channel 7 */
#define                  MUTE_CH7  0x80000000 /* Mute Channel 7 */

/* Bit masks for MXVR_ROUTING_2 */

#define                    TX_CH8  0x3f       /* Transmit Channel 8 */
#define                  MUTE_CH8  0x80       /* Mute Channel 8 */
#define                    TX_CH9  0x3f00     /* Transmit Channel 9 */
#define                  MUTE_CH9  0x8000     /* Mute Channel 9 */
#define                   TX_CH10  0x3f0000   /* Transmit Channel 10 */
#define                 MUTE_CH10  0x800000   /* Mute Channel 10 */
#define                   TX_CH11  0x3f000000 /* Transmit Channel 11 */
#define                 MUTE_CH11  0x80000000 /* Mute Channel 11 */

/* Bit masks for MXVR_ROUTING_3 */

#define                   TX_CH12  0x3f       /* Transmit Channel 12 */
#define                 MUTE_CH12  0x80       /* Mute Channel 12 */
#define                   TX_CH13  0x3f00     /* Transmit Channel 13 */
#define                 MUTE_CH13  0x8000     /* Mute Channel 13 */
#define                   TX_CH14  0x3f0000   /* Transmit Channel 14 */
#define                 MUTE_CH14  0x800000   /* Mute Channel 14 */
#define                   TX_CH15  0x3f000000 /* Transmit Channel 15 */
#define                 MUTE_CH15  0x80000000 /* Mute Channel 15 */

/* Bit masks for MXVR_ROUTING_4 */

#define                   TX_CH16  0x3f       /* Transmit Channel 16 */
#define                 MUTE_CH16  0x80       /* Mute Channel 16 */
#define                   TX_CH17  0x3f00     /* Transmit Channel 17 */
#define                 MUTE_CH17  0x8000     /* Mute Channel 17 */
#define                   TX_CH18  0x3f0000   /* Transmit Channel 18 */
#define                 MUTE_CH18  0x800000   /* Mute Channel 18 */
#define                   TX_CH19  0x3f000000 /* Transmit Channel 19 */
#define                 MUTE_CH19  0x80000000 /* Mute Channel 19 */

/* Bit masks for MXVR_ROUTING_5 */

#define                   TX_CH20  0x3f       /* Transmit Channel 20 */
#define                 MUTE_CH20  0x80       /* Mute Channel 20 */
#define                   TX_CH21  0x3f00     /* Transmit Channel 21 */
#define                 MUTE_CH21  0x8000     /* Mute Channel 21 */
#define                   TX_CH22  0x3f0000   /* Transmit Channel 22 */
#define                 MUTE_CH22  0x800000   /* Mute Channel 22 */
#define                   TX_CH23  0x3f000000 /* Transmit Channel 23 */
#define                 MUTE_CH23  0x80000000 /* Mute Channel 23 */

/* Bit masks for MXVR_ROUTING_6 */

#define                   TX_CH24  0x3f       /* Transmit Channel 24 */
#define                 MUTE_CH24  0x80       /* Mute Channel 24 */
#define                   TX_CH25  0x3f00     /* Transmit Channel 25 */
#define                 MUTE_CH25  0x8000     /* Mute Channel 25 */
#define                   TX_CH26  0x3f0000   /* Transmit Channel 26 */
#define                 MUTE_CH26  0x800000   /* Mute Channel 26 */
#define                   TX_CH27  0x3f000000 /* Transmit Channel 27 */
#define                 MUTE_CH27  0x80000000 /* Mute Channel 27 */

/* Bit masks for MXVR_ROUTING_7 */

#define                   TX_CH28  0x3f       /* Transmit Channel 28 */
#define                 MUTE_CH28  0x80       /* Mute Channel 28 */
#define                   TX_CH29  0x3f00     /* Transmit Channel 29 */
#define                 MUTE_CH29  0x8000     /* Mute Channel 29 */
#define                   TX_CH30  0x3f0000   /* Transmit Channel 30 */
#define                 MUTE_CH30  0x800000   /* Mute Channel 30 */
#define                   TX_CH31  0x3f000000 /* Transmit Channel 31 */
#define                 MUTE_CH31  0x80000000 /* Mute Channel 31 */

/* Bit masks for MXVR_ROUTING_8 */

#define                   TX_CH32  0x3f       /* Transmit Channel 32 */
#define                 MUTE_CH32  0x80       /* Mute Channel 32 */
#define                   TX_CH33  0x3f00     /* Transmit Channel 33 */
#define                 MUTE_CH33  0x8000     /* Mute Channel 33 */
#define                   TX_CH34  0x3f0000   /* Transmit Channel 34 */
#define                 MUTE_CH34  0x800000   /* Mute Channel 34 */
#define                   TX_CH35  0x3f000000 /* Transmit Channel 35 */
#define                 MUTE_CH35  0x80000000 /* Mute Channel 35 */

/* Bit masks for MXVR_ROUTING_9 */

#define                   TX_CH36  0x3f       /* Transmit Channel 36 */
#define                 MUTE_CH36  0x80       /* Mute Channel 36 */
#define                   TX_CH37  0x3f00     /* Transmit Channel 37 */
#define                 MUTE_CH37  0x8000     /* Mute Channel 37 */
#define                   TX_CH38  0x3f0000   /* Transmit Channel 38 */
#define                 MUTE_CH38  0x800000   /* Mute Channel 38 */
#define                   TX_CH39  0x3f000000 /* Transmit Channel 39 */
#define                 MUTE_CH39  0x80000000 /* Mute Channel 39 */

/* Bit masks for MXVR_ROUTING_10 */

#define                   TX_CH40  0x3f       /* Transmit Channel 40 */
#define                 MUTE_CH40  0x80       /* Mute Channel 40 */
#define                   TX_CH41  0x3f00     /* Transmit Channel 41 */
#define                 MUTE_CH41  0x8000     /* Mute Channel 41 */
#define                   TX_CH42  0x3f0000   /* Transmit Channel 42 */
#define                 MUTE_CH42  0x800000   /* Mute Channel 42 */
#define                   TX_CH43  0x3f000000 /* Transmit Channel 43 */
#define                 MUTE_CH43  0x80000000 /* Mute Channel 43 */

/* Bit masks for MXVR_ROUTING_11 */

#define                   TX_CH44  0x3f       /* Transmit Channel 44 */
#define                 MUTE_CH44  0x80       /* Mute Channel 44 */
#define                   TX_CH45  0x3f00     /* Transmit Channel 45 */
#define                 MUTE_CH45  0x8000     /* Mute Channel 45 */
#define                   TX_CH46  0x3f0000   /* Transmit Channel 46 */
#define                 MUTE_CH46  0x800000   /* Mute Channel 46 */
#define                   TX_CH47  0x3f000000 /* Transmit Channel 47 */
#define                 MUTE_CH47  0x80000000 /* Mute Channel 47 */

/* Bit masks for MXVR_ROUTING_12 */

#define                   TX_CH48  0x3f       /* Transmit Channel 48 */
#define                 MUTE_CH48  0x80       /* Mute Channel 48 */
#define                   TX_CH49  0x3f00     /* Transmit Channel 49 */
#define                 MUTE_CH49  0x8000     /* Mute Channel 49 */
#define                   TX_CH50  0x3f0000   /* Transmit Channel 50 */
#define                 MUTE_CH50  0x800000   /* Mute Channel 50 */
#define                   TX_CH51  0x3f000000 /* Transmit Channel 51 */
#define                 MUTE_CH51  0x80000000 /* Mute Channel 51 */

/* Bit masks for MXVR_ROUTING_13 */

#define                   TX_CH52  0x3f       /* Transmit Channel 52 */
#define                 MUTE_CH52  0x80       /* Mute Channel 52 */
#define                   TX_CH53  0x3f00     /* Transmit Channel 53 */
#define                 MUTE_CH53  0x8000     /* Mute Channel 53 */
#define                   TX_CH54  0x3f0000   /* Transmit Channel 54 */
#define                 MUTE_CH54  0x800000   /* Mute Channel 54 */
#define                   TX_CH55  0x3f000000 /* Transmit Channel 55 */
#define                 MUTE_CH55  0x80000000 /* Mute Channel 55 */

/* Bit masks for MXVR_ROUTING_14 */

#define                   TX_CH56  0x3f       /* Transmit Channel 56 */
#define                 MUTE_CH56  0x80       /* Mute Channel 56 */
#define                   TX_CH57  0x3f00     /* Transmit Channel 57 */
#define                 MUTE_CH57  0x8000     /* Mute Channel 57 */
#define                   TX_CH58  0x3f0000   /* Transmit Channel 58 */
#define                 MUTE_CH58  0x800000   /* Mute Channel 58 */
#define                   TX_CH59  0x3f000000 /* Transmit Channel 59 */
#define                 MUTE_CH59  0x80000000 /* Mute Channel 59 */

/* Bit masks for MXVR_BLOCK_CNT */

#define                      BCNT  0xffff     /* Block Count */

/* Bit masks for MXVR_CLK_CTL */

#define                  MXTALCEN  0x1        /* MXVR Crystal Oscillator Clock Enable */
#define                  MXTALFEN  0x2        /* MXVR Crystal Oscillator Feedback Enable */
#define                  MXTALMUL  0x30       /* MXVR Crystal Multiplier */
#define                  CLKX3SEL  0x80       /* Clock Generation Source Select */
#define                   MMCLKEN  0x100      /* Master Clock Enable */
#define                  MMCLKMUL  0x1e00     /* Master Clock Multiplication Factor */
#define                   PLLSMPS  0xe000     /* MXVR PLL State Machine Prescaler */
#define                   MBCLKEN  0x10000    /* Bit Clock Enable */
#define                  MBCLKDIV  0x1e0000   /* Bit Clock Divide Factor */
#define                     INVRX  0x800000   /* Invert Receive Data */
#define                     MFSEN  0x1000000  /* Frame Sync Enable */
#define                    MFSDIV  0x1e000000 /* Frame Sync Divide Factor */
#define                    MFSSEL  0x60000000 /* Frame Sync Select */
#define                   MFSSYNC  0x80000000 /* Frame Sync Synchronization Select */

/* Bit masks for MXVR_CDRPLL_CTL */

#define                   CDRSMEN  0x1        /* MXVR CDRPLL State Machine Enable */
#define                   CDRRSTB  0x2        /* MXVR CDRPLL Reset */
#define                   CDRSVCO  0x4        /* MXVR CDRPLL Start VCO */
#define                   CDRMODE  0x8        /* MXVR CDRPLL CDR Mode Select */
#define                   CDRSCNT  0x3f0      /* MXVR CDRPLL Start Counter */
#define                   CDRLCNT  0xfc00     /* MXVR CDRPLL Lock Counter */
#define                 CDRSHPSEL  0x3f0000   /* MXVR CDRPLL Shaper Select */
#define                  CDRSHPEN  0x800000   /* MXVR CDRPLL Shaper Enable */
#define                  CDRCPSEL  0xff000000 /* MXVR CDRPLL Charge Pump Current Select */

/* Bit masks for MXVR_FMPLL_CTL */

#define                    FMSMEN  0x1        /* MXVR FMPLL State Machine Enable */
#define                    FMRSTB  0x2        /* MXVR FMPLL Reset */
#define                    FMSVCO  0x4        /* MXVR FMPLL Start VCO */
#define                    FMSCNT  0x3f0      /* MXVR FMPLL Start Counter */
#define                    FMLCNT  0xfc00     /* MXVR FMPLL Lock Counter */
#define                   FMCPSEL  0xff000000 /* MXVR FMPLL Charge Pump Current Select */

/* Bit masks for MXVR_PIN_CTL */

#define                  MTXONBOD  0x1        /* MTXONB Open Drain Select */
#define                   MTXONBG  0x2        /* MTXONB Gates MTX Select */
#define                     MFSOE  0x10       /* MFS Output Enable */
#define                  MFSGPSEL  0x20       /* MFS General Purpose Output Select */
#define                  MFSGPDAT  0x40       /* MFS General Purpose Output Data */

/* Bit masks for MXVR_SCLK_CNT */

#define                      SCNT  0xffff     /* System Clock Count */

/* ******************************************* */
/*     MULTI BIT MACRO ENUMERATIONS            */
/* ******************************************* */

/* ************************ */
/*   MXVR Address Offsets   */
/* ************************ */

/* Control Message Receive Buffer (CMRB) Address Offsets */

#define CMRB_STRIDE       0x00000016lu

#define CMRB_DST_OFFSET   0x00000000lu
#define CMRB_SRC_OFFSET   0x00000002lu
#define CMRB_DATA_OFFSET  0x00000005lu

/* Control Message Transmit Buffer (CMTB) Address Offsets */

#define CMTB_PRIO_OFFSET    0x00000000lu
#define CMTB_DST_OFFSET     0x00000002lu
#define CMTB_SRC_OFFSET     0x00000004lu
#define CMTB_TYPE_OFFSET    0x00000006lu
#define CMTB_DATA_OFFSET    0x00000007lu

#define CMTB_ANSWER_OFFSET  0x0000000Alu

#define CMTB_STAT_N_OFFSET  0x00000018lu
#define CMTB_STAT_A_OFFSET  0x00000016lu
#define CMTB_STAT_D_OFFSET  0x0000000Elu
#define CMTB_STAT_R_OFFSET  0x00000014lu
#define CMTB_STAT_W_OFFSET  0x00000014lu
#define CMTB_STAT_G_OFFSET  0x00000014lu

/* Asynchronous Packet Receive Buffer (APRB) Address Offsets */

#define APRB_STRIDE       0x00000400lu

#define APRB_DST_OFFSET   0x00000000lu
#define APRB_LEN_OFFSET   0x00000002lu
#define APRB_SRC_OFFSET   0x00000004lu
#define APRB_DATA_OFFSET  0x00000006lu

/* Asynchronous Packet Transmit Buffer (APTB) Address Offsets */

#define APTB_PRIO_OFFSET  0x00000000lu
#define APTB_DST_OFFSET   0x00000002lu
#define APTB_LEN_OFFSET   0x00000004lu
#define APTB_SRC_OFFSET   0x00000006lu
#define APTB_DATA_OFFSET  0x00000008lu

/* Remote Read Buffer (RRDB) Address Offsets */

#define RRDB_WADDR_OFFSET 0x00000100lu
#define RRDB_WLEN_OFFSET  0x00000101lu

/* **************** */
/*   MXVR Macros    */
/* **************** */

/* MXVR_CONFIG Macros */

#define SET_MSB(x)       ( ( (x) & 0xF  ) << 9)

/* MXVR_INT_STAT_1 Macros */

#define DONEX(x)         (0x00000002 << (4 * (x)))
#define HDONEX(x)        (0x00000001 << (4 * (x)))

/* MXVR_INT_EN_1 Macros */

#define DONEENX(x)       (0x00000002 << (4 * (x)))
#define HDONEENX(x)      (0x00000001 << (4 * (x)))

/* MXVR_CDRPLL_CTL Macros */

#define SET_CDRSHPSEL(x) ( ( (x) & 0x3F ) << 16)

/* MXVR_FMPLL_CTL Macros */

#define SET_CDRCPSEL(x)  ( ( (x) & 0xFF ) << 24)
#define SET_FMCPSEL(x)   ( ( (x) & 0xFF ) << 24)

#endif /* _DEF_BF549_H */

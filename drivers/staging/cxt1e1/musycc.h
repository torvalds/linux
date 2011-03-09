/*
 * $Id: musycc.h,v 1.3 2005/09/28 00:10:08 rickd PMCC4_3_1B $
 */

#ifndef _INC_MUSYCC_H_
#define _INC_MUSYCC_H_

/*-----------------------------------------------------------------------------
 * musycc.h - Multichannel Synchronous Communications Controller
 *            CN8778/8474A/8472A/8471A
 *
 * Copyright (C) 2002-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *-----------------------------------------------------------------------------
 * RCS info:
 * RCS revision: $Revision: 1.3 $
 * Last changed on $Date: 2005/09/28 00:10:08 $
 * Changed by $Author: rickd $
 *-----------------------------------------------------------------------------
 * $Log: musycc.h,v $
 * Revision 1.3  2005/09/28 00:10:08  rickd
 * Add GNU license info. Add PMCC4 PCI/DevIDs.  Implement new
 * musycc reg&bits namings. Use PORTMAP_0 GCD grouping.
 *
 * Revision 1.2  2005/04/28 23:43:04  rickd
 * Add RCS tracking heading.
 *
 *-----------------------------------------------------------------------------
 */

#if defined (__FreeBSD__) || defined (__NetBSD__)
#include <sys/types.h>
#else
#include <linux/types.h>
#endif

#define VINT8   volatile u_int8_t
#define VINT32  volatile u_int32_t

#ifdef __cplusplus
extern      "C"
{
#endif

#include "pmcc4_defs.h"


/*------------------------------------------------------------------------
//      Vendor, Board Identification definitions
//------------------------------------------------------------------------
*/

#define PCI_VENDOR_ID_CONEXANT   0x14f1
#define PCI_DEVICE_ID_CN8471     0x8471
#define PCI_DEVICE_ID_CN8472     0x8472
#define PCI_DEVICE_ID_CN8474     0x8474
#define PCI_DEVICE_ID_CN8478     0x8478
#define PCI_DEVICE_ID_CN8500     0x8500
#define PCI_DEVICE_ID_CN8501     0x8501
#define PCI_DEVICE_ID_CN8502     0x8502
#define PCI_DEVICE_ID_CN8503     0x8503

#define INT_QUEUE_SIZE    MUSYCC_NIQD

/* RAM image of MUSYCC registers layed out as a C structure */
    struct musycc_groupr
    {
        VINT32      thp[32];    /* Transmit Head Pointer [5-29]           */
        VINT32      tmp[32];    /* Transmit Message Pointer [5-30]        */
        VINT32      rhp[32];    /* Receive Head Pointer [5-29]            */
        VINT32      rmp[32];    /* Receive Message Pointer [5-30]         */
        VINT8       ttsm[128];  /* Time Slot Map [5-22]                   */
        VINT8       tscm[256];  /* Subchannel Map [5-24]                  */
        VINT32      tcct[32];   /* Channel Configuration [5-26]           */
        VINT8       rtsm[128];  /* Time Slot Map [5-22]                   */
        VINT8       rscm[256];  /* Subchannel Map [5-24]                  */
        VINT32      rcct[32];   /* Channel Configuration [5-26]           */
        VINT32      __glcd;     /* Global Configuration Descriptor [5-10] */
        VINT32      __iqp;      /* Interrupt Queue Pointer [5-36]         */
        VINT32      __iql;      /* Interrupt Queue Length [5-36]          */
        VINT32      grcd;       /* Group Configuration Descriptor [5-16]  */
        VINT32      mpd;        /* Memory Protection Descriptor [5-18]    */
        VINT32      mld;        /* Message Length Descriptor [5-20]       */
        VINT32      pcd;        /* Port Configuration Descriptor [5-19]   */
    };

/* hardware MUSYCC registers layed out as a C structure */
    struct musycc_globalr
    {
        VINT32      gbp;        /* Group Base Pointer                     */
        VINT32      dacbp;      /* Dual Address Cycle Base Pointer        */
        VINT32      srd;        /* Service Request Descriptor             */
        VINT32      isd;        /* Interrupt Service Descriptor           */
        /*
         * adjust __thp due to above 4 registers, which are not contained
         * within musycc_groupr[]. All __XXX[] are just place holders,
         * anyhow.
         */
        VINT32      __thp[32 - 4];      /* Transmit Head Pointer [5-29]           */
        VINT32      __tmp[32];  /* Transmit Message Pointer [5-30]        */
        VINT32      __rhp[32];  /* Receive Head Pointer [5-29]            */
        VINT32      __rmp[32];  /* Receive Message Pointer [5-30]         */
        VINT8       ttsm[128];  /* Time Slot Map [5-22]                   */
        VINT8       tscm[256];  /* Subchannel Map [5-24]                  */
        VINT32      tcct[32];   /* Channel Configuration [5-26]           */
        VINT8       rtsm[128];  /* Time Slot Map [5-22]                   */
        VINT8       rscm[256];  /* Subchannel Map [5-24]                  */
        VINT32      rcct[32];   /* Channel Configuration [5-26]           */
        VINT32      glcd;       /* Global Configuration Descriptor [5-10] */
        VINT32      iqp;        /* Interrupt Queue Pointer [5-36]         */
        VINT32      iql;        /* Interrupt Queue Length [5-36]          */
        VINT32      grcd;       /* Group Configuration Descriptor [5-16]  */
        VINT32      mpd;        /* Memory Protection Descriptor [5-18]    */
        VINT32      mld;        /* Message Length Descriptor [5-20]       */
        VINT32      pcd;        /* Port Configuration Descriptor [5-19]   */
        VINT32      rbist;      /* Receive BIST status [5-4]              */
        VINT32      tbist;      /* Receive BIST status [5-4]              */
    };

/* Global Config Descriptor bit macros */
#define MUSYCC_GCD_ECLK_ENABLE  0x00000800      /* EBUS clock enable */
#define MUSYCC_GCD_INTEL_SELECT 0x00000400      /* MPU type select */
#define MUSYCC_GCD_INTA_DISABLE 0x00000008      /* PCI INTA disable */
#define MUSYCC_GCD_INTB_DISABLE 0x00000004      /* PCI INTB disable */
#define MUSYCC_GCD_BLAPSE       12      /* Position index for BLAPSE bit
                                         * field */
#define MUSYCC_GCD_ALAPSE       8       /* Position index for ALAPSE bit
                                         * field */
#define MUSYCC_GCD_ELAPSE       4       /* Position index for ELAPSE bit
                                         * field */
#define MUSYCC_GCD_PORTMAP_3    3       /* Reserved */
#define MUSYCC_GCD_PORTMAP_2    2       /* Port 0=>Grp 0,1,2,3; Port 1=>Grp
                                         * 4,5,6,7 */
#define MUSYCC_GCD_PORTMAP_1    1       /* Port 0=>Grp 0,1; Port 1=>Grp 2,3,
                                         * etc... */
#define MUSYCC_GCD_PORTMAP_0    0       /* Port 0=>Grp 0; Port 1=>Grp 2,
                                         * etc... */

/* and board specific assignments... */
#ifdef SBE_WAN256T3_ENABLE
#define BLAPSE_VAL      0
#define ALAPSE_VAL      0
#define ELAPSE_VAL      7
#define PORTMAP_VAL     MUSYCC_GCD_PORTMAP_2
#endif

#ifdef SBE_PMCC4_ENABLE
#define BLAPSE_VAL      7
#define ALAPSE_VAL      3
#define ELAPSE_VAL      7
#define PORTMAP_VAL     MUSYCC_GCD_PORTMAP_0
#endif

#define GCD_MAGIC   (((BLAPSE_VAL)<<(MUSYCC_GCD_BLAPSE)) | \
                     ((ALAPSE_VAL)<<(MUSYCC_GCD_ALAPSE)) | \
                     ((ELAPSE_VAL)<<(MUSYCC_GCD_ELAPSE)) | \
                     (MUSYCC_GCD_ECLK_ENABLE) | PORTMAP_VAL)

/* Group Config Descriptor bit macros */
#define MUSYCC_GRCD_RX_ENABLE       0x00000001  /* Enable receive processing */
#define MUSYCC_GRCD_TX_ENABLE       0x00000002  /* Enable transmit processing */
#define MUSYCC_GRCD_SUBCHAN_DISABLE 0x00000004  /* Master disable for
                                                 * subchanneling */
#define MUSYCC_GRCD_OOFMP_DISABLE   0x00000008  /* Out of Frame message
                                                 * processing disabled all
                                                 * channels */
#define MUSYCC_GRCD_OOFIRQ_DISABLE  0x00000010  /* Out of Frame/In Frame irqs
                                                 * disabled */
#define MUSYCC_GRCD_COFAIRQ_DISABLE 0x00000020  /* Change of Frame Alignment
                                                 * irq disabled */
#define MUSYCC_GRCD_INHRBSD         0x00000100  /* Receive Buffer Status
                                                 * overwrite disabled */
#define MUSYCC_GRCD_INHTBSD         0x00000200  /* Transmit Buffer Status
                                                 * overwrite disabled */
#define MUSYCC_GRCD_SF_ALIGN        0x00008000  /* External frame sync */
#define MUSYCC_GRCD_MC_ENABLE       0x00000040  /* Message configuration bits
                                                 * copy enable. Conexant sez
                                                 * turn this on */
#define MUSYCC_GRCD_POLLTH_16       0x00000001  /* Poll every 16th frame */
#define MUSYCC_GRCD_POLLTH_32       0x00000002  /* Poll every 32nd frame */
#define MUSYCC_GRCD_POLLTH_64       0x00000003  /* Poll every 64th frame */
#define MUSYCC_GRCD_POLLTH_SHIFT    10  /* Position index for poll throttle
                                         * bit field */
#define MUSYCC_GRCD_SUERM_THRESH_SHIFT 16       /* Position index for SUERM
                                                 * count threshold */

/* Port Config Descriptor bit macros */
#define MUSYCC_PCD_E1X2_MODE       2    /* Port mode in bits 0-2. T1 and E1 */
#define MUSYCC_PCD_E1X4_MODE       3    /* are defined in cn847x.h */
#define MUSYCC_PCD_NX64_MODE       4
#define MUSYCC_PCD_TXDATA_RISING   0x00000010   /* Sample Tx data on TCLK
                                                 * rising edge */
#define MUSYCC_PCD_TXSYNC_RISING   0x00000020   /* Sample Tx frame sync on
                                                 * TCLK rising edge */
#define MUSYCC_PCD_RXDATA_RISING   0x00000040   /* Sample Rx data on RCLK
                                                 * rising edge */
#define MUSYCC_PCD_RXSYNC_RISING   0x00000080   /* Sample Rx frame sync on
                                                 * RCLK rising edge */
#define MUSYCC_PCD_ROOF_RISING     0x00000100   /* Sample Rx Out Of Frame
                                                 * signal on RCLK rising edge */
#define MUSYCC_PCD_TX_DRIVEN       0x00000200   /* No mapped timeslots causes
                                                 * logic 1 on output, else
                                                 * tristate */
#define MUSYCC_PCD_PORTMODE_MASK   0xfffffff8   /* For changing the port mode
                                                 * between E1 and T1 */

/* Time Slot Descriptor bit macros */
#define MUSYCC_TSD_MODE_64KBPS              4
#define MUSYCC_TSD_MODE_56KBPS              5
#define MUSYCC_TSD_SUBCHANNEL_WO_FIRST      6
#define MUSYCC_TSD_SUBCHANNEL_WITH_FIRST    7

/* Message Descriptor bit macros */
#define MUSYCC_MDT_BASE03_ADDR     0x00006000

/* Channel Config Descriptor bit macros */
#define MUSYCC_CCD_BUFIRQ_DISABLE  0x00000002   /* BUFF and ONR irqs disabled */
#define MUSYCC_CCD_EOMIRQ_DISABLE  0x00000004   /* EOM irq disabled */
#define MUSYCC_CCD_MSGIRQ_DISABLE  0x00000008   /* LNG, FCS, ALIGN, and ABT
                                                 * irqs disabled */
#define MUSYCC_CCD_IDLEIRQ_DISABLE 0x00000010   /* CHABT, CHIC, and SHT irqs
                                                 * disabled */
#define MUSYCC_CCD_FILTIRQ_DISABLE 0x00000020   /* SFILT irq disabled */
#define MUSYCC_CCD_SDECIRQ_DISABLE 0x00000040   /* SDEC irq disabled */
#define MUSYCC_CCD_SINCIRQ_DISABLE 0x00000080   /* SINC irq disabled */
#define MUSYCC_CCD_SUERIRQ_DISABLE 0x00000100   /* SUERR irq disabled */
#define MUSYCC_CCD_FCS_XFER        0x00000200   /* Propagate FCS along with
                                                 * received data */
#define MUSYCC_CCD_PROTO_SHIFT     12   /* Position index for protocol bit
                                         * field */
#define MUSYCC_CCD_TRANS           0    /* Protocol mode in bits 12-14 */
#define MUSYCC_CCD_SS7             1
#define MUSYCC_CCD_HDLC_FCS16      2
#define MUSYCC_CCD_HDLC_FCS32      3
#define MUSYCC_CCD_EOPIRQ_DISABLE  0x00008000   /* EOP irq disabled */
#define MUSYCC_CCD_INVERT_DATA     0x00800000   /* Invert data */
#define MUSYCC_CCD_MAX_LENGTH      10   /* Position index for max length bit
                                         * field */
#define MUSYCC_CCD_BUFFER_LENGTH   16   /* Position index for internal data
                                         * buffer length */
#define MUSYCC_CCD_BUFFER_LOC      24   /* Position index for internal data
                                         * buffer starting location */

/****************************************************************************
 * Interrupt Descriptor Information */

#define INT_EMPTY_ENTRY     0xfeedface
#define INT_EMPTY_ENTRY2    0xdeadface

/****************************************************************************
 * Interrupt Status Descriptor
 *
 * NOTE: One must first fetch the value of the interrupt status descriptor
 * into a local variable, then pass that value into the read macros. This
 * is required to avoid race conditions.
 ***/

#define INTRPTS_NEXTINT_M      0x7FFF0000
#define INTRPTS_NEXTINT_S      16
#define INTRPTS_NEXTINT(x)     ((x & INTRPTS_NEXTINT_M) >> INTRPTS_NEXTINT_S)

#define INTRPTS_INTFULL_M      0x00008000
#define INTRPTS_INTFULL_S      15
#define INTRPTS_INTFULL(x)     ((x & INTRPTS_INTFULL_M) >> INTRPTS_INTFULL_S)

#define INTRPTS_INTCNT_M       0x00007FFF
#define INTRPTS_INTCNT_S       0
#define INTRPTS_INTCNT(x)      ((x & INTRPTS_INTCNT_M) >> INTRPTS_INTCNT_S)


/****************************************************************************
 * Interrupt Descriptor
 ***/

#define INTRPT_DIR_M           0x80000000
#define INTRPT_DIR_S           31
#define INTRPT_DIR(x)          ((x & INTRPT_DIR_M) >> INTRPT_DIR_S)

#define INTRPT_GRP_M           0x60000000
#define INTRPT_GRP_MSB_M       0x00004000
#define INTRPT_GRP_S           29
#define INTRPT_GRP_MSB_S       12
#define INTRPT_GRP(x)          (((x & INTRPT_GRP_M) >> INTRPT_GRP_S) | \
                               ((x & INTRPT_GRP_MSB_M) >> INTRPT_GRP_MSB_S))

#define INTRPT_CH_M            0x1F000000
#define INTRPT_CH_S            24
#define INTRPT_CH(x)           ((x & INTRPT_CH_M) >> INTRPT_CH_S)

#define INTRPT_EVENT_M         0x00F00000
#define INTRPT_EVENT_S         20
#define INTRPT_EVENT(x)        ((x & INTRPT_EVENT_M) >> INTRPT_EVENT_S)

#define INTRPT_ERROR_M         0x000F0000
#define INTRPT_ERROR_S         16
#define INTRPT_ERROR(x)        ((x & INTRPT_ERROR_M) >> INTRPT_ERROR_S)

#define INTRPT_ILOST_M         0x00008000
#define INTRPT_ILOST_S         15
#define INTRPT_ILOST(x)        ((x & INTRPT_ILOST_M) >> INTRPT_ILOST_S)

#define INTRPT_PERR_M          0x00004000
#define INTRPT_PERR_S          14
#define INTRPT_PERR(x)         ((x & INTRPT_PERR_M) >> INTRPT_PERR_S)

#define INTRPT_BLEN_M          0x00003FFF
#define INTRPT_BLEN_S          0
#define INTRPT_BLEN(x)         ((x & INTRPT_BLEN_M) >> INTRPT_BLEN_S)


/* Buffer Descriptor bit macros */
#define OWNER_BIT       0x80000000      /* Set for MUSYCC owner on xmit, host
                                         * owner on receive */
#define HOST_TX_OWNED   0x00000000      /* Host owns descriptor */
#define MUSYCC_TX_OWNED 0x80000000      /* MUSYCC owns descriptor */
#define HOST_RX_OWNED   0x80000000      /* Host owns descriptor */
#define MUSYCC_RX_OWNED 0x00000000      /* MUSYCC owns descriptor */

#define POLL_DISABLED   0x40000000      /* MUSYCC not allowed to poll buffer
                                         * for ownership */
#define EOMIRQ_ENABLE   0x20000000      /* This buffer contains the end of
                                         * the message */
#define EOBIRQ_ENABLE   0x10000000      /* EOB irq enabled */
#define PADFILL_ENABLE  0x01000000      /* Enable padfill */
#define REPEAT_BIT      0x00008000      /* Bit on for FISU descriptor */
#define LENGTH_MASK         0X3fff      /* This part of status descriptor is
                                         * length */
#define IDLE_CODE               25      /* Position index for idle code (2
                                         * bits) */
#define EXTRA_FLAGS             16      /* Position index for minimum flags
                                         * between messages (8 bits) */
#define IDLE_CODE_MASK        0x03      /* Gets rid of garbage before the
                                         * pattern is OR'd in */
#define EXTRA_FLAGS_MASK      0xff      /* Gets rid of garbage before the
                                         * pattern is OR'd in */
#define PCI_PERMUTED_OWNER_BIT  0x00000080      /* For flipping the bit on
                                                 * the polled mode descriptor */

/* Service Request Descriptor bit macros */
#define SREQ  8                 /* Position index for service request bit
                                 * field */
#define SR_NOOP                 (0<<(SREQ))     /* No Operation. Generates SACK */
#define SR_CHIP_RESET           (1<<(SREQ))     /* Soft chip reset */
#define SR_GROUP_RESET          (2<<(SREQ))     /* Group reset */
#define SR_GLOBAL_INIT          (4<<(SREQ))     /* Global init: read global
                                                 * config deswc and interrupt
                                                 * queue desc */
#define SR_GROUP_INIT           (5<<(SREQ))     /* Group init: read Timeslot
                                                 * and Subchannel maps,
                                                 * Channel Config, */
    /*
     * Group Config, Memory Protect, Message Length, and Port Config
     * Descriptors
     */
#define SR_CHANNEL_ACTIVATE     (8<<(SREQ))     /* Init channel, read Head
                                                 * Pointer, process first
                                                 * Message Descriptor */
#define SR_GCHANNEL_MASK        0x001F          /* channel portion (gchan) */
#define SR_CHANNEL_DEACTIVATE   (9<<(SREQ))     /* Stop channel processing */
#define SR_JUMP                 (10<<(SREQ))    /* a: Process new Message
                                                 * List */
#define SR_CHANNEL_CONFIG       (11<<(SREQ))    /* b: Read channel
                                                 * Configuration Descriptor */
#define SR_GLOBAL_CONFIG        (16<<(SREQ))    /* 10: Read Global
                                                 * Configuration Descriptor */
#define SR_INTERRUPT_Q          (17<<(SREQ))    /* 11: Read Interrupt Queue
                                                 * Descriptor */
#define SR_GROUP_CONFIG         (18<<(SREQ))    /* 12: Read Group
                                                 * Configuration Descriptor */
#define SR_MEMORY_PROTECT       (19<<(SREQ))    /* 13: Read Memory Protection
                                                 * Descriptor */
#define SR_MESSAGE_LENGTH       (20<<(SREQ))    /* 14: Read Message Length
                                                 * Descriptor */
#define SR_PORT_CONFIG          (21<<(SREQ))    /* 15: Read Port
                                                 * Configuration Descriptor */
#define SR_TIMESLOT_MAP         (24<<(SREQ))    /* 18: Read Timeslot Map */
#define SR_SUBCHANNEL_MAP       (25<<(SREQ))    /* 19: Read Subchannel Map */
#define SR_CHAN_CONFIG_TABLE    (26<<(SREQ))    /* 20: Read Channel
                                                 * Configuration Table for
                                                 * the group */
#define SR_TX_DIRECTION         0x00000020      /* Transmit direction bit.
                                                 * Bit off indicates receive
                                                 * direction */
#define SR_RX_DIRECTION         0x00000000

/* Interrupt Descriptor bit macros */
#define GROUP10                     29  /* Position index for the 2 LS group
                                         * bits */
#define CHANNEL                     24  /* Position index for channel bits */
#define INT_IQD_TX          0x80000000
#define INT_IQD_GRP         0x60000000
#define INT_IQD_CHAN        0x1f000000
#define INT_IQD_EVENT       0x00f00000
#define INT_IQD_ERROR       0x000f0000
#define INT_IQD_ILOST       0x00008000
#define INT_IQD_PERR        0x00004000
#define INT_IQD_BLEN        0x00003fff

/* Interrupt Descriptor Events */
#define EVE_EVENT               20      /* Position index for event bits */
#define EVE_NONE                0       /* No event to report in this
                                         * interrupt */
#define EVE_SACK                1       /* Service Request acknowledge */
#define EVE_EOB                 2       /* End of Buffer */
#define EVE_EOM                 3       /* End of Message */
#define EVE_EOP                 4       /* End of Padfill */
#define EVE_CHABT               5       /* Change to Abort Code */
#define EVE_CHIC                6       /* Change to Idle Code */
#define EVE_FREC                7       /* Frame Recovery */
#define EVE_SINC                8       /* MTP2 SUERM Increment */
#define EVE_SDEC                9       /* MTP2 SUERM Decrement */
#define EVE_SFILT               10      /* MTP2 SUERM Filtered Message */
/* Interrupt Descriptor Errors */
#define ERR_ERRORS              16      /* Position index for error bits */
#define ERR_BUF                 1       /* Buffer Error */
#define ERR_COFA                2       /* Change of Frame Alignment Error */
#define ERR_ONR                 3       /* Owner Bit Error */
#define ERR_PROT                4       /* Memory Protection Error */
#define ERR_OOF                 8       /* Out of Frame Error */
#define ERR_FCS                 9       /* FCS Error */
#define ERR_ALIGN               10      /* Octet Alignment Error */
#define ERR_ABT                 11      /* Abort Termination */
#define ERR_LNG                 12      /* Long Message Error */
#define ERR_SHT                 13      /* Short Message Error */
#define ERR_SUERR               14      /* SUERM threshold exceeded */
#define ERR_PERR                15      /* PCI Parity Error */
/* Other Stuff */
#define TRANSMIT_DIRECTION  0x80000000  /* Transmit direction bit. Bit off
                                         * indicates receive direction */
#define ILOST               0x00008000  /* Interrupt Lost */
#define GROUPMSB            0x00004000  /* Group number MSB */
#define SACK_IMAGE          0x00100000  /* Used in IRQ for semaphore test */
#define INITIAL_STATUS      0x10000     /* IRQ status should be this after
                                         * reset */

/*  This must be defined on an entire channel group (Port) basis */
#define SUERM_THRESHOLD     0x1f

#ifdef __cplusplus
}
#endif

#undef VINT32
#undef VINT8

#endif                          /*** _INC_MUSYCC_H_ ***/

/*** End-of-File ***/

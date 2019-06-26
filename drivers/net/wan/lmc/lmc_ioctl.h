/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LMC_IOCTL_H_
#define _LMC_IOCTL_H_
/*	$Id: lmc_ioctl.h,v 1.15 2000/04/06 12:16:43 asj Exp $	*/

 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  */

#define LMCIOCGINFO             SIOCDEVPRIVATE+3 /* get current state */
#define LMCIOCSINFO             SIOCDEVPRIVATE+4 /* set state to user values */
#define LMCIOCGETLMCSTATS       SIOCDEVPRIVATE+5
#define LMCIOCCLEARLMCSTATS     SIOCDEVPRIVATE+6
#define LMCIOCDUMPEVENTLOG      SIOCDEVPRIVATE+7
#define LMCIOCGETXINFO          SIOCDEVPRIVATE+8
#define LMCIOCSETCIRCUIT        SIOCDEVPRIVATE+9
#define LMCIOCUNUSEDATM         SIOCDEVPRIVATE+10
#define LMCIOCRESET             SIOCDEVPRIVATE+11
#define LMCIOCT1CONTROL         SIOCDEVPRIVATE+12
#define LMCIOCIFTYPE            SIOCDEVPRIVATE+13
#define LMCIOCXILINX            SIOCDEVPRIVATE+14

#define LMC_CARDTYPE_UNKNOWN            -1
#define LMC_CARDTYPE_HSSI               1       /* probed card is a HSSI card */
#define LMC_CARDTYPE_DS3                2       /* probed card is a DS3 card */
#define LMC_CARDTYPE_SSI                3       /* probed card is a SSI card */
#define LMC_CARDTYPE_T1                 4       /* probed card is a T1 card */

#define LMC_CTL_CARDTYPE_LMC5200	0	/* HSSI */
#define LMC_CTL_CARDTYPE_LMC5245	1	/* DS3 */
#define LMC_CTL_CARDTYPE_LMC1000	2	/* SSI, V.35 */
#define LMC_CTL_CARDTYPE_LMC1200        3       /* DS1 */

#define LMC_CTL_OFF			0	/* generic OFF value */
#define LMC_CTL_ON			1	/* generic ON value */

#define LMC_CTL_CLOCK_SOURCE_EXT	0	/* clock off line */
#define LMC_CTL_CLOCK_SOURCE_INT	1	/* internal clock */

#define LMC_CTL_CRC_LENGTH_16		16
#define LMC_CTL_CRC_LENGTH_32		32
#define LMC_CTL_CRC_BYTESIZE_2          2
#define LMC_CTL_CRC_BYTESIZE_4          4


#define LMC_CTL_CABLE_LENGTH_LT_100FT	0	/* DS3 cable < 100 feet */
#define LMC_CTL_CABLE_LENGTH_GT_100FT	1	/* DS3 cable >= 100 feet */

#define LMC_CTL_CIRCUIT_TYPE_E1 0
#define LMC_CTL_CIRCUIT_TYPE_T1 1

/*
 * IFTYPE defines
 */
#define LMC_PPP         1               /* use generic HDLC interface */
#define LMC_NET         2               /* use direct net interface */
#define LMC_RAW         3               /* use direct net interface */

/*
 * These are not in the least IOCTL related, but I want them common.
 */
/*
 * assignments for the GPIO register on the DEC chip (common)
 */
#define LMC_GEP_INIT		0x01 /* 0: */
#define LMC_GEP_RESET		0x02 /* 1: */
#define LMC_GEP_MODE		0x10 /* 4: */
#define LMC_GEP_DP		0x20 /* 5: */
#define LMC_GEP_DATA		0x40 /* 6: serial out */
#define LMC_GEP_CLK	        0x80 /* 7: serial clock */

/*
 * HSSI GPIO assignments
 */
#define LMC_GEP_HSSI_ST		0x04 /* 2: receive timing sense (deprecated) */
#define LMC_GEP_HSSI_CLOCK	0x08 /* 3: clock source */

/*
 * T1 GPIO assignments
 */
#define LMC_GEP_SSI_GENERATOR	0x04 /* 2: enable prog freq gen serial i/f */
#define LMC_GEP_SSI_TXCLOCK	0x08 /* 3: provide clock on TXCLOCK output */

/*
 * Common MII16 bits
 */
#define LMC_MII16_LED0         0x0080
#define LMC_MII16_LED1         0x0100
#define LMC_MII16_LED2         0x0200
#define LMC_MII16_LED3         0x0400  /* Error, and the red one */
#define LMC_MII16_LED_ALL      0x0780  /* LED bit mask */
#define LMC_MII16_FIFO_RESET   0x0800

/*
 * definitions for HSSI
 */
#define LMC_MII16_HSSI_TA      0x0001
#define LMC_MII16_HSSI_CA      0x0002
#define LMC_MII16_HSSI_LA      0x0004
#define LMC_MII16_HSSI_LB      0x0008
#define LMC_MII16_HSSI_LC      0x0010
#define LMC_MII16_HSSI_TM      0x0020
#define LMC_MII16_HSSI_CRC     0x0040

/*
 * assignments for the MII register 16 (DS3)
 */
#define LMC_MII16_DS3_ZERO	0x0001
#define LMC_MII16_DS3_TRLBK	0x0002
#define LMC_MII16_DS3_LNLBK	0x0004
#define LMC_MII16_DS3_RAIS	0x0008
#define LMC_MII16_DS3_TAIS	0x0010
#define LMC_MII16_DS3_BIST	0x0020
#define LMC_MII16_DS3_DLOS	0x0040
#define LMC_MII16_DS3_CRC	0x1000
#define LMC_MII16_DS3_SCRAM	0x2000
#define LMC_MII16_DS3_SCRAM_LARS 0x4000

/* Note: 2 pairs of LEDs where swapped by mistake
 * in Xilinx code for DS3 & DS1 adapters */
#define LMC_DS3_LED0    0x0100          /* bit 08  yellow */
#define LMC_DS3_LED1    0x0080          /* bit 07  blue   */
#define LMC_DS3_LED2    0x0400          /* bit 10  green  */
#define LMC_DS3_LED3    0x0200          /* bit 09  red    */

/*
 * framer register 0 and 7 (7 is latched and reset on read)
 */
#define LMC_FRAMER_REG0_DLOS            0x80    /* digital loss of service */
#define LMC_FRAMER_REG0_OOFS            0x40    /* out of frame sync */
#define LMC_FRAMER_REG0_AIS             0x20    /* alarm indication signal */
#define LMC_FRAMER_REG0_CIS             0x10    /* channel idle */
#define LMC_FRAMER_REG0_LOC             0x08    /* loss of clock */

/*
 * Framer register 9 contains the blue alarm signal
 */
#define LMC_FRAMER_REG9_RBLUE          0x02     /* Blue alarm failure */

/*
 * Framer register 0x10 contains xbit error
 */
#define LMC_FRAMER_REG10_XBIT          0x01     /* X bit error alarm failure */

/*
 * And SSI, LMC1000
 */
#define LMC_MII16_SSI_DTR	0x0001	/* DTR output RW */
#define LMC_MII16_SSI_DSR	0x0002	/* DSR input RO */
#define LMC_MII16_SSI_RTS	0x0004	/* RTS output RW */
#define LMC_MII16_SSI_CTS	0x0008	/* CTS input RO */
#define LMC_MII16_SSI_DCD	0x0010	/* DCD input RO */
#define LMC_MII16_SSI_RI		0x0020	/* RI input RO */
#define LMC_MII16_SSI_CRC                0x1000  /* CRC select - RW */

/*
 * bits 0x0080 through 0x0800 are generic, and described
 * above with LMC_MII16_LED[0123] _LED_ALL, and _FIFO_RESET
 */
#define LMC_MII16_SSI_LL		0x1000	/* LL output RW */
#define LMC_MII16_SSI_RL		0x2000	/* RL output RW */
#define LMC_MII16_SSI_TM		0x4000	/* TM input RO */
#define LMC_MII16_SSI_LOOP	0x8000	/* loopback enable RW */

/*
 * Some of the MII16 bits are mirrored in the MII17 register as well,
 * but let's keep thing separate for now, and get only the cable from
 * the MII17.
 */
#define LMC_MII17_SSI_CABLE_MASK	0x0038	/* mask to extract the cable type */
#define LMC_MII17_SSI_CABLE_SHIFT 3	/* shift to extract the cable type */

/*
 * And T1, LMC1200
 */
#define LMC_MII16_T1_UNUSED1    0x0003
#define LMC_MII16_T1_XOE                0x0004
#define LMC_MII16_T1_RST                0x0008  /* T1 chip reset - RW */
#define LMC_MII16_T1_Z                  0x0010  /* output impedance T1=1, E1=0 output - RW */
#define LMC_MII16_T1_INTR               0x0020  /* interrupt from 8370 - RO */
#define LMC_MII16_T1_ONESEC             0x0040  /* one second square wave - ro */

#define LMC_MII16_T1_LED0               0x0100
#define LMC_MII16_T1_LED1               0x0080
#define LMC_MII16_T1_LED2               0x0400
#define LMC_MII16_T1_LED3               0x0200
#define LMC_MII16_T1_FIFO_RESET 0x0800

#define LMC_MII16_T1_CRC                0x1000  /* CRC select - RW */
#define LMC_MII16_T1_UNUSED2    0xe000


/* 8370 framer registers  */

#define T1FRAMER_ALARM1_STATUS  0x47
#define T1FRAMER_ALARM2_STATUS  0x48
#define T1FRAMER_FERR_LSB               0x50
#define T1FRAMER_FERR_MSB               0x51    /* framing bit error counter */
#define T1FRAMER_LCV_LSB                0x54
#define T1FRAMER_LCV_MSB                0x55    /* line code violation counter */
#define T1FRAMER_AERR                   0x5A

/* mask for the above AERR register */
#define T1FRAMER_LOF_MASK               (0x0f0) /* receive loss of frame */
#define T1FRAMER_COFA_MASK              (0x0c0) /* change of frame alignment */
#define T1FRAMER_SEF_MASK               (0x03)  /* severely errored frame  */

/* 8370 framer register ALM1 (0x47) values
 * used to determine link status
 */

#define T1F_SIGFRZ      0x01    /* signaling freeze */
#define T1F_RLOF        0x02    /* receive loss of frame alignment */
#define T1F_RLOS        0x04    /* receive loss of signal */
#define T1F_RALOS       0x08    /* receive analog loss of signal or RCKI loss of clock */
#define T1F_RAIS        0x10    /* receive alarm indication signal */
#define T1F_UNUSED      0x20
#define T1F_RYEL        0x40    /* receive yellow alarm */
#define T1F_RMYEL       0x80    /* receive multiframe yellow alarm */

#define LMC_T1F_WRITE       0
#define LMC_T1F_READ        1

typedef struct lmc_st1f_control {
  int command;
  int address;
  int value;
  char __user *data;
} lmc_t1f_control;

enum lmc_xilinx_c {
    lmc_xilinx_reset = 1,
    lmc_xilinx_load_prom = 2,
    lmc_xilinx_load = 3
};

struct lmc_xilinx_control {
    enum lmc_xilinx_c command;
    int len;
    char __user *data;
};

/* ------------------ end T1 defs ------------------- */

#define LMC_MII_LedMask                 0x0780
#define LMC_MII_LedBitPos               7

#endif

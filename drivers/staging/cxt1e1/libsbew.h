#ifndef _INC_LIBSBEW_H_
#define _INC_LIBSBEW_H_

/*-----------------------------------------------------------------------------
 * libsbew.h - common library elements, charge across mulitple boards
 *
 *   This file contains common Ioctl structures and contents definitions.
 *
 * Copyright (C) 2004-2005  SBE, Inc.
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
 */

/********************************/
/**  set driver logging level  **/
/********************************/

/* routine/ioctl: wancfg_set_loglevel() - SBE_IOC_SET_LOGLEVEL */

#define LOG_NONE          0
#define LOG_ERROR         1
#define LOG_SBEBUG3       3     /* hidden, for development/debug usage */
#define LOG_LSCHANGE      5     /* line state change logging */
#define LOG_LSIMMEDIATE   6     /* line state change logging w/o hysterisis */
#define LOG_WARN          8
#define LOG_MONITOR      10
#define LOG_SBEBUG12     12     /* hidden, for development/debug usage */
#define LOG_MONITOR2     14     /* hidden, for development/debug usage */
#define LOG_DEBUG        16

    /* TEMPORARY DEFINES *//* RLD DEBUG */
#define c4_LOG_NONE      LOG_NONE
#define c4_LOG_ERROR     LOG_ERROR
#define c4_LOG_WARN      LOG_WARN
#define c4_LOG_sTrace    LOG_MONITOR    /* do some trace logging into
                                         * functions */
#define c4_LOG_DEBUG     LOG_DEBUG
#define c4_LOG_MAX       LOG_DEBUG



/******************************/
/**  get driver information  **/
/******************************/

/* routine/ioctl: wancfg_get_drvinfo() - SBE_IOC_GET_DRVINFO */

#define REL_STRLEN   80
    struct sbe_drv_info
    {
        int         rel_strlen;
        char        release[REL_STRLEN];
    };


/*****************************/
/**  get board information  **/
/*****************************/

/* routine/ioctl: wancfg_get_brdinfo() - SBE_IOC_GET_BRDINFO */

#define CHNM_STRLEN   16
    struct sbe_brd_info
    {
        u_int32_t brd_id;       /* SBE's unique PCI VENDOR/DEVID */
        u_int32_t   brd_sn;
        int         brd_chan_cnt;       /* number of channels being used */
        int         brd_port_cnt;       /* number of ports being used */
        unsigned char brdno;    /* our board number */
        unsigned char brd_pci_speed;    /* PCI speed, 33/66Mhz */
                    u_int8_t brd_mac_addr[6];
        char        first_iname[CHNM_STRLEN];   /* first assigned channel's
                                                 * interface name */
        char        last_iname[CHNM_STRLEN];    /* last assigned channel's
                                                 * interface name */
        u_int8_t    brd_hdw_id; /* on/board unique hdw ID */
        u_int8_t    reserved8[3];       /* alignment preservation */
        u_int32_t   reserved32[3];      /* size preservation */
    };

/* These IDs are sometimes available thru pci_ids.h, but not currently. */

#define PCI_VENDOR_ID_SBE              0x1176
#define PCI_DEVICE_ID_WANPMC_C4T1E1    0x0701   /* BID 0x0X, BTYP 0x0X */
#define PCI_DEVICE_ID_WANPTMC_C4T1E1   0x0702   /* BID 0x41 */
#define PCI_DEVICE_ID_WANADAPT_HC4T1E1 0x0703   /* BID 0x44 */
#define PCI_DEVICE_ID_WANPTMC_256T3_T1 0x0704   /* BID 0x42 (T1 Version) */
#define PCI_DEVICE_ID_WANPCI_C4T1E1    0x0705   /* BID 0x1X, BTYP 0x0X */
#define PCI_DEVICE_ID_WANPMC_C1T3      0x0706   /* BID 0x45 */
#define PCI_DEVICE_ID_WANPCI_C2T1E1    0x0707   /* BID 0x1X, BTYP 0x2X */
#define PCI_DEVICE_ID_WANPCI_C1T1E1    0x0708   /* BID 0x1X, BTYP 0x1X */
#define PCI_DEVICE_ID_WANPMC_C2T1E1    0x0709   /* BID 0x0X, BTYP 0x2X */
#define PCI_DEVICE_ID_WANPMC_C1T1E1    0x070A   /* BID 0x0X, BTYP 0x1X */
#define PCI_DEVICE_ID_WANPTMC_256T3_E1 0x070B   /* BID 0x46 (E1 Version) */
#define PCI_DEVICE_ID_WANPTMC_C24TE1   0x070C   /* BID 0x47 */
#define PCI_DEVICE_ID_WANPMC_C4T1E1_L  0x070D   /* BID 0x2X, BTYPE 0x0X w/FP
                                                 * LEDs */
#define PCI_DEVICE_ID_WANPMC_C2T1E1_L  0x070E   /* BID 0x2X, BTYPE 0x2X w/FP
                                                 * LEDs */
#define PCI_DEVICE_ID_WANPMC_C1T1E1_L  0x070F   /* BID 0x2X, BTYPE 0x1X w/FP
                                                 * LEDs */
#define PCI_DEVICE_ID_WANPMC_2SSI      0x0801
#define PCI_DEVICE_ID_WANPCI_4SSI      0x0802
#define PCI_DEVICE_ID_WANPMC_2T3E3     0x0900   /* BID 0x43 */
#define SBE_BOARD_ID(v,id)           ((v<<16) | id)

#define BINFO_PCI_SPEED_unk     0
#define BINFO_PCI_SPEED_33      1
#define BINFO_PCI_SPEED_66      2

/***************************/
/**  obtain interface ID  **/
/***************************/

/* routine/ioctl: wancfg_get_iid() - SBE_IOC_IID_GET */

    struct sbe_iid_info
    {
        u_int32_t   channum;    /* channel requested */
        char        iname[CHNM_STRLEN]; /* channel's interface name */
    };

/**************************************/
/**  get board address information  **/
/**************************************/

/* routine/ioctl: wancfg_get_brdaddr() - SBE_IOC_BRDADDR_GET */

    struct sbe_brd_addr
    {
        unsigned char func;     /* select PCI address space function */
        unsigned char brdno;    /* returns brdno requested */
        unsigned char irq;
        unsigned char size;     /* returns size of address */
#define BRDADDR_SIZE_64  1
#define BRDADDR_SIZE_32  2
        int         reserved1;  /* mod64 align, reserved for future use */

        union
        {
            unsigned long virt64;       /* virtual/mapped address */
            u_int32_t   virt32[2];
        }           v;
        union
        {
            unsigned long phys64;       /* physical bus address */
            u_int32_t   phys32[2];
        }           p;
        int         reserved2[4];       /* reserved for future use */
    };

/**********************************/
/**  read/write board registers  **/
/**********************************/

/* routine/ioctl: wancfg_read_vec() - SBE_IOC_READ_VEC */
/* routine/ioctl: wancfg_write_vec() - SBE_IOC_WRITE_VEC */

    struct sbecom_wrt_vec
    {
        u_int32_t   reg;
        u_int32_t   data;
    };

#define C1T3_CHIP_MSCC_32        0x01000000
#define C1T3_CHIP_TECT3_8        0x02000000
#define C1T3_CHIP_CPLD_8         0x03000000
#define C1T3_CHIP_EEPROM_8       0x04000000

#define W256T3_CHIP_MUSYCC_32    0x02000000
#define W256T3_CHIP_TEMUX_8      0x10000000
#define W256T3_CHIP_T8110_8      0x20000000
#define W256T3_CHIP_T8110_32     0x22000000
#define W256T3_CHIP_CPLD_8       0x30000000
#define W256T3_CHIP_EEPROM_8     0x40000000


/**********************************/
/**  read write port parameters  **/
/**********************************/

/* routine/ioctl: wancfg_getset_port_param() - SBE_IOC_PORT_GET */
/* routine/ioctl: wancfg_set_port_param() - SBE_IOC_PORT_SET */

/* NOTE: this structure supports hardware which supports individual per/port control */

struct sbecom_port_param
{
    u_int8_t    portnum;
    u_int8_t    port_mode;           /* variations of T1 or E1 mode */
    u_int8_t    portStatus;
    u_int8_t    portP;          /* more port parameters (clock source - 0x80;
                                 * and LBO - 0xf; */
                                /* bits 0x70 are reserved for future use ) */
#ifdef SBE_PMCC4_ENABLE
	u_int32_t   hypersize;  /* RLD DEBUG - add this in until I learn how to make this entry obsolete */
#endif
    int         reserved[3-1];    /* reserved for future use */
    int    _res[4];
};

#define CFG_CLK_PORT_MASK      0x80     /* Loop timing */
#define CFG_CLK_PORT_INTERNAL  0x80     /* Loop timing */
#define CFG_CLK_PORT_EXTERNAL  0x00     /* Loop timing */

#define CFG_LBO_MASK      0x0F
#define CFG_LBO_unk       0     /* <not defined> */
#define CFG_LBO_LH0       1     /* T1 Long Haul (default) */
#define CFG_LBO_LH7_5     2     /* T1 Long Haul */
#define CFG_LBO_LH15      3     /* T1 Long Haul */
#define CFG_LBO_LH22_5    4     /* T1 Long Haul */
#define CFG_LBO_SH110     5     /* T1 Short Haul */
#define CFG_LBO_SH220     6     /* T1 Short Haul */
#define CFG_LBO_SH330     7     /* T1 Short Haul */
#define CFG_LBO_SH440     8     /* T1 Short Haul */
#define CFG_LBO_SH550     9     /* T1 Short Haul */
#define CFG_LBO_SH660     10    /* T1 Short Haul */
#define CFG_LBO_E75       11    /* E1 75 Ohm */
#define CFG_LBO_E120      12    /* E1 120 Ohm (default) */


/*************************************/
/**  read write channel parameters  **/
/*************************************/

/* routine/ioctl: wancfg_getset_chan_param() - SBE_IOC_CHAN_GET */
/* routine/ioctl: wancfg_set_chan_param() - SBE_IOC_CHAN_SET */

/* NOTE: this structure supports hardware which supports individual per/channel control */

    struct sbecom_chan_param
    {
        u_int32_t   channum;    /* 0: */
#ifdef SBE_PMCC4_ENABLE
	u_int32_t   card;  /* RLD DEBUG - add this in until I learn how to make this entry obsolete */
	u_int32_t   port;  /* RLD DEBUG - add this in until I learn how to make this entry obsolete */
	u_int8_t bitmask[32];
#endif
        u_int32_t   intr_mask;  /* 4: interrupt mask, specify ored
                                 * (SS7_)INTR_* to disable */
        u_int8_t    status;     /* 8: channel transceiver status (TX_ENABLED,
                                 * RX_ENABLED) */
        u_int8_t    chan_mode;  /* 9: protocol mode */
        u_int8_t    idlecode;   /* A: idle code, in (FLAG_7E, FLAG_FF,
                                 * FLAG_00) */
        u_int8_t    pad_fill_count;     /* B: pad fill count (1-127), 0 - pad
                                         * fill disabled */
        u_int8_t    data_inv;   /* C: channel data inversion selection */
        u_int8_t    mode_56k;   /* D: 56kbps mode */
        u_int8_t    reserved[2 + 8];    /* E: */
    };

/* SS7 interrupt signals <intr_mask> */
#define SS7_INTR_SFILT      0x00000020
#define SS7_INTR_SDEC       0x00000040
#define SS7_INTR_SINC       0x00000080
#define SS7_INTR_SUERR      0x00000100
/* Other interrupts that can be masked */
#define INTR_BUFF           0x00000002
#define INTR_EOM            0x00000004
#define INTR_MSG            0x00000008
#define INTR_IDLE           0x00000010

/* transceiver status flags <status> */
#define TX_ENABLED          0x01
#define RX_ENABLED          0x02

/* Protocol modes <mode> */
#define CFG_CH_PROTO_TRANS         0
#define CFG_CH_PROTO_SS7           1
#define CFG_CH_PROTO_HDLC_FCS16    2
#define CFG_CH_PROTO_HDLC_FCS32    3
#define CFG_CH_PROTO_ISLP_MODE     4

/* Possible idle code assignments <idlecode> */
#define CFG_CH_FLAG_7E      0
#define CFG_CH_FLAG_FF      1
#define CFG_CH_FLAG_00      2

/* data inversion selection <data_inv> */
#define CFG_CH_DINV_NONE    0x00
#define CFG_CH_DINV_RX      0x01
#define CFG_CH_DINV_TX      0x02


/* Possible resettable chipsets/functions */
#define RESET_DEV_TEMUX     1
#define RESET_DEV_TECT3     RESET_DEV_TEMUX
#define RESET_DEV_PLL       2


/*********************************************/
/**  read reset channel thruput statistics  **/
/*********************************************/

/* routine/ioctl: wancfg_get_chan_stats() - SBE_IOC_CHAN_GET_STAT */
/* routine/ioctl: wancfg_del_chan_stats() - SBE_IOC_CHAN_DEL_STAT */
/* routine/ioctl: wancfg_get_card_chan_stats() - SBE_IOC_CARD_CHAN_STAT */

    struct sbecom_chan_stats
    {
        unsigned long rx_packets;       /* total packets received       */
        unsigned long tx_packets;       /* total packets transmitted    */
        unsigned long rx_bytes; /* total bytes received         */
        unsigned long tx_bytes; /* total bytes transmitted      */
        unsigned long rx_errors;/* bad packets received         */
        unsigned long tx_errors;/* packet transmit problems     */
        unsigned long rx_dropped;       /* no space in linux buffers    */
        unsigned long tx_dropped;       /* no space available in linux  */

        /* detailed rx_errors: */
        unsigned long rx_length_errors;
        unsigned long rx_over_errors;   /* receiver ring buff overflow  */
        unsigned long rx_crc_errors;    /* recved pkt with crc error    */
        unsigned long rx_frame_errors;  /* recv'd frame alignment error */
        unsigned long rx_fifo_errors;   /* recv'r fifo overrun          */
        unsigned long rx_missed_errors; /* receiver missed packet       */

        /* detailed tx_errors */
        unsigned long tx_aborted_errors;
        unsigned long tx_fifo_errors;
        unsigned long tx_pending;
    };


/****************************************/
/**  read write card level parameters  **/
/****************************************/

 /* NOTE: this structure supports hardware which supports per/card control */

    struct sbecom_card_param
    {
        u_int8_t    framing_type;       /* 0: CBP or M13 */
        u_int8_t    loopback;   /* 1: one of LOOPBACK_* */
        u_int8_t    line_build_out;     /* 2: boolean */
        u_int8_t    receive_eq; /* 3: boolean */
        u_int8_t    transmit_ones;      /* 4: boolean */
        u_int8_t    clock;      /* 5: 0 - internal, i>0 - external (recovered
                                 * from framer i) */
        u_int8_t    h110enable; /* 6: */
        u_int8_t    disable_leds;       /* 7: */
        u_int8_t    reserved1;  /* 8: available - old 256t3 hypersized, but
                                 * never used */
        u_int8_t    rear_io;    /* 9: rear I/O off/on */
        u_int8_t    disable_tx; /* A: disable TX off/on */
        u_int8_t    mute_los;   /* B: mute LOS off/on */
        u_int8_t    los_threshold;      /* C: LOS threshold norm/low
                                         * (default: norm) */
        u_int8_t    ds1_mode;   /* D: DS1 mode T1/E1 (default: T1) */
        u_int8_t    ds3_unchan; /* E: DS3 unchannelized mode off/on */
        u_int8_t    reserved[1 + 16];   /* reserved for expansion - must be
                                         * ZERO filled */
    };

/* framing types <framing_type> */
#define FRAMING_M13                0
#define FRAMING_CBP                1

/* card level loopback options <loopback> */
#define CFG_CARD_LOOPBACK_NONE     0x00
#define CFG_CARD_LOOPBACK_DIAG     0x01
#define CFG_CARD_LOOPBACK_LINE     0x02
#define CFG_CARD_LOOPBACK_PAYLOAD  0x03

/* line level loopback options <loopback> */
#define CFG_LIU_LOOPBACK_NONE      0x00
#define CFG_LIU_LOOPBACK_ANALOG    0x10
#define CFG_LIU_LOOPBACK_DIGITAL   0x11
#define CFG_LIU_LOOPBACK_REMOTE    0x12

/* card level clock options <clock> */
#define CFG_CLK_INTERNAL           0x00
#define CFG_CLK_EXTERNAL           0x01

/* legacy 256T3 loopback values */
#define LOOPBACK_NONE              0
#define LOOPBACK_LIU_ANALOG        1
#define LOOPBACK_LIU_DIGITAL       2
#define LOOPBACK_FRAMER_DS3        3
#define LOOPBACK_FRAMER_T1         4
#define LOOPBACK_LIU_REMOTE        5

/* DS1 mode <ds1_mode> */
#define CFG_DS1_MODE_MASK          0x0f
#define CFG_DS1_MODE_T1            0x00
#define CFG_DS1_MODE_E1            0x01
#define CFG_DS1_MODE_CHANGE        0x80

/* DS3 unchannelized values <ds1_unchan> */
#define CFG_DS3_UNCHAN_MASK        0x01
#define CFG_DS3_UNCHAN_OFF         0x00
#define CFG_DS3_UNCHAN_ON          0x01


/************************************/
/**  read write framer parameters  **/
/************************************/

/* routine/ioctl: wancfg_get_framer() - SBE_IOC_FRAMER_GET */
/* routine/ioctl: wancfg_set_framer() - SBE_IOC_FRAMER_SET */

    struct sbecom_framer_param
    {
        u_int8_t    framer_num;
        u_int8_t    frame_type; /* SF, ESF, E1PLAIN, E1CAS, E1CRC, E1CRC+CAS */
        u_int8_t    loopback_type;      /* DIGITAL, LINE, PAYLOAD */
        u_int8_t    auto_alarms;/* auto alarms */
        u_int8_t    reserved[12];       /* reserved for expansion - must be
                                         * ZERO filled */
    };

/* frame types <frame_type> */
#define CFG_FRAME_NONE             0
#define CFG_FRAME_SF               1    /* T1 B8ZS */
#define CFG_FRAME_ESF              2    /* T1 B8ZS */
#define CFG_FRAME_E1PLAIN          3    /* HDB3 w/o CAS,CRC */
#define CFG_FRAME_E1CAS            4    /* HDB3 */
#define CFG_FRAME_E1CRC            5    /* HDB3 */
#define CFG_FRAME_E1CRC_CAS        6    /* HDB3 */
#define CFG_FRAME_SF_AMI           7    /* T1 AMI */
#define CFG_FRAME_ESF_AMI          8    /* T1 AMI */
#define CFG_FRAME_E1PLAIN_AMI      9    /* E1 AMI w/o CAS,CRC */
#define CFG_FRAME_E1CAS_AMI       10    /* E1 AMI */
#define CFG_FRAME_E1CRC_AMI       11    /* E1 AMI */
#define CFG_FRAME_E1CRC_CAS_AMI   12    /* E1 AMI */

#define IS_FRAME_ANY_T1(field) \
                    (((field) == CFG_FRAME_NONE) || \
                     ((field) == CFG_FRAME_SF)   || \
                     ((field) == CFG_FRAME_ESF)  || \
                     ((field) == CFG_FRAME_SF_AMI) || \
                     ((field) == CFG_FRAME_ESF_AMI))

#define IS_FRAME_ANY_T1ESF(field) \
                    (((field) == CFG_FRAME_ESF) || \
                     ((field) == CFG_FRAME_ESF_AMI))

#define IS_FRAME_ANY_E1(field) \
                    (((field) == CFG_FRAME_E1PLAIN) || \
                     ((field) == CFG_FRAME_E1CAS)   || \
                     ((field) == CFG_FRAME_E1CRC)   || \
                     ((field) == CFG_FRAME_E1CRC_CAS) || \
                     ((field) == CFG_FRAME_E1PLAIN_AMI) || \
                     ((field) == CFG_FRAME_E1CAS_AMI) || \
                     ((field) == CFG_FRAME_E1CRC_AMI) || \
                     ((field) == CFG_FRAME_E1CRC_CAS_AMI))

#define IS_FRAME_ANY_AMI(field) \
                    (((field) == CFG_FRAME_SF_AMI) || \
                     ((field) == CFG_FRAME_ESF_AMI) || \
                     ((field) == CFG_FRAME_E1PLAIN_AMI) || \
                     ((field) == CFG_FRAME_E1CAS_AMI) || \
                     ((field) == CFG_FRAME_E1CRC_AMI) || \
                     ((field) == CFG_FRAME_E1CRC_CAS_AMI))

/* frame level loopback options <loopback_type> */
#define CFG_FRMR_LOOPBACK_NONE     0
#define CFG_FRMR_LOOPBACK_DIAG     1
#define CFG_FRMR_LOOPBACK_LINE     2
#define CFG_FRMR_LOOPBACK_PAYLOAD  3


/****************************************/
/**  read reset card error statistics  **/
/****************************************/

/* routine/ioctl: wancfg_get_card_stats() - SBE_IOC_CARD_GET_STAT */
/* routine/ioctl: wancfg_del_card_stats() - SBE_IOC_CARD_DEL_STAT */

    struct temux_card_stats
    {
        struct temux_stats
        {
            /* TEMUX DS3 PMON counters */
            u_int32_t   lcv;
            u_int32_t   err_framing;
            u_int32_t   febe;
            u_int32_t   err_cpbit;
            u_int32_t   err_parity;
            /* TEMUX DS3 FRMR status */
            u_int8_t    los;
            u_int8_t    oof;
            u_int8_t    red;
            u_int8_t    yellow;
            u_int8_t    idle;
            u_int8_t    ais;
            u_int8_t    cbit;
            /* TEMUX DS3 FEAC receiver */
            u_int8_t    feac;
            u_int8_t    feac_last;
        }           t;
        u_int32_t   tx_pending; /* total */
    };

/**************************************************************/

    struct wancfg
    {
        int         cs, ds;
        char       *p;
    };
    typedef struct wancfg wcfg_t;

    extern wcfg_t *wancfg_init(char *, char *);
    extern int wancfg_card_blink(wcfg_t *, int);
    extern int wancfg_ctl(wcfg_t *, int, void *, int, void *, int);
    extern int wancfg_del_card_stats(wcfg_t *);
    extern int wancfg_del_chan_stats(wcfg_t *, int);
    extern int wancfg_enable_ports(wcfg_t *, int);
    extern int wancfg_free(wcfg_t *);
    extern int wancfg_get_brdaddr(wcfg_t *, struct sbe_brd_addr *);
    extern int wancfg_get_brdinfo(wcfg_t *, struct sbe_brd_info *);
    extern int wancfg_get_card(wcfg_t *, struct sbecom_card_param *);
    extern int wancfg_get_card_chan_stats(wcfg_t *, struct sbecom_chan_stats *);
    extern int wancfg_get_card_sn(wcfg_t *);
    extern int wancfg_get_card_stats(wcfg_t *, struct temux_card_stats *);
    extern int wancfg_get_chan(wcfg_t *, int, struct sbecom_chan_param *);
    extern int wancfg_get_chan_stats(wcfg_t *, int, struct sbecom_chan_stats *);
    extern int wancfg_get_drvinfo(wcfg_t *, int, struct sbe_drv_info *);
    extern int wancfg_get_framer(wcfg_t *, int, struct sbecom_framer_param *);
    extern int wancfg_get_iid(wcfg_t *, int, struct sbe_iid_info *);
    extern int wancfg_get_sn(wcfg_t *, unsigned int *);
    extern int wancfg_read(wcfg_t *, int, struct sbecom_wrt_vec *);
    extern int wancfg_reset_device(wcfg_t *, int);
    extern int wancfg_set_card(wcfg_t *, struct sbecom_card_param *);
    extern int wancfg_set_chan(wcfg_t *, int, struct sbecom_chan_param *);
    extern int wancfg_set_framer(wcfg_t *, int, struct sbecom_framer_param *);
    extern int wancfg_set_loglevel(wcfg_t *, uint);
    extern int wancfg_write(wcfg_t *, int, struct sbecom_wrt_vec *);

#ifdef NOT_YET_COMMON
    extern int  wancfg_get_tsioc(wcfg_t *, struct wanc1t3_ts_hdr *, struct wanc1t3_ts_param *);
    extern int  wancfg_set_tsioc(wcfg_t *, struct wanc1t3_ts_param *);
#endif

#endif                          /*** _INC_LIBSBEW_H_ ***/

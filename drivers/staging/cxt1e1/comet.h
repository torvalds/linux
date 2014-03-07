#ifndef _INC_COMET_H_
#define _INC_COMET_H_

/*-----------------------------------------------------------------------------
 * comet.h -
 *
 * Copyright (C) 2005  SBE, Inc.
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

#include <linux/types.h>

#define VINT32  volatile u_int32_t

struct s_comet_reg
{
    VINT32 gbl_cfg;      /* 00  Global Cfg */
    VINT32 clkmon;       /* 01  Clk Monitor */
    VINT32 rx_opt;       /* 02  RX Options */
    VINT32 rx_line_cfg;  /* 03  RX Line Interface Cfg */
    VINT32 tx_line_cfg;  /* 04  TX Line Interface Cfg */
    VINT32 tx_frpass;    /* 05  TX Framing & Bypass Options */
    VINT32 tx_time;      /* 06  TX Timing Options */
    VINT32 intr_1;       /* 07  Intr Source #1 */
    VINT32 intr_2;       /* 08  Intr Source #2 */
    VINT32 intr_3;       /* 09  Intr Source #3 */
    VINT32 mdiag;        /* 0A  Master Diagnostics */
    VINT32 mtest;        /* 0B  Master Test */
    VINT32 adiag;        /* 0C  Analog Diagnostics */
    VINT32 rev_id;       /* 0D  Rev/Chip Id/Global PMON Update */
#define pmon  rev_id
    VINT32 reset;        /* 0E  Reset */
    VINT32 prgd_phctl;   /* 0F  PRGD Positioning/Ctl & HDLC Ctl */
    VINT32 cdrc_cfg;     /* 10  CDRC Cfg */
    VINT32 cdrc_ien;     /* 11  CDRC Intr Enable */
    VINT32 cdrc_ists;    /* 12  CDRC Intr Sts */
    VINT32 cdrc_alos;    /* 13  CDRC Alternate Loss of Signal */

    VINT32 rjat_ists;    /* 14  RJAT Intr Sts */
    VINT32 rjat_n1clk;   /* 15  RJAT Reference Clk Divisor (N1) Ctl */
    VINT32 rjat_n2clk;   /* 16  RJAT Output Clk Divisor (N2) Ctl */
    VINT32 rjat_cfg;     /* 17  RJAT Cfg */

    VINT32 tjat_ists;    /* 18  TJAT Intr Sts */
    VINT32 tjat_n1clk;   /* 19  TJAT Reference Clk Divisor (N1) Ctl */
    VINT32 tjat_n2clk;   /* 1A  TJAT Output Clk Divisor (N2) Ctl */
    VINT32 tjat_cfg;     /* 1B  TJAT Cfg */

    VINT32 rx_elst_cfg;      /* 1C  RX-ELST Cfg */
    VINT32 rx_elst_ists;     /* 1D  RX-ELST Intr Sts */
    VINT32 rx_elst_idle;     /* 1E  RX-ELST Idle Code */
    VINT32 _rx_elst_res1f;   /* 1F     RX-ELST Reserved */

    VINT32 tx_elst_cfg;      /* 20  TX-ELST Cfg */
    VINT32 tx_elst_ists;     /* 21  TX-ELST Intr Sts */
    VINT32 _tx_elst_res22;   /* 22     TX-ELST Reserved */
    VINT32 _tx_elst_res23;   /* 23     TX-ELST Reserved */
    VINT32 __res24;          /* 24     Reserved */
    VINT32 __res25;          /* 25     Reserved */
    VINT32 __res26;          /* 26     Reserved */
    VINT32 __res27;          /* 27     Reserved */

    VINT32 rxce1_ctl;        /* 28  RXCE RX Data Link 1 Ctl */
    VINT32 rxce1_bits;       /* 29  RXCE RX Data Link 1 Bit Select */
    VINT32 rxce2_ctl;        /* 2A  RXCE RX Data Link 2 Ctl */
    VINT32 rxce2_bits;       /* 2B  RXCE RX Data Link 2 Bit Select */
    VINT32 rxce3_ctl;        /* 2C  RXCE RX Data Link 3 Ctl */
    VINT32 rxce3_bits;       /* 2D  RXCE RX Data Link 3 Bit Select */
    VINT32 _rxce_res2E;      /* 2E     RXCE Reserved */
    VINT32 _rxce_res2F;      /* 2F     RXCE Reserved */

    VINT32 brif_cfg;         /* 30  BRIF RX Backplane Cfg */
    VINT32 brif_fpcfg;       /* 31  BRIF RX Backplane Frame Pulse Cfg */
    VINT32 brif_pfcfg;       /* 32  BRIF RX Backplane Parity/F-Bit Cfg */
    VINT32 brif_tsoff;       /* 33  BRIF RX Backplane Time Slot Offset */
    VINT32 brif_boff;        /* 34  BRIF RX Backplane Bit Offset */
    VINT32 _brif_res35;      /* 35     BRIF RX Backplane Reserved */
    VINT32 _brif_res36;      /* 36     BRIF RX Backplane Reserved */
    VINT32 _brif_res37;      /* 37     BRIF RX Backplane Reserved */

    VINT32 txci1_ctl;        /* 38  TXCI TX Data Link 1 Ctl */
    VINT32 txci1_bits;       /* 39  TXCI TX Data Link 2 Bit Select */
    VINT32 txci2_ctl;        /* 3A  TXCI TX Data Link 1 Ctl */
    VINT32 txci2_bits;       /* 3B  TXCI TX Data Link 2 Bit Select */
    VINT32 txci3_ctl;        /* 3C  TXCI TX Data Link 1 Ctl */
    VINT32 txci3_bits;       /* 3D  TXCI TX Data Link 2 Bit Select */
    VINT32 _txci_res3E;      /* 3E     TXCI Reserved */
    VINT32 _txci_res3F;      /* 3F     TXCI Reserved */

    VINT32 btif_cfg;         /* 40  BTIF TX Backplane Cfg */
    VINT32 btif_fpcfg;       /* 41  BTIF TX Backplane Frame Pulse Cfg */
    VINT32 btif_pcfgsts;     /* 42  BTIF TX Backplane Parity Cfg & Sts */
    VINT32 btif_tsoff;       /* 43  BTIF TX Backplane Time Slot Offset */
    VINT32 btif_boff;        /* 44  BTIF TX Backplane Bit Offset */
    VINT32 _btif_res45;      /* 45     BTIF TX Backplane Reserved */
    VINT32 _btif_res46;      /* 46     BTIF TX Backplane Reserved */
    VINT32 _btif_res47;      /* 47     BTIF TX Backplane Reserved */
    VINT32 t1_frmr_cfg;      /* 48  T1 FRMR Cfg */
    VINT32 t1_frmr_ien;      /* 49  T1 FRMR Intr Enable */
    VINT32 t1_frmr_ists;     /* 4A  T1 FRMR Intr Sts */
    VINT32 __res_4B;         /* 4B     Reserved */
    VINT32 ibcd_cfg;         /* 4C  IBCD Cfg */
    VINT32 ibcd_ies;         /* 4D  IBCD Intr Enable/Sts */
    VINT32 ibcd_act;         /* 4E  IBCD Activate Code */
    VINT32 ibcd_deact;       /* 4F  IBCD Deactivate Code */

    VINT32 sigx_cfg;         /* 50  SIGX Cfg/Change of Signaling State */
    VINT32 sigx_acc_cos;     /* 51  SIGX uP Access Sts/Change of Signaling State */
    VINT32 sigx_iac_cos;     /* 52  SIGX Channel Indirect
                              * Addr/Ctl/Change of Signaling State */
    VINT32 sigx_idb_cos;     /* 53  SIGX Channel Indirect Data
                              * Buffer/Change of Signaling State */

    VINT32 t1_xbas_cfg;      /* 54  T1 XBAS Cfg */
    VINT32 t1_xbas_altx;     /* 55  T1 XBAS Alarm TX */
    VINT32 t1_xibc_ctl;      /* 56  T1 XIBC Ctl */
    VINT32 t1_xibc_lbcode;   /* 57  T1 XIBC Loopback Code */

    VINT32 pmon_ies;         /* 58  PMON Intr Enable/Sts */
    VINT32 pmon_fberr;       /* 59  PMON Framing Bit Err Cnt */
    VINT32 pmon_feb_lsb;     /* 5A  PMON OFF/COFA/Far End Block Err Cnt (LSB) */
    VINT32 pmon_feb_msb;     /* 5B  PMON OFF/COFA/Far End Block Err Cnt (MSB) */
    VINT32 pmon_bed_lsb;     /* 5C  PMON Bit/Err/CRCE Cnt (LSB) */
    VINT32 pmon_bed_msb;     /* 5D  PMON Bit/Err/CRCE Cnt (MSB) */
    VINT32 pmon_lvc_lsb;     /* 5E  PMON LVC Cnt (LSB) */
    VINT32 pmon_lvc_msb;     /* 5F  PMON LVC Cnt (MSB) */

    VINT32 t1_almi_cfg;      /* 60  T1 ALMI Cfg */
    VINT32 t1_almi_ien;      /* 61  T1 ALMI Intr Enable */
    VINT32 t1_almi_ists;     /* 62  T1 ALMI Intr Sts */
    VINT32 t1_almi_detsts;   /* 63  T1 ALMI Alarm Detection Sts */

    VINT32 _t1_pdvd_res64;   /* 64     T1 PDVD Reserved */
    VINT32 t1_pdvd_ies;      /* 65  T1 PDVD Intr Enable/Sts */
    VINT32 _t1_xboc_res66;   /* 66     T1 XBOC Reserved */
    VINT32 t1_xboc_code;     /* 67  T1 XBOC Code */
    VINT32 _t1_xpde_res68;   /* 68     T1 XPDE Reserved */
    VINT32 t1_xpde_ies;      /* 69  T1 XPDE Intr Enable/Sts */

    VINT32 t1_rboc_ena;      /* 6A  T1 RBOC Enable */
    VINT32 t1_rboc_sts;      /* 6B  T1 RBOC Code Sts */

    VINT32 t1_tpsc_cfg;      /* 6C  TPSC Cfg */
    VINT32 t1_tpsc_sts;      /* 6D  TPSC uP Access Sts */
    VINT32 t1_tpsc_ciaddr;   /* 6E  TPSC Channel Indirect
                                          * Addr/Ctl */
    VINT32 t1_tpsc_cidata;   /* 6F  TPSC Channel Indirect Data
                                          * Buffer */
    VINT32 t1_rpsc_cfg;      /* 70  RPSC Cfg */
    VINT32 t1_rpsc_sts;      /* 71  RPSC uP Access Sts */
    VINT32 t1_rpsc_ciaddr;   /* 72  RPSC Channel Indirect
                                          * Addr/Ctl */
    VINT32 t1_rpsc_cidata;   /* 73  RPSC Channel Indirect Data
                                          * Buffer */
    VINT32 __res74;          /* 74     Reserved */
    VINT32 __res75;          /* 75     Reserved */
    VINT32 __res76;          /* 76     Reserved */
    VINT32 __res77;          /* 77     Reserved */

    VINT32 t1_aprm_cfg;      /* 78  T1 APRM Cfg/Ctl */
    VINT32 t1_aprm_load;     /* 79  T1 APRM Manual Load */
    VINT32 t1_aprm_ists;     /* 7A  T1 APRM Intr Sts */
    VINT32 t1_aprm_1sec_2;   /* 7B  T1 APRM One Second Content Octet 2 */
    VINT32 t1_aprm_1sec_3;   /* 7C  T1 APRM One Second Content Octet 3 */
    VINT32 t1_aprm_1sec_4;   /* 7D  T1 APRM One Second Content Octet 4 */
    VINT32 t1_aprm_1sec_5;   /* 7E  T1 APRM One Second Content MSB (Octect 5) */
    VINT32 t1_aprm_1sec_6;   /* 7F  T1 APRM One Second Content MSB (Octect 6) */

    VINT32 e1_tran_cfg;      /* 80  E1 TRAN Cfg */
    VINT32 e1_tran_txalarm;  /* 81  E1 TRAN TX Alarm/Diagnostic Ctl */
    VINT32 e1_tran_intctl;   /* 82  E1 TRAN International Ctl */
    VINT32 e1_tran_extrab;   /* 83  E1 TRAN Extra Bits Ctl */
    VINT32 e1_tran_ien;      /* 84  E1 TRAN Intr Enable */
    VINT32 e1_tran_ists;     /* 85  E1 TRAN Intr Sts */
    VINT32 e1_tran_nats;     /* 86  E1 TRAN National Bit Codeword
                                          * Select */
    VINT32 e1_tran_nat;      /* 87  E1 TRAN National Bit Codeword */
    VINT32 __res88;          /* 88     Reserved */
    VINT32 __res89;          /* 89     Reserved */
    VINT32 __res8A;          /* 8A     Reserved */
    VINT32 __res8B;          /* 8B     Reserved */

    VINT32 _t1_frmr_res8C;   /* 8C     T1 FRMR Reserved */
    VINT32 _t1_frmr_res8D;   /* 8D     T1 FRMR Reserved */
    VINT32 __res8E;          /* 8E     Reserved */
    VINT32 __res8F;          /* 8F     Reserved */

    VINT32 e1_frmr_aopts;    /* 90  E1 FRMR Frame Alignment Options */
    VINT32 e1_frmr_mopts;    /* 91  E1 FRMR Maintenance Mode Options */
    VINT32 e1_frmr_ien;      /* 92  E1 FRMR Framing Sts Intr Enable */
    VINT32 e1_frmr_mien;     /* 93  E1 FRMR Maintenance/Alarm Sts Intr Enable */
    VINT32 e1_frmr_ists;     /* 94  E1 FRMR Framing Sts Intr Indication */
    VINT32 e1_frmr_mists;    /* 95  E1 FRMR Maintenance/Alarm Sts Indication Enable */
    VINT32 e1_frmr_sts;      /* 96  E1 FRMR Framing Sts */
    VINT32 e1_frmr_masts;    /* 97  E1 FRMR Maintenance/Alarm Sts */
    VINT32 e1_frmr_nat_bits; /* 98  E1 FRMR International/National Bits */
    VINT32 e1_frmr_crc_lsb;  /* 99  E1 FRMR CRC Err Cnt - LSB */
    VINT32 e1_frmr_crc_msb;  /* 9A  E1 FRMR CRC Err Cnt - MSB */
    VINT32 e1_frmr_nat_ien;  /* 9B  E1 FRMR National Bit Codeword Intr Enables */
    VINT32 e1_frmr_nat_ists; /* 9C  E1 FRMR National Bit Codeword Intr/Sts */
    VINT32 e1_frmr_nat;      /* 9D  E1 FRMR National Bit Codewords */
    VINT32 e1_frmr_fp_ien;   /* 9E  E1 FRMR Frame Pulse/Alarm Intr Enables */
    VINT32 e1_frmr_fp_ists;  /* 9F  E1 FRMR Frame Pulse/Alarm Intr/Sts */

    VINT32 __resA0;          /* A0     Reserved */
    VINT32 __resA1;          /* A1     Reserved */
    VINT32 __resA2;          /* A2     Reserved */
    VINT32 __resA3;          /* A3     Reserved */
    VINT32 __resA4;          /* A4     Reserved */
    VINT32 __resA5;          /* A5     Reserved */
    VINT32 __resA6;          /* A6     Reserved */
    VINT32 __resA7;          /* A7     Reserved */

    VINT32 tdpr1_cfg;        /* A8  TDPR #1 Cfg */
    VINT32 tdpr1_utl;        /* A9  TDPR #1 Upper TX Threshold */
    VINT32 tdpr1_ltl;        /* AA  TDPR #1 Lower TX Threshold */
    VINT32 tdpr1_ien;        /* AB  TDPR #1 Intr Enable */
    VINT32 tdpr1_ists;       /* AC  TDPR #1 Intr Sts/UDR Clear */
    VINT32 tdpr1_data;       /* AD  TDPR #1 TX Data */
    VINT32 __resAE;          /* AE     Reserved */
    VINT32 __resAF;          /* AF     Reserved */
    VINT32 tdpr2_cfg;        /* B0  TDPR #2 Cfg */
    VINT32 tdpr2_utl;        /* B1  TDPR #2 Upper TX Threshold */
    VINT32 tdpr2_ltl;        /* B2  TDPR #2 Lower TX Threshold */
    VINT32 tdpr2_ien;        /* B3  TDPR #2 Intr Enable */
    VINT32 tdpr2_ists;       /* B4  TDPR #2 Intr Sts/UDR Clear */
    VINT32 tdpr2_data;       /* B5  TDPR #2 TX Data */
    VINT32 __resB6;          /* B6     Reserved */
    VINT32 __resB7;          /* B7     Reserved1 */
    VINT32 tdpr3_cfg;        /* B8  TDPR #3 Cfg */
    VINT32 tdpr3_utl;        /* B9  TDPR #3 Upper TX Threshold */
    VINT32 tdpr3_ltl;        /* BA  TDPR #3 Lower TX Threshold */
    VINT32 tdpr3_ien;        /* BB  TDPR #3 Intr Enable */
    VINT32 tdpr3_ists;       /* BC  TDPR #3 Intr Sts/UDR Clear */
    VINT32 tdpr3_data;       /* BD  TDPR #3 TX Data */
    VINT32 __resBE;          /* BE     Reserved */
    VINT32 __resBF;          /* BF     Reserved */

    VINT32 rdlc1_cfg;        /* C0  RDLC #1 Cfg */
    VINT32 rdlc1_intctl;     /* C1  RDLC #1 Intr Ctl */
    VINT32 rdlc1_sts;        /* C2  RDLC #1 Sts */
    VINT32 rdlc1_data;       /* C3  RDLC #1 Data */
    VINT32 rdlc1_paddr;      /* C4  RDLC #1 Primary Addr Match */
    VINT32 rdlc1_saddr;      /* C5  RDLC #1 Secondary Addr Match */
    VINT32 __resC6;          /* C6     Reserved */
    VINT32 __resC7;          /* C7     Reserved */
    VINT32 rdlc2_cfg;        /* C8  RDLC #2 Cfg */
    VINT32 rdlc2_intctl;     /* C9  RDLC #2 Intr Ctl */
    VINT32 rdlc2_sts;        /* CA  RDLC #2 Sts */
    VINT32 rdlc2_data;       /* CB  RDLC #2 Data */
    VINT32 rdlc2_paddr;      /* CC  RDLC #2 Primary Addr Match */
    VINT32 rdlc2_saddr;      /* CD  RDLC #2 Secondary Addr Match */
    VINT32 __resCE;          /* CE     Reserved */
    VINT32 __resCF;          /* CF     Reserved */
    VINT32 rdlc3_cfg;        /* D0  RDLC #3 Cfg */
    VINT32 rdlc3_intctl;     /* D1  RDLC #3 Intr Ctl */
    VINT32 rdlc3_sts;        /* D2  RDLC #3 Sts */
    VINT32 rdlc3_data;       /* D3  RDLC #3 Data */
    VINT32 rdlc3_paddr;      /* D4  RDLC #3 Primary Addr Match */
    VINT32 rdlc3_saddr;      /* D5  RDLC #3 Secondary Addr Match */

    VINT32 csu_cfg;          /* D6  CSU Cfg */
    VINT32 _csu_resD7;       /* D7     CSU Reserved */

    VINT32 rlps_idata3;      /* D8  RLPS Indirect Data, 24-31 */
    VINT32 rlps_idata2;      /* D9  RLPS Indirect Data, 16-23 */
    VINT32 rlps_idata1;      /* DA  RLPS Indirect Data, 8-15 */
    VINT32 rlps_idata0;      /* DB  RLPS Indirect Data, 0-7 */
    VINT32 rlps_eqvr;        /* DC  RLPS Equalizer Voltage Reference
                              *    (E1 missing) */
    VINT32 _rlps_resDD;      /* DD     RLPS Reserved */
    VINT32 _rlps_resDE;      /* DE     RLPS Reserved */
    VINT32 _rlps_resDF;      /* DF     RLPS Reserved */

    VINT32 prgd_ctl;         /* E0  PRGD Ctl */
    VINT32 prgd_ies;         /* E1  PRGD Intr Enable/Sts */
    VINT32 prgd_shift_len;   /* E2  PRGD Shift Length */
    VINT32 prgd_tap;         /* E3  PRGD Tap */
    VINT32 prgd_errin;       /* E4  PRGD Err Insertion */
    VINT32 _prgd_resE5;      /* E5     PRGD Reserved */
    VINT32 _prgd_resE6;      /* E6     PRGD Reserved */
    VINT32 _prgd_resE7;      /* E7     PRGD Reserved */
    VINT32 prgd_patin1;      /* E8  PRGD Pattern Insertion #1 */
    VINT32 prgd_patin2;      /* E9  PRGD Pattern Insertion #2 */
    VINT32 prgd_patin3;      /* EA  PRGD Pattern Insertion #3 */
    VINT32 prgd_patin4;      /* EB  PRGD Pattern Insertion #4 */
    VINT32 prgd_patdet1;     /* EC  PRGD Pattern Detector #1 */
    VINT32 prgd_patdet2;     /* ED  PRGD Pattern Detector #2 */
    VINT32 prgd_patdet3;     /* EE  PRGD Pattern Detector #3 */
    VINT32 prgd_patdet4;     /* EF  PRGD Pattern Detector #4 */

    VINT32 xlpg_cfg;         /* F0  XLPG Line Driver Cfg */
    VINT32 xlpg_ctlsts;      /* F1  XLPG Ctl/Sts */
    VINT32 xlpg_pwave_addr;  /* F2  XLPG Pulse Waveform Storage Write Addr */
    VINT32 xlpg_pwave_data;  /* F3  XLPG Pulse Waveform Storage Data */
    VINT32 xlpg_atest_pctl;  /* F4  XLPG Analog Test Positive Ctl */
    VINT32 xlpg_atest_nctl;  /* F5  XLPG Analog Test Negative Ctl */
    VINT32 xlpg_fdata_sel;   /* F6  XLPG Fuse Data Select */
    VINT32 _xlpg_resF7;      /* F7     XLPG Reserved */

    VINT32 rlps_cfgsts;      /* F8  RLPS Cfg & Sts */
    VINT32 rlps_alos_thresh; /* F9  RLPS ALOS Detection/Clearance Threshold */
    VINT32 rlps_alos_dper;   /* FA  RLPS ALOS Detection Period */
    VINT32 rlps_alos_cper;   /* FB  RLPS ALOS Clearance Period */
    VINT32 rlps_eq_iaddr;    /* FC  RLPS Equalization Indirect Addr */
    VINT32 rlps_eq_rwsel;    /* FD  RLPS Equalization Read/WriteB Select */
    VINT32 rlps_eq_ctlsts;   /* FE  RLPS Equalizer Loop Sts & Ctl */
    VINT32 rlps_eq_cfg;      /* FF  RLPS Equalizer Cfg */
};

typedef struct s_comet_reg comet_t;

/* 00AH: MDIAG Register bit definitions */
#define COMET_MDIAG_ID5        0x40
#define COMET_MDIAG_LBMASK     0x3F
#define COMET_MDIAG_PAYLB      0x20
#define COMET_MDIAG_LINELB     0x10
#define COMET_MDIAG_RAIS       0x08
#define COMET_MDIAG_DDLB       0x04
#define COMET_MDIAG_TXMFP      0x02
#define COMET_MDIAG_TXLOS      0x01
#define COMET_MDIAG_LBOFF      0x00

#undef VINT32

#ifdef __KERNEL__
extern void
init_comet(void *, comet_t *, u_int32_t, int, u_int8_t);
#endif

#endif                          /* _INC_COMET_H_ */

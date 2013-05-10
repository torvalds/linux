
/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef _CARDTYPE_H_
#define _CARDTYPE_H_
#ifndef CARDTYPE_H_WANT_DATA
#define CARDTYPE_H_WANT_DATA   0
#endif
#ifndef CARDTYPE_H_WANT_IDI_DATA
#define CARDTYPE_H_WANT_IDI_DATA  0
#endif
#ifndef CARDTYPE_H_WANT_RESOURCE_DATA
#define CARDTYPE_H_WANT_RESOURCE_DATA 1
#endif
#ifndef CARDTYPE_H_WANT_FILE_DATA
#define CARDTYPE_H_WANT_FILE_DATA  1
#endif
/*
 * D-channel protocol identifiers
 *
 * Attention: Unfortunately the identifiers defined here differ from
 *      the identifiers used in Protocol/1/Common/prot/q931.h .
 *     The only reason for this is that q931.h has not a global
 *     scope and we did not know about the definitions there.
 *     But the definitions here cannot be changed easily because
 *     they are used in setup scripts and programs.
 *     Thus the definitions here have to be mapped if they are
 *     used in the protocol code context !
 *
 * Now the identifiers are defined in the q931lib/constant.h file.
 * Unfortunately this file has also not a global scope.
 * But beginning with PROTTYPE_US any new identifier will get the same
 * value as the corresponding PROT_* definition in q931lib/constant.h !
 */
#define PROTTYPE_MINVAL     0
#define PROTTYPE_ETSI       0
#define PROTTYPE_1TR6       1
#define PROTTYPE_BELG       2
#define PROTTYPE_FRANC      3
#define PROTTYPE_ATEL       4
#define PROTTYPE_NI         5  /* DMS 100, Nortel, National ISDN */
#define PROTTYPE_5ESS       6  /* 5ESS   , AT&T,   5ESS Custom   */
#define PROTTYPE_JAPAN      7
#define PROTTYPE_SWED       8
#define PROTTYPE_US         9  /* US autodetect */
#define PROTTYPE_ITALY      10
#define PROTTYPE_TWAN       11
#define PROTTYPE_AUSTRAL    12
#define PROTTYPE_4ESDN      13
#define PROTTYPE_4ESDS      14
#define PROTTYPE_4ELDS      15
#define PROTTYPE_4EMGC      16
#define PROTTYPE_4EMGI      17
#define PROTTYPE_HONGKONG   18
#define PROTTYPE_RBSCAS     19
#define PROTTYPE_CORNETN    20
#define PROTTYPE_QSIG       21
#define PROTTYPE_NI_EWSD    22 /* EWSD, Siemens, National ISDN   */
#define PROTTYPE_5ESS_NI    23 /* 5ESS, Lucent, National ISDN    */
#define PROTTYPE_T1CORNETN  24
#define PROTTYPE_CORNETNQ   25
#define PROTTYPE_T1CORNETNQ 26
#define PROTTYPE_T1QSIG     27
#define PROTTYPE_E1UNCH     28
#define PROTTYPE_T1UNCH     29
#define PROTTYPE_E1CHAN     30
#define PROTTYPE_T1CHAN     31
#define PROTTYPE_R2CAS      32
#define PROTTYPE_MAXVAL     32
/*
 * Card type identifiers
 */
#define CARD_UNKNOWN                      0
#define CARD_NONE                         0
  /* DIVA cards */
#define CARDTYPE_DIVA_MCA                 0
#define CARDTYPE_DIVA_ISA                 1
#define CARDTYPE_DIVA_PCM                 2
#define CARDTYPE_DIVAPRO_ISA              3
#define CARDTYPE_DIVAPRO_PCM              4
#define CARDTYPE_DIVAPICO_ISA             5
#define CARDTYPE_DIVAPICO_PCM             6
  /* DIVA 2.0 cards */
#define CARDTYPE_DIVAPRO20_PCI            7
#define CARDTYPE_DIVA20_PCI               8
  /* S cards */
#define CARDTYPE_QUADRO_ISA               9
#define CARDTYPE_S_ISA                    10
#define CARDTYPE_S_MCA                    11
#define CARDTYPE_SX_ISA                   12
#define CARDTYPE_SX_MCA                   13
#define CARDTYPE_SXN_ISA                  14
#define CARDTYPE_SXN_MCA                  15
#define CARDTYPE_SCOM_ISA                 16
#define CARDTYPE_SCOM_MCA                 17
#define CARDTYPE_PR_ISA                   18
#define CARDTYPE_PR_MCA                   19
  /* Diva Server cards (formerly called Maestra, later Amadeo) */
#define CARDTYPE_MAESTRA_ISA              20
#define CARDTYPE_MAESTRA_PCI              21
  /* Diva Server cards to be developed (Quadro, Primary rate) */
#define CARDTYPE_DIVASRV_Q_8M_PCI         22
#define CARDTYPE_DIVASRV_P_30M_PCI        23
#define CARDTYPE_DIVASRV_P_2M_PCI         24
#define CARDTYPE_DIVASRV_P_9M_PCI         25
  /* DIVA 2.0 cards */
#define CARDTYPE_DIVA20_ISA               26
#define CARDTYPE_DIVA20U_ISA              27
#define CARDTYPE_DIVA20U_PCI              28
#define CARDTYPE_DIVAPRO20_ISA            29
#define CARDTYPE_DIVAPRO20U_ISA           30
#define CARDTYPE_DIVAPRO20U_PCI           31
  /* DIVA combi cards (piccola ISDN + rockwell V.34 modem) */
#define CARDTYPE_DIVAMOBILE_PCM           32
#define CARDTYPE_TDKGLOBALPRO_PCM         33
  /* DIVA Pro PC OEM card for 'New Media Corporation' */
#define CARDTYPE_NMC_DIVAPRO_PCM          34
  /* DIVA Pro 2.0 OEM cards for 'British Telecom' */
#define CARDTYPE_BT_EXLANE_PCI            35
#define CARDTYPE_BT_EXLANE_ISA            36
  /* DIVA low cost cards, 1st name DIVA 3.0, 2nd DIVA 2.01, 3rd ??? */
#define CARDTYPE_DIVALOW_ISA              37
#define CARDTYPE_DIVALOWU_ISA             38
#define CARDTYPE_DIVALOW_PCI              39
#define CARDTYPE_DIVALOWU_PCI             40
  /* DIVA combi cards (piccola ISDN + rockwell V.90 modem) */
#define CARDTYPE_DIVAMOBILE_V90_PCM       41
#define CARDTYPE_TDKGLOBPRO_V90_PCM       42
#define CARDTYPE_DIVASRV_P_23M_PCI        43
#define CARDTYPE_DIVALOW_USB              44
  /* DIVA Audio (CT) family */
#define CARDTYPE_DIVA_CT_ST               45
#define CARDTYPE_DIVA_CT_U                46
#define CARDTYPE_DIVA_CTLITE_ST           47
#define CARDTYPE_DIVA_CTLITE_U            48
  /* DIVA ISDN plus V.90 series */
#define CARDTYPE_DIVAISDN_V90_PCM         49
#define CARDTYPE_DIVAISDN_V90_PCI         50
#define CARDTYPE_DIVAISDN_TA              51
  /* DIVA Server Voice cards */
#define CARDTYPE_DIVASRV_VOICE_Q_8M_PCI   52
  /* DIVA Server V2 cards */
#define CARDTYPE_DIVASRV_Q_8M_V2_PCI      53
#define CARDTYPE_DIVASRV_P_30M_V2_PCI     54
  /* DIVA Server Voice V2 cards */
#define CARDTYPE_DIVASRV_VOICE_Q_8M_V2_PCI 55
#define CARDTYPE_DIVASRV_VOICE_P_30M_V2_PCI 56
    /* Diva LAN */
#define CARDTYPE_DIVAISDN_LAN             57
#define CARDTYPE_DIVA_202_PCI_ST          58
#define CARDTYPE_DIVA_202_PCI_U           59
#define CARDTYPE_DIVASRV_B_2M_V2_PCI      60
#define CARDTYPE_DIVASRV_B_2F_PCI         61
#define CARDTYPE_DIVALOW_USBV2            62
#define CARDTYPE_DIVASRV_VOICE_B_2M_V2_PCI 63
#define CARDTYPE_DIVA_PRO_30_PCI_ST       64
#define CARDTYPE_DIVA_CT_ST_V20           65
/* Diva Mobile V.90 PC Card and Diva ISDN PC Card */
#define CARDTYPE_DIVAMOBILE_V2_PCM        66
#define CARDTYPE_DIVA_V2_PCM              67
/* Re-badged Diva Pro PC Card */
#define CARDTYPE_DIVA_PC_CARD             68
  /* next free card type identifier */
#define CARDTYPE_MAX                      69
/*
 * The card families
 */
#define FAMILY_DIVA   1
#define FAMILY_S   2
#define FAMILY_MAESTRA  3
#define FAMILY_MAX   4
/*
 * The basic card types
 */
#define CARD_DIVA           1  /* DSP based, old DSP */
#define CARD_PRO            2  /* DSP based, new DSP */
#define CARD_PICO           3  /* HSCX based   */
#define CARD_S    4  /* IDI on board based */
#define CARD_SX    5  /* IDI on board based */
#define CARD_SXN   6  /* IDI on board based */
#define CARD_SCOM   7  /* IDI on board based */
#define CARD_QUAD   8  /* IDI on board based */
#define CARD_PR    9  /* IDI on board based */
#define CARD_MAE         10  /* IDI on board based */
#define CARD_MAEQ        11  /* IDI on board based */
#define CARD_MAEP        12  /* IDI on board based */
#define CARD_DIVALOW  13  /* IPAC based   */
#define CARD_CT    14  /* SCOUT based          */
#define CARD_DIVATA   15  /* DIVA TA */
#define CARD_DIVALAN  16  /* DIVA LAN */
#define CARD_MAE2         17  /* IDI on board based */
#define CARD_MAX   18
/*
 * The internal card types of the S family
 */
#define CARD_I_NONE   0
#define CARD_I_S   0
#define CARD_I_SX   1
#define CARD_I_SCOM   2
#define CARD_I_QUAD   3
#define CARD_I_PR   4
/*
 * The bus types we support
 */
#define BUS_ISA             1
#define BUS_PCM             2
#define BUS_PCI             3
#define BUS_MCA             4
#define BUS_USB             5
#define BUS_COM    6
#define BUS_LAN    7
/*
 * The chips we use for B-channel traffic
 */
#define CHIP_NONE           0
#define CHIP_DSP            1
#define CHIP_HSCX           2
#define CHIP_IPAC           3
#define CHIP_SCOUT          4
#define CHIP_EXTERN         5
#define CHIP_IPACX          6
/*
 * The structures where the card properties are aggregated by id
 */
typedef struct CARD_PROPERTIES
{   char     *Name;  /* official marketing name     */
 unsigned short PnPId;  /* plug and play ID (for non PCMIA cards) */
 unsigned short Version; /* major and minor version no of the card */
 unsigned char DescType; /* card type to set in the IDI descriptor */
 unsigned char  Family;  /* basic family of the card     */
 unsigned short  Features; /* features bits to set in the IDI desc. */
 unsigned char Card;  /* basic card type       */
 unsigned char IType;  /* internal type of S cards (read from ram) */
 unsigned char  Bus;  /* bus type this card is designed for  */
 unsigned char  Chip;  /* chipset used on card      */
 unsigned char Adapters; /* number of adapters on card    */
 unsigned char Channels; /* # of channels per adapter    */
 unsigned short E_info;  /* # of ram entity info structs per adapter */
 unsigned short SizeIo;  /* size of IO window per adapter   */
 unsigned short SizeMem; /* size of memory window per adapter  */
} CARD_PROPERTIES;
typedef struct CARD_RESOURCE
{ unsigned char Int [10];
 unsigned short IoFirst;
 unsigned short IoStep;
 unsigned short IoCnt;
 unsigned long MemFirst;
 unsigned long MemStep;
 unsigned short MemCnt;
} CARD_RESOURCE;
/* test if the card of type 't' is a plug & play card */
#define IS_PNP(t) \
( \
 ( \
  CardProperties[t].Bus != BUS_ISA \
  && \
  CardProperties[t].Bus != BUS_MCA \
 ) \
 || \
 ( \
  CardProperties[t].Family != FAMILY_S \
  && \
  CardProperties[t].Card != CARD_DIVA \
 ) \
)
/* extract IDI Descriptor info for card type 't' (p == DescType/Features) */
#define IDI_PROP(t,p) (CardProperties[t].p)
#if CARDTYPE_H_WANT_DATA
#if CARDTYPE_H_WANT_IDI_DATA
/* include "di_defs.h" for IDI adapter type and feature flag definitions */
#include "di_defs.h"
#else /*!CARDTYPE_H_WANT_IDI_DATA*/
/* define IDI adapter types and feature flags here to prevent inclusion  */
#ifndef IDI_ADAPTER_S
#define IDI_ADAPTER_S           1
#define IDI_ADAPTER_PR          2
#define IDI_ADAPTER_DIVA        3
#define IDI_ADAPTER_MAESTRA     4
#endif
#ifndef DI_VOICE
#define DI_VOICE          0x0 /* obsolete define */
#define DI_FAX3           0x1
#define DI_MODEM          0x2
#define DI_POST           0x4
#define DI_V110           0x8
#define DI_V120           0x10
#define DI_POTS           0x20
#define DI_CODEC          0x40
#define DI_MANAGE         0x80
#define DI_V_42           0x0100
#define DI_EXTD_FAX       0x0200 /* Extended FAX (ECM, 2D, T.6, Polling) */
#define DI_AT_PARSER      0x0400 /* Build-in AT Parser in the L2 */
#define DI_VOICE_OVER_IP  0x0800 /* Voice over IP support */
#endif
#endif /*CARDTYPE_H_WANT_IDI_DATA*/
#define DI_V1x0         (DI_V110 | DI_V120)
#define DI_NULL         0x0000
#if defined(SOFT_DSP_SUPPORT)
#define SOFT_DSP_ADD_FEATURES  (DI_MODEM | DI_FAX3 | DI_AT_PARSER)
#else
#define SOFT_DSP_ADD_FEATURES  0
#endif
#if defined(SOFT_V110_SUPPORT)
#define DI_SOFT_V110  DI_V110
#else
#define DI_SOFT_V110  0
#endif
/*--- CardProperties [Index=CARDTYPE_....] ---------------------------------*/
CARD_PROPERTIES CardProperties [ ] =
{
{ /*  0  */
 "Diva MCA",       0x6336,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3,
 CARD_DIVA,   CARD_I_NONE, BUS_MCA, CHIP_DSP,
 1, 2,  0,   8,      0
},
{ /*  1  */
 "Diva ISA",       0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3,
 CARD_DIVA,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,  0,   8,      0
},
{ /*  2  */
 "Diva/PCM",       0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3,
 CARD_DIVA,   CARD_I_NONE, BUS_PCM, CHIP_DSP,
 1, 2,  0,   8,      0
},
{ /*  3  */
 "Diva PRO ISA",      0x0031,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_PRO,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,  0,   8,      0
},
{ /*  4  */
 "Diva PRO PC-Card",     0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_PRO,   CARD_I_NONE, BUS_PCM, CHIP_DSP,
 1, 2,   0,   8,      0
},
{ /*  5  */
 "Diva PICCOLA ISA",     0x0051,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_ISA, CHIP_HSCX,
 1, 2,   0,   8,      0
},
{ /*  6  */
 "Diva PICCOLA PCM",     0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_HSCX,
 1, 2,   0,   8,      0
},
{ /*  7  */
 "Diva PRO 2.0 S/T PCI",    0xe001,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   0,   8,      0
},
{ /*  8  */
 "Diva 2.0 S/T PCI",     0xe002,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | DI_POTS | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCI, CHIP_HSCX,
 1, 2,   0,   8,      0
},
{ /*  9  */
 "QUADRO ISA",      0x0000,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_NULL,
 CARD_QUAD,   CARD_I_QUAD, BUS_ISA, CHIP_NONE,
 4, 2,   16,  0,  0x800
},
{ /* 10  */
 "S ISA",       0x0000,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_CODEC,
 CARD_S,    CARD_I_S,  BUS_ISA, CHIP_NONE,
 1, 1,   16,  0,  0x800
},
{ /* 11  */
 "S MCA",       0x6a93,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_CODEC,
 CARD_S,    CARD_I_S,  BUS_MCA, CHIP_NONE,
 1, 1,   16,  16,  0x400
},
{ /* 12 */
 "SX ISA",       0x0000,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_NULL,
 CARD_SX,   CARD_I_SX,  BUS_ISA, CHIP_NONE,
 1, 2,  16,  0,  0x800
},
{ /* 13 */
 "SX MCA",       0x6a93,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_NULL,
 CARD_SX,   CARD_I_SX,  BUS_MCA, CHIP_NONE,
 1, 2,  16,  16,  0x400
},
{ /* 14 */
 "SXN ISA",       0x0000,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_NULL,
 CARD_SXN,   CARD_I_SCOM, BUS_ISA, CHIP_NONE,
 1, 2,   16,  0,   0x800
},
{ /* 15 */
 "SXN MCA",       0x6a93,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_NULL,
 CARD_SXN,   CARD_I_SCOM, BUS_MCA, CHIP_NONE,
 1, 2,  16,  16,  0x400
},
{ /* 16 */
 "SCOM ISA",       0x0000,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_CODEC,
 CARD_SCOM,   CARD_I_SCOM, BUS_ISA, CHIP_NONE,
 1, 2,   16,  0,   0x800
},
{ /* 17 */
 "SCOM MCA",       0x6a93,  0x0100,
 IDI_ADAPTER_S,  FAMILY_S,  DI_CODEC,
 CARD_SCOM,   CARD_I_SCOM, BUS_MCA, CHIP_NONE,
 1, 2,  16,  16,  0x400
},
{ /* 18 */
 "S2M ISA",       0x0000,  0x0100,
 IDI_ADAPTER_PR,  FAMILY_S,  DI_NULL,
 CARD_PR,   CARD_I_PR,  BUS_ISA, CHIP_NONE,
 1, 30,  256, 0,   0x4000
},
{ /* 19 */
 "S2M MCA",       0x6abb,  0x0100,
 IDI_ADAPTER_PR,  FAMILY_S,  DI_NULL,
 CARD_PR,   CARD_I_PR,  BUS_MCA, CHIP_NONE,
 1, 30,  256, 16,  0x4000
},
{ /* 20 */
 "Diva Server BRI-2M ISA",   0x0041,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAE,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,   16,  8,  0
},
{ /* 21 */
 "Diva Server BRI-2M PCI",   0xE010,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAE,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   16,  8,   0
},
{ /* 22 */
 "Diva Server 4BRI-8M PCI",   0xE012,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEQ,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 4, 2,   16,  8,   0
},
{ /* 23 */
 "Diva Server PRI-30M PCI",   0xE014,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,  256,  8,   0
},
{ /* 24 */
 "Diva Server PRI-2M PCI",   0xe014,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,  256,  8,   0
},
{ /* 25 */
 "Diva Server PRI-9M PCI",   0x0000,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,     256,  8,   0
},
{ /* 26 */
 "Diva 2.0 S/T ISA",     0x0071,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | DI_POTS | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_ISA, CHIP_HSCX,
 1, 2,  0,   8,   0
},
{ /* 27 */
 "Diva 2.0 U ISA",     0x0091,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | DI_POTS | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_ISA, CHIP_HSCX,
 1, 2,   0,   8,   0
},
{ /* 28 */
 "Diva 2.0 U PCI",     0xe004,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | DI_POTS | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCI, CHIP_HSCX,
 1, 2,   0,   8,   0
},
{ /* 29 */
 "Diva PRO 2.0 S/T ISA",    0x0061,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,  0,   8,   0
},
{ /* 30 */
 "Diva PRO 2.0 U ISA",    0x0081,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,  0,   8,   0
},
{ /* 31 */
 "Diva PRO 2.0 U PCI",    0xe003,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   0,   8,   0
},
{ /* 32 */
 "Diva MOBILE",      0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_HSCX,
 1, 2,  0,   8,   0
},
{ /* 33 */
 "TDK DFI3600",      0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_HSCX,
 1, 2,  0,   8,   0
},
{ /* 34 (OEM version of 4 - "Diva PRO PC-Card") */
 "New Media ISDN",     0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_PRO,   CARD_I_NONE, BUS_PCM, CHIP_DSP,
 1, 2,   0,   8,   0
},
{ /* 35 (OEM version of 7 - "Diva PRO 2.0 S/T PCI") */
 "BT ExLane PCI",     0xe101,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   0,   8,   0
},
{ /* 36 (OEM version of 29 - "Diva PRO 2.0 S/T ISA") */
 "BT ExLane ISA",     0x1061,  0x0200,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_POTS,
 CARD_PRO,   CARD_I_NONE, BUS_ISA, CHIP_DSP,
 1, 2,   0,   8,   0
},
{ /* 37 */
 "Diva 2.01 S/T ISA",    0x00A1,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_ISA, CHIP_IPAC,
 1, 2,   0,   8,      0
},
{ /* 38 */
 "Diva 2.01 U ISA",     0x00B1,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_ISA, CHIP_IPAC,
 1, 2,   0,   8,      0
},
{ /* 39 */
 "Diva 2.01 S/T PCI",    0xe005,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_PCI, CHIP_IPAC,
 1, 2,   0,   8,   0
},
{ /* 40        no ID yet */
 "Diva 2.01 U PCI",     0x0000,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_PCI, CHIP_IPAC,
 1, 2,   0,   8,   0
},
{ /* 41 */
 "Diva MOBILE V.90",     0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_HSCX,
 1, 2,  0,   8,   0
},
{ /* 42 */
 "TDK DFI3600 V.90",     0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_HSCX,
 1, 2,  0,   8,   0
},
{ /* 43 */
 "Diva Server PRI-23M PCI",   0xe014,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,  256,  8,   0
},
{ /* 44 */
 "Diva 2.01 S/T USB",    0x1000,     0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_USB, CHIP_IPAC,
 1,  2,  0,  8,   0
},
{ /* 45 */
 "Diva CT S/T PCI",    0xe006,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_CT,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 46 */
 "Diva CT U PCI",     0xe007,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_CT,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 47 */
 "Diva CT Lite S/T PCI",   0xe008,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_CT,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 48 */
 "Diva CT Lite U PCI",   0xe009,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_CT,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 49 */
 "Diva ISDN+V.90 PC Card", 0x8D8C, 0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_DIVALOW, CARD_I_NONE, BUS_PCM, CHIP_IPAC,
 1, 2,  0,   8,   0
},
{ /* 50 */
 "Diva ISDN+V.90 PCI",    0xe00A,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120  | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_PCI, CHIP_IPAC,
 1, 2,   0,   8,   0
},
{ /* 51 (DivaTA)      no ID */
 "Diva TA",       0x0000,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V110 | DI_FAX3 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVATA,  CARD_I_NONE, BUS_COM, CHIP_EXTERN,
 1, 1,   0,   8,   0
},
{ /* 52 (Diva Server 4BRI-8M PCI adapter enabled for Voice) */
 "Diva Server Voice 4BRI-8M PCI", 0xE016,  0x0100,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_VOICE_OVER_IP,
 CARD_MAEQ,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 4, 2,   16,  8,   0
},
{ /* 53 (Diva Server 4BRI 2.0 adapter) */
 "Diva Server 4BRI-8M 2.0 PCI",  0xE013,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEQ,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 4, 2,   16,  8,   0
},
{ /* 54 (Diva Server PRI 2.0 adapter) */
 "Diva Server PRI 2.0 PCI",   0xE015,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,  256,  8,   0
},
{ /* 55 (Diva Server 4BRI-8M 2.0 PCI adapter enabled for Voice) */
 "Diva Server Voice 4BRI-8M 2.0 PCI", 0xE017,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_VOICE_OVER_IP,
 CARD_MAEQ,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 4, 2,   16,  8,   0
},
{ /* 56 (Diva Server PRI 2.0 PCI adapter enabled for Voice) */
 "Diva Server Voice PRI 2.0 PCI",  0xE019,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_VOICE_OVER_IP,
 CARD_MAEP,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 30,  256,  8,   0
},
{
 /* 57 (DivaLan )      no ID */
 "Diva LAN",       0x0000,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V110 | DI_FAX3 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALAN,  CARD_I_NONE, BUS_LAN, CHIP_EXTERN,
 1, 1,   0,   8,   0
},
{ /* 58 */
 "Diva 2.02 PCI S/T",    0xE00B,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES | DI_SOFT_V110,
 CARD_DIVALOW,  CARD_I_NONE, BUS_PCI, CHIP_IPACX,
 1, 2,   0,   8,   0
},
{ /* 59 */
 "Diva 2.02 PCI U",     0xE00C,  0x0300,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_PCI, CHIP_IPACX,
 1, 2,   0,   8,   0
},
{ /* 60 */
 "Diva Server BRI-2M 2.0 PCI",     0xE018,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_MAE2,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   16,  8,   0
},
{ /* 61  (the previous name was Diva Server BRI-2F 2.0 PCI) */
 "Diva Server 2FX",                      0xE01A,     0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_SOFT_V110,
 CARD_MAE2,          CARD_I_NONE,    BUS_PCI,    CHIP_IPACX,
 1,  2,      16,     8,   0
},
{ /* 62 */
 " Diva ISDN USB 2.0",    0x1003,     0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_DIVALOW,  CARD_I_NONE, BUS_USB, CHIP_IPACX,
 1, 2,  0,  8,   0
},
{ /* 63 (Diva Server BRI-2M 2.0 PCI adapter enabled for Voice) */
 "Diva Server Voice BRI-2M 2.0 PCI", 0xE01B,  0x0200,
 IDI_ADAPTER_MAESTRA,FAMILY_MAESTRA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_VOICE_OVER_IP,
 CARD_MAE2,   CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1, 2,   16,  8,   0
},
{ /* 64 */
 "Diva Pro 3.0 PCI",    0xe00d,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM,
 CARD_PRO,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 65 */
 "Diva ISDN + CT 2.0",    0xE00E,  0x0300,
 IDI_ADAPTER_DIVA   ,FAMILY_DIVA, DI_V1x0 | DI_FAX3 | DI_MODEM | DI_CODEC,
 CARD_CT,       CARD_I_NONE, BUS_PCI, CHIP_DSP,
 1,  2,  0,  0,   0
},
{ /* 66 */
 "Diva Mobile V.90 PC Card",  0x8331,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_IPACX,
 1, 2,  0,   8,   0
},
{ /* 67 */
 "Diva ISDN PC Card",  0x8311,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PICO,   CARD_I_NONE, BUS_PCM, CHIP_IPACX,
 1, 2,  0,   8,   0
},
{ /* 68 */
 "Diva ISDN PC Card",  0x0000,  0x0100,
 IDI_ADAPTER_DIVA, FAMILY_DIVA, DI_V120 | SOFT_DSP_ADD_FEATURES,
 CARD_PRO,   CARD_I_NONE, BUS_PCM, CHIP_DSP,
 1, 2,   0,   8,      0
},
} ;
#if CARDTYPE_H_WANT_RESOURCE_DATA
/*--- CardResource [Index=CARDTYPE_....]   ---------------------------(GEI)-*/
CARD_RESOURCE CardResource [ ] =  {
/*   Interrupts     IO-Address   Mem-Address */
/* 0*/ {  3,4,9,0,0,0,0,0,0,0,   0x200,0x20,16,   0x0,0x0,0   }, // DIVA MCA
/* 1*/ {  3,4,9,10,11,12,0,0,0,0,  0x200,0x20,16,   0x0,0x0,0   }, // DIVA ISA
/* 2*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA PCMCIA
/* 3*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,   0x0,0x0,0   }, // DIVA PRO ISA
/* 4*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA PRO PCMCIA
/* 5*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // DIVA PICCOLA ISA
/* 6*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA PICCOLA PCMCIA
/* 7*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA PRO 2.0 PCI
/* 8*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.0 PCI
/* 9*/ {  3,4,5,7,9,10,11,12,0,0,  0x0,0x0,0,   0x80000,0x2000,64 }, // QUADRO ISA
/*10*/ {  3,4,9,10,11,12,0,0,0,0,  0x0,0x0,0,   0xc0000,0x2000,16 }, // S ISA
/*11*/ {  3,4,9,0,0,0,0,0,0,0,  0xc00,0x10,16,  0xc0000,0x2000,16 }, // S MCA
/*12*/ {  3,4,9,10,11,12,0,0,0,0,  0x0,0x0,0,   0xc0000,0x2000,16 }, // SX ISA
/*13*/ {  3,4,9,0,0,0,0,0,0,0,  0xc00,0x10,16,  0xc0000,0x2000,16 }, // SX MCA
/*14*/ {  3,4,5,7,9,10,11,12,0,0,  0x0,0x0,0,   0x80000,0x0800,256 }, // SXN ISA
/*15*/ {  3,4,9,0,0,0,0,0,0,0,  0xc00,0x10,16,  0xc0000,0x2000,16 }, // SXN MCA
/*16*/ {  3,4,5,7,9,10,11,12,0,0,  0x0,0x0,0,   0x80000,0x0800,256 }, // SCOM ISA
/*17*/ {  3,4,9,0,0,0,0,0,0,0,  0xc00,0x10,16,  0xc0000,0x2000,16 }, // SCOM MCA
/*18*/ {  3,4,5,7,9,10,11,12,0,0,  0x0,0x0,0,   0xc0000,0x4000,16 }, // S2M ISA
/*19*/ {  3,4,9,0,0,0,0,0,0,0,  0xc00,0x10,16,  0xc0000,0x4000,16 }, // S2M MCA
/*20*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // MAESTRA ISA
/*21*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // MAESTRA PCI
/*22*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // MAESTRA QUADRO ISA
/*23*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x20,2048,  0x0,0x0,0   }, // MAESTRA QUADRO PCI
/*24*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // MAESTRA PRIMARY ISA
/*25*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // MAESTRA PRIMARY PCI
/*26*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // DIVA 2.0 ISA
/*27*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // DIVA 2.0 /U ISA
/*28*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.0 /U PCI
/*29*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,   0x0,0x0,0   }, // DIVA PRO 2.0 ISA
/*30*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,   0x0,0x0,0   }, // DIVA PRO 2.0 /U ISA
/*31*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA PRO 2.0 /U PCI
/*32*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA MOBILE
/*33*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // TDK DFI3600 (same as DIVA MOBILE [32])
/*34*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // New Media ISDN (same as DIVA PRO PCMCIA [4])
/*35*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // BT ExLane PCI (same as DIVA PRO 2.0 PCI [7])
/*36*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,   0x0,0x0,0   }, // BT ExLane ISA (same as DIVA PRO 2.0 ISA [29])
/*37*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // DIVA 2.01 S/T ISA
/*38*/ {  3,5,7,9,10,11,12,14,15,0, 0x200,0x20,16,  0x0,0x0,0   }, // DIVA 2.01 U ISA
/*39*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.01 S/T PCI
/*40*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.01 U PCI
/*41*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA MOBILE V.90
/*42*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // TDK DFI3600 V.90 (same as DIVA MOBILE V.90 [39])
/*43*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x20,2048,  0x0,0x0,0   }, // DIVA Server PRI-23M PCI
/*44*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA 2.01 S/T USB
/*45*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA CT S/T PCI
/*46*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA CT U PCI
/*47*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA CT Lite S/T PCI
/*48*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA CT Lite U PCI
/*49*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA ISDN+V.90 PC Card
/*50*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA ISDN+V.90 PCI
/*51*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA TA
/*52*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x20,2048,  0x0,0x0,0   }, // MAESTRA VOICE QUADRO PCI
/*53*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x20,2048,  0x0,0x0,0   }, // MAESTRA VOICE QUADRO PCI
/*54*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // MAESTRA VOICE PRIMARY PCI
/*55*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x20,2048,  0x0,0x0,0   }, // MAESTRA VOICE QUADRO PCI
/*56*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // MAESTRA VOICE PRIMARY PCI
/*57*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA LAN
/*58*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.02 S/T PCI
/*59*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 2.02 U PCI
/*60*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // Diva Server BRI-2M 2.0 PCI
/*61*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // Diva Server BRI-2F PCI
/*62*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA 2.01 S/T USB
/*63*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // Diva Server Voice BRI-2M 2.0 PCI
/*64*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA 3.0 PCI
/*65*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA CT S/T PCI V2.0
/*66*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA Mobile V.90 PC Card
/*67*/ {  0,0,0,0,0,0,0,0,0,0,  0x0,0x0,0,   0x0,0x0,0   }, // DIVA ISDN PC Card
/*68*/ {  3,4,5,7,9,10,11,12,14,15, 0x0,0x8,8192,  0x0,0x0,0   }, // DIVA ISDN PC Card
};
#endif /*CARDTYPE_H_WANT_RESOURCE_DATA*/
#else /*!CARDTYPE_H_WANT_DATA*/
extern CARD_PROPERTIES  CardProperties [] ;
extern CARD_RESOURCE  CardResource [] ;
#endif /*CARDTYPE_H_WANT_DATA*/
/*
 * all existing download files
 */
#define CARD_DSP_CNT  5
#define CARD_PROT_CNT  9
#define CARD_FT_UNKNOWN     0
#define CARD_FT_B   1
#define CARD_FT_D   2
#define CARD_FT_S   3
#define CARD_FT_M   4
#define CARD_FT_NEW_DSP_COMBIFILE 5  /* File format of new DSP code (the DSP code powered by Telindus) */
#define CARD_FILE_NONE      0
#define CARD_B_S   1
#define CARD_B_P   2
#define CARD_D_K1   3
#define CARD_D_K2   4
#define CARD_D_H   5
#define CARD_D_V   6
#define CARD_D_M   7
#define CARD_D_F   8
#define CARD_P_S_E   9
#define CARD_P_S_1   10
#define CARD_P_S_B   11
#define CARD_P_S_F   12
#define CARD_P_S_A   13
#define CARD_P_S_N   14
#define CARD_P_S_5   15
#define CARD_P_S_J   16
#define CARD_P_SX_E   17
#define CARD_P_SX_1   18
#define CARD_P_SX_B   19
#define CARD_P_SX_F   20
#define CARD_P_SX_A   21
#define CARD_P_SX_N   22
#define CARD_P_SX_5   23
#define CARD_P_SX_J   24
#define CARD_P_SY_E   25
#define CARD_P_SY_1   26
#define CARD_P_SY_B   27
#define CARD_P_SY_F   28
#define CARD_P_SY_A   29
#define CARD_P_SY_N   30
#define CARD_P_SY_5   31
#define CARD_P_SY_J   32
#define CARD_P_SQ_E   33
#define CARD_P_SQ_1   34
#define CARD_P_SQ_B   35
#define CARD_P_SQ_F   36
#define CARD_P_SQ_A   37
#define CARD_P_SQ_N   38
#define CARD_P_SQ_5   39
#define CARD_P_SQ_J   40
#define CARD_P_P_E   41
#define CARD_P_P_1   42
#define CARD_P_P_B   43
#define CARD_P_P_F   44
#define CARD_P_P_A   45
#define CARD_P_P_N   46
#define CARD_P_P_5   47
#define CARD_P_P_J   48
#define CARD_P_M_E   49
#define CARD_P_M_1   50
#define CARD_P_M_B   51
#define CARD_P_M_F   52
#define CARD_P_M_A   53
#define CARD_P_M_N   54
#define CARD_P_M_5   55
#define CARD_P_M_J   56
#define CARD_P_S_S   57
#define CARD_P_SX_S   58
#define CARD_P_SY_S   59
#define CARD_P_SQ_S   60
#define CARD_P_P_S   61
#define CARD_P_M_S   62
#define CARD_D_NEW_DSP_COMBIFILE 63
typedef struct CARD_FILES_DATA
{
 char *    Name;
 unsigned char  Type;
}
CARD_FILES_DATA;
typedef struct CARD_FILES
{
 unsigned char  Boot;
 unsigned char  Dsp  [CARD_DSP_CNT];
 unsigned char  DspTelindus;
 unsigned char  Prot [CARD_PROT_CNT];
}
CARD_FILES;
#if CARDTYPE_H_WANT_DATA
#if CARDTYPE_H_WANT_FILE_DATA
CARD_FILES_DATA CardFData [] =  {
// Filename   Filetype
 0,     CARD_FT_UNKNOWN,
 "didnload.bin",  CARD_FT_B,
 "diprload.bin",  CARD_FT_B,
 "didiva.bin",  CARD_FT_D,
 "didivapp.bin",  CARD_FT_D,
 "dihscx.bin",  CARD_FT_D,
 "div110.bin",  CARD_FT_D,
 "dimodem.bin",  CARD_FT_D,
 "difax.bin",  CARD_FT_D,
 "di_etsi.bin",  CARD_FT_S,
 "di_1tr6.bin",  CARD_FT_S,
 "di_belg.bin",  CARD_FT_S,
 "di_franc.bin",  CARD_FT_S,
 "di_atel.bin",  CARD_FT_S,
 "di_ni.bin",  CARD_FT_S,
 "di_5ess.bin",  CARD_FT_S,
 "di_japan.bin",  CARD_FT_S,
 "di_etsi.sx",  CARD_FT_S,
 "di_1tr6.sx",  CARD_FT_S,
 "di_belg.sx",  CARD_FT_S,
 "di_franc.sx",  CARD_FT_S,
 "di_atel.sx",  CARD_FT_S,
 "di_ni.sx",   CARD_FT_S,
 "di_5ess.sx",  CARD_FT_S,
 "di_japan.sx",  CARD_FT_S,
 "di_etsi.sy",  CARD_FT_S,
 "di_1tr6.sy",  CARD_FT_S,
 "di_belg.sy",  CARD_FT_S,
 "di_franc.sy",  CARD_FT_S,
 "di_atel.sy",  CARD_FT_S,
 "di_ni.sy",   CARD_FT_S,
 "di_5ess.sy",  CARD_FT_S,
 "di_japan.sy",  CARD_FT_S,
 "di_etsi.sq",  CARD_FT_S,
 "di_1tr6.sq",  CARD_FT_S,
 "di_belg.sq",  CARD_FT_S,
 "di_franc.sq",  CARD_FT_S,
 "di_atel.sq",  CARD_FT_S,
 "di_ni.sq",   CARD_FT_S,
 "di_5ess.sq",  CARD_FT_S,
 "di_japan.sq",  CARD_FT_S,
 "di_etsi.p",  CARD_FT_S,
 "di_1tr6.p",  CARD_FT_S,
 "di_belg.p",  CARD_FT_S,
 "di_franc.p",  CARD_FT_S,
 "di_atel.p",  CARD_FT_S,
 "di_ni.p",   CARD_FT_S,
 "di_5ess.p",  CARD_FT_S,
 "di_japan.p",  CARD_FT_S,
 "di_etsi.sm",  CARD_FT_M,
 "di_1tr6.sm",  CARD_FT_M,
 "di_belg.sm",  CARD_FT_M,
 "di_franc.sm",  CARD_FT_M,
 "di_atel.sm",  CARD_FT_M,
 "di_ni.sm",   CARD_FT_M,
 "di_5ess.sm",  CARD_FT_M,
 "di_japan.sm",  CARD_FT_M,
 "di_swed.bin",  CARD_FT_S,
 "di_swed.sx",  CARD_FT_S,
 "di_swed.sy",  CARD_FT_S,
 "di_swed.sq",  CARD_FT_S,
 "di_swed.p",  CARD_FT_S,
 "di_swed.sm",  CARD_FT_M,
    "didspdld.bin",     CARD_FT_NEW_DSP_COMBIFILE
};
CARD_FILES CardFiles [] =
{
 { /* CARD_UNKNOWN */
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 },
 { /* CARD_DIVA */
  CARD_FILE_NONE,
  CARD_D_K1, CARD_D_H, CARD_D_V, CARD_FILE_NONE, CARD_D_F,
  CARD_D_NEW_DSP_COMBIFILE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 },
 { /* CARD_PRO  */
  CARD_FILE_NONE,
  CARD_D_K2, CARD_D_H, CARD_D_V, CARD_D_M, CARD_D_F,
  CARD_D_NEW_DSP_COMBIFILE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 },
 { /* CARD_PICO */
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 },
 { /* CARD_S    */
  CARD_B_S,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_S_E, CARD_P_S_1, CARD_P_S_B, CARD_P_S_F,
  CARD_P_S_A, CARD_P_S_N, CARD_P_S_5, CARD_P_S_J,
  CARD_P_S_S
 },
 { /* CARD_SX   */
  CARD_B_S,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_SX_E, CARD_P_SX_1, CARD_P_SX_B, CARD_P_SX_F,
  CARD_P_SX_A, CARD_P_SX_N, CARD_P_SX_5, CARD_P_SX_J,
  CARD_P_SX_S
 },
 { /* CARD_SXN  */
  CARD_B_S,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_SY_E, CARD_P_SY_1, CARD_P_SY_B, CARD_P_SY_F,
  CARD_P_SY_A, CARD_P_SY_N, CARD_P_SY_5, CARD_P_SY_J,
  CARD_P_SY_S
 },
 { /* CARD_SCOM */
  CARD_B_S,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_SY_E, CARD_P_SY_1, CARD_P_SY_B, CARD_P_SY_F,
  CARD_P_SY_A, CARD_P_SY_N, CARD_P_SY_5, CARD_P_SY_J,
  CARD_P_SY_S
 },
 { /* CARD_QUAD */
  CARD_B_S,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_SQ_E, CARD_P_SQ_1, CARD_P_SQ_B, CARD_P_SQ_F,
  CARD_P_SQ_A, CARD_P_SQ_N, CARD_P_SQ_5, CARD_P_SQ_J,
  CARD_P_SQ_S
 },
 { /* CARD_PR   */
  CARD_B_P,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_P_P_E, CARD_P_P_1, CARD_P_P_B, CARD_P_P_F,
  CARD_P_P_A, CARD_P_P_N, CARD_P_P_5, CARD_P_P_J,
  CARD_P_P_S
 },
 { /* CARD_MAE  */
  CARD_FILE_NONE,
  CARD_D_K2, CARD_D_H, CARD_D_V, CARD_D_M, CARD_D_F,
  CARD_D_NEW_DSP_COMBIFILE,
  CARD_P_M_E, CARD_P_M_1, CARD_P_M_B, CARD_P_M_F,
  CARD_P_M_A, CARD_P_M_N, CARD_P_M_5, CARD_P_M_J,
  CARD_P_M_S
 },
 { /* CARD_MAEQ */  /* currently not supported */
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 },
 { /* CARD_MAEP */  /* currently not supported */
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE, CARD_FILE_NONE,
  CARD_FILE_NONE
 }
};
#endif /*CARDTYPE_H_WANT_FILE_DATA*/
#else /*!CARDTYPE_H_WANT_DATA*/
extern CARD_FILES_DATA  CardFData [] ;
extern CARD_FILES   CardFiles [] ;
#endif /*CARDTYPE_H_WANT_DATA*/
#endif /* _CARDTYPE_H_ */

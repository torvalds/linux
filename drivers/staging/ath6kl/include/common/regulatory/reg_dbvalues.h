//------------------------------------------------------------------------------
// Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================


#ifndef __REG_DBVALUE_H__
#define __REG_DBVALUE_H__

/*
 * Numbering from ISO 3166
 */
enum CountryCode {
    CTRY_ALBANIA              = 8,       /* Albania */
    CTRY_ALGERIA              = 12,      /* Algeria */
    CTRY_ARGENTINA            = 32,      /* Argentina */
    CTRY_ARMENIA              = 51,      /* Armenia */
    CTRY_ARUBA                = 533,     /* Aruba */
    CTRY_AUSTRALIA            = 36,      /* Australia (for STA) */
    CTRY_AUSTRALIA_AP         = 5000,    /* Australia (for AP) */
    CTRY_AUSTRIA              = 40,      /* Austria */
    CTRY_AZERBAIJAN           = 31,      /* Azerbaijan */
    CTRY_BAHRAIN              = 48,      /* Bahrain */
    CTRY_BANGLADESH           = 50,      /* Bangladesh */
    CTRY_BARBADOS             = 52,      /* Barbados */
    CTRY_BELARUS              = 112,     /* Belarus */
    CTRY_BELGIUM              = 56,      /* Belgium */
    CTRY_BELIZE               = 84,      /* Belize */
    CTRY_BOLIVIA              = 68,      /* Bolivia */
    CTRY_BOSNIA_HERZEGOWANIA  = 70,      /* Bosnia & Herzegowania */
    CTRY_BRAZIL               = 76,      /* Brazil */
    CTRY_BRUNEI_DARUSSALAM    = 96,      /* Brunei Darussalam */
    CTRY_BULGARIA             = 100,     /* Bulgaria */
    CTRY_CAMBODIA             = 116,     /* Cambodia */
    CTRY_CANADA               = 124,     /* Canada (for STA) */
    CTRY_CANADA_AP            = 5001,    /* Canada (for AP) */
    CTRY_CHILE                = 152,     /* Chile */
    CTRY_CHINA                = 156,     /* People's Republic of China */
    CTRY_COLOMBIA             = 170,     /* Colombia */
    CTRY_COSTA_RICA           = 188,     /* Costa Rica */
    CTRY_CROATIA              = 191,     /* Croatia */
    CTRY_CYPRUS               = 196,
    CTRY_CZECH                = 203,     /* Czech Republic */
    CTRY_DENMARK              = 208,     /* Denmark */
    CTRY_DOMINICAN_REPUBLIC   = 214,     /* Dominican Republic */
    CTRY_ECUADOR              = 218,     /* Ecuador */
    CTRY_EGYPT                = 818,     /* Egypt */
    CTRY_EL_SALVADOR          = 222,     /* El Salvador */
    CTRY_ESTONIA              = 233,     /* Estonia */
    CTRY_FAEROE_ISLANDS       = 234,     /* Faeroe Islands */
    CTRY_FINLAND              = 246,     /* Finland */
    CTRY_FRANCE               = 250,     /* France */
    CTRY_FRANCE2              = 255,     /* France2 */
    CTRY_GEORGIA              = 268,     /* Georgia */
    CTRY_GERMANY              = 276,     /* Germany */
    CTRY_GREECE               = 300,     /* Greece */
    CTRY_GREENLAND            = 304,     /* Greenland */
    CTRY_GRENADA              = 308,     /* Grenada */
    CTRY_GUAM                 = 316,     /* Guam */
    CTRY_GUATEMALA            = 320,     /* Guatemala */
    CTRY_HAITI                = 332,     /* Haiti */
    CTRY_HONDURAS             = 340,     /* Honduras */
    CTRY_HONG_KONG            = 344,     /* Hong Kong S.A.R., P.R.C. */
    CTRY_HUNGARY              = 348,     /* Hungary */
    CTRY_ICELAND              = 352,     /* Iceland */
    CTRY_INDIA                = 356,     /* India */
    CTRY_INDONESIA            = 360,     /* Indonesia */
    CTRY_IRAN                 = 364,     /* Iran */
    CTRY_IRAQ                 = 368,     /* Iraq */
    CTRY_IRELAND              = 372,     /* Ireland */
    CTRY_ISRAEL               = 376,     /* Israel */
    CTRY_ITALY                = 380,     /* Italy */
    CTRY_JAMAICA              = 388,     /* Jamaica */
    CTRY_JAPAN                = 392,     /* Japan */
    CTRY_JAPAN1               = 393,     /* Japan (JP1) */
    CTRY_JAPAN2               = 394,     /* Japan (JP0) */
    CTRY_JAPAN3               = 395,     /* Japan (JP1-1) */
    CTRY_JAPAN4               = 396,     /* Japan (JE1) */
    CTRY_JAPAN5               = 397,     /* Japan (JE2) */
    CTRY_JAPAN6               = 399,     /* Japan (JP6) */
    CTRY_JORDAN               = 400,     /* Jordan */
    CTRY_KAZAKHSTAN           = 398,     /* Kazakhstan */
    CTRY_KENYA                = 404,     /* Kenya */
    CTRY_KOREA_NORTH          = 408,     /* North Korea */
    CTRY_KOREA_ROC            = 410,     /* South Korea (for STA) */
    CTRY_KOREA_ROC2           = 411,     /* South Korea */
    CTRY_KOREA_ROC3           = 412,     /* South Korea (for AP) */
    CTRY_KUWAIT               = 414,     /* Kuwait */
    CTRY_LATVIA               = 428,     /* Latvia */
    CTRY_LEBANON              = 422,     /* Lebanon */
    CTRY_LIBYA                = 434,     /* Libya */
    CTRY_LIECHTENSTEIN        = 438,     /* Liechtenstein */
    CTRY_LITHUANIA            = 440,     /* Lithuania */
    CTRY_LUXEMBOURG           = 442,     /* Luxembourg */
    CTRY_MACAU                = 446,     /* Macau */
    CTRY_MACEDONIA            = 807,     /* the Former Yugoslav Republic of Macedonia */
    CTRY_MALAYSIA             = 458,     /* Malaysia */
    CTRY_MALTA                = 470,     /* Malta */
    CTRY_MEXICO               = 484,     /* Mexico */
    CTRY_MONACO               = 492,     /* Principality of Monaco */
    CTRY_MOROCCO              = 504,     /* Morocco */
    CTRY_NEPAL                = 524,     /* Nepal */   
    CTRY_NETHERLANDS          = 528,     /* Netherlands */
    CTRY_NETHERLAND_ANTILLES  = 530,     /* Netherlands-Antilles */
    CTRY_NEW_ZEALAND          = 554,     /* New Zealand */
    CTRY_NICARAGUA            = 558,     /* Nicaragua */
    CTRY_NORWAY               = 578,     /* Norway */
    CTRY_OMAN                 = 512,     /* Oman */
    CTRY_PAKISTAN             = 586,     /* Islamic Republic of Pakistan */
    CTRY_PANAMA               = 591,     /* Panama */
    CTRY_PARAGUAY             = 600,     /* Paraguay */
    CTRY_PERU                 = 604,     /* Peru */
    CTRY_PHILIPPINES          = 608,     /* Republic of the Philippines */
    CTRY_POLAND               = 616,     /* Poland */
    CTRY_PORTUGAL             = 620,     /* Portugal */
    CTRY_PUERTO_RICO          = 630,     /* Puerto Rico */
    CTRY_QATAR                = 634,     /* Qatar */
    CTRY_ROMANIA              = 642,     /* Romania */
    CTRY_RUSSIA               = 643,     /* Russia */
    CTRY_SAUDI_ARABIA         = 682,     /* Saudi Arabia */
    CTRY_MONTENEGRO           = 891,     /* Montenegro */
    CTRY_SINGAPORE            = 702,     /* Singapore */
    CTRY_SLOVAKIA             = 703,     /* Slovak Republic */
    CTRY_SLOVENIA             = 705,     /* Slovenia */
    CTRY_SOUTH_AFRICA         = 710,     /* South Africa */
    CTRY_SPAIN                = 724,     /* Spain */
    CTRY_SRILANKA             = 144,     /* Sri Lanka */
    CTRY_SWEDEN               = 752,     /* Sweden */
    CTRY_SWITZERLAND          = 756,     /* Switzerland */
    CTRY_SYRIA                = 760,     /* Syria */
    CTRY_TAIWAN               = 158,     /* Taiwan */
    CTRY_THAILAND             = 764,     /* Thailand */
    CTRY_TRINIDAD_Y_TOBAGO    = 780,     /* Trinidad y Tobago */
    CTRY_TUNISIA              = 788,     /* Tunisia */
    CTRY_TURKEY               = 792,     /* Turkey */
    CTRY_UAE                  = 784,     /* U.A.E. */
    CTRY_UKRAINE              = 804,     /* Ukraine */
    CTRY_UNITED_KINGDOM       = 826,     /* United Kingdom */
    CTRY_UNITED_STATES        = 840,     /* United States (for STA) */
    CTRY_UNITED_STATES_AP     = 841,     /* United States (for AP) */
    CTRY_UNITED_STATES_PS     = 842,     /* United States - public safety */
    CTRY_URUGUAY              = 858,     /* Uruguay */
    CTRY_UZBEKISTAN           = 860,     /* Uzbekistan */
    CTRY_VENEZUELA            = 862,     /* Venezuela */
    CTRY_VIET_NAM             = 704,     /* Viet Nam */
    CTRY_YEMEN                = 887,     /* Yemen */
    CTRY_ZIMBABWE             = 716      /* Zimbabwe */
};

#define CTRY_DEBUG      0
#define CTRY_DEFAULT    0x1ff

/*
 * The following regulatory domain definitions are
 * found in the EEPROM. Each regulatory domain
 * can operate in either a 5GHz or 2.4GHz wireless mode or
 * both 5GHz and 2.4GHz wireless modes.
 * In general, the value holds no special
 * meaning and is used to decode into either specific
 * 2.4GHz or 5GHz wireless mode for that particular
 * regulatory domain.
 *
 * Enumerated Regulatory Domain Information 8 bit values indicate that
 * the regdomain is really a pair of unitary regdomains.  12 bit values
 * are the real unitary regdomains and are the only ones which have the
 * frequency bitmasks and flags set.
 */

enum EnumRd {
    NO_ENUMRD   = 0x00,
    NULL1_WORLD = 0x03,     /* For 11b-only countries (no 11a allowed) */
    NULL1_ETSIB = 0x07,     /* Israel */
    NULL1_ETSIC = 0x08,

    FCC1_FCCA   = 0x10,     /* USA */
    FCC1_WORLD  = 0x11,     /* Hong Kong */
    FCC2_FCCA   = 0x20,     /* Canada */
    FCC2_WORLD  = 0x21,     /* Australia & HK */
    FCC2_ETSIC  = 0x22,
    FCC3_FCCA   = 0x3A,     /* USA & Canada w/5470 band, 11h, DFS enabled */
    FCC3_WORLD  = 0x3B,     /* USA & Canada w/5470 band, 11h, DFS enabled */
    FCC4_FCCA   = 0x12,     /* FCC public safety plus UNII bands */
    FCC5_FCCA   = 0x13,     /* US with no DFS */
    FCC5_WORLD  = 0x16,     /* US with no DFS */
    FCC6_FCCA   = 0x14,     /* Same as FCC2_FCCA but with 5600-5650MHz channels disabled for US & Canada APs */
    FCC6_WORLD  = 0x23,     /* Same as FCC2_FCCA but with 5600-5650MHz channels disabled for Australia APs */

    ETSI1_WORLD = 0x37,

    ETSI2_WORLD = 0x35,     /* Hungary & others */
    ETSI3_WORLD = 0x36,     /* France & others */
    ETSI4_WORLD = 0x30,
    ETSI4_ETSIC = 0x38,
    ETSI5_WORLD = 0x39,
    ETSI6_WORLD = 0x34,     /* Bulgaria */
    ETSI_RESERVED   = 0x33,     /* Reserved (Do not used) */
    FRANCE_RES  = 0x31,     /* Legacy France for OEM */

    APL6_WORLD  = 0x5B,     /* Singapore */
    APL4_WORLD  = 0x42,     /* Singapore */
    APL3_FCCA   = 0x50,
    APL_RESERVED    = 0x44,     /* Reserved (Do not used)  */
    APL2_WORLD  = 0x45,     /* Korea */
    APL2_APLC   = 0x46,
    APL3_WORLD  = 0x47,
    APL2_APLD   = 0x49,     /* Korea with 2.3G channels */
    APL2_FCCA   = 0x4D,     /* Specific Mobile Customer */
    APL1_WORLD  = 0x52,     /* Latin America */
    APL1_FCCA   = 0x53,
    APL1_ETSIC  = 0x55,
    APL2_ETSIC  = 0x56,     /* Venezuela */
    APL5_WORLD  = 0x58,     /* Chile */
    APL7_FCCA   = 0x5C,
    APL8_WORLD  = 0x5D,
    APL9_WORLD  = 0x5E,
    APL10_WORLD = 0x5F,     /* Korea 5GHz for STA */


    MKK5_MKKA   = 0x99, /* This is a temporary value. MG and DQ have to give official one */
    MKK5_FCCA   = 0x9A, /* This is a temporary value. MG and DQ have to give official one */
    MKK5_MKKC   = 0x88,
    MKK11_MKKA  = 0xD4,
    MKK11_FCCA  = 0xD5,
    MKK11_MKKC  = 0xD7,

    /*
     * World mode SKUs
     */
    WOR0_WORLD  = 0x60,     /* World0 (WO0 SKU) */
    WOR1_WORLD  = 0x61,     /* World1 (WO1 SKU) */
    WOR2_WORLD  = 0x62,     /* World2 (WO2 SKU) */
    WOR3_WORLD  = 0x63,     /* World3 (WO3 SKU) */
    WOR4_WORLD  = 0x64,     /* World4 (WO4 SKU) */  
    WOR5_ETSIC  = 0x65,     /* World5 (WO5 SKU) */    

    WOR01_WORLD = 0x66,     /* World0-1 (WW0-1 SKU) */
    WOR02_WORLD = 0x67,     /* World0-2 (WW0-2 SKU) */
    EU1_WORLD   = 0x68,     /* Same as World0-2 (WW0-2 SKU), except active scan ch1-13. No ch14 */

    WOR9_WORLD  = 0x69,     /* World9 (WO9 SKU) */  
    WORA_WORLD  = 0x6A,     /* WorldA (WOA SKU) */  
    WORB_WORLD  = 0x6B,     /* WorldB (WOA SKU) */  
    WORC_WORLD  = 0x6C,     /* WorldC (WOA SKU) */  

    /*
     * Regulator domains ending in a number (e.g. APL1,
     * MK1, ETSI4, etc) apply to 5GHz channel and power
     * information.  Regulator domains ending in a letter
     * (e.g. APLA, FCCA, etc) apply to 2.4GHz channel and
     * power information.
     */
    APL1        = 0x0150,   /* LAT & Asia */
    APL2        = 0x0250,   /* LAT & Asia */
    APL3        = 0x0350,   /* Taiwan */
    APL4        = 0x0450,   /* Jordan */
    APL5        = 0x0550,   /* Chile */
    APL6        = 0x0650,   /* Singapore */
    APL7        = 0x0750,   /* Taiwan */
    APL8        = 0x0850,   /* Malaysia */
    APL9        = 0x0950,   /* Korea */
    APL10       = 0x1050,   /* Korea 5GHz */

    ETSI1       = 0x0130,   /* Europe & others */
    ETSI2       = 0x0230,   /* Europe & others */
    ETSI3       = 0x0330,   /* Europe & others */
    ETSI4       = 0x0430,   /* Europe & others */
    ETSI5       = 0x0530,   /* Europe & others */
    ETSI6       = 0x0630,   /* Europe & others */
    ETSIB       = 0x0B30,   /* Israel */
    ETSIC       = 0x0C30,   /* Latin America */

    FCC1        = 0x0110,   /* US & others */
    FCC2        = 0x0120,   /* Canada, Australia & New Zealand */
    FCC3        = 0x0160,   /* US w/new middle band & DFS */    
    FCC4        = 0x0165,
    FCC5        = 0x0180,
    FCC6        = 0x0610,
    FCCA        = 0x0A10,    

    APLD        = 0x0D50,   /* South Korea */

    MKK1        = 0x0140,   /* Japan */
    MKK2        = 0x0240,   /* Japan Extended */
    MKK3        = 0x0340,   /* Japan new 5GHz */
    MKK4        = 0x0440,   /* Japan new 5GHz */
    MKK5        = 0x0540,   /* Japan new 5GHz */
    MKK6        = 0x0640,   /* Japan new 5GHz */
    MKK7        = 0x0740,   /* Japan new 5GHz */
    MKK8        = 0x0840,   /* Japan new 5GHz */
    MKK9        = 0x0940,   /* Japan new 5GHz */
    MKK10       = 0x1040,   /* Japan new 5GHz */
    MKK11       = 0x1140,   /* Japan new 5GHz */
    MKK12       = 0x1240,   /* Japan new 5GHz */

    MKKA        = 0x0A40,   /* Japan */
    MKKC        = 0x0A50,

    NULL1       = 0x0198,
    WORLD       = 0x0199,
    DEBUG_REG_DMN   = 0x01ff,
    UNINIT_REG_DMN  = 0x0fff,
};

enum {                  /* conformance test limits */
    FCC = 0x10,
    MKK = 0x40,
    ETSI    = 0x30,
    NO_CTL  = 0xff,
    CTL_11B = 1,
    CTL_11G = 2
};


/*
 * The following are flags for different requirements per reg domain.
 * These requirements are either inhereted from the reg domain pair or
 * from the unitary reg domain if the reg domain pair flags value is
 * 0
 */

enum {
    NO_REQ              = 0x00,
    DISALLOW_ADHOC_11A  = 0x01,
    ADHOC_PER_11D       = 0x02,
    ADHOC_NO_11A        = 0x04,
    DISALLOW_ADHOC_11G  = 0x08
};




/*
 * The following describe the bit masks for different passive scan
 * capability/requirements per regdomain.
 */
#define NO_PSCAN        0x00000000
#define PSCAN_FCC       0x00000001
#define PSCAN_ETSI      0x00000002
#define PSCAN_MKK       0x00000004
#define PSCAN_ETSIB     0x00000008
#define PSCAN_ETSIC     0x00000010
#define PSCAN_WWR       0x00000020
#define PSCAN_DEFER     0xFFFFFFFF

/* Bit masks for DFS per regdomain */

enum {
    NO_DFS   = 0x00,
    DFS_FCC3 = 0x01,
    DFS_ETSI = 0x02,
    DFS_MKK  = 0x04
};


#define DEF_REGDMN      FCC1_FCCA

/* 
 * The following table is the master list for all different freqeuncy
 * bands with the complete matrix of all possible flags and settings
 * for each band if it is used in ANY reg domain.
 *
 * The table of frequency bands is indexed by a bitmask.  The ordering
 * must be consistent with the enum below.  When adding a new
 * frequency band, be sure to match the location in the enum with the
 * comments 
 */

/*
 * These frequency values are as per channel tags and regulatory domain
 * info. Please update them as database is updated.
 */
#define A_FREQ_MIN              4920
#define A_FREQ_MAX              5825

#define A_CHAN0_FREQ            5000
#define A_CHAN_MAX              ((A_FREQ_MAX - A_CHAN0_FREQ)/5)

#define BG_FREQ_MIN             2412
#define BG_FREQ_MAX             2484

#define BG_CHAN0_FREQ           2407
#define BG_CHAN_MIN             ((BG_FREQ_MIN - BG_CHAN0_FREQ)/5)
#define BG_CHAN_MAX             14      /* corresponding to 2484 MHz */

#define A_20MHZ_BAND_FREQ_MAX   5000


/*
 * 5GHz 11A channel tags
 */

enum {
    F1_4920_4980,
    F1_5040_5080,

    F1_5120_5240,

    F1_5180_5240,
    F2_5180_5240,
    F3_5180_5240,
    F4_5180_5240,
    F5_5180_5240,
    F6_5180_5240,
    F7_5180_5240,

    F1_5260_5280,

    F1_5260_5320,
    F2_5260_5320,
    F3_5260_5320,
    F4_5260_5320,
    F5_5260_5320,
    F6_5260_5320,

    F1_5260_5700,

    F1_5280_5320,

    F1_5500_5620,

    F1_5500_5700,
    F2_5500_5700,
    F3_5500_5700,
    F4_5500_5700,
    F5_5500_5700,
    F6_5500_5700,
    F7_5500_5700,

    F1_5745_5805,
    F2_5745_5805,

    F1_5745_5825,
    F2_5745_5825,
    F3_5745_5825,
    F4_5745_5825,
    F5_5745_5825,
    F6_5745_5825,

    W1_4920_4980,
    W1_5040_5080,
    W1_5170_5230,
    W1_5180_5240,
    W1_5260_5320,
    W1_5745_5825,
    W1_5500_5700,
};


/* 2.4 GHz table - for 11b and 11g info */
enum {
    BG1_2312_2372,
    BG2_2312_2372,

    BG1_2412_2472,
    BG2_2412_2472,
    BG3_2412_2472,
    BG4_2412_2472,

    BG1_2412_2462,
    BG2_2412_2462,

    BG1_2432_2442,

    BG1_2457_2472,

    BG1_2467_2472,

    BG1_2484_2484, /* No G */
    BG2_2484_2484, /* No G */

    BG1_2512_2732,

    WBG1_2312_2372,
    WBG1_2412_2412,
    WBG1_2417_2432,
    WBG1_2437_2442,
    WBG1_2447_2457,
    WBG1_2462_2462,
    WBG1_2467_2467,
    WBG2_2467_2467,
    WBG1_2472_2472,
    WBG2_2472_2472,
    WBG1_2484_2484, /* No G */
    WBG2_2484_2484, /* No G */
};
    
#endif /* __REG_DBVALUE_H__ */

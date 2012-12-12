/*
 * zc030x registers
 *
 * Copyright (c) 2008 Mauro Carvalho Chehab <mchehab@infradead.org>
 *
 * The register aliases used here came from this driver:
 *	http://zc0302.sourceforge.net/zc0302.php
 *
 * This code is placed under the terms of the GNU General Public License v2
 */

/* Define the register map */
#define ZC3XX_R000_SYSTEMCONTROL       0x0000
#define ZC3XX_R001_SYSTEMOPERATING     0x0001

/* Picture size */
#define ZC3XX_R002_CLOCKSELECT         0x0002
#define ZC3XX_R003_FRAMEWIDTHHIGH      0x0003
#define ZC3XX_R004_FRAMEWIDTHLOW       0x0004
#define ZC3XX_R005_FRAMEHEIGHTHIGH     0x0005
#define ZC3XX_R006_FRAMEHEIGHTLOW      0x0006

/* JPEG control */
#define ZC3XX_R008_CLOCKSETTING        0x0008

/* Test mode */
#define ZC3XX_R00B_TESTMODECONTROL     0x000b

/* Frame retreiving */
#define ZC3XX_R00C_LASTACQTIME         0x000c
#define ZC3XX_R00D_MONITORRES          0x000d
#define ZC3XX_R00E_TIMESTAMPHIGH       0x000e
#define ZC3XX_R00F_TIMESTAMPLOW        0x000f
#define ZC3XX_R018_FRAMELOST           0x0018
#define ZC3XX_R019_AUTOADJUSTFPS       0x0019
#define ZC3XX_R01A_LASTFRAMESTATE      0x001a
#define ZC3XX_R025_DATACOUNTER         0x0025

/* Stream and sensor specific */
#define ZC3XX_R010_CMOSSENSORSELECT    0x0010
#define ZC3XX_R011_VIDEOSTATUS         0x0011
#define ZC3XX_R012_VIDEOCONTROLFUNC    0x0012

/* Horizontal and vertical synchros */
#define ZC3XX_R01D_HSYNC_0             0x001d
#define ZC3XX_R01E_HSYNC_1             0x001e
#define ZC3XX_R01F_HSYNC_2             0x001f
#define ZC3XX_R020_HSYNC_3             0x0020

/* Target picture size in byte */
#define ZC3XX_R022_TARGETPICTSIZE_0    0x0022
#define ZC3XX_R023_TARGETPICTSIZE_1    0x0023
#define ZC3XX_R024_TARGETPICTSIZE_2    0x0024

/* Audio registers */
#define ZC3XX_R030_AUDIOADC            0x0030
#define ZC3XX_R031_AUDIOSTREAMSTATUS   0x0031
#define ZC3XX_R032_AUDIOSTATUS         0x0032

/* Sensor interface */
#define ZC3XX_R080_HBLANKHIGH          0x0080
#define ZC3XX_R081_HBLANKLOW           0x0081
#define ZC3XX_R082_RESETLEVELADDR      0x0082
#define ZC3XX_R083_RGAINADDR           0x0083
#define ZC3XX_R084_GGAINADDR           0x0084
#define ZC3XX_R085_BGAINADDR           0x0085
#define ZC3XX_R086_EXPTIMEHIGH         0x0086
#define ZC3XX_R087_EXPTIMEMID          0x0087
#define ZC3XX_R088_EXPTIMELOW          0x0088
#define ZC3XX_R089_RESETBLACKHIGH      0x0089
#define ZC3XX_R08A_RESETWHITEHIGH      0x008a
#define ZC3XX_R08B_I2CDEVICEADDR       0x008b
#define ZC3XX_R08C_I2CIDLEANDNACK      0x008c
#define ZC3XX_R08D_COMPABILITYMODE     0x008d
#define ZC3XX_R08E_COMPABILITYMODE2    0x008e

/* I2C control */
#define ZC3XX_R090_I2CCOMMAND          0x0090
#define ZC3XX_R091_I2CSTATUS           0x0091
#define ZC3XX_R092_I2CADDRESSSELECT    0x0092
#define ZC3XX_R093_I2CSETVALUE         0x0093
#define ZC3XX_R094_I2CWRITEACK         0x0094
#define ZC3XX_R095_I2CREAD             0x0095
#define ZC3XX_R096_I2CREADACK          0x0096

/* Window inside the sensor array */
#define ZC3XX_R097_WINYSTARTHIGH       0x0097
#define ZC3XX_R098_WINYSTARTLOW        0x0098
#define ZC3XX_R099_WINXSTARTHIGH       0x0099
#define ZC3XX_R09A_WINXSTARTLOW        0x009a
#define ZC3XX_R09B_WINHEIGHTHIGH       0x009b
#define ZC3XX_R09C_WINHEIGHTLOW        0x009c
#define ZC3XX_R09D_WINWIDTHHIGH        0x009d
#define ZC3XX_R09E_WINWIDTHLOW         0x009e
#define ZC3XX_R119_FIRSTYHIGH          0x0119
#define ZC3XX_R11A_FIRSTYLOW           0x011a
#define ZC3XX_R11B_FIRSTXHIGH          0x011b
#define ZC3XX_R11C_FIRSTXLOW           0x011c

/* Max sensor array size */
#define ZC3XX_R09F_MAXXHIGH            0x009f
#define ZC3XX_R0A0_MAXXLOW             0x00a0
#define ZC3XX_R0A1_MAXYHIGH            0x00a1
#define ZC3XX_R0A2_MAXYLOW             0x00a2
#define ZC3XX_R0A3_EXPOSURETIMEHIGH    0x00a3
#define ZC3XX_R0A4_EXPOSURETIMELOW     0x00a4
#define ZC3XX_R0A5_EXPOSUREGAIN        0x00a5
#define ZC3XX_R0A6_EXPOSUREBLACKLVL    0x00a6

/* Other registers */
#define ZC3XX_R100_OPERATIONMODE       0x0100
#define ZC3XX_R101_SENSORCORRECTION    0x0101

/* Gains */
#define ZC3XX_R116_RGAIN               0x0116
#define ZC3XX_R117_GGAIN               0x0117
#define ZC3XX_R118_BGAIN               0x0118
#define ZC3XX_R11D_GLOBALGAIN          0x011d
#define ZC3XX_R1A8_DIGITALGAIN         0x01a8
#define ZC3XX_R1A9_DIGITALLIMITDIFF    0x01a9
#define ZC3XX_R1AA_DIGITALGAINSTEP     0x01aa

/* Auto correction */
#define ZC3XX_R180_AUTOCORRECTENABLE   0x0180
#define ZC3XX_R181_WINXSTART           0x0181
#define ZC3XX_R182_WINXWIDTH           0x0182
#define ZC3XX_R183_WINXCENTER          0x0183
#define ZC3XX_R184_WINYSTART           0x0184
#define ZC3XX_R185_WINYWIDTH           0x0185
#define ZC3XX_R186_WINYCENTER          0x0186

/* Gain range */
#define ZC3XX_R187_MAXGAIN             0x0187
#define ZC3XX_R188_MINGAIN             0x0188

/* Auto exposure and white balance */
#define ZC3XX_R189_AWBSTATUS           0x0189
#define ZC3XX_R18A_AWBFREEZE           0x018a
#define ZC3XX_R18B_AESTATUS            0x018b
#define ZC3XX_R18C_AEFREEZE            0x018c
#define ZC3XX_R18F_AEUNFREEZE          0x018f
#define ZC3XX_R190_EXPOSURELIMITHIGH   0x0190
#define ZC3XX_R191_EXPOSURELIMITMID    0x0191
#define ZC3XX_R192_EXPOSURELIMITLOW    0x0192
#define ZC3XX_R195_ANTIFLICKERHIGH     0x0195
#define ZC3XX_R196_ANTIFLICKERMID      0x0196
#define ZC3XX_R197_ANTIFLICKERLOW      0x0197

/* What is this ? */
#define ZC3XX_R18D_YTARGET             0x018d
#define ZC3XX_R18E_RESETLVL            0x018e

/* Color */
#define ZC3XX_R1A0_REDMEANAFTERAGC     0x01a0
#define ZC3XX_R1A1_GREENMEANAFTERAGC   0x01a1
#define ZC3XX_R1A2_BLUEMEANAFTERAGC    0x01a2
#define ZC3XX_R1A3_REDMEANAFTERAWB     0x01a3
#define ZC3XX_R1A4_GREENMEANAFTERAWB   0x01a4
#define ZC3XX_R1A5_BLUEMEANAFTERAWB    0x01a5
#define ZC3XX_R1A6_YMEANAFTERAE        0x01a6
#define ZC3XX_R1A7_CALCGLOBALMEAN      0x01a7

/* Matrixes */

/* Color matrix is like :
   R' = R * RGB00 + G * RGB01 + B * RGB02 + RGB03
   G' = R * RGB10 + G * RGB11 + B * RGB22 + RGB13
   B' = R * RGB20 + G * RGB21 + B * RGB12 + RGB23
 */
#define ZC3XX_R10A_RGB00               0x010a
#define ZC3XX_R10B_RGB01               0x010b
#define ZC3XX_R10C_RGB02               0x010c
#define ZC3XX_R113_RGB03               0x0113
#define ZC3XX_R10D_RGB10               0x010d
#define ZC3XX_R10E_RGB11               0x010e
#define ZC3XX_R10F_RGB12               0x010f
#define ZC3XX_R114_RGB13               0x0114
#define ZC3XX_R110_RGB20               0x0110
#define ZC3XX_R111_RGB21               0x0111
#define ZC3XX_R112_RGB22               0x0112
#define ZC3XX_R115_RGB23               0x0115

/* Gamma matrix */
#define ZC3XX_R120_GAMMA00             0x0120
#define ZC3XX_R121_GAMMA01             0x0121
#define ZC3XX_R122_GAMMA02             0x0122
#define ZC3XX_R123_GAMMA03             0x0123
#define ZC3XX_R124_GAMMA04             0x0124
#define ZC3XX_R125_GAMMA05             0x0125
#define ZC3XX_R126_GAMMA06             0x0126
#define ZC3XX_R127_GAMMA07             0x0127
#define ZC3XX_R128_GAMMA08             0x0128
#define ZC3XX_R129_GAMMA09             0x0129
#define ZC3XX_R12A_GAMMA0A             0x012a
#define ZC3XX_R12B_GAMMA0B             0x012b
#define ZC3XX_R12C_GAMMA0C             0x012c
#define ZC3XX_R12D_GAMMA0D             0x012d
#define ZC3XX_R12E_GAMMA0E             0x012e
#define ZC3XX_R12F_GAMMA0F             0x012f
#define ZC3XX_R130_GAMMA10             0x0130
#define ZC3XX_R131_GAMMA11             0x0131
#define ZC3XX_R132_GAMMA12             0x0132
#define ZC3XX_R133_GAMMA13             0x0133
#define ZC3XX_R134_GAMMA14             0x0134
#define ZC3XX_R135_GAMMA15             0x0135
#define ZC3XX_R136_GAMMA16             0x0136
#define ZC3XX_R137_GAMMA17             0x0137
#define ZC3XX_R138_GAMMA18             0x0138
#define ZC3XX_R139_GAMMA19             0x0139
#define ZC3XX_R13A_GAMMA1A             0x013a
#define ZC3XX_R13B_GAMMA1B             0x013b
#define ZC3XX_R13C_GAMMA1C             0x013c
#define ZC3XX_R13D_GAMMA1D             0x013d
#define ZC3XX_R13E_GAMMA1E             0x013e
#define ZC3XX_R13F_GAMMA1F             0x013f

/* Luminance gamma */
#define ZC3XX_R140_YGAMMA00            0x0140
#define ZC3XX_R141_YGAMMA01            0x0141
#define ZC3XX_R142_YGAMMA02            0x0142
#define ZC3XX_R143_YGAMMA03            0x0143
#define ZC3XX_R144_YGAMMA04            0x0144
#define ZC3XX_R145_YGAMMA05            0x0145
#define ZC3XX_R146_YGAMMA06            0x0146
#define ZC3XX_R147_YGAMMA07            0x0147
#define ZC3XX_R148_YGAMMA08            0x0148
#define ZC3XX_R149_YGAMMA09            0x0149
#define ZC3XX_R14A_YGAMMA0A            0x014a
#define ZC3XX_R14B_YGAMMA0B            0x014b
#define ZC3XX_R14C_YGAMMA0C            0x014c
#define ZC3XX_R14D_YGAMMA0D            0x014d
#define ZC3XX_R14E_YGAMMA0E            0x014e
#define ZC3XX_R14F_YGAMMA0F            0x014f
#define ZC3XX_R150_YGAMMA10            0x0150
#define ZC3XX_R151_YGAMMA11            0x0151

#define ZC3XX_R1C5_SHARPNESSMODE       0x01c5
#define ZC3XX_R1C6_SHARPNESS00         0x01c6
#define ZC3XX_R1C7_SHARPNESS01         0x01c7
#define ZC3XX_R1C8_SHARPNESS02         0x01c8
#define ZC3XX_R1C9_SHARPNESS03         0x01c9
#define ZC3XX_R1CA_SHARPNESS04         0x01ca
#define ZC3XX_R1CB_SHARPNESS05         0x01cb

/* Dead pixels */
#define ZC3XX_R250_DEADPIXELSMODE      0x0250

/* EEPROM */
#define ZC3XX_R300_EEPROMCONFIG        0x0300
#define ZC3XX_R301_EEPROMACCESS        0x0301
#define ZC3XX_R302_EEPROMSTATUS        0x0302

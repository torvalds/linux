/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef __ATHENA_SRAM_H__
#define __ATHENA_SRAM_H__

/* #define RX_SRAM_START_SIZE        = 0;  //  Start of reserved SRAM */
#define VID_CMDS_SIZE             80	/* Video CMDS size in bytes */
#define AUDIO_CMDS_SIZE           80	/* AUDIO CMDS size in bytes */
#define MBIF_CMDS_SIZE            80	/* MBIF  CMDS size in bytes */

/* #define RX_SRAM_POOL_START_SIZE   = 0;  //  Start of useable RX SRAM for buffers */
#define VID_IQ_SIZE               64	/* VID instruction queue size in bytes */
#define MBIF_IQ_SIZE              64
#define AUDIO_IQ_SIZE             64	/* AUD instruction queue size in bytes */

#define VID_CDT_SIZE              64	/* VID cluster descriptor table size in bytes */
#define MBIF_CDT_SIZE             64	/* MBIF/HBI cluster descriptor table size in bytes */
#define AUDIO_CDT_SIZE            48	/* AUD cluster descriptor table size in bytes */

/* #define RX_SRAM_POOL_FREE_SIZE    = 16; //  Start of available RX SRAM */
/* #define RX_SRAM_END_SIZE          = 0;  //  End of RX SRAM */

/* #define TX_SRAM_POOL_START_SIZE   = 0;  //  Start of transmit pool SRAM */
/* #define MSI_DATA_SIZE             = 64; //  Reserved (MSI Data, RISC working stora */

#define VID_CLUSTER_SIZE          1440	/* VID cluster data line */
#define AUDIO_CLUSTER_SIZE        128	/* AUDIO cluster data line */
#define MBIF_CLUSTER_SIZE         1440	/* MBIF/HBI cluster data line */

/* #define TX_SRAM_POOL_FREE_SIZE    = 704;    //  Start of available TX SRAM */
/* #define TX_SRAM_END_SIZE          = 0;      //  End of TX SRAM */

/* Receive SRAM */
#define RX_SRAM_START             0x10000
#define VID_A_DOWN_CMDS           0x10000
#define VID_B_DOWN_CMDS           0x10050
#define VID_C_DOWN_CMDS           0x100A0
#define VID_D_DOWN_CMDS           0x100F0
#define VID_E_DOWN_CMDS           0x10140
#define VID_F_DOWN_CMDS           0x10190
#define VID_G_DOWN_CMDS           0x101E0
#define VID_H_DOWN_CMDS           0x10230
#define VID_A_UP_CMDS             0x10280
#define VID_B_UP_CMDS             0x102D0
#define VID_C_UP_CMDS             0x10320
#define VID_D_UP_CMDS             0x10370
#define VID_E_UP_CMDS             0x103C0
#define VID_F_UP_CMDS             0x10410
#define VID_I_UP_CMDS             0x10460
#define VID_J_UP_CMDS             0x104B0
#define AUD_A_DOWN_CMDS           0x10500
#define AUD_B_DOWN_CMDS           0x10550
#define AUD_C_DOWN_CMDS           0x105A0
#define AUD_D_DOWN_CMDS           0x105F0
#define AUD_A_UP_CMDS             0x10640
#define AUD_B_UP_CMDS             0x10690
#define AUD_C_UP_CMDS             0x106E0
#define AUD_E_UP_CMDS             0x10730
#define MBIF_A_DOWN_CMDS          0x10780
#define MBIF_B_DOWN_CMDS          0x107D0
#define DMA_SCRATCH_PAD           0x10820	/* Scratch pad area from 0x10820 to 0x10B40 */

/* #define RX_SRAM_POOL_START        = 0x105B0; */

#define VID_A_IQ                  0x11000
#define VID_B_IQ                  0x11040
#define VID_C_IQ                  0x11080
#define VID_D_IQ                  0x110C0
#define VID_E_IQ                  0x11100
#define VID_F_IQ                  0x11140
#define VID_G_IQ                  0x11180
#define VID_H_IQ                  0x111C0
#define VID_I_IQ                  0x11200
#define VID_J_IQ                  0x11240
#define AUD_A_IQ                  0x11280
#define AUD_B_IQ                  0x112C0
#define AUD_C_IQ                  0x11300
#define AUD_D_IQ                  0x11340
#define AUD_E_IQ                  0x11380
#define MBIF_A_IQ                 0x11000
#define MBIF_B_IQ                 0x110C0

#define VID_A_CDT                 0x10C00
#define VID_B_CDT                 0x10C40
#define VID_C_CDT                 0x10C80
#define VID_D_CDT                 0x10CC0
#define VID_E_CDT                 0x10D00
#define VID_F_CDT                 0x10D40
#define VID_G_CDT                 0x10D80
#define VID_H_CDT                 0x10DC0
#define VID_I_CDT                 0x10E00
#define VID_J_CDT                 0x10E40
#define AUD_A_CDT                 0x10E80
#define AUD_B_CDT                 0x10EB0
#define AUD_C_CDT                 0x10EE0
#define AUD_D_CDT                 0x10F10
#define AUD_E_CDT                 0x10F40
#define MBIF_A_CDT                0x10C00
#define MBIF_B_CDT                0x10CC0

/* Cluster Buffer for RX */
#define VID_A_UP_CLUSTER_1        0x11400
#define VID_A_UP_CLUSTER_2        0x119A0
#define VID_A_UP_CLUSTER_3        0x11F40
#define VID_A_UP_CLUSTER_4        0x124E0

#define VID_B_UP_CLUSTER_1        0x12A80
#define VID_B_UP_CLUSTER_2        0x13020
#define VID_B_UP_CLUSTER_3        0x135C0
#define VID_B_UP_CLUSTER_4        0x13B60

#define VID_C_UP_CLUSTER_1        0x14100
#define VID_C_UP_CLUSTER_2        0x146A0
#define VID_C_UP_CLUSTER_3        0x14C40
#define VID_C_UP_CLUSTER_4        0x151E0

#define VID_D_UP_CLUSTER_1        0x15780
#define VID_D_UP_CLUSTER_2        0x15D20
#define VID_D_UP_CLUSTER_3        0x162C0
#define VID_D_UP_CLUSTER_4        0x16860

#define VID_E_UP_CLUSTER_1        0x16E00
#define VID_E_UP_CLUSTER_2        0x173A0
#define VID_E_UP_CLUSTER_3        0x17940
#define VID_E_UP_CLUSTER_4        0x17EE0

#define VID_F_UP_CLUSTER_1        0x18480
#define VID_F_UP_CLUSTER_2        0x18A20
#define VID_F_UP_CLUSTER_3        0x18FC0
#define VID_F_UP_CLUSTER_4        0x19560

#define VID_I_UP_CLUSTER_1        0x19B00
#define VID_I_UP_CLUSTER_2        0x1A0A0
#define VID_I_UP_CLUSTER_3        0x1A640
#define VID_I_UP_CLUSTER_4        0x1ABE0

#define VID_J_UP_CLUSTER_1        0x1B180
#define VID_J_UP_CLUSTER_2        0x1B720
#define VID_J_UP_CLUSTER_3        0x1BCC0
#define VID_J_UP_CLUSTER_4        0x1C260

#define AUD_A_UP_CLUSTER_1        0x1C800
#define AUD_A_UP_CLUSTER_2        0x1C880
#define AUD_A_UP_CLUSTER_3        0x1C900

#define AUD_B_UP_CLUSTER_1        0x1C980
#define AUD_B_UP_CLUSTER_2        0x1CA00
#define AUD_B_UP_CLUSTER_3        0x1CA80

#define AUD_C_UP_CLUSTER_1        0x1CB00
#define AUD_C_UP_CLUSTER_2        0x1CB80
#define AUD_C_UP_CLUSTER_3        0x1CC00

#define AUD_E_UP_CLUSTER_1        0x1CC80
#define AUD_E_UP_CLUSTER_2        0x1CD00
#define AUD_E_UP_CLUSTER_3        0x1CD80

#define RX_SRAM_POOL_FREE         0x1CE00
#define RX_SRAM_END               0x1D000

/* Free Receive SRAM    144 Bytes */

/* Transmit SRAM */
#define TX_SRAM_POOL_START        0x00000

#define VID_A_DOWN_CLUSTER_1      0x00040
#define VID_A_DOWN_CLUSTER_2      0x005E0
#define VID_A_DOWN_CLUSTER_3      0x00B80
#define VID_A_DOWN_CLUSTER_4      0x01120

#define VID_B_DOWN_CLUSTER_1      0x016C0
#define VID_B_DOWN_CLUSTER_2      0x01C60
#define VID_B_DOWN_CLUSTER_3      0x02200
#define VID_B_DOWN_CLUSTER_4      0x027A0

#define VID_C_DOWN_CLUSTER_1      0x02D40
#define VID_C_DOWN_CLUSTER_2      0x032E0
#define VID_C_DOWN_CLUSTER_3      0x03880
#define VID_C_DOWN_CLUSTER_4      0x03E20

#define VID_D_DOWN_CLUSTER_1      0x043C0
#define VID_D_DOWN_CLUSTER_2      0x04960
#define VID_D_DOWN_CLUSTER_3      0x04F00
#define VID_D_DOWN_CLUSTER_4      0x054A0

#define VID_E_DOWN_CLUSTER_1      0x05a40
#define VID_E_DOWN_CLUSTER_2      0x05FE0
#define VID_E_DOWN_CLUSTER_3      0x06580
#define VID_E_DOWN_CLUSTER_4      0x06B20

#define VID_F_DOWN_CLUSTER_1      0x070C0
#define VID_F_DOWN_CLUSTER_2      0x07660
#define VID_F_DOWN_CLUSTER_3      0x07C00
#define VID_F_DOWN_CLUSTER_4      0x081A0

#define VID_G_DOWN_CLUSTER_1      0x08740
#define VID_G_DOWN_CLUSTER_2      0x08CE0
#define VID_G_DOWN_CLUSTER_3      0x09280
#define VID_G_DOWN_CLUSTER_4      0x09820

#define VID_H_DOWN_CLUSTER_1      0x09DC0
#define VID_H_DOWN_CLUSTER_2      0x0A360
#define VID_H_DOWN_CLUSTER_3      0x0A900
#define VID_H_DOWN_CLUSTER_4      0x0AEA0

#define AUD_A_DOWN_CLUSTER_1      0x0B500
#define AUD_A_DOWN_CLUSTER_2      0x0B580
#define AUD_A_DOWN_CLUSTER_3      0x0B600

#define AUD_B_DOWN_CLUSTER_1      0x0B680
#define AUD_B_DOWN_CLUSTER_2      0x0B700
#define AUD_B_DOWN_CLUSTER_3      0x0B780

#define AUD_C_DOWN_CLUSTER_1      0x0B800
#define AUD_C_DOWN_CLUSTER_2      0x0B880
#define AUD_C_DOWN_CLUSTER_3      0x0B900

#define AUD_D_DOWN_CLUSTER_1      0x0B980
#define AUD_D_DOWN_CLUSTER_2      0x0BA00
#define AUD_D_DOWN_CLUSTER_3      0x0BA80

#define TX_SRAM_POOL_FREE         0x0BB00
#define TX_SRAM_END               0x0C000

#define BYTES_TO_DWORDS(bcount) ((bcount) >> 2)
#define BYTES_TO_QWORDS(bcount) ((bcount) >> 3)
#define BYTES_TO_OWORDS(bcount) ((bcount) >> 4)

#define VID_IQ_SIZE_DW             BYTES_TO_DWORDS(VID_IQ_SIZE)
#define VID_CDT_SIZE_QW            BYTES_TO_QWORDS(VID_CDT_SIZE)
#define VID_CLUSTER_SIZE_OW        BYTES_TO_OWORDS(VID_CLUSTER_SIZE)

#define AUDIO_IQ_SIZE_DW           BYTES_TO_DWORDS(AUDIO_IQ_SIZE)
#define AUDIO_CDT_SIZE_QW          BYTES_TO_QWORDS(AUDIO_CDT_SIZE)
#define AUDIO_CLUSTER_SIZE_QW      BYTES_TO_QWORDS(AUDIO_CLUSTER_SIZE)

#define MBIF_IQ_SIZE_DW            BYTES_TO_DWORDS(MBIF_IQ_SIZE)
#define MBIF_CDT_SIZE_QW           BYTES_TO_QWORDS(MBIF_CDT_SIZE)
#define MBIF_CLUSTER_SIZE_OW       BYTES_TO_OWORDS(MBIF_CLUSTER_SIZE)

#endif

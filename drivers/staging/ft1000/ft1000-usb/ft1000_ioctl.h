/*
*---------------------------------------------------------------------------
* FT1000 driver for Flarion Flash OFDM NIC Device
*
* Copyright (C) 2002 Flarion Technologies, All rights reserved.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option) any
* later version. This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details. You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place -
* Suite 330, Boston, MA 02111-1307, USA.
*---------------------------------------------------------------------------
*
* File:         ft1000_ioctl.h
*
* Description:    Common structures and defines relating to IOCTL
*
* History:
* 11/5/02    Whc                Created.
*
*---------------------------------------------------------------------------//---------------------------------------------------------------------------
*/
#ifndef _FT1000IOCTLH_
#define _FT1000IOCTLH_

typedef struct _IOCTL_GET_VER
{
    unsigned long drv_ver;
} __attribute__ ((packed)) IOCTL_GET_VER, *PIOCTL_GET_VER;

/* Data structure for Dsp statistics */
typedef struct _IOCTL_GET_DSP_STAT
{
    unsigned char DspVer[DSPVERSZ];        /* DSP version number */
    unsigned char HwSerNum[HWSERNUMSZ];    /* Hardware Serial Number */
    unsigned char Sku[SKUSZ];              /* SKU */
    unsigned char eui64[EUISZ];            /* EUI64 */
    unsigned short ConStat;                /* Connection Status */
                                /*    Bits 0-3 = Connection Status Field */
                                /*               0000=Idle (Disconnect) */
                                /*               0001=Searching */
                                /*               0010=Active (Connected) */
                                /*               0011=Waiting for L2 down */
                                /*               0100=Sleep */
    unsigned short LedStat;                /* Led Status */
                                /*    Bits 0-3   = Signal Strength Field */
                                /*                 0000 = -105dBm to -92dBm */
                                /*                 0001 = -92dBm to -85dBm */
                                /*                 0011 = -85dBm to -75dBm */
                                /*                 0111 = -75dBm to -50dBm */
                                /*                 1111 = -50dBm to 0dBm */
                                /*    Bits 4-7   = Reserved */
                                /*    Bits 8-11  = SNR Field */
                                /*                 0000 = <2dB */
                                /*                 0001 = 2dB to 8dB */
                                /*                 0011 = 8dB to 15dB */
                                /*                 0111 = 15dB to 22dB */
                                /*                 1111 = >22dB */
                                /*    Bits 12-15 = Reserved */
    unsigned long nTxPkts;                /* Number of packets transmitted from host to dsp */
    unsigned long nRxPkts;                /* Number of packets received from dsp to host */
    unsigned long nTxBytes;               /* Number of bytes transmitted from host to dsp */
    unsigned long nRxBytes;               /* Number of bytes received from dsp to host */
    unsigned long ConTm;                  /* Current session connection time in seconds */
    unsigned char CalVer[CALVERSZ];       /* Proprietary Calibration Version */
    unsigned char CalDate[CALDATESZ];     /* Proprietary Calibration Date */
} __attribute__ ((packed)) IOCTL_GET_DSP_STAT, *PIOCTL_GET_DSP_STAT;

/* Data structure for Dual Ported RAM messaging between Host and Dsp */
typedef struct _IOCTL_DPRAM_BLK
{
    unsigned short total_len;
	struct pseudo_hdr pseudohdr;
    unsigned char buffer[1780];
} __attribute__ ((packed)) IOCTL_DPRAM_BLK, *PIOCTL_DPRAM_BLK;

typedef struct _IOCTL_DPRAM_COMMAND
{
    unsigned short extra;
    IOCTL_DPRAM_BLK dpram_blk;
} __attribute__ ((packed)) IOCTL_DPRAM_COMMAND, *PIOCTL_DPRAM_COMMAND;

/*
* Custom IOCTL command codes
*/
#define FT1000_MAGIC_CODE      'F'

#define IOCTL_REGISTER_CMD					0
#define IOCTL_SET_DPRAM_CMD					3
#define IOCTL_GET_DPRAM_CMD					4
#define IOCTL_GET_DSP_STAT_CMD      6
#define IOCTL_GET_VER_CMD           7
#define IOCTL_CONNECT               10
#define IOCTL_DISCONNECT            11

#define IOCTL_FT1000_GET_DSP_STAT _IOR (FT1000_MAGIC_CODE, IOCTL_GET_DSP_STAT_CMD, sizeof(IOCTL_GET_DSP_STAT) )
#define IOCTL_FT1000_GET_VER _IOR (FT1000_MAGIC_CODE, IOCTL_GET_VER_CMD, sizeof(IOCTL_GET_VER) )
#define IOCTL_FT1000_CONNECT _IOW (FT1000_MAGIC_CODE, IOCTL_CONNECT, 0 )
#define IOCTL_FT1000_DISCONNECT _IOW (FT1000_MAGIC_CODE, IOCTL_DISCONNECT, 0 )
#define IOCTL_FT1000_SET_DPRAM _IOW (FT1000_MAGIC_CODE, IOCTL_SET_DPRAM_CMD, sizeof(IOCTL_DPRAM_BLK) )
#define IOCTL_FT1000_GET_DPRAM _IOR (FT1000_MAGIC_CODE, IOCTL_GET_DPRAM_CMD, sizeof(IOCTL_DPRAM_BLK) )
#define IOCTL_FT1000_REGISTER  _IOW (FT1000_MAGIC_CODE, IOCTL_REGISTER_CMD, sizeof(unsigned short *) )
#endif /* _FT1000IOCTLH_ */


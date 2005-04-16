/*
 * Definitions for a Sanyo CD-ROM interface.
 *
 *   Copyright (C) 1995  Vadim V. Model
 *                                       model@cecmow.enet.dec.com
 *                                       vadim@rbrf.msk.su
 *                                       vadim@ipsun.ras.ru
 *                       Eric van der Maarel
 *                                       H.T.M.v.d.Maarel@marin.nl
 *
 *  This information is based on mcd.c from M. Harriss and sjcd102.lst from
 *  E. Moenkeberg.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __SJCD_H__
#define __SJCD_H__

/*
 * Change this to set the I/O port address as default. More flexibility
 * come with setup implementation.
 */
#define SJCD_BASE_ADDR      0x340

/*
 * Change this to set the irq as default. Really SANYO do not use interrupts
 * at all.
 */
#define SJCD_INTR_NR        0

/*
 * Change this to set the dma as default value. really SANYO does not use
 * direct memory access at all.
 */
#define SJCD_DMA_NR         0

/*
 * Macros which allow us to find out the status of the drive.
 */
#define SJCD_STATUS_AVAILABLE( x ) (((x)&0x02)==0)
#define SJCD_DATA_AVAILABLE( x )   (((x)&0x01)==0)

/*
 * Port access macro. Three ports are available: S-data port (command port),
 * status port (read only) and D-data port (read only).
 */
#define SJCDPORT( x )       ( sjcd_base + ( x ) )
#define SJCD_STATUS_PORT    SJCDPORT( 1 )
#define SJCD_S_DATA_PORT    SJCDPORT( 0 )
#define SJCD_COMMAND_PORT   SJCDPORT( 0 )
#define SJCD_D_DATA_PORT    SJCDPORT( 2 )

/*
 * Drive info bits. Drive info available as first (mandatory) byte of
 * command completion status.
 */
#define SST_NOT_READY       0x10        /* no disk in the drive (???) */
#define SST_MEDIA_CHANGED   0x20        /* disk is changed */
#define SST_DOOR_OPENED     0x40        /* door is open */

/* commands */

#define SCMD_EJECT_TRAY     0xD0        /* eject tray if not locked */
#define SCMD_LOCK_TRAY      0xD2        /* lock tray when in */
#define SCMD_UNLOCK_TRAY    0xD4        /* unlock tray when in */
#define SCMD_CLOSE_TRAY     0xD6        /* load tray in */

#define SCMD_RESET          0xFA        /* soft reset */
#define SCMD_GET_STATUS     0x80
#define SCMD_GET_VERSION    0xCC

#define SCMD_DATA_READ      0xA0        /* are the same, depend on mode&args */
#define SCMD_SEEK           0xA0
#define SCMD_PLAY           0xA0

#define SCMD_GET_QINFO      0xA8

#define SCMD_SET_MODE       0xC4
#define SCMD_MODE_PLAY      0xE0
#define SCMD_MODE_COOKED    (0xF8 & ~0x20)
#define SCMD_MODE_RAW       0xF9
#define SCMD_MODE_x20_BIT   0x20        /* What is it for ? */

#define SCMD_SET_VOLUME     0xAE
#define SCMD_PAUSE          0xE0
#define SCMD_STOP           0xE0

#define SCMD_GET_DISK_INFO  0xAA

/*
 * Some standard arguments for SCMD_GET_DISK_INFO.
 */
#define SCMD_GET_1_TRACK    0xA0    /* get the first track information */
#define SCMD_GET_L_TRACK    0xA1    /* get the last track information */
#define SCMD_GET_D_SIZE     0xA2    /* get the whole disk information */

/*
 * Borrowed from hd.c. Allows to optimize multiple port read commands.
 */
#define S_READ_DATA( port, buf, nr )      insb( port, buf, nr )

/*
 * We assume that there are no audio disks with TOC length more than this
 * number (I personally have never seen disks with more than 20 fragments).
 */
#define SJCD_MAX_TRACKS		100

struct msf {
  unsigned char   min;
  unsigned char   sec;
  unsigned char   frame;
};

struct sjcd_hw_disk_info {
  unsigned char track_control;
  unsigned char track_no;
  unsigned char x, y, z;
  union {
    unsigned char track_no;
    struct msf track_msf;
  } un;
};

struct sjcd_hw_qinfo {
  unsigned char track_control;
  unsigned char track_no;
  unsigned char x;
  struct msf rel;
  struct msf abs;
};

struct sjcd_play_msf {
  struct msf  start;
  struct msf  end;
};

struct sjcd_disk_info {
  unsigned char   first;
  unsigned char   last;
  struct msf      disk_length;
  struct msf      first_track;
};

struct sjcd_toc {
  unsigned char   ctrl_addr;
  unsigned char   track;
  unsigned char   point_index;
  struct msf      track_time;
  struct msf      disk_time;
};

#if defined( SJCD_GATHER_STAT )

struct sjcd_stat {
  int ticks;
  int tticks[ 8 ];
  int idle_ticks;
  int start_ticks;
  int mode_ticks;
  int read_ticks;
  int data_ticks;
  int stop_ticks;
  int stopping_ticks;
};

#endif

#endif

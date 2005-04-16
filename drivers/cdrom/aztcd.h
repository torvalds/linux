/* $Id: aztcd.h,v 2.60 1997/11/29 09:51:22 root Exp root $
 *
 * Definitions for a AztechCD268 CD-ROM interface
 *	Copyright (C) 1994-98  Werner Zimmermann
 *
 *	based on Mitsumi CDROM driver by Martin Harriss
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
 *
 *  History:	W.Zimmermann adaption to Aztech CD268-01A Version 1.3
 *		October 1994 Email: Werner.Zimmermann@fht-esslingen.de
 */

/* *** change this to set the I/O port address of your CD-ROM drive,
       set to '-1', if you want autoprobing */
#define AZT_BASE_ADDR		-1

/* list of autoprobing addresses (not more than 15), last value must be 0x000
   Note: Autoprobing is only enabled, if AZT_BASE_ADDR is set to '-1' ! */
#define AZT_BASE_AUTO 		{ 0x320, 0x300, 0x310, 0x330, 0x000 }

/* Uncomment this, if your CDROM is connected to a Soundwave32-soundcard
   and configure AZT_BASE_ADDR and AZT_SW32_BASE_ADDR */
/*#define AZT_SW32 1
*/

#ifdef AZT_SW32 
#define AZT_SW32_BASE_ADDR      0x220  /*I/O port base address of your soundcard*/
#endif

/* Set this to 1, if you want your tray to be locked, set to 0 to prevent tray 
   from locking */
#define AZT_ALLOW_TRAY_LOCK	1

/*Set this to 1 to allow auto-eject when unmounting a disk, set to 0, if you 
  don't want the auto-eject feature*/
#define AZT_AUTO_EJECT          0

/*Set this to 1, if you want to use incompatible ioctls for reading in raw and
  cooked mode */
#define AZT_PRIVATE_IOCTLS      1

/*Set this to 1, if you want multisession support by the ISO fs. Even if you set 
  this value to '0' you can use multisession CDs. In that case the drive's firm-
  ware will do the appropriate redirection automatically. The CD will then look
  like a single session CD (but nevertheless all data may be read). Please read 
  chapter '5.1 Multisession support' in README.aztcd for details. Normally it's 
  uncritical to leave this setting untouched */
#define AZT_MULTISESSION        1

/*Uncomment this, if you are using a linux kernel version prior to 2.1.0 */
/*#define AZT_KERNEL_PRIOR_2_1 */

/*---------------------------------------------------------------------------*/
/*-----nothing to be configured for normal applications below this line------*/


/* Increase this if you get lots of timeouts; if you get kernel panic, replace
   STEN_LOW_WAIT by STEN_LOW in the source code */
#define AZT_STATUS_DELAY	400       /*for timer wait, STEN_LOW_WAIT*/
#define AZT_TIMEOUT		8000000   /*for busy wait STEN_LOW, DTEN_LOW*/
#define AZT_FAST_TIMEOUT	10000     /*for reading the version string*/

/* number of times to retry a command before giving up */
#define AZT_RETRY_ATTEMPTS	3

/* port access macros */
#define CMD_PORT		azt_port
#define DATA_PORT		azt_port
#define STATUS_PORT		azt_port+1
#define MODE_PORT		azt_port+2
#ifdef  AZT_SW32                
 #define AZT_SW32_INIT          (unsigned int) (0xFF00 & (AZT_BASE_ADDR*16))
 #define AZT_SW32_CONFIG_REG    AZT_SW32_BASE_ADDR+0x16  /*Soundwave32 Config. Register*/
 #define AZT_SW32_ID_REG        AZT_SW32_BASE_ADDR+0x04  /*Soundwave32 ID Version Register*/
#endif

/* status bits */
#define AST_CMD_CHECK		0x80		/* 1 = command error */
#define AST_DOOR_OPEN		0x40		/* 1 = door is open */
#define AST_NOT_READY		0x20		/* 1 = no disk in the drive */
#define AST_DSK_CHG		0x02		/* 1 = disk removed or changed */
#define AST_MODE                0x01            /* 0=MODE1, 1=MODE2 */
#define AST_MODE_BITS		0x1C		/* Mode Bits */
#define AST_INITIAL		0x0C		/* initial, only valid ... */
#define AST_BUSY		0x04		/* now playing, only valid
						   in combination with mode
						   bits */
/* flag bits */
#define AFL_DATA		0x02		/* data available if low */
#define AFL_STATUS		0x04		/* status available if low */
#define AFL_OP_OK		0x01		/* OP_OK command correct*/
#define AFL_PA_OK		0x02		/* PA_OK parameter correct*/
#define AFL_OP_ERR		0x05		/* error in command*/
#define AFL_PA_ERR		0x06		/* error in parameters*/
#define POLLED			0x04		/* polled mode */

/* commands */
#define ACMD_SOFT_RESET		0x10		/* reset drive */
#define ACMD_PLAY_READ		0x20		/* read data track in cooked mode */
#define ACMD_PLAY_READ_RAW      0x21		/* reading in raw mode*/
#define ACMD_SEEK               0x30            /* seek msf address*/
#define ACMD_SEEK_TO_LEADIN     0x31		/* seek to leadin track*/
#define ACMD_GET_ERROR		0x40		/* get error code */
#define ACMD_GET_STATUS		0x41		/* get status */
#define ACMD_GET_Q_CHANNEL      0x50		/* read info from q channel */
#define ACMD_EJECT		0x60		/* eject/open tray */
#define ACMD_CLOSE              0x61            /* close tray */
#define ACMD_LOCK		0x71		/* lock tray closed */
#define ACMD_UNLOCK		0x72		/* unlock tray */
#define ACMD_PAUSE		0x80		/* pause */
#define ACMD_STOP		0x81		/* stop play */
#define ACMD_PLAY_AUDIO		0x90		/* play audio track */
#define ACMD_SET_VOLUME		0x93		/* set audio level */
#define ACMD_GET_VERSION	0xA0		/* get firmware version */
#define ACMD_SET_DISK_TYPE	0xA1		/* set disk data mode */

#define MAX_TRACKS		104

struct msf {
	unsigned char	min;
	unsigned char	sec;
	unsigned char	frame;
};

struct azt_Play_msf {
	struct msf	start;
	struct msf	end;
};

struct azt_DiskInfo {
	unsigned char	first;
        unsigned char   next;
	unsigned char	last;
	struct msf	diskLength;
	struct msf	firstTrack;
        unsigned char   multi;
        struct msf      nextSession;
        struct msf      lastSession;
        unsigned char   xa;
        unsigned char   audio;
};

struct azt_Toc {
	unsigned char	ctrl_addr;
	unsigned char	track;
	unsigned char	pointIndex;
	struct msf	trackTime;
	struct msf	diskTime;
};

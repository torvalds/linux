/*	linux/drivers/cdrom/optcd.c - Optics Storage 8000 AT CDROM driver
	$Id: optcd.c,v 1.11 1997/01/26 07:13:00 davem Exp $

	Copyright (C) 1995 Leo Spiekman (spiekman@dutette.et.tudelft.nl)


	Based on Aztech CD268 CDROM driver by Werner Zimmermann and preworks
	by Eberhard Moenkeberg (emoenke@gwdg.de). 

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*	Revision history


	14-5-95		v0.0	Plays sound tracks. No reading of data CDs yet.
				Detection of disk change doesn't work.
	21-5-95		v0.1	First ALPHA version. CD can be mounted. The
				device major nr is borrowed from the Aztech
				driver. Speed is around 240 kb/s, as measured
				with "time dd if=/dev/cdrom of=/dev/null \
				bs=2048 count=4096".
	24-6-95		v0.2	Reworked the #defines for the command codes
				and the like, as well as the structure of
				the hardware communication protocol, to
				reflect the "official" documentation, kindly
				supplied by C.K. Tan, Optics Storage Pte. Ltd.
				Also tidied up the state machine somewhat.
	28-6-95		v0.3	Removed the ISP-16 interface code, as this
				should go into its own driver. The driver now
				has its own major nr.
				Disk change detection now seems to work, too.
				This version became part of the standard
				kernel as of version 1.3.7
	24-9-95		v0.4	Re-inserted ISP-16 interface code which I
				copied from sjcd.c, with a few changes.
				Updated README.optcd. Submitted for
				inclusion in 1.3.21
	29-9-95		v0.4a	Fixed bug that prevented compilation as module
	25-10-95	v0.5	Started multisession code. Implementation
				copied from Werner Zimmermann, who copied it
				from Heiko Schlittermann's mcdx.
	17-1-96		v0.6	Multisession works; some cleanup too.
	18-4-96		v0.7	Increased some timing constants;
				thanks to Luke McFarlane. Also tidied up some
				printk behaviour. ISP16 initialization
				is now handled by a separate driver.
				
	09-11-99 	  	Make kernel-parameter implementation work with 2.3.x 
	                 	Removed init_module & cleanup_module in favor of 
			 	module_init & module_exit.
			 	Torben Mathiasen <tmm@image.dk>
*/

/* Includes */


#include <linux/module.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>
#include <linux/blkdev.h>

#include <linux/cdrom.h>
#include "optcd.h"

#include <asm/uaccess.h>

#define MAJOR_NR OPTICS_CDROM_MAJOR
#define QUEUE (opt_queue)
#define CURRENT elv_next_request(opt_queue)


/* Debug support */


/* Don't forget to add new debug flags here. */
#if DEBUG_DRIVE_IF | DEBUG_VFS | DEBUG_CONV | DEBUG_TOC | \
    DEBUG_BUFFERS | DEBUG_REQUEST | DEBUG_STATE | DEBUG_MULTIS
#define DEBUG(x) debug x
static void debug(int debug_this, const char* fmt, ...)
{
	char s[1024];
	va_list args;

	if (!debug_this)
		return;

	va_start(args, fmt);
	vsprintf(s, fmt, args);
	printk(KERN_DEBUG "optcd: %s\n", s);
	va_end(args);
}
#else
#define DEBUG(x)
#endif


/* Drive hardware/firmware characteristics
   Identifiers in accordance with Optics Storage documentation */


#define optcd_port optcd			/* Needed for the modutils. */
static short optcd_port = OPTCD_PORTBASE;	/* I/O base of drive. */
module_param(optcd_port, short, 0);
/* Drive registers, read */
#define DATA_PORT	optcd_port	/* Read data/status */
#define STATUS_PORT	optcd_port+1	/* Indicate data/status availability */

/* Drive registers, write */
#define COMIN_PORT	optcd_port	/* For passing command/parameter */
#define RESET_PORT	optcd_port+1	/* Write anything and wait 0.5 sec */
#define HCON_PORT	optcd_port+2	/* Host Xfer Configuration */


/* Command completion/status read from DATA register */
#define ST_DRVERR		0x80
#define ST_DOOR_OPEN		0x40
#define ST_MIXEDMODE_DISK	0x20
#define ST_MODE_BITS		0x1c
#define ST_M_STOP		0x00
#define ST_M_READ		0x04
#define ST_M_AUDIO		0x04
#define ST_M_PAUSE		0x08
#define ST_M_INITIAL		0x0c
#define ST_M_ERROR		0x10
#define ST_M_OTHERS		0x14
#define	ST_MODE2TRACK		0x02
#define	ST_DSK_CHG		0x01
#define ST_L_LOCK		0x01
#define ST_CMD_OK		0x00
#define ST_OP_OK		0x01
#define ST_PA_OK		0x02
#define ST_OP_ERROR		0x05
#define ST_PA_ERROR		0x06


/* Error codes (appear as command completion code from DATA register) */
/* Player related errors */
#define ERR_ILLCMD	0x11	/* Illegal command to player module */
#define ERR_ILLPARM	0x12	/* Illegal parameter to player module */
#define ERR_SLEDGE	0x13
#define ERR_FOCUS	0x14
#define ERR_MOTOR	0x15
#define ERR_RADIAL	0x16
#define ERR_PLL		0x17	/* PLL lock error */
#define ERR_SUB_TIM	0x18	/* Subcode timeout error */
#define ERR_SUB_NF	0x19	/* Subcode not found error */
#define ERR_TRAY	0x1a
#define ERR_TOC		0x1b	/* Table of Contents read error */
#define ERR_JUMP	0x1c
/* Data errors */
#define ERR_MODE	0x21
#define ERR_FORM	0x22
#define ERR_HEADADDR	0x23	/* Header Address not found */
#define ERR_CRC		0x24
#define ERR_ECC		0x25	/* Uncorrectable ECC error */
#define ERR_CRC_UNC	0x26	/* CRC error and uncorrectable error */
#define ERR_ILLBSYNC	0x27	/* Illegal block sync error */
#define ERR_VDST	0x28	/* VDST not found */
/* Timeout errors */
#define ERR_READ_TIM	0x31	/* Read timeout error */
#define ERR_DEC_STP	0x32	/* Decoder stopped */
#define ERR_DEC_TIM	0x33	/* Decoder interrupt timeout error */
/* Function abort codes */
#define ERR_KEY		0x41	/* Key -Detected abort */
#define ERR_READ_FINISH	0x42	/* Read Finish */
/* Second Byte diagnostic codes */
#define ERR_NOBSYNC	0x01	/* No block sync */
#define ERR_SHORTB	0x02	/* Short block */
#define ERR_LONGB	0x03	/* Long block */
#define ERR_SHORTDSP	0x04	/* Short DSP word */
#define ERR_LONGDSP	0x05	/* Long DSP word */


/* Status availability flags read from STATUS register */
#define FL_EJECT	0x20
#define FL_WAIT		0x10	/* active low */
#define FL_EOP		0x08	/* active low */
#define FL_STEN		0x04	/* Status available when low */
#define FL_DTEN		0x02	/* Data available when low */
#define FL_DRQ		0x01	/* active low */
#define FL_RESET	0xde	/* These bits are high after a reset */
#define FL_STDT		(FL_STEN|FL_DTEN)


/* Transfer mode, written to HCON register */
#define HCON_DTS	0x08
#define HCON_SDRQB	0x04
#define HCON_LOHI	0x02
#define HCON_DMA16	0x01


/* Drive command set, written to COMIN register */
/* Quick response commands */
#define COMDRVST	0x20	/* Drive Status Read */
#define COMERRST	0x21	/* Error Status Read */
#define COMIOCTLISTAT	0x22	/* Status Read; reset disk changed bit */
#define COMINITSINGLE	0x28	/* Initialize Single Speed */
#define COMINITDOUBLE	0x29	/* Initialize Double Speed */
#define COMUNLOCK	0x30	/* Unlock */
#define COMLOCK		0x31	/* Lock */
#define COMLOCKST	0x32	/* Lock/Unlock Status */
#define COMVERSION	0x40	/* Get Firmware Revision */
#define COMVOIDREADMODE	0x50	/* Void Data Read Mode */
/* Read commands */
#define COMFETCH	0x60	/* Prefetch Data */
#define COMREAD		0x61	/* Read */
#define COMREADRAW	0x62	/* Read Raw Data */
#define COMREADALL	0x63	/* Read All 2646 Bytes */
/* Player control commands */
#define COMLEADIN	0x70	/* Seek To Lead-in */
#define COMSEEK		0x71	/* Seek */
#define COMPAUSEON	0x80	/* Pause On */
#define COMPAUSEOFF	0x81	/* Pause Off */
#define COMSTOP		0x82	/* Stop */
#define COMOPEN		0x90	/* Open Tray Door */
#define COMCLOSE	0x91	/* Close Tray Door */
#define COMPLAY		0xa0	/* Audio Play */
#define COMPLAY_TNO	0xa2	/* Audio Play By Track Number */
#define COMSUBQ		0xb0	/* Read Sub-q Code */
#define COMLOCATION	0xb1	/* Read Head Position */
/* Audio control commands */
#define COMCHCTRL	0xc0	/* Audio Channel Control */
/* Miscellaneous (test) commands */
#define COMDRVTEST	0xd0	/* Write Test Bytes */
#define COMTEST		0xd1	/* Diagnostic Test */

/* Low level drive interface. Only here we do actual I/O
   Waiting for status / data available */


/* Busy wait until FLAG goes low. Return 0 on timeout. */
inline static int flag_low(int flag, unsigned long timeout)
{
	int flag_high;
	unsigned long count = 0;

	while ((flag_high = (inb(STATUS_PORT) & flag)))
		if (++count >= timeout)
			break;

	DEBUG((DEBUG_DRIVE_IF, "flag_low 0x%x count %ld%s",
		flag, count, flag_high ? " timeout" : ""));
	return !flag_high;
}


/* Timed waiting for status or data */
static int sleep_timeout;	/* max # of ticks to sleep */
static DECLARE_WAIT_QUEUE_HEAD(waitq);
static void sleep_timer(unsigned long data);
static struct timer_list delay_timer = TIMER_INITIALIZER(sleep_timer, 0, 0);
static DEFINE_SPINLOCK(optcd_lock);
static struct request_queue *opt_queue;

/* Timer routine: wake up when desired flag goes low,
   or when timeout expires. */
static void sleep_timer(unsigned long data)
{
	int flags = inb(STATUS_PORT) & FL_STDT;

	if (flags == FL_STDT && --sleep_timeout > 0) {
		mod_timer(&delay_timer, jiffies + HZ/100); /* multi-statement macro */
	} else
		wake_up(&waitq);
}


/* Sleep until FLAG goes low. Return 0 on timeout or wrong flag low. */
static int sleep_flag_low(int flag, unsigned long timeout)
{
	int flag_high;

	DEBUG((DEBUG_DRIVE_IF, "sleep_flag_low"));

	sleep_timeout = timeout;
	flag_high = inb(STATUS_PORT) & flag;
	if (flag_high && sleep_timeout > 0) {
		mod_timer(&delay_timer, jiffies + HZ/100);
		sleep_on(&waitq);
		flag_high = inb(STATUS_PORT) & flag;
	}

	DEBUG((DEBUG_DRIVE_IF, "flag 0x%x count %ld%s",
		flag, timeout, flag_high ? " timeout" : ""));
	return !flag_high;
}

/* Low level drive interface. Only here we do actual I/O
   Sending commands and parameters */


/* Errors in the command protocol */
#define ERR_IF_CMD_TIMEOUT	0x100
#define ERR_IF_ERR_TIMEOUT	0x101
#define ERR_IF_RESP_TIMEOUT	0x102
#define ERR_IF_DATA_TIMEOUT	0x103
#define ERR_IF_NOSTAT		0x104


/* Send command code. Return <0 indicates error */
static int send_cmd(int cmd)
{
	unsigned char ack;

	DEBUG((DEBUG_DRIVE_IF, "sending command 0x%02x\n", cmd));

	outb(HCON_DTS, HCON_PORT);	/* Enable Suspend Data Transfer */
	outb(cmd, COMIN_PORT);		/* Send command code */
	if (!flag_low(FL_STEN, BUSY_TIMEOUT))	/* Wait for status */
		return -ERR_IF_CMD_TIMEOUT;
	ack = inb(DATA_PORT);		/* read command acknowledge */
	outb(HCON_SDRQB, HCON_PORT);	/* Disable Suspend Data Transfer */
	return ack==ST_OP_OK ? 0 : -ack;
}


/* Send command parameters. Return <0 indicates error */
static int send_params(struct cdrom_msf *params)
{
	unsigned char ack;

	DEBUG((DEBUG_DRIVE_IF, "sending parameters"
		" %02x:%02x:%02x"
		" %02x:%02x:%02x",
		params->cdmsf_min0,
		params->cdmsf_sec0,
		params->cdmsf_frame0,
		params->cdmsf_min1,
		params->cdmsf_sec1,
		params->cdmsf_frame1));

	outb(params->cdmsf_min0, COMIN_PORT);
	outb(params->cdmsf_sec0, COMIN_PORT);
	outb(params->cdmsf_frame0, COMIN_PORT);
	outb(params->cdmsf_min1, COMIN_PORT);
	outb(params->cdmsf_sec1, COMIN_PORT);
	outb(params->cdmsf_frame1, COMIN_PORT);
	if (!flag_low(FL_STEN, BUSY_TIMEOUT))	/* Wait for status */
		return -ERR_IF_CMD_TIMEOUT;
	ack = inb(DATA_PORT);		/* read command acknowledge */
	return ack==ST_PA_OK ? 0 : -ack;
}


/* Send parameters for SEEK command. Return <0 indicates error */
static int send_seek_params(struct cdrom_msf *params)
{
	unsigned char ack;

	DEBUG((DEBUG_DRIVE_IF, "sending seek parameters"
		" %02x:%02x:%02x",
		params->cdmsf_min0,
		params->cdmsf_sec0,
		params->cdmsf_frame0));

	outb(params->cdmsf_min0, COMIN_PORT);
	outb(params->cdmsf_sec0, COMIN_PORT);
	outb(params->cdmsf_frame0, COMIN_PORT);
	if (!flag_low(FL_STEN, BUSY_TIMEOUT))	/* Wait for status */
		return -ERR_IF_CMD_TIMEOUT;
	ack = inb(DATA_PORT);		/* read command acknowledge */
	return ack==ST_PA_OK ? 0 : -ack;
}


/* Wait for command execution status. Choice between busy waiting
   and sleeping. Return value <0 indicates timeout. */
inline static int get_exec_status(int busy_waiting)
{
	unsigned char exec_status;

	if (busy_waiting
	    ? !flag_low(FL_STEN, BUSY_TIMEOUT)
	    : !sleep_flag_low(FL_STEN, SLEEP_TIMEOUT))
		return -ERR_IF_CMD_TIMEOUT;

	exec_status = inb(DATA_PORT);
	DEBUG((DEBUG_DRIVE_IF, "returned exec status 0x%02x", exec_status));
	return exec_status;
}


/* Wait busy for extra byte of data that a command returns.
   Return value <0 indicates timeout. */
inline static int get_data(int short_timeout)
{
	unsigned char data;

	if (!flag_low(FL_STEN, short_timeout ? FAST_TIMEOUT : BUSY_TIMEOUT))
		return -ERR_IF_DATA_TIMEOUT;

	data = inb(DATA_PORT);
	DEBUG((DEBUG_DRIVE_IF, "returned data 0x%02x", data));
	return data;
}


/* Returns 0 if failed */
static int reset_drive(void)
{
	unsigned long count = 0;
	int flags;

	DEBUG((DEBUG_DRIVE_IF, "reset drive"));

	outb(0, RESET_PORT);
	while (++count < RESET_WAIT)
		inb(DATA_PORT);

	count = 0;
	while ((flags = (inb(STATUS_PORT) & FL_RESET)) != FL_RESET)
		if (++count >= BUSY_TIMEOUT)
			break;

	DEBUG((DEBUG_DRIVE_IF, "reset %s",
		flags == FL_RESET ? "succeeded" : "failed"));

	if (flags != FL_RESET)
		return 0;		/* Reset failed */
	outb(HCON_SDRQB, HCON_PORT);	/* Disable Suspend Data Transfer */
	return 1;			/* Reset succeeded */
}


/* Facilities for asynchronous operation */

/* Read status/data availability flags FL_STEN and FL_DTEN */
inline static int stdt_flags(void)
{
	return inb(STATUS_PORT) & FL_STDT;
}


/* Fetch status that has previously been waited for. <0 means not available */
inline static int fetch_status(void)
{
	unsigned char status;

	if (inb(STATUS_PORT) & FL_STEN)
		return -ERR_IF_NOSTAT;

	status = inb(DATA_PORT);
	DEBUG((DEBUG_DRIVE_IF, "fetched exec status 0x%02x", status));
	return status;
}


/* Fetch data that has previously been waited for. */
inline static void fetch_data(char *buf, int n)
{
	insb(DATA_PORT, buf, n);
	DEBUG((DEBUG_DRIVE_IF, "fetched 0x%x bytes", n));
}


/* Flush status and data fifos */
inline static void flush_data(void)
{
	while ((inb(STATUS_PORT) & FL_STDT) != FL_STDT)
		inb(DATA_PORT);
	DEBUG((DEBUG_DRIVE_IF, "flushed fifos"));
}

/* Command protocol */


/* Send a simple command and wait for response. Command codes < COMFETCH
   are quick response commands */
inline static int exec_cmd(int cmd)
{
	int ack = send_cmd(cmd);
	if (ack < 0)
		return ack;
	return get_exec_status(cmd < COMFETCH);
}


/* Send a command with parameters. Don't wait for the response,
 * which consists of data blocks read from the CD. */
inline static int exec_read_cmd(int cmd, struct cdrom_msf *params)
{
	int ack = send_cmd(cmd);
	if (ack < 0)
		return ack;
	return send_params(params);
}


/* Send a seek command with parameters and wait for response */
inline static int exec_seek_cmd(int cmd, struct cdrom_msf *params)
{
	int ack = send_cmd(cmd);
	if (ack < 0)
		return ack;
	ack = send_seek_params(params);
	if (ack < 0)
		return ack;
	return 0;
}


/* Send a command with parameters and wait for response */
inline static int exec_long_cmd(int cmd, struct cdrom_msf *params)
{
	int ack = exec_read_cmd(cmd, params);
	if (ack < 0)
		return ack;
	return get_exec_status(0);
}

/* Address conversion routines */


/* Binary to BCD (2 digits) */
inline static void single_bin2bcd(u_char *p)
{
	DEBUG((DEBUG_CONV, "bin2bcd %02d", *p));
	*p = (*p % 10) | ((*p / 10) << 4);
}


/* Convert entire msf struct */
static void bin2bcd(struct cdrom_msf *msf)
{
	single_bin2bcd(&msf->cdmsf_min0);
	single_bin2bcd(&msf->cdmsf_sec0);
	single_bin2bcd(&msf->cdmsf_frame0);
	single_bin2bcd(&msf->cdmsf_min1);
	single_bin2bcd(&msf->cdmsf_sec1);
	single_bin2bcd(&msf->cdmsf_frame1);
}


/* Linear block address to minute, second, frame form */
#define CD_FPM	(CD_SECS * CD_FRAMES)	/* frames per minute */

static void lba2msf(int lba, struct cdrom_msf *msf)
{
	DEBUG((DEBUG_CONV, "lba2msf %d", lba));
	lba += CD_MSF_OFFSET;
	msf->cdmsf_min0 = lba / CD_FPM; lba %= CD_FPM;
	msf->cdmsf_sec0 = lba / CD_FRAMES;
	msf->cdmsf_frame0 = lba % CD_FRAMES;
	msf->cdmsf_min1 = 0;
	msf->cdmsf_sec1 = 0;
	msf->cdmsf_frame1 = 0;
	bin2bcd(msf);
}


/* Two BCD digits to binary */
inline static u_char bcd2bin(u_char bcd)
{
	DEBUG((DEBUG_CONV, "bcd2bin %x%02x", bcd));
	return (bcd >> 4) * 10 + (bcd & 0x0f);
}


static void msf2lba(union cdrom_addr *addr)
{
	addr->lba = addr->msf.minute * CD_FPM
	            + addr->msf.second * CD_FRAMES
	            + addr->msf.frame - CD_MSF_OFFSET;
}


/* Minute, second, frame address BCD to binary or to linear address,
   depending on MODE */
static void msf_bcd2bin(union cdrom_addr *addr)
{
	addr->msf.minute = bcd2bin(addr->msf.minute);
	addr->msf.second = bcd2bin(addr->msf.second);
	addr->msf.frame = bcd2bin(addr->msf.frame);
}

/* High level drive commands */


static int audio_status = CDROM_AUDIO_NO_STATUS;
static char toc_uptodate = 0;
static char disk_changed = 1;

/* Get drive status, flagging completion of audio play and disk changes. */
static int drive_status(void)
{
	int status;

	status = exec_cmd(COMIOCTLISTAT);
	DEBUG((DEBUG_DRIVE_IF, "IOCTLISTAT: %03x", status));
	if (status < 0)
		return status;
	if (status == 0xff)	/* No status available */
		return -ERR_IF_NOSTAT;

	if (((status & ST_MODE_BITS) != ST_M_AUDIO) &&
		(audio_status == CDROM_AUDIO_PLAY)) {
		audio_status = CDROM_AUDIO_COMPLETED;
	}

	if (status & ST_DSK_CHG) {
		toc_uptodate = 0;
		disk_changed = 1;
		audio_status = CDROM_AUDIO_NO_STATUS;
	}

	return status;
}


/* Read the current Q-channel info. Also used for reading the
   table of contents. qp->cdsc_format must be set on entry to
   indicate the desired address format */
static int get_q_channel(struct cdrom_subchnl *qp)
{
	int status, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10;

	status = drive_status();
	if (status < 0)
		return status;
	qp->cdsc_audiostatus = audio_status;

	status = exec_cmd(COMSUBQ);
	if (status < 0)
		return status;

	d1 = get_data(0);
	if (d1 < 0)
		return d1;
	qp->cdsc_adr = d1;
	qp->cdsc_ctrl = d1 >> 4;

	d2 = get_data(0);
	if (d2 < 0)
		return d2;
	qp->cdsc_trk = bcd2bin(d2);

	d3 = get_data(0);
	if (d3 < 0)
		return d3;
	qp->cdsc_ind = bcd2bin(d3);

	d4 = get_data(0);
	if (d4 < 0)
		return d4;
	qp->cdsc_reladdr.msf.minute = d4;

	d5 = get_data(0);
	if (d5 < 0)
		return d5;
	qp->cdsc_reladdr.msf.second = d5;

	d6 = get_data(0);
	if (d6 < 0)
		return d6;
	qp->cdsc_reladdr.msf.frame = d6;

	d7 = get_data(0);
	if (d7 < 0)
		return d7;
	/* byte not used */

	d8 = get_data(0);
	if (d8 < 0)
		return d8;
	qp->cdsc_absaddr.msf.minute = d8;

	d9 = get_data(0);
	if (d9 < 0)
		return d9;
	qp->cdsc_absaddr.msf.second = d9;

	d10 = get_data(0);
	if (d10 < 0)
		return d10;
	qp->cdsc_absaddr.msf.frame = d10;

	DEBUG((DEBUG_TOC, "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		d1, d2, d3, d4, d5, d6, d7, d8, d9, d10));

	msf_bcd2bin(&qp->cdsc_absaddr);
	msf_bcd2bin(&qp->cdsc_reladdr);
	if (qp->cdsc_format == CDROM_LBA) {
		msf2lba(&qp->cdsc_absaddr);
		msf2lba(&qp->cdsc_reladdr);
	}

	return 0;
}

/* Table of contents handling */


/* Errors in table of contents */
#define ERR_TOC_MISSINGINFO	0x120
#define ERR_TOC_MISSINGENTRY	0x121


struct cdrom_disk_info {
	unsigned char		first;
	unsigned char		last;
	struct cdrom_msf0	disk_length;
	struct cdrom_msf0	first_track;
	/* Multisession info: */
	unsigned char		next;
	struct cdrom_msf0	next_session;
	struct cdrom_msf0	last_session;
	unsigned char		multi;
	unsigned char		xa;
	unsigned char		audio;
};
static struct cdrom_disk_info disk_info;

#define MAX_TRACKS		111
static struct cdrom_subchnl toc[MAX_TRACKS];

#define QINFO_FIRSTTRACK	100 /* bcd2bin(0xa0) */
#define QINFO_LASTTRACK		101 /* bcd2bin(0xa1) */
#define QINFO_DISKLENGTH	102 /* bcd2bin(0xa2) */
#define QINFO_NEXTSESSION	110 /* bcd2bin(0xb0) */

#define I_FIRSTTRACK	0x01
#define I_LASTTRACK	0x02
#define I_DISKLENGTH	0x04
#define I_NEXTSESSION	0x08
#define I_ALL	(I_FIRSTTRACK | I_LASTTRACK | I_DISKLENGTH)


#if DEBUG_TOC
static void toc_debug_info(int i)
{
	printk(KERN_DEBUG "#%3d ctl %1x, adr %1x, track %2d index %3d"
		"  %2d:%02d.%02d %2d:%02d.%02d\n",
		i, toc[i].cdsc_ctrl, toc[i].cdsc_adr,
		toc[i].cdsc_trk, toc[i].cdsc_ind,
		toc[i].cdsc_reladdr.msf.minute,
		toc[i].cdsc_reladdr.msf.second,
		toc[i].cdsc_reladdr.msf.frame,
		toc[i].cdsc_absaddr.msf.minute,
		toc[i].cdsc_absaddr.msf.second,
		toc[i].cdsc_absaddr.msf.frame);
}
#endif


static int read_toc(void)
{
	int status, limit, count;
	unsigned char got_info = 0;
	struct cdrom_subchnl q_info;
#if DEBUG_TOC
	int i;
#endif

	DEBUG((DEBUG_TOC, "starting read_toc"));

	count = 0;
	for (limit = 60; limit > 0; limit--) {
		int index;

		q_info.cdsc_format = CDROM_MSF;
		status = get_q_channel(&q_info);
		if (status < 0)
			return status;

		index = q_info.cdsc_ind;
		if (index > 0 && index < MAX_TRACKS
		    && q_info.cdsc_trk == 0 && toc[index].cdsc_ind == 0) {
			toc[index] = q_info;
			DEBUG((DEBUG_TOC, "got %d", index));
			if (index < 100)
				count++;

			switch (q_info.cdsc_ind) {
			case QINFO_FIRSTTRACK:
				got_info |= I_FIRSTTRACK;
				break;
			case QINFO_LASTTRACK:
				got_info |= I_LASTTRACK;
				break;
			case QINFO_DISKLENGTH:
				got_info |= I_DISKLENGTH;
				break;
			case QINFO_NEXTSESSION:
				got_info |= I_NEXTSESSION;
				break;
			}
		}

		if ((got_info & I_ALL) == I_ALL
		    && toc[QINFO_FIRSTTRACK].cdsc_absaddr.msf.minute + count
		       >= toc[QINFO_LASTTRACK].cdsc_absaddr.msf.minute + 1)
			break;
	}

	/* Construct disk_info from TOC */
	if (disk_info.first == 0) {
		disk_info.first = toc[QINFO_FIRSTTRACK].cdsc_absaddr.msf.minute;
		disk_info.first_track.minute =
			toc[disk_info.first].cdsc_absaddr.msf.minute;
		disk_info.first_track.second =
			toc[disk_info.first].cdsc_absaddr.msf.second;
		disk_info.first_track.frame =
			toc[disk_info.first].cdsc_absaddr.msf.frame;
	}
	disk_info.last = toc[QINFO_LASTTRACK].cdsc_absaddr.msf.minute;
	disk_info.disk_length.minute =
			toc[QINFO_DISKLENGTH].cdsc_absaddr.msf.minute;
	disk_info.disk_length.second =
			toc[QINFO_DISKLENGTH].cdsc_absaddr.msf.second-2;
	disk_info.disk_length.frame =
			toc[QINFO_DISKLENGTH].cdsc_absaddr.msf.frame;
	disk_info.next_session.minute =
			toc[QINFO_NEXTSESSION].cdsc_reladdr.msf.minute;
	disk_info.next_session.second =
			toc[QINFO_NEXTSESSION].cdsc_reladdr.msf.second;
	disk_info.next_session.frame =
			toc[QINFO_NEXTSESSION].cdsc_reladdr.msf.frame;
	disk_info.next = toc[QINFO_FIRSTTRACK].cdsc_absaddr.msf.minute;
	disk_info.last_session.minute =
			toc[disk_info.next].cdsc_absaddr.msf.minute;
	disk_info.last_session.second =
			toc[disk_info.next].cdsc_absaddr.msf.second;
	disk_info.last_session.frame =
			toc[disk_info.next].cdsc_absaddr.msf.frame;
	toc[disk_info.last + 1].cdsc_absaddr.msf.minute =
			disk_info.disk_length.minute;
	toc[disk_info.last + 1].cdsc_absaddr.msf.second =
			disk_info.disk_length.second;
	toc[disk_info.last + 1].cdsc_absaddr.msf.frame =
			disk_info.disk_length.frame;
#if DEBUG_TOC
	for (i = 1; i <= disk_info.last + 1; i++)
		toc_debug_info(i);
	toc_debug_info(QINFO_FIRSTTRACK);
	toc_debug_info(QINFO_LASTTRACK);
	toc_debug_info(QINFO_DISKLENGTH);
	toc_debug_info(QINFO_NEXTSESSION);
#endif

	DEBUG((DEBUG_TOC, "exiting read_toc, got_info %x, count %d",
		got_info, count));
	if ((got_info & I_ALL) != I_ALL
	    || toc[QINFO_FIRSTTRACK].cdsc_absaddr.msf.minute + count
	       < toc[QINFO_LASTTRACK].cdsc_absaddr.msf.minute + 1)
		return -ERR_TOC_MISSINGINFO;
	return 0;
}


#ifdef MULTISESSION
static int get_multi_disk_info(void)
{
	int sessions, status;
	struct cdrom_msf multi_index;


	for (sessions = 2; sessions < 10 /* %%for now */; sessions++) {
		int count;

		for (count = 100; count < MAX_TRACKS; count++) 
			toc[count].cdsc_ind = 0;

		multi_index.cdmsf_min0 = disk_info.next_session.minute;
		multi_index.cdmsf_sec0 = disk_info.next_session.second;
		multi_index.cdmsf_frame0 = disk_info.next_session.frame;
		if (multi_index.cdmsf_sec0 >= 20)
			multi_index.cdmsf_sec0 -= 20;
		else {
			multi_index.cdmsf_sec0 += 40;
			multi_index.cdmsf_min0--;
		}
		DEBUG((DEBUG_MULTIS, "Try %d: %2d:%02d.%02d", sessions,
			multi_index.cdmsf_min0,
			multi_index.cdmsf_sec0,
			multi_index.cdmsf_frame0));
		bin2bcd(&multi_index);
		multi_index.cdmsf_min1 = 0;
		multi_index.cdmsf_sec1 = 0;
		multi_index.cdmsf_frame1 = 1;

		status = exec_read_cmd(COMREAD, &multi_index);
		if (status < 0) {
			DEBUG((DEBUG_TOC, "exec_read_cmd COMREAD: %02x",
				-status));
			break;
		}
		status = sleep_flag_low(FL_DTEN, MULTI_SEEK_TIMEOUT) ?
				0 : -ERR_TOC_MISSINGINFO;
		flush_data();
		if (status < 0) {
			DEBUG((DEBUG_TOC, "sleep_flag_low: %02x", -status));
			break;
		}

		status = read_toc();
		if (status < 0) {
			DEBUG((DEBUG_TOC, "read_toc: %02x", -status));
			break;
		}

		disk_info.multi = 1;
	}

	exec_cmd(COMSTOP);

	if (status < 0)
		return -EIO;
	return 0;
}
#endif /* MULTISESSION */


static int update_toc(void)
{
	int status, count;

	if (toc_uptodate)
		return 0;

	DEBUG((DEBUG_TOC, "starting update_toc"));

	disk_info.first = 0;
	for (count = 0; count < MAX_TRACKS; count++) 
		toc[count].cdsc_ind = 0;

	status = exec_cmd(COMLEADIN);
	if (status < 0)
		return -EIO;

	status = read_toc();
	if (status < 0) {
		DEBUG((DEBUG_TOC, "read_toc: %02x", -status));
		return -EIO;
	}

        /* Audio disk detection. Look at first track. */
	disk_info.audio =
		(toc[disk_info.first].cdsc_ctrl & CDROM_DATA_TRACK) ? 0 : 1;

	/* XA detection */
	disk_info.xa = drive_status() & ST_MODE2TRACK;

	/* Multisession detection: if we want this, define MULTISESSION */
	disk_info.multi = 0;
#ifdef MULTISESSION
 	if (disk_info.xa)
		get_multi_disk_info();	/* Here disk_info.multi is set */
#endif /* MULTISESSION */
	if (disk_info.multi)
		printk(KERN_WARNING "optcd: Multisession support experimental, "
			"see Documentation/cdrom/optcd\n");

	DEBUG((DEBUG_TOC, "exiting update_toc"));

	toc_uptodate = 1;
	return 0;
}

/* Request handling */

static int current_valid(void)
{
        return CURRENT &&
		CURRENT->cmd == READ &&
		CURRENT->sector != -1;
}

/* Buffers for block size conversion. */
#define NOBUF		-1

static char buf[CD_FRAMESIZE * N_BUFS];
static volatile int buf_bn[N_BUFS], next_bn;
static volatile int buf_in = 0, buf_out = NOBUF;

inline static void opt_invalidate_buffers(void)
{
	int i;

	DEBUG((DEBUG_BUFFERS, "executing opt_invalidate_buffers"));

	for (i = 0; i < N_BUFS; i++)
		buf_bn[i] = NOBUF;
	buf_out = NOBUF;
}


/* Take care of the different block sizes between cdrom and Linux.
   When Linux gets variable block sizes this will probably go away. */
static void transfer(void)
{
#if DEBUG_BUFFERS | DEBUG_REQUEST
	printk(KERN_DEBUG "optcd: executing transfer\n");
#endif

	if (!current_valid())
		return;
	while (CURRENT -> nr_sectors) {
		int bn = CURRENT -> sector / 4;
		int i, offs, nr_sectors;
		for (i = 0; i < N_BUFS && buf_bn[i] != bn; ++i);

		DEBUG((DEBUG_REQUEST, "found %d", i));

		if (i >= N_BUFS) {
			buf_out = NOBUF;
			break;
		}

		offs = (i * 4 + (CURRENT -> sector & 3)) * 512;
		nr_sectors = 4 - (CURRENT -> sector & 3);

		if (buf_out != i) {
			buf_out = i;
			if (buf_bn[i] != bn) {
				buf_out = NOBUF;
				continue;
			}
		}

		if (nr_sectors > CURRENT -> nr_sectors)
			nr_sectors = CURRENT -> nr_sectors;
		memcpy(CURRENT -> buffer, buf + offs, nr_sectors * 512);
		CURRENT -> nr_sectors -= nr_sectors;
		CURRENT -> sector += nr_sectors;
		CURRENT -> buffer += nr_sectors * 512;
	}
}


/* State machine for reading disk blocks */

enum state_e {
	S_IDLE,		/* 0 */
	S_START,	/* 1 */
	S_READ,		/* 2 */
	S_DATA,		/* 3 */
	S_STOP,		/* 4 */
	S_STOPPING	/* 5 */
};

static volatile enum state_e state = S_IDLE;
#if DEBUG_STATE
static volatile enum state_e state_old = S_STOP;
static volatile int flags_old = 0;
static volatile long state_n = 0;
#endif


/* Used as mutex to keep do_optcd_request (and other processes calling
   ioctl) out while some process is inside a VFS call.
   Reverse is accomplished by checking if state = S_IDLE upon entry
   of opt_ioctl and opt_media_change. */
static int in_vfs = 0;


static volatile int transfer_is_active = 0;
static volatile int error = 0;	/* %% do something with this?? */
static int tries;		/* ibid?? */
static int timeout = 0;

static void poll(unsigned long data);
static struct timer_list req_timer = {.function = poll};


static void poll(unsigned long data)
{
	static volatile int read_count = 1;
	int flags;
	int loop_again = 1;
	int status = 0;
	int skip = 0;

	if (error) {
		printk(KERN_ERR "optcd: I/O error 0x%02x\n", error);
		opt_invalidate_buffers();
		if (!tries--) {
			printk(KERN_ERR "optcd: read block %d failed;"
				" Giving up\n", next_bn);
			if (transfer_is_active)
				loop_again = 0;
			if (current_valid())
				end_request(CURRENT, 0);
			tries = 5;
		}
		error = 0;
		state = S_STOP;
	}

	while (loop_again)
	{
		loop_again = 0; /* each case must flip this back to 1 if we want
		                 to come back up here */

#if DEBUG_STATE
		if (state == state_old)
			state_n++;
		else {
			state_old = state;
			if (++state_n > 1)
				printk(KERN_DEBUG "optcd: %ld times "
					"in previous state\n", state_n);
			printk(KERN_DEBUG "optcd: state %d\n", state);
			state_n = 0;
		}
#endif

		switch (state) {
		case S_IDLE:
			return;
		case S_START:
			if (in_vfs)
				break;
			if (send_cmd(COMDRVST)) {
				state = S_IDLE;
				while (current_valid())
					end_request(CURRENT, 0);
				return;
			}
			state = S_READ;
			timeout = READ_TIMEOUT;
			break;
		case S_READ: {
			struct cdrom_msf msf;
			if (!skip) {
				status = fetch_status();
				if (status < 0)
					break;
				if (status & ST_DSK_CHG) {
					toc_uptodate = 0;
					opt_invalidate_buffers();
				}
			}
			skip = 0;
			if ((status & ST_DOOR_OPEN) || (status & ST_DRVERR)) {
				toc_uptodate = 0;
				opt_invalidate_buffers();
				printk(KERN_WARNING "optcd: %s\n",
					(status & ST_DOOR_OPEN)
					? "door open"
					: "disk removed");
				state = S_IDLE;
				while (current_valid())
					end_request(CURRENT, 0);
				return;
			}
			if (!current_valid()) {
				state = S_STOP;
				loop_again = 1;
				break;
			}
			next_bn = CURRENT -> sector / 4;
			lba2msf(next_bn, &msf);
			read_count = N_BUFS;
			msf.cdmsf_frame1 = read_count; /* Not BCD! */

			DEBUG((DEBUG_REQUEST, "reading %x:%x.%x %x:%x.%x",
				msf.cdmsf_min0,
				msf.cdmsf_sec0,
				msf.cdmsf_frame0,
				msf.cdmsf_min1,
				msf.cdmsf_sec1,
				msf.cdmsf_frame1));
			DEBUG((DEBUG_REQUEST, "next_bn:%d buf_in:%d"
				" buf_out:%d buf_bn:%d",
				next_bn,
				buf_in,
				buf_out,
				buf_bn[buf_in]));

			exec_read_cmd(COMREAD, &msf);
			state = S_DATA;
			timeout = READ_TIMEOUT;
			break;
		}
		case S_DATA:
			flags = stdt_flags() & (FL_STEN|FL_DTEN);

#if DEBUG_STATE
			if (flags != flags_old) {
				flags_old = flags;
				printk(KERN_DEBUG "optcd: flags:%x\n", flags);
			}
			if (flags == FL_STEN)
				printk(KERN_DEBUG "timeout cnt: %d\n", timeout);
#endif

			switch (flags) {
			case FL_DTEN:		/* only STEN low */
				if (!tries--) {
					printk(KERN_ERR
						"optcd: read block %d failed; "
						"Giving up\n", next_bn);
					if (transfer_is_active) {
						tries = 0;
						break;
					}
					if (current_valid())
						end_request(CURRENT, 0);
					tries = 5;
				}
				state = S_START;
				timeout = READ_TIMEOUT;
				loop_again = 1;
			case (FL_STEN|FL_DTEN):	 /* both high */
				break;
			default:	/* DTEN low */
				tries = 5;
				if (!current_valid() && buf_in == buf_out) {
					state = S_STOP;
					loop_again = 1;
					break;
				}
				if (read_count<=0)
					printk(KERN_WARNING
						"optcd: warning - try to read"
						" 0 frames\n");
				while (read_count) {
					buf_bn[buf_in] = NOBUF;
					if (!flag_low(FL_DTEN, BUSY_TIMEOUT)) {
					/* should be no waiting here!?? */
						printk(KERN_ERR
						   "read_count:%d "
						   "CURRENT->nr_sectors:%ld "
						   "buf_in:%d\n",
							read_count,
							CURRENT->nr_sectors,
							buf_in);
						printk(KERN_ERR
							"transfer active: %x\n",
							transfer_is_active);
						read_count = 0;
						state = S_STOP;
						loop_again = 1;
						end_request(CURRENT, 0);
						break;
					}
					fetch_data(buf+
					    CD_FRAMESIZE*buf_in,
					    CD_FRAMESIZE);
					read_count--;

					DEBUG((DEBUG_REQUEST,
						"S_DATA; ---I've read data- "
						"read_count: %d",
						read_count));
					DEBUG((DEBUG_REQUEST,
						"next_bn:%d  buf_in:%d "
						"buf_out:%d  buf_bn:%d",
						next_bn,
						buf_in,
						buf_out,
						buf_bn[buf_in]));

					buf_bn[buf_in] = next_bn++;
					if (buf_out == NOBUF)
						buf_out = buf_in;
					buf_in = buf_in + 1 ==
						N_BUFS ? 0 : buf_in + 1;
				}
				if (!transfer_is_active) {
					while (current_valid()) {
						transfer();
						if (CURRENT -> nr_sectors == 0)
							end_request(CURRENT, 1);
						else
							break;
					}
				}

				if (current_valid()
				    && (CURRENT -> sector / 4 < next_bn ||
				    CURRENT -> sector / 4 >
				     next_bn + N_BUFS)) {
					state = S_STOP;
					loop_again = 1;
					break;
				}
				timeout = READ_TIMEOUT;
				if (read_count == 0) {
					state = S_STOP;
					loop_again = 1;
					break;
				}
			}
			break;
		case S_STOP:
			if (read_count != 0)
				printk(KERN_ERR
					"optcd: discard data=%x frames\n",
					read_count);
			flush_data();
			if (send_cmd(COMDRVST)) {
				state = S_IDLE;
				while (current_valid())
					end_request(CURRENT, 0);
				return;
			}
			state = S_STOPPING;
			timeout = STOP_TIMEOUT;
			break;
		case S_STOPPING:
			status = fetch_status();
			if (status < 0 && timeout)
					break;
			if ((status >= 0) && (status & ST_DSK_CHG)) {
				toc_uptodate = 0;
				opt_invalidate_buffers();
			}
			if (current_valid()) {
				if (status >= 0) {
					state = S_READ;
					loop_again = 1;
					skip = 1;
					break;
				} else {
					state = S_START;
					timeout = 1;
				}
			} else {
				state = S_IDLE;
				return;
			}
			break;
		default:
			printk(KERN_ERR "optcd: invalid state %d\n", state);
			return;
		} /* case */
	} /* while */

	if (!timeout--) {
		printk(KERN_ERR "optcd: timeout in state %d\n", state);
		state = S_STOP;
		if (exec_cmd(COMSTOP) < 0) {
			state = S_IDLE;
			while (current_valid())
				end_request(CURRENT, 0);
			return;
		}
	}

	mod_timer(&req_timer, jiffies + HZ/100);
}


static void do_optcd_request(request_queue_t * q)
{
	DEBUG((DEBUG_REQUEST, "do_optcd_request(%ld+%ld)",
	       CURRENT -> sector, CURRENT -> nr_sectors));

	if (disk_info.audio) {
		printk(KERN_WARNING "optcd: tried to mount an Audio CD\n");
		end_request(CURRENT, 0);
		return;
	}

	transfer_is_active = 1;
	while (current_valid()) {
		transfer();	/* First try to transfer block from buffers */
		if (CURRENT -> nr_sectors == 0) {
			end_request(CURRENT, 1);
		} else {	/* Want to read a block not in buffer */
			buf_out = NOBUF;
			if (state == S_IDLE) {
				/* %% Should this block the request queue?? */
				if (update_toc() < 0) {
					while (current_valid())
						end_request(CURRENT, 0);
					break;
				}
				/* Start state machine */
				state = S_START;
				timeout = READ_TIMEOUT;
				tries = 5;
				/* %% why not start right away?? */
				mod_timer(&req_timer, jiffies + HZ/100);
			}
			break;
		}
	}
	transfer_is_active = 0;

	DEBUG((DEBUG_REQUEST, "next_bn:%d  buf_in:%d buf_out:%d  buf_bn:%d",
	       next_bn, buf_in, buf_out, buf_bn[buf_in]));
	DEBUG((DEBUG_REQUEST, "do_optcd_request ends"));
}

/* IOCTLs */


static char auto_eject = 0;

static int cdrompause(void)
{
	int status;

	if (audio_status != CDROM_AUDIO_PLAY)
		return -EINVAL;

	status = exec_cmd(COMPAUSEON);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_cmd COMPAUSEON: %02x", -status));
		return -EIO;
	}
	audio_status = CDROM_AUDIO_PAUSED;
	return 0;
}


static int cdromresume(void)
{
	int status;

	if (audio_status != CDROM_AUDIO_PAUSED)
		return -EINVAL;

	status = exec_cmd(COMPAUSEOFF);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_cmd COMPAUSEOFF: %02x", -status));
		audio_status = CDROM_AUDIO_ERROR;
		return -EIO;
	}
	audio_status = CDROM_AUDIO_PLAY;
	return 0;
}


static int cdromplaymsf(void __user *arg)
{
	int status;
	struct cdrom_msf msf;

	if (copy_from_user(&msf, arg, sizeof msf))
		return -EFAULT;

	bin2bcd(&msf);
	status = exec_long_cmd(COMPLAY, &msf);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_long_cmd COMPLAY: %02x", -status));
		audio_status = CDROM_AUDIO_ERROR;
		return -EIO;
	}

	audio_status = CDROM_AUDIO_PLAY;
	return 0;
}


static int cdromplaytrkind(void __user *arg)
{
	int status;
	struct cdrom_ti ti;
	struct cdrom_msf msf;

	if (copy_from_user(&ti, arg, sizeof ti))
		return -EFAULT;

	if (ti.cdti_trk0 < disk_info.first
	    || ti.cdti_trk0 > disk_info.last
	    || ti.cdti_trk1 < ti.cdti_trk0)
		return -EINVAL;
	if (ti.cdti_trk1 > disk_info.last)
		ti.cdti_trk1 = disk_info.last;

	msf.cdmsf_min0 = toc[ti.cdti_trk0].cdsc_absaddr.msf.minute;
	msf.cdmsf_sec0 = toc[ti.cdti_trk0].cdsc_absaddr.msf.second;
	msf.cdmsf_frame0 = toc[ti.cdti_trk0].cdsc_absaddr.msf.frame;
	msf.cdmsf_min1 = toc[ti.cdti_trk1 + 1].cdsc_absaddr.msf.minute;
	msf.cdmsf_sec1 = toc[ti.cdti_trk1 + 1].cdsc_absaddr.msf.second;
	msf.cdmsf_frame1 = toc[ti.cdti_trk1 + 1].cdsc_absaddr.msf.frame;

	DEBUG((DEBUG_VFS, "play %02d:%02d.%02d to %02d:%02d.%02d",
		msf.cdmsf_min0,
		msf.cdmsf_sec0,
		msf.cdmsf_frame0,
		msf.cdmsf_min1,
		msf.cdmsf_sec1,
		msf.cdmsf_frame1));

	bin2bcd(&msf);
	status = exec_long_cmd(COMPLAY, &msf);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_long_cmd COMPLAY: %02x", -status));
		audio_status = CDROM_AUDIO_ERROR;
		return -EIO;
	}

	audio_status = CDROM_AUDIO_PLAY;
	return 0;
}


static int cdromreadtochdr(void __user *arg)
{
	struct cdrom_tochdr tochdr;

	tochdr.cdth_trk0 = disk_info.first;
	tochdr.cdth_trk1 = disk_info.last;

	return copy_to_user(arg, &tochdr, sizeof tochdr) ? -EFAULT : 0;
}


static int cdromreadtocentry(void __user *arg)
{
	struct cdrom_tocentry entry;
	struct cdrom_subchnl *tocptr;

	if (copy_from_user(&entry, arg, sizeof entry))
		return -EFAULT;

	if (entry.cdte_track == CDROM_LEADOUT)
		tocptr = &toc[disk_info.last + 1];
	else if (entry.cdte_track > disk_info.last
		|| entry.cdte_track < disk_info.first)
		return -EINVAL;
	else
		tocptr = &toc[entry.cdte_track];

	entry.cdte_adr = tocptr->cdsc_adr;
	entry.cdte_ctrl = tocptr->cdsc_ctrl;
	entry.cdte_addr.msf.minute = tocptr->cdsc_absaddr.msf.minute;
	entry.cdte_addr.msf.second = tocptr->cdsc_absaddr.msf.second;
	entry.cdte_addr.msf.frame = tocptr->cdsc_absaddr.msf.frame;
	/* %% What should go into entry.cdte_datamode? */

	if (entry.cdte_format == CDROM_LBA)
		msf2lba(&entry.cdte_addr);
	else if (entry.cdte_format != CDROM_MSF)
		return -EINVAL;

	return copy_to_user(arg, &entry, sizeof entry) ? -EFAULT : 0;
}


static int cdromvolctrl(void __user *arg)
{
	int status;
	struct cdrom_volctrl volctrl;
	struct cdrom_msf msf;

	if (copy_from_user(&volctrl, arg, sizeof volctrl))
		return -EFAULT;

	msf.cdmsf_min0 = 0x10;
	msf.cdmsf_sec0 = 0x32;
	msf.cdmsf_frame0 = volctrl.channel0;
	msf.cdmsf_min1 = volctrl.channel1;
	msf.cdmsf_sec1 = volctrl.channel2;
	msf.cdmsf_frame1 = volctrl.channel3;

	status = exec_long_cmd(COMCHCTRL, &msf);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_long_cmd COMCHCTRL: %02x", -status));
		return -EIO;
	}
	return 0;
}


static int cdromsubchnl(void __user *arg)
{
	int status;
	struct cdrom_subchnl subchnl;

	if (copy_from_user(&subchnl, arg, sizeof subchnl))
		return -EFAULT;

	if (subchnl.cdsc_format != CDROM_LBA
	    && subchnl.cdsc_format != CDROM_MSF)
		return -EINVAL;

	status = get_q_channel(&subchnl);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "get_q_channel: %02x", -status));
		return -EIO;
	}

	if (copy_to_user(arg, &subchnl, sizeof subchnl))
		return -EFAULT;
	return 0;
}


static struct gendisk *optcd_disk;


static int cdromread(void __user *arg, int blocksize, int cmd)
{
	int status;
	struct cdrom_msf msf;

	if (copy_from_user(&msf, arg, sizeof msf))
		return -EFAULT;

	bin2bcd(&msf);
	msf.cdmsf_min1 = 0;
	msf.cdmsf_sec1 = 0;
	msf.cdmsf_frame1 = 1;	/* read only one frame */
	status = exec_read_cmd(cmd, &msf);

	DEBUG((DEBUG_VFS, "read cmd status 0x%x", status));

	if (!sleep_flag_low(FL_DTEN, SLEEP_TIMEOUT))
		return -EIO;

	fetch_data(optcd_disk->private_data, blocksize);

	if (copy_to_user(arg, optcd_disk->private_data, blocksize))
		return -EFAULT;

	return 0;
}


static int cdromseek(void __user *arg)
{
	int status;
	struct cdrom_msf msf;

	if (copy_from_user(&msf, arg, sizeof msf))
		return -EFAULT;

	bin2bcd(&msf);
	status = exec_seek_cmd(COMSEEK, &msf);

	DEBUG((DEBUG_VFS, "COMSEEK status 0x%x", status));

	if (status < 0)
		return -EIO;
	return 0;
}


#ifdef MULTISESSION
static int cdrommultisession(void __user *arg)
{
	struct cdrom_multisession ms;

	if (copy_from_user(&ms, arg, sizeof ms))
		return -EFAULT;

	ms.addr.msf.minute = disk_info.last_session.minute;
	ms.addr.msf.second = disk_info.last_session.second;
	ms.addr.msf.frame = disk_info.last_session.frame;

	if (ms.addr_format != CDROM_LBA
	   && ms.addr_format != CDROM_MSF)
		return -EINVAL;
	if (ms.addr_format == CDROM_LBA)
		msf2lba(&ms.addr);

	ms.xa_flag = disk_info.xa;

  	if (copy_to_user(arg, &ms, sizeof(struct cdrom_multisession)))
		return -EFAULT;

#if DEBUG_MULTIS
 	if (ms.addr_format == CDROM_MSF)
               	printk(KERN_DEBUG
			"optcd: multisession xa:%d, msf:%02d:%02d.%02d\n",
			ms.xa_flag,
			ms.addr.msf.minute,
			ms.addr.msf.second,
			ms.addr.msf.frame);
	else
		printk(KERN_DEBUG
		    "optcd: multisession %d, lba:0x%08x [%02d:%02d.%02d])\n",
			ms.xa_flag,
			ms.addr.lba,
			disk_info.last_session.minute,
			disk_info.last_session.second,
			disk_info.last_session.frame);
#endif /* DEBUG_MULTIS */

	return 0;
}
#endif /* MULTISESSION */


static int cdromreset(void)
{
	if (state != S_IDLE) {
		error = 1;
		tries = 0;
	}

	toc_uptodate = 0;
	disk_changed = 1;
	opt_invalidate_buffers();
	audio_status = CDROM_AUDIO_NO_STATUS;

	if (!reset_drive())
		return -EIO;
	return 0;
}

/* VFS calls */


static int opt_ioctl(struct inode *ip, struct file *fp,
                     unsigned int cmd, unsigned long arg)
{
	int status, err, retval = 0;
	void __user *argp = (void __user *)arg;

	DEBUG((DEBUG_VFS, "starting opt_ioctl"));

	if (!ip)
		return -EINVAL;

	if (cmd == CDROMRESET)
		return cdromreset();

	/* is do_optcd_request or another ioctl busy? */
	if (state != S_IDLE || in_vfs)
		return -EBUSY;

	in_vfs = 1;

	status = drive_status();
	if (status < 0) {
		DEBUG((DEBUG_VFS, "drive_status: %02x", -status));
		in_vfs = 0;
		return -EIO;
	}

	if (status & ST_DOOR_OPEN)
		switch (cmd) {	/* Actions that can be taken with door open */
		case CDROMCLOSETRAY:
			/* We do this before trying to read the toc. */
			err = exec_cmd(COMCLOSE);
			if (err < 0) {
				DEBUG((DEBUG_VFS,
				       "exec_cmd COMCLOSE: %02x", -err));
				in_vfs = 0;
				return -EIO;
			}
			break;
		default:	in_vfs = 0;
				return -EBUSY;
		}

	err = update_toc();
	if (err < 0) {
		DEBUG((DEBUG_VFS, "update_toc: %02x", -err));
		in_vfs = 0;
		return -EIO;
	}

	DEBUG((DEBUG_VFS, "ioctl cmd 0x%x", cmd));

	switch (cmd) {
	case CDROMPAUSE:	retval = cdrompause(); break;
	case CDROMRESUME:	retval = cdromresume(); break;
	case CDROMPLAYMSF:	retval = cdromplaymsf(argp); break;
	case CDROMPLAYTRKIND:	retval = cdromplaytrkind(argp); break;
	case CDROMREADTOCHDR:	retval = cdromreadtochdr(argp); break;
	case CDROMREADTOCENTRY:	retval = cdromreadtocentry(argp); break;

	case CDROMSTOP:		err = exec_cmd(COMSTOP);
				if (err < 0) {
					DEBUG((DEBUG_VFS,
						"exec_cmd COMSTOP: %02x",
						-err));
					retval = -EIO;
				} else
					audio_status = CDROM_AUDIO_NO_STATUS;
				break;
	case CDROMSTART:	break;	/* This is a no-op */
	case CDROMEJECT:	err = exec_cmd(COMUNLOCK);
				if (err < 0) {
					DEBUG((DEBUG_VFS,
						"exec_cmd COMUNLOCK: %02x",
						-err));
					retval = -EIO;
					break;
				}
				err = exec_cmd(COMOPEN);
				if (err < 0) {
					DEBUG((DEBUG_VFS,
						"exec_cmd COMOPEN: %02x",
						-err));
					retval = -EIO;
				}
				break;

	case CDROMVOLCTRL:	retval = cdromvolctrl(argp); break;
	case CDROMSUBCHNL:	retval = cdromsubchnl(argp); break;

	/* The drive detects the mode and automatically delivers the
	   correct 2048 bytes, so we don't need these IOCTLs */
	case CDROMREADMODE2:	retval = -EINVAL; break;
	case CDROMREADMODE1:	retval = -EINVAL; break;

	/* Drive doesn't support reading audio */
	case CDROMREADAUDIO:	retval = -EINVAL; break;

	case CDROMEJECT_SW:	auto_eject = (char) arg;
				break;

#ifdef MULTISESSION
	case CDROMMULTISESSION:	retval = cdrommultisession(argp); break;
#endif

	case CDROM_GET_MCN:	retval = -EINVAL; break; /* not implemented */
	case CDROMVOLREAD:	retval = -EINVAL; break; /* not implemented */

	case CDROMREADRAW:
			/* this drive delivers 2340 bytes in raw mode */
			retval = cdromread(argp, CD_FRAMESIZE_RAW1, COMREADRAW);
			break;
	case CDROMREADCOOKED:
			retval = cdromread(argp, CD_FRAMESIZE, COMREAD);
			break;
	case CDROMREADALL:
			retval = cdromread(argp, CD_FRAMESIZE_RAWER, COMREADALL);
			break;

	case CDROMSEEK:		retval = cdromseek(argp); break;
	case CDROMPLAYBLK:	retval = -EINVAL; break; /* not implemented */
	case CDROMCLOSETRAY:	break;	/* The action was taken earlier */
	default:		retval = -EINVAL;
	}
	in_vfs = 0;
	return retval;
}


static int open_count = 0;

/* Open device special file; check that a disk is in. */
static int opt_open(struct inode *ip, struct file *fp)
{
	DEBUG((DEBUG_VFS, "starting opt_open"));

	if (!open_count && state == S_IDLE) {
		int status;
		char *buf;

		buf = kmalloc(CD_FRAMESIZE_RAWER, GFP_KERNEL);
		if (!buf) {
			printk(KERN_INFO "optcd: cannot allocate read buffer\n");
			return -ENOMEM;
		}
		optcd_disk->private_data = buf;		/* save read buffer */

		toc_uptodate = 0;
		opt_invalidate_buffers();

		status = exec_cmd(COMCLOSE);	/* close door */
		if (status < 0) {
			DEBUG((DEBUG_VFS, "exec_cmd COMCLOSE: %02x", -status));
		}

		status = drive_status();
		if (status < 0) {
			DEBUG((DEBUG_VFS, "drive_status: %02x", -status));
			goto err_out;
		}
		DEBUG((DEBUG_VFS, "status: %02x", status));
		if ((status & ST_DOOR_OPEN) || (status & ST_DRVERR)) {
			printk(KERN_INFO "optcd: no disk or door open\n");
			goto err_out;
		}
		status = exec_cmd(COMLOCK);		/* Lock door */
		if (status < 0) {
			DEBUG((DEBUG_VFS, "exec_cmd COMLOCK: %02x", -status));
		}
		status = update_toc();	/* Read table of contents */
		if (status < 0)	{
			DEBUG((DEBUG_VFS, "update_toc: %02x", -status));
	 		status = exec_cmd(COMUNLOCK);	/* Unlock door */
			if (status < 0) {
				DEBUG((DEBUG_VFS,
				       "exec_cmd COMUNLOCK: %02x", -status));
			}
			goto err_out;
		}
		open_count++;
	}

	DEBUG((DEBUG_VFS, "exiting opt_open"));

	return 0;

err_out:
	return -EIO;
}


/* Release device special file; flush all blocks from the buffer cache */
static int opt_release(struct inode *ip, struct file *fp)
{
	int status;

	DEBUG((DEBUG_VFS, "executing opt_release"));
	DEBUG((DEBUG_VFS, "inode: %p, device: %s, file: %p\n",
		ip, ip->i_bdev->bd_disk->disk_name, fp));

	if (!--open_count) {
		toc_uptodate = 0;
		opt_invalidate_buffers();
	 	status = exec_cmd(COMUNLOCK);	/* Unlock door */
		if (status < 0) {
			DEBUG((DEBUG_VFS, "exec_cmd COMUNLOCK: %02x", -status));
		}
		if (auto_eject) {
			status = exec_cmd(COMOPEN);
			DEBUG((DEBUG_VFS, "exec_cmd COMOPEN: %02x", -status));
		}
		kfree(optcd_disk->private_data);
		del_timer(&delay_timer);
		del_timer(&req_timer);
	}
	return 0;
}


/* Check if disk has been changed */
static int opt_media_change(struct gendisk *disk)
{
	DEBUG((DEBUG_VFS, "executing opt_media_change"));
	DEBUG((DEBUG_VFS, "dev: %s; disk_changed = %d\n",
			disk->disk_name, disk_changed));

	if (disk_changed) {
		disk_changed = 0;
		return 1;
	}
	return 0;
}

/* Driver initialisation */


/* Returns 1 if a drive is detected with a version string
   starting with "DOLPHIN". Otherwise 0. */
static int __init version_ok(void)
{
	char devname[100];
	int count, i, ch, status;

	status = exec_cmd(COMVERSION);
	if (status < 0) {
		DEBUG((DEBUG_VFS, "exec_cmd COMVERSION: %02x", -status));
		return 0;
	}
	if ((count = get_data(1)) < 0) {
		DEBUG((DEBUG_VFS, "get_data(1): %02x", -count));
		return 0;
	}
	for (i = 0, ch = -1; count > 0; count--) {
		if ((ch = get_data(1)) < 0) {
			DEBUG((DEBUG_VFS, "get_data(1): %02x", -ch));
			break;
		}
		if (i < 99)
			devname[i++] = ch;
	}
	devname[i] = '\0';
	if (ch < 0)
		return 0;

	printk(KERN_INFO "optcd: Device %s detected\n", devname);
	return ((devname[0] == 'D')
	     && (devname[1] == 'O')
	     && (devname[2] == 'L')
	     && (devname[3] == 'P')
	     && (devname[4] == 'H')
	     && (devname[5] == 'I')
	     && (devname[6] == 'N'));
}


static struct block_device_operations opt_fops = {
	.owner		= THIS_MODULE,
	.open		= opt_open,
	.release	= opt_release,
	.ioctl		= opt_ioctl,
	.media_changed	= opt_media_change,
};

#ifndef MODULE
/* Get kernel parameter when used as a kernel driver */
static int optcd_setup(char *str)
{
	int ints[4];
	(void)get_options(str, ARRAY_SIZE(ints), ints);
	
	if (ints[0] > 0)
		optcd_port = ints[1];

 	return 1;
}

__setup("optcd=", optcd_setup);

#endif /* MODULE */

/* Test for presence of drive and initialize it. Called at boot time
   or during module initialisation. */
static int __init optcd_init(void)
{
	int status;

	if (optcd_port <= 0) {
		printk(KERN_INFO
			"optcd: no Optics Storage CDROM Initialization\n");
		return -EIO;
	}
	optcd_disk = alloc_disk(1);
	if (!optcd_disk) {
		printk(KERN_ERR "optcd: can't allocate disk\n");
		return -ENOMEM;
	}
	optcd_disk->major = MAJOR_NR;
	optcd_disk->first_minor = 0;
	optcd_disk->fops = &opt_fops;
	sprintf(optcd_disk->disk_name, "optcd");
	sprintf(optcd_disk->devfs_name, "optcd");

	if (!request_region(optcd_port, 4, "optcd")) {
		printk(KERN_ERR "optcd: conflict, I/O port 0x%x already used\n",
			optcd_port);
		put_disk(optcd_disk);
		return -EIO;
	}

	if (!reset_drive()) {
		printk(KERN_ERR "optcd: drive at 0x%x not ready\n", optcd_port);
		release_region(optcd_port, 4);
		put_disk(optcd_disk);
		return -EIO;
	}
	if (!version_ok()) {
		printk(KERN_ERR "optcd: unknown drive detected; aborting\n");
		release_region(optcd_port, 4);
		put_disk(optcd_disk);
		return -EIO;
	}
	status = exec_cmd(COMINITDOUBLE);
	if (status < 0) {
		printk(KERN_ERR "optcd: cannot init double speed mode\n");
		release_region(optcd_port, 4);
		DEBUG((DEBUG_VFS, "exec_cmd COMINITDOUBLE: %02x", -status));
		put_disk(optcd_disk);
		return -EIO;
	}
	if (register_blkdev(MAJOR_NR, "optcd")) {
		release_region(optcd_port, 4);
		put_disk(optcd_disk);
		return -EIO;
	}


	opt_queue = blk_init_queue(do_optcd_request, &optcd_lock);
	if (!opt_queue) {
		unregister_blkdev(MAJOR_NR, "optcd");
		release_region(optcd_port, 4);
		put_disk(optcd_disk);
		return -ENOMEM;
	}

	blk_queue_hardsect_size(opt_queue, 2048);
	optcd_disk->queue = opt_queue;
	add_disk(optcd_disk);

	printk(KERN_INFO "optcd: DOLPHIN 8000 AT CDROM at 0x%x\n", optcd_port);
	return 0;
}


static void __exit optcd_exit(void)
{
	del_gendisk(optcd_disk);
	put_disk(optcd_disk);
	if (unregister_blkdev(MAJOR_NR, "optcd") == -EINVAL) {
		printk(KERN_ERR "optcd: what's that: can't unregister\n");
		return;
	}
	blk_cleanup_queue(opt_queue);
	release_region(optcd_port, 4);
	printk(KERN_INFO "optcd: module released.\n");
}

module_init(optcd_init);
module_exit(optcd_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(OPTICS_CDROM_MAJOR);

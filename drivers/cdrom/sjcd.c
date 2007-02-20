/* -- sjcd.c
 *
 *   Sanyo CD-ROM device driver implementation, Version 1.6
 *   Copyright (C) 1995  Vadim V. Model
 *
 *   model@cecmow.enet.dec.com
 *   vadim@rbrf.ru
 *   vadim@ipsun.ras.ru
 *
 *
 *  This driver is based on pre-works by Eberhard Moenkeberg (emoenke@gwdg.de);
 *  it was developed under use of mcd.c from Martin Harriss, with help of
 *  Eric van der Maarel (H.T.M.v.d.Maarel@marin.nl).
 *
 *  It is planned to include these routines into sbpcd.c later - to make
 *  a "mixed use" on one cable possible for all kinds of drives which use
 *  the SoundBlaster/Panasonic style CDROM interface. But today, the
 *  ability to install directly from CDROM is more important than flexibility.
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
 *  History:
 *  1.1 First public release with kernel version 1.3.7.
 *      Written by Vadim Model.
 *  1.2 Added detection and configuration of cdrom interface
 *      on ISP16 soundcard.
 *      Allow for command line options: sjcd=<io_base>,<irq>,<dma>
 *  1.3 Some minor changes to README.sjcd.
 *  1.4 MSS Sound support!! Listen to a CD through the speakers.
 *  1.5 Module support and bugfixes.
 *      Tray locking.
 *  1.6 Removed ISP16 code from this driver.
 *      Allow only to set io base address on command line: sjcd=<io_base>
 *      Changes to Documentation/cdrom/sjcd
 *      Added cleanup after any error in the initialisation.
 *  1.7 Added code to set the sector size tables to prevent the bug present in 
 *      the previous version of this driver.  Coded added by Anthony Barbachan 
 *      from bugfix tip originally suggested by Alan Cox.
 *
 *  November 1999 -- Make kernel-parameter implementation work with 2.3.x 
 *	             Removed init_module & cleanup_module in favor of 
 *	             module_init & module_exit.
 *	             Torben Mathiasen <tmm@image.dk>
 */

#define SJCD_VERSION_MAJOR 1
#define SJCD_VERSION_MINOR 7

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/blkdev.h>
#include "sjcd.h"

static int sjcd_present = 0;
static struct request_queue *sjcd_queue;

#define MAJOR_NR SANYO_CDROM_MAJOR
#define QUEUE (sjcd_queue)
#define CURRENT elv_next_request(sjcd_queue)

#define SJCD_BUF_SIZ 32		/* cdr-h94a has internal 64K buffer */

/*
 * buffer for block size conversion
 */
static char sjcd_buf[2048 * SJCD_BUF_SIZ];
static volatile int sjcd_buf_bn[SJCD_BUF_SIZ], sjcd_next_bn;
static volatile int sjcd_buf_in, sjcd_buf_out = -1;

/*
 * Status.
 */
static unsigned short sjcd_status_valid = 0;
static unsigned short sjcd_door_closed;
static unsigned short sjcd_door_was_open;
static unsigned short sjcd_media_is_available;
static unsigned short sjcd_media_is_changed;
static unsigned short sjcd_toc_uptodate = 0;
static unsigned short sjcd_command_failed;
static volatile unsigned char sjcd_completion_status = 0;
static volatile unsigned char sjcd_completion_error = 0;
static unsigned short sjcd_command_is_in_progress = 0;
static unsigned short sjcd_error_reported = 0;
static DEFINE_SPINLOCK(sjcd_lock);

static int sjcd_open_count;

static int sjcd_audio_status;
static struct sjcd_play_msf sjcd_playing;

static int sjcd_base = SJCD_BASE_ADDR;

module_param(sjcd_base, int, 0);

static DECLARE_WAIT_QUEUE_HEAD(sjcd_waitq);

/*
 * Data transfer.
 */
static volatile unsigned short sjcd_transfer_is_active = 0;

enum sjcd_transfer_state {
	SJCD_S_IDLE = 0,
	SJCD_S_START = 1,
	SJCD_S_MODE = 2,
	SJCD_S_READ = 3,
	SJCD_S_DATA = 4,
	SJCD_S_STOP = 5,
	SJCD_S_STOPPING = 6
};
static enum sjcd_transfer_state sjcd_transfer_state = SJCD_S_IDLE;
static long sjcd_transfer_timeout = 0;
static int sjcd_read_count = 0;
static unsigned char sjcd_mode = 0;

#define SJCD_READ_TIMEOUT 5000

#if defined( SJCD_GATHER_STAT )
/*
 * Statistic.
 */
static struct sjcd_stat statistic;
#endif

/*
 * Timer.
 */
static DEFINE_TIMER(sjcd_delay_timer, NULL, 0, 0);

#define SJCD_SET_TIMER( func, tmout )           \
    ( sjcd_delay_timer.expires = jiffies+tmout,         \
      sjcd_delay_timer.function = ( void * )func, \
      add_timer( &sjcd_delay_timer ) )

#define CLEAR_TIMER del_timer( &sjcd_delay_timer )

/*
 * Set up device, i.e., use command line data to set
 * base address.
 */
#ifndef MODULE
static int __init sjcd_setup(char *str)
{
	int ints[2];
	(void) get_options(str, ARRAY_SIZE(ints), ints);
	if (ints[0] > 0)
		sjcd_base = ints[1];

	return 1;
}

__setup("sjcd=", sjcd_setup);

#endif

/*
 * Special converters.
 */
static unsigned char bin2bcd(int bin)
{
	int u, v;

	u = bin % 10;
	v = bin / 10;
	return (u | (v << 4));
}

static int bcd2bin(unsigned char bcd)
{
	return ((bcd >> 4) * 10 + (bcd & 0x0F));
}

static long msf2hsg(struct msf *mp)
{
	return (bcd2bin(mp->frame) + bcd2bin(mp->sec) * 75
		+ bcd2bin(mp->min) * 4500 - 150);
}

static void hsg2msf(long hsg, struct msf *msf)
{
	hsg += 150;
	msf->min = hsg / 4500;
	hsg %= 4500;
	msf->sec = hsg / 75;
	msf->frame = hsg % 75;
	msf->min = bin2bcd(msf->min);	/* convert to BCD */
	msf->sec = bin2bcd(msf->sec);
	msf->frame = bin2bcd(msf->frame);
}

/*
 * Send a command to cdrom. Invalidate status.
 */
static void sjcd_send_cmd(unsigned char cmd)
{
#if defined( SJCD_TRACE )
	printk("SJCD: send_cmd( 0x%x )\n", cmd);
#endif
	outb(cmd, SJCDPORT(0));
	sjcd_command_is_in_progress = 1;
	sjcd_status_valid = 0;
	sjcd_command_failed = 0;
}

/*
 * Send a command with one arg to cdrom. Invalidate status.
 */
static void sjcd_send_1_cmd(unsigned char cmd, unsigned char a)
{
#if defined( SJCD_TRACE )
	printk("SJCD: send_1_cmd( 0x%x, 0x%x )\n", cmd, a);
#endif
	outb(cmd, SJCDPORT(0));
	outb(a, SJCDPORT(0));
	sjcd_command_is_in_progress = 1;
	sjcd_status_valid = 0;
	sjcd_command_failed = 0;
}

/*
 * Send a command with four args to cdrom. Invalidate status.
 */
static void sjcd_send_4_cmd(unsigned char cmd, unsigned char a,
			    unsigned char b, unsigned char c,
			    unsigned char d)
{
#if defined( SJCD_TRACE )
	printk("SJCD: send_4_cmd( 0x%x )\n", cmd);
#endif
	outb(cmd, SJCDPORT(0));
	outb(a, SJCDPORT(0));
	outb(b, SJCDPORT(0));
	outb(c, SJCDPORT(0));
	outb(d, SJCDPORT(0));
	sjcd_command_is_in_progress = 1;
	sjcd_status_valid = 0;
	sjcd_command_failed = 0;
}

/*
 * Send a play or read command to cdrom. Invalidate Status.
 */
static void sjcd_send_6_cmd(unsigned char cmd, struct sjcd_play_msf *pms)
{
#if defined( SJCD_TRACE )
	printk("SJCD: send_long_cmd( 0x%x )\n", cmd);
#endif
	outb(cmd, SJCDPORT(0));
	outb(pms->start.min, SJCDPORT(0));
	outb(pms->start.sec, SJCDPORT(0));
	outb(pms->start.frame, SJCDPORT(0));
	outb(pms->end.min, SJCDPORT(0));
	outb(pms->end.sec, SJCDPORT(0));
	outb(pms->end.frame, SJCDPORT(0));
	sjcd_command_is_in_progress = 1;
	sjcd_status_valid = 0;
	sjcd_command_failed = 0;
}

/*
 * Get a value from the data port. Should not block, so we use a little
 * wait for a while. Returns 0 if OK.
 */
static int sjcd_load_response(void *buf, int len)
{
	unsigned char *resp = (unsigned char *) buf;

	for (; len; --len) {
		int i;
		for (i = 200;
		     i-- && !SJCD_STATUS_AVAILABLE(inb(SJCDPORT(1))););
		if (i > 0)
			*resp++ = (unsigned char) inb(SJCDPORT(0));
		else
			break;
	}
	return (len);
}

/*
 * Load and parse command completion status (drive info byte and maybe error).
 * Sorry, no error classification yet.
 */
static void sjcd_load_status(void)
{
	sjcd_media_is_changed = 0;
	sjcd_completion_error = 0;
	sjcd_completion_status = inb(SJCDPORT(0));
	if (sjcd_completion_status & SST_DOOR_OPENED) {
		sjcd_door_closed = sjcd_media_is_available = 0;
	} else {
		sjcd_door_closed = 1;
		if (sjcd_completion_status & SST_MEDIA_CHANGED)
			sjcd_media_is_available = sjcd_media_is_changed =
			    1;
		else if (sjcd_completion_status & 0x0F) {
			/*
			 * OK, we seem to catch an error ...
			 */
			while (!SJCD_STATUS_AVAILABLE(inb(SJCDPORT(1))));
			sjcd_completion_error = inb(SJCDPORT(0));
			if ((sjcd_completion_status & 0x08) &&
			    (sjcd_completion_error & 0x40))
				sjcd_media_is_available = 0;
			else
				sjcd_command_failed = 1;
		} else
			sjcd_media_is_available = 1;
	}
	/*
	 * Ok, status loaded successfully.
	 */
	sjcd_status_valid = 1, sjcd_error_reported = 0;
	sjcd_command_is_in_progress = 0;

	/*
	 * If the disk is changed, the TOC is not valid.
	 */
	if (sjcd_media_is_changed)
		sjcd_toc_uptodate = 0;
#if defined( SJCD_TRACE )
	printk("SJCD: status %02x.%02x loaded.\n",
	       (int) sjcd_completion_status, (int) sjcd_completion_error);
#endif
}

/*
 * Read status from cdrom. Check to see if the status is available.
 */
static int sjcd_check_status(void)
{
	/*
	 * Try to load the response from cdrom into buffer.
	 */
	if (SJCD_STATUS_AVAILABLE(inb(SJCDPORT(1)))) {
		sjcd_load_status();
		return (1);
	} else {
		/*
		 * No status is available.
		 */
		return (0);
	}
}

/*
 * This is just timeout counter, and nothing more. Surprised ? :-)
 */
static volatile long sjcd_status_timeout;

/*
 * We need about 10 seconds to wait. The longest command takes about 5 seconds
 * to probe the disk (usually after tray closed or drive reset). Other values
 * should be thought of for other commands.
 */
#define SJCD_WAIT_FOR_STATUS_TIMEOUT 1000

static void sjcd_status_timer(void)
{
	if (sjcd_check_status()) {
		/*
		 * The command completed and status is loaded, stop waiting.
		 */
		wake_up(&sjcd_waitq);
	} else if (--sjcd_status_timeout <= 0) {
		/*
		 * We are timed out. 
		 */
		wake_up(&sjcd_waitq);
	} else {
		/*
		 * We have still some time to wait. Try again.
		 */
		SJCD_SET_TIMER(sjcd_status_timer, 1);
	}
}

/*
 * Wait for status for 10 sec approx. Returns non-positive when timed out.
 * Should not be used while reading data CDs.
 */
static int sjcd_wait_for_status(void)
{
	sjcd_status_timeout = SJCD_WAIT_FOR_STATUS_TIMEOUT;
	SJCD_SET_TIMER(sjcd_status_timer, 1);
	sleep_on(&sjcd_waitq);
#if defined( SJCD_DIAGNOSTIC ) || defined ( SJCD_TRACE )
	if (sjcd_status_timeout <= 0)
		printk("SJCD: Error Wait For Status.\n");
#endif
	return (sjcd_status_timeout);
}

static int sjcd_receive_status(void)
{
	int i;
#if defined( SJCD_TRACE )
	printk("SJCD: receive_status\n");
#endif
	/*
	 * Wait a bit for status available.
	 */
	for (i = 200; i-- && (sjcd_check_status() == 0););
	if (i < 0) {
#if defined( SJCD_TRACE )
		printk("SJCD: long wait for status\n");
#endif
		if (sjcd_wait_for_status() <= 0)
			printk("SJCD: Timeout when read status.\n");
		else
			i = 0;
	}
	return (i);
}

/*
 * Load the status. Issue get status command and wait for status available.
 */
static void sjcd_get_status(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: get_status\n");
#endif
	sjcd_send_cmd(SCMD_GET_STATUS);
	sjcd_receive_status();
}

/*
 * Check the drive if the disk is changed. Should be revised.
 */
static int sjcd_disk_change(struct gendisk *disk)
{
#if 0
	printk("SJCD: sjcd_disk_change(%s)\n", disk->disk_name);
#endif
	if (!sjcd_command_is_in_progress)
		sjcd_get_status();
	return (sjcd_status_valid ? sjcd_media_is_changed : 0);
}

/*
 * Read the table of contents (TOC) and TOC header if necessary.
 * We assume that the drive contains no more than 99 toc entries.
 */
static struct sjcd_hw_disk_info sjcd_table_of_contents[SJCD_MAX_TRACKS];
static unsigned char sjcd_first_track_no, sjcd_last_track_no;
#define sjcd_disk_length  sjcd_table_of_contents[0].un.track_msf

static int sjcd_update_toc(void)
{
	struct sjcd_hw_disk_info info;
	int i;
#if defined( SJCD_TRACE )
	printk("SJCD: update toc:\n");
#endif
	/*
	 * check to see if we need to do anything
	 */
	if (sjcd_toc_uptodate)
		return (0);

	/*
	 * Get the TOC start information.
	 */
	sjcd_send_1_cmd(SCMD_GET_DISK_INFO, SCMD_GET_1_TRACK);
	sjcd_receive_status();

	if (!sjcd_status_valid) {
		printk("SJCD: cannot load status.\n");
		return (-1);
	}

	if (!sjcd_media_is_available) {
		printk("SJCD: no disk in drive\n");
		return (-1);
	}

	if (!sjcd_command_failed) {
		if (sjcd_load_response(&info, sizeof(info)) != 0) {
			printk
			    ("SJCD: cannot load response about TOC start.\n");
			return (-1);
		}
		sjcd_first_track_no = bcd2bin(info.un.track_no);
	} else {
		printk("SJCD: get first failed\n");
		return (-1);
	}
#if defined( SJCD_TRACE )
	printk("SJCD: TOC start 0x%02x ", sjcd_first_track_no);
#endif
	/*
	 * Get the TOC finish information.
	 */
	sjcd_send_1_cmd(SCMD_GET_DISK_INFO, SCMD_GET_L_TRACK);
	sjcd_receive_status();

	if (!sjcd_status_valid) {
		printk("SJCD: cannot load status.\n");
		return (-1);
	}

	if (!sjcd_media_is_available) {
		printk("SJCD: no disk in drive\n");
		return (-1);
	}

	if (!sjcd_command_failed) {
		if (sjcd_load_response(&info, sizeof(info)) != 0) {
			printk
			    ("SJCD: cannot load response about TOC finish.\n");
			return (-1);
		}
		sjcd_last_track_no = bcd2bin(info.un.track_no);
	} else {
		printk("SJCD: get last failed\n");
		return (-1);
	}
#if defined( SJCD_TRACE )
	printk("SJCD: TOC finish 0x%02x ", sjcd_last_track_no);
#endif
	for (i = sjcd_first_track_no; i <= sjcd_last_track_no; i++) {
		/*
		 * Get the first track information.
		 */
		sjcd_send_1_cmd(SCMD_GET_DISK_INFO, bin2bcd(i));
		sjcd_receive_status();

		if (!sjcd_status_valid) {
			printk("SJCD: cannot load status.\n");
			return (-1);
		}

		if (!sjcd_media_is_available) {
			printk("SJCD: no disk in drive\n");
			return (-1);
		}

		if (!sjcd_command_failed) {
			if (sjcd_load_response(&sjcd_table_of_contents[i],
					       sizeof(struct
						      sjcd_hw_disk_info))
			    != 0) {
				printk
				    ("SJCD: cannot load info for %d track\n",
				     i);
				return (-1);
			}
		} else {
			printk("SJCD: get info %d failed\n", i);
			return (-1);
		}
	}

	/*
	 * Get the disk length info.
	 */
	sjcd_send_1_cmd(SCMD_GET_DISK_INFO, SCMD_GET_D_SIZE);
	sjcd_receive_status();

	if (!sjcd_status_valid) {
		printk("SJCD: cannot load status.\n");
		return (-1);
	}

	if (!sjcd_media_is_available) {
		printk("SJCD: no disk in drive\n");
		return (-1);
	}

	if (!sjcd_command_failed) {
		if (sjcd_load_response(&info, sizeof(info)) != 0) {
			printk
			    ("SJCD: cannot load response about disk size.\n");
			return (-1);
		}
		sjcd_disk_length.min = info.un.track_msf.min;
		sjcd_disk_length.sec = info.un.track_msf.sec;
		sjcd_disk_length.frame = info.un.track_msf.frame;
	} else {
		printk("SJCD: get size failed\n");
		return (1);
	}
#if defined( SJCD_TRACE )
	printk("SJCD: (%02x:%02x.%02x)\n", sjcd_disk_length.min,
	       sjcd_disk_length.sec, sjcd_disk_length.frame);
#endif
	return (0);
}

/*
 * Load subchannel information.
 */
static int sjcd_get_q_info(struct sjcd_hw_qinfo *qp)
{
	int s;
#if defined( SJCD_TRACE )
	printk("SJCD: load sub q\n");
#endif
	sjcd_send_cmd(SCMD_GET_QINFO);
	s = sjcd_receive_status();
	if (s < 0 || sjcd_command_failed || !sjcd_status_valid) {
		sjcd_send_cmd(0xF2);
		s = sjcd_receive_status();
		if (s < 0 || sjcd_command_failed || !sjcd_status_valid)
			return (-1);
		sjcd_send_cmd(SCMD_GET_QINFO);
		s = sjcd_receive_status();
		if (s < 0 || sjcd_command_failed || !sjcd_status_valid)
			return (-1);
	}
	if (sjcd_media_is_available)
		if (sjcd_load_response(qp, sizeof(*qp)) == 0)
			return (0);
	return (-1);
}

/*
 * Start playing from the specified position.
 */
static int sjcd_play(struct sjcd_play_msf *mp)
{
	struct sjcd_play_msf msf;

	/*
	 * Turn the device to play mode.
	 */
	sjcd_send_1_cmd(SCMD_SET_MODE, SCMD_MODE_PLAY);
	if (sjcd_receive_status() < 0)
		return (-1);

	/*
	 * Seek to the starting point.
	 */
	msf.start = mp->start;
	msf.end.min = msf.end.sec = msf.end.frame = 0x00;
	sjcd_send_6_cmd(SCMD_SEEK, &msf);
	if (sjcd_receive_status() < 0)
		return (-1);

	/*
	 * Start playing.
	 */
	sjcd_send_6_cmd(SCMD_PLAY, mp);
	return (sjcd_receive_status());
}

/*
 * Tray control functions.
 */
static int sjcd_tray_close(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: tray_close\n");
#endif
	sjcd_send_cmd(SCMD_CLOSE_TRAY);
	return (sjcd_receive_status());
}

static int sjcd_tray_lock(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: tray_lock\n");
#endif
	sjcd_send_cmd(SCMD_LOCK_TRAY);
	return (sjcd_receive_status());
}

static int sjcd_tray_unlock(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: tray_unlock\n");
#endif
	sjcd_send_cmd(SCMD_UNLOCK_TRAY);
	return (sjcd_receive_status());
}

static int sjcd_tray_open(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: tray_open\n");
#endif
	sjcd_send_cmd(SCMD_EJECT_TRAY);
	return (sjcd_receive_status());
}

/*
 * Do some user commands.
 */
static int sjcd_ioctl(struct inode *ip, struct file *fp,
		      unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
#if defined( SJCD_TRACE )
	printk("SJCD:ioctl\n");
#endif

	sjcd_get_status();
	if (!sjcd_status_valid)
		return (-EIO);
	if (sjcd_update_toc() < 0)
		return (-EIO);

	switch (cmd) {
	case CDROMSTART:{
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: start\n");
#endif
			return (0);
		}

	case CDROMSTOP:{
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: stop\n");
#endif
			sjcd_send_cmd(SCMD_PAUSE);
			(void) sjcd_receive_status();
			sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
			return (0);
		}

	case CDROMPAUSE:{
			struct sjcd_hw_qinfo q_info;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: pause\n");
#endif
			if (sjcd_audio_status == CDROM_AUDIO_PLAY) {
				sjcd_send_cmd(SCMD_PAUSE);
				(void) sjcd_receive_status();
				if (sjcd_get_q_info(&q_info) < 0) {
					sjcd_audio_status =
					    CDROM_AUDIO_NO_STATUS;
				} else {
					sjcd_audio_status =
					    CDROM_AUDIO_PAUSED;
					sjcd_playing.start = q_info.abs;
				}
				return (0);
			} else
				return (-EINVAL);
		}

	case CDROMRESUME:{
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: resume\n");
#endif
			if (sjcd_audio_status == CDROM_AUDIO_PAUSED) {
				/*
				 * continue play starting at saved location
				 */
				if (sjcd_play(&sjcd_playing) < 0) {
					sjcd_audio_status =
					    CDROM_AUDIO_ERROR;
					return (-EIO);
				} else {
					sjcd_audio_status =
					    CDROM_AUDIO_PLAY;
					return (0);
				}
			} else
				return (-EINVAL);
		}

	case CDROMPLAYTRKIND:{
			struct cdrom_ti ti;
			int s = -EFAULT;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: playtrkind\n");
#endif
			if (!copy_from_user(&ti, argp, sizeof(ti))) {
				s = 0;
				if (ti.cdti_trk0 < sjcd_first_track_no)
					return (-EINVAL);
				if (ti.cdti_trk1 > sjcd_last_track_no)
					ti.cdti_trk1 = sjcd_last_track_no;
				if (ti.cdti_trk0 > ti.cdti_trk1)
					return (-EINVAL);

				sjcd_playing.start =
				    sjcd_table_of_contents[ti.cdti_trk0].
				    un.track_msf;
				sjcd_playing.end =
				    (ti.cdti_trk1 <
				     sjcd_last_track_no) ?
				    sjcd_table_of_contents[ti.cdti_trk1 +
							   1].un.
				    track_msf : sjcd_table_of_contents[0].
				    un.track_msf;

				if (sjcd_play(&sjcd_playing) < 0) {
					sjcd_audio_status =
					    CDROM_AUDIO_ERROR;
					return (-EIO);
				} else
					sjcd_audio_status =
					    CDROM_AUDIO_PLAY;
			}
			return (s);
		}

	case CDROMPLAYMSF:{
			struct cdrom_msf sjcd_msf;
			int s;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: playmsf\n");
#endif
			if ((s =
			     access_ok(VERIFY_READ, argp, sizeof(sjcd_msf))
			     		? 0 : -EFAULT) == 0) {
				if (sjcd_audio_status == CDROM_AUDIO_PLAY) {
					sjcd_send_cmd(SCMD_PAUSE);
					(void) sjcd_receive_status();
					sjcd_audio_status =
					    CDROM_AUDIO_NO_STATUS;
				}

				if (copy_from_user(&sjcd_msf, argp,
					       sizeof(sjcd_msf)))
					return (-EFAULT);

				sjcd_playing.start.min =
				    bin2bcd(sjcd_msf.cdmsf_min0);
				sjcd_playing.start.sec =
				    bin2bcd(sjcd_msf.cdmsf_sec0);
				sjcd_playing.start.frame =
				    bin2bcd(sjcd_msf.cdmsf_frame0);
				sjcd_playing.end.min =
				    bin2bcd(sjcd_msf.cdmsf_min1);
				sjcd_playing.end.sec =
				    bin2bcd(sjcd_msf.cdmsf_sec1);
				sjcd_playing.end.frame =
				    bin2bcd(sjcd_msf.cdmsf_frame1);

				if (sjcd_play(&sjcd_playing) < 0) {
					sjcd_audio_status =
					    CDROM_AUDIO_ERROR;
					return (-EIO);
				} else
					sjcd_audio_status =
					    CDROM_AUDIO_PLAY;
			}
			return (s);
		}

	case CDROMREADTOCHDR:{
			struct cdrom_tochdr toc_header;
#if defined (SJCD_TRACE )
			printk("SJCD: ioctl: readtocheader\n");
#endif
			toc_header.cdth_trk0 = sjcd_first_track_no;
			toc_header.cdth_trk1 = sjcd_last_track_no;
			if (copy_to_user(argp, &toc_header,
					 sizeof(toc_header)))
				return -EFAULT;
			return 0;
		}

	case CDROMREADTOCENTRY:{
			struct cdrom_tocentry toc_entry;
			int s;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: readtocentry\n");
#endif
			if ((s =
			     access_ok(VERIFY_WRITE, argp, sizeof(toc_entry))
			     		? 0 : -EFAULT) == 0) {
				struct sjcd_hw_disk_info *tp;

				if (copy_from_user(&toc_entry, argp,
					       sizeof(toc_entry)))
					return (-EFAULT);
				if (toc_entry.cdte_track == CDROM_LEADOUT)
					tp = &sjcd_table_of_contents[0];
				else if (toc_entry.cdte_track <
					 sjcd_first_track_no)
					return (-EINVAL);
				else if (toc_entry.cdte_track >
					 sjcd_last_track_no)
					return (-EINVAL);
				else
					tp = &sjcd_table_of_contents
					    [toc_entry.cdte_track];

				toc_entry.cdte_adr =
				    tp->track_control & 0x0F;
				toc_entry.cdte_ctrl =
				    tp->track_control >> 4;

				switch (toc_entry.cdte_format) {
				case CDROM_LBA:
					toc_entry.cdte_addr.lba =
					    msf2hsg(&(tp->un.track_msf));
					break;
				case CDROM_MSF:
					toc_entry.cdte_addr.msf.minute =
					    bcd2bin(tp->un.track_msf.min);
					toc_entry.cdte_addr.msf.second =
					    bcd2bin(tp->un.track_msf.sec);
					toc_entry.cdte_addr.msf.frame =
					    bcd2bin(tp->un.track_msf.
						    frame);
					break;
				default:
					return (-EINVAL);
				}
				if (copy_to_user(argp, &toc_entry,
						 sizeof(toc_entry)))
					s = -EFAULT;
			}
			return (s);
		}

	case CDROMSUBCHNL:{
			struct cdrom_subchnl subchnl;
			int s;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: subchnl\n");
#endif
			if ((s =
			     access_ok(VERIFY_WRITE, argp, sizeof(subchnl))
			     		? 0 : -EFAULT) == 0) {
				struct sjcd_hw_qinfo q_info;

				if (copy_from_user(&subchnl, argp,
					       sizeof(subchnl)))
					return (-EFAULT);

				if (sjcd_get_q_info(&q_info) < 0)
					return (-EIO);

				subchnl.cdsc_audiostatus =
				    sjcd_audio_status;
				subchnl.cdsc_adr =
				    q_info.track_control & 0x0F;
				subchnl.cdsc_ctrl =
				    q_info.track_control >> 4;
				subchnl.cdsc_trk =
				    bcd2bin(q_info.track_no);
				subchnl.cdsc_ind = bcd2bin(q_info.x);

				switch (subchnl.cdsc_format) {
				case CDROM_LBA:
					subchnl.cdsc_absaddr.lba =
					    msf2hsg(&(q_info.abs));
					subchnl.cdsc_reladdr.lba =
					    msf2hsg(&(q_info.rel));
					break;
				case CDROM_MSF:
					subchnl.cdsc_absaddr.msf.minute =
					    bcd2bin(q_info.abs.min);
					subchnl.cdsc_absaddr.msf.second =
					    bcd2bin(q_info.abs.sec);
					subchnl.cdsc_absaddr.msf.frame =
					    bcd2bin(q_info.abs.frame);
					subchnl.cdsc_reladdr.msf.minute =
					    bcd2bin(q_info.rel.min);
					subchnl.cdsc_reladdr.msf.second =
					    bcd2bin(q_info.rel.sec);
					subchnl.cdsc_reladdr.msf.frame =
					    bcd2bin(q_info.rel.frame);
					break;
				default:
					return (-EINVAL);
				}
				if (copy_to_user(argp, &subchnl,
					         sizeof(subchnl)))
					s = -EFAULT;
			}
			return (s);
		}

	case CDROMVOLCTRL:{
			struct cdrom_volctrl vol_ctrl;
			int s;
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: volctrl\n");
#endif
			if ((s =
			     access_ok(VERIFY_READ, argp, sizeof(vol_ctrl))
			     		? 0 : -EFAULT) == 0) {
				unsigned char dummy[4];

				if (copy_from_user(&vol_ctrl, argp,
					       sizeof(vol_ctrl)))
					return (-EFAULT);
				sjcd_send_4_cmd(SCMD_SET_VOLUME,
						vol_ctrl.channel0, 0xFF,
						vol_ctrl.channel1, 0xFF);
				if (sjcd_receive_status() < 0)
					return (-EIO);
				(void) sjcd_load_response(dummy, 4);
			}
			return (s);
		}

	case CDROMEJECT:{
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: eject\n");
#endif
			if (!sjcd_command_is_in_progress) {
				sjcd_tray_unlock();
				sjcd_send_cmd(SCMD_EJECT_TRAY);
				(void) sjcd_receive_status();
			}
			return (0);
		}

#if defined( SJCD_GATHER_STAT )
	case 0xABCD:{
#if defined( SJCD_TRACE )
			printk("SJCD: ioctl: statistic\n");
#endif
			if (copy_to_user(argp, &statistic, sizeof(statistic)))
				return -EFAULT;
			return 0;
		}
#endif

	default:
		return (-EINVAL);
	}
}

/*
 * Invalidate internal buffers of the driver.
 */
static void sjcd_invalidate_buffers(void)
{
	int i;
	for (i = 0; i < SJCD_BUF_SIZ; sjcd_buf_bn[i++] = -1);
	sjcd_buf_out = -1;
}

/*
 * Take care of the different block sizes between cdrom and Linux.
 * When Linux gets variable block sizes this will probably go away.
 */

static int current_valid(void)
{
        return CURRENT &&
		CURRENT->cmd == READ &&
		CURRENT->sector != -1;
}

static void sjcd_transfer(void)
{
#if defined( SJCD_TRACE )
	printk("SJCD: transfer:\n");
#endif
	if (current_valid()) {
		while (CURRENT->nr_sectors) {
			int i, bn = CURRENT->sector / 4;
			for (i = 0;
			     i < SJCD_BUF_SIZ && sjcd_buf_bn[i] != bn;
			     i++);
			if (i < SJCD_BUF_SIZ) {
				int offs =
				    (i * 4 + (CURRENT->sector & 3)) * 512;
				int nr_sectors = 4 - (CURRENT->sector & 3);
				if (sjcd_buf_out != i) {
					sjcd_buf_out = i;
					if (sjcd_buf_bn[i] != bn) {
						sjcd_buf_out = -1;
						continue;
					}
				}
				if (nr_sectors > CURRENT->nr_sectors)
					nr_sectors = CURRENT->nr_sectors;
#if defined( SJCD_TRACE )
				printk("SJCD: copy out\n");
#endif
				memcpy(CURRENT->buffer, sjcd_buf + offs,
				       nr_sectors * 512);
				CURRENT->nr_sectors -= nr_sectors;
				CURRENT->sector += nr_sectors;
				CURRENT->buffer += nr_sectors * 512;
			} else {
				sjcd_buf_out = -1;
				break;
			}
		}
	}
#if defined( SJCD_TRACE )
	printk("SJCD: transfer: done\n");
#endif
}

static void sjcd_poll(void)
{
#if defined( SJCD_GATHER_STAT )
	/*
	 * Update total number of ticks.
	 */
	statistic.ticks++;
	statistic.tticks[sjcd_transfer_state]++;
#endif

      ReSwitch:switch (sjcd_transfer_state) {

	case SJCD_S_IDLE:{
#if defined( SJCD_GATHER_STAT )
			statistic.idle_ticks++;
#endif
#if defined( SJCD_TRACE )
			printk("SJCD_S_IDLE\n");
#endif
			return;
		}

	case SJCD_S_START:{
#if defined( SJCD_GATHER_STAT )
			statistic.start_ticks++;
#endif
			sjcd_send_cmd(SCMD_GET_STATUS);
			sjcd_transfer_state =
			    sjcd_mode ==
			    SCMD_MODE_COOKED ? SJCD_S_READ : SJCD_S_MODE;
			sjcd_transfer_timeout = 500;
#if defined( SJCD_TRACE )
			printk("SJCD_S_START: goto SJCD_S_%s mode\n",
			       sjcd_transfer_state ==
			       SJCD_S_READ ? "READ" : "MODE");
#endif
			break;
		}

	case SJCD_S_MODE:{
			if (sjcd_check_status()) {
				/*
				 * Previous command is completed.
				 */
				if (!sjcd_status_valid
				    || sjcd_command_failed) {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_MODE: pre-cmd failed: goto to SJCD_S_STOP mode\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}

				sjcd_mode = 0;	/* unknown mode; should not be valid when failed */
				sjcd_send_1_cmd(SCMD_SET_MODE,
						SCMD_MODE_COOKED);
				sjcd_transfer_state = SJCD_S_READ;
				sjcd_transfer_timeout = 1000;
#if defined( SJCD_TRACE )
				printk
				    ("SJCD_S_MODE: goto SJCD_S_READ mode\n");
#endif
			}
#if defined( SJCD_GATHER_STAT )
			else
				statistic.mode_ticks++;
#endif
			break;
		}

	case SJCD_S_READ:{
			if (sjcd_status_valid ? 1 : sjcd_check_status()) {
				/*
				 * Previous command is completed.
				 */
				if (!sjcd_status_valid
				    || sjcd_command_failed) {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_READ: pre-cmd failed: goto to SJCD_S_STOP mode\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}
				if (!sjcd_media_is_available) {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_READ: no disk: goto to SJCD_S_STOP mode\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}
				if (sjcd_mode != SCMD_MODE_COOKED) {
					/*
					 * We seem to come from set mode. So discard one byte of result.
					 */
					if (sjcd_load_response
					    (&sjcd_mode, 1) != 0) {
#if defined( SJCD_TRACE )
						printk
						    ("SJCD_S_READ: load failed: goto to SJCD_S_STOP mode\n");
#endif
						sjcd_transfer_state =
						    SJCD_S_STOP;
						goto ReSwitch;
					}
					if (sjcd_mode != SCMD_MODE_COOKED) {
#if defined( SJCD_TRACE )
						printk
						    ("SJCD_S_READ: mode failed: goto to SJCD_S_STOP mode\n");
#endif
						sjcd_transfer_state =
						    SJCD_S_STOP;
						goto ReSwitch;
					}
				}

				if (current_valid()) {
					struct sjcd_play_msf msf;

					sjcd_next_bn = CURRENT->sector / 4;
					hsg2msf(sjcd_next_bn, &msf.start);
					msf.end.min = 0;
					msf.end.sec = 0;
					msf.end.frame = sjcd_read_count =
					    SJCD_BUF_SIZ;
#if defined( SJCD_TRACE )
					printk
					    ("SJCD: ---reading msf-address %x:%x:%x  %x:%x:%x\n",
					     msf.start.min, msf.start.sec,
					     msf.start.frame, msf.end.min,
					     msf.end.sec, msf.end.frame);
					printk
					    ("sjcd_next_bn:%x buf_in:%x buf_out:%x buf_bn:%x\n",
					     sjcd_next_bn, sjcd_buf_in,
					     sjcd_buf_out,
					     sjcd_buf_bn[sjcd_buf_in]);
#endif
					sjcd_send_6_cmd(SCMD_DATA_READ,
							&msf);
					sjcd_transfer_state = SJCD_S_DATA;
					sjcd_transfer_timeout = 500;
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_READ: go to SJCD_S_DATA mode\n");
#endif
				} else {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_READ: nothing to read: go to SJCD_S_STOP mode\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}
			}
#if defined( SJCD_GATHER_STAT )
			else
				statistic.read_ticks++;
#endif
			break;
		}

	case SJCD_S_DATA:{
			unsigned char stat;

		      sjcd_s_data:stat =
			    inb(SJCDPORT
				(1));
#if defined( SJCD_TRACE )
			printk("SJCD_S_DATA: status = 0x%02x\n", stat);
#endif
			if (SJCD_STATUS_AVAILABLE(stat)) {
				/*
				 * No data is waiting for us in the drive buffer. Status of operation
				 * completion is available. Read and parse it.
				 */
				sjcd_load_status();

				if (!sjcd_status_valid
				    || sjcd_command_failed) {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD: read block %d failed, maybe audio disk? Giving up\n",
					     sjcd_next_bn);
#endif
					if (current_valid())
						end_request(CURRENT, 0);
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_DATA: pre-cmd failed: go to SJCD_S_STOP mode\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}

				if (!sjcd_media_is_available) {
					printk
					    ("SJCD_S_DATA: no disk: go to SJCD_S_STOP mode\n");
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}

				sjcd_transfer_state = SJCD_S_READ;
				goto ReSwitch;
			} else if (SJCD_DATA_AVAILABLE(stat)) {
				/*
				 * One frame is read into device buffer. We must copy it to our memory.
				 * Otherwise cdrom hangs up. Check to see if we have something to copy
				 * to.
				 */
				if (!current_valid()
				    && sjcd_buf_in == sjcd_buf_out) {
#if defined( SJCD_TRACE )
					printk
					    ("SJCD_S_DATA: nothing to read: go to SJCD_S_STOP mode\n");
					printk
					    (" ... all the date would be discarded\n");
#endif
					sjcd_transfer_state = SJCD_S_STOP;
					goto ReSwitch;
				}

				/*
				 * Everything seems to be OK. Just read the frame and recalculate
				 * indices.
				 */
				sjcd_buf_bn[sjcd_buf_in] = -1;	/* ??? */
				insb(SJCDPORT(2),
				     sjcd_buf + 2048 * sjcd_buf_in, 2048);
#if defined( SJCD_TRACE )
				printk
				    ("SJCD_S_DATA: next_bn=%d, buf_in=%d, buf_out=%d, buf_bn=%d\n",
				     sjcd_next_bn, sjcd_buf_in,
				     sjcd_buf_out,
				     sjcd_buf_bn[sjcd_buf_in]);
#endif
				sjcd_buf_bn[sjcd_buf_in] = sjcd_next_bn++;
				if (sjcd_buf_out == -1)
					sjcd_buf_out = sjcd_buf_in;
				if (++sjcd_buf_in == SJCD_BUF_SIZ)
					sjcd_buf_in = 0;

				/*
				 * Only one frame is ready at time. So we should turn over to wait for
				 * another frame. If we need that, of course.
				 */
				if (--sjcd_read_count == 0) {
					/*
					 * OK, request seems to be precessed. Continue transferring...
					 */
					if (!sjcd_transfer_is_active) {
						while (current_valid()) {
							/*
							 * Continue transferring.
							 */
							sjcd_transfer();
							if (CURRENT->
							    nr_sectors ==
							    0)
								end_request
								    (CURRENT, 1);
							else
								break;
						}
					}
					if (current_valid() &&
					    (CURRENT->sector / 4 <
					     sjcd_next_bn
					     || CURRENT->sector / 4 >
					     sjcd_next_bn +
					     SJCD_BUF_SIZ)) {
#if defined( SJCD_TRACE )
						printk
						    ("SJCD_S_DATA: can't read: go to SJCD_S_STOP mode\n");
#endif
						sjcd_transfer_state =
						    SJCD_S_STOP;
						goto ReSwitch;
					}
				}
				/*
				 * Now we should turn around rather than wait for while.
				 */
				goto sjcd_s_data;
			}
#if defined( SJCD_GATHER_STAT )
			else
				statistic.data_ticks++;
#endif
			break;
		}

	case SJCD_S_STOP:{
			sjcd_read_count = 0;
			sjcd_send_cmd(SCMD_STOP);
			sjcd_transfer_state = SJCD_S_STOPPING;
			sjcd_transfer_timeout = 500;
#if defined( SJCD_GATHER_STAT )
			statistic.stop_ticks++;
#endif
			break;
		}

	case SJCD_S_STOPPING:{
			unsigned char stat;

			stat = inb(SJCDPORT(1));
#if defined( SJCD_TRACE )
			printk("SJCD_S_STOP: status = 0x%02x\n", stat);
#endif
			if (SJCD_DATA_AVAILABLE(stat)) {
				int i;
#if defined( SJCD_TRACE )
				printk("SJCD_S_STOP: discard data\n");
#endif
				/*
				 * Discard all the data from the pipe. Foolish method.
				 */
				for (i = 2048; i--;
				     (void) inb(SJCDPORT(2)));
				sjcd_transfer_timeout = 500;
			} else if (SJCD_STATUS_AVAILABLE(stat)) {
				sjcd_load_status();
				if (sjcd_status_valid
				    && sjcd_media_is_changed) {
					sjcd_toc_uptodate = 0;
					sjcd_invalidate_buffers();
				}
				if (current_valid()) {
					if (sjcd_status_valid)
						sjcd_transfer_state =
						    SJCD_S_READ;
					else
						sjcd_transfer_state =
						    SJCD_S_START;
				} else
					sjcd_transfer_state = SJCD_S_IDLE;
				goto ReSwitch;
			}
#if defined( SJCD_GATHER_STAT )
			else
				statistic.stopping_ticks++;
#endif
			break;
		}

	default:
		printk("SJCD: poll: invalid state %d\n",
		       sjcd_transfer_state);
		return;
	}

	if (--sjcd_transfer_timeout == 0) {
		printk("SJCD: timeout in state %d\n", sjcd_transfer_state);
		while (current_valid())
			end_request(CURRENT, 0);
		sjcd_send_cmd(SCMD_STOP);
		sjcd_transfer_state = SJCD_S_IDLE;
		goto ReSwitch;
	}

	/*
	 * Get back in some time. 1 should be replaced with count variable to
	 * avoid unnecessary testings.
	 */
	SJCD_SET_TIMER(sjcd_poll, 1);
}

static void do_sjcd_request(request_queue_t * q)
{
#if defined( SJCD_TRACE )
	printk("SJCD: do_sjcd_request(%ld+%ld)\n",
	       CURRENT->sector, CURRENT->nr_sectors);
#endif
	sjcd_transfer_is_active = 1;
	while (current_valid()) {
		sjcd_transfer();
		if (CURRENT->nr_sectors == 0)
			end_request(CURRENT, 1);
		else {
			sjcd_buf_out = -1;	/* Want to read a block not in buffer */
			if (sjcd_transfer_state == SJCD_S_IDLE) {
				if (!sjcd_toc_uptodate) {
					if (sjcd_update_toc() < 0) {
						printk
						    ("SJCD: transfer: discard\n");
						while (current_valid())
							end_request(CURRENT, 0);
						break;
					}
				}
				sjcd_transfer_state = SJCD_S_START;
				SJCD_SET_TIMER(sjcd_poll, HZ / 100);
			}
			break;
		}
	}
	sjcd_transfer_is_active = 0;
#if defined( SJCD_TRACE )
	printk
	    ("sjcd_next_bn:%x sjcd_buf_in:%x sjcd_buf_out:%x sjcd_buf_bn:%x\n",
	     sjcd_next_bn, sjcd_buf_in, sjcd_buf_out,
	     sjcd_buf_bn[sjcd_buf_in]);
	printk("do_sjcd_request ends\n");
#endif
}

/*
 * Open the device special file. Check disk is in.
 */
static int sjcd_open(struct inode *ip, struct file *fp)
{
	/*
	 * Check the presence of device.
	 */
	if (!sjcd_present)
		return (-ENXIO);

	/*
	 * Only read operations are allowed. Really? (:-)
	 */
	if (fp->f_mode & 2)
		return (-EROFS);

	if (sjcd_open_count == 0) {
		int s, sjcd_open_tries;
/* We don't know that, do we? */
/*
    sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
*/
		sjcd_mode = 0;
		sjcd_door_was_open = 0;
		sjcd_transfer_state = SJCD_S_IDLE;
		sjcd_invalidate_buffers();
		sjcd_status_valid = 0;

		/*
		 * Strict status checking.
		 */
		for (sjcd_open_tries = 4; --sjcd_open_tries;) {
			if (!sjcd_status_valid)
				sjcd_get_status();
			if (!sjcd_status_valid) {
#if defined( SJCD_DIAGNOSTIC )
				printk
				    ("SJCD: open: timed out when check status.\n");
#endif
				goto err_out;
			} else if (!sjcd_media_is_available) {
#if defined( SJCD_DIAGNOSTIC )
				printk("SJCD: open: no disk in drive\n");
#endif
				if (!sjcd_door_closed) {
					sjcd_door_was_open = 1;
#if defined( SJCD_TRACE )
					printk
					    ("SJCD: open: close the tray\n");
#endif
					s = sjcd_tray_close();
					if (s < 0 || !sjcd_status_valid
					    || sjcd_command_failed) {
#if defined( SJCD_DIAGNOSTIC )
						printk
						    ("SJCD: open: tray close attempt failed\n");
#endif
						goto err_out;
					}
					continue;
				} else
					goto err_out;
			}
			break;
		}
		s = sjcd_tray_lock();
		if (s < 0 || !sjcd_status_valid || sjcd_command_failed) {
#if defined( SJCD_DIAGNOSTIC )
			printk("SJCD: open: tray lock attempt failed\n");
#endif
			goto err_out;
		}
#if defined( SJCD_TRACE )
		printk("SJCD: open: done\n");
#endif
	}

	++sjcd_open_count;
	return (0);

      err_out:
	return (-EIO);
}

/*
 * On close, we flush all sjcd blocks from the buffer cache.
 */
static int sjcd_release(struct inode *inode, struct file *file)
{
	int s;

#if defined( SJCD_TRACE )
	printk("SJCD: release\n");
#endif
	if (--sjcd_open_count == 0) {
		sjcd_invalidate_buffers();
		s = sjcd_tray_unlock();
		if (s < 0 || !sjcd_status_valid || sjcd_command_failed) {
#if defined( SJCD_DIAGNOSTIC )
			printk
			    ("SJCD: release: tray unlock attempt failed.\n");
#endif
		}
		if (sjcd_door_was_open) {
			s = sjcd_tray_open();
			if (s < 0 || !sjcd_status_valid
			    || sjcd_command_failed) {
#if defined( SJCD_DIAGNOSTIC )
				printk
				    ("SJCD: release: tray unload attempt failed.\n");
#endif
			}
		}
	}
	return 0;
}

/*
 * A list of file operations allowed for this cdrom.
 */
static struct block_device_operations sjcd_fops = {
	.owner		= THIS_MODULE,
	.open		= sjcd_open,
	.release	= sjcd_release,
	.ioctl		= sjcd_ioctl,
	.media_changed	= sjcd_disk_change,
};

/*
 * Following stuff is intended for initialization of the cdrom. It
 * first looks for presence of device. If the device is present, it
 * will be reset. Then read the version of the drive and load status.
 * The version is two BCD-coded bytes.
 */
static struct {
	unsigned char major, minor;
} sjcd_version;

static struct gendisk *sjcd_disk;

/*
 * Test for presence of drive and initialize it. Called at boot time.
 * Probe cdrom, find out version and status.
 */
static int __init sjcd_init(void)
{
	int i;

	printk(KERN_INFO
	       "SJCD: Sanyo CDR-H94A cdrom driver version %d.%d.\n",
	       SJCD_VERSION_MAJOR, SJCD_VERSION_MINOR);

#if defined( SJCD_TRACE )
	printk("SJCD: sjcd=0x%x: ", sjcd_base);
#endif

	if (register_blkdev(MAJOR_NR, "sjcd"))
		return -EIO;

	sjcd_queue = blk_init_queue(do_sjcd_request, &sjcd_lock);
	if (!sjcd_queue)
		goto out0;

	blk_queue_hardsect_size(sjcd_queue, 2048);

	sjcd_disk = alloc_disk(1);
	if (!sjcd_disk) {
		printk(KERN_ERR "SJCD: can't allocate disk");
		goto out1;
	}
	sjcd_disk->major = MAJOR_NR,
	sjcd_disk->first_minor = 0,
	sjcd_disk->fops = &sjcd_fops,
	sprintf(sjcd_disk->disk_name, "sjcd");

	if (!request_region(sjcd_base, 4,"sjcd")) {
		printk
		    ("SJCD: Init failed, I/O port (%X) is already in use\n",
		     sjcd_base);
		goto out2;
	}

	/*
	 * Check for card. Since we are booting now, we can't use standard
	 * wait algorithm.
	 */
	printk(KERN_INFO "SJCD: Resetting: ");
	sjcd_send_cmd(SCMD_RESET);
	for (i = 1000; i > 0 && !sjcd_status_valid; --i) {
		unsigned long timer;

		/*
		 * Wait 10ms approx.
		 */
		for (timer = jiffies; time_before_eq(jiffies, timer););
		if ((i % 100) == 0)
			printk(".");
		(void) sjcd_check_status();
	}
	if (i == 0 || sjcd_command_failed) {
		printk(" reset failed, no drive found.\n");
		goto out3;
	} else
		printk("\n");

	/*
	 * Get and print out cdrom version.
	 */
	printk(KERN_INFO "SJCD: Getting version: ");
	sjcd_send_cmd(SCMD_GET_VERSION);
	for (i = 1000; i > 0 && !sjcd_status_valid; --i) {
		unsigned long timer;

		/*
		 * Wait 10ms approx.
		 */
		for (timer = jiffies; time_before_eq(jiffies, timer););
		if ((i % 100) == 0)
			printk(".");
		(void) sjcd_check_status();
	}
	if (i == 0 || sjcd_command_failed) {
		printk(" get version failed, no drive found.\n");
		goto out3;
	}

	if (sjcd_load_response(&sjcd_version, sizeof(sjcd_version)) == 0) {
		printk(" %1x.%02x\n", (int) sjcd_version.major,
		       (int) sjcd_version.minor);
	} else {
		printk(" read version failed, no drive found.\n");
		goto out3;
	}

	/*
	 * Check and print out the tray state. (if it is needed?).
	 */
	if (!sjcd_status_valid) {
		printk(KERN_INFO "SJCD: Getting status: ");
		sjcd_send_cmd(SCMD_GET_STATUS);
		for (i = 1000; i > 0 && !sjcd_status_valid; --i) {
			unsigned long timer;

			/*
			 * Wait 10ms approx.
			 */
			for (timer = jiffies;
			     time_before_eq(jiffies, timer););
			if ((i % 100) == 0)
				printk(".");
			(void) sjcd_check_status();
		}
		if (i == 0 || sjcd_command_failed) {
			printk(" get status failed, no drive found.\n");
			goto out3;
		} else
			printk("\n");
	}

	printk(KERN_INFO "SJCD: Status: port=0x%x.\n", sjcd_base);
	sjcd_disk->queue = sjcd_queue;
	add_disk(sjcd_disk);

	sjcd_present++;
	return (0);
out3:
	release_region(sjcd_base, 4);
out2:
	put_disk(sjcd_disk);
out1:
	blk_cleanup_queue(sjcd_queue);
out0:
	if ((unregister_blkdev(MAJOR_NR, "sjcd") == -EINVAL))
		printk("SJCD: cannot unregister device.\n");
	return (-EIO);
}

static void __exit sjcd_exit(void)
{
	del_gendisk(sjcd_disk);
	put_disk(sjcd_disk);
	release_region(sjcd_base, 4);
	blk_cleanup_queue(sjcd_queue);
	if ((unregister_blkdev(MAJOR_NR, "sjcd") == -EINVAL))
		printk("SJCD: cannot unregister device.\n");
	printk(KERN_INFO "SJCD: module: removed.\n");
}

module_init(sjcd_init);
module_exit(sjcd_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(SANYO_CDROM_MAJOR);

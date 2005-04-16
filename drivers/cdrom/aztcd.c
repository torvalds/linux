#define AZT_VERSION "2.60"

/*      $Id: aztcd.c,v 2.60 1997/11/29 09:51:19 root Exp root $
	linux/drivers/block/aztcd.c - Aztech CD268 CDROM driver

	Copyright (C) 1994-98 Werner Zimmermann(Werner.Zimmermann@fht-esslingen.de)

	based on Mitsumi CDROM driver by  Martin Hariss and preworks by
	Eberhard Moenkeberg; contains contributions by Joe Nardone and Robby 
	Schirmer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	HISTORY
	V0.0    Adaption to Aztech CD268-01A Version 1.3
		Version is PRE_ALPHA, unresolved points:
		1. I use busy wait instead of timer wait in STEN_LOW,DTEN_LOW
		   thus driver causes CPU overhead and is very slow 
		2. could not find a way to stop the drive, when it is
		   in data read mode, therefore I had to set
		   msf.end.min/sec/frame to 0:0:1 (in azt_poll); so only one
		   frame can be read in sequence, this is also the reason for
		3. getting 'timeout in state 4' messages, but nevertheless
		   it works
		W.Zimmermann, Oct. 31, 1994
	V0.1    Version is ALPHA, problems #2 and #3 resolved.  
		W.Zimmermann, Nov. 3, 1994
	V0.2    Modification to some comments, debugging aids for partial test
		with Borland C under DOS eliminated. Timer interrupt wait 
		STEN_LOW_WAIT additionally to busy wait for STEN_LOW implemented; 
		use it only for the 'slow' commands (ACMD_GET_Q_CHANNEL, ACMD_
		SEEK_TO_LEAD_IN), all other commands are so 'fast', that busy 
		waiting seems better to me than interrupt rescheduling.
		Besides that, when used in the wrong place, STEN_LOW_WAIT causes
		kernel panic.
		In function aztPlay command ACMD_PLAY_AUDIO added, should make
		audio functions work. The Aztech drive needs different commands
		to read data tracks and play audio tracks.
		W.Zimmermann, Nov. 8, 1994
	V0.3    Recognition of missing drive during boot up improved (speeded up).
		W.Zimmermann, Nov. 13, 1994
	V0.35   Rewrote the control mechanism in azt_poll (formerly mcd_poll) 
		including removal of all 'goto' commands. :-); 
		J. Nardone, Nov. 14, 1994
	V0.4    Renamed variables and constants to 'azt' instead of 'mcd'; had
		to make some "compatibility" defines in azt.h; please note,
		that the source file was renamed to azt.c, the include file to
		azt.h                
		Speeded up drive recognition during init (will be a little bit 
		slower than before if no drive is installed!); suggested by
		Robby Schirmer.
		read_count declared volatile and set to AZT_BUF_SIZ to make
		drive faster (now 300kB/sec, was 60kB/sec before, measured
		by 'time dd if=/dev/cdrom of=/dev/null bs=2048 count=4096';
		different AZT_BUF_SIZes were test, above 16 no further im-
		provement seems to be possible; suggested by E.Moenkeberg.
		W.Zimmermann, Nov. 18, 1994
	V0.42   Included getAztStatus command in GetQChannelInfo() to allow
		reading Q-channel info on audio disks, if drive is stopped, 
		and some other bug fixes in the audio stuff, suggested by 
		Robby Schirmer.
		Added more ioctls (reading data in mode 1 and mode 2).
		Completely removed the old azt_poll() routine.
		Detection of ORCHID CDS-3110 in aztcd_init implemented.
		Additional debugging aids (see the readme file).
		W.Zimmermann, Dec. 9, 1994  
	V0.50   Autodetection of drives implemented.
		W.Zimmermann, Dec. 12, 1994
	V0.52   Prepared for including in the standard kernel, renamed most
		variables to contain 'azt', included autoconf.h
		W.Zimmermann, Dec. 16, 1994        
	V0.6    Version for being included in the standard Linux kernel.
		Renamed source and header file to aztcd.c and aztcd.h
		W.Zimmermann, Dec. 24, 1994
	V0.7    Changed VERIFY_READ to VERIFY_WRITE in aztcd_ioctl, case
		CDROMREADMODE1 and CDROMREADMODE2; bug fix in the ioctl,
		which causes kernel crashes when playing audio, changed 
		include-files (config.h instead of autoconf.h, removed
		delay.h)
		W.Zimmermann, Jan. 8, 1995
	V0.72   Some more modifications for adaption to the standard kernel.
		W.Zimmermann, Jan. 16, 1995
        V0.80   aztcd is now part of the standard kernel since version 1.1.83.
                Modified the SET_TIMER and CLEAR_TIMER macros to comply with
                the new timer scheme.
                W.Zimmermann, Jan. 21, 1995
        V0.90   Included CDROMVOLCTRL, but with my Aztech drive I can only turn
                the channels on and off. If it works better with your drive, 
                please mail me. Also implemented ACMD_CLOSE for CDROMSTART.
                W.Zimmermann, Jan. 24, 1995
        V1.00   Implemented close and lock tray commands. Patches supplied by
		Frank Racis        
                Added support for loadable MODULEs, so aztcd can now also be
                loaded by insmod and removed by rmmod during run time
                Werner Zimmermann, Mar. 24, 95
        V1.10   Implemented soundcard configuration for Orchid CDS-3110 drives
                connected to Soundwave32 cards. Release for LST 2.1.
                (still experimental)
                Werner Zimmermann, May 8, 95
        V1.20   Implemented limited support for DOSEMU0.60's cdrom.c. Now it works, but
                sometimes DOSEMU may hang for 30 seconds or so. A fully functional ver-
                sion needs an update of Dosemu0.60's cdrom.c, which will come with the 
                next revision of Dosemu.
                Also Soundwave32 support now works.
                Werner Zimmermann, May 22, 95
	V1.30   Auto-eject feature. Inspired by Franc Racis (racis@psu.edu)
	        Werner Zimmermann, July 4, 95
	V1.40   Started multisession support. Implementation copied from mcdx.c
	        by Heiko Schlittermann. Not tested yet.
	        Werner Zimmermann, July 15, 95
        V1.50   Implementation of ioctl CDROMRESET, continued multisession, began
                XA, but still untested. Heavy modifications to drive status de-
                tection.
                Werner Zimmermann, July 25, 95
        V1.60   XA support now should work. Speeded up drive recognition in cases, 
                where no drive is installed.
                Werner Zimmermann, August 8, 1995
        V1.70   Multisession support now is completed, but there is still not 
                enough testing done. If you can test it, please contact me. For
                details please read Documentation/cdrom/aztcd
                Werner Zimmermann, August 19, 1995
        V1.80   Modification to suit the new kernel boot procedure introduced
                with kernel 1.3.33. Will definitely not work with older kernels.
                Programming done by Linus himself.
                Werner Zimmermann, October 11, 1995
	V1.90   Support for Conrad TXC drives, thank's to Jochen Kunz and Olaf Kaluza.
	        Werner Zimmermann, October 21, 1995
        V2.00   Changed #include "blk.h" to <linux/blk.h> as the directory
                structure was changed. README.aztcd is now /usr/src/docu-
                mentation/cdrom/aztcd
                Werner Zimmermann, November 10, 95
        V2.10   Started to modify azt_poll to prevent reading beyond end of
                tracks.
                Werner Zimmermann, December 3, 95
        V2.20   Changed some comments
                Werner Zimmermann, April 1, 96
        V2.30   Implemented support for CyCDROM CR520, CR940, Code for CR520 
        	delivered by H.Berger with preworks by E.Moenkeberg.
                Werner Zimmermann, April 29, 96
        V2.40   Reorganized the placement of functions in the source code file
                to reflect the layered approach; did not actually change code
                Werner Zimmermann, May 1, 96
        V2.50   Heiko Eissfeldt suggested to remove some VERIFY_READs in 
                aztcd_ioctl; check_aztcd_media_change modified 
                Werner Zimmermann, May 16, 96       
	V2.60   Implemented Auto-Probing; made changes for kernel's 2.1.xx blocksize
                Adaption to linux kernel > 2.1.0
		Werner Zimmermann, Nov 29, 97
		
        November 1999 -- Make kernel-parameter implementation work with 2.3.x 
	                 Removed init_module & cleanup_module in favor of 
			 module_init & module_exit.
			 Torben Mathiasen <tmm@image.dk>
*/

#include <linux/blkdev.h>
#include "aztcd.h"

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
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

/*###########################################################################
  Defines
  ###########################################################################
*/

#define MAJOR_NR AZTECH_CDROM_MAJOR
#define QUEUE (azt_queue)
#define CURRENT elv_next_request(azt_queue)
#define SET_TIMER(func, jifs)   delay_timer.expires = jiffies + (jifs); \
                                delay_timer.function = (void *) (func); \
                                add_timer(&delay_timer);

#define CLEAR_TIMER             del_timer(&delay_timer);

#define RETURNM(message,value) {printk("aztcd: Warning: %s failed\n",message);\
                                return value;}
#define RETURN(message)        {printk("aztcd: Warning: %s failed\n",message);\
                                return;}

/* Macros to switch the IDE-interface to the slave device and back to the master*/
#define SWITCH_IDE_SLAVE  outb_p(0xa0,azt_port+6); \
	                  outb_p(0x10,azt_port+6); \
	                  outb_p(0x00,azt_port+7); \
	                  outb_p(0x10,azt_port+6);
#define SWITCH_IDE_MASTER outb_p(0xa0,azt_port+6);


#if 0
#define AZT_TEST
#define AZT_TEST1		/* <int-..> */
#define AZT_TEST2		/* do_aztcd_request */
#define AZT_TEST3		/* AZT_S_state */
#define AZT_TEST4		/* QUICK_LOOP-counter */
#define AZT_TEST5		/* port(1) state */
#define AZT_DEBUG
#define AZT_DEBUG_MULTISESSION
#endif

static struct request_queue *azt_queue;

static int current_valid(void)
{
        return CURRENT &&
		CURRENT->cmd == READ &&
		CURRENT->sector != -1;
}

#define AFL_STATUSorDATA (AFL_STATUS | AFL_DATA)
#define AZT_BUF_SIZ 16

#define READ_TIMEOUT 3000

#define azt_port aztcd		/*needed for the modutils */

/*##########################################################################
  Type Definitions
  ##########################################################################
*/
enum azt_state_e { AZT_S_IDLE,	/* 0 */
	AZT_S_START,		/* 1 */
	AZT_S_MODE,		/* 2 */
	AZT_S_READ,		/* 3 */
	AZT_S_DATA,		/* 4 */
	AZT_S_STOP,		/* 5 */
	AZT_S_STOPPING		/* 6 */
};
enum azt_read_modes { AZT_MODE_0,	/*read mode for audio disks, not supported by Aztech firmware */
	AZT_MODE_1,		/*read mode for normal CD-ROMs */
	AZT_MODE_2		/*read mode for XA CD-ROMs */
};

/*##########################################################################
  Global Variables
  ##########################################################################
*/
static int aztPresent = 0;

static volatile int azt_transfer_is_active = 0;

static char azt_buf[CD_FRAMESIZE_RAW * AZT_BUF_SIZ];	/*buffer for block size conversion */
#if AZT_PRIVATE_IOCTLS
static char buf[CD_FRAMESIZE_RAW];	/*separate buffer for the ioctls */
#endif

static volatile int azt_buf_bn[AZT_BUF_SIZ], azt_next_bn;
static volatile int azt_buf_in, azt_buf_out = -1;
static volatile int azt_error = 0;
static int azt_open_count = 0;
static volatile enum azt_state_e azt_state = AZT_S_IDLE;
#ifdef AZT_TEST3
static volatile enum azt_state_e azt_state_old = AZT_S_STOP;
static volatile int azt_st_old = 0;
#endif
static volatile enum azt_read_modes azt_read_mode = AZT_MODE_1;

static int azt_mode = -1;
static volatile int azt_read_count = 1;

static int azt_port = AZT_BASE_ADDR;

module_param(azt_port, int, 0);

static int azt_port_auto[16] = AZT_BASE_AUTO;

static char azt_cont = 0;
static char azt_init_end = 0;
static char azt_auto_eject = AZT_AUTO_EJECT;

static int AztTimeout, AztTries;
static DECLARE_WAIT_QUEUE_HEAD(azt_waitq);
static struct timer_list delay_timer = TIMER_INITIALIZER(NULL, 0, 0);

static struct azt_DiskInfo DiskInfo;
static struct azt_Toc Toc[MAX_TRACKS];
static struct azt_Play_msf azt_Play;

static int aztAudioStatus = CDROM_AUDIO_NO_STATUS;
static char aztDiskChanged = 1;
static char aztTocUpToDate = 0;

static unsigned char aztIndatum;
static unsigned long aztTimeOutCount;
static int aztCmd = 0;

static DEFINE_SPINLOCK(aztSpin);

/*###########################################################################
   Function Prototypes
  ###########################################################################
*/
/* CDROM Drive Low Level I/O Functions */
static void aztStatTimer(void);

/* CDROM Drive Command Functions */
static int aztGetDiskInfo(void);
#if AZT_MULTISESSION
static int aztGetMultiDiskInfo(void);
#endif
static int aztGetToc(int multi);

/* Kernel Interface Functions */
static int check_aztcd_media_change(struct gendisk *disk);
static int aztcd_ioctl(struct inode *ip, struct file *fp, unsigned int cmd,
		       unsigned long arg);
static int aztcd_open(struct inode *ip, struct file *fp);
static int aztcd_release(struct inode *inode, struct file *file);

static struct block_device_operations azt_fops = {
	.owner		= THIS_MODULE,
	.open		= aztcd_open,
	.release	= aztcd_release,
	.ioctl		= aztcd_ioctl,
	.media_changed	= check_aztcd_media_change,
};

/* Aztcd State Machine: Controls Drive Operating State */
static void azt_poll(void);

/* Miscellaneous support functions */
static void azt_hsg2msf(long hsg, struct msf *msf);
static long azt_msf2hsg(struct msf *mp);
static void azt_bin2bcd(unsigned char *p);
static int azt_bcd2bin(unsigned char bcd);

/*##########################################################################
  CDROM Drive Low Level I/O Functions
  ##########################################################################
*/
/* Macros for the drive hardware interface handshake, these macros use
   busy waiting */
/* Wait for OP_OK = drive answers with AFL_OP_OK after receiving a command*/
# define OP_OK op_ok()
static void op_ok(void)
{
	aztTimeOutCount = 0;
	do {
		aztIndatum = inb(DATA_PORT);
		aztTimeOutCount++;
		if (aztTimeOutCount >= AZT_TIMEOUT) {
			printk("aztcd: Error Wait OP_OK\n");
			break;
		}
	} while (aztIndatum != AFL_OP_OK);
}

/* Wait for PA_OK = drive answers with AFL_PA_OK after receiving parameters*/
#if 0
# define PA_OK pa_ok()
static void pa_ok(void)
{
	aztTimeOutCount = 0;
	do {
		aztIndatum = inb(DATA_PORT);
		aztTimeOutCount++;
		if (aztTimeOutCount >= AZT_TIMEOUT) {
			printk("aztcd: Error Wait PA_OK\n");
			break;
		}
	} while (aztIndatum != AFL_PA_OK);
}
#endif

/* Wait for STEN=Low = handshake signal 'AFL_.._OK available or command executed*/
# define STEN_LOW  sten_low()
static void sten_low(void)
{
	aztTimeOutCount = 0;
	do {
		aztIndatum = inb(STATUS_PORT);
		aztTimeOutCount++;
		if (aztTimeOutCount >= AZT_TIMEOUT) {
			if (azt_init_end)
				printk
				    ("aztcd: Error Wait STEN_LOW commands:%x\n",
				     aztCmd);
			break;
		}
	} while (aztIndatum & AFL_STATUS);
}

/* Wait for DTEN=Low = handshake signal 'Data available'*/
# define DTEN_LOW dten_low()
static void dten_low(void)
{
	aztTimeOutCount = 0;
	do {
		aztIndatum = inb(STATUS_PORT);
		aztTimeOutCount++;
		if (aztTimeOutCount >= AZT_TIMEOUT) {
			printk("aztcd: Error Wait DTEN_OK\n");
			break;
		}
	} while (aztIndatum & AFL_DATA);
}

/* 
 * Macro for timer wait on STEN=Low, should only be used for 'slow' commands;
 * may cause kernel panic when used in the wrong place
*/
#define STEN_LOW_WAIT   statusAzt()
static void statusAzt(void)
{
	AztTimeout = AZT_STATUS_DELAY;
	SET_TIMER(aztStatTimer, HZ / 100);
	sleep_on(&azt_waitq);
	if (AztTimeout <= 0)
		printk("aztcd: Error Wait STEN_LOW_WAIT command:%x\n",
		       aztCmd);
	return;
}

static void aztStatTimer(void)
{
	if (!(inb(STATUS_PORT) & AFL_STATUS)) {
		wake_up(&azt_waitq);
		return;
	}
	AztTimeout--;
	if (AztTimeout <= 0) {
		wake_up(&azt_waitq);
		printk("aztcd: Error aztStatTimer: Timeout\n");
		return;
	}
	SET_TIMER(aztStatTimer, HZ / 100);
}

/*##########################################################################
  CDROM Drive Command Functions
  ##########################################################################
*/
/* 
 * Send a single command, return -1 on error, else 0
*/
static int aztSendCmd(int cmd)
{
	unsigned char data;
	int retry;

#ifdef AZT_DEBUG
	printk("aztcd: Executing command %x\n", cmd);
#endif

	if ((azt_port == 0x1f0) || (azt_port == 0x170))
		SWITCH_IDE_SLAVE;	/*switch IDE interface to slave configuration */

	aztCmd = cmd;
	outb(POLLED, MODE_PORT);
	do {
		if (inb(STATUS_PORT) & AFL_STATUS)
			break;
		inb(DATA_PORT);	/* if status left from last command, read and */
	} while (1);		/* discard it */
	do {
		if (inb(STATUS_PORT) & AFL_DATA)
			break;
		inb(DATA_PORT);	/* if data left from last command, read and */
	} while (1);		/* discard it */
	for (retry = 0; retry < AZT_RETRY_ATTEMPTS; retry++) {
		outb((unsigned char) cmd, CMD_PORT);
		STEN_LOW;
		data = inb(DATA_PORT);
		if (data == AFL_OP_OK) {
			return 0;
		}		/*OP_OK? */
		if (data == AFL_OP_ERR) {
			STEN_LOW;
			data = inb(DATA_PORT);
			printk
			    ("### Error 1 aztcd: aztSendCmd %x  Error Code %x\n",
			     cmd, data);
		}
	}
	if (retry >= AZT_RETRY_ATTEMPTS) {
		printk("### Error 2 aztcd: aztSendCmd %x \n", cmd);
		azt_error = 0xA5;
	}
	RETURNM("aztSendCmd", -1);
}

/*
 * Send a play or read command to the drive, return -1 on error, else 0
*/
static int sendAztCmd(int cmd, struct azt_Play_msf *params)
{
	unsigned char data;
	int retry;

#ifdef AZT_DEBUG
	printk("aztcd: play start=%02x:%02x:%02x  end=%02x:%02x:%02x\n",
	       params->start.min, params->start.sec, params->start.frame,
	       params->end.min, params->end.sec, params->end.frame);
#endif
	for (retry = 0; retry < AZT_RETRY_ATTEMPTS; retry++) {
		aztSendCmd(cmd);
		outb(params->start.min, CMD_PORT);
		outb(params->start.sec, CMD_PORT);
		outb(params->start.frame, CMD_PORT);
		outb(params->end.min, CMD_PORT);
		outb(params->end.sec, CMD_PORT);
		outb(params->end.frame, CMD_PORT);
		STEN_LOW;
		data = inb(DATA_PORT);
		if (data == AFL_PA_OK) {
			return 0;
		}		/*PA_OK ? */
		if (data == AFL_PA_ERR) {
			STEN_LOW;
			data = inb(DATA_PORT);
			printk
			    ("### Error 1 aztcd: sendAztCmd %x  Error Code %x\n",
			     cmd, data);
		}
	}
	if (retry >= AZT_RETRY_ATTEMPTS) {
		printk("### Error 2 aztcd: sendAztCmd %x\n ", cmd);
		azt_error = 0xA5;
	}
	RETURNM("sendAztCmd", -1);
}

/*
 * Send a seek command to the drive, return -1 on error, else 0
*/
static int aztSeek(struct azt_Play_msf *params)
{
	unsigned char data;
	int retry;

#ifdef AZT_DEBUG
	printk("aztcd: aztSeek %02x:%02x:%02x\n",
	       params->start.min, params->start.sec, params->start.frame);
#endif
	for (retry = 0; retry < AZT_RETRY_ATTEMPTS; retry++) {
		aztSendCmd(ACMD_SEEK);
		outb(params->start.min, CMD_PORT);
		outb(params->start.sec, CMD_PORT);
		outb(params->start.frame, CMD_PORT);
		STEN_LOW;
		data = inb(DATA_PORT);
		if (data == AFL_PA_OK) {
			return 0;
		}		/*PA_OK ? */
		if (data == AFL_PA_ERR) {
			STEN_LOW;
			data = inb(DATA_PORT);
			printk("### Error 1 aztcd: aztSeek\n");
		}
	}
	if (retry >= AZT_RETRY_ATTEMPTS) {
		printk("### Error 2 aztcd: aztSeek\n ");
		azt_error = 0xA5;
	}
	RETURNM("aztSeek", -1);
}

/* Send a Set Disk Type command
   does not seem to work with Aztech drives, behavior is completely indepen-
   dent on which mode is set ???
*/
static int aztSetDiskType(int type)
{
	unsigned char data;
	int retry;

#ifdef AZT_DEBUG
	printk("aztcd: set disk type command: type= %i\n", type);
#endif
	for (retry = 0; retry < AZT_RETRY_ATTEMPTS; retry++) {
		aztSendCmd(ACMD_SET_DISK_TYPE);
		outb(type, CMD_PORT);
		STEN_LOW;
		data = inb(DATA_PORT);
		if (data == AFL_PA_OK) {	/*PA_OK ? */
			azt_read_mode = type;
			return 0;
		}
		if (data == AFL_PA_ERR) {
			STEN_LOW;
			data = inb(DATA_PORT);
			printk
			    ("### Error 1 aztcd: aztSetDiskType %x Error Code %x\n",
			     type, data);
		}
	}
	if (retry >= AZT_RETRY_ATTEMPTS) {
		printk("### Error 2 aztcd: aztSetDiskType %x\n ", type);
		azt_error = 0xA5;
	}
	RETURNM("aztSetDiskType", -1);
}


/* used in azt_poll to poll the status, expects another program to issue a 
 * ACMD_GET_STATUS directly before 
 */
static int aztStatus(void)
{
	int st;
/*	int i;

	i = inb(STATUS_PORT) & AFL_STATUS;    is STEN=0?    ???
	if (!i)
*/ STEN_LOW;
	if (aztTimeOutCount < AZT_TIMEOUT) {
		st = inb(DATA_PORT) & 0xFF;
		return st;
	} else
		RETURNM("aztStatus", -1);
}

/*
 * Get the drive status
 */
static int getAztStatus(void)
{
	int st;

	if (aztSendCmd(ACMD_GET_STATUS))
		RETURNM("getAztStatus 1", -1);
	STEN_LOW;
	st = inb(DATA_PORT) & 0xFF;
#ifdef AZT_DEBUG
	printk("aztcd: Status = %x\n", st);
#endif
	if ((st == 0xFF) || (st & AST_CMD_CHECK)) {
		printk
		    ("aztcd: AST_CMD_CHECK error or no status available\n");
		return -1;
	}

	if (((st & AST_MODE_BITS) != AST_BUSY)
	    && (aztAudioStatus == CDROM_AUDIO_PLAY))
		/* XXX might be an error? look at q-channel? */
		aztAudioStatus = CDROM_AUDIO_COMPLETED;

	if ((st & AST_DSK_CHG) || (st & AST_NOT_READY)) {
		aztDiskChanged = 1;
		aztTocUpToDate = 0;
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
	}
	return st;
}


/*
 * Send a 'Play' command and get the status.  Use only from the top half.
 */
static int aztPlay(struct azt_Play_msf *arg)
{
	if (sendAztCmd(ACMD_PLAY_AUDIO, arg) < 0)
		RETURNM("aztPlay", -1);
	return 0;
}

/*
 * Subroutines to automatically close the door (tray) and 
 * lock it closed when the cd is mounted.  Leave the tray
 * locking as an option
 */
static void aztCloseDoor(void)
{
	aztSendCmd(ACMD_CLOSE);
	STEN_LOW;
	return;
}

static void aztLockDoor(void)
{
#if AZT_ALLOW_TRAY_LOCK
	aztSendCmd(ACMD_LOCK);
	STEN_LOW;
#endif
	return;
}

static void aztUnlockDoor(void)
{
#if AZT_ALLOW_TRAY_LOCK
	aztSendCmd(ACMD_UNLOCK);
	STEN_LOW;
#endif
	return;
}

/*
 * Read a value from the drive.  Should return quickly, so a busy wait
 * is used to avoid excessive rescheduling. The read command itself must
 * be issued with aztSendCmd() directly before
 */
static int aztGetValue(unsigned char *result)
{
	int s;

	STEN_LOW;
	if (aztTimeOutCount >= AZT_TIMEOUT) {
		printk("aztcd: aztGetValue timeout\n");
		return -1;
	}
	s = inb(DATA_PORT) & 0xFF;
	*result = (unsigned char) s;
	return 0;
}

/*
 * Read the current Q-channel info.  Also used for reading the
 * table of contents.
 */
static int aztGetQChannelInfo(struct azt_Toc *qp)
{
	unsigned char notUsed;
	int st;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetQChannelInfo  Time:%li\n", jiffies);
#endif
	if ((st = getAztStatus()) == -1)
		RETURNM("aztGetQChannelInfo 1", -1);
	if (aztSendCmd(ACMD_GET_Q_CHANNEL))
		RETURNM("aztGetQChannelInfo 2", -1);
	/*STEN_LOW_WAIT; ??? Dosemu0.60's cdrom.c does not like STEN_LOW_WAIT here */
	if (aztGetValue(&notUsed))
		RETURNM("aztGetQChannelInfo 3", -1);	/*??? Nullbyte einlesen */
	if ((st & AST_MODE_BITS) == AST_INITIAL) {
		qp->ctrl_addr = 0;	/* when audio stop ACMD_GET_Q_CHANNEL returns */
		qp->track = 0;	/* only one byte with Aztech drives */
		qp->pointIndex = 0;
		qp->trackTime.min = 0;
		qp->trackTime.sec = 0;
		qp->trackTime.frame = 0;
		qp->diskTime.min = 0;
		qp->diskTime.sec = 0;
		qp->diskTime.frame = 0;
		return 0;
	} else {
		if (aztGetValue(&qp->ctrl_addr) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->track) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->pointIndex) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->trackTime.min) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->trackTime.sec) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->trackTime.frame) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&notUsed) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->diskTime.min) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->diskTime.sec) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
		if (aztGetValue(&qp->diskTime.frame) < 0)
			RETURNM("aztGetQChannelInfo 4", -1);
	}
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztGetQChannelInfo  Time:%li\n", jiffies);
#endif
	return 0;
}

/*
 * Read the table of contents (TOC) and TOC header if necessary
 */
static int aztUpdateToc(void)
{
	int st;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztUpdateToc  Time:%li\n", jiffies);
#endif
	if (aztTocUpToDate)
		return 0;

	if (aztGetDiskInfo() < 0)
		return -EIO;

	if (aztGetToc(0) < 0)
		return -EIO;

	/*audio disk detection
	   with my Aztech drive there is no audio status bit, so I use the copy
	   protection bit of the first track. If this track is copy protected 
	   (copy bit = 0), I assume, it's an audio  disk. Strange, but works ??? */
	if (!(Toc[DiskInfo.first].ctrl_addr & 0x40))
		DiskInfo.audio = 1;
	else
		DiskInfo.audio = 0;

	/* XA detection */
	if (!DiskInfo.audio) {
		azt_Play.start.min = 0;	/*XA detection only seems to work */
		azt_Play.start.sec = 2;	/*when we play a track */
		azt_Play.start.frame = 0;
		azt_Play.end.min = 0;
		azt_Play.end.sec = 0;
		azt_Play.end.frame = 1;
		if (sendAztCmd(ACMD_PLAY_READ, &azt_Play))
			return -1;
		DTEN_LOW;
		for (st = 0; st < CD_FRAMESIZE; st++)
			inb(DATA_PORT);
	}
	DiskInfo.xa = getAztStatus() & AST_MODE;
	if (DiskInfo.xa) {
		printk
		    ("aztcd: XA support experimental - mail results to Werner.Zimmermann@fht-esslingen.de\n");
	}

	/*multisession detection
	   support for multisession CDs is done automatically with Aztech drives,
	   we don't have to take care about TOC redirection; if we want the isofs
	   to take care about redirection, we have to set AZT_MULTISESSION to 1 */
	DiskInfo.multi = 0;
#if AZT_MULTISESSION
	if (DiskInfo.xa) {
		aztGetMultiDiskInfo();	/*here Disk.Info.multi is set */
	}
#endif
	if (DiskInfo.multi) {
		DiskInfo.lastSession.min = Toc[DiskInfo.next].diskTime.min;
		DiskInfo.lastSession.sec = Toc[DiskInfo.next].diskTime.sec;
		DiskInfo.lastSession.frame =
		    Toc[DiskInfo.next].diskTime.frame;
		printk("aztcd: Multisession support experimental\n");
	} else {
		DiskInfo.lastSession.min =
		    Toc[DiskInfo.first].diskTime.min;
		DiskInfo.lastSession.sec =
		    Toc[DiskInfo.first].diskTime.sec;
		DiskInfo.lastSession.frame =
		    Toc[DiskInfo.first].diskTime.frame;
	}

	aztTocUpToDate = 1;
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztUpdateToc  Time:%li\n", jiffies);
#endif
	return 0;
}


/* Read the table of contents header, i.e. no. of tracks and start of first 
 * track
 */
static int aztGetDiskInfo(void)
{
	int limit;
	unsigned char test;
	struct azt_Toc qInfo;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetDiskInfo  Time:%li\n", jiffies);
#endif
	if (aztSendCmd(ACMD_SEEK_TO_LEADIN))
		RETURNM("aztGetDiskInfo 1", -1);
	STEN_LOW_WAIT;
	test = 0;
	for (limit = 300; limit > 0; limit--) {
		if (aztGetQChannelInfo(&qInfo) < 0)
			RETURNM("aztGetDiskInfo 2", -1);
		if (qInfo.pointIndex == 0xA0) {	/*Number of FirstTrack */
			DiskInfo.first = qInfo.diskTime.min;
			DiskInfo.first = azt_bcd2bin(DiskInfo.first);
			test = test | 0x01;
		}
		if (qInfo.pointIndex == 0xA1) {	/*Number of LastTrack */
			DiskInfo.last = qInfo.diskTime.min;
			DiskInfo.last = azt_bcd2bin(DiskInfo.last);
			test = test | 0x02;
		}
		if (qInfo.pointIndex == 0xA2) {	/*DiskLength */
			DiskInfo.diskLength.min = qInfo.diskTime.min;
			DiskInfo.diskLength.sec = qInfo.diskTime.sec;
			DiskInfo.diskLength.frame = qInfo.diskTime.frame;
			test = test | 0x04;
		}
		if ((qInfo.pointIndex == DiskInfo.first) && (test & 0x01)) {	/*StartTime of First Track */
			DiskInfo.firstTrack.min = qInfo.diskTime.min;
			DiskInfo.firstTrack.sec = qInfo.diskTime.sec;
			DiskInfo.firstTrack.frame = qInfo.diskTime.frame;
			test = test | 0x08;
		}
		if (test == 0x0F)
			break;
	}
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztGetDiskInfo  Time:%li\n", jiffies);
	printk
	    ("Disk Info: first %d last %d length %02X:%02X.%02X dez  first %02X:%02X.%02X dez\n",
	     DiskInfo.first, DiskInfo.last, DiskInfo.diskLength.min,
	     DiskInfo.diskLength.sec, DiskInfo.diskLength.frame,
	     DiskInfo.firstTrack.min, DiskInfo.firstTrack.sec,
	     DiskInfo.firstTrack.frame);
#endif
	if (test != 0x0F)
		return -1;
	return 0;
}

#if AZT_MULTISESSION
/*
 * Get Multisession Disk Info
 */
static int aztGetMultiDiskInfo(void)
{
	int limit, k = 5;
	unsigned char test;
	struct azt_Toc qInfo;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetMultiDiskInfo\n");
#endif

	do {
		azt_Play.start.min = Toc[DiskInfo.last + 1].diskTime.min;
		azt_Play.start.sec = Toc[DiskInfo.last + 1].diskTime.sec;
		azt_Play.start.frame =
		    Toc[DiskInfo.last + 1].diskTime.frame;
		test = 0;

		for (limit = 30; limit > 0; limit--) {	/*Seek for LeadIn of next session */
			if (aztSeek(&azt_Play))
				RETURNM("aztGetMultiDiskInfo 1", -1);
			if (aztGetQChannelInfo(&qInfo) < 0)
				RETURNM("aztGetMultiDiskInfo 2", -1);
			if ((qInfo.track == 0) && (qInfo.pointIndex))
				break;	/*LeadIn found */
			if ((azt_Play.start.sec += 10) > 59) {
				azt_Play.start.sec = 0;
				azt_Play.start.min++;
			}
		}
		if (!limit)
			break;	/*Check, if a leadin track was found, if not we're
				   at the end of the disk */
#ifdef AZT_DEBUG_MULTISESSION
		printk("leadin found track %d  pointIndex %x  limit %d\n",
		       qInfo.track, qInfo.pointIndex, limit);
#endif
		for (limit = 300; limit > 0; limit--) {
			if (++azt_Play.start.frame > 74) {
				azt_Play.start.frame = 0;
				if (azt_Play.start.sec > 59) {
					azt_Play.start.sec = 0;
					azt_Play.start.min++;
				}
			}
			if (aztSeek(&azt_Play))
				RETURNM("aztGetMultiDiskInfo 3", -1);
			if (aztGetQChannelInfo(&qInfo) < 0)
				RETURNM("aztGetMultiDiskInfo 4", -1);
			if (qInfo.pointIndex == 0xA0) {	/*Number of NextTrack */
				DiskInfo.next = qInfo.diskTime.min;
				DiskInfo.next = azt_bcd2bin(DiskInfo.next);
				test = test | 0x01;
			}
			if (qInfo.pointIndex == 0xA1) {	/*Number of LastTrack */
				DiskInfo.last = qInfo.diskTime.min;
				DiskInfo.last = azt_bcd2bin(DiskInfo.last);
				test = test | 0x02;
			}
			if (qInfo.pointIndex == 0xA2) {	/*DiskLength */
				DiskInfo.diskLength.min =
				    qInfo.diskTime.min;
				DiskInfo.diskLength.sec =
				    qInfo.diskTime.sec;
				DiskInfo.diskLength.frame =
				    qInfo.diskTime.frame;
				test = test | 0x04;
			}
			if ((qInfo.pointIndex == DiskInfo.next) && (test & 0x01)) {	/*StartTime of Next Track */
				DiskInfo.nextSession.min =
				    qInfo.diskTime.min;
				DiskInfo.nextSession.sec =
				    qInfo.diskTime.sec;
				DiskInfo.nextSession.frame =
				    qInfo.diskTime.frame;
				test = test | 0x08;
			}
			if (test == 0x0F)
				break;
		}
#ifdef AZT_DEBUG_MULTISESSION
		printk
		    ("MultiDisk Info: first %d next %d last %d length %02x:%02x.%02x dez  first %02x:%02x.%02x dez  next %02x:%02x.%02x dez\n",
		     DiskInfo.first, DiskInfo.next, DiskInfo.last,
		     DiskInfo.diskLength.min, DiskInfo.diskLength.sec,
		     DiskInfo.diskLength.frame, DiskInfo.firstTrack.min,
		     DiskInfo.firstTrack.sec, DiskInfo.firstTrack.frame,
		     DiskInfo.nextSession.min, DiskInfo.nextSession.sec,
		     DiskInfo.nextSession.frame);
#endif
		if (test != 0x0F)
			break;
		else
			DiskInfo.multi = 1;	/*found TOC of more than one session */
		aztGetToc(1);
	} while (--k);

#ifdef AZT_DEBUG
	printk("aztcd: exiting aztGetMultiDiskInfo  Time:%li\n", jiffies);
#endif
	return 0;
}
#endif

/*
 * Read the table of contents (TOC)
 */
static int aztGetToc(int multi)
{
	int i, px;
	int limit;
	struct azt_Toc qInfo;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztGetToc  Time:%li\n", jiffies);
#endif
	if (!multi) {
		for (i = 0; i < MAX_TRACKS; i++)
			Toc[i].pointIndex = 0;
		i = DiskInfo.last + 3;
	} else {
		for (i = DiskInfo.next; i < MAX_TRACKS; i++)
			Toc[i].pointIndex = 0;
		i = DiskInfo.last + 4 - DiskInfo.next;
	}

/*Is there a good reason to stop motor before TOC read?
  if (aztSendCmd(ACMD_STOP)) RETURNM("aztGetToc 1",-1);
      STEN_LOW_WAIT;
*/

	if (!multi) {
		azt_mode = 0x05;
		if (aztSendCmd(ACMD_SEEK_TO_LEADIN))
			RETURNM("aztGetToc 2", -1);
		STEN_LOW_WAIT;
	}
	for (limit = 300; limit > 0; limit--) {
		if (multi) {
			if (++azt_Play.start.sec > 59) {
				azt_Play.start.sec = 0;
				azt_Play.start.min++;
			}
			if (aztSeek(&azt_Play))
				RETURNM("aztGetToc 3", -1);
		}
		if (aztGetQChannelInfo(&qInfo) < 0)
			break;

		px = azt_bcd2bin(qInfo.pointIndex);

		if (px > 0 && px < MAX_TRACKS && qInfo.track == 0)
			if (Toc[px].pointIndex == 0) {
				Toc[px] = qInfo;
				i--;
			}

		if (i <= 0)
			break;
	}

	Toc[DiskInfo.last + 1].diskTime = DiskInfo.diskLength;
	Toc[DiskInfo.last].trackTime = DiskInfo.diskLength;

#ifdef AZT_DEBUG_MULTISESSION
	printk("aztcd: exiting aztGetToc\n");
	for (i = 1; i <= DiskInfo.last + 1; i++)
		printk
		    ("i = %2d ctl-adr = %02X track %2d px %02X %02X:%02X.%02X dez  %02X:%02X.%02X dez\n",
		     i, Toc[i].ctrl_addr, Toc[i].track, Toc[i].pointIndex,
		     Toc[i].trackTime.min, Toc[i].trackTime.sec,
		     Toc[i].trackTime.frame, Toc[i].diskTime.min,
		     Toc[i].diskTime.sec, Toc[i].diskTime.frame);
	for (i = 100; i < 103; i++)
		printk
		    ("i = %2d ctl-adr = %02X track %2d px %02X %02X:%02X.%02X dez  %02X:%02X.%02X dez\n",
		     i, Toc[i].ctrl_addr, Toc[i].track, Toc[i].pointIndex,
		     Toc[i].trackTime.min, Toc[i].trackTime.sec,
		     Toc[i].trackTime.frame, Toc[i].diskTime.min,
		     Toc[i].diskTime.sec, Toc[i].diskTime.frame);
#endif

	return limit > 0 ? 0 : -1;
}


/*##########################################################################
  Kernel Interface Functions
  ##########################################################################
*/

#ifndef MODULE
static int __init aztcd_setup(char *str)
{
	int ints[4];

	(void) get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] > 0)
		azt_port = ints[1];
	if (ints[1] > 1)
		azt_cont = ints[2];
	return 1;
}

__setup("aztcd=", aztcd_setup);

#endif				/* !MODULE */

/* 
 * Checking if the media has been changed
*/
static int check_aztcd_media_change(struct gendisk *disk)
{
	if (aztDiskChanged) {	/* disk changed */
		aztDiskChanged = 0;
		return 1;
	} else
		return 0;	/* no change */
}

/*
 * Kernel IO-controls
*/
static int aztcd_ioctl(struct inode *ip, struct file *fp, unsigned int cmd,
		       unsigned long arg)
{
	int i;
	struct azt_Toc qInfo;
	struct cdrom_ti ti;
	struct cdrom_tochdr tocHdr;
	struct cdrom_msf msf;
	struct cdrom_tocentry entry;
	struct azt_Toc *tocPtr;
	struct cdrom_subchnl subchnl;
	struct cdrom_volctrl volctrl;
	void __user *argp = (void __user *)arg;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztcd_ioctl - Command:%x   Time: %li\n",
	       cmd, jiffies);
	printk("aztcd Status %x\n", getAztStatus());
#endif
	if (!ip)
		RETURNM("aztcd_ioctl 1", -EINVAL);
	if (getAztStatus() < 0)
		RETURNM("aztcd_ioctl 2", -EIO);
	if ((!aztTocUpToDate) || (aztDiskChanged)) {
		if ((i = aztUpdateToc()) < 0)
			RETURNM("aztcd_ioctl 3", i);	/* error reading TOC */
	}

	switch (cmd) {
	case CDROMSTART:	/* Spin up the drive. Don't know, what to do,
				   at least close the tray */
#if AZT_PRIVATE_IOCTLS
		if (aztSendCmd(ACMD_CLOSE))
			RETURNM("aztcd_ioctl 4", -1);
		STEN_LOW_WAIT;
#endif
		break;
	case CDROMSTOP:	/* Spin down the drive */
		if (aztSendCmd(ACMD_STOP))
			RETURNM("aztcd_ioctl 5", -1);
		STEN_LOW_WAIT;
		/* should we do anything if it fails? */
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		break;
	case CDROMPAUSE:	/* Pause the drive */
		if (aztAudioStatus != CDROM_AUDIO_PLAY)
			return -EINVAL;

		if (aztGetQChannelInfo(&qInfo) < 0) {	/* didn't get q channel info */
			aztAudioStatus = CDROM_AUDIO_NO_STATUS;
			RETURNM("aztcd_ioctl 7", 0);
		}
		azt_Play.start = qInfo.diskTime;	/* remember restart point */

		if (aztSendCmd(ACMD_PAUSE))
			RETURNM("aztcd_ioctl 8", -1);
		STEN_LOW_WAIT;
		aztAudioStatus = CDROM_AUDIO_PAUSED;
		break;
	case CDROMRESUME:	/* Play it again, Sam */
		if (aztAudioStatus != CDROM_AUDIO_PAUSED)
			return -EINVAL;
		/* restart the drive at the saved position. */
		i = aztPlay(&azt_Play);
		if (i < 0) {
			aztAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;
	case CDROMMULTISESSION:	/*multisession support -- experimental */
		{
			struct cdrom_multisession ms;
#ifdef AZT_DEBUG
			printk("aztcd ioctl MULTISESSION\n");
#endif
			if (copy_from_user(&ms, argp,
			     sizeof(struct cdrom_multisession)))
				return -EFAULT;
			if (ms.addr_format == CDROM_MSF) {
				ms.addr.msf.minute =
				    azt_bcd2bin(DiskInfo.lastSession.min);
				ms.addr.msf.second =
				    azt_bcd2bin(DiskInfo.lastSession.sec);
				ms.addr.msf.frame =
				    azt_bcd2bin(DiskInfo.lastSession.
						frame);
			} else if (ms.addr_format == CDROM_LBA)
				ms.addr.lba =
				    azt_msf2hsg(&DiskInfo.lastSession);
			else
				return -EINVAL;
			ms.xa_flag = DiskInfo.xa;
			if (copy_to_user(argp, &ms,
			     sizeof(struct cdrom_multisession)))
				return -EFAULT;
#ifdef AZT_DEBUG
			if (ms.addr_format == CDROM_MSF)
				printk
				    ("aztcd multisession xa:%d, msf:%02x:%02x.%02x [%02x:%02x.%02x])\n",
				     ms.xa_flag, ms.addr.msf.minute,
				     ms.addr.msf.second, ms.addr.msf.frame,
				     DiskInfo.lastSession.min,
				     DiskInfo.lastSession.sec,
				     DiskInfo.lastSession.frame);
			else
				printk
				    ("aztcd multisession %d, lba:0x%08x [%02x:%02x.%02x])\n",
				     ms.xa_flag, ms.addr.lba,
				     DiskInfo.lastSession.min,
				     DiskInfo.lastSession.sec,
				     DiskInfo.lastSession.frame);
#endif
			return 0;
		}
	case CDROMPLAYTRKIND:	/* Play a track.  This currently ignores index. */
		if (copy_from_user(&ti, argp, sizeof ti))
			return -EFAULT;
		if (ti.cdti_trk0 < DiskInfo.first
		    || ti.cdti_trk0 > DiskInfo.last
		    || ti.cdti_trk1 < ti.cdti_trk0) {
			return -EINVAL;
		}
		if (ti.cdti_trk1 > DiskInfo.last)
			ti.cdti_trk1 = DiskInfo.last;
		azt_Play.start = Toc[ti.cdti_trk0].diskTime;
		azt_Play.end = Toc[ti.cdti_trk1 + 1].diskTime;
#ifdef AZT_DEBUG
		printk("aztcd play: %02x:%02x.%02x to %02x:%02x.%02x\n",
		       azt_Play.start.min, azt_Play.start.sec,
		       azt_Play.start.frame, azt_Play.end.min,
		       azt_Play.end.sec, azt_Play.end.frame);
#endif
		i = aztPlay(&azt_Play);
		if (i < 0) {
			aztAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;
	case CDROMPLAYMSF:	/* Play starting at the given MSF address. */
/*              if (aztAudioStatus == CDROM_AUDIO_PLAY) 
		{ if (aztSendCmd(ACMD_STOP)) RETURNM("aztcd_ioctl 9",-1);
		  STEN_LOW;
		  aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		}
*/
		if (copy_from_user(&msf, argp, sizeof msf))
			return -EFAULT;
		/* convert to bcd */
		azt_bin2bcd(&msf.cdmsf_min0);
		azt_bin2bcd(&msf.cdmsf_sec0);
		azt_bin2bcd(&msf.cdmsf_frame0);
		azt_bin2bcd(&msf.cdmsf_min1);
		azt_bin2bcd(&msf.cdmsf_sec1);
		azt_bin2bcd(&msf.cdmsf_frame1);
		azt_Play.start.min = msf.cdmsf_min0;
		azt_Play.start.sec = msf.cdmsf_sec0;
		azt_Play.start.frame = msf.cdmsf_frame0;
		azt_Play.end.min = msf.cdmsf_min1;
		azt_Play.end.sec = msf.cdmsf_sec1;
		azt_Play.end.frame = msf.cdmsf_frame1;
#ifdef AZT_DEBUG
		printk("aztcd play: %02x:%02x.%02x to %02x:%02x.%02x\n",
		       azt_Play.start.min, azt_Play.start.sec,
		       azt_Play.start.frame, azt_Play.end.min,
		       azt_Play.end.sec, azt_Play.end.frame);
#endif
		i = aztPlay(&azt_Play);
		if (i < 0) {
			aztAudioStatus = CDROM_AUDIO_ERROR;
			return -EIO;
		}
		aztAudioStatus = CDROM_AUDIO_PLAY;
		break;

	case CDROMREADTOCHDR:	/* Read the table of contents header */
		tocHdr.cdth_trk0 = DiskInfo.first;
		tocHdr.cdth_trk1 = DiskInfo.last;
		if (copy_to_user(argp, &tocHdr, sizeof tocHdr))
			return -EFAULT;
		break;
	case CDROMREADTOCENTRY:	/* Read an entry in the table of contents */
		if (copy_from_user(&entry, argp, sizeof entry))
			return -EFAULT;
		if ((!aztTocUpToDate) || aztDiskChanged)
			aztUpdateToc();
		if (entry.cdte_track == CDROM_LEADOUT)
			tocPtr = &Toc[DiskInfo.last + 1];
		else if (entry.cdte_track > DiskInfo.last
			 || entry.cdte_track < DiskInfo.first) {
			return -EINVAL;
		} else
			tocPtr = &Toc[entry.cdte_track];
		entry.cdte_adr = tocPtr->ctrl_addr;
		entry.cdte_ctrl = tocPtr->ctrl_addr >> 4;
		if (entry.cdte_format == CDROM_LBA)
			entry.cdte_addr.lba =
			    azt_msf2hsg(&tocPtr->diskTime);
		else if (entry.cdte_format == CDROM_MSF) {
			entry.cdte_addr.msf.minute =
			    azt_bcd2bin(tocPtr->diskTime.min);
			entry.cdte_addr.msf.second =
			    azt_bcd2bin(tocPtr->diskTime.sec);
			entry.cdte_addr.msf.frame =
			    azt_bcd2bin(tocPtr->diskTime.frame);
		} else {
			return -EINVAL;
		}
		if (copy_to_user(argp, &entry, sizeof entry))
			return -EFAULT;
		break;
	case CDROMSUBCHNL:	/* Get subchannel info */
		if (copy_from_user
		    (&subchnl, argp, sizeof(struct cdrom_subchnl)))
			return -EFAULT;
		if (aztGetQChannelInfo(&qInfo) < 0) {
#ifdef AZT_DEBUG
			printk
			    ("aztcd: exiting aztcd_ioctl - Error 3 - Command:%x\n",
			     cmd);
#endif
			return -EIO;
		}
		subchnl.cdsc_audiostatus = aztAudioStatus;
		subchnl.cdsc_adr = qInfo.ctrl_addr;
		subchnl.cdsc_ctrl = qInfo.ctrl_addr >> 4;
		subchnl.cdsc_trk = azt_bcd2bin(qInfo.track);
		subchnl.cdsc_ind = azt_bcd2bin(qInfo.pointIndex);
		if (subchnl.cdsc_format == CDROM_LBA) {
			subchnl.cdsc_absaddr.lba =
			    azt_msf2hsg(&qInfo.diskTime);
			subchnl.cdsc_reladdr.lba =
			    azt_msf2hsg(&qInfo.trackTime);
		} else {	/*default */
			subchnl.cdsc_format = CDROM_MSF;
			subchnl.cdsc_absaddr.msf.minute =
			    azt_bcd2bin(qInfo.diskTime.min);
			subchnl.cdsc_absaddr.msf.second =
			    azt_bcd2bin(qInfo.diskTime.sec);
			subchnl.cdsc_absaddr.msf.frame =
			    azt_bcd2bin(qInfo.diskTime.frame);
			subchnl.cdsc_reladdr.msf.minute =
			    azt_bcd2bin(qInfo.trackTime.min);
			subchnl.cdsc_reladdr.msf.second =
			    azt_bcd2bin(qInfo.trackTime.sec);
			subchnl.cdsc_reladdr.msf.frame =
			    azt_bcd2bin(qInfo.trackTime.frame);
		}
		if (copy_to_user(argp, &subchnl, sizeof(struct cdrom_subchnl)))
			return -EFAULT;
		break;
	case CDROMVOLCTRL:	/* Volume control 
				   * With my Aztech CD268-01A volume control does not work, I can only
				   turn the channels on (any value !=0) or off (value==0). Maybe it
				   works better with your drive */
		if (copy_from_user(&volctrl, argp, sizeof(volctrl)))
			return -EFAULT;
		azt_Play.start.min = 0x21;
		azt_Play.start.sec = 0x84;
		azt_Play.start.frame = volctrl.channel0;
		azt_Play.end.min = volctrl.channel1;
		azt_Play.end.sec = volctrl.channel2;
		azt_Play.end.frame = volctrl.channel3;
		sendAztCmd(ACMD_SET_VOLUME, &azt_Play);
		STEN_LOW_WAIT;
		break;
	case CDROMEJECT:
		aztUnlockDoor();	/* Assume user knows what they're doing */
		/* all drives can at least stop! */
		if (aztAudioStatus == CDROM_AUDIO_PLAY) {
			if (aztSendCmd(ACMD_STOP))
				RETURNM("azt_ioctl 10", -1);
			STEN_LOW_WAIT;
		}
		if (aztSendCmd(ACMD_EJECT))
			RETURNM("azt_ioctl 11", -1);
		STEN_LOW_WAIT;
		aztAudioStatus = CDROM_AUDIO_NO_STATUS;
		break;
	case CDROMEJECT_SW:
		azt_auto_eject = (char) arg;
		break;
	case CDROMRESET:
		outb(ACMD_SOFT_RESET, CMD_PORT);	/*send reset */
		STEN_LOW;
		if (inb(DATA_PORT) != AFL_OP_OK) {	/*OP_OK? */
			printk
			    ("aztcd: AZTECH CD-ROM drive does not respond\n");
		}
		break;
/*Take care, the following code is not compatible with other CD-ROM drivers,
  use it at your own risk with cdplay.c. Set AZT_PRIVATE_IOCTLS to 0 in aztcd.h,
  if you do not want to use it!
*/
#if AZT_PRIVATE_IOCTLS
	case CDROMREADCOOKED:	/*read data in mode 1 (2048 Bytes) */
	case CDROMREADRAW:	/*read data in mode 2 (2336 Bytes) */
		{
			if (copy_from_user(&msf, argp, sizeof msf))
				return -EFAULT;
			/* convert to bcd */
			azt_bin2bcd(&msf.cdmsf_min0);
			azt_bin2bcd(&msf.cdmsf_sec0);
			azt_bin2bcd(&msf.cdmsf_frame0);
			msf.cdmsf_min1 = 0;
			msf.cdmsf_sec1 = 0;
			msf.cdmsf_frame1 = 1;	/*read only one frame */
			azt_Play.start.min = msf.cdmsf_min0;
			azt_Play.start.sec = msf.cdmsf_sec0;
			azt_Play.start.frame = msf.cdmsf_frame0;
			azt_Play.end.min = msf.cdmsf_min1;
			azt_Play.end.sec = msf.cdmsf_sec1;
			azt_Play.end.frame = msf.cdmsf_frame1;
			if (cmd == CDROMREADRAW) {
				if (DiskInfo.xa) {
					return -1;	/*XA Disks can't be read raw */
				} else {
					if (sendAztCmd(ACMD_PLAY_READ_RAW, &azt_Play))
						return -1;
					DTEN_LOW;
					insb(DATA_PORT, buf, CD_FRAMESIZE_RAW);
					if (copy_to_user(argp, &buf, CD_FRAMESIZE_RAW))
						return -EFAULT;
				}
			} else
				/*CDROMREADCOOKED*/ {
				if (sendAztCmd(ACMD_PLAY_READ, &azt_Play))
					return -1;
				DTEN_LOW;
				insb(DATA_PORT, buf, CD_FRAMESIZE);
				if (copy_to_user(argp, &buf, CD_FRAMESIZE))
					return -EFAULT;
				}
		}
		break;
	case CDROMSEEK:	/*seek msf address */
		if (copy_from_user(&msf, argp, sizeof msf))
			return -EFAULT;
		/* convert to bcd */
		azt_bin2bcd(&msf.cdmsf_min0);
		azt_bin2bcd(&msf.cdmsf_sec0);
		azt_bin2bcd(&msf.cdmsf_frame0);
		azt_Play.start.min = msf.cdmsf_min0;
		azt_Play.start.sec = msf.cdmsf_sec0;
		azt_Play.start.frame = msf.cdmsf_frame0;
		if (aztSeek(&azt_Play))
			return -1;
		break;
#endif				/*end of incompatible code */
	case CDROMREADMODE1:	/*set read data in mode 1 */
		return aztSetDiskType(AZT_MODE_1);
	case CDROMREADMODE2:	/*set read data in mode 2 */
		return aztSetDiskType(AZT_MODE_2);
	default:
		return -EINVAL;
	}
#ifdef AZT_DEBUG
	printk("aztcd: exiting aztcd_ioctl Command:%x  Time:%li\n", cmd,
	       jiffies);
#endif
	return 0;
}

/*
 * Take care of the different block sizes between cdrom and Linux.
 * When Linux gets variable block sizes this will probably go away.
 */
static void azt_transfer(void)
{
#ifdef AZT_TEST
	printk("aztcd: executing azt_transfer Time:%li\n", jiffies);
#endif
	if (!current_valid())
	        return;

	while (CURRENT->nr_sectors) {
		int bn = CURRENT->sector / 4;
		int i;
		for (i = 0; i < AZT_BUF_SIZ && azt_buf_bn[i] != bn; ++i);
		if (i < AZT_BUF_SIZ) {
			int offs = (i * 4 + (CURRENT->sector & 3)) * 512;
			int nr_sectors = 4 - (CURRENT->sector & 3);
			if (azt_buf_out != i) {
				azt_buf_out = i;
				if (azt_buf_bn[i] != bn) {
					azt_buf_out = -1;
					continue;
				}
			}
			if (nr_sectors > CURRENT->nr_sectors)
			    nr_sectors = CURRENT->nr_sectors;
			memcpy(CURRENT->buffer, azt_buf + offs,
				nr_sectors * 512);
			CURRENT->nr_sectors -= nr_sectors;
			CURRENT->sector += nr_sectors;
			CURRENT->buffer += nr_sectors * 512;
		} else {
			azt_buf_out = -1;
			break;
		}
	}
}

static void do_aztcd_request(request_queue_t * q)
{
#ifdef AZT_TEST
	printk(" do_aztcd_request(%ld+%ld) Time:%li\n", CURRENT->sector,
	       CURRENT->nr_sectors, jiffies);
#endif
	if (DiskInfo.audio) {
		printk("aztcd: Error, tried to mount an Audio CD\n");
		end_request(CURRENT, 0);
		return;
	}
	azt_transfer_is_active = 1;
	while (current_valid()) {
		azt_transfer();
		if (CURRENT->nr_sectors == 0) {
			end_request(CURRENT, 1);
		} else {
			azt_buf_out = -1;	/* Want to read a block not in buffer */
			if (azt_state == AZT_S_IDLE) {
				if ((!aztTocUpToDate) || aztDiskChanged) {
					if (aztUpdateToc() < 0) {
						while (current_valid())
							end_request(CURRENT, 0);
						break;
					}
				}
				azt_state = AZT_S_START;
				AztTries = 5;
				SET_TIMER(azt_poll, HZ / 100);
			}
			break;
		}
	}
	azt_transfer_is_active = 0;
#ifdef AZT_TEST2
	printk
	    ("azt_next_bn:%x  azt_buf_in:%x azt_buf_out:%x  azt_buf_bn:%x\n",
	     azt_next_bn, azt_buf_in, azt_buf_out, azt_buf_bn[azt_buf_in]);
	printk(" do_aztcd_request ends  Time:%li\n", jiffies);
#endif
}


static void azt_invalidate_buffers(void)
{
	int i;

#ifdef AZT_DEBUG
	printk("aztcd: executing azt_invalidate_buffers\n");
#endif
	for (i = 0; i < AZT_BUF_SIZ; ++i)
		azt_buf_bn[i] = -1;
	azt_buf_out = -1;
}

/*
 * Open the device special file.  Check that a disk is in.
 */
static int aztcd_open(struct inode *ip, struct file *fp)
{
	int st;

#ifdef AZT_DEBUG
	printk("aztcd: starting aztcd_open\n");
#endif

	if (aztPresent == 0)
		return -ENXIO;	/* no hardware */

	if (!azt_open_count && azt_state == AZT_S_IDLE) {
		azt_invalidate_buffers();

		st = getAztStatus();	/* check drive status */
		if (st == -1)
			goto err_out;	/* drive doesn't respond */

		if (st & AST_DOOR_OPEN) {	/* close door, then get the status again. */
			printk("aztcd: Door Open?\n");
			aztCloseDoor();
			st = getAztStatus();
		}

		if ((st & AST_NOT_READY) || (st & AST_DSK_CHG)) {	/*no disk in drive or changed */
			printk
			    ("aztcd: Disk Changed or No Disk in Drive?\n");
			aztTocUpToDate = 0;
		}
		if (aztUpdateToc())
			goto err_out;

	}
	++azt_open_count;
	aztLockDoor();

#ifdef AZT_DEBUG
	printk("aztcd: exiting aztcd_open\n");
#endif
	return 0;

      err_out:
	return -EIO;
}


/*
 * On close, we flush all azt blocks from the buffer cache.
 */
static int aztcd_release(struct inode *inode, struct file *file)
{
#ifdef AZT_DEBUG
	printk("aztcd: executing aztcd_release\n");
	printk("inode: %p, device: %s    file: %p\n", inode,
	       inode->i_bdev->bd_disk->disk_name, file);
#endif
	if (!--azt_open_count) {
		azt_invalidate_buffers();
		aztUnlockDoor();
		if (azt_auto_eject)
			aztSendCmd(ACMD_EJECT);
		CLEAR_TIMER;
	}
	return 0;
}

static struct gendisk *azt_disk;

/*
 * Test for presence of drive and initialize it.  Called at boot time.
 */

static int __init aztcd_init(void)
{
	long int count, max_count;
	unsigned char result[50];
	int st;
	void* status = NULL;
	int i = 0;
	int ret = 0;

	if (azt_port == 0) {
		printk(KERN_INFO "aztcd: no Aztech CD-ROM Initialization");
		return -EIO;
	}

	printk(KERN_INFO "aztcd: AZTECH, ORCHID, OKANO, WEARNES, TXC, CyDROM "
	       "CD-ROM Driver\n");
	printk(KERN_INFO "aztcd: (C) 1994-98 W.Zimmermann\n");
	if (azt_port == -1) {
		printk
		    ("aztcd: DriverVersion=%s For IDE/ATAPI-drives use ide-cd.c\n",
		     AZT_VERSION);
	} else
		printk
		    ("aztcd: DriverVersion=%s BaseAddress=0x%x  For IDE/ATAPI-drives use ide-cd.c\n",
		     AZT_VERSION, azt_port);
	printk(KERN_INFO "aztcd: If you have problems, read /usr/src/linux/"
	       "Documentation/cdrom/aztcd\n");


#ifdef AZT_SW32			/*CDROM connected to Soundwave32 card */
	if ((0xFF00 & inw(AZT_SW32_ID_REG)) != 0x4500) {
		printk
		    ("aztcd: no Soundwave32 card detected at base:%x init:%x config:%x id:%x\n",
		     AZT_SW32_BASE_ADDR, AZT_SW32_INIT,
		     AZT_SW32_CONFIG_REG, AZT_SW32_ID_REG);
		return -EIO;
	} else {
		printk(KERN_INFO
		       "aztcd: Soundwave32 card detected at %x  Version %x\n",
		       AZT_SW32_BASE_ADDR, inw(AZT_SW32_ID_REG));
		outw(AZT_SW32_INIT, AZT_SW32_CONFIG_REG);
		for (count = 0; count < 10000; count++);	/*delay a bit */
	}
#endif

	/* check for presence of drive */

	if (azt_port == -1) {	/* autoprobing for proprietary interface  */
		for (i = 0; (azt_port_auto[i] != 0) && (i < 16); i++) {
			azt_port = azt_port_auto[i];
			printk(KERN_INFO "aztcd: Autoprobing BaseAddress=0x%x"
			       "\n", azt_port);
			 /*proprietary interfaces need 4 bytes */
			if (!request_region(azt_port, 4, "aztcd")) {
				continue;
			}
			outb(POLLED, MODE_PORT);
			inb(CMD_PORT);
			inb(CMD_PORT);
			outb(ACMD_GET_VERSION, CMD_PORT);	/*Try to get version info */

			aztTimeOutCount = 0;
			do {
				aztIndatum = inb(STATUS_PORT);
				aztTimeOutCount++;
				if (aztTimeOutCount >= AZT_FAST_TIMEOUT)
					break;
			} while (aztIndatum & AFL_STATUS);
			if (inb(DATA_PORT) == AFL_OP_OK) { /* OK drive found */
				break;
			}
			else {  /* Drive not found on this port - try next one */
				release_region(azt_port, 4);
			}
		}
		if ((azt_port_auto[i] == 0) || (i == 16)) {
			printk(KERN_INFO "aztcd: no AZTECH CD-ROM drive found\n");
			return -EIO;
		}
	} else {		/* no autoprobing */
		if ((azt_port == 0x1f0) || (azt_port == 0x170))
			status = request_region(azt_port, 8, "aztcd");	/*IDE-interfaces need 8 bytes */
		else
			status = request_region(azt_port, 4, "aztcd");	/*proprietary interfaces need 4 bytes */
		if (!status) {
			printk(KERN_WARNING "aztcd: conflict, I/O port (%X) "
			       "already used\n", azt_port);
			return -EIO;
		}

		if ((azt_port == 0x1f0) || (azt_port == 0x170))
			SWITCH_IDE_SLAVE;	/*switch IDE interface to slave configuration */

		outb(POLLED, MODE_PORT);
		inb(CMD_PORT);
		inb(CMD_PORT);
		outb(ACMD_GET_VERSION, CMD_PORT);	/*Try to get version info */

		aztTimeOutCount = 0;
		do {
			aztIndatum = inb(STATUS_PORT);
			aztTimeOutCount++;
			if (aztTimeOutCount >= AZT_FAST_TIMEOUT)
				break;
		} while (aztIndatum & AFL_STATUS);

		if (inb(DATA_PORT) != AFL_OP_OK) {	/*OP_OK? If not, reset and try again */
#ifndef MODULE
			if (azt_cont != 0x79) {
				printk(KERN_WARNING "aztcd: no AZTECH CD-ROM "
				       "drive found-Try boot parameter aztcd="
				       "<BaseAddress>,0x79\n");
				ret = -EIO;
				goto err_out;
			}
#else
			if (0) {
			}
#endif
			else {
				printk(KERN_INFO "aztcd: drive reset - "
				       "please wait\n");
				for (count = 0; count < 50; count++) {
					inb(STATUS_PORT);	/*removing all data from earlier tries */
					inb(DATA_PORT);
				}
				outb(POLLED, MODE_PORT);
				inb(CMD_PORT);
				inb(CMD_PORT);
				getAztStatus();	/*trap errors */
				outb(ACMD_SOFT_RESET, CMD_PORT);	/*send reset */
				STEN_LOW;
				if (inb(DATA_PORT) != AFL_OP_OK) {	/*OP_OK? */
					printk(KERN_WARNING "aztcd: no AZTECH "
					       "CD-ROM drive found\n");
					ret = -EIO;
					goto err_out;
				}

				for (count = 0; count < AZT_TIMEOUT;
				     count++)
					barrier();	/* Stop gcc 2.96 being smart */
				/* use udelay(), damnit -- AV */

				if ((st = getAztStatus()) == -1) {
					printk(KERN_WARNING "aztcd: Drive Status"
					       " Error Status=%x\n", st);
					ret = -EIO;
					goto err_out;
				}
#ifdef AZT_DEBUG
				printk(KERN_DEBUG "aztcd: Status = %x\n", st);
#endif
				outb(POLLED, MODE_PORT);
				inb(CMD_PORT);
				inb(CMD_PORT);
				outb(ACMD_GET_VERSION, CMD_PORT);	/*GetVersion */
				STEN_LOW;
				OP_OK;
			}
		}
	}

	azt_init_end = 1;
	STEN_LOW;
	result[0] = inb(DATA_PORT);	/*reading in a null byte??? */
	for (count = 1; count < 50; count++) {	/*Reading version string */
		aztTimeOutCount = 0;	/*here we must implement STEN_LOW differently */
		do {
			aztIndatum = inb(STATUS_PORT);	/*because we want to exit by timeout */
			aztTimeOutCount++;
			if (aztTimeOutCount >= AZT_FAST_TIMEOUT)
				break;
		} while (aztIndatum & AFL_STATUS);
		if (aztTimeOutCount >= AZT_FAST_TIMEOUT)
			break;	/*all chars read? */
		result[count] = inb(DATA_PORT);
	}
	if (count > 30)
		max_count = 30;	/*print max.30 chars of the version string */
	else
		max_count = count;
	printk(KERN_INFO "aztcd: FirmwareVersion=");
	for (count = 1; count < max_count; count++)
		printk("%c", result[count]);
	printk("<<>> ");

	if ((result[1] == 'A') && (result[2] == 'Z') && (result[3] == 'T')) {
		printk("AZTECH drive detected\n");
	/*AZTECH*/}
		else if ((result[2] == 'C') && (result[3] == 'D')
			 && (result[4] == 'D')) {
		printk("ORCHID or WEARNES drive detected\n");	/*ORCHID or WEARNES */
	} else if ((result[1] == 0x03) && (result[2] == '5')) {
		printk("TXC or CyCDROM drive detected\n");	/*Conrad TXC, CyCDROM */
	} else {		/*OTHERS or none */
		printk("\nunknown drive or firmware version detected\n");
		printk
		    ("aztcd may not run stable, if you want to try anyhow,\n");
		printk("boot with: aztcd=<BaseAddress>,0x79\n");
		if ((azt_cont != 0x79)) {
			printk("aztcd: FirmwareVersion=");
			for (count = 1; count < 5; count++)
				printk("%c", result[count]);
			printk("<<>> ");
			printk("Aborted\n");
			ret = -EIO;
			goto err_out;
		}
	}
	azt_disk = alloc_disk(1);
	if (!azt_disk)
		goto err_out;

	if (register_blkdev(MAJOR_NR, "aztcd")) {
		ret = -EIO;
		goto err_out2;
	}

	azt_queue = blk_init_queue(do_aztcd_request, &aztSpin);
	if (!azt_queue) {
		ret = -ENOMEM;
		goto err_out3;
	}

	blk_queue_hardsect_size(azt_queue, 2048);
	azt_disk->major = MAJOR_NR;
	azt_disk->first_minor = 0;
	azt_disk->fops = &azt_fops;
	sprintf(azt_disk->disk_name, "aztcd");
	sprintf(azt_disk->devfs_name, "aztcd");
	azt_disk->queue = azt_queue;
	add_disk(azt_disk);
	azt_invalidate_buffers();
	aztPresent = 1;
	aztCloseDoor();
	return 0;
err_out3:
	unregister_blkdev(MAJOR_NR, "aztcd");
err_out2:
	put_disk(azt_disk);
err_out:
	if ((azt_port == 0x1f0) || (azt_port == 0x170)) {
		SWITCH_IDE_MASTER;
		release_region(azt_port, 8);	/*IDE-interface */
	} else
		release_region(azt_port, 4);	/*proprietary interface */
	return ret;

}

static void __exit aztcd_exit(void)
{
	del_gendisk(azt_disk);
	put_disk(azt_disk);
	if ((unregister_blkdev(MAJOR_NR, "aztcd") == -EINVAL)) {
		printk("What's that: can't unregister aztcd\n");
		return;
	}
	blk_cleanup_queue(azt_queue);
	if ((azt_port == 0x1f0) || (azt_port == 0x170)) {
		SWITCH_IDE_MASTER;
		release_region(azt_port, 8);	/*IDE-interface */
	} else
		release_region(azt_port, 4);	/*proprietary interface */
	printk(KERN_INFO "aztcd module released.\n");
}

module_init(aztcd_init);
module_exit(aztcd_exit);

/*##########################################################################
  Aztcd State Machine: Controls Drive Operating State
  ##########################################################################
*/
static void azt_poll(void)
{
	int st = 0;
	int loop_ctl = 1;
	int skip = 0;

	if (azt_error) {
		if (aztSendCmd(ACMD_GET_ERROR))
			RETURN("azt_poll 1");
		STEN_LOW;
		azt_error = inb(DATA_PORT) & 0xFF;
		printk("aztcd: I/O error 0x%02x\n", azt_error);
		azt_invalidate_buffers();
#ifdef WARN_IF_READ_FAILURE
		if (AztTries == 5)
			printk
			    ("aztcd: Read of Block %d Failed - Maybe Audio Disk?\n",
			     azt_next_bn);
#endif
		if (!AztTries--) {
			printk
			    ("aztcd: Read of Block %d Failed, Maybe Audio Disk? Giving up\n",
			     azt_next_bn);
			if (azt_transfer_is_active) {
				AztTries = 0;
				loop_ctl = 0;
			}
			if (current_valid())
				end_request(CURRENT, 0);
			AztTries = 5;
		}
		azt_error = 0;
		azt_state = AZT_S_STOP;
	}

	while (loop_ctl) {
		loop_ctl = 0;	/* each case must flip this back to 1 if we want
				   to come back up here */
		switch (azt_state) {

		case AZT_S_IDLE:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_IDLE\n");
			}
#endif
			return;

		case AZT_S_START:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_START\n");
			}
#endif
			if (aztSendCmd(ACMD_GET_STATUS))
				RETURN("azt_poll 2");	/*result will be checked by aztStatus() */
			azt_state =
			    azt_mode == 1 ? AZT_S_READ : AZT_S_MODE;
			AztTimeout = 3000;
			break;

		case AZT_S_MODE:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_MODE\n");
			}
#endif
			if (!skip) {
				if ((st = aztStatus()) != -1) {
					if ((st & AST_DSK_CHG)
					    || (st & AST_NOT_READY)) {
						aztDiskChanged = 1;
						aztTocUpToDate = 0;
						azt_invalidate_buffers();
						end_request(CURRENT, 0);
						printk
						    ("aztcd: Disk Changed or Not Ready 1 - Unmount Disk!\n");
					}
				} else
					break;
			}
			skip = 0;

			if ((st & AST_DOOR_OPEN) || (st & AST_NOT_READY)) {
				aztDiskChanged = 1;
				aztTocUpToDate = 0;
				printk
				    ("aztcd: Disk Changed or Not Ready 2 - Unmount Disk!\n");
				end_request(CURRENT, 0);
				printk((st & AST_DOOR_OPEN) ?
				       "aztcd: door open\n" :
				       "aztcd: disk removed\n");
				if (azt_transfer_is_active) {
					azt_state = AZT_S_START;
					loop_ctl = 1;	/* goto immediately */
					break;
				}
				azt_state = AZT_S_IDLE;
				while (current_valid())
					end_request(CURRENT, 0);
				return;
			}

/*	  if (aztSendCmd(ACMD_SET_MODE)) RETURN("azt_poll 3");
	  outb(0x01, DATA_PORT);
	  PA_OK;
	  STEN_LOW;
*/
			if (aztSendCmd(ACMD_GET_STATUS))
				RETURN("azt_poll 4");
			STEN_LOW;
			azt_mode = 1;
			azt_state = AZT_S_READ;
			AztTimeout = 3000;

			break;


		case AZT_S_READ:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_READ\n");
			}
#endif
			if (!skip) {
				if ((st = aztStatus()) != -1) {
					if ((st & AST_DSK_CHG)
					    || (st & AST_NOT_READY)) {
						aztDiskChanged = 1;
						aztTocUpToDate = 0;
						azt_invalidate_buffers();
						printk
						    ("aztcd: Disk Changed or Not Ready 3 - Unmount Disk!\n");
						end_request(CURRENT, 0);
					}
				} else
					break;
			}

			skip = 0;
			if ((st & AST_DOOR_OPEN) || (st & AST_NOT_READY)) {
				aztDiskChanged = 1;
				aztTocUpToDate = 0;
				printk((st & AST_DOOR_OPEN) ?
				       "aztcd: door open\n" :
				       "aztcd: disk removed\n");
				if (azt_transfer_is_active) {
					azt_state = AZT_S_START;
					loop_ctl = 1;
					break;
				}
				azt_state = AZT_S_IDLE;
				while (current_valid())
					end_request(CURRENT, 0);
				return;
			}

			if (current_valid()) {
				struct azt_Play_msf msf;
				int i;
				azt_next_bn = CURRENT->sector / 4;
				azt_hsg2msf(azt_next_bn, &msf.start);
				i = 0;
				/* find out in which track we are */
				while (azt_msf2hsg(&msf.start) >
				       azt_msf2hsg(&Toc[++i].trackTime)) {
				};
				if (azt_msf2hsg(&msf.start) <
				    azt_msf2hsg(&Toc[i].trackTime) -
				    AZT_BUF_SIZ) {
					azt_read_count = AZT_BUF_SIZ;	/*fast, because we read ahead */
					/*azt_read_count=CURRENT->nr_sectors;    slow, no read ahead */
				} else	/* don't read beyond end of track */
#if AZT_MULTISESSION
				{
					azt_read_count =
					    (azt_msf2hsg(&Toc[i].trackTime)
					     / 4) * 4 -
					    azt_msf2hsg(&msf.start);
					if (azt_read_count < 0)
						azt_read_count = 0;
					if (azt_read_count > AZT_BUF_SIZ)
						azt_read_count =
						    AZT_BUF_SIZ;
					printk
					    ("aztcd: warning - trying to read beyond end of track\n");
/*               printk("%i %i %li %li\n",i,azt_read_count,azt_msf2hsg(&msf.start),azt_msf2hsg(&Toc[i].trackTime));
*/ }
#else
				{
					azt_read_count = AZT_BUF_SIZ;
				}
#endif
				msf.end.min = 0;
				msf.end.sec = 0;
				msf.end.frame = azt_read_count;	/*Mitsumi here reads 0xffffff sectors */
#ifdef AZT_TEST3
				printk
				    ("---reading msf-address %x:%x:%x  %x:%x:%x\n",
				     msf.start.min, msf.start.sec,
				     msf.start.frame, msf.end.min,
				     msf.end.sec, msf.end.frame);
				printk
				    ("azt_next_bn:%x  azt_buf_in:%x azt_buf_out:%x  azt_buf_bn:%x\n",
				     azt_next_bn, azt_buf_in, azt_buf_out,
				     azt_buf_bn[azt_buf_in]);
#endif
				if (azt_read_mode == AZT_MODE_2) {
					sendAztCmd(ACMD_PLAY_READ_RAW, &msf);	/*XA disks in raw mode */
				} else {
					sendAztCmd(ACMD_PLAY_READ, &msf);	/*others in cooked mode */
				}
				azt_state = AZT_S_DATA;
				AztTimeout = READ_TIMEOUT;
			} else {
				azt_state = AZT_S_STOP;
				loop_ctl = 1;
				break;
			}

			break;


		case AZT_S_DATA:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_DATA\n");
			}
#endif

			st = inb(STATUS_PORT) & AFL_STATUSorDATA;

			switch (st) {

			case AFL_DATA:
#ifdef AZT_TEST3
				if (st != azt_st_old) {
					azt_st_old = st;
					printk("---AFL_DATA st:%x\n", st);
				}
#endif
				if (!AztTries--) {
					printk
					    ("aztcd: Read of Block %d Failed, Maybe Audio Disk ? Giving up\n",
					     azt_next_bn);
					if (azt_transfer_is_active) {
						AztTries = 0;
						break;
					}
					if (current_valid())
						end_request(CURRENT, 0);
					AztTries = 5;
				}
				azt_state = AZT_S_START;
				AztTimeout = READ_TIMEOUT;
				loop_ctl = 1;
				break;

			case AFL_STATUSorDATA:
#ifdef AZT_TEST3
				if (st != azt_st_old) {
					azt_st_old = st;
					printk
					    ("---AFL_STATUSorDATA st:%x\n",
					     st);
				}
#endif
				break;

			default:
#ifdef AZT_TEST3
				if (st != azt_st_old) {
					azt_st_old = st;
					printk("---default: st:%x\n", st);
				}
#endif
				AztTries = 5;
				if (!current_valid() && azt_buf_in == azt_buf_out) {
					azt_state = AZT_S_STOP;
					loop_ctl = 1;
					break;
				}
				if (azt_read_count <= 0)
					printk
					    ("aztcd: warning - try to read 0 frames\n");
				while (azt_read_count) {	/*??? fast read ahead loop */
					azt_buf_bn[azt_buf_in] = -1;
					DTEN_LOW;	/*??? unsolved problem, very
							   seldom we get timeouts
							   here, don't now the real
							   reason. With my drive this
							   sometimes also happens with
							   Aztech's original driver under
							   DOS. Is it a hardware bug? 
							   I tried to recover from such
							   situations here. Zimmermann */
					if (aztTimeOutCount >= AZT_TIMEOUT) {
						printk
						    ("read_count:%d CURRENT->nr_sectors:%ld azt_buf_in:%d\n",
						     azt_read_count,
						     CURRENT->nr_sectors,
						     azt_buf_in);
						printk
						    ("azt_transfer_is_active:%x\n",
						     azt_transfer_is_active);
						azt_read_count = 0;
						azt_state = AZT_S_STOP;
						loop_ctl = 1;
						end_request(CURRENT, 1);	/*should we have here (1) or (0)? */
					} else {
						if (azt_read_mode ==
						    AZT_MODE_2) {
							insb(DATA_PORT,
							     azt_buf +
							     CD_FRAMESIZE_RAW
							     * azt_buf_in,
							     CD_FRAMESIZE_RAW);
						} else {
							insb(DATA_PORT,
							     azt_buf +
							     CD_FRAMESIZE *
							     azt_buf_in,
							     CD_FRAMESIZE);
						}
						azt_read_count--;
#ifdef AZT_TEST3
						printk
						    ("AZT_S_DATA; ---I've read data- read_count: %d\n",
						     azt_read_count);
						printk
						    ("azt_next_bn:%d  azt_buf_in:%d azt_buf_out:%d  azt_buf_bn:%d\n",
						     azt_next_bn,
						     azt_buf_in,
						     azt_buf_out,
						     azt_buf_bn
						     [azt_buf_in]);
#endif
						azt_buf_bn[azt_buf_in] =
						    azt_next_bn++;
						if (azt_buf_out == -1)
							azt_buf_out =
							    azt_buf_in;
						azt_buf_in =
						    azt_buf_in + 1 ==
						    AZT_BUF_SIZ ? 0 :
						    azt_buf_in + 1;
					}
				}
				if (!azt_transfer_is_active) {
					while (current_valid()) {
						azt_transfer();
						if (CURRENT->nr_sectors ==
						    0)
							end_request(CURRENT, 1);
						else
							break;
					}
				}

				if (current_valid()
				    && (CURRENT->sector / 4 < azt_next_bn
					|| CURRENT->sector / 4 >
					azt_next_bn + AZT_BUF_SIZ)) {
					azt_state = AZT_S_STOP;
					loop_ctl = 1;
					break;
				}
				AztTimeout = READ_TIMEOUT;
				if (azt_read_count == 0) {
					azt_state = AZT_S_STOP;
					loop_ctl = 1;
					break;
				}
				break;
			}
			break;


		case AZT_S_STOP:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_STOP\n");
			}
#endif
			if (azt_read_count != 0)
				printk("aztcd: discard data=%x frames\n",
				       azt_read_count);
			while (azt_read_count != 0) {
				int i;
				if (!(inb(STATUS_PORT) & AFL_DATA)) {
					if (azt_read_mode == AZT_MODE_2)
						for (i = 0;
						     i < CD_FRAMESIZE_RAW;
						     i++)
							inb(DATA_PORT);
					else
						for (i = 0;
						     i < CD_FRAMESIZE; i++)
							inb(DATA_PORT);
				}
				azt_read_count--;
			}
			if (aztSendCmd(ACMD_GET_STATUS))
				RETURN("azt_poll 5");
			azt_state = AZT_S_STOPPING;
			AztTimeout = 1000;
			break;

		case AZT_S_STOPPING:
#ifdef AZT_TEST3
			if (azt_state != azt_state_old) {
				azt_state_old = azt_state;
				printk("AZT_S_STOPPING\n");
			}
#endif

			if ((st = aztStatus()) == -1 && AztTimeout)
				break;

			if ((st != -1)
			    && ((st & AST_DSK_CHG)
				|| (st & AST_NOT_READY))) {
				aztDiskChanged = 1;
				aztTocUpToDate = 0;
				azt_invalidate_buffers();
				printk
				    ("aztcd: Disk Changed or Not Ready 4 - Unmount Disk!\n");
				end_request(CURRENT, 0);
			}

#ifdef AZT_TEST3
			printk("CURRENT_VALID %d azt_mode %d\n",
			       current_valid(), azt_mode);
#endif

			if (current_valid()) {
				if (st != -1) {
					if (azt_mode == 1) {
						azt_state = AZT_S_READ;
						loop_ctl = 1;
						skip = 1;
						break;
					} else {
						azt_state = AZT_S_MODE;
						loop_ctl = 1;
						skip = 1;
						break;
					}
				} else {
					azt_state = AZT_S_START;
					AztTimeout = 1;
				}
			} else {
				azt_state = AZT_S_IDLE;
				return;
			}
			break;

		default:
			printk("aztcd: invalid state %d\n", azt_state);
			return;
		}		/* case */
	}			/* while */


	if (!AztTimeout--) {
		printk("aztcd: timeout in state %d\n", azt_state);
		azt_state = AZT_S_STOP;
		if (aztSendCmd(ACMD_STOP))
			RETURN("azt_poll 6");
		STEN_LOW_WAIT;
	};

	SET_TIMER(azt_poll, HZ / 100);
}


/*###########################################################################
 * Miscellaneous support functions
  ###########################################################################
*/
static void azt_hsg2msf(long hsg, struct msf *msf)
{
	hsg += 150;
	msf->min = hsg / 4500;
	hsg %= 4500;
	msf->sec = hsg / 75;
	msf->frame = hsg % 75;
#ifdef AZT_DEBUG
	if (msf->min >= 70)
		printk("aztcd: Error hsg2msf address Minutes\n");
	if (msf->sec >= 60)
		printk("aztcd: Error hsg2msf address Seconds\n");
	if (msf->frame >= 75)
		printk("aztcd: Error hsg2msf address Frames\n");
#endif
	azt_bin2bcd(&msf->min);	/* convert to BCD */
	azt_bin2bcd(&msf->sec);
	azt_bin2bcd(&msf->frame);
}

static long azt_msf2hsg(struct msf *mp)
{
	return azt_bcd2bin(mp->frame) + azt_bcd2bin(mp->sec) * 75
	    + azt_bcd2bin(mp->min) * 4500 - CD_MSF_OFFSET;
}

static void azt_bin2bcd(unsigned char *p)
{
	int u, t;

	u = *p % 10;
	t = *p / 10;
	*p = u | (t << 4);
}

static int azt_bcd2bin(unsigned char bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0xF);
}

MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(AZTECH_CDROM_MAJOR);

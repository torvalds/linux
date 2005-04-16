/*
 * Definitions for a GoldStar R420 CD-ROM interface
 *
 *   Copyright (C) 1995  Oliver Raupach <raupach@nwfs1.rz.fh-hannover.de>
 *                       Eberhard Moenkeberg <emoenke@gwdg.de>
 *
 *  Published under the GPL.
 *
 */


/* The Interface Card default address is 0x340. This will work for most
   applications. Address selection is accomplished by jumpers PN801-1 to
   PN801-4 on the GoldStar Interface Card.
   Appropriate settings are: 0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360
   0x370, 0x380, 0x390, 0x3A0, 0x3B0, 0x3C0, 0x3D0, 0x3E0, 0x3F0             */

/* insert here the I/O port address and extent */
#define GSCD_BASE_ADDR	        0x340
#define GSCD_IO_EXTENT          4


/************** nothing to set up below here *********************/

/* port access macro */
#define GSCDPORT(x)		(gscd_port + (x))

/*
 * commands
 * the lower nibble holds the command length
 */
#define CMD_STATUS     0x01
#define CMD_READSUBQ   0x02 /* 1: ?, 2: UPC, 5: ? */
#define CMD_SEEK       0x05 /* read_mode M-S-F */
#define CMD_READ       0x07 /* read_mode M-S-F nsec_h nsec_l */
#define CMD_RESET      0x11
#define CMD_SETMODE    0x15
#define CMD_PLAY       0x17 /* M-S-F M-S-F */
#define CMD_LOCK_CTL   0x22 /* 0: unlock, 1: lock */
#define CMD_IDENT      0x31
#define CMD_SETSPEED   0x32 /* 0: auto */ /* ??? */
#define CMD_GETMODE    0x41
#define CMD_PAUSE      0x51
#define CMD_READTOC    0x61
#define CMD_DISKINFO   0x71
#define CMD_TRAY_CTL   0x81

/*
 * disk_state:
 */
#define ST_PLAYING	0x80
#define ST_UNLOCKED	0x40
#define ST_NO_DISK	0x20
#define ST_DOOR_OPEN	0x10
#define ST_x08  0x08
#define ST_x04	0x04
#define ST_INVALID	0x02
#define ST_x01	0x01

/*
 * cmd_type:
 */
#define TYPE_INFO	0x01
#define TYPE_DATA	0x02

/*
 * read_mode:
 */
#define MOD_POLLED	0x80
#define MOD_x08	0x08
#define MOD_RAW	0x04

#define READ_DATA(port, buf, nr) insb(port, buf, nr)

#define SET_TIMER(func, jifs) \
	((mod_timer(&gscd_timer, jiffies + jifs)), \
	(gscd_timer.function = func))

#define CLEAR_TIMER		del_timer_sync(&gscd_timer)

#define MAX_TRACKS		104

struct msf {
	unsigned char	min;
	unsigned char	sec;
	unsigned char	frame;
};

struct gscd_Play_msf {
	struct msf	start;
	struct msf	end;
};

struct gscd_DiskInfo {
	unsigned char	first;
	unsigned char	last;
	struct msf	diskLength;
	struct msf	firstTrack;
};

struct gscd_Toc {
	unsigned char	ctrl_addr;
	unsigned char	track;
	unsigned char	pointIndex;
	struct msf	trackTime;
	struct msf	diskTime;
};


#ifndef PWC_IOCTL_H
#define PWC_IOCTL_H

/* (C) 2001-2004 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* This is pwc-ioctl.h belonging to PWC 10.0.10
   It contains structures and defines to communicate from user space
   directly to the driver.
 */

/*
   Changes
   2001/08/03  Alvarado   Added ioctl constants to access methods for
			  changing white balance and red/blue gains
   2002/12/15  G. H. Fernandez-Toribio   VIDIOCGREALSIZE
   2003/12/13  Nemosft Unv. Some modifications to make interfacing to
	       PWCX easier
 */

/* These are private ioctl() commands, specific for the Philips webcams.
   They contain functions not found in other webcams, and settings not
   specified in the Video4Linux API.

   The #define names are built up like follows:
   VIDIOC		VIDeo IOCtl prefix
	 PWC		Philps WebCam
	    G           optional: Get
	    S           optional: Set
	     ... 	the function
 */

#include <linux/types.h>
#include <linux/version.h>

 /* Enumeration of image sizes */
#define PSZ_SQCIF	0x00
#define PSZ_QSIF	0x01
#define PSZ_QCIF	0x02
#define PSZ_SIF		0x03
#define PSZ_CIF		0x04
#define PSZ_VGA		0x05
#define PSZ_MAX		6


/* The frame rate is encoded in the video_window.flags parameter using
   the upper 16 bits, since some flags are defined nowadays. The following
   defines provide a mask and shift to filter out this value.
   This value can also be passing using the private flag when using v4l2 and
   VIDIOC_S_FMT ioctl.

   In 'Snapshot' mode the camera freezes its automatic exposure and colour
   balance controls.
 */
#define PWC_FPS_SHIFT		16
#define PWC_FPS_MASK		0x00FF0000
#define PWC_FPS_FRMASK		0x003F0000
#define PWC_FPS_SNAPSHOT	0x00400000
#define PWC_QLT_MASK		0x03000000
#define PWC_QLT_SHIFT		24


/* structure for transferring x & y coordinates */
struct pwc_coord
{
	int x, y;		/* guess what */
	int size;		/* size, or offset */
};


/* Used with VIDIOCPWCPROBE */
struct pwc_probe
{
	char name[32];
	int type;
};

struct pwc_serial
{
	char serial[30];	/* String with serial number. Contains terminating 0 */
};

/* pwc_whitebalance.mode values */
#define PWC_WB_INDOOR		0
#define PWC_WB_OUTDOOR		1
#define PWC_WB_FL		2
#define PWC_WB_MANUAL		3
#define PWC_WB_AUTO		4

/* Used with VIDIOCPWC[SG]AWB (Auto White Balance).
   Set mode to one of the PWC_WB_* values above.
   *red and *blue are the respective gains of these colour components inside
   the camera; range 0..65535
   When 'mode' == PWC_WB_MANUAL, 'manual_red' and 'manual_blue' are set or read;
   otherwise undefined.
   'read_red' and 'read_blue' are read-only.
*/
struct pwc_whitebalance
{
	int mode;
	int manual_red, manual_blue;	/* R/W */
	int read_red, read_blue;	/* R/O */
};

/*
   'control_speed' and 'control_delay' are used in automatic whitebalance mode,
   and tell the camera how fast it should react to changes in lighting, and
   with how much delay. Valid values are 0..65535.
*/
struct pwc_wb_speed
{
	int control_speed;
	int control_delay;

};

/* Used with VIDIOCPWC[SG]LED */
struct pwc_leds
{
	int led_on;			/* Led on-time; range = 0..25000 */
	int led_off;			/* Led off-time; range = 0..25000  */
};

/* Image size (used with GREALSIZE) */
struct pwc_imagesize
{
	int width;
	int height;
};

/* Defines and structures for Motorized Pan & Tilt */
#define PWC_MPT_PAN		0x01
#define PWC_MPT_TILT		0x02
#define PWC_MPT_TIMEOUT		0x04 /* for status */

/* Set angles; when absolute != 0, the angle is absolute and the
   driver calculates the relative offset for you. This can only
   be used with VIDIOCPWCSANGLE; VIDIOCPWCGANGLE always returns
   absolute angles.
 */
struct pwc_mpt_angles
{
	int absolute;		/* write-only */
	int pan;		/* degrees * 100 */
	int tilt;		/* degress * 100 */
};

/* Range of angles of the camera, both horizontally and vertically.
 */
struct pwc_mpt_range
{
	int pan_min, pan_max;		/* degrees * 100 */
	int tilt_min, tilt_max;
};

struct pwc_mpt_status
{
	int status;
	int time_pan;
	int time_tilt;
};


/* This is used for out-of-kernel decompression. With it, you can get
   all the necessary information to initialize and use the decompressor
   routines in standalone applications.
 */
struct pwc_video_command
{
	int type;		/* camera type (645, 675, 730, etc.) */
	int release;		/* release number */

	int size;		/* one of PSZ_* */
	int alternate;
	int command_len;	/* length of USB video command */
	unsigned char command_buf[13];	/* Actual USB video command */
	int bandlength;		/* >0 = compressed */
	int frame_size;		/* Size of one (un)compressed frame */
};

/* Flags for PWCX subroutines. Not all modules honour all flags. */
#define PWCX_FLAG_PLANAR	0x0001
#define PWCX_FLAG_BAYER		0x0008


/* IOCTL definitions */

 /* Restore user settings */
#define VIDIOCPWCRUSER		_IO('v', 192)
 /* Save user settings */
#define VIDIOCPWCSUSER		_IO('v', 193)
 /* Restore factory settings */
#define VIDIOCPWCFACTORY	_IO('v', 194)

 /* You can manipulate the compression factor. A compression preference of 0
    means use uncompressed modes when available; 1 is low compression, 2 is
    medium and 3 is high compression preferred. Of course, the higher the
    compression, the lower the bandwidth used but more chance of artefacts
    in the image. The driver automatically chooses a higher compression when
    the preferred mode is not available.
  */
 /* Set preferred compression quality (0 = uncompressed, 3 = highest compression) */
#define VIDIOCPWCSCQUAL		_IOW('v', 195, int)
 /* Get preferred compression quality */
#define VIDIOCPWCGCQUAL		_IOR('v', 195, int)


/* Retrieve serial number of camera */
#define VIDIOCPWCGSERIAL	_IOR('v', 198, struct pwc_serial)

 /* This is a probe function; since so many devices are supported, it
    becomes difficult to include all the names in programs that want to
    check for the enhanced Philips stuff. So in stead, try this PROBE;
    it returns a structure with the original name, and the corresponding
    Philips type.
    To use, fill the structure with zeroes, call PROBE and if that succeeds,
    compare the name with that returned from VIDIOCGCAP; they should be the
    same. If so, you can be assured it is a Philips (OEM) cam and the type
    is valid.
 */
#define VIDIOCPWCPROBE		_IOR('v', 199, struct pwc_probe)

 /* Set AGC (Automatic Gain Control); int < 0 = auto, 0..65535 = fixed */
#define VIDIOCPWCSAGC		_IOW('v', 200, int)
 /* Get AGC; int < 0 = auto; >= 0 = fixed, range 0..65535 */
#define VIDIOCPWCGAGC		_IOR('v', 200, int)
 /* Set shutter speed; int < 0 = auto; >= 0 = fixed, range 0..65535 */
#define VIDIOCPWCSSHUTTER	_IOW('v', 201, int)

 /* Color compensation (Auto White Balance) */
#define VIDIOCPWCSAWB           _IOW('v', 202, struct pwc_whitebalance)
#define VIDIOCPWCGAWB           _IOR('v', 202, struct pwc_whitebalance)

 /* Auto WB speed */
#define VIDIOCPWCSAWBSPEED	_IOW('v', 203, struct pwc_wb_speed)
#define VIDIOCPWCGAWBSPEED	_IOR('v', 203, struct pwc_wb_speed)

 /* LEDs on/off/blink; int range 0..65535 */
#define VIDIOCPWCSLED           _IOW('v', 205, struct pwc_leds)
#define VIDIOCPWCGLED           _IOR('v', 205, struct pwc_leds)

  /* Contour (sharpness); int < 0 = auto, 0..65536 = fixed */
#define VIDIOCPWCSCONTOUR	_IOW('v', 206, int)
#define VIDIOCPWCGCONTOUR	_IOR('v', 206, int)

  /* Backlight compensation; 0 = off, otherwise on */
#define VIDIOCPWCSBACKLIGHT	_IOW('v', 207, int)
#define VIDIOCPWCGBACKLIGHT	_IOR('v', 207, int)

  /* Flickerless mode; = 0 off, otherwise on */
#define VIDIOCPWCSFLICKER	_IOW('v', 208, int)
#define VIDIOCPWCGFLICKER	_IOR('v', 208, int)

  /* Dynamic noise reduction; 0 off, 3 = high noise reduction */
#define VIDIOCPWCSDYNNOISE	_IOW('v', 209, int)
#define VIDIOCPWCGDYNNOISE	_IOR('v', 209, int)

 /* Real image size as used by the camera; tells you whether or not there's a gray border around the image */
#define VIDIOCPWCGREALSIZE	_IOR('v', 210, struct pwc_imagesize)

 /* Motorized pan & tilt functions */
#define VIDIOCPWCMPTRESET	_IOW('v', 211, int)
#define VIDIOCPWCMPTGRANGE	_IOR('v', 211, struct pwc_mpt_range)
#define VIDIOCPWCMPTSANGLE	_IOW('v', 212, struct pwc_mpt_angles)
#define VIDIOCPWCMPTGANGLE	_IOR('v', 212, struct pwc_mpt_angles)
#define VIDIOCPWCMPTSTATUS	_IOR('v', 213, struct pwc_mpt_status)

 /* Get the USB set-video command; needed for initializing libpwcx */
#define VIDIOCPWCGVIDCMD	_IOR('v', 215, struct pwc_video_command)
struct pwc_table_init_buffer {
   int len;
   char *buffer;

};
#define VIDIOCPWCGVIDTABLE	_IOR('v', 216, struct pwc_table_init_buffer)

/*
 * This is private command used when communicating with v4l2.
 * In the future all private ioctl will be remove/replace to
 * use interface offer by v4l2.
 */

#define V4L2_CID_PRIVATE_SAVE_USER       (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_RESTORE_USER    (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PRIVATE_RESTORE_FACTORY (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PRIVATE_COLOUR_MODE     (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PRIVATE_AUTOCONTOUR     (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_PRIVATE_CONTOUR         (V4L2_CID_PRIVATE_BASE + 5)
#define V4L2_CID_PRIVATE_BACKLIGHT       (V4L2_CID_PRIVATE_BASE + 6)
#define V4L2_CID_PRIVATE_FLICKERLESS     (V4L2_CID_PRIVATE_BASE + 7)
#define V4L2_CID_PRIVATE_NOISE_REDUCTION (V4L2_CID_PRIVATE_BASE + 8)

struct pwc_raw_frame {
   __le16 type;		/* type of the webcam */
   __le16 vbandlength;	/* Size of 4lines compressed (used by the decompressor) */
   __u8   cmd[4];	/* the four byte of the command (in case of nala,
			   only the first 3 bytes is filled) */
   __u8   rawframe[0];	/* frame_size = H/4*vbandlength */
} __attribute__ ((packed));


#endif

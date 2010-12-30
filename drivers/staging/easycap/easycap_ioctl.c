/******************************************************************************
*                                                                             *
*  easycap_ioctl.c                                                            *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include <linux/smp_lock.h>
#include "easycap.h"
#include "easycap_debug.h"
#include "easycap_standard.h"
#include "easycap_ioctl.h"

/*--------------------------------------------------------------------------*/
/*
 *  UNLESS THERE IS A PREMATURE ERROR RETURN THIS ROUTINE UPDATES THE
 *  FOLLOWING:
 *          peasycap->standard_offset
 *          peasycap->fps
 *          peasycap->usec
 *          peasycap->tolerate
 */
/*---------------------------------------------------------------------------*/
int adjust_standard(struct easycap *peasycap, v4l2_std_id std_id)
{
struct easycap_standard const *peasycap_standard;
__u16 reg, set;
int ir, rc, need;
unsigned int itwas, isnow;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
peasycap_standard = &easycap_standard[0];
while (0xFFFF != peasycap_standard->mask) {
	if (std_id & peasycap_standard->v4l2_standard.id)
		break;
	peasycap_standard++;
}
if (0xFFFF == peasycap_standard->mask) {
	SAY("ERROR: 0x%08X=std_id: standard not found\n", \
							(unsigned int)std_id);
	return -EINVAL;
}
SAY("user requests standard: %s\n", \
			&(peasycap_standard->v4l2_standard.name[0]));
if (peasycap->standard_offset == \
			(int)(peasycap_standard - &easycap_standard[0])) {
	SAY("requested standard already in effect\n");
	return 0;
}
peasycap->standard_offset = (int)(peasycap_standard - &easycap_standard[0]);
peasycap->fps = peasycap_standard->v4l2_standard.frameperiod.denominator / \
		peasycap_standard->v4l2_standard.frameperiod.numerator;
if (!peasycap->fps) {
	SAY("MISTAKE: frames-per-second is zero\n");
	return -EFAULT;
}
JOT(8, "%i frames-per-second\n", peasycap->fps);
peasycap->usec = 1000000 / (2 * peasycap->fps);
peasycap->tolerate = 1000 * (25 / peasycap->fps);

kill_video_urbs(peasycap);

/*--------------------------------------------------------------------------*/
/*
 *  SAA7113H DATASHEET PAGE 44, TABLE 42
 */
/*--------------------------------------------------------------------------*/
need = 0;  itwas = 0;  reg = 0x00;  set = 0x00;
switch (peasycap_standard->mask & 0x000F) {
case NTSC_M_JP: {
	reg = 0x0A;  set = 0x95;
	ir = read_saa(peasycap->pusb_device, reg);
	if (0 > ir)
		SAY("ERROR: cannot read SAA register 0x%02X\n", reg);
	else
		itwas = (unsigned int)ir;


	set2to78(peasycap->pusb_device);


	rc = write_saa(peasycap->pusb_device, reg, set);
	if (0 != rc)
		SAY("ERROR: failed to set SAA register " \
			"0x%02X to 0x%02X for JP standard\n", reg, set);
	else {
		isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			JOT(8, "SAA register 0x%02X changed " \
				"to 0x%02X\n", reg, isnow);
		else
			JOT(8, "SAA register 0x%02X changed " \
				"from 0x%02X to 0x%02X\n", reg, itwas, isnow);

		set2to78(peasycap->pusb_device);

	}

	reg = 0x0B;  set = 0x48;
	ir = read_saa(peasycap->pusb_device, reg);
	if (0 > ir)
		SAY("ERROR: cannot read SAA register 0x%02X\n", reg);
	else
		itwas = (unsigned int)ir;

	set2to78(peasycap->pusb_device);

	rc = write_saa(peasycap->pusb_device, reg, set);
	if (0 != rc)
		SAY("ERROR: failed to set SAA register 0x%02X to 0x%02X " \
						"for JP standard\n", reg, set);
	else {
		isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			JOT(8, "SAA register 0x%02X changed " \
				"to 0x%02X\n", reg, isnow);
		else
			JOT(8, "SAA register 0x%02X changed " \
				"from 0x%02X to 0x%02X\n", reg, itwas, isnow);

		set2to78(peasycap->pusb_device);

	}
/*--------------------------------------------------------------------------*/
/*
 *  NOTE:  NO break HERE:  RUN ON TO NEXT CASE
 */
/*--------------------------------------------------------------------------*/
}
case NTSC_M:
case PAL_BGHIN: {
	reg = 0x0E;  set = 0x01;  need = 1;  break;
}
case NTSC_N_443:
case PAL_60: {
	reg = 0x0E;  set = 0x11;  need = 1;  break;
}
case NTSC_443:
case PAL_Nc: {
	reg = 0x0E;  set = 0x21;  need = 1;  break;
}
case NTSC_N:
case PAL_M: {
	reg = 0x0E;  set = 0x31;  need = 1;  break;
}
case SECAM: {
	reg = 0x0E;  set = 0x51;  need = 1;  break;
}
default:
	break;
}
/*--------------------------------------------------------------------------*/
if (need) {
	ir = read_saa(peasycap->pusb_device, reg);
	if (0 > ir)
		SAY("ERROR: failed to read SAA register 0x%02X\n", reg);
	else
		itwas = (unsigned int)ir;

	set2to78(peasycap->pusb_device);

	rc = write_saa(peasycap->pusb_device, reg, set);
	if (0 != write_saa(peasycap->pusb_device, reg, set)) {
		SAY("ERROR: failed to set SAA register " \
			"0x%02X to 0x%02X for table 42\n", reg, set);
	} else {
		isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			JOT(8, "SAA register 0x%02X changed " \
				"to 0x%02X\n", reg, isnow);
		else
			JOT(8, "SAA register 0x%02X changed " \
				"from 0x%02X to 0x%02X\n", reg, itwas, isnow);
	}
}
/*--------------------------------------------------------------------------*/
/*
 *  SAA7113H DATASHEET PAGE 41
 */
/*--------------------------------------------------------------------------*/
reg = 0x08;
ir = read_saa(peasycap->pusb_device, reg);
if (0 > ir)
	SAY("ERROR: failed to read SAA register 0x%02X " \
						"so cannot reset\n", reg);
else {
	itwas = (unsigned int)ir;
	if (peasycap_standard->mask & 0x0001)
		set = itwas | 0x40 ;
	else
		set = itwas & ~0x40 ;

set2to78(peasycap->pusb_device);

rc  = write_saa(peasycap->pusb_device, reg, set);
if (0 != rc)
	SAY("ERROR: failed to set SAA register 0x%02X to 0x%02X\n", reg, set);
else {
	isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
	if (0 > ir)
		JOT(8, "SAA register 0x%02X changed to 0x%02X\n", reg, isnow);
	else
		JOT(8, "SAA register 0x%02X changed " \
			"from 0x%02X to 0x%02X\n", reg, itwas, isnow);
	}
}
/*--------------------------------------------------------------------------*/
/*
 *  SAA7113H DATASHEET PAGE 51, TABLE 57
 */
/*---------------------------------------------------------------------------*/
reg = 0x40;
ir = read_saa(peasycap->pusb_device, reg);
if (0 > ir)
	SAY("ERROR: failed to read SAA register 0x%02X " \
						"so cannot reset\n", reg);
else {
	itwas = (unsigned int)ir;
	if (peasycap_standard->mask & 0x0001)
		set = itwas | 0x80 ;
	else
		set = itwas & ~0x80 ;

set2to78(peasycap->pusb_device);

rc = write_saa(peasycap->pusb_device, reg, set);
if (0 != rc)
	SAY("ERROR: failed to set SAA register 0x%02X to 0x%02X\n", reg, set);
else {
	isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
	if (0 > ir)
		JOT(8, "SAA register 0x%02X changed to 0x%02X\n", reg, isnow);
	else
		JOT(8, "SAA register 0x%02X changed " \
			"from 0x%02X to 0x%02X\n", reg, itwas, isnow);
	}
}
/*--------------------------------------------------------------------------*/
/*
 *  SAA7113H DATASHEET PAGE 53, TABLE 66
 */
/*--------------------------------------------------------------------------*/
reg = 0x5A;
ir = read_saa(peasycap->pusb_device, reg);
if (0 > ir)
	SAY("ERROR: failed to read SAA register 0x%02X but continuing\n", reg);
	itwas = (unsigned int)ir;
	if (peasycap_standard->mask & 0x0001)
		set = 0x0A ;
	else
		set = 0x07 ;

	set2to78(peasycap->pusb_device);

	if (0 != write_saa(peasycap->pusb_device, reg, set))
		SAY("ERROR: failed to set SAA register 0x%02X to 0x%02X\n", \
								reg, set);
	else {
		isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			JOT(8, "SAA register 0x%02X changed "
				"to 0x%02X\n", reg, isnow);
		else
			JOT(8, "SAA register 0x%02X changed "
				"from 0x%02X to 0x%02X\n", reg, itwas, isnow);
	}
	if (0 != check_saa(peasycap->pusb_device))
		SAY("ERROR: check_saa() failed\n");
return 0;
}
/*****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  THE ALGORITHM FOR RESPONDING TO THE VIDIO_S_FMT IOCTL DEPENDS ON THE
 *  CURRENT VALUE OF peasycap->standard_offset.
 *  PROVIDED THE ARGUMENT try IS false AND THERE IS NO PREMATURE ERROR RETURN
 *  THIS ROUTINE UPDATES THE FOLLOWING:
 *          peasycap->format_offset
 *          peasycap->pixelformat
 *          peasycap->field
 *          peasycap->height
 *          peasycap->width
 *          peasycap->bytesperpixel
 *          peasycap->byteswaporder
 *          peasycap->decimatepixel
 *          peasycap->frame_buffer_used
 *          peasycap->videofieldamount
 *          peasycap->offerfields
 *
 *  IF SUCCESSFUL THE FUNCTION RETURNS THE OFFSET IN easycap_format[]
 *  IDENTIFYING THE FORMAT WHICH IS TO RETURNED TO THE USER.
 *  ERRORS RETURN A NEGATIVE NUMBER.
 */
/*--------------------------------------------------------------------------*/
int adjust_format(struct easycap *peasycap, \
	__u32 width, __u32 height, __u32 pixelformat, int field, bool try)
{
struct easycap_format *peasycap_format, *peasycap_best_format;
__u16 mask;
struct usb_device *p;
int miss, multiplier, best;
char bf[5], *pc;
__u32 uc;

if ((struct easycap *)NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
p = peasycap->pusb_device;
if ((struct usb_device *)NULL == p) {
	SAY("ERROR: peaycap->pusb_device is NULL\n");
	return -EFAULT;
}
pc = &bf[0];
uc = pixelformat;  memcpy((void *)pc, (void *)(&uc), 4);  bf[4] = 0;
mask = easycap_standard[peasycap->standard_offset].mask;
SAY("sought:    %ix%i,%s(0x%08X),%i=field,0x%02X=std mask\n", \
				width, height, pc, pixelformat, field, mask);
if (V4L2_FIELD_ANY == field) {
	field = V4L2_FIELD_INTERLACED;
	SAY("prefer:    V4L2_FIELD_INTERLACED=field, was V4L2_FIELD_ANY\n");
}
peasycap_best_format = (struct easycap_format *)NULL;
peasycap_format = &easycap_format[0];
while (0 != peasycap_format->v4l2_format.fmt.pix.width) {
	JOT(16, ".> %i %i 0x%08X %ix%i\n", \
		peasycap_format->mask & 0x01,
		peasycap_format->v4l2_format.fmt.pix.field,
		peasycap_format->v4l2_format.fmt.pix.pixelformat,
		peasycap_format->v4l2_format.fmt.pix.width,
		peasycap_format->v4l2_format.fmt.pix.height);

	if (((peasycap_format->mask & 0x0F) == (mask & 0x0F)) && \
		(peasycap_format->v4l2_format.fmt.pix.field == field) && \
		(peasycap_format->v4l2_format.fmt.pix.pixelformat == \
							pixelformat) && \
		(peasycap_format->v4l2_format.fmt.pix.width  == width) && \
		(peasycap_format->v4l2_format.fmt.pix.height == height)) {
			peasycap_best_format = peasycap_format;
			break;
		}
	peasycap_format++;
}
if (0 == peasycap_format->v4l2_format.fmt.pix.width) {
	SAY("cannot do: %ix%i with standard mask 0x%02X\n", \
							width, height, mask);
	peasycap_format = &easycap_format[0];  best = -1;
	while (0 != peasycap_format->v4l2_format.fmt.pix.width) {
		if (((peasycap_format->mask & 0x0F) == (mask & 0x0F)) && \
				 (peasycap_format->v4l2_format.fmt.pix\
						.field == field) && \
				 (peasycap_format->v4l2_format.fmt.pix\
						.pixelformat == pixelformat)) {
			miss = abs(peasycap_format->\
					v4l2_format.fmt.pix.width  - width);
			if ((best > miss) || (best < 0)) {
				best = miss;
				peasycap_best_format = peasycap_format;
				if (!miss)
					break;
			}
		}
		peasycap_format++;
	}
	if (-1 == best) {
		SAY("cannot do %ix... with standard mask 0x%02X\n", \
								width, mask);
		SAY("cannot do ...x%i with standard mask 0x%02X\n", \
								height, mask);
		SAY("           %ix%i unmatched\n", width, height);
		return peasycap->format_offset;
	}
}
if ((struct easycap_format *)NULL == peasycap_best_format) {
	SAY("MISTAKE: peasycap_best_format is NULL");
	return -EINVAL;
}
peasycap_format = peasycap_best_format;

/*...........................................................................*/
if (true == try)
	return (int)(peasycap_best_format - &easycap_format[0]);
/*...........................................................................*/

if (false != try) {
	SAY("MISTAKE: true==try where is should be false\n");
	return -EINVAL;
}
SAY("actioning: %ix%i %s\n", \
			peasycap_format->v4l2_format.fmt.pix.width, \
			peasycap_format->v4l2_format.fmt.pix.height,
			&peasycap_format->name[0]);
peasycap->height        = peasycap_format->v4l2_format.fmt.pix.height;
peasycap->width         = peasycap_format->v4l2_format.fmt.pix.width;
peasycap->pixelformat   = peasycap_format->v4l2_format.fmt.pix.pixelformat;
peasycap->field         = peasycap_format->v4l2_format.fmt.pix.field;
peasycap->format_offset = (int)(peasycap_format - &easycap_format[0]);
peasycap->bytesperpixel = (0x00F0 & peasycap_format->mask) >> 4 ;
if (0x0100 & peasycap_format->mask)
	peasycap->byteswaporder = true;
else
	peasycap->byteswaporder = false;
if (0x0800 & peasycap_format->mask)
	peasycap->decimatepixel = true;
else
	peasycap->decimatepixel = false;
if (0x1000 & peasycap_format->mask)
	peasycap->offerfields = true;
else
	peasycap->offerfields = false;
if (true == peasycap->decimatepixel)
	multiplier = 2;
else
	multiplier = 1;
peasycap->videofieldamount = multiplier * peasycap->width * \
					multiplier * peasycap->height;
peasycap->frame_buffer_used = peasycap->bytesperpixel * \
					peasycap->width * peasycap->height;

if (true == peasycap->offerfields) {
	SAY("WARNING: %i=peasycap->field is untested: " \
				"please report problems\n", peasycap->field);


/*
 *    FIXME ---- THIS IS UNTESTED, MAY BE (AND PROBABLY IS) INCORRECT:
 *
 *    peasycap->frame_buffer_used = peasycap->frame_buffer_used / 2;
 *
 *    SO DO NOT RISK IT YET.
 *
 */



}

kill_video_urbs(peasycap);

/*---------------------------------------------------------------------------*/
/*
 *  PAL
 */
/*---------------------------------------------------------------------------*/
if (0 == (0x01 & peasycap_format->mask)) {
	if (((720 == peasycap_format->v4l2_format.fmt.pix.width) && \
			(576 == \
			peasycap_format->v4l2_format.fmt.pix.height)) || \
			((360 == \
			peasycap_format->v4l2_format.fmt.pix.width) && \
			(288 == \
			peasycap_format->v4l2_format.fmt.pix.height))) {
		if (0 != set_resolution(p, 0x0000, 0x0001, 0x05A0, 0x0121)) {
			SAY("ERROR: set_resolution() failed\n");
			return -EINVAL;
		}
	} else if ((704 == peasycap_format->v4l2_format.fmt.pix.width) && \
			(576 == peasycap_format->v4l2_format.fmt.pix.height)) {
		if (0 != set_resolution(p, 0x0004, 0x0001, 0x0584, 0x0121)) {
			SAY("ERROR: set_resolution() failed\n");
			return -EINVAL;
		}
	} else if (((640 == peasycap_format->v4l2_format.fmt.pix.width) && \
			(480 == \
			peasycap_format->v4l2_format.fmt.pix.height)) || \
			((320 == \
			peasycap_format->v4l2_format.fmt.pix.width) && \
			(240 == \
			peasycap_format->v4l2_format.fmt.pix.height))) {
		if (0 != set_resolution(p, 0x0014, 0x0020, 0x0514, 0x0110)) {
			SAY("ERROR: set_resolution() failed\n");
			return -EINVAL;
		}
	} else {
		SAY("MISTAKE: bad format, cannot set resolution\n");
		return -EINVAL;
	}
/*---------------------------------------------------------------------------*/
/*
 *  NTSC
 */
/*---------------------------------------------------------------------------*/
} else {
	if (((720 == peasycap_format->v4l2_format.fmt.pix.width) && \
			(480 == \
			peasycap_format->v4l2_format.fmt.pix.height)) || \
			((360 == \
			peasycap_format->v4l2_format.fmt.pix.width) && \
			(240 == \
			peasycap_format->v4l2_format.fmt.pix.height))) {
		if (0 != set_resolution(p, 0x0000, 0x0003, 0x05A0, 0x00F3)) {
			SAY("ERROR: set_resolution() failed\n");
			return -EINVAL;
		}
	} else if (((640 == peasycap_format->v4l2_format.fmt.pix.width) && \
			(480 == \
			peasycap_format->v4l2_format.fmt.pix.height)) || \
			((320 == \
			peasycap_format->v4l2_format.fmt.pix.width) && \
			(240 == \
			peasycap_format->v4l2_format.fmt.pix.height))) {
		if (0 != set_resolution(p, 0x0014, 0x0003, 0x0514, 0x00F3)) {
			SAY("ERROR: set_resolution() failed\n");
			return -EINVAL;
		}
	} else {
		SAY("MISTAKE: bad format, cannot set resolution\n");
		return -EINVAL;
	}
}
/*---------------------------------------------------------------------------*/

check_stk(peasycap->pusb_device);

return (int)(peasycap_best_format - &easycap_format[0]);
}
/*****************************************************************************/
int adjust_brightness(struct easycap *peasycap, int value)
{
unsigned int mood;
int i1;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_BRIGHTNESS == easycap_control[i1].id) {
		if ((easycap_control[i1].minimum > value) || \
					(easycap_control[i1].maximum < value))
			value = easycap_control[i1].default_value;
		peasycap->brightness = value;
		mood = 0x00FF & (unsigned int)peasycap->brightness;

		set2to78(peasycap->pusb_device);

		if (!write_saa(peasycap->pusb_device, 0x0A, mood)) {
			SAY("adjusting brightness to  0x%02X\n", mood);
			return 0;
		} else {
			SAY("WARNING: failed to adjust brightness " \
							"to 0x%02X\n", mood);
			return -ENOENT;
		}

		set2to78(peasycap->pusb_device);

		break;
	}
	i1++;
}
SAY("WARNING: failed to adjust brightness: control not found\n");
return -ENOENT;
}
/*****************************************************************************/
int adjust_contrast(struct easycap *peasycap, int value)
{
unsigned int mood;
int i1;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_CONTRAST == easycap_control[i1].id) {
		if ((easycap_control[i1].minimum > value) || \
					(easycap_control[i1].maximum < value))
			value = easycap_control[i1].default_value;
		peasycap->contrast = value;
		mood = 0x00FF & (unsigned int) (peasycap->contrast - 128);

		set2to78(peasycap->pusb_device);

		if (!write_saa(peasycap->pusb_device, 0x0B, mood)) {
			SAY("adjusting contrast to  0x%02X\n", mood);
			return 0;
		} else {
			SAY("WARNING: failed to adjust contrast to " \
							"0x%02X\n", mood);
			return -ENOENT;
		}

		set2to78(peasycap->pusb_device);

		break;
	}
	i1++;
}
SAY("WARNING: failed to adjust contrast: control not found\n");
return -ENOENT;
}
/*****************************************************************************/
int adjust_saturation(struct easycap *peasycap, int value)
{
unsigned int mood;
int i1;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_SATURATION == easycap_control[i1].id) {
		if ((easycap_control[i1].minimum > value) || \
					(easycap_control[i1].maximum < value))
			value = easycap_control[i1].default_value;
		peasycap->saturation = value;
		mood = 0x00FF & (unsigned int) (peasycap->saturation - 128);

		set2to78(peasycap->pusb_device);

		if (!write_saa(peasycap->pusb_device, 0x0C, mood)) {
			SAY("adjusting saturation to  0x%02X\n", mood);
			return 0;
		} else {
			SAY("WARNING: failed to adjust saturation to " \
							"0x%02X\n", mood);
			return -ENOENT;
		}
		break;

		set2to78(peasycap->pusb_device);

	}
	i1++;
}
SAY("WARNING: failed to adjust saturation: control not found\n");
return -ENOENT;
}
/*****************************************************************************/
int adjust_hue(struct easycap *peasycap, int value)
{
unsigned int mood;
int i1, i2;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_HUE == easycap_control[i1].id) {
		if ((easycap_control[i1].minimum > value) || \
					(easycap_control[i1].maximum < value))
			value = easycap_control[i1].default_value;
		peasycap->hue = value;
		i2 = peasycap->hue - 128;
		mood = 0x00FF & ((int) i2);

		set2to78(peasycap->pusb_device);

		if (!write_saa(peasycap->pusb_device, 0x0D, mood)) {
			SAY("adjusting hue to  0x%02X\n", mood);
			return 0;
		} else {
			SAY("WARNING: failed to adjust hue to 0x%02X\n", mood);
			return -ENOENT;
		}

		set2to78(peasycap->pusb_device);

		break;
	}
	i1++;
}
SAY("WARNING: failed to adjust hue: control not found\n");
return -ENOENT;
}
/*****************************************************************************/
int adjust_volume(struct easycap *peasycap, int value)
{
__s8 mood;
int i1;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_AUDIO_VOLUME == easycap_control[i1].id) {
		if ((easycap_control[i1].minimum > value) || \
			(easycap_control[i1].maximum < value))
			value = easycap_control[i1].default_value;
		peasycap->volume = value;
		mood = (16 > peasycap->volume) ? 16 : \
			((31 < peasycap->volume) ? 31 : \
			(__s8) peasycap->volume);
		if (!audio_gainset(peasycap->pusb_device, mood)) {
			SAY("adjusting volume to 0x%01X\n", mood);
			return 0;
		} else {
			SAY("WARNING: failed to adjust volume to " \
							"0x%1X\n", mood);
			return -ENOENT;
		}
		break;
	}
i1++;
}
SAY("WARNING: failed to adjust volume: control not found\n");
return -ENOENT;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  AN ALTERNATIVE METHOD OF MUTING MIGHT SEEM TO BE:
 *            usb_set_interface(peasycap->pusb_device, \
 *                              peasycap->audio_interface, \
 *                              peasycap->audio_altsetting_off);
 *  HOWEVER, AFTER THIS COMMAND IS ISSUED ALL SUBSEQUENT URBS RECEIVE STATUS
 *  -ESHUTDOWN.  THE HANDLER ROUTINE easysnd_complete() DECLINES TO RESUBMIT
 *  THE URB AND THE PIPELINE COLLAPSES IRRETRIEVABLY.  BEWARE.
 */
/*---------------------------------------------------------------------------*/
int adjust_mute(struct easycap *peasycap, int value)
{
int i1;

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
i1 = 0;
while (0xFFFFFFFF != easycap_control[i1].id) {
	if (V4L2_CID_AUDIO_MUTE == easycap_control[i1].id) {
		peasycap->mute = value;
		switch (peasycap->mute) {
		case 1: {
			peasycap->audio_idle = 1;
			peasycap->timeval0.tv_sec = 0;
			SAY("adjusting mute: %i=peasycap->audio_idle\n", \
							peasycap->audio_idle);
			return 0;
		}
		default: {
			peasycap->audio_idle = 0;
			SAY("adjusting mute: %i=peasycap->audio_idle\n", \
							peasycap->audio_idle);
			return 0;
		}
		}
		break;
	}
	i1++;
}
SAY("WARNING: failed to adjust mute: control not found\n");
return -ENOENT;
}

/*--------------------------------------------------------------------------*/
static int easycap_ioctl_bkl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
static struct easycap *peasycap;
static struct usb_device *p;
static __u32 isequence;

peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL\n");
	return -1;
}
p = peasycap->pusb_device;
if ((struct usb_device *)NULL == p) {
	SAY("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/
/*
 *  MOST OF THE VARIABLES DECLARED static IN THE case{} BLOCKS BELOW ARE SO
 *  DECLARED SIMPLY TO AVOID A COMPILER WARNING OF THE KIND:
 *  easycap_ioctl.c: warning:
 *                       the frame size of ... bytes is larger than 1024 bytes
 */
/*---------------------------------------------------------------------------*/
switch (cmd) {
case VIDIOC_QUERYCAP: {
	static struct v4l2_capability v4l2_capability;
	static char version[16], *p1, *p2;
	static int i, rc, k[3];
	static long lng;

	JOT(8, "VIDIOC_QUERYCAP\n");

	if (16 <= strlen(EASYCAP_DRIVER_VERSION)) {
		SAY("ERROR: bad driver version string\n"); return -EINVAL;
	}
	strcpy(&version[0], EASYCAP_DRIVER_VERSION);
	for (i = 0; i < 3; i++)
		k[i] = 0;
	p2 = &version[0];  i = 0;
	while (*p2) {
		p1 = p2;
		while (*p2 && ('.' != *p2))
			p2++;
		if (*p2)
			*p2++ = 0;
		if (3 > i) {
			rc = (int) strict_strtol(p1, 10, &lng);
			if (0 != rc) {
				SAY("ERROR: %i=strict_strtol(%s,.,,)\n", \
								rc, p1);
				return -EINVAL;
			}
			k[i] = (int)lng;
		}
		i++;
	}

	memset(&v4l2_capability, 0, sizeof(struct v4l2_capability));
	strlcpy(&v4l2_capability.driver[0], "easycap", \
					sizeof(v4l2_capability.driver));

	v4l2_capability.capabilities = \
				V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | \
				V4L2_CAP_AUDIO         | V4L2_CAP_READWRITE;

	v4l2_capability.version = KERNEL_VERSION(k[0], k[1], k[2]);
	JOT(8, "v4l2_capability.version=(%i,%i,%i)\n", k[0], k[1], k[2]);

	strlcpy(&v4l2_capability.card[0], "EasyCAP DC60", \
		sizeof(v4l2_capability.card));

	if (usb_make_path(peasycap->pusb_device, &v4l2_capability.bus_info[0],\
				sizeof(v4l2_capability.bus_info)) < 0) {
		strlcpy(&v4l2_capability.bus_info[0], "EasyCAP bus_info", \
					sizeof(v4l2_capability.bus_info));
		JOT(8, "%s=v4l2_capability.bus_info\n", \
					&v4l2_capability.bus_info[0]);
	}
	if (0 != copy_to_user((void __user *)arg, &v4l2_capability, \
					sizeof(struct v4l2_capability))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_ENUMINPUT: {
	static struct v4l2_input v4l2_input;
	static __u32 index;

	JOT(8, "VIDIOC_ENUMINPUT\n");

	if (0 != copy_from_user(&v4l2_input, (void __user *)arg, \
					sizeof(struct v4l2_input))) {
		POUT;
		return -EFAULT;
	}

	index = v4l2_input.index;
	memset(&v4l2_input, 0, sizeof(struct v4l2_input));

	switch (index) {
	case 0: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "CVBS0");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	case 1: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "CVBS1");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	case 2: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "CVBS2");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	case 3: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "CVBS3");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	case 4: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "CVBS4");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	case 5: {
		v4l2_input.index = index;
		strcpy(&v4l2_input.name[0], "S-VIDEO");
		v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
		v4l2_input.audioset = 0x01;
		v4l2_input.tuner = 0;
		v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM | \
				V4L2_STD_NTSC ;
		v4l2_input.status = 0;
		JOT(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
		break;
	}
	default: {
		JOT(8, "%i=index: exhausts inputs\n", index);
		return -EINVAL;
	}
	}

	if (0 != copy_to_user((void __user *)arg, &v4l2_input, \
						sizeof(struct v4l2_input))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_INPUT: {
	static __u32 index;

	JOT(8, "VIDIOC_G_INPUT\n");
	index = (__u32)peasycap->input;
	JOT(8, "user is told: %i\n", index);
	if (0 != copy_to_user((void __user *)arg, &index, sizeof(__u32))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_INPUT:
	{
	static __u32 index;

	JOT(8, "VIDIOC_S_INPUT\n");

	if (0 != copy_from_user(&index, (void __user *)arg, sizeof(__u32))) {
		POUT;
		return -EFAULT;
	}

	JOT(8, "user requests input %i\n", index);

	if ((int)index == peasycap->input) {
		SAY("requested input already in effect\n");
		break;
	}

	if ((0 > index) || (5 < index)) {
		JOT(8, "ERROR:  bad requested input: %i\n", index);
		return -EINVAL;
	}
	peasycap->input = (int)index;

	select_input(peasycap->pusb_device, peasycap->input, 9);

	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_ENUMAUDIO: {
	JOT(8, "VIDIOC_ENUMAUDIO\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_ENUMAUDOUT: {
	static struct v4l2_audioout v4l2_audioout;

	JOT(8, "VIDIOC_ENUMAUDOUT\n");

	if (0 != copy_from_user(&v4l2_audioout, (void __user *)arg, \
					sizeof(struct v4l2_audioout))) {
		POUT;
		return -EFAULT;
	}

	if (0 != v4l2_audioout.index)
		return -EINVAL;
	memset(&v4l2_audioout, 0, sizeof(struct v4l2_audioout));
	v4l2_audioout.index = 0;
	strcpy(&v4l2_audioout.name[0], "Soundtrack");

	if (0 != copy_to_user((void __user *)arg, &v4l2_audioout, \
					sizeof(struct v4l2_audioout))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_QUERYCTRL: {
	static int i1;
	static struct v4l2_queryctrl v4l2_queryctrl;

	JOT(8, "VIDIOC_QUERYCTRL\n");

	if (0 != copy_from_user(&v4l2_queryctrl, (void __user *)arg, \
					sizeof(struct v4l2_queryctrl))) {
		POUT;
		return -EFAULT;
	}

	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (easycap_control[i1].id == v4l2_queryctrl.id) {
			JOT(8, "VIDIOC_QUERYCTRL  %s=easycap_control[%i]" \
				".name\n", &easycap_control[i1].name[0], i1);
			memcpy(&v4l2_queryctrl, &easycap_control[i1], \
						sizeof(struct v4l2_queryctrl));
			break;
		}
		i1++;
	}
	if (0xFFFFFFFF == easycap_control[i1].id) {
		JOT(8, "%i=index: exhausts controls\n", i1);
		return -EINVAL;
	}
	if (0 != copy_to_user((void __user *)arg, &v4l2_queryctrl, \
					sizeof(struct v4l2_queryctrl))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_QUERYMENU: {
	JOT(8, "VIDIOC_QUERYMENU unsupported\n");
	return -EINVAL;
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_CTRL: {
	static struct v4l2_control v4l2_control;

	JOT(8, "VIDIOC_G_CTRL\n");

	if (0 != copy_from_user(&v4l2_control, (void __user *)arg, \
					sizeof(struct v4l2_control))) {
		POUT;
		return -EFAULT;
	}

	switch (v4l2_control.id) {
	case V4L2_CID_BRIGHTNESS: {
		v4l2_control.value = peasycap->brightness;
		JOT(8, "user enquires brightness: %i\n", v4l2_control.value);
		break;
	}
	case V4L2_CID_CONTRAST: {
		v4l2_control.value = peasycap->contrast;
		JOT(8, "user enquires contrast: %i\n", v4l2_control.value);
		break;
	}
	case V4L2_CID_SATURATION: {
		v4l2_control.value = peasycap->saturation;
		JOT(8, "user enquires saturation: %i\n", v4l2_control.value);
		break;
	}
	case V4L2_CID_HUE: {
		v4l2_control.value = peasycap->hue;
		JOT(8, "user enquires hue: %i\n", v4l2_control.value);
		break;
	}
	case V4L2_CID_AUDIO_VOLUME: {
		v4l2_control.value = peasycap->volume;
		JOT(8, "user enquires volume: %i\n", v4l2_control.value);
		break;
	}
	case V4L2_CID_AUDIO_MUTE: {
		if (1 == peasycap->mute)
			v4l2_control.value = true;
		else
			v4l2_control.value = false;
		JOT(8, "user enquires mute: %i\n", v4l2_control.value);
		break;
	}
	default: {
		SAY("ERROR: unknown V4L2 control: 0x%08X=id\n", \
							v4l2_control.id);
		explain_cid(v4l2_control.id);
		return -EINVAL;
	}
	}
	if (0 != copy_to_user((void __user *)arg, &v4l2_control, \
					sizeof(struct v4l2_control))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#if defined(VIDIOC_S_CTRL_OLD)
case VIDIOC_S_CTRL_OLD: {
	JOT(8, "VIDIOC_S_CTRL_OLD required at least for xawtv\n");
}
#endif /*VIDIOC_S_CTRL_OLD*/
case VIDIOC_S_CTRL:
	{
	static struct v4l2_control v4l2_control;

	JOT(8, "VIDIOC_S_CTRL\n");

	if (0 != copy_from_user(&v4l2_control, (void __user *)arg, \
					sizeof(struct v4l2_control))) {
		POUT;
		return -EFAULT;
	}

	switch (v4l2_control.id) {
	case V4L2_CID_BRIGHTNESS: {
		JOT(8, "user requests brightness %i\n", v4l2_control.value);
		if (0 != adjust_brightness(peasycap, v4l2_control.value))
			;
		break;
	}
	case V4L2_CID_CONTRAST: {
		JOT(8, "user requests contrast %i\n", v4l2_control.value);
		if (0 != adjust_contrast(peasycap, v4l2_control.value))
			;
		break;
	}
	case V4L2_CID_SATURATION: {
		JOT(8, "user requests saturation %i\n", v4l2_control.value);
		if (0 != adjust_saturation(peasycap, v4l2_control.value))
			;
		break;
	}
	case V4L2_CID_HUE: {
		JOT(8, "user requests hue %i\n", v4l2_control.value);
		if (0 != adjust_hue(peasycap, v4l2_control.value))
			;
		break;
	}
	case V4L2_CID_AUDIO_VOLUME: {
		JOT(8, "user requests volume %i\n", v4l2_control.value);
		if (0 != adjust_volume(peasycap, v4l2_control.value))
			;
		break;
	}
	case V4L2_CID_AUDIO_MUTE: {
		int mute;

		JOT(8, "user requests mute %i\n", v4l2_control.value);
		if (true == v4l2_control.value)
			mute = 1;
		else
			mute = 0;

		if (0 != adjust_mute(peasycap, mute))
			SAY("WARNING: failed to adjust mute to %i\n", mute);
		break;
	}
	default: {
		SAY("ERROR: unknown V4L2 control: 0x%08X=id\n", \
							v4l2_control.id);
		explain_cid(v4l2_control.id);
	return -EINVAL;
			}
		}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_EXT_CTRLS: {
	JOT(8, "VIDIOC_S_EXT_CTRLS unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_ENUM_FMT: {
	static __u32 index;
	static struct v4l2_fmtdesc v4l2_fmtdesc;

	JOT(8, "VIDIOC_ENUM_FMT\n");

	if (0 != copy_from_user(&v4l2_fmtdesc, (void __user *)arg, \
					sizeof(struct v4l2_fmtdesc))) {
		POUT;
		return -EFAULT;
	}

	index = v4l2_fmtdesc.index;
	memset(&v4l2_fmtdesc, 0, sizeof(struct v4l2_fmtdesc));

	v4l2_fmtdesc.index = index;
	v4l2_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	switch (index) {
	case 0: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "uyvy");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_UYVY;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	case 1: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "yuy2");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_YUYV;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	case 2: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "rgb24");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_RGB24;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	case 3: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "rgb32");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_RGB32;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	case 4: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "bgr24");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_BGR24;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	case 5: {
		v4l2_fmtdesc.flags = 0;
		strcpy(&v4l2_fmtdesc.description[0], "bgr32");
		v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_BGR32;
		JOT(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
		break;
	}
	default: {
		JOT(8, "%i=index: exhausts formats\n", index);
		return -EINVAL;
	}
	}
	if (0 != copy_to_user((void __user *)arg, &v4l2_fmtdesc, \
					sizeof(struct v4l2_fmtdesc))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_ENUM_FRAMESIZES: {
	JOT(8, "VIDIOC_ENUM_FRAMESIZES unsupported\n");
	return -EINVAL;
}
case VIDIOC_ENUM_FRAMEINTERVALS: {
	JOT(8, "VIDIOC_ENUM_FRAME_INTERVALS unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_FMT: {
	static struct v4l2_format v4l2_format;
	static struct v4l2_pix_format v4l2_pix_format;

	JOT(8, "VIDIOC_G_FMT\n");

	if (0 != copy_from_user(&v4l2_format, (void __user *)arg, \
					sizeof(struct v4l2_format))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		POUT;
		return -EINVAL;
	}

	memset(&v4l2_pix_format, 0, sizeof(struct v4l2_pix_format));
	v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	memcpy(&(v4l2_format.fmt.pix), \
			 &(easycap_format[peasycap->format_offset]\
			.v4l2_format.fmt.pix), sizeof(v4l2_pix_format));
	JOT(8, "user is told: %s\n", \
			&easycap_format[peasycap->format_offset].name[0]);

	if (0 != copy_to_user((void __user *)arg, &v4l2_format, \
					sizeof(struct v4l2_format))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_TRY_FMT:
case VIDIOC_S_FMT: {
	static struct v4l2_format v4l2_format;
	static struct v4l2_pix_format v4l2_pix_format;
	static bool try;
	static int best_format;

	if (VIDIOC_TRY_FMT == cmd) {
		JOT(8, "VIDIOC_TRY_FMT\n");
		try = true;
	} else {
		JOT(8, "VIDIOC_S_FMT\n");
		try = false;
	}

	if (0 != copy_from_user(&v4l2_format, (void __user *)arg, \
					sizeof(struct v4l2_format))) {
		POUT;
		return -EFAULT;
	}

	best_format = adjust_format(peasycap, \
					v4l2_format.fmt.pix.width, \
					v4l2_format.fmt.pix.height, \
					v4l2_format.fmt.pix.pixelformat, \
					v4l2_format.fmt.pix.field, \
					try);
	if (0 > best_format) {
		JOT(8, "WARNING: adjust_format() returned %i\n", best_format);
		return -ENOENT;
	}
/*...........................................................................*/
	memset(&v4l2_pix_format, 0, sizeof(struct v4l2_pix_format));
	v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	memcpy(&(v4l2_format.fmt.pix), &(easycap_format[best_format]\
			.v4l2_format.fmt.pix), sizeof(v4l2_pix_format));
	JOT(8, "user is told: %s\n", &easycap_format[best_format].name[0]);

	if (0 != copy_to_user((void __user *)arg, &v4l2_format, \
					sizeof(struct v4l2_format))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_CROPCAP: {
	static struct v4l2_cropcap v4l2_cropcap;

	JOT(8, "VIDIOC_CROPCAP\n");

	if (0 != copy_from_user(&v4l2_cropcap, (void __user *)arg, \
					sizeof(struct v4l2_cropcap))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_cropcap.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		JOT(8, "v4l2_cropcap.type != V4L2_BUF_TYPE_VIDEO_CAPTURE\n");

	memset(&v4l2_cropcap, 0, sizeof(struct v4l2_cropcap));
	v4l2_cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_cropcap.bounds.left      = 0;
	v4l2_cropcap.bounds.top       = 0;
	v4l2_cropcap.bounds.width     = peasycap->width;
	v4l2_cropcap.bounds.height    = peasycap->height;
	v4l2_cropcap.defrect.left     = 0;
	v4l2_cropcap.defrect.top      = 0;
	v4l2_cropcap.defrect.width    = peasycap->width;
	v4l2_cropcap.defrect.height   = peasycap->height;
	v4l2_cropcap.pixelaspect.numerator = 1;
	v4l2_cropcap.pixelaspect.denominator = 1;

	JOT(8, "user is told: %ix%i\n", peasycap->width, peasycap->height);

	if (0 != copy_to_user((void __user *)arg, &v4l2_cropcap, \
					sizeof(struct v4l2_cropcap))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_CROP:
case VIDIOC_S_CROP: {
	JOT(8, "VIDIOC_G_CROP|VIDIOC_S_CROP  unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_QUERYSTD: {
	JOT(8, "VIDIOC_QUERYSTD: " \
			"EasyCAP is incapable of detecting standard\n");
	return -EINVAL;
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*---------------------------------------------------------------------------*/
/*
 *  THE MANIPULATIONS INVOLVING last0,last1,last2,last3 CONSTITUTE A WORKAROUND
 *  FOR WHAT APPEARS TO BE A BUG IN 64-BIT mplayer.
 *  NOT NEEDED, BUT HOPEFULLY HARMLESS, FOR 32-BIT mplayer.
 */
/*---------------------------------------------------------------------------*/
case VIDIOC_ENUMSTD: {
	static int last0 = -1, last1 = -1, last2 = -1, last3 = -1;
	static struct v4l2_standard v4l2_standard;
	static __u32 index;
	static struct easycap_standard const *peasycap_standard;

	JOT(8, "VIDIOC_ENUMSTD\n");

	if (0 != copy_from_user(&v4l2_standard, (void __user *)arg, \
					sizeof(struct v4l2_standard))) {
		POUT;
		return -EFAULT;
	}
	index = v4l2_standard.index;

	last3 = last2; last2 = last1; last1 = last0; last0 = index;
	if ((index == last3) && (index == last2) && \
			(index == last1) && (index == last0)) {
		index++;
		last3 = last2; last2 = last1; last1 = last0; last0 = index;
	}

	memset(&v4l2_standard, 0, sizeof(struct v4l2_standard));

	peasycap_standard = &easycap_standard[0];
	while (0xFFFF != peasycap_standard->mask) {
		if ((int)(peasycap_standard - &easycap_standard[0]) == index)
			break;
		peasycap_standard++;
	}
	if (0xFFFF == peasycap_standard->mask) {
		JOT(8, "%i=index: exhausts standards\n", index);
		return -EINVAL;
	}
	JOT(8, "%i=index: %s\n", index, \
				&(peasycap_standard->v4l2_standard.name[0]));
	memcpy(&v4l2_standard, &(peasycap_standard->v4l2_standard), \
					sizeof(struct v4l2_standard));

	v4l2_standard.index = index;

	if (0 != copy_to_user((void __user *)arg, &v4l2_standard, \
					sizeof(struct v4l2_standard))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_STD: {
	static v4l2_std_id std_id;
	static struct easycap_standard const *peasycap_standard;

	JOT(8, "VIDIOC_G_STD\n");

	if (0 != copy_from_user(&std_id, (void __user *)arg, \
						sizeof(v4l2_std_id))) {
		POUT;
		return -EFAULT;
	}

	peasycap_standard = &easycap_standard[peasycap->standard_offset];
	std_id = peasycap_standard->v4l2_standard.id;

	JOT(8, "user is told: %s\n", \
				&peasycap_standard->v4l2_standard.name[0]);

	if (0 != copy_to_user((void __user *)arg, &std_id, \
						sizeof(v4l2_std_id))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_STD: {
	static v4l2_std_id std_id;
	static int rc;

	JOT(8, "VIDIOC_S_STD\n");

	if (0 != copy_from_user(&std_id, (void __user *)arg, \
						sizeof(v4l2_std_id))) {
		POUT;
		return -EFAULT;
	}

	rc = adjust_standard(peasycap, std_id);
	if (0 > rc) {
		JOT(8, "WARNING: adjust_standard() returned %i\n", rc);
		return -ENOENT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_REQBUFS: {
	static int nbuffers;
	static struct v4l2_requestbuffers v4l2_requestbuffers;

	JOT(8, "VIDIOC_REQBUFS\n");

	if (0 != copy_from_user(&v4l2_requestbuffers, (void __user *)arg, \
				sizeof(struct v4l2_requestbuffers))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_requestbuffers.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (v4l2_requestbuffers.memory != V4L2_MEMORY_MMAP) {
		POUT;
		return -EINVAL;
	}
	nbuffers = v4l2_requestbuffers.count;
	JOT(8, "                   User requests %i buffers ...\n", nbuffers);
	if (nbuffers < 2)
		nbuffers = 2;
	if (nbuffers > FRAME_BUFFER_MANY)
		nbuffers = FRAME_BUFFER_MANY;
	if (v4l2_requestbuffers.count == nbuffers) {
		JOT(8, "                   ... agree to  %i buffers\n", \
								nbuffers);
	} else {
		JOT(8, "                  ... insist on  %i buffers\n", \
								nbuffers);
		v4l2_requestbuffers.count = nbuffers;
	}
	peasycap->frame_buffer_many = nbuffers;

	if (0 != copy_to_user((void __user *)arg, &v4l2_requestbuffers, \
				sizeof(struct v4l2_requestbuffers))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_QUERYBUF: {
	static __u32 index;
	static struct v4l2_buffer v4l2_buffer;

	JOT(8, "VIDIOC_QUERYBUF\n");

	if (peasycap->video_eof) {
		JOT(8, "returning -1 because  %i=video_eof\n", \
							peasycap->video_eof);
		return -1;
	}

	if (0 != copy_from_user(&v4l2_buffer, (void __user *)arg, \
					sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	index = v4l2_buffer.index;
	if (index < 0 || index >= peasycap->frame_buffer_many)
		return -EINVAL;
	memset(&v4l2_buffer, 0, sizeof(struct v4l2_buffer));
	v4l2_buffer.index = index;
	v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_buffer.bytesused = peasycap->frame_buffer_used;
	v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED | \
						peasycap->done[index] | \
						peasycap->queued[index];
	v4l2_buffer.field = peasycap->field;
	v4l2_buffer.memory = V4L2_MEMORY_MMAP;
	v4l2_buffer.m.offset = index * FRAME_BUFFER_SIZE;
	v4l2_buffer.length = FRAME_BUFFER_SIZE;

	JOT(16, "  %10i=index\n", v4l2_buffer.index);
	JOT(16, "  0x%08X=type\n", v4l2_buffer.type);
	JOT(16, "  %10i=bytesused\n", v4l2_buffer.bytesused);
	JOT(16, "  0x%08X=flags\n", v4l2_buffer.flags);
	JOT(16, "  %10i=field\n", v4l2_buffer.field);
	JOT(16, "  %10li=timestamp.tv_usec\n", \
					 (long)v4l2_buffer.timestamp.tv_usec);
	JOT(16, "  %10i=sequence\n", v4l2_buffer.sequence);
	JOT(16, "  0x%08X=memory\n", v4l2_buffer.memory);
	JOT(16, "  %10i=m.offset\n", v4l2_buffer.m.offset);
	JOT(16, "  %10i=length\n", v4l2_buffer.length);

	if (0 != copy_to_user((void __user *)arg, &v4l2_buffer, \
					sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_QBUF: {
	static struct v4l2_buffer v4l2_buffer;

	JOT(8, "VIDIOC_QBUF\n");

	if (0 != copy_from_user(&v4l2_buffer, (void __user *)arg, \
					sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (v4l2_buffer.memory != V4L2_MEMORY_MMAP)
		return -EINVAL;
	if (v4l2_buffer.index < 0 || \
		 (v4l2_buffer.index >= peasycap->frame_buffer_many))
		return -EINVAL;
	v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;

	peasycap->done[v4l2_buffer.index]   = 0;
	peasycap->queued[v4l2_buffer.index] = V4L2_BUF_FLAG_QUEUED;

	if (0 != copy_to_user((void __user *)arg, &v4l2_buffer, \
					sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}

	JOT(8, ".....   user queueing frame buffer %i\n", \
						(int)v4l2_buffer.index);

	peasycap->frame_lock = 0;

	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_DQBUF:
	{
#if defined(AUDIOTIME)
	static struct signed_div_result sdr;
	static long long int above, below, dnbydt, fudge, sll;
	static unsigned long long int ull;
	static struct timeval timeval0;
	struct timeval timeval1;
#endif /*AUDIOTIME*/
	static struct timeval timeval, timeval2;
	static int i, j;
	static struct v4l2_buffer v4l2_buffer;

	JOT(8, "VIDIOC_DQBUF\n");

	if ((peasycap->video_idle) || (peasycap->video_eof)) {
		JOT(8, "returning -EIO because  " \
				"%i=video_idle  %i=video_eof\n", \
				peasycap->video_idle, peasycap->video_eof);
		return -EIO;
	}

	if (0 != copy_from_user(&v4l2_buffer, (void __user *)arg, \
					sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!peasycap->video_isoc_streaming) {
		JOT(16, "returning -EIO because video urbs not streaming\n");
		return -EIO;
	}
/*---------------------------------------------------------------------------*/
/*
 *  IF THE USER HAS PREVIOUSLY CALLED easycap_poll(), AS DETERMINED BY FINDING
 *  THE FLAG peasycap->polled SET, THERE MUST BE NO FURTHER WAIT HERE.  IN THIS
 *  CASE, JUST CHOOSE THE FRAME INDICATED BY peasycap->frame_read
 */
/*---------------------------------------------------------------------------*/

	if (!peasycap->polled) {
		if (-EIO == easycap_dqbuf(peasycap, 0))
			return -EIO;
	} else {
		if (peasycap->video_eof)
			return -EIO;
	}
	if (V4L2_BUF_FLAG_DONE != peasycap->done[peasycap->frame_read]) {
		SAY("ERROR: V4L2_BUF_FLAG_DONE != 0x%08X\n", \
					peasycap->done[peasycap->frame_read]);
	}
	peasycap->polled = 0;

	if (!(isequence % 10)) {
		for (i = 0; i < 179; i++)
			peasycap->merit[i] = peasycap->merit[i+1];
		peasycap->merit[179] = merit_saa(peasycap->pusb_device);
		j = 0;
		for (i = 0; i < 180; i++)
			j += peasycap->merit[i];
		if (90 < j) {
			SAY("easycap driver shutting down " \
							"on condition blue\n");
			peasycap->video_eof = 1; peasycap->audio_eof = 1;
		}
	}

	v4l2_buffer.index = peasycap->frame_read;
	v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_buffer.bytesused = peasycap->frame_buffer_used;
	v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;
	v4l2_buffer.field =  peasycap->field;
	if (V4L2_FIELD_ALTERNATE == v4l2_buffer.field)
		v4l2_buffer.field = \
				0x000F & (peasycap->\
				frame_buffer[peasycap->frame_read][0].kount);
	do_gettimeofday(&timeval);
	timeval2 = timeval;

#if defined(AUDIOTIME)
	if (!peasycap->timeval0.tv_sec) {
		timeval0 = timeval;
		timeval1 = timeval;
		timeval2 = timeval;
		dnbydt = 192000;

		if (mutex_lock_interruptible(&(peasycap->mutex_timeval0)))
			return -ERESTARTSYS;
		peasycap->timeval0 = timeval0;
		mutex_unlock(&(peasycap->mutex_timeval0));
	} else {
		if (mutex_lock_interruptible(&(peasycap->mutex_timeval1)))
			return -ERESTARTSYS;
		dnbydt = peasycap->dnbydt;
		timeval1 = peasycap->timeval1;
		mutex_unlock(&(peasycap->mutex_timeval1));
		above = dnbydt * MICROSECONDS(timeval, timeval1);
		below = 192000;
		sdr = signed_div(above, below);

		above = sdr.quotient + timeval1.tv_usec - 350000;

		below = 1000000;
		sdr = signed_div(above, below);
		timeval2.tv_usec = sdr.remainder;
		timeval2.tv_sec = timeval1.tv_sec + sdr.quotient;
	}
	if (!(isequence % 500)) {
		fudge = ((long long int)(1000000)) * \
				((long long int)(timeval.tv_sec - \
						timeval2.tv_sec)) + \
				(long long int)(timeval.tv_usec - \
				timeval2.tv_usec);
		sdr = signed_div(fudge, 1000);
		sll = sdr.quotient;
		ull = sdr.remainder;

		SAY("%5lli.%-3lli=ms timestamp fudge\n", sll, ull);
	}
#endif /*AUDIOTIME*/

	v4l2_buffer.timestamp = timeval2;
	v4l2_buffer.sequence = isequence++;
	v4l2_buffer.memory = V4L2_MEMORY_MMAP;
	v4l2_buffer.m.offset = v4l2_buffer.index * FRAME_BUFFER_SIZE;
	v4l2_buffer.length = FRAME_BUFFER_SIZE;

	JOT(16, "  %10i=index\n", v4l2_buffer.index);
	JOT(16, "  0x%08X=type\n", v4l2_buffer.type);
	JOT(16, "  %10i=bytesused\n", v4l2_buffer.bytesused);
	JOT(16, "  0x%08X=flags\n", v4l2_buffer.flags);
	JOT(16, "  %10i=field\n", v4l2_buffer.field);
	JOT(16, "  %10li=timestamp.tv_usec\n", \
					(long)v4l2_buffer.timestamp.tv_usec);
	JOT(16, "  %10i=sequence\n", v4l2_buffer.sequence);
	JOT(16, "  0x%08X=memory\n", v4l2_buffer.memory);
	JOT(16, "  %10i=m.offset\n", v4l2_buffer.m.offset);
	JOT(16, "  %10i=length\n", v4l2_buffer.length);

	if (0 != copy_to_user((void __user *)arg, &v4l2_buffer, \
						sizeof(struct v4l2_buffer))) {
		POUT;
		return -EFAULT;
	}

	JOT(8, "..... user is offered frame buffer %i\n", \
							peasycap->frame_read);
	peasycap->frame_lock = 1;
	if (peasycap->frame_read == peasycap->frame_fill) {
		if (peasycap->frame_lock) {
			JOT(8, "ERROR:  filling frame buffer " \
						"while offered to user\n");
		}
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*---------------------------------------------------------------------------*/
/*
 *  AUDIO URBS HAVE ALREADY BEEN SUBMITTED WHEN THIS COMMAND IS RECEIVED;
 *  VIDEO URBS HAVE NOT.
 */
/*---------------------------------------------------------------------------*/
case VIDIOC_STREAMON: {
	static int i;

	JOT(8, "VIDIOC_STREAMON\n");

	isequence = 0;
	for (i = 0; i < 180; i++)
		peasycap->merit[i] = 0;
	if ((struct usb_device *)NULL == peasycap->pusb_device) {
		SAY("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	submit_video_urbs(peasycap);
	peasycap->video_idle = 0;
	peasycap->audio_idle = 0;
	peasycap->video_eof = 0;
	peasycap->audio_eof = 0;
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_STREAMOFF: {
	JOT(8, "VIDIOC_STREAMOFF\n");

	if ((struct usb_device *)NULL == peasycap->pusb_device) {
		SAY("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}

	peasycap->video_idle = 1;
	peasycap->audio_idle = 1;  peasycap->timeval0.tv_sec = 0;
/*---------------------------------------------------------------------------*/
/*
 *  IF THE WAIT QUEUES ARE NOT CLEARED IN RESPONSE TO THE STREAMOFF COMMAND
 *  THE USERSPACE PROGRAM, E.G. mplayer, MAY HANG ON EXIT.   BEWARE.
 */
/*---------------------------------------------------------------------------*/
	JOT(8, "calling wake_up on wq_video and wq_audio\n");
	wake_up_interruptible(&(peasycap->wq_video));
	wake_up_interruptible(&(peasycap->wq_audio));
/*---------------------------------------------------------------------------*/
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_PARM: {
	static struct v4l2_streamparm v4l2_streamparm;

	JOT(8, "VIDIOC_G_PARM\n");

	if (0 != copy_from_user(&v4l2_streamparm, (void __user *)arg, \
					sizeof(struct v4l2_streamparm))) {
		POUT;
		return -EFAULT;
	}

	if (v4l2_streamparm.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		POUT;
		return -EINVAL;
	}
	v4l2_streamparm.parm.capture.capability = 0;
	v4l2_streamparm.parm.capture.capturemode = 0;
	v4l2_streamparm.parm.capture.timeperframe.numerator = 1;
	v4l2_streamparm.parm.capture.timeperframe.denominator = 30;
	v4l2_streamparm.parm.capture.readbuffers = peasycap->frame_buffer_many;
	v4l2_streamparm.parm.capture.extendedmode = 0;
	if (0 != copy_to_user((void __user *)arg, &v4l2_streamparm, \
					sizeof(struct v4l2_streamparm))) {
		POUT;
		return -EFAULT;
	}
	break;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_PARM: {
	JOT(8, "VIDIOC_S_PARM unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_AUDIO: {
	JOT(8, "VIDIOC_G_AUDIO unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_AUDIO: {
	JOT(8, "VIDIOC_S_AUDIO unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_S_TUNER: {
	JOT(8, "VIDIOC_S_TUNER unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_FBUF:
case VIDIOC_S_FBUF:
case VIDIOC_OVERLAY: {
	JOT(8, "VIDIOC_G_FBUF|VIDIOC_S_FBUF|VIDIOC_OVERLAY unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
case VIDIOC_G_TUNER: {
	JOT(8, "VIDIOC_G_TUNER unsupported\n");
	return -EINVAL;
}
case VIDIOC_G_FREQUENCY:
case VIDIOC_S_FREQUENCY: {
	JOT(8, "VIDIOC_G_FREQUENCY|VIDIOC_S_FREQUENCY unsupported\n");
	return -EINVAL;
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
default: {
	JOT(8, "ERROR: unrecognized V4L2 IOCTL command: 0x%08X\n", cmd);
	explain_ioctl(cmd);
	POUT;
	return -ENOIOCTLCMD;
}
}
return 0;
}

long easycap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file->f_dentry->d_inode;
	long ret;

	lock_kernel();
	ret = easycap_ioctl_bkl(inode, file, cmd, arg);
	unlock_kernel();

	return ret;
}

/*--------------------------------------------------------------------------*/
static int easysnd_ioctl_bkl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
struct easycap *peasycap;
struct usb_device *p;

peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	return -1;
}
p = peasycap->pusb_device;
/*---------------------------------------------------------------------------*/
switch (cmd) {
case SNDCTL_DSP_GETCAPS: {
	int caps;
	JOT(8, "SNDCTL_DSP_GETCAPS\n");

#if defined(UPSAMPLE)
	if (true == peasycap->microphone)
		caps = 0x04400000;
	else
		caps = 0x04400000;
#else
	if (true == peasycap->microphone)
		caps = 0x02400000;
	else
		caps = 0x04400000;
#endif /*UPSAMPLE*/

	if (0 != copy_to_user((void __user *)arg, &caps, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_GETFMTS: {
	int incoming;
	JOT(8, "SNDCTL_DSP_GETFMTS\n");

#if defined(UPSAMPLE)
	if (true == peasycap->microphone)
		incoming = AFMT_S16_LE;
	else
		incoming = AFMT_S16_LE;
#else
	if (true == peasycap->microphone)
		incoming = AFMT_S16_LE;
	else
		incoming = AFMT_S16_LE;
#endif /*UPSAMPLE*/

	if (0 != copy_to_user((void __user *)arg, &incoming, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_SETFMT: {
	int incoming, outgoing;
	JOT(8, "SNDCTL_DSP_SETFMT\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);

#if defined(UPSAMPLE)
	if (true == peasycap->microphone)
		outgoing = AFMT_S16_LE;
	else
		outgoing = AFMT_S16_LE;
#else
	if (true == peasycap->microphone)
		outgoing = AFMT_S16_LE;
	else
		outgoing = AFMT_S16_LE;
#endif /*UPSAMPLE*/

	if (incoming != outgoing) {
		JOT(8, "........... %i=outgoing\n", outgoing);
		JOT(8, "        cf. %i=AFMT_S16_LE\n", AFMT_S16_LE);
		JOT(8, "        cf. %i=AFMT_U8\n", AFMT_U8);
		if (0 != copy_to_user((void __user *)arg, &outgoing, \
								sizeof(int)))
			return -EFAULT;
		return -EINVAL ;
	}
	break;
}
case SNDCTL_DSP_STEREO: {
	int incoming;
	JOT(8, "SNDCTL_DSP_STEREO\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);

#if defined(UPSAMPLE)
	if (true == peasycap->microphone)
		incoming = 1;
	else
		incoming = 1;
#else
	if (true == peasycap->microphone)
		incoming = 0;
	else
		incoming = 1;
#endif /*UPSAMPLE*/

	if (0 != copy_to_user((void __user *)arg, &incoming, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_SPEED: {
	int incoming;
	JOT(8, "SNDCTL_DSP_SPEED\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);

#if defined(UPSAMPLE)
	if (true == peasycap->microphone)
		incoming = 32000;
	else
		incoming = 48000;
#else
	if (true == peasycap->microphone)
		incoming = 8000;
	else
		incoming = 48000;
#endif /*UPSAMPLE*/

	if (0 != copy_to_user((void __user *)arg, &incoming, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_GETTRIGGER: {
	int incoming;
	JOT(8, "SNDCTL_DSP_GETTRIGGER\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);

	incoming = PCM_ENABLE_INPUT;
	if (0 != copy_to_user((void __user *)arg, &incoming, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_SETTRIGGER: {
	int incoming;
	JOT(8, "SNDCTL_DSP_SETTRIGGER\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);
	JOT(8, "........... cf 0x%x=PCM_ENABLE_INPUT " \
				"0x%x=PCM_ENABLE_OUTPUT\n", \
					PCM_ENABLE_INPUT, PCM_ENABLE_OUTPUT);
	;
	;
	;
	;
	break;
}
case SNDCTL_DSP_GETBLKSIZE: {
	int incoming;
	JOT(8, "SNDCTL_DSP_GETBLKSIZE\n");
	if (0 != copy_from_user(&incoming, (void __user *)arg, sizeof(int)))
		return -EFAULT;
	JOT(8, "........... %i=incoming\n", incoming);
	incoming = peasycap->audio_bytes_per_fragment;
	if (0 != copy_to_user((void __user *)arg, &incoming, sizeof(int)))
		return -EFAULT;
	break;
}
case SNDCTL_DSP_GETISPACE: {
	struct audio_buf_info audio_buf_info;

	JOT(8, "SNDCTL_DSP_GETISPACE\n");

	audio_buf_info.bytes      = peasycap->audio_bytes_per_fragment;
	audio_buf_info.fragments  = 1;
	audio_buf_info.fragsize   = 0;
	audio_buf_info.fragstotal = 0;

	if (0 != copy_to_user((void __user *)arg, &audio_buf_info, \
								sizeof(int)))
		return -EFAULT;
	break;
}
default: {
	JOT(8, "ERROR: unrecognized DSP IOCTL command: 0x%08X\n", cmd);
	POUT;
	return -ENOIOCTLCMD;
}
}
return 0;
}

long easysnd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file->f_dentry->d_inode;
	long ret;

	lock_kernel();
	ret = easysnd_ioctl_bkl(inode, file, cmd, arg);
	unlock_kernel();

	return ret;
}

/*****************************************************************************/
int explain_ioctl(__u32 wot)
{
int k;
/*---------------------------------------------------------------------------*/
/*
 *  THE DATA FOR THE ARRAY mess BELOW WERE CONSTRUCTED BY RUNNING THE FOLLOWING
 *  SHELL SCRIPT:
 *  #
 *  cat /usr/src/linux-headers-`uname -r`/include/linux/videodev2.h | \
 *     grep "^#define VIDIOC_" - | grep -v "_OLD" - | \
 *     sed -e "s,_IO.*$,,;p" | sed -e "N;s,\n,, " | \
 *     sed -e "s/^#define /  {/;s/#define /, \"/;s/$/\"},/" | \
 *     sed -e "s,	,,g;s, ,,g" >ioctl.tmp
 *  echo "{0xFFFFFFFF,\"\"}" >>ioctl.tmp
 *  exit 0
 *  #
 * AND REINSTATING THE EXCISED "_OLD" CASES WERE LATER MANUALLY.
 *
 * THE DATA FOR THE ARRAY mess1 BELOW WERE CONSTRUCTED BY RUNNING THE FOLLOWING
 * SHELL SCRIPT:
 *  cat /usr/src/linux-headers-`uname -r`/include/linux/videodev.h | \
 *     grep "^#define VIDIOC" - | grep -v "_OLD" - | \
 *     sed -e "s,_IO.*$,,;p" | sed -e "N;s,\n,, " | \
 *     sed -e "s/^#define /  {/;s/#define /, \"/;s/$/\"},/" | \
 *     sed -e "s,   ,,g;s, ,,g" >ioctl.tmp
 *  echo "{0xFFFFFFFF,\"\"}" >>ioctl.tmp
 *  exit 0
 *  #
 */
/*---------------------------------------------------------------------------*/
static struct mess {
	__u32 command;
	char  name[64];
} mess[] = {
#if defined(VIDIOC_QUERYCAP)
{VIDIOC_QUERYCAP, "VIDIOC_QUERYCAP"},
#endif
#if defined(VIDIOC_RESERVED)
{VIDIOC_RESERVED, "VIDIOC_RESERVED"},
#endif
#if defined(VIDIOC_ENUM_FMT)
{VIDIOC_ENUM_FMT, "VIDIOC_ENUM_FMT"},
#endif
#if defined(VIDIOC_G_FMT)
{VIDIOC_G_FMT, "VIDIOC_G_FMT"},
#endif
#if defined(VIDIOC_S_FMT)
{VIDIOC_S_FMT, "VIDIOC_S_FMT"},
#endif
#if defined(VIDIOC_REQBUFS)
{VIDIOC_REQBUFS, "VIDIOC_REQBUFS"},
#endif
#if defined(VIDIOC_QUERYBUF)
{VIDIOC_QUERYBUF, "VIDIOC_QUERYBUF"},
#endif
#if defined(VIDIOC_G_FBUF)
{VIDIOC_G_FBUF, "VIDIOC_G_FBUF"},
#endif
#if defined(VIDIOC_S_FBUF)
{VIDIOC_S_FBUF, "VIDIOC_S_FBUF"},
#endif
#if defined(VIDIOC_OVERLAY)
{VIDIOC_OVERLAY, "VIDIOC_OVERLAY"},
#endif
#if defined(VIDIOC_QBUF)
{VIDIOC_QBUF, "VIDIOC_QBUF"},
#endif
#if defined(VIDIOC_DQBUF)
{VIDIOC_DQBUF, "VIDIOC_DQBUF"},
#endif
#if defined(VIDIOC_STREAMON)
{VIDIOC_STREAMON, "VIDIOC_STREAMON"},
#endif
#if defined(VIDIOC_STREAMOFF)
{VIDIOC_STREAMOFF, "VIDIOC_STREAMOFF"},
#endif
#if defined(VIDIOC_G_PARM)
{VIDIOC_G_PARM, "VIDIOC_G_PARM"},
#endif
#if defined(VIDIOC_S_PARM)
{VIDIOC_S_PARM, "VIDIOC_S_PARM"},
#endif
#if defined(VIDIOC_G_STD)
{VIDIOC_G_STD, "VIDIOC_G_STD"},
#endif
#if defined(VIDIOC_S_STD)
{VIDIOC_S_STD, "VIDIOC_S_STD"},
#endif
#if defined(VIDIOC_ENUMSTD)
{VIDIOC_ENUMSTD, "VIDIOC_ENUMSTD"},
#endif
#if defined(VIDIOC_ENUMINPUT)
{VIDIOC_ENUMINPUT, "VIDIOC_ENUMINPUT"},
#endif
#if defined(VIDIOC_G_CTRL)
{VIDIOC_G_CTRL, "VIDIOC_G_CTRL"},
#endif
#if defined(VIDIOC_S_CTRL)
{VIDIOC_S_CTRL, "VIDIOC_S_CTRL"},
#endif
#if defined(VIDIOC_G_TUNER)
{VIDIOC_G_TUNER, "VIDIOC_G_TUNER"},
#endif
#if defined(VIDIOC_S_TUNER)
{VIDIOC_S_TUNER, "VIDIOC_S_TUNER"},
#endif
#if defined(VIDIOC_G_AUDIO)
{VIDIOC_G_AUDIO, "VIDIOC_G_AUDIO"},
#endif
#if defined(VIDIOC_S_AUDIO)
{VIDIOC_S_AUDIO, "VIDIOC_S_AUDIO"},
#endif
#if defined(VIDIOC_QUERYCTRL)
{VIDIOC_QUERYCTRL, "VIDIOC_QUERYCTRL"},
#endif
#if defined(VIDIOC_QUERYMENU)
{VIDIOC_QUERYMENU, "VIDIOC_QUERYMENU"},
#endif
#if defined(VIDIOC_G_INPUT)
{VIDIOC_G_INPUT, "VIDIOC_G_INPUT"},
#endif
#if defined(VIDIOC_S_INPUT)
{VIDIOC_S_INPUT, "VIDIOC_S_INPUT"},
#endif
#if defined(VIDIOC_G_OUTPUT)
{VIDIOC_G_OUTPUT, "VIDIOC_G_OUTPUT"},
#endif
#if defined(VIDIOC_S_OUTPUT)
{VIDIOC_S_OUTPUT, "VIDIOC_S_OUTPUT"},
#endif
#if defined(VIDIOC_ENUMOUTPUT)
{VIDIOC_ENUMOUTPUT, "VIDIOC_ENUMOUTPUT"},
#endif
#if defined(VIDIOC_G_AUDOUT)
{VIDIOC_G_AUDOUT, "VIDIOC_G_AUDOUT"},
#endif
#if defined(VIDIOC_S_AUDOUT)
{VIDIOC_S_AUDOUT, "VIDIOC_S_AUDOUT"},
#endif
#if defined(VIDIOC_G_MODULATOR)
{VIDIOC_G_MODULATOR, "VIDIOC_G_MODULATOR"},
#endif
#if defined(VIDIOC_S_MODULATOR)
{VIDIOC_S_MODULATOR, "VIDIOC_S_MODULATOR"},
#endif
#if defined(VIDIOC_G_FREQUENCY)
{VIDIOC_G_FREQUENCY, "VIDIOC_G_FREQUENCY"},
#endif
#if defined(VIDIOC_S_FREQUENCY)
{VIDIOC_S_FREQUENCY, "VIDIOC_S_FREQUENCY"},
#endif
#if defined(VIDIOC_CROPCAP)
{VIDIOC_CROPCAP, "VIDIOC_CROPCAP"},
#endif
#if defined(VIDIOC_G_CROP)
{VIDIOC_G_CROP, "VIDIOC_G_CROP"},
#endif
#if defined(VIDIOC_S_CROP)
{VIDIOC_S_CROP, "VIDIOC_S_CROP"},
#endif
#if defined(VIDIOC_G_JPEGCOMP)
{VIDIOC_G_JPEGCOMP, "VIDIOC_G_JPEGCOMP"},
#endif
#if defined(VIDIOC_S_JPEGCOMP)
{VIDIOC_S_JPEGCOMP, "VIDIOC_S_JPEGCOMP"},
#endif
#if defined(VIDIOC_QUERYSTD)
{VIDIOC_QUERYSTD, "VIDIOC_QUERYSTD"},
#endif
#if defined(VIDIOC_TRY_FMT)
{VIDIOC_TRY_FMT, "VIDIOC_TRY_FMT"},
#endif
#if defined(VIDIOC_ENUMAUDIO)
{VIDIOC_ENUMAUDIO, "VIDIOC_ENUMAUDIO"},
#endif
#if defined(VIDIOC_ENUMAUDOUT)
{VIDIOC_ENUMAUDOUT, "VIDIOC_ENUMAUDOUT"},
#endif
#if defined(VIDIOC_G_PRIORITY)
{VIDIOC_G_PRIORITY, "VIDIOC_G_PRIORITY"},
#endif
#if defined(VIDIOC_S_PRIORITY)
{VIDIOC_S_PRIORITY, "VIDIOC_S_PRIORITY"},
#endif
#if defined(VIDIOC_G_SLICED_VBI_CAP)
{VIDIOC_G_SLICED_VBI_CAP, "VIDIOC_G_SLICED_VBI_CAP"},
#endif
#if defined(VIDIOC_LOG_STATUS)
{VIDIOC_LOG_STATUS, "VIDIOC_LOG_STATUS"},
#endif
#if defined(VIDIOC_G_EXT_CTRLS)
{VIDIOC_G_EXT_CTRLS, "VIDIOC_G_EXT_CTRLS"},
#endif
#if defined(VIDIOC_S_EXT_CTRLS)
{VIDIOC_S_EXT_CTRLS, "VIDIOC_S_EXT_CTRLS"},
#endif
#if defined(VIDIOC_TRY_EXT_CTRLS)
{VIDIOC_TRY_EXT_CTRLS, "VIDIOC_TRY_EXT_CTRLS"},
#endif
#if defined(VIDIOC_ENUM_FRAMESIZES)
{VIDIOC_ENUM_FRAMESIZES, "VIDIOC_ENUM_FRAMESIZES"},
#endif
#if defined(VIDIOC_ENUM_FRAMEINTERVALS)
{VIDIOC_ENUM_FRAMEINTERVALS, "VIDIOC_ENUM_FRAMEINTERVALS"},
#endif
#if defined(VIDIOC_G_ENC_INDEX)
{VIDIOC_G_ENC_INDEX, "VIDIOC_G_ENC_INDEX"},
#endif
#if defined(VIDIOC_ENCODER_CMD)
{VIDIOC_ENCODER_CMD, "VIDIOC_ENCODER_CMD"},
#endif
#if defined(VIDIOC_TRY_ENCODER_CMD)
{VIDIOC_TRY_ENCODER_CMD, "VIDIOC_TRY_ENCODER_CMD"},
#endif
#if defined(VIDIOC_G_CHIP_IDENT)
{VIDIOC_G_CHIP_IDENT, "VIDIOC_G_CHIP_IDENT"},
#endif

#if defined(VIDIOC_OVERLAY_OLD)
{VIDIOC_OVERLAY_OLD, "VIDIOC_OVERLAY_OLD"},
#endif
#if defined(VIDIOC_S_PARM_OLD)
{VIDIOC_S_PARM_OLD, "VIDIOC_S_PARM_OLD"},
#endif
#if defined(VIDIOC_S_CTRL_OLD)
{VIDIOC_S_CTRL_OLD, "VIDIOC_S_CTRL_OLD"},
#endif
#if defined(VIDIOC_G_AUDIO_OLD)
{VIDIOC_G_AUDIO_OLD, "VIDIOC_G_AUDIO_OLD"},
#endif
#if defined(VIDIOC_G_AUDOUT_OLD)
{VIDIOC_G_AUDOUT_OLD, "VIDIOC_G_AUDOUT_OLD"},
#endif
#if defined(VIDIOC_CROPCAP_OLD)
{VIDIOC_CROPCAP_OLD, "VIDIOC_CROPCAP_OLD"},
#endif
{0xFFFFFFFF, ""}
};

static struct mess mess1[] = \
{
#if defined(VIDIOCGCAP)
{VIDIOCGCAP, "VIDIOCGCAP"},
#endif
#if defined(VIDIOCGCHAN)
{VIDIOCGCHAN, "VIDIOCGCHAN"},
#endif
#if defined(VIDIOCSCHAN)
{VIDIOCSCHAN, "VIDIOCSCHAN"},
#endif
#if defined(VIDIOCGTUNER)
{VIDIOCGTUNER, "VIDIOCGTUNER"},
#endif
#if defined(VIDIOCSTUNER)
{VIDIOCSTUNER, "VIDIOCSTUNER"},
#endif
#if defined(VIDIOCGPICT)
{VIDIOCGPICT, "VIDIOCGPICT"},
#endif
#if defined(VIDIOCSPICT)
{VIDIOCSPICT, "VIDIOCSPICT"},
#endif
#if defined(VIDIOCCAPTURE)
{VIDIOCCAPTURE, "VIDIOCCAPTURE"},
#endif
#if defined(VIDIOCGWIN)
{VIDIOCGWIN, "VIDIOCGWIN"},
#endif
#if defined(VIDIOCSWIN)
{VIDIOCSWIN, "VIDIOCSWIN"},
#endif
#if defined(VIDIOCGFBUF)
{VIDIOCGFBUF, "VIDIOCGFBUF"},
#endif
#if defined(VIDIOCSFBUF)
{VIDIOCSFBUF, "VIDIOCSFBUF"},
#endif
#if defined(VIDIOCKEY)
{VIDIOCKEY, "VIDIOCKEY"},
#endif
#if defined(VIDIOCGFREQ)
{VIDIOCGFREQ, "VIDIOCGFREQ"},
#endif
#if defined(VIDIOCSFREQ)
{VIDIOCSFREQ, "VIDIOCSFREQ"},
#endif
#if defined(VIDIOCGAUDIO)
{VIDIOCGAUDIO, "VIDIOCGAUDIO"},
#endif
#if defined(VIDIOCSAUDIO)
{VIDIOCSAUDIO, "VIDIOCSAUDIO"},
#endif
#if defined(VIDIOCSYNC)
{VIDIOCSYNC, "VIDIOCSYNC"},
#endif
#if defined(VIDIOCMCAPTURE)
{VIDIOCMCAPTURE, "VIDIOCMCAPTURE"},
#endif
#if defined(VIDIOCGMBUF)
{VIDIOCGMBUF, "VIDIOCGMBUF"},
#endif
#if defined(VIDIOCGUNIT)
{VIDIOCGUNIT, "VIDIOCGUNIT"},
#endif
#if defined(VIDIOCGCAPTURE)
{VIDIOCGCAPTURE, "VIDIOCGCAPTURE"},
#endif
#if defined(VIDIOCSCAPTURE)
{VIDIOCSCAPTURE, "VIDIOCSCAPTURE"},
#endif
#if defined(VIDIOCSPLAYMODE)
{VIDIOCSPLAYMODE, "VIDIOCSPLAYMODE"},
#endif
#if defined(VIDIOCSWRITEMODE)
{VIDIOCSWRITEMODE, "VIDIOCSWRITEMODE"},
#endif
#if defined(VIDIOCGPLAYINFO)
{VIDIOCGPLAYINFO, "VIDIOCGPLAYINFO"},
#endif
#if defined(VIDIOCSMICROCODE)
{VIDIOCSMICROCODE, "VIDIOCSMICROCODE"},
#endif
{0xFFFFFFFF, ""}
};

k = 0;
while (mess[k].name[0]) {
	if (wot == mess[k].command) {
		JOT(8, "ioctl 0x%08X is %s\n", \
					mess[k].command, &mess[k].name[0]);
		return 0;
	}
	k++;
}
JOT(8, "ioctl 0x%08X is not in videodev2.h\n", wot);

k = 0;
while (mess1[k].name[0]) {
	if (wot == mess1[k].command) {
		JOT(8, "ioctl 0x%08X is %s (V4L1)\n", \
					mess1[k].command, &mess1[k].name[0]);
		return 0;
	}
	k++;
}
JOT(8, "ioctl 0x%08X is not in videodev.h\n", wot);
return -1;
}
/*****************************************************************************/
int explain_cid(__u32 wot)
{
int k;
/*---------------------------------------------------------------------------*/
/*
 *  THE DATA FOR THE ARRAY mess BELOW WERE CONSTRUCTED BY RUNNING THE FOLLOWING
 *  SHELL SCRIPT:
 *  #
 *  cat /usr/src/linux-headers-`uname -r`/include/linux/videodev2.h | \
 *     grep "^#define V4L2_CID_" |  \
 *     sed -e "s,(.*$,,;p" | sed -e "N;s,\n,, " | \
 *     sed -e "s/^#define /  {/;s/#define /, \"/;s/$/\"},/" | \
 *     sed -e "s,	,,g;s, ,,g" | grep -v "_BASE" | grep -v "MPEG" >cid.tmp
 *  echo "{0xFFFFFFFF,\"\"}" >>cid.tmp
 *  exit 0
 *  #
 */
/*---------------------------------------------------------------------------*/
static struct mess
{
__u32 command;
char  name[64];
} mess[] = {
#if defined(V4L2_CID_USER_CLASS)
{V4L2_CID_USER_CLASS, "V4L2_CID_USER_CLASS"},
#endif
#if defined(V4L2_CID_BRIGHTNESS)
{V4L2_CID_BRIGHTNESS, "V4L2_CID_BRIGHTNESS"},
#endif
#if defined(V4L2_CID_CONTRAST)
{V4L2_CID_CONTRAST, "V4L2_CID_CONTRAST"},
#endif
#if defined(V4L2_CID_SATURATION)
{V4L2_CID_SATURATION, "V4L2_CID_SATURATION"},
#endif
#if defined(V4L2_CID_HUE)
{V4L2_CID_HUE, "V4L2_CID_HUE"},
#endif
#if defined(V4L2_CID_AUDIO_VOLUME)
{V4L2_CID_AUDIO_VOLUME, "V4L2_CID_AUDIO_VOLUME"},
#endif
#if defined(V4L2_CID_AUDIO_BALANCE)
{V4L2_CID_AUDIO_BALANCE, "V4L2_CID_AUDIO_BALANCE"},
#endif
#if defined(V4L2_CID_AUDIO_BASS)
{V4L2_CID_AUDIO_BASS, "V4L2_CID_AUDIO_BASS"},
#endif
#if defined(V4L2_CID_AUDIO_TREBLE)
{V4L2_CID_AUDIO_TREBLE, "V4L2_CID_AUDIO_TREBLE"},
#endif
#if defined(V4L2_CID_AUDIO_MUTE)
{V4L2_CID_AUDIO_MUTE, "V4L2_CID_AUDIO_MUTE"},
#endif
#if defined(V4L2_CID_AUDIO_LOUDNESS)
{V4L2_CID_AUDIO_LOUDNESS, "V4L2_CID_AUDIO_LOUDNESS"},
#endif
#if defined(V4L2_CID_BLACK_LEVEL)
{V4L2_CID_BLACK_LEVEL, "V4L2_CID_BLACK_LEVEL"},
#endif
#if defined(V4L2_CID_AUTO_WHITE_BALANCE)
{V4L2_CID_AUTO_WHITE_BALANCE, "V4L2_CID_AUTO_WHITE_BALANCE"},
#endif
#if defined(V4L2_CID_DO_WHITE_BALANCE)
{V4L2_CID_DO_WHITE_BALANCE, "V4L2_CID_DO_WHITE_BALANCE"},
#endif
#if defined(V4L2_CID_RED_BALANCE)
{V4L2_CID_RED_BALANCE, "V4L2_CID_RED_BALANCE"},
#endif
#if defined(V4L2_CID_BLUE_BALANCE)
{V4L2_CID_BLUE_BALANCE, "V4L2_CID_BLUE_BALANCE"},
#endif
#if defined(V4L2_CID_GAMMA)
{V4L2_CID_GAMMA, "V4L2_CID_GAMMA"},
#endif
#if defined(V4L2_CID_WHITENESS)
{V4L2_CID_WHITENESS, "V4L2_CID_WHITENESS"},
#endif
#if defined(V4L2_CID_EXPOSURE)
{V4L2_CID_EXPOSURE, "V4L2_CID_EXPOSURE"},
#endif
#if defined(V4L2_CID_AUTOGAIN)
{V4L2_CID_AUTOGAIN, "V4L2_CID_AUTOGAIN"},
#endif
#if defined(V4L2_CID_GAIN)
{V4L2_CID_GAIN, "V4L2_CID_GAIN"},
#endif
#if defined(V4L2_CID_HFLIP)
{V4L2_CID_HFLIP, "V4L2_CID_HFLIP"},
#endif
#if defined(V4L2_CID_VFLIP)
{V4L2_CID_VFLIP, "V4L2_CID_VFLIP"},
#endif
#if defined(V4L2_CID_HCENTER)
{V4L2_CID_HCENTER, "V4L2_CID_HCENTER"},
#endif
#if defined(V4L2_CID_VCENTER)
{V4L2_CID_VCENTER, "V4L2_CID_VCENTER"},
#endif
#if defined(V4L2_CID_POWER_LINE_FREQUENCY)
{V4L2_CID_POWER_LINE_FREQUENCY, "V4L2_CID_POWER_LINE_FREQUENCY"},
#endif
#if defined(V4L2_CID_HUE_AUTO)
{V4L2_CID_HUE_AUTO, "V4L2_CID_HUE_AUTO"},
#endif
#if defined(V4L2_CID_WHITE_BALANCE_TEMPERATURE)
{V4L2_CID_WHITE_BALANCE_TEMPERATURE, "V4L2_CID_WHITE_BALANCE_TEMPERATURE"},
#endif
#if defined(V4L2_CID_SHARPNESS)
{V4L2_CID_SHARPNESS, "V4L2_CID_SHARPNESS"},
#endif
#if defined(V4L2_CID_BACKLIGHT_COMPENSATION)
{V4L2_CID_BACKLIGHT_COMPENSATION, "V4L2_CID_BACKLIGHT_COMPENSATION"},
#endif
#if defined(V4L2_CID_CHROMA_AGC)
{V4L2_CID_CHROMA_AGC, "V4L2_CID_CHROMA_AGC"},
#endif
#if defined(V4L2_CID_COLOR_KILLER)
{V4L2_CID_COLOR_KILLER, "V4L2_CID_COLOR_KILLER"},
#endif
#if defined(V4L2_CID_LASTP1)
{V4L2_CID_LASTP1, "V4L2_CID_LASTP1"},
#endif
#if defined(V4L2_CID_CAMERA_CLASS)
{V4L2_CID_CAMERA_CLASS, "V4L2_CID_CAMERA_CLASS"},
#endif
#if defined(V4L2_CID_EXPOSURE_AUTO)
{V4L2_CID_EXPOSURE_AUTO, "V4L2_CID_EXPOSURE_AUTO"},
#endif
#if defined(V4L2_CID_EXPOSURE_ABSOLUTE)
{V4L2_CID_EXPOSURE_ABSOLUTE, "V4L2_CID_EXPOSURE_ABSOLUTE"},
#endif
#if defined(V4L2_CID_EXPOSURE_AUTO_PRIORITY)
{V4L2_CID_EXPOSURE_AUTO_PRIORITY, "V4L2_CID_EXPOSURE_AUTO_PRIORITY"},
#endif
#if defined(V4L2_CID_PAN_RELATIVE)
{V4L2_CID_PAN_RELATIVE, "V4L2_CID_PAN_RELATIVE"},
#endif
#if defined(V4L2_CID_TILT_RELATIVE)
{V4L2_CID_TILT_RELATIVE, "V4L2_CID_TILT_RELATIVE"},
#endif
#if defined(V4L2_CID_PAN_RESET)
{V4L2_CID_PAN_RESET, "V4L2_CID_PAN_RESET"},
#endif
#if defined(V4L2_CID_TILT_RESET)
{V4L2_CID_TILT_RESET, "V4L2_CID_TILT_RESET"},
#endif
#if defined(V4L2_CID_PAN_ABSOLUTE)
{V4L2_CID_PAN_ABSOLUTE, "V4L2_CID_PAN_ABSOLUTE"},
#endif
#if defined(V4L2_CID_TILT_ABSOLUTE)
{V4L2_CID_TILT_ABSOLUTE, "V4L2_CID_TILT_ABSOLUTE"},
#endif
#if defined(V4L2_CID_FOCUS_ABSOLUTE)
{V4L2_CID_FOCUS_ABSOLUTE, "V4L2_CID_FOCUS_ABSOLUTE"},
#endif
#if defined(V4L2_CID_FOCUS_RELATIVE)
{V4L2_CID_FOCUS_RELATIVE, "V4L2_CID_FOCUS_RELATIVE"},
#endif
#if defined(V4L2_CID_FOCUS_AUTO)
{V4L2_CID_FOCUS_AUTO, "V4L2_CID_FOCUS_AUTO"},
#endif
{0xFFFFFFFF, ""}
};

k = 0;
while (mess[k].name[0]) {
	if (wot == mess[k].command) {
		JOT(8, "ioctl 0x%08X is %s\n", \
					mess[k].command, &mess[k].name[0]);
		return 0;
	}
	k++;
}
JOT(8, "cid 0x%08X is not in videodev2.h\n", wot);
return -1;
}
/*****************************************************************************/

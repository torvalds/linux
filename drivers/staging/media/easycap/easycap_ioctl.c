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

#include <linux/version.h>
#include "easycap.h"

/*--------------------------------------------------------------------------*/
/*
 *  UNLESS THERE IS A PREMATURE ERROR RETURN THIS ROUTINE UPDATES THE
 *  FOLLOWING:
 *          peasycap->standard_offset
 *          peasycap->inputset[peasycap->input].standard_offset
 *          peasycap->fps
 *          peasycap->usec
 *          peasycap->tolerate
 *          peasycap->skip
 */
/*---------------------------------------------------------------------------*/
int adjust_standard(struct easycap *peasycap, v4l2_std_id std_id)
{
	struct easycap_standard const *peasycap_standard;
	u16 reg, set;
	int ir, rc, need, k;
	unsigned int itwas, isnow;
	bool resubmit;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	peasycap_standard = &easycap_standard[0];
	while (0xFFFF != peasycap_standard->mask) {
		if (std_id == peasycap_standard->v4l2_standard.id)
			break;
		peasycap_standard++;
	}
	if (0xFFFF == peasycap_standard->mask) {
		peasycap_standard = &easycap_standard[0];
		while (0xFFFF != peasycap_standard->mask) {
			if (std_id & peasycap_standard->v4l2_standard.id)
				break;
			peasycap_standard++;
		}
	}
	if (0xFFFF == peasycap_standard->mask) {
		SAM("ERROR: 0x%08X=std_id: standard not found\n",
		    (unsigned int)std_id);
		return -EINVAL;
	}
	SAM("selected standard: %s\n",
	    &(peasycap_standard->v4l2_standard.name[0]));
	if (peasycap->standard_offset == peasycap_standard - easycap_standard) {
		SAM("requested standard already in effect\n");
		return 0;
	}
	peasycap->standard_offset = peasycap_standard - easycap_standard;
	for (k = 0; k < INPUT_MANY;  k++) {
		if (!peasycap->inputset[k].standard_offset_ok) {
			peasycap->inputset[k].standard_offset =
				peasycap->standard_offset;
		}
	}
	if ((0 <= peasycap->input) && (INPUT_MANY > peasycap->input)) {
		peasycap->inputset[peasycap->input].standard_offset =
			peasycap->standard_offset;
		peasycap->inputset[peasycap->input].standard_offset_ok = 1;
	} else
		JOM(8, "%i=peasycap->input\n", peasycap->input);

	peasycap->fps = peasycap_standard->v4l2_standard.frameperiod.denominator /
			peasycap_standard->v4l2_standard.frameperiod.numerator;
	switch (peasycap->fps) {
	case 6:
	case 30: {
		peasycap->ntsc = true;
		break;
	}
	case 5:
	case 25: {
		peasycap->ntsc = false;
		break;
	}
	default: {
		SAM("MISTAKE: %i=frames-per-second\n", peasycap->fps);
		return -ENOENT;
	}
	}
	JOM(8, "%i frames-per-second\n", peasycap->fps);
	if (0x8000 & peasycap_standard->mask) {
		peasycap->skip = 5;
		peasycap->usec = 1000000 / (2 * (5 * peasycap->fps));
		peasycap->tolerate = 1000 * (25 / (5 * peasycap->fps));
	} else {
		peasycap->skip = 0;
		peasycap->usec = 1000000 / (2 * peasycap->fps);
		peasycap->tolerate = 1000 * (25 / peasycap->fps);
	}
	if (peasycap->video_isoc_streaming) {
		resubmit = true;
		kill_video_urbs(peasycap);
	} else
		resubmit = false;
/*--------------------------------------------------------------------------*/
/*
 *  SAA7113H DATASHEET PAGE 44, TABLE 42
 */
/*--------------------------------------------------------------------------*/
	need = 0;
	itwas = 0;
	reg = 0x00;
	set = 0x00;
	switch (peasycap_standard->mask & 0x000F) {
	case NTSC_M_JP: {
		reg = 0x0A;
		set = 0x95;
		ir = read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			SAM("ERROR: cannot read SAA register 0x%02X\n", reg);
		else
			itwas = (unsigned int)ir;
		rc = write_saa(peasycap->pusb_device, reg, set);
		if (rc)
			SAM("ERROR: failed to set SAA register "
			    "0x%02X to 0x%02X for JP standard\n", reg, set);
		else {
			isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
			if (0 > ir)
				JOM(8, "SAA register 0x%02X changed "
				    "to 0x%02X\n", reg, isnow);
			else
				JOM(8, "SAA register 0x%02X changed "
				    "from 0x%02X to 0x%02X\n", reg, itwas, isnow);
		}

		reg = 0x0B;
		set = 0x48;
		ir = read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			SAM("ERROR: cannot read SAA register 0x%02X\n", reg);
		else
			itwas = (unsigned int)ir;
		rc = write_saa(peasycap->pusb_device, reg, set);
		if (rc)
			SAM("ERROR: failed to set SAA register 0x%02X to 0x%02X "
			    "for JP standard\n", reg, set);
		else {
			isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
			if (0 > ir)
				JOM(8, "SAA register 0x%02X changed "
				    "to 0x%02X\n", reg, isnow);
			else
				JOM(8, "SAA register 0x%02X changed "
				    "from 0x%02X to 0x%02X\n", reg, itwas, isnow);
		}
/*--------------------------------------------------------------------------*/
/*
 *  NOTE:  NO break HERE:  RUN ON TO NEXT CASE
 */
/*--------------------------------------------------------------------------*/
	}
	case NTSC_M:
	case PAL_BGHIN: {
		reg = 0x0E;
		set = 0x01;
		need = 1;
		break;
	}
	case NTSC_N_443:
	case PAL_60: {
		reg = 0x0E;
		set = 0x11;
		need = 1;
		break;
	}
	case NTSC_443:
	case PAL_Nc: {
		reg = 0x0E;
		set = 0x21;
		need = 1;
		break;
	}
	case NTSC_N:
	case PAL_M: {
		reg = 0x0E;
		set = 0x31;
		need = 1;
		break;
	}
	case SECAM: {
		reg = 0x0E;
		set = 0x51;
		need = 1;
		break;
	}
	default:
		break;
	}
/*--------------------------------------------------------------------------*/
	if (need) {
		ir = read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			SAM("ERROR: failed to read SAA register 0x%02X\n", reg);
		else
			itwas = (unsigned int)ir;
		rc = write_saa(peasycap->pusb_device, reg, set);
		if (0 != write_saa(peasycap->pusb_device, reg, set)) {
			SAM("ERROR: failed to set SAA register "
			    "0x%02X to 0x%02X for table 42\n", reg, set);
		} else {
			isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
			if (0 > ir)
				JOM(8, "SAA register 0x%02X changed "
				    "to 0x%02X\n", reg, isnow);
			else
				JOM(8, "SAA register 0x%02X changed "
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
		SAM("ERROR: failed to read SAA register 0x%02X "
		    "so cannot reset\n", reg);
	else {
		itwas = (unsigned int)ir;
		if (peasycap_standard->mask & 0x0001)
			set = itwas | 0x40 ;
		else
			set = itwas & ~0x40 ;
		rc  = write_saa(peasycap->pusb_device, reg, set);
		if (rc)
			SAM("ERROR: failed to set SAA register 0x%02X to 0x%02X\n",
			    reg, set);
		else {
			isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
			if (0 > ir)
				JOM(8, "SAA register 0x%02X changed to 0x%02X\n",
				    reg, isnow);
			else
				JOM(8, "SAA register 0x%02X changed "
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
		SAM("ERROR: failed to read SAA register 0x%02X "
		    "so cannot reset\n", reg);
	else {
		itwas = (unsigned int)ir;
		if (peasycap_standard->mask & 0x0001)
			set = itwas | 0x80 ;
		else
			set = itwas & ~0x80 ;
		rc = write_saa(peasycap->pusb_device, reg, set);
		if (rc)
			SAM("ERROR: failed to set SAA register 0x%02X to 0x%02X\n",
			    reg, set);
		else {
			isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
			if (0 > ir)
				JOM(8, "SAA register 0x%02X changed to 0x%02X\n",
				    reg, isnow);
			else
				JOM(8, "SAA register 0x%02X changed "
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
		SAM("ERROR: failed to read SAA register 0x%02X but continuing\n", reg);
	itwas = (unsigned int)ir;
	if (peasycap_standard->mask & 0x0001)
		set = 0x0A ;
	else
		set = 0x07 ;
	if (0 != write_saa(peasycap->pusb_device, reg, set))
		SAM("ERROR: failed to set SAA register 0x%02X to 0x%02X\n",
		    reg, set);
	else {
		isnow = (unsigned int)read_saa(peasycap->pusb_device, reg);
		if (0 > ir)
			JOM(8, "SAA register 0x%02X changed "
			    "to 0x%02X\n", reg, isnow);
		else
			JOM(8, "SAA register 0x%02X changed "
			    "from 0x%02X to 0x%02X\n", reg, itwas, isnow);
	}
	if (resubmit)
		submit_video_urbs(peasycap);
	return 0;
}
/*****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  THE ALGORITHM FOR RESPONDING TO THE VIDIO_S_FMT IOCTL REQUIRES
 *  A VALID VALUE OF peasycap->standard_offset, OTHERWISE -EBUSY IS RETURNED.
 *
 *  PROVIDED THE ARGUMENT try IS false AND THERE IS NO PREMATURE ERROR RETURN
 *  THIS ROUTINE UPDATES THE FOLLOWING:
 *          peasycap->format_offset
 *          peasycap->inputset[peasycap->input].format_offset
 *          peasycap->pixelformat
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
int adjust_format(struct easycap *peasycap,
		  u32 width, u32 height, u32 pixelformat, int field, bool try)
{
	struct easycap_format *peasycap_format, *peasycap_best_format;
	u16 mask;
	struct usb_device *p;
	int miss, multiplier, best, k;
	char bf[5], fo[32], *pc;
	u32 uc;
	bool resubmit;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (0 > peasycap->standard_offset) {
		JOM(8, "%i=peasycap->standard_offset\n", peasycap->standard_offset);
		return -EBUSY;
	}
	p = peasycap->pusb_device;
	if (!p) {
		SAM("ERROR: peaycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	pc = &bf[0];
	uc = pixelformat;
	memcpy((void *)pc, (void *)(&uc), 4);
	bf[4] = 0;
	mask = 0xFF & easycap_standard[peasycap->standard_offset].mask;
	SAM("sought:    %ix%i,%s(0x%08X),%i=field,0x%02X=std mask\n",
	    width, height, pc, pixelformat, field, mask);
	switch (field) {
	case V4L2_FIELD_ANY: {
		strcpy(&fo[0], "V4L2_FIELD_ANY ");
		break;
	}
	case V4L2_FIELD_NONE: {
		strcpy(&fo[0], "V4L2_FIELD_NONE");
		break;
	}
	case V4L2_FIELD_TOP: {
		strcpy(&fo[0], "V4L2_FIELD_TOP");
		break;
	}
	case V4L2_FIELD_BOTTOM: {
		strcpy(&fo[0], "V4L2_FIELD_BOTTOM");
		break;
	}
	case V4L2_FIELD_INTERLACED: {
		strcpy(&fo[0], "V4L2_FIELD_INTERLACED");
		break;
	}
	case V4L2_FIELD_SEQ_TB: {
		strcpy(&fo[0], "V4L2_FIELD_SEQ_TB");
		break;
	}
	case V4L2_FIELD_SEQ_BT: {
		strcpy(&fo[0], "V4L2_FIELD_SEQ_BT");
		break;
	}
	case V4L2_FIELD_ALTERNATE: {
		strcpy(&fo[0], "V4L2_FIELD_ALTERNATE");
		break;
	}
	case V4L2_FIELD_INTERLACED_TB: {
		strcpy(&fo[0], "V4L2_FIELD_INTERLACED_TB");
		break;
	}
	case V4L2_FIELD_INTERLACED_BT: {
		strcpy(&fo[0], "V4L2_FIELD_INTERLACED_BT");
		break;
	}
	default: {
		strcpy(&fo[0], "V4L2_FIELD_... UNKNOWN  ");
		break;
	}
	}
	SAM("sought:    %s\n", &fo[0]);
	if (V4L2_FIELD_ANY == field) {
		field = V4L2_FIELD_NONE;
		SAM("prefer:    V4L2_FIELD_NONE=field, was V4L2_FIELD_ANY\n");
	}
	peasycap_best_format = NULL;
	peasycap_format = &easycap_format[0];
	while (0 != peasycap_format->v4l2_format.fmt.pix.width) {
		JOM(16, ".> %i %i 0x%08X %ix%i\n",
		    peasycap_format->mask & 0x01,
		    peasycap_format->v4l2_format.fmt.pix.field,
		    peasycap_format->v4l2_format.fmt.pix.pixelformat,
		    peasycap_format->v4l2_format.fmt.pix.width,
		    peasycap_format->v4l2_format.fmt.pix.height);

		if (((peasycap_format->mask & 0x1F) == (mask & 0x1F)) &&
		    (peasycap_format->v4l2_format.fmt.pix.field == field) &&
		    (peasycap_format->v4l2_format.fmt.pix.pixelformat == pixelformat) &&
		    (peasycap_format->v4l2_format.fmt.pix.width  == width) &&
		    (peasycap_format->v4l2_format.fmt.pix.height == height)) {

			peasycap_best_format = peasycap_format;
			break;
		}
		peasycap_format++;
	}
	if (0 == peasycap_format->v4l2_format.fmt.pix.width) {
		SAM("cannot do: %ix%i with standard mask 0x%02X\n",
		    width, height, mask);
		peasycap_format = &easycap_format[0];
		best = -1;
		while (0 != peasycap_format->v4l2_format.fmt.pix.width) {
			if (((peasycap_format->mask & 0x1F) == (mask & 0x1F)) &&
			    (peasycap_format->v4l2_format.fmt.pix.field == field) &&
			    (peasycap_format->v4l2_format.fmt.pix.pixelformat == pixelformat)) {

				miss = abs(peasycap_format->v4l2_format.fmt.pix.width  - width);
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
			SAM("cannot do %ix... with standard mask 0x%02X\n",
			    width, mask);
			SAM("cannot do ...x%i with standard mask 0x%02X\n",
			    height, mask);
			SAM("           %ix%i unmatched\n", width, height);
			return peasycap->format_offset;
		}
	}
	if (!peasycap_best_format) {
		SAM("MISTAKE: peasycap_best_format is NULL");
		return -EINVAL;
	}
	peasycap_format = peasycap_best_format;

/*...........................................................................*/
	if (try)
		return peasycap_best_format - easycap_format;
/*...........................................................................*/

	if (false != try) {
		SAM("MISTAKE: true==try where is should be false\n");
		return -EINVAL;
	}
	SAM("actioning: %ix%i %s\n",
	    peasycap_format->v4l2_format.fmt.pix.width,
	    peasycap_format->v4l2_format.fmt.pix.height,
	    &peasycap_format->name[0]);
	peasycap->height        = peasycap_format->v4l2_format.fmt.pix.height;
	peasycap->width         = peasycap_format->v4l2_format.fmt.pix.width;
	peasycap->pixelformat   = peasycap_format->v4l2_format.fmt.pix.pixelformat;
	peasycap->format_offset = peasycap_format - easycap_format;


	for (k = 0; k < INPUT_MANY; k++) {
		if (!peasycap->inputset[k].format_offset_ok) {
			peasycap->inputset[k].format_offset =
				peasycap->format_offset;
		}
	}
	if ((0 <= peasycap->input) && (INPUT_MANY > peasycap->input)) {
		peasycap->inputset[peasycap->input].format_offset =
			peasycap->format_offset;
		peasycap->inputset[peasycap->input].format_offset_ok = 1;
	} else
		JOM(8, "%i=peasycap->input\n", peasycap->input);



	peasycap->bytesperpixel = (0x00E0 & peasycap_format->mask) >> 5 ;
	if (0x0100 & peasycap_format->mask)
		peasycap->byteswaporder = true;
	else
		peasycap->byteswaporder = false;
	if (0x0200 & peasycap_format->mask)
		peasycap->skip = 5;
	else
		peasycap->skip = 0;
	if (0x0800 & peasycap_format->mask)
		peasycap->decimatepixel = true;
	else
		peasycap->decimatepixel = false;
	if (0x1000 & peasycap_format->mask)
		peasycap->offerfields = true;
	else
		peasycap->offerfields = false;
	if (peasycap->decimatepixel)
		multiplier = 2;
	else
		multiplier = 1;
	peasycap->videofieldamount =
		multiplier * peasycap->width * multiplier * peasycap->height;
	peasycap->frame_buffer_used =
		peasycap->bytesperpixel * peasycap->width * peasycap->height;
	if (peasycap->video_isoc_streaming) {
		resubmit = true;
		kill_video_urbs(peasycap);
	} else
		resubmit = false;
/*---------------------------------------------------------------------------*/
/*
	 *  PAL
	 */
/*---------------------------------------------------------------------------*/
	if (0 == (0x01 & peasycap_format->mask)) {
		if (((720 == peasycap_format->v4l2_format.fmt.pix.width) &&
		     (576 == peasycap_format->v4l2_format.fmt.pix.height)) ||
		    ((360 == peasycap_format->v4l2_format.fmt.pix.width) &&
		     (288 == peasycap_format->v4l2_format.fmt.pix.height))) {
			if (set_resolution(p, 0x0000, 0x0001, 0x05A0, 0x0121)) {
				SAM("ERROR: set_resolution() failed\n");
				return -EINVAL;
			}
		} else if ((704 == peasycap_format->v4l2_format.fmt.pix.width) &&
			   (576 == peasycap_format->v4l2_format.fmt.pix.height)) {
			if (set_resolution(p, 0x0004, 0x0001, 0x0584, 0x0121)) {
				SAM("ERROR: set_resolution() failed\n");
				return -EINVAL;
			}
		} else if (((640 == peasycap_format->v4l2_format.fmt.pix.width) &&
			    (480 == peasycap_format->v4l2_format.fmt.pix.height)) ||
			   ((320 == peasycap_format->v4l2_format.fmt.pix.width) &&
			    (240 == peasycap_format->v4l2_format.fmt.pix.height))) {
			if (set_resolution(p, 0x0014, 0x0020, 0x0514, 0x0110)) {
				SAM("ERROR: set_resolution() failed\n");
				return -EINVAL;
			}
		} else {
			SAM("MISTAKE: bad format, cannot set resolution\n");
			return -EINVAL;
		}
/*---------------------------------------------------------------------------*/
/*
 *  NTSC
 */
/*---------------------------------------------------------------------------*/
	} else {
		if (((720 == peasycap_format->v4l2_format.fmt.pix.width) &&
		     (480 == peasycap_format->v4l2_format.fmt.pix.height)) ||
		    ((360 == peasycap_format->v4l2_format.fmt.pix.width) &&
		     (240 == peasycap_format->v4l2_format.fmt.pix.height))) {
			if (set_resolution(p, 0x0000, 0x0003, 0x05A0, 0x00F3)) {
				SAM("ERROR: set_resolution() failed\n");
				return -EINVAL;
			}
		} else if (((640 == peasycap_format->v4l2_format.fmt.pix.width) &&
			    (480 == peasycap_format->v4l2_format.fmt.pix.height)) ||
			   ((320 == peasycap_format->v4l2_format.fmt.pix.width) &&
			    (240 == peasycap_format->v4l2_format.fmt.pix.height))) {
			if (set_resolution(p, 0x0014, 0x0003, 0x0514, 0x00F3)) {
				SAM("ERROR: set_resolution() failed\n");
				return -EINVAL;
			}
		} else {
			SAM("MISTAKE: bad format, cannot set resolution\n");
			return -EINVAL;
		}
	}
/*---------------------------------------------------------------------------*/
	if (resubmit)
		submit_video_urbs(peasycap);

	return peasycap_best_format - easycap_format;
}
/*****************************************************************************/
int adjust_brightness(struct easycap *peasycap, int value)
{
	unsigned int mood;
	int i1, k;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_BRIGHTNESS == easycap_control[i1].id) {
			if ((easycap_control[i1].minimum > value) ||
			    (easycap_control[i1].maximum < value))
				value = easycap_control[i1].default_value;

			if ((easycap_control[i1].minimum <= peasycap->brightness) &&
			    (easycap_control[i1].maximum >= peasycap->brightness)) {
				if (peasycap->brightness == value) {
					SAM("unchanged brightness at  0x%02X\n",
					    value);
					return 0;
				}
			}
			peasycap->brightness = value;
			for (k = 0; k < INPUT_MANY; k++) {
				if (!peasycap->inputset[k].brightness_ok)
					peasycap->inputset[k].brightness =
						peasycap->brightness;
			}
			if ((0 <= peasycap->input) && (INPUT_MANY > peasycap->input)) {
				peasycap->inputset[peasycap->input].brightness =
					peasycap->brightness;
				peasycap->inputset[peasycap->input].brightness_ok = 1;
			} else
				JOM(8, "%i=peasycap->input\n", peasycap->input);
			mood = 0x00FF & (unsigned int)peasycap->brightness;
			if (!write_saa(peasycap->pusb_device, 0x0A, mood)) {
				SAM("adjusting brightness to  0x%02X\n", mood);
				return 0;
			} else {
				SAM("WARNING: failed to adjust brightness "
				    "to 0x%02X\n", mood);
				return -ENOENT;
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust brightness: control not found\n");
	return -ENOENT;
}
/*****************************************************************************/
int adjust_contrast(struct easycap *peasycap, int value)
{
	unsigned int mood;
	int i1, k;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_CONTRAST == easycap_control[i1].id) {
			if ((easycap_control[i1].minimum > value) ||
			    (easycap_control[i1].maximum < value))
				value = easycap_control[i1].default_value;


			if ((easycap_control[i1].minimum <= peasycap->contrast) &&
			    (easycap_control[i1].maximum >= peasycap->contrast)) {
				if (peasycap->contrast == value) {
					SAM("unchanged contrast at  0x%02X\n", value);
					return 0;
				}
			}
			peasycap->contrast = value;
			for (k = 0; k < INPUT_MANY; k++) {
				if (!peasycap->inputset[k].contrast_ok)
					peasycap->inputset[k].contrast = peasycap->contrast;
			}

			if ((0 <= peasycap->input) && (INPUT_MANY > peasycap->input)) {
				peasycap->inputset[peasycap->input].contrast =
						peasycap->contrast;
				peasycap->inputset[peasycap->input].contrast_ok = 1;
			} else
				JOM(8, "%i=peasycap->input\n", peasycap->input);

			mood = 0x00FF & (unsigned int) (peasycap->contrast - 128);
			if (!write_saa(peasycap->pusb_device, 0x0B, mood)) {
				SAM("adjusting contrast to  0x%02X\n", mood);
				return 0;
			} else {
				SAM("WARNING: failed to adjust contrast to "
				    "0x%02X\n", mood);
				return -ENOENT;
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust contrast: control not found\n");
	return -ENOENT;
}
/*****************************************************************************/
int adjust_saturation(struct easycap *peasycap, int value)
{
	unsigned int mood;
	int i1, k;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_SATURATION == easycap_control[i1].id) {
			if ((easycap_control[i1].minimum > value) ||
			    (easycap_control[i1].maximum < value))
				value = easycap_control[i1].default_value;


			if ((easycap_control[i1].minimum <= peasycap->saturation) &&
			    (easycap_control[i1].maximum >= peasycap->saturation)) {
				if (peasycap->saturation == value) {
					SAM("unchanged saturation at  0x%02X\n",
					    value);
					return 0;
				}
			}
			peasycap->saturation = value;
			for (k = 0; k < INPUT_MANY; k++) {
				if (!peasycap->inputset[k].saturation_ok)
					peasycap->inputset[k].saturation =
						peasycap->saturation;
			}
			if ((0 <= peasycap->input) && (INPUT_MANY > peasycap->input)) {
				peasycap->inputset[peasycap->input].saturation =
					peasycap->saturation;
				peasycap->inputset[peasycap->input].saturation_ok = 1;
			} else
				JOM(8, "%i=peasycap->input\n", peasycap->input);
			mood = 0x00FF & (unsigned int) (peasycap->saturation - 128);
			if (!write_saa(peasycap->pusb_device, 0x0C, mood)) {
				SAM("adjusting saturation to  0x%02X\n", mood);
				return 0;
			} else {
				SAM("WARNING: failed to adjust saturation to "
				    "0x%02X\n", mood);
				return -ENOENT;
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust saturation: control not found\n");
	return -ENOENT;
}
/*****************************************************************************/
int adjust_hue(struct easycap *peasycap, int value)
{
	unsigned int mood;
	int i1, i2, k;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_HUE == easycap_control[i1].id) {
			if ((easycap_control[i1].minimum > value) ||
			    (easycap_control[i1].maximum < value))
				value = easycap_control[i1].default_value;

			if ((easycap_control[i1].minimum <= peasycap->hue) &&
			    (easycap_control[i1].maximum >= peasycap->hue)) {
				if (peasycap->hue == value) {
					SAM("unchanged hue at  0x%02X\n", value);
					return 0;
				}
			}
			peasycap->hue = value;
			for (k = 0; k < INPUT_MANY; k++) {
				if (!peasycap->inputset[k].hue_ok)
					peasycap->inputset[k].hue = peasycap->hue;
			}
			if (0 <= peasycap->input && INPUT_MANY > peasycap->input) {
				peasycap->inputset[peasycap->input].hue = peasycap->hue;
				peasycap->inputset[peasycap->input].hue_ok = 1;
			} else
				JOM(8, "%i=peasycap->input\n", peasycap->input);
			i2 = peasycap->hue - 128;
			mood = 0x00FF & ((int) i2);
			if (!write_saa(peasycap->pusb_device, 0x0D, mood)) {
				SAM("adjusting hue to  0x%02X\n", mood);
				return 0;
			} else {
				SAM("WARNING: failed to adjust hue to 0x%02X\n", mood);
				return -ENOENT;
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust hue: control not found\n");
	return -ENOENT;
}
/*****************************************************************************/
int adjust_volume(struct easycap *peasycap, int value)
{
	s8 mood;
	int i1;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_AUDIO_VOLUME == easycap_control[i1].id) {
			if ((easycap_control[i1].minimum > value) ||
			    (easycap_control[i1].maximum < value))
				value = easycap_control[i1].default_value;

			if ((easycap_control[i1].minimum <= peasycap->volume) &&
			    (easycap_control[i1].maximum >= peasycap->volume)) {
				if (peasycap->volume == value) {
					SAM("unchanged volume at  0x%02X\n", value);
					return 0;
				}
			}
			peasycap->volume = value;
			mood = (16 > peasycap->volume) ? 16 :
				((31 < peasycap->volume) ? 31 :
				  (s8) peasycap->volume);
			if (!audio_gainset(peasycap->pusb_device, mood)) {
				SAM("adjusting volume to 0x%02X\n", mood);
				return 0;
			} else {
				SAM("WARNING: failed to adjust volume to "
				    "0x%2X\n", mood);
				return -ENOENT;
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust volume: control not found\n");
	return -ENOENT;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  AN ALTERNATIVE METHOD OF MUTING MIGHT SEEM TO BE:
 *            usb_set_interface(peasycap->pusb_device,
 *                              peasycap->audio_interface,
 *                              peasycap->audio_altsetting_off);
 *  HOWEVER, AFTER THIS COMMAND IS ISSUED ALL SUBSEQUENT URBS RECEIVE STATUS
 *  -ESHUTDOWN.  THE HANDLER ROUTINE easyxxx_complete() DECLINES TO RESUBMIT
 *  THE URB AND THE PIPELINE COLLAPSES IRRETRIEVABLY.  BEWARE.
 */
/*---------------------------------------------------------------------------*/
static int adjust_mute(struct easycap *peasycap, int value)
{
	int i1;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return -EFAULT;
	}
	if (!peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	i1 = 0;
	while (0xFFFFFFFF != easycap_control[i1].id) {
		if (V4L2_CID_AUDIO_MUTE == easycap_control[i1].id) {
			peasycap->mute = value;
			switch (peasycap->mute) {
			case 1: {
				peasycap->audio_idle = 1;
				SAM("adjusting mute: %i=peasycap->audio_idle\n",
				    peasycap->audio_idle);
				return 0;
			}
			default: {
				peasycap->audio_idle = 0;
				SAM("adjusting mute: %i=peasycap->audio_idle\n",
				    peasycap->audio_idle);
				return 0;
			}
			}
			break;
		}
		i1++;
	}
	SAM("WARNING: failed to adjust mute: control not found\n");
	return -ENOENT;
}
/*---------------------------------------------------------------------------*/
long easycap_unlocked_ioctl(struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct easycap *peasycap;
	struct usb_device *p;
	int kd;

	if (!file) {
		SAY("ERROR:  file is NULL\n");
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (!peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		return -1;
	}
	p = peasycap->pusb_device;
	if (!p) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		return -EFAULT;
	}
	kd = isdongle(peasycap);
	if (0 <= kd && DONGLE_MANY > kd) {
		if (mutex_lock_interruptible(&easycapdc60_dongle[kd].mutex_video)) {
			SAY("ERROR: cannot lock "
			    "easycapdc60_dongle[%i].mutex_video\n", kd);
			return -ERESTARTSYS;
		}
		JOM(4, "locked easycapdc60_dongle[%i].mutex_video\n", kd);
/*---------------------------------------------------------------------------*/
/*
 *  MEANWHILE, easycap_usb_disconnect() MAY HAVE FREED POINTER peasycap,
 *  IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
 *  IF NECESSARY, BAIL OUT.
 */
/*---------------------------------------------------------------------------*/
		if (kd != isdongle(peasycap))
			return -ERESTARTSYS;
		if (!file) {
			SAY("ERROR:  file is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
		peasycap = file->private_data;
		if (!peasycap) {
			SAY("ERROR:  peasycap is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ERESTARTSYS;
		}
	} else {
/*---------------------------------------------------------------------------*/
/*
 *  IF easycap_usb_disconnect() HAS ALREADY FREED POINTER peasycap BEFORE THE
 *  ATTEMPT TO ACQUIRE THE SEMAPHORE, isdongle() WILL HAVE FAILED.  BAIL OUT.
 */
/*---------------------------------------------------------------------------*/
		return -ERESTARTSYS;
	}
/*---------------------------------------------------------------------------*/
	switch (cmd) {
	case VIDIOC_QUERYCAP: {
		struct v4l2_capability v4l2_capability;
		char version[16], *p1, *p2;
		int i, rc, k[3];
		long lng;

		JOM(8, "VIDIOC_QUERYCAP\n");

		if (16 <= strlen(EASYCAP_DRIVER_VERSION)) {
			SAM("ERROR: bad driver version string\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		strcpy(&version[0], EASYCAP_DRIVER_VERSION);
		for (i = 0; i < 3; i++)
			k[i] = 0;
		p2 = &version[0];
		i = 0;
		while (*p2) {
			p1 = p2;
			while (*p2 && ('.' != *p2))
				p2++;
			if (*p2)
				*p2++ = 0;
			if (3 > i) {
				rc = (int) strict_strtol(p1, 10, &lng);
				if (rc) {
					SAM("ERROR: %i=strict_strtol(%s,.,,)\n",
					    rc, p1);
					mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
					return -EINVAL;
				}
				k[i] = (int)lng;
			}
			i++;
		}

		memset(&v4l2_capability, 0, sizeof(struct v4l2_capability));
		strlcpy(&v4l2_capability.driver[0],
			"easycap", sizeof(v4l2_capability.driver));

		v4l2_capability.capabilities = V4L2_CAP_VIDEO_CAPTURE |
						V4L2_CAP_STREAMING |
						V4L2_CAP_AUDIO |
						V4L2_CAP_READWRITE;

		v4l2_capability.version = KERNEL_VERSION(k[0], k[1], k[2]);
		JOM(8, "v4l2_capability.version=(%i,%i,%i)\n", k[0], k[1], k[2]);

		strlcpy(&v4l2_capability.card[0],
			"EasyCAP DC60", sizeof(v4l2_capability.card));

		if (usb_make_path(peasycap->pusb_device,
				&v4l2_capability.bus_info[0],
				sizeof(v4l2_capability.bus_info)) < 0) {

			strlcpy(&v4l2_capability.bus_info[0], "EasyCAP bus_info",
				sizeof(v4l2_capability.bus_info));
			JOM(8, "%s=v4l2_capability.bus_info\n",
				&v4l2_capability.bus_info[0]);
		}
		if (copy_to_user((void __user *)arg, &v4l2_capability,
				sizeof(struct v4l2_capability))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUMINPUT: {
		struct v4l2_input v4l2_input;
		u32 index;

		JOM(8, "VIDIOC_ENUMINPUT\n");

		if (copy_from_user(&v4l2_input, (void __user *)arg,
					sizeof(struct v4l2_input))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
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
			v4l2_input.std = V4L2_STD_PAL |
					V4L2_STD_SECAM |
					V4L2_STD_NTSC ;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		case 1: {
			v4l2_input.index = index;
			strcpy(&v4l2_input.name[0], "CVBS1");
			v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
			v4l2_input.audioset = 0x01;
			v4l2_input.tuner = 0;
			v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM |
					V4L2_STD_NTSC;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		case 2: {
			v4l2_input.index = index;
			strcpy(&v4l2_input.name[0], "CVBS2");
			v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
			v4l2_input.audioset = 0x01;
			v4l2_input.tuner = 0;
			v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM |
					V4L2_STD_NTSC ;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		case 3: {
			v4l2_input.index = index;
			strcpy(&v4l2_input.name[0], "CVBS3");
			v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
			v4l2_input.audioset = 0x01;
			v4l2_input.tuner = 0;
			v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM |
					V4L2_STD_NTSC ;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		case 4: {
			v4l2_input.index = index;
			strcpy(&v4l2_input.name[0], "CVBS4");
			v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
			v4l2_input.audioset = 0x01;
			v4l2_input.tuner = 0;
			v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM |
					V4L2_STD_NTSC ;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		case 5: {
			v4l2_input.index = index;
			strcpy(&v4l2_input.name[0], "S-VIDEO");
			v4l2_input.type = V4L2_INPUT_TYPE_CAMERA;
			v4l2_input.audioset = 0x01;
			v4l2_input.tuner = 0;
			v4l2_input.std = V4L2_STD_PAL | V4L2_STD_SECAM |
					V4L2_STD_NTSC ;
			v4l2_input.status = 0;
			JOM(8, "%i=index: %s\n", index, &v4l2_input.name[0]);
			break;
		}
		default: {
			JOM(8, "%i=index: exhausts inputs\n", index);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		}

		if (copy_to_user((void __user *)arg, &v4l2_input,
				sizeof(struct v4l2_input))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_INPUT: {
		u32 index;

		JOM(8, "VIDIOC_G_INPUT\n");
		index = (u32)peasycap->input;
		JOM(8, "user is told: %i\n", index);
		if (copy_to_user((void __user *)arg, &index, sizeof(u32))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_INPUT:
	{
		u32 index;
		int rc;

		JOM(8, "VIDIOC_S_INPUT\n");

		if (0 != copy_from_user(&index, (void __user *)arg, sizeof(u32))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		JOM(8, "user requests input %i\n", index);

		if ((int)index == peasycap->input) {
			SAM("requested input already in effect\n");
			break;
		}

		if ((0 > index) || (INPUT_MANY <= index)) {
			JOM(8, "ERROR:  bad requested input: %i\n", index);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}

		rc = newinput(peasycap, (int)index);
		if (0 == rc) {
			JOM(8, "newinput(.,%i) OK\n", (int)index);
		} else {
			SAM("ERROR: newinput(.,%i) returned %i\n", (int)index, rc);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUMAUDIO: {
		JOM(8, "VIDIOC_ENUMAUDIO\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUMAUDOUT: {
		struct v4l2_audioout v4l2_audioout;

		JOM(8, "VIDIOC_ENUMAUDOUT\n");

		if (copy_from_user(&v4l2_audioout, (void __user *)arg,
					sizeof(struct v4l2_audioout))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (0 != v4l2_audioout.index) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		memset(&v4l2_audioout, 0, sizeof(struct v4l2_audioout));
		v4l2_audioout.index = 0;
		strcpy(&v4l2_audioout.name[0], "Soundtrack");

		if (copy_to_user((void __user *)arg, &v4l2_audioout,
				sizeof(struct v4l2_audioout))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_QUERYCTRL: {
		int i1;
		struct v4l2_queryctrl v4l2_queryctrl;

		JOM(8, "VIDIOC_QUERYCTRL\n");

		if (0 != copy_from_user(&v4l2_queryctrl, (void __user *)arg,
				sizeof(struct v4l2_queryctrl))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		i1 = 0;
		while (0xFFFFFFFF != easycap_control[i1].id) {
			if (easycap_control[i1].id == v4l2_queryctrl.id) {
				JOM(8, "VIDIOC_QUERYCTRL  %s=easycap_control[%i]"
				    ".name\n", &easycap_control[i1].name[0], i1);
				memcpy(&v4l2_queryctrl, &easycap_control[i1],
				       sizeof(struct v4l2_queryctrl));
				break;
			}
			i1++;
		}
		if (0xFFFFFFFF == easycap_control[i1].id) {
			JOM(8, "%i=index: exhausts controls\n", i1);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		if (copy_to_user((void __user *)arg, &v4l2_queryctrl,
				sizeof(struct v4l2_queryctrl))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_QUERYMENU: {
		JOM(8, "VIDIOC_QUERYMENU unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_CTRL: {
		struct v4l2_control *pv4l2_control;

		JOM(8, "VIDIOC_G_CTRL\n");
		pv4l2_control = memdup_user((void __user *)arg,
					    sizeof(struct v4l2_control));
		if (IS_ERR(pv4l2_control)) {
			SAM("ERROR: copy from user failed\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return PTR_ERR(pv4l2_control);
		}

		switch (pv4l2_control->id) {
		case V4L2_CID_BRIGHTNESS: {
			pv4l2_control->value = peasycap->brightness;
			JOM(8, "user enquires brightness: %i\n", pv4l2_control->value);
			break;
		}
		case V4L2_CID_CONTRAST: {
			pv4l2_control->value = peasycap->contrast;
			JOM(8, "user enquires contrast: %i\n", pv4l2_control->value);
			break;
		}
		case V4L2_CID_SATURATION: {
			pv4l2_control->value = peasycap->saturation;
			JOM(8, "user enquires saturation: %i\n", pv4l2_control->value);
			break;
		}
		case V4L2_CID_HUE: {
			pv4l2_control->value = peasycap->hue;
			JOM(8, "user enquires hue: %i\n", pv4l2_control->value);
			break;
		}
		case V4L2_CID_AUDIO_VOLUME: {
			pv4l2_control->value = peasycap->volume;
			JOM(8, "user enquires volume: %i\n", pv4l2_control->value);
			break;
		}
		case V4L2_CID_AUDIO_MUTE: {
			if (1 == peasycap->mute)
				pv4l2_control->value = true;
			else
				pv4l2_control->value = false;
			JOM(8, "user enquires mute: %i\n", pv4l2_control->value);
			break;
		}
		default: {
			SAM("ERROR: unknown V4L2 control: 0x%08X=id\n",
			    pv4l2_control->id);
			kfree(pv4l2_control);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		}
		if (copy_to_user((void __user *)arg, pv4l2_control,
				sizeof(struct v4l2_control))) {
			kfree(pv4l2_control);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		kfree(pv4l2_control);
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_CTRL: {
		struct v4l2_control v4l2_control;

		JOM(8, "VIDIOC_S_CTRL\n");

		if (0 != copy_from_user(&v4l2_control, (void __user *)arg,
				sizeof(struct v4l2_control))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		switch (v4l2_control.id) {
		case V4L2_CID_BRIGHTNESS: {
			JOM(8, "user requests brightness %i\n", v4l2_control.value);
			if (0 != adjust_brightness(peasycap, v4l2_control.value))
				;
			break;
		}
		case V4L2_CID_CONTRAST: {
			JOM(8, "user requests contrast %i\n", v4l2_control.value);
			if (0 != adjust_contrast(peasycap, v4l2_control.value))
				;
			break;
		}
		case V4L2_CID_SATURATION: {
			JOM(8, "user requests saturation %i\n", v4l2_control.value);
			if (0 != adjust_saturation(peasycap, v4l2_control.value))
				;
			break;
		}
		case V4L2_CID_HUE: {
			JOM(8, "user requests hue %i\n", v4l2_control.value);
			if (0 != adjust_hue(peasycap, v4l2_control.value))
				;
			break;
		}
		case V4L2_CID_AUDIO_VOLUME: {
			JOM(8, "user requests volume %i\n", v4l2_control.value);
			if (0 != adjust_volume(peasycap, v4l2_control.value))
				;
			break;
		}
		case V4L2_CID_AUDIO_MUTE: {
			int mute;

			JOM(8, "user requests mute %i\n", v4l2_control.value);
			if (v4l2_control.value)
				mute = 1;
			else
				mute = 0;

			if (0 != adjust_mute(peasycap, mute))
				SAM("WARNING: failed to adjust mute to %i\n", mute);
			break;
		}
		default: {
			SAM("ERROR: unknown V4L2 control: 0x%08X=id\n",
			    v4l2_control.id);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_EXT_CTRLS: {
		JOM(8, "VIDIOC_S_EXT_CTRLS unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUM_FMT: {
		u32 index;
		struct v4l2_fmtdesc v4l2_fmtdesc;

		JOM(8, "VIDIOC_ENUM_FMT\n");

		if (0 != copy_from_user(&v4l2_fmtdesc, (void __user *)arg,
				sizeof(struct v4l2_fmtdesc))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
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
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		case 1: {
			v4l2_fmtdesc.flags = 0;
			strcpy(&v4l2_fmtdesc.description[0], "yuy2");
			v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_YUYV;
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		case 2: {
			v4l2_fmtdesc.flags = 0;
			strcpy(&v4l2_fmtdesc.description[0], "rgb24");
			v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_RGB24;
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		case 3: {
			v4l2_fmtdesc.flags = 0;
			strcpy(&v4l2_fmtdesc.description[0], "rgb32");
			v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_RGB32;
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		case 4: {
			v4l2_fmtdesc.flags = 0;
			strcpy(&v4l2_fmtdesc.description[0], "bgr24");
			v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_BGR24;
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		case 5: {
			v4l2_fmtdesc.flags = 0;
			strcpy(&v4l2_fmtdesc.description[0], "bgr32");
			v4l2_fmtdesc.pixelformat = V4L2_PIX_FMT_BGR32;
			JOM(8, "%i=index: %s\n", index, &v4l2_fmtdesc.description[0]);
			break;
		}
		default: {
			JOM(8, "%i=index: exhausts formats\n", index);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		}
		if (copy_to_user((void __user *)arg, &v4l2_fmtdesc,
				sizeof(struct v4l2_fmtdesc))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*
	 *  THE RESPONSE TO VIDIOC_ENUM_FRAMESIZES MUST BE CONDITIONED ON THE
	 *  THE CURRENT STANDARD, BECAUSE THAT IS WHAT gstreamer EXPECTS.  BEWARE.
	*/
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUM_FRAMESIZES: {
		u32 index;
		struct v4l2_frmsizeenum v4l2_frmsizeenum;

		JOM(8, "VIDIOC_ENUM_FRAMESIZES\n");

		if (0 != copy_from_user(&v4l2_frmsizeenum, (void __user *)arg,
				sizeof(struct v4l2_frmsizeenum))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		index = v4l2_frmsizeenum.index;

		v4l2_frmsizeenum.type = (u32) V4L2_FRMSIZE_TYPE_DISCRETE;

		if (peasycap->ntsc) {
			switch (index) {
			case 0: {
				v4l2_frmsizeenum.discrete.width = 640;
				v4l2_frmsizeenum.discrete.height = 480;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 1: {
				v4l2_frmsizeenum.discrete.width = 320;
				v4l2_frmsizeenum.discrete.height = 240;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 2: {
				v4l2_frmsizeenum.discrete.width = 720;
				v4l2_frmsizeenum.discrete.height = 480;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 3: {
				v4l2_frmsizeenum.discrete.width = 360;
				v4l2_frmsizeenum.discrete.height = 240;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			default: {
				JOM(8, "%i=index: exhausts framesizes\n", index);
				mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
				return -EINVAL;
			}
			}
		} else {
			switch (index) {
			case 0: {
				v4l2_frmsizeenum.discrete.width = 640;
				v4l2_frmsizeenum.discrete.height = 480;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 1: {
				v4l2_frmsizeenum.discrete.width = 320;
				v4l2_frmsizeenum.discrete.height = 240;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 2: {
				v4l2_frmsizeenum.discrete.width = 704;
				v4l2_frmsizeenum.discrete.height = 576;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 3: {
				v4l2_frmsizeenum.discrete.width = 720;
				v4l2_frmsizeenum.discrete.height = 576;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			case 4: {
				v4l2_frmsizeenum.discrete.width = 360;
				v4l2_frmsizeenum.discrete.height = 288;
				JOM(8, "%i=index: %ix%i\n", index,
				    (int)(v4l2_frmsizeenum.
					  discrete.width),
				    (int)(v4l2_frmsizeenum.
					  discrete.height));
				break;
			}
			default: {
				JOM(8, "%i=index: exhausts framesizes\n", index);
				mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
				return -EINVAL;
			}
			}
		}
		if (copy_to_user((void __user *)arg, &v4l2_frmsizeenum,
				sizeof(struct v4l2_frmsizeenum))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*
	 *  THE RESPONSE TO VIDIOC_ENUM_FRAMEINTERVALS MUST BE CONDITIONED ON THE
	 *  THE CURRENT STANDARD, BECAUSE THAT IS WHAT gstreamer EXPECTS.  BEWARE.
	*/
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_ENUM_FRAMEINTERVALS: {
		u32 index;
		int denominator;
		struct v4l2_frmivalenum v4l2_frmivalenum;

		JOM(8, "VIDIOC_ENUM_FRAMEINTERVALS\n");

		if (peasycap->fps)
			denominator = peasycap->fps;
		else {
			if (peasycap->ntsc)
				denominator = 30;
			else
				denominator = 25;
		}

		if (0 != copy_from_user(&v4l2_frmivalenum, (void __user *)arg,
				sizeof(struct v4l2_frmivalenum))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		index = v4l2_frmivalenum.index;

		v4l2_frmivalenum.type = (u32) V4L2_FRMIVAL_TYPE_DISCRETE;

		switch (index) {
		case 0: {
			v4l2_frmivalenum.discrete.numerator = 1;
			v4l2_frmivalenum.discrete.denominator = denominator;
			JOM(8, "%i=index: %i/%i\n", index,
			    (int)(v4l2_frmivalenum.discrete.numerator),
			    (int)(v4l2_frmivalenum.discrete.denominator));
			break;
		}
		case 1: {
			v4l2_frmivalenum.discrete.numerator = 1;
			v4l2_frmivalenum.discrete.denominator = denominator/5;
			JOM(8, "%i=index: %i/%i\n", index,
			    (int)(v4l2_frmivalenum.discrete.numerator),
			    (int)(v4l2_frmivalenum.discrete.denominator));
			break;
		}
		default: {
			JOM(8, "%i=index: exhausts frameintervals\n", index);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		}
		if (copy_to_user((void __user *)arg, &v4l2_frmivalenum,
					sizeof(struct v4l2_frmivalenum))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_FMT: {
		struct v4l2_format *pv4l2_format;
		struct v4l2_pix_format *pv4l2_pix_format;

		JOM(8, "VIDIOC_G_FMT\n");
		pv4l2_format = kzalloc(sizeof(struct v4l2_format), GFP_KERNEL);
		if (!pv4l2_format) {
			SAM("ERROR: out of memory\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ENOMEM;
		}
		pv4l2_pix_format = kzalloc(sizeof(struct v4l2_pix_format), GFP_KERNEL);
		if (!pv4l2_pix_format) {
			SAM("ERROR: out of memory\n");
			kfree(pv4l2_format);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ENOMEM;
		}
		if (0 != copy_from_user(pv4l2_format, (void __user *)arg,
					sizeof(struct v4l2_format))) {
			kfree(pv4l2_format);
			kfree(pv4l2_pix_format);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (pv4l2_format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			kfree(pv4l2_format);
			kfree(pv4l2_pix_format);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}

		memset(pv4l2_pix_format, 0, sizeof(struct v4l2_pix_format));
		pv4l2_format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		memcpy(&pv4l2_format->fmt.pix,
		       &easycap_format[peasycap->format_offset]
		       .v4l2_format.fmt.pix, sizeof(struct v4l2_pix_format));
		JOM(8, "user is told: %s\n",
		    &easycap_format[peasycap->format_offset].name[0]);

		if (copy_to_user((void __user *)arg, pv4l2_format,
					sizeof(struct v4l2_format))) {
			kfree(pv4l2_format);
			kfree(pv4l2_pix_format);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		kfree(pv4l2_format);
		kfree(pv4l2_pix_format);
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_TRY_FMT:
	case VIDIOC_S_FMT: {
		struct v4l2_format v4l2_format;
		struct v4l2_pix_format v4l2_pix_format;
		bool try;
		int best_format;

		if (VIDIOC_TRY_FMT == cmd) {
			JOM(8, "VIDIOC_TRY_FMT\n");
			try = true;
		} else {
			JOM(8, "VIDIOC_S_FMT\n");
			try = false;
		}

		if (0 != copy_from_user(&v4l2_format, (void __user *)arg,
					sizeof(struct v4l2_format))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		best_format = adjust_format(peasycap,
					v4l2_format.fmt.pix.width,
					v4l2_format.fmt.pix.height,
					v4l2_format.fmt.pix.pixelformat,
					v4l2_format.fmt.pix.field,
					try);
		if (0 > best_format) {
			if (-EBUSY == best_format) {
				mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
				return -EBUSY;
			}
			JOM(8, "WARNING: adjust_format() returned %i\n", best_format);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ENOENT;
		}
/*...........................................................................*/
		memset(&v4l2_pix_format, 0, sizeof(struct v4l2_pix_format));
		v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		memcpy(&(v4l2_format.fmt.pix),
			&(easycap_format[best_format].v4l2_format.fmt.pix),
			sizeof(v4l2_pix_format));
		JOM(8, "user is told: %s\n", &easycap_format[best_format].name[0]);

		if (copy_to_user((void __user *)arg, &v4l2_format,
					sizeof(struct v4l2_format))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_CROPCAP: {
		struct v4l2_cropcap v4l2_cropcap;

		JOM(8, "VIDIOC_CROPCAP\n");

		if (0 != copy_from_user(&v4l2_cropcap, (void __user *)arg,
					sizeof(struct v4l2_cropcap))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (v4l2_cropcap.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			JOM(8, "v4l2_cropcap.type != V4L2_BUF_TYPE_VIDEO_CAPTURE\n");

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

		JOM(8, "user is told: %ix%i\n", peasycap->width, peasycap->height);

		if (copy_to_user((void __user *)arg, &v4l2_cropcap,
					sizeof(struct v4l2_cropcap))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_CROP:
	case VIDIOC_S_CROP: {
		JOM(8, "VIDIOC_G_CROP|VIDIOC_S_CROP  unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_QUERYSTD: {
		JOM(8, "VIDIOC_QUERYSTD: "
		    "EasyCAP is incapable of detecting standard\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
		break;
	}
	/*-------------------------------------------------------------------*/
	/*
	 *  THE MANIPULATIONS INVOLVING last0,last1,last2,last3
	 *  CONSTITUTE A WORKAROUND *  FOR WHAT APPEARS TO BE
	 *  A BUG IN 64-BIT mplayer.
	 *  NOT NEEDED, BUT HOPEFULLY HARMLESS, FOR 32-BIT mplayer.
	 */
	/*------------------------------------------------------------------*/
	case VIDIOC_ENUMSTD: {
		int last0 = -1, last1 = -1, last2 = -1, last3 = -1;
		struct v4l2_standard v4l2_standard;
		u32 index;
		struct easycap_standard const *peasycap_standard;

		JOM(8, "VIDIOC_ENUMSTD\n");

		if (0 != copy_from_user(&v4l2_standard, (void __user *)arg,
					sizeof(struct v4l2_standard))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		index = v4l2_standard.index;

		last3 = last2;
		last2 = last1;
		last1 = last0;
		last0 = index;
		if ((index == last3) && (index == last2) &&
		    (index == last1) && (index == last0)) {
			index++;
			last3 = last2;
			last2 = last1;
			last1 = last0;
			last0 = index;
		}

		memset(&v4l2_standard, 0, sizeof(struct v4l2_standard));

		peasycap_standard = &easycap_standard[0];
		while (0xFFFF != peasycap_standard->mask) {
			if ((int)(peasycap_standard - &easycap_standard[0]) == index)
				break;
			peasycap_standard++;
		}
		if (0xFFFF == peasycap_standard->mask) {
			JOM(8, "%i=index: exhausts standards\n", index);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		JOM(8, "%i=index: %s\n", index,
		    &(peasycap_standard->v4l2_standard.name[0]));
		memcpy(&v4l2_standard, &(peasycap_standard->v4l2_standard),
		       sizeof(struct v4l2_standard));

		v4l2_standard.index = index;

		if (copy_to_user((void __user *)arg, &v4l2_standard,
				sizeof(struct v4l2_standard))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_STD: {
		v4l2_std_id std_id;
		struct easycap_standard const *peasycap_standard;

		JOM(8, "VIDIOC_G_STD\n");

		if (0 > peasycap->standard_offset) {
			JOM(8, "%i=peasycap->standard_offset\n",
			    peasycap->standard_offset);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EBUSY;
		}

		if (0 != copy_from_user(&std_id, (void __user *)arg,
					sizeof(v4l2_std_id))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		peasycap_standard = &easycap_standard[peasycap->standard_offset];
		std_id = peasycap_standard->v4l2_standard.id;

		JOM(8, "user is told: %s\n",
		    &peasycap_standard->v4l2_standard.name[0]);

		if (copy_to_user((void __user *)arg, &std_id,
					sizeof(v4l2_std_id))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_STD: {
		v4l2_std_id std_id;
		int rc;

		JOM(8, "VIDIOC_S_STD\n");

		if (0 != copy_from_user(&std_id, (void __user *)arg,
					sizeof(v4l2_std_id))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		JOM(8, "User requests standard: 0x%08X%08X\n",
		    (int)((std_id & (((v4l2_std_id)0xFFFFFFFF) << 32)) >> 32),
		    (int)(std_id & ((v4l2_std_id)0xFFFFFFFF)));

		rc = adjust_standard(peasycap, std_id);
		if (0 > rc) {
			JOM(8, "WARNING: adjust_standard() returned %i\n", rc);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -ENOENT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_REQBUFS: {
		int nbuffers;
		struct v4l2_requestbuffers v4l2_requestbuffers;

		JOM(8, "VIDIOC_REQBUFS\n");

		if (0 != copy_from_user(&v4l2_requestbuffers,
					(void __user *)arg,
					sizeof(struct v4l2_requestbuffers))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (v4l2_requestbuffers.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		if (v4l2_requestbuffers.memory != V4L2_MEMORY_MMAP) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		nbuffers = v4l2_requestbuffers.count;
		JOM(8, "                   User requests %i buffers ...\n", nbuffers);
		if (nbuffers < 2)
			nbuffers = 2;
		if (nbuffers > FRAME_BUFFER_MANY)
			nbuffers = FRAME_BUFFER_MANY;
		if (v4l2_requestbuffers.count == nbuffers) {
			JOM(8, "                   ... agree to  %i buffers\n",
			    nbuffers);
		} else {
			JOM(8, "                  ... insist on  %i buffers\n",
			    nbuffers);
			v4l2_requestbuffers.count = nbuffers;
		}
		peasycap->frame_buffer_many = nbuffers;

		if (copy_to_user((void __user *)arg, &v4l2_requestbuffers,
					sizeof(struct v4l2_requestbuffers))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_QUERYBUF: {
		u32 index;
		struct v4l2_buffer v4l2_buffer;

		JOM(8, "VIDIOC_QUERYBUF\n");

		if (peasycap->video_eof) {
			JOM(8, "returning -EIO because  %i=video_eof\n",
			    peasycap->video_eof);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EIO;
		}

		if (0 != copy_from_user(&v4l2_buffer, (void __user *)arg,
					sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		index = v4l2_buffer.index;
		if (index < 0 || index >= peasycap->frame_buffer_many)
			return -EINVAL;
		memset(&v4l2_buffer, 0, sizeof(struct v4l2_buffer));
		v4l2_buffer.index = index;
		v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buffer.bytesused = peasycap->frame_buffer_used;
		v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED |
					peasycap->done[index] |
					peasycap->queued[index];
		v4l2_buffer.field = V4L2_FIELD_NONE;
		v4l2_buffer.memory = V4L2_MEMORY_MMAP;
		v4l2_buffer.m.offset = index * FRAME_BUFFER_SIZE;
		v4l2_buffer.length = FRAME_BUFFER_SIZE;

		JOM(16, "  %10i=index\n", v4l2_buffer.index);
		JOM(16, "  0x%08X=type\n", v4l2_buffer.type);
		JOM(16, "  %10i=bytesused\n", v4l2_buffer.bytesused);
		JOM(16, "  0x%08X=flags\n", v4l2_buffer.flags);
		JOM(16, "  %10i=field\n", v4l2_buffer.field);
		JOM(16, "  %10li=timestamp.tv_usec\n",
		    (long)v4l2_buffer.timestamp.tv_usec);
		JOM(16, "  %10i=sequence\n", v4l2_buffer.sequence);
		JOM(16, "  0x%08X=memory\n", v4l2_buffer.memory);
		JOM(16, "  %10i=m.offset\n", v4l2_buffer.m.offset);
		JOM(16, "  %10i=length\n", v4l2_buffer.length);

		if (copy_to_user((void __user *)arg, &v4l2_buffer,
					sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_QBUF: {
		struct v4l2_buffer v4l2_buffer;

		JOM(8, "VIDIOC_QBUF\n");

		if (0 != copy_from_user(&v4l2_buffer, (void __user *)arg,
					sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		if (v4l2_buffer.memory != V4L2_MEMORY_MMAP) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		if (v4l2_buffer.index < 0 ||
		    v4l2_buffer.index >= peasycap->frame_buffer_many) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;

		peasycap->done[v4l2_buffer.index]   = 0;
		peasycap->queued[v4l2_buffer.index] = V4L2_BUF_FLAG_QUEUED;

		if (copy_to_user((void __user *)arg, &v4l2_buffer,
					sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		JOM(8, ".....   user queueing frame buffer %i\n",
		    (int)v4l2_buffer.index);

		peasycap->frame_lock = 0;

		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_DQBUF:
	{
		struct timeval timeval, timeval2;
		int i, j;
		struct v4l2_buffer v4l2_buffer;
		int rcdq;
		u16 input;

		JOM(8, "VIDIOC_DQBUF\n");

		if ((peasycap->video_idle) || (peasycap->video_eof)) {
			JOM(8, "returning -EIO because  "
			    "%i=video_idle  %i=video_eof\n",
			    peasycap->video_idle, peasycap->video_eof);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EIO;
		}

		if (copy_from_user(&v4l2_buffer, (void __user *)arg,
				  sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		if (v4l2_buffer.type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}

		if (peasycap->offerfields) {
			/*---------------------------------------------------*/
			/*
			 *  IN ITS 50 "fps" MODE tvtime SEEMS ALWAYS TO REQUEST
			 *  V4L2_FIELD_BOTTOM
			 */
			/*---------------------------------------------------*/
			if (V4L2_FIELD_TOP == v4l2_buffer.field)
				JOM(8, "user wants V4L2_FIELD_TOP\n");
			else if (V4L2_FIELD_BOTTOM == v4l2_buffer.field)
				JOM(8, "user wants V4L2_FIELD_BOTTOM\n");
			else if (V4L2_FIELD_ANY == v4l2_buffer.field)
				JOM(8, "user wants V4L2_FIELD_ANY\n");
			else
				JOM(8, "user wants V4L2_FIELD_...UNKNOWN: %i\n",
				    v4l2_buffer.field);
		}

		if (!peasycap->video_isoc_streaming) {
			JOM(16, "returning -EIO because video urbs not streaming\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EIO;
		}
	/*-------------------------------------------------------------------*/
	/*
	 *  IF THE USER HAS PREVIOUSLY CALLED easycap_poll(),
	 *  AS DETERMINED BY FINDING
	 *  THE FLAG peasycap->polled SET, THERE MUST BE
	 *  NO FURTHER WAIT HERE.  IN THIS
	 *  CASE, JUST CHOOSE THE FRAME INDICATED BY peasycap->frame_read
	 */
	/*-------------------------------------------------------------------*/

		if (!peasycap->polled) {
			do {
				rcdq = easycap_dqbuf(peasycap, 0);
				if (-EIO == rcdq) {
					JOM(8, "returning -EIO because "
					    "dqbuf() returned -EIO\n");
					mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
					return -EIO;
				}
			} while (0 != rcdq);
		} else {
			if (peasycap->video_eof) {
				mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
				return -EIO;
			}
		}
		if (V4L2_BUF_FLAG_DONE != peasycap->done[peasycap->frame_read]) {
			JOM(8, "V4L2_BUF_FLAG_DONE != 0x%08X\n",
			    peasycap->done[peasycap->frame_read]);
		}
		peasycap->polled = 0;

		if (!(peasycap->isequence % 10)) {
			for (i = 0; i < 179; i++)
				peasycap->merit[i] = peasycap->merit[i+1];
			peasycap->merit[179] = merit_saa(peasycap->pusb_device);
			j = 0;
			for (i = 0; i < 180; i++)
				j += peasycap->merit[i];
			if (90 < j) {
				SAM("easycap driver shutting down "
				    "on condition blue\n");
				peasycap->video_eof = 1;
				peasycap->audio_eof = 1;
			}
		}

		v4l2_buffer.index = peasycap->frame_read;
		v4l2_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buffer.bytesused = peasycap->frame_buffer_used;
		v4l2_buffer.flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;
		if (peasycap->offerfields)
			v4l2_buffer.field = V4L2_FIELD_BOTTOM;
		else
			v4l2_buffer.field = V4L2_FIELD_NONE;
		do_gettimeofday(&timeval);
		timeval2 = timeval;

		v4l2_buffer.timestamp = timeval2;
		v4l2_buffer.sequence = peasycap->isequence++;
		v4l2_buffer.memory = V4L2_MEMORY_MMAP;
		v4l2_buffer.m.offset = v4l2_buffer.index * FRAME_BUFFER_SIZE;
		v4l2_buffer.length = FRAME_BUFFER_SIZE;

		JOM(16, "  %10i=index\n", v4l2_buffer.index);
		JOM(16, "  0x%08X=type\n", v4l2_buffer.type);
		JOM(16, "  %10i=bytesused\n", v4l2_buffer.bytesused);
		JOM(16, "  0x%08X=flags\n", v4l2_buffer.flags);
		JOM(16, "  %10i=field\n", v4l2_buffer.field);
		JOM(16, "  %10li=timestamp.tv_sec\n",
		    (long)v4l2_buffer.timestamp.tv_sec);
		JOM(16, "  %10li=timestamp.tv_usec\n",
		    (long)v4l2_buffer.timestamp.tv_usec);
		JOM(16, "  %10i=sequence\n", v4l2_buffer.sequence);
		JOM(16, "  0x%08X=memory\n", v4l2_buffer.memory);
		JOM(16, "  %10i=m.offset\n", v4l2_buffer.m.offset);
		JOM(16, "  %10i=length\n", v4l2_buffer.length);

		if (copy_to_user((void __user *)arg, &v4l2_buffer,
					sizeof(struct v4l2_buffer))) {
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		input = peasycap->frame_buffer[peasycap->frame_read][0].input;
		if (0x08 & input) {
			JOM(8, "user is offered frame buffer %i, input %i\n",
			    peasycap->frame_read, (0x07 & input));
		} else {
			JOM(8, "user is offered frame buffer %i\n",
			    peasycap->frame_read);
		}
		peasycap->frame_lock = 1;
		JOM(8, "%i=peasycap->frame_fill\n", peasycap->frame_fill);
		if (peasycap->frame_read == peasycap->frame_fill) {
			if (peasycap->frame_lock) {
				JOM(8, "WORRY:  filling frame buffer "
				    "while offered to user\n");
			}
		}
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_STREAMON: {
		int i;

		JOM(8, "VIDIOC_STREAMON\n");

		peasycap->isequence = 0;
		for (i = 0; i < 180; i++)
			peasycap->merit[i] = 0;
		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
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
		JOM(8, "VIDIOC_STREAMOFF\n");

		if (!peasycap->pusb_device) {
			SAM("ERROR: peasycap->pusb_device is NULL\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}

		peasycap->video_idle = 1;
		peasycap->audio_idle = 1;
/*---------------------------------------------------------------------------*/
/*
 *  IF THE WAIT QUEUES ARE NOT CLEARED IN RESPONSE TO THE STREAMOFF COMMAND
 *  THE USERSPACE PROGRAM, E.G. mplayer, MAY HANG ON EXIT.   BEWARE.
 */
/*---------------------------------------------------------------------------*/
		JOM(8, "calling wake_up on wq_video and wq_audio\n");
		wake_up_interruptible(&(peasycap->wq_video));
		if (peasycap->psubstream)
			snd_pcm_period_elapsed(peasycap->psubstream);
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_PARM: {
		struct v4l2_streamparm *pv4l2_streamparm;

		JOM(8, "VIDIOC_G_PARM\n");
		pv4l2_streamparm = memdup_user((void __user *)arg,
					       sizeof(struct v4l2_streamparm));
		if (IS_ERR(pv4l2_streamparm)) {
			SAM("ERROR: copy from user failed\n");
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return PTR_ERR(pv4l2_streamparm);
		}

		if (pv4l2_streamparm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			kfree(pv4l2_streamparm);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EINVAL;
		}
		pv4l2_streamparm->parm.capture.capability = 0;
		pv4l2_streamparm->parm.capture.capturemode = 0;
		pv4l2_streamparm->parm.capture.timeperframe.numerator = 1;

		if (peasycap->fps) {
			pv4l2_streamparm->parm.capture.timeperframe.
			denominator = peasycap->fps;
		} else {
			if (peasycap->ntsc) {
				pv4l2_streamparm->parm.capture.timeperframe.
				denominator = 30;
			} else {
				pv4l2_streamparm->parm.capture.timeperframe.
				denominator = 25;
			}
		}

		pv4l2_streamparm->parm.capture.readbuffers =
			peasycap->frame_buffer_many;
		pv4l2_streamparm->parm.capture.extendedmode = 0;
		if (copy_to_user((void __user *)arg,
				pv4l2_streamparm,
				sizeof(struct v4l2_streamparm))) {
			kfree(pv4l2_streamparm);
			mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
			return -EFAULT;
		}
		kfree(pv4l2_streamparm);
		break;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_PARM: {
		JOM(8, "VIDIOC_S_PARM unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_AUDIO: {
		JOM(8, "VIDIOC_G_AUDIO unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_AUDIO: {
		JOM(8, "VIDIOC_S_AUDIO unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_S_TUNER: {
		JOM(8, "VIDIOC_S_TUNER unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_FBUF:
	case VIDIOC_S_FBUF:
	case VIDIOC_OVERLAY: {
		JOM(8, "VIDIOC_G_FBUF|VIDIOC_S_FBUF|VIDIOC_OVERLAY unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	case VIDIOC_G_TUNER: {
		JOM(8, "VIDIOC_G_TUNER unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY: {
		JOM(8, "VIDIOC_G_FREQUENCY|VIDIOC_S_FREQUENCY unsupported\n");
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -EINVAL;
	}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	default: {
		JOM(8, "ERROR: unrecognized V4L2 IOCTL command: 0x%08X\n", cmd);
		mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
		return -ENOIOCTLCMD;
	}
	}
	mutex_unlock(&easycapdc60_dongle[kd].mutex_video);
	JOM(4, "unlocked easycapdc60_dongle[%i].mutex_video\n", kd);
	return 0;
}
/*****************************************************************************/

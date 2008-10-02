/*
 *  cx18 ioctl control functions
 *
 *  Derived from ivtv-controls.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>

 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

int cx18_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *a);
int cx18_g_ext_ctrls(struct file *file, void *fh, struct v4l2_ext_controls *a);
int cx18_s_ext_ctrls(struct file *file, void *fh, struct v4l2_ext_controls *a);
int cx18_try_ext_ctrls(struct file *file, void *fh,
			struct v4l2_ext_controls *a);
int cx18_querymenu(struct file *file, void *fh, struct v4l2_querymenu *a);

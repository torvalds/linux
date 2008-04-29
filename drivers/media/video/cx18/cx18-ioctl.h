/*
 *  cx18 ioctl system call
 *
 *  Derived from ivtv-ioctl.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

u16 cx18_service2vbi(int type);
void cx18_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal);
u16 cx18_get_service_set(struct v4l2_sliced_vbi_format *fmt);
int cx18_v4l2_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		    unsigned long arg);
int cx18_v4l2_ioctls(struct cx18 *cx, struct file *filp, unsigned cmd,
		     void *arg);

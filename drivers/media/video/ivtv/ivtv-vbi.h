/*
    Vertical Blank Interval support functions
    Copyright (C) 2004-2007  Hans Verkuil <hverkuil@xs4all.nl>

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

ssize_t ivtv_write_vbi(struct ivtv *itv, const char __user *ubuf, size_t count);
void ivtv_process_vbi_data(struct ivtv *itv, struct ivtv_buffer *buf,
			   u64 pts_stamp, int streamtype);
int ivtv_used_line(struct ivtv *itv, int line, int field);
void ivtv_disable_vbi(struct ivtv *itv);
void ivtv_set_vbi(unsigned long arg);
void ivtv_vbi_work_handler(struct ivtv *itv);

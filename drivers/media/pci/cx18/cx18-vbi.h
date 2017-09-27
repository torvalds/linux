/*
 *  cx18 Vertical Blank Interval support functions
 *
 *  Derived from ivtv-vbi.h
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
 */

void cx18_process_vbi_data(struct cx18 *cx, struct cx18_mdl *mdl,
			   int streamtype);
int cx18_used_line(struct cx18 *cx, int line, int field);

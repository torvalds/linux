/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 Vertical Blank Interval support functions
 *
 *  Derived from ivtv-vbi.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 */

void cx18_process_vbi_data(struct cx18 *cx, struct cx18_mdl *mdl,
			   int streamtype);
int cx18_used_line(struct cx18 *cx, int line, int field);

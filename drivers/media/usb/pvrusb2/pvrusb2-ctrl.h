/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#ifndef __PVRUSB2_CTRL_H
#define __PVRUSB2_CTRL_H

struct pvr2_ctrl;

enum pvr2_ctl_type {
	pvr2_ctl_int = 0,
	pvr2_ctl_enum = 1,
	pvr2_ctl_bitmask = 2,
	pvr2_ctl_bool = 3,
};


/* Set the given control. */
int pvr2_ctrl_set_value(struct pvr2_ctrl *,int val);

/* Set/clear specific bits of the given control. */
int pvr2_ctrl_set_mask_value(struct pvr2_ctrl *,int mask,int val);

/* Get the current value of the given control. */
int pvr2_ctrl_get_value(struct pvr2_ctrl *,int *valptr);

/* Retrieve control's type */
enum pvr2_ctl_type pvr2_ctrl_get_type(struct pvr2_ctrl *);

/* Retrieve control's maximum value (int type) */
int pvr2_ctrl_get_max(struct pvr2_ctrl *);

/* Retrieve control's minimum value (int type) */
int pvr2_ctrl_get_min(struct pvr2_ctrl *);

/* Retrieve control's default value (any type) */
int pvr2_ctrl_get_def(struct pvr2_ctrl *, int *valptr);

/* Retrieve control's enumeration count (enum only) */
int pvr2_ctrl_get_cnt(struct pvr2_ctrl *);

/* Retrieve control's valid mask bits (bit mask only) */
int pvr2_ctrl_get_mask(struct pvr2_ctrl *);

/* Retrieve the control's name */
const char *pvr2_ctrl_get_name(struct pvr2_ctrl *);

/* Retrieve the control's desc */
const char *pvr2_ctrl_get_desc(struct pvr2_ctrl *);

/* Retrieve a control enumeration or bit mask value */
int pvr2_ctrl_get_valname(struct pvr2_ctrl *,int,char *,unsigned int,
			  unsigned int *);

/* Return true if control is writable */
int pvr2_ctrl_is_writable(struct pvr2_ctrl *);

/* Return V4L flags value for control (or zero if there is no v4l control
   actually under this control) */
unsigned int pvr2_ctrl_get_v4lflags(struct pvr2_ctrl *);

/* Return V4L ID for this control or zero if none */
int pvr2_ctrl_get_v4lid(struct pvr2_ctrl *);

/* Return true if control has custom symbolic representation */
int pvr2_ctrl_has_custom_symbols(struct pvr2_ctrl *);

/* Convert a given mask/val to a custom symbolic value */
int pvr2_ctrl_custom_value_to_sym(struct pvr2_ctrl *,
				  int mask,int val,
				  char *buf,unsigned int maxlen,
				  unsigned int *len);

/* Convert a symbolic value to a mask/value pair */
int pvr2_ctrl_custom_sym_to_value(struct pvr2_ctrl *,
				  const char *buf,unsigned int len,
				  int *maskptr,int *valptr);

/* Convert a given mask/val to a symbolic value */
int pvr2_ctrl_value_to_sym(struct pvr2_ctrl *,
			   int mask,int val,
			   char *buf,unsigned int maxlen,
			   unsigned int *len);

/* Convert a symbolic value to a mask/value pair */
int pvr2_ctrl_sym_to_value(struct pvr2_ctrl *,
			   const char *buf,unsigned int len,
			   int *maskptr,int *valptr);

/* Convert a given mask/val to a symbolic value - must already be
   inside of critical region. */
int pvr2_ctrl_value_to_sym_internal(struct pvr2_ctrl *,
			   int mask,int val,
			   char *buf,unsigned int maxlen,
			   unsigned int *len);

#endif /* __PVRUSB2_CTRL_H */

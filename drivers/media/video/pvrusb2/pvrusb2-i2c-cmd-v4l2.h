/*
 *
 *  $Id$
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __PVRUSB2_CMD_V4L2_H
#define __PVRUSB2_CMD_V4L2_H

#include "pvrusb2-i2c-core.h"

extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_standard;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_radio;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_bcsh;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_volume;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_frequency;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_size;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_audiomode;
extern const struct pvr2_i2c_op pvr2_i2c_op_v4l2_log;

void pvr2_v4l2_cmd_stream(struct pvr2_i2c_client *,int);
void pvr2_v4l2_cmd_status_poll(struct pvr2_i2c_client *);

#endif /* __PVRUSB2_CMD_V4L2_H */

/*
  Stuff for Emacs to see, in order to encourage consistent editing style:
  *** Local Variables: ***
  *** mode: c ***
  *** fill-column: 70 ***
  *** tab-width: 8 ***
  *** c-basic-offset: 8 ***
  *** End: ***
  */

/*
 *
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
 */

#ifndef __PVRUSB2_ENCODER_H
#define __PVRUSB2_ENCODER_H

struct pvr2_hdw;

int pvr2_encoder_adjust(struct pvr2_hdw *);
int pvr2_encoder_configure(struct pvr2_hdw *);
int pvr2_encoder_start(struct pvr2_hdw *);
int pvr2_encoder_stop(struct pvr2_hdw *);

#endif /* __PVRUSB2_ENCODER_H */

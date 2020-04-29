/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 */

#ifndef __PVRUSB2_ENCODER_H
#define __PVRUSB2_ENCODER_H

struct pvr2_hdw;

int pvr2_encoder_adjust(struct pvr2_hdw *);
int pvr2_encoder_configure(struct pvr2_hdw *);
int pvr2_encoder_start(struct pvr2_hdw *);
int pvr2_encoder_stop(struct pvr2_hdw *);

#endif /* __PVRUSB2_ENCODER_H */

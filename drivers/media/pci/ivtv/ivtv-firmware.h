/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    ivtv firmware functions.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_FIRMWARE_H
#define IVTV_FIRMWARE_H

int ivtv_firmware_init(struct ivtv *itv);
void ivtv_firmware_versions(struct ivtv *itv);
void ivtv_halt_firmware(struct ivtv *itv);
void ivtv_init_mpeg_decoder(struct ivtv *itv);
int ivtv_firmware_check(struct ivtv *itv, char *where);

#endif

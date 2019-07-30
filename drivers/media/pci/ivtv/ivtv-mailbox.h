/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    mailbox functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_MAILBOX_H
#define IVTV_MAILBOX_H

#define IVTV_MBOX_DMA_END         8
#define IVTV_MBOX_DMA             9

void ivtv_api_get_data(struct ivtv_mailbox_data *mbdata, int mb,
		       int argc, u32 data[]);
int ivtv_api(struct ivtv *itv, int cmd, int args, u32 data[]);
int ivtv_vapi_result(struct ivtv *itv, u32 data[CX2341X_MBOX_MAX_DATA], int cmd, int args, ...);
int ivtv_vapi(struct ivtv *itv, int cmd, int args, ...);
int ivtv_api_func(void *priv, u32 cmd, int in, int out, u32 data[CX2341X_MBOX_MAX_DATA]);
void ivtv_mailbox_cache_invalidate(struct ivtv *itv);

#endif

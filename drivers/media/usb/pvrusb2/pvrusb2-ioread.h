/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */
#ifndef __PVRUSB2_IOREAD_H
#define __PVRUSB2_IOREAD_H

#include "pvrusb2-io.h"

struct pvr2_ioread;

struct pvr2_ioread *pvr2_ioread_create(void);
void pvr2_ioread_destroy(struct pvr2_ioread *);
int pvr2_ioread_setup(struct pvr2_ioread *,struct pvr2_stream *);
struct pvr2_stream *pvr2_ioread_get_stream(struct pvr2_ioread *);
void pvr2_ioread_set_sync_key(struct pvr2_ioread *,
			      const char *sync_key_ptr,
			      unsigned int sync_key_len);
int pvr2_ioread_set_enabled(struct pvr2_ioread *,int fl);
int pvr2_ioread_read(struct pvr2_ioread *,void __user *buf,unsigned int cnt);
int pvr2_ioread_avail(struct pvr2_ioread *);

#endif /* __PVRUSB2_IOREAD_H */

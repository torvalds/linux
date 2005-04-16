#ifndef _ZFTAPE_INIT_H
#define _ZFTAPE_INIT_H

/*
 * Copyright (C) 1996, 1997 Claus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-init.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:05 $
 *
 * This file contains definitions and macro for the vfs 
 * interface defined by zftape
 *
 */

#include <linux/ftape-header-segment.h>

#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-buffer.h"
#include "../lowlevel/ftape-format.h"

#include "../zftape/zftape-rw.h"

#ifdef MODULE
#define ftape_status (*zft_status)
#endif

extern const  ftape_info *zft_status; /* needed for zftape-vtbl.h */

#include "../zftape/zftape-vtbl.h"

struct zft_cmpr_ops {
	int (*write)(int *write_cnt,
		     __u8 *dst_buf, const int seg_sz,
		     const __u8 __user *src_buf, const int req_len, 
		     const zft_position *pos, const zft_volinfo *volume);
	int (*read)(int *read_cnt,
		    __u8 __user *dst_buf, const int req_len,
		    const __u8 *src_buf, const int seg_sz,
		    const zft_position *pos, const zft_volinfo *volume);
	int (*seek)(unsigned int new_block_pos,
		    zft_position *pos, const zft_volinfo *volume,
		    __u8 *buffer);
	void (*lock)   (void);
	void (*reset)  (void);
	void (*cleanup)(void);
};

extern struct zft_cmpr_ops *zft_cmpr_ops;
/* zftape-init.c defined global functions.
 */
extern int                  zft_cmpr_register(struct zft_cmpr_ops *new_ops);
extern int                  zft_cmpr_lock(int try_to_load);

#endif



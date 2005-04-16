#ifndef _ZFTAPE_CTL_H
#define _ZFTAPE_CTL_H

/*
 * Copyright (C) 1996, 1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-ctl.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:02 $
 *
 *      This file contains the non-standard IOCTL related definitions
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/config.h>
#include <linux/ioctl.h>
#include <linux/mtio.h>

#include "../zftape/zftape-rw.h"

#ifdef CONFIG_ZFTAPE_MODULE
#define ftape_status (*zft_status)
#endif

extern int zft_offline;
extern int zft_mt_compression;
extern int zft_write_protected;
extern int zft_header_read;
extern unsigned int zft_unit;
extern int zft_resid;

extern void zft_reset_position(zft_position *pos);
extern int  zft_check_write_access(zft_position *pos);
extern int  zft_def_idle_state(void);

/*  hooks for the VFS interface 
 */
extern int  _zft_open(unsigned int dev_minor, unsigned int access_mode);
extern int  _zft_close(void);
extern int  _zft_ioctl(unsigned int command, void __user *arg);
#endif




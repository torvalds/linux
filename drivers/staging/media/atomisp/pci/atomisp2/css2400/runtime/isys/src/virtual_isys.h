#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef __VIRTUAL_ISYS_H_INCLUDED__
#define __VIRTUAL_ISYS_H_INCLUDED__

/* cmd for storing a number of packets indicated by reg _STREAM2MMIO_NUM_ITEMS*/
#define _STREAM2MMIO_CMD_TOKEN_STORE_PACKETS	1

/* command for waiting for a frame start */
#define _STREAM2MMIO_CMD_TOKEN_SYNC_FRAME	2

#endif /* __VIRTUAL_ISYS_H_INCLUDED__ */


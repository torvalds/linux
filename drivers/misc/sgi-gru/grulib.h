/*
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __GRULIB_H__
#define __GRULIB_H__

#define GRU_BASENAME		"gru"
#define GRU_FULLNAME		"/dev/gru"
#define GRU_IOCTL_NUM 		 'G'

/*
 * Maximum number of GRU segments that a user can have open
 * ZZZ temp - set high for testing. Revisit.
 */
#define GRU_MAX_OPEN_CONTEXTS		32

/* Set Number of Request Blocks */
#define GRU_CREATE_CONTEXT		_IOWR(GRU_IOCTL_NUM, 1, void *)

/* Register task as using the slice */
#define GRU_SET_TASK_SLICE		_IOWR(GRU_IOCTL_NUM, 5, void *)

/* Fetch exception detail */
#define GRU_USER_GET_EXCEPTION_DETAIL	_IOWR(GRU_IOCTL_NUM, 6, void *)

/* For user call_os handling - normally a TLB fault */
#define GRU_USER_CALL_OS		_IOWR(GRU_IOCTL_NUM, 8, void *)

/* For user unload context */
#define GRU_USER_UNLOAD_CONTEXT		_IOWR(GRU_IOCTL_NUM, 9, void *)

/* For fetching GRU chiplet status */
#define GRU_GET_CHIPLET_STATUS		_IOWR(GRU_IOCTL_NUM, 10, void *)

/* For user TLB flushing (primarily for tests) */
#define GRU_USER_FLUSH_TLB		_IOWR(GRU_IOCTL_NUM, 50, void *)

/* Get some config options (primarily for tests & emulator) */
#define GRU_GET_CONFIG_INFO		_IOWR(GRU_IOCTL_NUM, 51, void *)

#define CONTEXT_WINDOW_BYTES(th)        (GRU_GSEG_PAGESIZE * (th))
#define THREAD_POINTER(p, th)		(p + GRU_GSEG_PAGESIZE * (th))

/*
 * Structure used to pass TLB flush parameters to the driver
 */
struct gru_create_context_req {
	unsigned long		gseg;
	unsigned int		data_segment_bytes;
	unsigned int		control_blocks;
	unsigned int		maximum_thread_count;
	unsigned int		options;
};

/*
 * Structure used to pass unload context parameters to the driver
 */
struct gru_unload_context_req {
	unsigned long	gseg;
};

/*
 * Structure used to pass TLB flush parameters to the driver
 */
struct gru_flush_tlb_req {
	unsigned long	gseg;
	unsigned long	vaddr;
	size_t		len;
};

/*
 * GRU configuration info (temp - for testing)
 */
struct gru_config_info {
	int		cpus;
	int		blades;
	int		nodes;
	int		chiplets;
	int		fill[16];
};

#endif /* __GRULIB_H__ */

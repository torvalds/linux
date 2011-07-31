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

/*  Set Context Options */
#define GRU_SET_CONTEXT_OPTION		_IOWR(GRU_IOCTL_NUM, 4, void *)

/* Fetch exception detail */
#define GRU_USER_GET_EXCEPTION_DETAIL	_IOWR(GRU_IOCTL_NUM, 6, void *)

/* For user call_os handling - normally a TLB fault */
#define GRU_USER_CALL_OS		_IOWR(GRU_IOCTL_NUM, 8, void *)

/* For user unload context */
#define GRU_USER_UNLOAD_CONTEXT		_IOWR(GRU_IOCTL_NUM, 9, void *)

/* For dumpping GRU chiplet state */
#define GRU_DUMP_CHIPLET_STATE		_IOWR(GRU_IOCTL_NUM, 11, void *)

/* For getting gseg statistics */
#define GRU_GET_GSEG_STATISTICS		_IOWR(GRU_IOCTL_NUM, 12, void *)

/* For user TLB flushing (primarily for tests) */
#define GRU_USER_FLUSH_TLB		_IOWR(GRU_IOCTL_NUM, 50, void *)

/* Get some config options (primarily for tests & emulator) */
#define GRU_GET_CONFIG_INFO		_IOWR(GRU_IOCTL_NUM, 51, void *)

/* Various kernel self-tests */
#define GRU_KTEST			_IOWR(GRU_IOCTL_NUM, 52, void *)

#define CONTEXT_WINDOW_BYTES(th)        (GRU_GSEG_PAGESIZE * (th))
#define THREAD_POINTER(p, th)		(p + GRU_GSEG_PAGESIZE * (th))
#define GSEG_START(cb)			((void *)((unsigned long)(cb) & ~(GRU_GSEG_PAGESIZE - 1)))

struct gru_get_gseg_statistics_req {
	unsigned long			gseg;
	struct gru_gseg_statistics	stats;
};

/*
 * Structure used to pass TLB flush parameters to the driver
 */
struct gru_create_context_req {
	unsigned long		gseg;
	unsigned int		data_segment_bytes;
	unsigned int		control_blocks;
	unsigned int		maximum_thread_count;
	unsigned int		options;
	unsigned char		tlb_preload_count;
};

/*
 * Structure used to pass unload context parameters to the driver
 */
struct gru_unload_context_req {
	unsigned long	gseg;
};

/*
 * Structure used to set context options
 */
enum {sco_gseg_owner, sco_cch_req_slice, sco_blade_chiplet};
struct gru_set_context_option_req {
	unsigned long	gseg;
	int		op;
	int		val0;
	long		val1;
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
 * Structure used to pass TLB flush parameters to the driver
 */
enum {dcs_pid, dcs_gid};
struct gru_dump_chiplet_state_req {
	unsigned int	op;
	unsigned int	gid;
	int		ctxnum;
	char		data_opt;
	char		lock_cch;
	char		flush_cbrs;
	char		fill[10];
	pid_t		pid;
	void		*buf;
	size_t		buflen;
	/* ---- output --- */
	unsigned int	num_contexts;
};

#define GRU_DUMP_MAGIC	0x3474ab6c
struct gru_dump_context_header {
	unsigned int	magic;
	unsigned int	gid;
	unsigned char	ctxnum;
	unsigned char	cbrcnt;
	unsigned char	dsrcnt;
	pid_t		pid;
	unsigned long	vaddr;
	int		cch_locked;
	unsigned long	data[0];
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

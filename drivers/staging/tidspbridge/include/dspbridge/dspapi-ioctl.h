/*
 * dspapi-ioctl.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Contains structures and commands that are used for interaction
 * between the DDSP API and Bridge driver.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DSPAPIIOCTL_
#define DSPAPIIOCTL_

#include <dspbridge/cmm.h>
#include <dspbridge/strmdefs.h>
#include <dspbridge/dbdcd.h>

union trapped_args {

	/* MGR Module */
	struct {
		u32 node_id;
		struct dsp_ndbprops __user *ndb_props;
		u32 ndb_props_size;
		u32 __user *num_nodes;
	} args_mgr_enumnode_info;

	struct {
		u32 processor_id;
		struct dsp_processorinfo __user *processor_info;
		u32 processor_info_size;
		u32 __user *num_procs;
	} args_mgr_enumproc_info;

	struct {
		struct dsp_uuid *uuid_obj;
		enum dsp_dcdobjtype obj_type;
		char *sz_path_name;
	} args_mgr_registerobject;

	struct {
		struct dsp_uuid *uuid_obj;
		enum dsp_dcdobjtype obj_type;
	} args_mgr_unregisterobject;

	struct {
		struct dsp_notification __user *__user *anotifications;
		u32 count;
		u32 __user *index;
		u32 timeout;
	} args_mgr_wait;

	/* PROC Module */
	struct {
		u32 processor_id;
		struct dsp_processorattrin __user *attr_in;
		void *__user *ph_processor;
	} args_proc_attach;

	struct {
		void *processor;
		u32 cmd;
		struct dsp_cbdata __user *args;
	} args_proc_ctrl;

	struct {
		void *processor;
	} args_proc_detach;

	struct {
		void *processor;
		void *__user *node_tab;
		u32 node_tab_size;
		u32 __user *num_nodes;
		u32 __user *allocated;
	} args_proc_enumnode_info;

	struct {
		void *processor;
		u32 resource_type;
		struct dsp_resourceinfo *resource_info;
		u32 resource_info_size;
	} args_proc_enumresources;

	struct {
		void *processor;
		struct dsp_processorstate __user *proc_state_obj;
		u32 state_info_size;
	} args_proc_getstate;

	struct {
		void *processor;
		u8 __user *buf;
		u8 __user *size;
		u32 max_size;
	} args_proc_gettrace;

	struct {
		void *processor;
		s32 argc_index;
		char __user *__user *user_args;
		char *__user *user_envp;
	} args_proc_load;

	struct {
		void *processor;
		u32 event_mask;
		u32 notify_type;
		struct dsp_notification __user *notification;
	} args_proc_register_notify;

	struct {
		void *processor;
		u32 size;
		void *__user *rsv_addr;
	} args_proc_rsvmem;

	struct {
		void *processor;
		u32 size;
		void *rsv_addr;
	} args_proc_unrsvmem;

	struct {
		void *processor;
		void *mpu_addr;
		u32 size;
		void *req_addr;
		void *__user *map_addr;
		u32 map_attr;
	} args_proc_mapmem;

	struct {
		void *processor;
		u32 size;
		void *map_addr;
	} args_proc_unmapmem;

	struct {
		void *processor;
		void *mpu_addr;
		u32 size;
		u32 dir;
	} args_proc_dma;

	struct {
		void *processor;
		void *mpu_addr;
		u32 size;
		u32 flags;
	} args_proc_flushmemory;

	struct {
		void *processor;
		void *mpu_addr;
		u32 size;
	} args_proc_invalidatememory;

	/* NODE Module */
	struct {
		void *processor;
		struct dsp_uuid __user *node_id_ptr;
		struct dsp_cbdata __user *args;
		struct dsp_nodeattrin __user *attr_in;
		void *__user *node;
	} args_node_allocate;

	struct {
		void *node;
		u32 size;
		struct dsp_bufferattr __user *attr;
		u8 *__user *buffer;
	} args_node_allocmsgbuf;

	struct {
		void *node;
		s32 prio;
	} args_node_changepriority;

	struct {
		void *node;
		u32 stream_id;
		void *other_node;
		u32 other_stream;
		struct dsp_strmattr __user *attrs;
		struct dsp_cbdata __user *conn_param;
	} args_node_connect;

	struct {
		void *node;
	} args_node_create;

	struct {
		void *node;
	} args_node_delete;

	struct {
		void *node;
		struct dsp_bufferattr __user *attr;
		u8 *buffer;
	} args_node_freemsgbuf;

	struct {
		void *node;
		struct dsp_nodeattr __user *attr;
		u32 attr_size;
	} args_node_getattr;

	struct {
		void *node;
		struct dsp_msg __user *message;
		u32 timeout;
	} args_node_getmessage;

	struct {
		void *node;
	} args_node_pause;

	struct {
		void *node;
		struct dsp_msg __user *message;
		u32 timeout;
	} args_node_putmessage;

	struct {
		void *node;
		u32 event_mask;
		u32 notify_type;
		struct dsp_notification __user *notification;
	} args_node_registernotify;

	struct {
		void *node;
	} args_node_run;

	struct {
		void *node;
		int __user *status;
	} args_node_terminate;

	struct {
		void *processor;
		struct dsp_uuid __user *node_id_ptr;
		struct dsp_ndbprops __user *node_props;
	} args_node_getuuidprops;

	/* STRM module */

	struct {
		void *stream;
		u32 size;
		u8 *__user *ap_buffer;
		u32 num_bufs;
	} args_strm_allocatebuffer;

	struct {
		void *stream;
	} args_strm_close;

	struct {
		void *stream;
		u8 *__user *ap_buffer;
		u32 num_bufs;
	} args_strm_freebuffer;

	struct {
		void *stream;
		void **event;
	} args_strm_geteventhandle;

	struct {
		void *stream;
		struct stream_info __user *stream_info;
		u32 stream_info_size;
	} args_strm_getinfo;

	struct {
		void *stream;
		bool flush_flag;
	} args_strm_idle;

	struct {
		void *stream;
		u8 *buffer;
		u32 bytes;
		u32 buf_size;
		u32 arg;
	} args_strm_issue;

	struct {
		void *node;
		u32 direction;
		u32 index;
		struct strm_attr __user *attr_in;
		void *__user *stream;
	} args_strm_open;

	struct {
		void *stream;
		u8 *__user *buf_ptr;
		u32 __user *bytes;
		u32 __user *buf_size_ptr;
		u32 __user *arg;
	} args_strm_reclaim;

	struct {
		void *stream;
		u32 event_mask;
		u32 notify_type;
		struct dsp_notification __user *notification;
	} args_strm_registernotify;

	struct {
		void *__user *stream_tab;
		u32 strm_num;
		u32 __user *mask;
		u32 timeout;
	} args_strm_select;

	/* CMM Module */
	struct {
		struct cmm_object *cmm_mgr;
		u32 size;
		struct cmm_attrs *attrs;
		void **buf_va;
	} args_cmm_allocbuf;

	struct {
		struct cmm_object *cmm_mgr;
		void *buf_pa;
		u32 seg_id;
	} args_cmm_freebuf;

	struct {
		void *processor;
		struct cmm_object *__user *cmm_mgr;
	} args_cmm_gethandle;

	struct {
		struct cmm_object *cmm_mgr;
		struct cmm_info __user *cmm_info_obj;
	} args_cmm_getinfo;

	/* UTIL module */
	struct {
		s32 util_argc;
		char **argv;
	} args_util_testdll;
};

/*
 * Dspbridge Ioctl numbering scheme
 *
 *    7                           0
 *  ---------------------------------
 *  |  Module   |   Ioctl Number    |
 *  ---------------------------------
 *  | x | x | x | 0 | 0 | 0 | 0 | 0 |
 *  ---------------------------------
 */

/* Ioctl driver identifier */
#define DB		0xDB

/*
 * Following are used to distinguish between module ioctls, this is needed
 * in case new ioctls are introduced.
 */
#define DB_MODULE_MASK		0xE0
#define DB_IOC_MASK		0x1F

/* Ioctl module masks */
#define DB_MGR		0x0
#define DB_PROC		0x20
#define DB_NODE		0x40
#define DB_STRM		0x60
#define DB_CMM		0x80

#define DB_MODULE_SHIFT		5

/* Used to calculate the ioctl per dspbridge module */
#define DB_IOC(module, num) \
			(((module) & DB_MODULE_MASK) | ((num) & DB_IOC_MASK))
/* Used to get dspbridge ioctl module */
#define DB_GET_MODULE(cmd)	((cmd) & DB_MODULE_MASK)
/* Used to get dspbridge ioctl number */
#define DB_GET_IOC(cmd)		((cmd) & DB_IOC_MASK)

/* TODO: Remove deprecated and not implemented */

/* MGR Module */
#define MGR_ENUMNODE_INFO	_IOWR(DB, DB_IOC(DB_MGR, 0), unsigned long)
#define MGR_ENUMPROC_INFO	_IOWR(DB, DB_IOC(DB_MGR, 1), unsigned long)
#define MGR_REGISTEROBJECT	_IOWR(DB, DB_IOC(DB_MGR, 2), unsigned long)
#define MGR_UNREGISTEROBJECT	_IOWR(DB, DB_IOC(DB_MGR, 3), unsigned long)
#define MGR_WAIT		_IOWR(DB, DB_IOC(DB_MGR, 4), unsigned long)
/* MGR_GET_PROC_RES Deprecated */
#define MGR_GET_PROC_RES	_IOR(DB, DB_IOC(DB_MGR, 5), unsigned long)

/* PROC Module */
#define PROC_ATTACH		_IOWR(DB, DB_IOC(DB_PROC, 0), unsigned long)
#define PROC_CTRL		_IOR(DB, DB_IOC(DB_PROC, 1), unsigned long)
/* PROC_DETACH Deprecated */
#define PROC_DETACH		_IOR(DB, DB_IOC(DB_PROC, 2), unsigned long)
#define PROC_ENUMNODE		_IOWR(DB, DB_IOC(DB_PROC, 3), unsigned long)
#define PROC_ENUMRESOURCES	_IOWR(DB, DB_IOC(DB_PROC, 4), unsigned long)
#define PROC_GET_STATE		_IOWR(DB, DB_IOC(DB_PROC, 5), unsigned long)
#define PROC_GET_TRACE		_IOWR(DB, DB_IOC(DB_PROC, 6), unsigned long)
#define PROC_LOAD		_IOW(DB, DB_IOC(DB_PROC, 7), unsigned long)
#define PROC_REGISTERNOTIFY	_IOWR(DB, DB_IOC(DB_PROC, 8), unsigned long)
#define PROC_START		_IOW(DB, DB_IOC(DB_PROC, 9), unsigned long)
#define PROC_RSVMEM		_IOWR(DB, DB_IOC(DB_PROC, 10), unsigned long)
#define PROC_UNRSVMEM		_IOW(DB, DB_IOC(DB_PROC, 11), unsigned long)
#define PROC_MAPMEM		_IOWR(DB, DB_IOC(DB_PROC, 12), unsigned long)
#define PROC_UNMAPMEM		_IOR(DB, DB_IOC(DB_PROC, 13), unsigned long)
#define PROC_FLUSHMEMORY	_IOW(DB, DB_IOC(DB_PROC, 14), unsigned long)
#define PROC_STOP		_IOWR(DB, DB_IOC(DB_PROC, 15), unsigned long)
#define PROC_INVALIDATEMEMORY	_IOW(DB, DB_IOC(DB_PROC, 16), unsigned long)
#define PROC_BEGINDMA		_IOW(DB, DB_IOC(DB_PROC, 17), unsigned long)
#define PROC_ENDDMA		_IOW(DB, DB_IOC(DB_PROC, 18), unsigned long)

/* NODE Module */
#define NODE_ALLOCATE		_IOWR(DB, DB_IOC(DB_NODE, 0), unsigned long)
#define NODE_ALLOCMSGBUF	_IOWR(DB, DB_IOC(DB_NODE, 1), unsigned long)
#define NODE_CHANGEPRIORITY	_IOW(DB, DB_IOC(DB_NODE, 2), unsigned long)
#define NODE_CONNECT		_IOW(DB, DB_IOC(DB_NODE, 3), unsigned long)
#define NODE_CREATE		_IOW(DB, DB_IOC(DB_NODE, 4), unsigned long)
#define NODE_DELETE		_IOW(DB, DB_IOC(DB_NODE, 5), unsigned long)
#define NODE_FREEMSGBUF		_IOW(DB, DB_IOC(DB_NODE, 6), unsigned long)
#define NODE_GETATTR		_IOWR(DB, DB_IOC(DB_NODE, 7), unsigned long)
#define NODE_GETMESSAGE		_IOWR(DB, DB_IOC(DB_NODE, 8), unsigned long)
#define NODE_PAUSE		_IOW(DB, DB_IOC(DB_NODE, 9), unsigned long)
#define NODE_PUTMESSAGE		_IOW(DB, DB_IOC(DB_NODE, 10), unsigned long)
#define NODE_REGISTERNOTIFY	_IOWR(DB, DB_IOC(DB_NODE, 11), unsigned long)
#define NODE_RUN		_IOW(DB, DB_IOC(DB_NODE, 12), unsigned long)
#define NODE_TERMINATE		_IOWR(DB, DB_IOC(DB_NODE, 13), unsigned long)
#define NODE_GETUUIDPROPS	_IOWR(DB, DB_IOC(DB_NODE, 14), unsigned long)

/* STRM Module */
#define STRM_ALLOCATEBUFFER	_IOWR(DB, DB_IOC(DB_STRM, 0), unsigned long)
#define STRM_CLOSE		_IOW(DB, DB_IOC(DB_STRM, 1), unsigned long)
#define STRM_FREEBUFFER		_IOWR(DB, DB_IOC(DB_STRM, 2), unsigned long)
#define STRM_GETEVENTHANDLE	_IO(DB, DB_IOC(DB_STRM, 3))	/* Not Impl'd */
#define STRM_GETINFO		_IOWR(DB, DB_IOC(DB_STRM, 4), unsigned long)
#define STRM_IDLE		_IOW(DB, DB_IOC(DB_STRM, 5), unsigned long)
#define STRM_ISSUE		_IOW(DB, DB_IOC(DB_STRM, 6), unsigned long)
#define STRM_OPEN		_IOWR(DB, DB_IOC(DB_STRM, 7), unsigned long)
#define STRM_RECLAIM		_IOWR(DB, DB_IOC(DB_STRM, 8), unsigned long)
#define STRM_REGISTERNOTIFY	_IOWR(DB, DB_IOC(DB_STRM, 9), unsigned long)
#define STRM_SELECT		_IOWR(DB, DB_IOC(DB_STRM, 10), unsigned long)

/* CMM Module */
#define CMM_ALLOCBUF		_IO(DB, DB_IOC(DB_CMM, 0))	/* Not Impl'd */
#define CMM_FREEBUF		_IO(DB, DB_IOC(DB_CMM, 1))	/* Not Impl'd */
#define CMM_GETHANDLE		_IOR(DB, DB_IOC(DB_CMM, 2), unsigned long)
#define CMM_GETINFO		_IOR(DB, DB_IOC(DB_CMM, 3), unsigned long)

#endif /* DSPAPIIOCTL_ */

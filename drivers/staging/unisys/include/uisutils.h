/* uisutils.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * Unisys Virtual HBA utilities header
 */

#ifndef __UISUTILS__H__
#define __UISUTILS__H__
#include <linux/string.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/uuid.h>
#include <linux/if_ether.h>

#include "vmcallinterface.h"
#include "channel.h"
#include "uisthread.h"
#include "uisqueue.h"
#include "diagnostics/appos_subsystems.h"
#include "vbusdeviceinfo.h"
#include <linux/atomic.h>

/* This is the MAGIC number stuffed by virthba in host->this_id. Used to
 * identify virtual hbas.
 */
#define UIS_MAGIC_VHBA 707

/* global function pointers that act as callback functions into
 * uisnicmod, uissdmod, and virtpcimod
 */
extern int (*uisnic_control_chan_func)(struct io_msgs *);
extern int (*uissd_control_chan_func)(struct io_msgs *);
extern int (*virt_control_chan_func)(struct guest_msgs *);

/* Return values of above callback functions: */
#define CCF_ERROR        0	/* completed and failed */
#define CCF_OK           1	/* completed successfully */
#define CCF_PENDING      2	/* operation still pending */
extern atomic_t uisutils_registered_services;

struct req_handler_info {
	uuid_le switch_uuid;
	int (*controlfunc)(struct io_msgs *);
	unsigned long min_channel_bytes;
	int (*server_channel_ok)(unsigned long channel_bytes);
	int (*server_channel_init)(void *x, unsigned char *client_str,
				   u32 client_str_len, u64 bytes);
	char switch_type_name[99];
	struct list_head list_link;	/* links into ReqHandlerInfo_list */
};

struct req_handler_info *req_handler_add(uuid_le switch_uuid,
				const char *switch_type_name,
				int (*controlfunc)(struct io_msgs *),
				unsigned long min_channel_bytes,
				int (*svr_channel_ok)(unsigned long
							 channel_bytes),
				int (*svr_channel_init)(void *x,
						unsigned char *client_str,
						u32 client_str_len, u64 bytes));
struct req_handler_info *req_handler_find(uuid_le switch_uuid);
int req_handler_del(uuid_le switch_uuid);

#define uislib_ioremap_cache(addr, size) \
	dbg_ioremap_cache(addr, size, __FILE__, __LINE__)

static inline void __iomem *
dbg_ioremap_cache(u64 addr, unsigned long size, char *file, int line)
{
	void __iomem *new;

	new = ioremap_cache(addr, size);
	return new;
}

#define uislib_ioremap(addr, size) dbg_ioremap(addr, size, __FILE__, __LINE__)

static inline void *
dbg_ioremap(u64 addr, unsigned long size, char *file, int line)
{
	void *new;

	new = ioremap(addr, size);
	return new;
}

#define uislib_iounmap(addr) dbg_iounmap(addr, __FILE__, __LINE__)

static inline void
dbg_iounmap(void __iomem *addr, char *file, int line)
{
	iounmap(addr);
}

#define PROC_READ_BUFFER_SIZE 131072	/* size of the buffer to allocate to
					 * hold all of /proc/XXX/info */
int uisutil_add_proc_line_ex(int *total, char **buffer, int *buffer_remaining,
			     char *format, ...);

int uisctrl_register_req_handler(int type, void *fptr,
			struct ultra_vbus_deviceinfo *chipset_driver_info);
int uisctrl_register_req_handler_ex(uuid_le switch_guid,
			const char *switch_type_name,
			int (*fptr)(struct io_msgs *),
			unsigned long min_channel_bytes,
			int (*svr_channel_ok)(unsigned long
					      channel_bytes),
			int (*svr_channel_init)(void *x,
						unsigned char *client_str,
						u32 client_str_len,
						u64 bytes),
			struct ultra_vbus_deviceinfo *chipset_driver_info);

int uisctrl_unregister_req_handler_ex(uuid_le switch_uuid);
unsigned char *util_map_virt(struct phys_info *sg);
void util_unmap_virt(struct phys_info *sg);
unsigned char *util_map_virt_atomic(struct phys_info *sg);
void util_unmap_virt_atomic(void *buf);
int uislib_client_inject_add_bus(u32 bus_no, uuid_le inst_uuid,
				 u64 channel_addr, ulong n_channel_bytes);
int  uislib_client_inject_del_bus(u32 bus_no);

int uislib_client_inject_add_vhba(u32 bus_no, u32 dev_no,
				  u64 phys_chan_addr, u32 chan_bytes,
				  int is_test_addr, uuid_le inst_uuid,
				  struct irq_info *intr);
int  uislib_client_inject_pause_vhba(u32 bus_no, u32 dev_no);
int  uislib_client_inject_resume_vhba(u32 bus_no, u32 dev_no);
int uislib_client_inject_del_vhba(u32 bus_no, u32 dev_no);
int uislib_client_inject_add_vnic(u32 bus_no, u32 dev_no,
				  u64 phys_chan_addr, u32 chan_bytes,
				  int is_test_addr, uuid_le inst_uuid,
				  struct irq_info *intr);
int uislib_client_inject_pause_vnic(u32 bus_no, u32 dev_no);
int uislib_client_inject_resume_vnic(u32 bus_no, u32 dev_no);
int uislib_client_inject_del_vnic(u32 bus_no, u32 dev_no);
#ifdef STORAGE_CHANNEL
u64 uislib_storage_channel(int client_id);
#endif
int uislib_get_owned_pdest(struct uisscsi_dest *pdest);

int uislib_send_event(enum controlvm_id id,
		      struct controlvm_message_packet *event);

/* structure used by vhba & vnic to keep track of queue & thread info */
struct chaninfo {
	struct uisqueue_info *queueinfo;
	/* this specifies the queue structures for a channel */
	/* ALLOCATED BY THE OTHER END - WE JUST GET A POINTER TO THE MEMORY */
	spinlock_t insertlock;
	/* currently used only in virtnic when sending data to uisnic */
	/* to synchronize the inserts into the signal queue */
	struct uisthread_info threadinfo;
	/* this specifies the thread structures used by the thread that */
	/* handles this channel */
};

/* this is the wait code for all the threads - it is used to get
* something from a queue choices: wait_for_completion_interruptible,
* _timeout, interruptible_timeout
*/
#define UIS_THREAD_WAIT_MSEC(x) { \
	set_current_state(TASK_INTERRUPTIBLE); \
	schedule_timeout(msecs_to_jiffies(x)); \
}

#define UIS_THREAD_WAIT_USEC(x) { \
	set_current_state(TASK_INTERRUPTIBLE); \
	schedule_timeout(usecs_to_jiffies(x)); \
}

#define UIS_THREAD_WAIT UIS_THREAD_WAIT_MSEC(5)

#define UIS_THREAD_WAIT_SEC(x) { \
	set_current_state(TASK_INTERRUPTIBLE); \
	schedule_timeout((x)*HZ); \
}

/* This is a hack until we fix IOVM to initialize the channel header
 * correctly at DEVICE_CREATE time, INSTEAD OF waiting until
 * DEVICE_CONFIGURE time.
 */
static inline void
wait_for_valid_guid(uuid_le __iomem *guid)
{
	uuid_le tmpguid;

	while (1) {
		memcpy_fromio((void *)&tmpguid,
			      (void __iomem *)guid, sizeof(uuid_le));
		if (uuid_le_cmp(tmpguid, NULL_UUID_LE) != 0)
			break;
		LOGERR("Waiting for non-0 GUID (why???)...\n");
		UIS_THREAD_WAIT_SEC(5);
	}
	LOGERR("OK... GUID is non-0 now\n");
}

/* CopyFragsInfoFromSkb returns the number of entries added to frags array
 * Returns -1 on failure.
 */
unsigned int uisutil_copy_fragsinfo_from_skb(unsigned char *calling_ctx,
					     void *skb_in,
					     unsigned int firstfraglen,
					     unsigned int frags_max,
					     struct phys_info frags[]);

static inline unsigned int
issue_vmcall_io_controlvm_addr(u64 *control_addr, u32 *control_bytes)
{
	struct vmcall_io_controlvm_addr_params params;
	int result = VMCALL_SUCCESS;
	u64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_CONTROLVM_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result)) {
		*control_addr = params.address;
		*control_bytes = params.channel_bytes;
	}
	return result;
}

static inline unsigned int issue_vmcall_io_diag_addr(u64 *diag_channel_addr)
{
	struct vmcall_io_diag_addr_params params;
	int result = VMCALL_SUCCESS;
	u64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_DIAG_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result))
		*diag_channel_addr = params.address;
	return result;
}

static inline unsigned int issue_vmcall_io_visorserial_addr(u64 *channel_addr)
{
	struct vmcall_io_visorserial_addr_params params;
	int result = VMCALL_SUCCESS;
	u64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_VISORSERIAL_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result))
		*channel_addr = params.address;
	return result;
}

static inline s64 issue_vmcall_query_guest_virtual_time_offset(void)
{
	u64 result = VMCALL_SUCCESS;
	u64 physaddr = 0;

	ISSUE_IO_VMCALL(VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET, physaddr,
			result);
	return result;
}

struct log_info_t {
	unsigned long long last_cycles;
	unsigned long long delta_sum[64];
	unsigned long long delta_cnt[64];
	unsigned long long max_delta[64];
	unsigned long long min_delta[64];
};

static inline int issue_vmcall_update_physical_time(u64 adjustment)
{
	int result = VMCALL_SUCCESS;

	ISSUE_IO_VMCALL(VMCALL_UPDATE_PHYSICAL_TIME, adjustment, result);
	return result;
}

static inline unsigned int issue_vmcall_channel_mismatch(const char *chname,
			      const char *item_name, u32 line_no,
			      const char *path_n_fn)
{
	struct vmcall_channel_version_mismatch_params params;
	int result = VMCALL_SUCCESS;
	u64 physaddr;
	char *last_slash = NULL;

	strlcpy(params.chname, chname, sizeof(params.chname));
	strlcpy(params.item_name, item_name, sizeof(params.item_name));
	params.line_no = line_no;

	last_slash = strrchr(path_n_fn, '/');
	if (last_slash != NULL) {
		last_slash++;
		strlcpy(params.file_name, last_slash, sizeof(params.file_name));
	} else
		strlcpy(params.file_name,
			"Cannot determine source filename",
			sizeof(params.file_name));

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_CHANNEL_VERSION_MISMATCH, physaddr, result);
	return result;
}

#define UIS_DAEMONIZE(nam)
void *uislib_cache_alloc(struct kmem_cache *cur_pool, char *fn, int ln);
#define UISCACHEALLOC(cur_pool) uislib_cache_alloc(cur_pool, __FILE__, __LINE__)
void uislib_cache_free(struct kmem_cache *cur_pool, void *p, char *fn, int ln);
#define UISCACHEFREE(cur_pool, p) \
	uislib_cache_free(cur_pool, p, __FILE__, __LINE__)

void uislib_enable_channel_interrupts(u32 bus_no, u32 dev_no,
				      int (*interrupt)(void *),
				      void *interrupt_context);
void uislib_disable_channel_interrupts(u32 bus_no, u32 dev_no);
void uislib_force_channel_interrupt(u32 bus_no, u32 dev_no);

#endif /* __UISUTILS__H__ */

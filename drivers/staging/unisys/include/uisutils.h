/* uisutils.h
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
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
extern int (*UisnicControlChanFunc)(struct io_msgs *);
extern int (*UissdControlChanFunc)(struct io_msgs *);
extern int (*VirtControlChanFunc)(struct guest_msgs *);

/* Return values of above callback functions: */
#define CCF_ERROR        0	/* completed and failed */
#define CCF_OK           1	/* completed successfully */
#define CCF_PENDING      2	/* operation still pending */
extern atomic_t UisUtils_Registered_Services;

typedef unsigned int MACARRAY[MAX_MACADDR_LEN];
typedef struct ReqHandlerInfo_struct {
	GUID switchTypeGuid;
	int (*controlfunc)(struct io_msgs *);
	unsigned long min_channel_bytes;
	int (*Server_Channel_Ok)(unsigned long channelBytes);
	int (*Server_Channel_Init)
	 (void *x, unsigned char *clientStr, U32 clientStrLen, U64 bytes);
	char switch_type_name[99];
	struct list_head list_link;	/* links into ReqHandlerInfo_list */
} ReqHandlerInfo_t;

ReqHandlerInfo_t *ReqHandlerAdd(GUID switchTypeGuid,
				const char *switch_type_name,
				int (*controlfunc)(struct io_msgs *),
				unsigned long min_channel_bytes,
				int (*Server_Channel_Ok)(unsigned long
							 channelBytes),
				int (*Server_Channel_Init)
				 (void *x, unsigned char *clientStr,
				  U32 clientStrLen, U64 bytes));
ReqHandlerInfo_t *ReqHandlerFind(GUID switchTypeGuid);
int ReqHandlerDel(GUID switchTypeGuid);

#define uislib_ioremap_cache(addr, size) \
	dbg_ioremap_cache(addr, size, __FILE__, __LINE__)

static inline void __iomem *
dbg_ioremap_cache(U64 addr, unsigned long size, char *file, int line)
{
	void __iomem *new;
	new = ioremap_cache(addr, size);
	return new;
}

#define uislib_ioremap(addr, size) dbg_ioremap(addr, size, __FILE__, __LINE__)

static inline void *
dbg_ioremap(U64 addr, unsigned long size, char *file, int line)
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
				 ULTRA_VBUS_DEVICEINFO *chipset_DriverInfo);
int uisctrl_register_req_handler_ex(GUID switchTypeGuid,
				    const char *switch_type_name,
				    int (*fptr)(struct io_msgs *),
				    unsigned long min_channel_bytes,
				    int (*Server_Channel_Ok)(unsigned long
							     channelBytes),
				    int (*Server_Channel_Init)
				    (void *x, unsigned char *clientStr,
				     U32 clientStrLen, U64 bytes),
				    ULTRA_VBUS_DEVICEINFO *chipset_DriverInfo);

int uisctrl_unregister_req_handler_ex(GUID switchTypeGuid);
unsigned char *util_map_virt(struct phys_info *sg);
void util_unmap_virt(struct phys_info *sg);
unsigned char *util_map_virt_atomic(struct phys_info *sg);
void util_unmap_virt_atomic(void *buf);
int uislib_server_inject_add_vnic(U32 switchNo, U32 BusNo, U32 numIntPorts,
				  U32 numExtPorts, MACARRAY pmac[],
				  pCHANNEL_HEADER **chan);
void uislib_server_inject_del_vnic(U32 switchNo, U32 busNo, U32 numIntPorts,
				   U32 numExtPorts);
int uislib_client_inject_add_bus(U32 busNo, GUID instGuid,
				 U64 channelAddr, ulong nChannelBytes);
int  uislib_client_inject_del_bus(U32 busNo);

int uislib_client_inject_add_vhba(U32 busNo, U32 devNo,
				  U64 phys_chan_addr, U32 chan_bytes,
				  int is_test_addr, GUID instGuid,
				  struct InterruptInfo *intr);
int  uislib_client_inject_pause_vhba(U32 busNo, U32 devNo);
int  uislib_client_inject_resume_vhba(U32 busNo, U32 devNo);
int uislib_client_inject_del_vhba(U32 busNo, U32 devNo);
int uislib_client_inject_add_vnic(U32 busNo, U32 devNo,
				  U64 phys_chan_addr, U32 chan_bytes,
				  int is_test_addr, GUID instGuid,
				  struct InterruptInfo *intr);
int uislib_client_inject_pause_vnic(U32 busNo, U32 devNo);
int uislib_client_inject_resume_vnic(U32 busNo, U32 devNo);
int uislib_client_inject_del_vnic(U32 busNo, U32 devNo);
#ifdef STORAGE_CHANNEL
U64 uislib_storage_channel(int client_id);
#endif
int uislib_get_owned_pdest(struct uisscsi_dest *pdest);

int uislib_send_event(CONTROLVM_ID id, CONTROLVM_MESSAGE_PACKET *event);

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
#define WAIT_FOR_VALID_GUID(guid) \
	do {						   \
		while (MEMCMP_IO(&guid, &Guid0, sizeof(Guid0)) == 0) {	\
			LOGERR("Waiting for non-0 GUID (why???)...\n"); \
			UIS_THREAD_WAIT_SEC(5);				\
		}							\
		LOGERR("OK... GUID is non-0 now\n");			\
	} while (0)

/* CopyFragsInfoFromSkb returns the number of entries added to frags array
 * Returns -1 on failure.
 */
unsigned int uisutil_copy_fragsinfo_from_skb(unsigned char *calling_ctx,
					     void *skb_in,
					     unsigned int firstfraglen,
					     unsigned int frags_max,
					     struct phys_info frags[]);

static inline unsigned int
Issue_VMCALL_IO_CONTROLVM_ADDR(U64 *ControlAddress, U32 *ControlBytes)
{
	VMCALL_IO_CONTROLVM_ADDR_PARAMS params;
	int result = VMCALL_SUCCESS;
	U64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_CONTROLVM_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result)) {
		*ControlAddress = params.ChannelAddress;
		*ControlBytes = params.ChannelBytes;
	}
	return result;
}

static inline unsigned int Issue_VMCALL_IO_DIAG_ADDR(U64 *DiagChannelAddress)
{
	VMCALL_IO_DIAG_ADDR_PARAMS params;
	int result = VMCALL_SUCCESS;
	U64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_DIAG_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result))
		*DiagChannelAddress = params.ChannelAddress;
	return result;
}

static inline unsigned int
Issue_VMCALL_IO_VISORSERIAL_ADDR(U64 *DiagChannelAddress)
{
	VMCALL_IO_VISORSERIAL_ADDR_PARAMS params;
	int result = VMCALL_SUCCESS;
	U64 physaddr;

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_IO_VISORSERIAL_ADDR, physaddr, result);
	if (VMCALL_SUCCESSFUL(result))
		*DiagChannelAddress = params.ChannelAddress;
	return result;
}

static inline S64 Issue_VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET(void)
{
	U64 result = VMCALL_SUCCESS;
	U64 physaddr = 0;

	ISSUE_IO_VMCALL(VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET, physaddr,
			result);
	return result;
}

static inline S64 Issue_VMCALL_MEASUREMENT_DO_NOTHING(void)
{
	U64 result = VMCALL_SUCCESS;
	U64 physaddr = 0;

	ISSUE_IO_VMCALL(VMCALL_MEASUREMENT_DO_NOTHING, physaddr, result);
	return result;
}

struct log_info_t {
	volatile unsigned long long last_cycles;
	unsigned long long delta_sum[64];
	unsigned long long delta_cnt[64];
	unsigned long long max_delta[64];
	unsigned long long min_delta[64];
};

static inline int Issue_VMCALL_UPDATE_PHYSICAL_TIME(U64 adjustment)
{
	int result = VMCALL_SUCCESS;

	ISSUE_IO_VMCALL(VMCALL_UPDATE_PHYSICAL_TIME, adjustment, result);
	return result;
}

static inline unsigned int
Issue_VMCALL_CHANNEL_MISMATCH(const char *ChannelName,
			      const char *ItemName,
			      U32 SourceLineNumber, const char *path_n_fn)
{
	VMCALL_CHANNEL_VERSION_MISMATCH_PARAMS params;
	int result = VMCALL_SUCCESS;
	U64 physaddr;
	char *last_slash = NULL;

	strncpy(params.ChannelName, ChannelName,
		lengthof(VMCALL_CHANNEL_VERSION_MISMATCH_PARAMS, ChannelName));
	strncpy(params.ItemName, ItemName,
		lengthof(VMCALL_CHANNEL_VERSION_MISMATCH_PARAMS, ItemName));
	params.SourceLineNumber = SourceLineNumber;

	last_slash = strrchr(path_n_fn, '/');
	if (last_slash != NULL) {
		last_slash++;
		strncpy(params.SourceFileName, last_slash,
			lengthof(VMCALL_CHANNEL_VERSION_MISMATCH_PARAMS,
				 SourceFileName));
	} else
		strncpy(params.SourceFileName,
			"Cannot determine source filename",
			lengthof(VMCALL_CHANNEL_VERSION_MISMATCH_PARAMS,
				 SourceFileName));

	physaddr = virt_to_phys(&params);
	ISSUE_IO_VMCALL(VMCALL_CHANNEL_VERSION_MISMATCH, physaddr, result);
	return result;
}

static inline unsigned int Issue_VMCALL_FATAL_BYE_BYE(void)
{
	int result = VMCALL_SUCCESS;
	U64 physaddr = 0;

	ISSUE_IO_VMCALL(VMCALL_GENERIC_SURRENDER_QUANTUM_FOREVER, physaddr,
			result);
	return result;
}

#define UIS_DAEMONIZE(nam)
void *uislib_cache_alloc(struct kmem_cache *cur_pool, char *fn, int ln);
#define UISCACHEALLOC(cur_pool) uislib_cache_alloc(cur_pool, __FILE__, __LINE__)
void uislib_cache_free(struct kmem_cache *cur_pool, void *p, char *fn, int ln);
#define UISCACHEFREE(cur_pool, p) \
	uislib_cache_free(cur_pool, p, __FILE__, __LINE__)

void uislib_enable_channel_interrupts(U32 busNo, U32 devNo,
				      int (*interrupt)(void *),
				      void *interrupt_context);
void uislib_disable_channel_interrupts(U32 busNo, U32 devNo);
void uislib_force_channel_interrupt(U32 busNo, U32 devNo);

#endif /* __UISUTILS__H__ */

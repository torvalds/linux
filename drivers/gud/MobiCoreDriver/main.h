/*
 * Header file of MobiCore Driver Kernel Module.
 *
 * Internal structures of the McDrvModule
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_MAIN_H_
#define _MC_MAIN_H_

#include <asm/pgtable.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#include "public/mc_linux.h"
/* Platform specific settings */
#include "platform.h"

#define MC_VERSION(major, minor) \
		(((major & 0x0000ffff) << 16) | (minor & 0x0000ffff))

/* Instance data for MobiCore Daemon and TLCs. */
struct mc_instance {
	/* lock for the instance */
	struct mutex lock;
	/* unique handle */
	unsigned int handle;
	bool admin;
};

/*
 * Contiguous buffer allocated to TLCs.
 * These buffers are uses as world shared memory (wsm) and shared with
 * secure world.
 * The virtual kernel address is added for a simpler search algorithm.
 */
struct mc_buffer {
	struct list_head	list;
	/* unique handle */
	unsigned int		handle;
	/* Number of references kept to this buffer */
	atomic_t		usage;
	/* virtual Kernel start address */
	void			*addr;
	/* virtual Userspace start address */
	void			*uaddr;
	/* physical start address */
	void			*phys;
	/* order of number of pages */
	unsigned int		order;
	uint32_t		len;
	struct mc_instance	*instance;
};

/* MobiCore Driver Kernel Module context data. */
struct mc_context {
	/* MobiCore MCI information */
	struct mc_buffer	mci_base;
	/* MobiCore MCP buffer */
	struct mc_mcp_buffer	*mcp;
	/* event completion */
	struct completion	isr_comp;
	/* isr event counter */
	unsigned int		evt_counter;
	atomic_t		isr_counter;
	/* ever incrementing counter */
	atomic_t		unique_counter;
	/* pointer to instance of daemon */
	struct mc_instance	*daemon_inst;
	/* pointer to instance of daemon */
	struct task_struct	*daemon;
	/* General list of contiguous buffers allocated by the kernel */
	struct list_head	cont_bufs;
	/* Lock for the list of contiguous buffers */
	struct mutex		bufs_lock;
};

struct mc_sleep_mode {
	uint16_t	SleepReq;
	uint16_t	ReadyToSleep;
};

/* MobiCore is idle. No scheduling required. */
#define SCHEDULE_IDLE		0
/* MobiCore is non idle, scheduling is required. */
#define SCHEDULE_NON_IDLE	1

/* MobiCore status flags */
struct mc_flags {
	/*
	 * Scheduling hint: if <> SCHEDULE_IDLE, MobiCore should
	 * be scheduled by the NWd
	 */
	uint32_t		schedule;
	/* State of sleep protocol */
	struct mc_sleep_mode	sleep_mode;
	/* Reserved for future use: Must not be interpreted */
	uint32_t		rfu[2];
};

/* MCP buffer structure */
struct mc_mcp_buffer {
	/* MobiCore Flags */
	struct mc_flags	flags;
	uint32_t	rfu; /* MCP message buffer - ignore */
};

unsigned int get_unique_id(void);

/* check if caller is MobiCore Daemon */
static inline bool is_daemon(struct mc_instance *instance)
{
	if (!instance)
		return false;
	return instance->admin;
}


/* Initialize a new mobicore API instance object */
struct mc_instance *mc_alloc_instance(void);
/* Release a mobicore instance object and all objects related to it */
int mc_release_instance(struct mc_instance *instance);

/*
 * mc_register_wsm_l2() - Create a L2 table from a virtual memory buffer which
 * can be vmalloc or user space virtual memory
 */
int mc_register_wsm_l2(struct mc_instance *instance,
	uint32_t buffer, uint32_t len,
	uint32_t *handle, uint32_t *phys);
/* Unregister the buffer mapped above */
int mc_unregister_wsm_l2(struct mc_instance *instance, uint32_t handle);

/* Allocate one mc_buffer of contiguous space */
int mc_get_buffer(struct mc_instance *instance,
	struct mc_buffer **buffer, unsigned long len);
/* Free the buffer allocated above */
int mc_free_buffer(struct mc_instance *instance, uint32_t handle);

/* Check if the other end of the fd owns instance */
bool mc_check_owner_fd(struct mc_instance *instance, int32_t fd);

#endif /* _MC_MAIN_H_ */

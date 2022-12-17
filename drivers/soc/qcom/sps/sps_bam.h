/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/*
 * Function and data structure declarations for SPS BAM handling.
 */


#ifndef _SPSBAM_H_
#define _SPSBAM_H_

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "spsi.h"

#define BAM_HANDLE_INVALID         0

#define to_sps_bam_dev(x) \
	container_of((x), struct sps_bam, base)

enum bam_irq {
	BAM_DEV_IRQ_RDY_TO_SLEEP = 0x00000001,
	BAM_DEV_IRQ_HRESP_ERROR = 0x00000002,
	BAM_DEV_IRQ_ERROR = 0x00000004,
	BAM_DEV_IRQ_TIMER = 0x00000010,
};

/* Pipe interrupt mask */
enum bam_pipe_irq {
	/* BAM finishes descriptor which has INT bit selected */
	BAM_PIPE_IRQ_DESC_INT = 0x00000001,
	/* Inactivity timer Expires */
	BAM_PIPE_IRQ_TIMER = 0x00000002,
	/* Wakeup peripheral (i.e. USB) */
	BAM_PIPE_IRQ_WAKE = 0x00000004,
	/* Producer - no free space for adding a descriptor */
	/* Consumer - no descriptors for processing */
	BAM_PIPE_IRQ_OUT_OF_DESC = 0x00000008,
	/* Pipe Error interrupt */
	BAM_PIPE_IRQ_ERROR = 0x00000010,
	/* End-Of-Transfer */
	BAM_PIPE_IRQ_EOT = 0x00000020,
	/* Pipe RESET unsuccessful */
	BAM_PIPE_IRQ_RST_ERROR = 0x00000040,
	/* Errorneous Hresponse by AHB MASTER */
	BAM_PIPE_IRQ_HRESP_ERROR = 0x00000080,
};

/* Halt Type */
enum bam_halt {
	BAM_HALT_OFF = 0,
	BAM_HALT_ON = 1,
};

/* Threshold values of the DMA channels */
enum bam_dma_thresh_dma {
	BAM_DMA_THRESH_512 = 0x3,
	BAM_DMA_THRESH_256 = 0x2,
	BAM_DMA_THRESH_128 = 0x1,
	BAM_DMA_THRESH_64 = 0x0,
};

/* Weight values of the DMA channels */
enum bam_dma_weight_dma {
	BAM_DMA_WEIGHT_HIGH = 7,
	BAM_DMA_WEIGHT_MED = 3,
	BAM_DMA_WEIGHT_LOW = 1,
	BAM_DMA_WEIGHT_DEFAULT = BAM_DMA_WEIGHT_LOW,
	BAM_DMA_WEIGHT_DISABLE = 0,
};


/* Invalid pipe index value */
#define SPS_BAM_PIPE_INVALID  ((u32)(-1))

/* Parameters for sps_bam_pipe_connect() */
struct sps_bam_connect_param {
	/* which end point must be initialized */
	enum sps_mode mode;

	/* OR'd connection end point options (see SPS_O defines) */
	u32 options;

	/* SETPEND/MTI interrupt generation parameters */
	u32 irq_gen_addr;
	u32 irq_gen_data;

};

/* Event registration struct */
struct sps_bam_event_reg {
	/* Client's event object handle */
	struct completion *xfer_done;
	void (*callback)(struct sps_event_notify *notify);

	/* Event trigger mode */
	enum sps_trigger mode;

	/* User pointer that will be provided in event payload data */
	void *user;

};

/* Descriptor FIFO cache entry */
struct sps_bam_desc_cache {
	struct sps_iovec iovec;
	void *user; /* User pointer registered with this transfer */
};

/* Forward declaration */
struct sps_bam;

/* System mode control */
struct sps_bam_sys_mode {
	/* Descriptor FIFO control */
	u8 *desc_buf; /* Descriptor FIFO for BAM pipe */
	u32 desc_offset; /* Next new descriptor to be written to hardware */
	u32 acked_offset; /* Next descriptor to be retired by software */

	/* Descriptor cache control (!no_queue only) */
	u8 *desc_cache; /* Software cache of descriptor FIFO contents */
	u32 cache_offset; /* Next descriptor to be cached (ack_xfers only) */

	/* User pointers associated with cached descriptors */
	void **user_ptrs;

	/* Event handling */
	struct sps_bam_event_reg event_regs[SPS_EVENT_INDEX(SPS_EVENT_MAX)];
	struct list_head events_q;

	struct sps_q_event event;	/* Temp storage for event creation */
	int no_queue;	/* Whether events are queued */
	int ack_xfers;	/* Whether client must ACK all descriptors */
	int handler_eot; /* Whether EOT handling is in progress (debug) */

	/* Statistics */
#ifdef SPS_BAM_STATISTICS
	u32 desc_wr_count;
	u32 desc_rd_count;
	u32 user_ptrs_count;
	u32 user_found;
	u32 int_flags;
	u32 eot_flags;
	u32 callback_events;
	u32 wait_events;
	u32 queued_events;
	u32 get_events;
	u32 get_iovecs;
#endif /* SPS_BAM_STATISTICS */
};

/* BAM pipe descriptor */
struct sps_pipe {
	struct list_head list;

	/* Client state */
	u32 client_state;
	struct sps_bam *bam;
	struct sps_connect connect;
	const struct sps_connection *map;

	/* Pipe parameters */
	u32 state;
	u32 pipe_index;
	u32 pipe_index_mask;
	u32 irq_mask;
	int polled;
	int hybrid;
	bool late_eot;
	u32 irq_gen_addr;
	enum sps_mode mode;
	u32 num_descs; /* Size (number of elements) of descriptor FIFO */
	u32 desc_size; /* Size (bytes) of descriptor FIFO */
	int wake_up_is_one_shot; /* Whether WAKEUP event is a one-shot or not */

	/* System mode control */
	struct sps_bam_sys_mode sys;

	bool disconnecting;
};

/* BAM device descriptor */
struct sps_bam {
	struct list_head list;

	/* BAM device properties, including connection defaults */
	struct sps_bam_props props;

	/* BAM device state */
	u32 state;
	struct mutex lock;
	void __iomem *base; /* BAM virtual base address */
	u32 version;
	spinlock_t isr_lock;
	spinlock_t connection_lock;
	unsigned long irqsave_flags;

	/* Pipe state */
	u32 pipe_active_mask;
	u32 pipe_remote_mask;
	struct sps_pipe *pipes[BAM_MAX_PIPES];
	struct list_head pipes_q;

	/* Statistics */
	u32 irq_from_disabled_pipe;
	u32 event_trigger_failures;

	void *ipc_log0;
	void *ipc_log1;
	void *ipc_log2;
	void *ipc_log3;
	void *ipc_log4;

	u32 ipc_loglevel;

	/* Desc cache pointers */
	u8 *desc_cache_pointers[BAM_MAX_PIPES];

	/* ISR behavior */
	bool no_serve_irq;
};

/**
 * BAM driver initialization
 *
 * This function initializes the BAM driver.
 *
 * @options - driver options bitflags (see SPS_OPT_*)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_driver_init(u32 options);

/**
 * BAM device initialization
 *
 * This function initializes a BAM device.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_device_init(struct sps_bam *dev);

/**
 * BAM device de-initialization
 *
 * This function de-initializes a BAM device.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_device_de_init(struct sps_bam *dev);

/**
 * BAM device reset
 *
 * This Function resets a BAM device.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_reset(struct sps_bam *dev);

/**
 * BAM device enable
 *
 * This function enables a BAM device.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_enable(struct sps_bam *dev);

/**
 * BAM device disable
 *
 * This Function disables a BAM device.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_disable(struct sps_bam *dev);

/**
 * Allocate a BAM pipe
 *
 * This function allocates a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - client-specified pipe index, or SPS_BAM_PIPE_INVALID if
 *    any available pipe is acceptable
 *
 * @return - allocated pipe index, or SPS_BAM_PIPE_INVALID on error
 *
 */
u32 sps_bam_pipe_alloc(struct sps_bam *dev, u32 pipe_index);

/**
 * Free a BAM pipe
 *
 * This function frees a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 */
void sps_bam_pipe_free(struct sps_bam *dev, u32 pipe_index);

/**
 * Establish BAM pipe connection
 *
 * This function establishes a connection for a BAM pipe (end point).
 *
 * @client - pointer to client pipe state struct
 *
 * @params - connection parameters
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_connect(struct sps_pipe *client,
			const struct sps_bam_connect_param *params);

/**
 * Disconnect a BAM pipe connection
 *
 * This function disconnects a connection for a BAM pipe (end point).
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_disconnect(struct sps_bam *dev, u32 pipe_index);

/**
 * Set BAM pipe parameters
 *
 * This function sets parameters for a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @options - bitflag options (see SPS_O_*)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_set_params(struct sps_bam *dev, u32 pipe_index, u32 options);

/**
 * Enable a BAM pipe
 *
 * This function enables a BAM pipe.  Note that this function
 *    is separate from the pipe connect function to allow proper
 *    sequencing of consumer enable followed by producer enable.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_enable(struct sps_bam *dev, u32 pipe_index);

/**
 * Disable a BAM pipe
 *
 * This function disables a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_disable(struct sps_bam *dev, u32 pipe_index);

/**
 * Register an event for a BAM pipe
 *
 * This function registers an event for a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @reg - pointer to event registration struct
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_reg_event(struct sps_bam *dev, u32 pipe_index,
			   struct sps_register_event *reg);

/**
 * Submit a transfer of a single buffer to a BAM pipe
 *
 * This function submits a transfer of a single buffer to a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @addr - physical address of buffer to transfer
 *
 * @size - number of bytes to transfer
 *
 * @user - user pointer to register for event
 *
 * @flags - descriptor flags (see SPS_IOVEC_FLAG defines)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_transfer_one(struct sps_bam *dev, u32 pipe_index, u32 addr,
			      u32 size, void *user, u32 flags);

/**
 * Submit a transfer to a BAM pipe
 *
 * This function submits a transfer to a BAM pipe.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @transfer - pointer to transfer struct
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_transfer(struct sps_bam *dev, u32 pipe_index,
			 struct sps_transfer *transfer);

/**
 * Get a BAM pipe event
 *
 * This function polls for a BAM pipe event.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @notify - pointer to event notification struct
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_get_event(struct sps_bam *dev, u32 pipe_index,
			   struct sps_event_notify *notify);

/**
 * Get processed I/O vector
 *
 * This function fetches the next processed I/O vector.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @iovec - Pointer to I/O vector struct (output).
 *   This struct will be zeroed if there are no more processed I/O vectors.
 *
 * @return 0 on success, negative value on error
 */
int sps_bam_pipe_get_iovec(struct sps_bam *dev, u32 pipe_index,
			   struct sps_iovec *iovec);

/**
 * Determine whether a BAM pipe descriptor FIFO is empty
 *
 * This function returns the empty state of a BAM pipe descriptor FIFO.
 *
 * The pipe mutex must be locked before calling this function.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @empty - pointer to client's empty status word (boolean)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_is_empty(struct sps_bam *dev, u32 pipe_index, u32 *empty);

/**
 * Get number of free slots in a BAM pipe descriptor FIFO
 *
 * This function returns the number of free slots in a BAM pipe descriptor FIFO.
 *
 * The pipe mutex must be locked before calling this function.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @count - pointer to count status
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_get_free_count(struct sps_bam *dev, u32 pipe_index, u32 *count);

/**
 * Set BAM pipe to satellite ownership
 *
 * This function sets the BAM pipe to satellite ownership.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_set_satellite(struct sps_bam *dev, u32 pipe_index);

/**
 * Perform BAM pipe timer control
 *
 * This function performs BAM pipe timer control operations.
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @timer_ctrl - Pointer to timer control specification
 *
 * @timer_result - Pointer to buffer for timer operation result.
 *    This argument can be NULL if no result is expected for the operation.
 *    If non-NULL, the current timer value will always provided.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_bam_pipe_timer_ctrl(struct sps_bam *dev, u32 pipe_index,
			    struct sps_timer_ctrl *timer_ctrl,
			    struct sps_timer_result *timer_result);


/**
 * Get the number of unused descriptors in the descriptor FIFO
 * of a pipe
 *
 * @dev - pointer to BAM device descriptor
 *
 * @pipe_index - pipe index
 *
 * @desc_num - number of unused descriptors
 *
 */
int sps_bam_pipe_get_unused_desc_num(struct sps_bam *dev, u32 pipe_index,
					u32 *desc_num);

/*
 * sps_bam_check_irq - check IRQ of a BAM device.
 * @dev - pointer to BAM device descriptor
 *
 * This function checks any pending interrupt of a BAM device.
 *
 * Return: 0 on success, negative value on error
 */
int sps_bam_check_irq(struct sps_bam *dev);

/*
 * sps_bam_enable_all_irqs - Enable all IRQs of a BAM
 * @dev - pointer to BAM device descriptor
 *
 * This function enables all irqs of a BAM and its pipes.
 *
 */
void sps_bam_enable_all_irqs(struct sps_bam *dev);

/*
 * sps_bam_disable_all_irqs - Disable all IRQs of a BAM
 * @dev - pointer to BAM device descriptor
 *
 * This function disables all irqs of a BAM and its pipes.
 *
 */
void sps_bam_disable_all_irqs(struct sps_bam *dev);

/*
 * sps_bam_pipe_pending_desc - checking pending descriptor.
 * @dev:	BAM device handle
 * @pipe_index:	pipe index
 *
 * This function checks if a pipe of a BAM has any pending descriptor.
 *
 * @return true if there is any desc pending
 */
bool sps_bam_pipe_pending_desc(struct sps_bam *dev, u32 pipe_index);

/*
 * sps_bam_pipe_inject_zlt - inject a ZLT with EOT.
 * @dev:	BAM device handle
 * @pipe_index:	pipe index
 *
 * This function injects a ZLT with EOT for a pipe of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_bam_pipe_inject_zlt(struct sps_bam *dev, u32 pipe_index);
#endif	/* _SPSBAM_H_ */

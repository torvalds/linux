/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* Bus-Access-Manager (BAM) Hardware manager functions API. */

#ifndef _BAM_H_
#define _BAM_H_

#include <linux/types.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include "spsi.h"

/* Pipe mode */
enum bam_pipe_mode {
	BAM_PIPE_MODE_BAM2BAM = 0,	/* BAM to BAM */
	BAM_PIPE_MODE_SYSTEM = 1,	/* BAM to/from System Memory */
};

/* Pipe direction */
enum bam_pipe_dir {
	/* The Pipe Reads data from data-fifo or system-memory */
	BAM_PIPE_CONSUMER = 0,
	/* The Pipe Writes data to data-fifo or system-memory */
	BAM_PIPE_PRODUCER = 1,
};

/* Stream mode Type */
enum bam_stream_mode {
	BAM_STREAM_MODE_DISABLE = 0,
	BAM_STREAM_MODE_ENABLE = 1,
};

/* NWD written Type */
enum bam_write_nwd {
	BAM_WRITE_NWD_DISABLE = 0,
	BAM_WRITE_NWD_ENABLE = 1,
};


/* Enable Type */
enum bam_enable {
	BAM_DISABLE = 0,
	BAM_ENABLE = 1,
};

/* Pipe timer mode */
enum bam_pipe_timer_mode {
	BAM_PIPE_TIMER_ONESHOT = 0,
	BAM_PIPE_TIMER_PERIODIC = 1,
};

struct transfer_descriptor {
	u32 addr;	/* Buffer physical address */
	u32 size:16;	/* Buffer size in bytes */
	u32 flags:16;	/* Flag bitmask (see SPS_IOVEC_FLAG_ #defines) */
}  __packed;

/* BAM pipe initialization parameters */
struct bam_pipe_parameters {
	u16 event_threshold;
	u32 pipe_irq_mask;
	enum bam_pipe_dir dir;
	enum bam_pipe_mode mode;
	enum bam_write_nwd write_nwd;
	phys_addr_t desc_base;	/* Physical address of descriptor FIFO */
	u32 desc_size;	/* Size (bytes) of descriptor FIFO */
	u32 lock_group;	/* The lock group this pipe belongs to */
	enum bam_stream_mode stream_mode;
	u32 ee;		/* BAM execution environment index */

	/* The following are only valid if mode is BAM2BAM */
	u32 peer_phys_addr;
	u32 peer_pipe;
	phys_addr_t data_base;	/* Physical address of data FIFO */
	u32 data_size;	/* Size (bytes) of data FIFO */
	bool dummy_peer;
};

/**
 * Initialize a BAM device
 *
 * This function initializes a BAM device.
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 *
 * @summing_threshold - summing threshold (global for all pipes)
 *
 * @irq_mask - error interrupts mask
 *
 * @version - return BAM hardware version
 *
 * @num_pipes - return number of pipes
 *
 * @options - BAM configuration options
 *
 * @return 0 on success, negative value on error
 *
 */
int bam_init(void *base,
		u32 ee,
		u16 summing_threshold,
		u32 irq_mask, u32 *version,
		u32 *num_pipes, u32 options);

/**
 * Initialize BAM device security execution environment
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 *
 * @vmid - virtual master identifier
 *
 * @pipe_mask - bit mask of pipes to assign to EE
 *
 * @return 0 on success, negative value on error
 *
 */
int bam_security_init(void *base, u32 ee, u32 vmid, u32 pipe_mask);

/**
 * Check a BAM device
 *
 * This function verifies that a BAM device is enabled and gathers
 *    the hardware configuration.
 *
 * @base - BAM virtual base address.
 *
 * @version - return BAM hardware version
 *
 * @ee - BAM execution environment index
 *
 * @num_pipes - return number of pipes
 *
 * @return 0 on success, negative value on error
 *
 */
int bam_check(void *base, u32 *version, u32 ee, u32 *num_pipes);

/**
 * Disable a BAM device
 *
 * This function disables a BAM device.
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 *
 */
void bam_exit(void *base, u32 ee);

/**
 * This function prints BAM register content
 * including TEST_BUS and PIPE register content.
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 */
void bam_output_register_content(void *base, u32 ee);


/**
 * Get BAM IRQ source and clear global IRQ status
 *
 * This function gets BAM IRQ source.
 * Clear global IRQ status if it is non-zero.
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 *
 * @mask - active pipes mask.
 *
 * @case - callback case.
 *
 * @return IRQ status
 *
 */
u32 bam_check_irq_source(void *base, u32 ee, u32 mask,
				enum sps_callback_case *cb_case);

/**
 * Set BAM global interrupts
 *
 * This function initializes a BAM device.
 *
 * @base - BAM virtual base address.
 *
 * @ee - BAM execution environment index
 *
 * @mask - error interrupts mask
 *
 * @en - Enable or Disable interrupt
 *
 */
void bam_set_global_irq(void *base, u32 ee, u32 mask, bool en);

/**
 * Initialize a BAM pipe
 *
 * This function initializes a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @param - bam pipe parameters.
 *
 * @ee - BAM execution environment index
 *
 * @return 0 on success, negative value on error
 *
 */
int bam_pipe_init(void *base, u32 pipe, struct bam_pipe_parameters *param,
					u32 ee);

/**
 * Reset the BAM pipe
 *
 * This function resets the BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @ee - BAM execution environment index
 *
 */
void bam_pipe_exit(void *base, u32 pipe, u32 ee);

/**
 * Enable a BAM pipe
 *
 * This function enables a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 */
void bam_pipe_enable(void *base, u32 pipe);

/**
 * Disable a BAM pipe
 *
 * This function disables a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 */
void bam_pipe_disable(void *base, u32 pipe);

/**
 * Get a BAM pipe enable state
 *
 * This function determines if a BAM pipe is enabled.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @return true if enabled, false if disabled
 *
 */
int bam_pipe_is_enabled(void *base, u32 pipe);

/**
 * Configure interrupt for a BAM pipe
 *
 * This function configures the interrupt for a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @irq_en - enable or disable interrupt
 *
 * @src_mask - interrupt source mask, set regardless of whether
 *    interrupt is disabled
 *
 * @ee - BAM execution environment index
 *
 */
void bam_pipe_set_irq(void *base, u32 pipe, enum bam_enable irq_en,
		      u32 src_mask, u32 ee);

/**
 * Configure a BAM pipe for satellite MTI use
 *
 * This function configures a BAM pipe for satellite MTI use.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @irq_gen_addr - physical address written to generate MTI
 *
 * @ee - BAM execution environment index
 *
 */
void bam_pipe_satellite_mti(void *base, u32 pipe, u32 irq_gen_addr, u32 ee);

/**
 * Configure MTI for a BAM pipe
 *
 * This function configures the interrupt for a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @irq_en - enable or disable interrupt
 *
 * @src_mask - interrupt source mask, set regardless of whether
 *    interrupt is disabled
 *
 * @irq_gen_addr - physical address written to generate MTI
 *
 */
void bam_pipe_set_mti(void *base, u32 pipe, enum bam_enable irq_en,
		      u32 src_mask, u32 irq_gen_addr);

/**
 * Get and Clear BAM pipe IRQ status
 *
 * This function gets and clears BAM pipe IRQ status.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @return IRQ status
 *
 */
u32 bam_pipe_get_and_clear_irq_status(void *base, u32 pipe);

/**
 * Set write offset for a BAM pipe
 *
 * This function sets the write offset for a BAM pipe.  This is
 *    the offset that is maintained by software in system mode.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @next_write - descriptor FIFO write offset
 *
 */
void bam_pipe_set_desc_write_offset(void *base, u32 pipe, u32 next_write);

/**
 * Get write offset for a BAM pipe
 *
 * This function gets the write offset for a BAM pipe.  This is
 *    the offset that is maintained by the pipe's peer pipe or by software.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @return descriptor FIFO write offset
 *
 */
u32 bam_pipe_get_desc_write_offset(void *base, u32 pipe);

/**
 * Get read offset for a BAM pipe
 *
 * This function gets the read offset for a BAM pipe.  This is
 *    the offset that is maintained by the pipe in system mode.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @return descriptor FIFO read offset
 *
 */
u32 bam_pipe_get_desc_read_offset(void *base, u32 pipe);

/**
 * Configure inactivity timer count for a BAM pipe
 *
 * This function configures the inactivity timer count for a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @mode - timer operating mode
 *
 * @timeout_count - timeout count
 *
 */
void bam_pipe_timer_config(void *base, u32 pipe,
			   enum bam_pipe_timer_mode mode,
			   u32 timeout_count);

/**
 * Reset inactivity timer for a BAM pipe
 *
 * This function resets the inactivity timer count for a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 */
void bam_pipe_timer_reset(void *base, u32 pipe);

/**
 * Get inactivity timer count for a BAM pipe
 *
 * This function gets the inactivity timer count for a BAM pipe.
 *
 * @base - BAM virtual base address.
 *
 * @pipe - pipe index
 *
 * @return inactivity timer count
 *
 */
u32 bam_pipe_timer_get_count(void *base, u32 pipe);

/*
 * bam_pipe_check_zlt - Check if the last desc is ZLT.
 * @base:	BAM virtual address
 * @pipe:	pipe index
 *
 * This function checks if the last desc in the desc FIFO is a ZLT desc.
 *
 * @return true if the last desc in the desc FIFO is a ZLT desc. Otherwise
 *  return false.
 */
bool bam_pipe_check_zlt(void *base, u32 pipe);

/*
 * bam_pipe_check_pipe_empty - Check if desc FIFO is empty.
 * @base:	BAM virtual address
 * @pipe:	pipe index
 *
 * This function checks if the desc FIFO of this pipe is empty.
 *
 * @return true if desc FIFO is empty. Otherwise return false.
 */
bool bam_pipe_check_pipe_empty(void *base, u32 pipe);
#endif				/* _BAM_H_ */

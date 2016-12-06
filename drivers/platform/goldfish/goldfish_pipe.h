/*
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef GOLDFISH_PIPE_H
#define GOLDFISH_PIPE_H

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/goldfish.h>
#include <linux/mm.h>
#include <linux/acpi.h>


/* Initialize the legacy version of the pipe device driver */
int goldfish_pipe_device_init_v1(struct platform_device *pdev);

/* Deinitialize the legacy version of the pipe device driver */
void goldfish_pipe_device_deinit_v1(struct platform_device *pdev);

/* Forward declarations for the device struct */
struct goldfish_pipe;
struct goldfish_pipe_device_buffers;

/* The global driver data. Holds a reference to the i/o page used to
 * communicate with the emulator, and a wake queue for blocked tasks
 * waiting to be awoken.
 */
struct goldfish_pipe_dev {
	/*
	 * Global device spinlock. Protects the following members:
	 *  - pipes, pipes_capacity
	 *  - [*pipes, *pipes + pipes_capacity) - array data
	 *  - first_signalled_pipe,
	 *      goldfish_pipe::prev_signalled,
	 *      goldfish_pipe::next_signalled,
	 *      goldfish_pipe::signalled_flags - all singnalled-related fields,
	 *                                       in all allocated pipes
	 *  - open_command_params - PIPE_CMD_OPEN-related buffers
	 *
	 * It looks like a lot of different fields, but the trick is that the only
	 * operation that happens often is the signalled pipes array manipulation.
	 * That's why it's OK for now to keep the rest of the fields under the same
	 * lock. If we notice too much contention because of PIPE_CMD_OPEN,
	 * then we should add a separate lock there.
	 */
	spinlock_t lock;

	/*
	 * Array of the pipes of |pipes_capacity| elements,
	 * indexed by goldfish_pipe::id
	 */
	struct goldfish_pipe **pipes;
	u32 pipes_capacity;

	/* Pointers to the buffers host uses for interaction with this driver */
	struct goldfish_pipe_dev_buffers *buffers;

	/* Head of a doubly linked list of signalled pipes */
	struct goldfish_pipe *first_signalled_pipe;

	/* Some device-specific data */
	int irq;
	int version;
	unsigned char __iomem *base;

	/* v1-specific access parameters */
	struct access_params *aps;
};

extern struct goldfish_pipe_dev pipe_dev[1];

#endif /* GOLDFISH_PIPE_H */

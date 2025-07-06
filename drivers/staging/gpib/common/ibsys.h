/* SPDX-License-Identifier: GPL-2.0 */

#include "gpibP.h"

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/timer.h>

#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <asm/dma.h>

#define MAX_GPIB_PRIMARY_ADDRESS 30
#define MAX_GPIB_SECONDARY_ADDRESS 31

int gpib_allocate_board(struct gpib_board *board);
void gpib_deallocate_board(struct gpib_board *board);

unsigned int num_status_bytes(const struct gpib_status_queue *dev);
int push_status_byte(struct gpib_board *board, struct gpib_status_queue *device,
		     u8 poll_byte);
int pop_status_byte(struct gpib_board *board, struct gpib_status_queue *device,
		    u8 *poll_byte);
struct gpib_status_queue *get_gpib_status_queue(struct gpib_board *board,
						unsigned int pad, int sad);
int get_serial_poll_byte(struct gpib_board *board, unsigned int pad, int sad,
			 unsigned int usec_timeout, u8 *poll_byte);
int autopoll_all_devices(struct gpib_board *board);

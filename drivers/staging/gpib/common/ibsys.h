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

int gpib_allocate_board(gpib_board_t *board);
void gpib_deallocate_board(gpib_board_t *board);

unsigned int num_status_bytes(const gpib_status_queue_t *dev);
int push_status_byte(gpib_board_t *board, gpib_status_queue_t *device, uint8_t poll_byte);
int pop_status_byte(gpib_board_t *board, gpib_status_queue_t *device, uint8_t *poll_byte);
gpib_status_queue_t *get_gpib_status_queue(gpib_board_t *board, unsigned int pad, int sad);
int get_serial_poll_byte(gpib_board_t *board, unsigned int pad, int sad,
			 unsigned int usec_timeout, uint8_t *poll_byte);
int autopoll_all_devices(gpib_board_t *board);

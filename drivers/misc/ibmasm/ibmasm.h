
/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/input.h>

/* Driver identification */
#define DRIVER_NAME	"ibmasm"
#define DRIVER_VERSION  "1.0"
#define DRIVER_AUTHOR   "Max Asbock <masbock@us.ibm.com>, Vernon Mauery <vernux@us.ibm.com>"
#define DRIVER_DESC     "IBM ASM Service Processor Driver"

#define err(msg) printk(KERN_ERR "%s: " msg "\n", DRIVER_NAME)
#define info(msg) printk(KERN_INFO "%s: " msg "\n", DRIVER_NAME)

extern int ibmasm_debug;
#define dbg(STR, ARGS...)					\
	do {							\
		if (ibmasm_debug)				\
			printk(KERN_DEBUG STR , ##ARGS);	\
	} while (0)

static inline char *get_timestamp(char *buf)
{
	struct timeval now;
	do_gettimeofday(&now);
	sprintf(buf, "%lu.%lu", now.tv_sec, now.tv_usec);
	return buf;
}

#define IBMASM_CMD_PENDING	0
#define IBMASM_CMD_COMPLETE	1
#define IBMASM_CMD_FAILED	2

#define IBMASM_CMD_TIMEOUT_NORMAL	45
#define IBMASM_CMD_TIMEOUT_EXTRA	240

#define IBMASM_CMD_MAX_BUFFER_SIZE	0x8000

#define REVERSE_HEARTBEAT_TIMEOUT	120

#define HEARTBEAT_BUFFER_SIZE		0x400

#ifdef IA64
#define IBMASM_DRIVER_VPD "Lin64 6.08      "
#else
#define IBMASM_DRIVER_VPD "Lin32 6.08      "
#endif

#define SYSTEM_STATE_OS_UP      5
#define SYSTEM_STATE_OS_DOWN    4

#define IBMASM_NAME_SIZE	16

#define IBMASM_NUM_EVENTS	10
#define IBMASM_EVENT_MAX_SIZE	2048u


struct command {
	struct list_head	queue_node;
	wait_queue_head_t	wait;
	unsigned char		*buffer;
	size_t			buffer_size;
	int			status;
	struct kobject		kobj;
	spinlock_t		*lock;
};
#define to_command(c) container_of(c, struct command, kobj)

static inline void command_put(struct command *cmd)
{
	unsigned long flags;
	spinlock_t *lock = cmd->lock;

	spin_lock_irqsave(lock, flags);
	kobject_put(&cmd->kobj);
	spin_unlock_irqrestore(lock, flags);
}

static inline void command_get(struct command *cmd)
{
	kobject_get(&cmd->kobj);
}


struct ibmasm_event {
	unsigned int	serial_number;
	unsigned int	data_size;
	unsigned char	data[IBMASM_EVENT_MAX_SIZE];
};

struct event_buffer {
	struct ibmasm_event	events[IBMASM_NUM_EVENTS];
	unsigned int		next_serial_number;
	unsigned int		next_index;
	struct list_head	readers;
};

struct event_reader {
	int			cancelled;
	unsigned int		next_serial_number;
	wait_queue_head_t	wait;
	struct list_head	node;
	unsigned int		data_size;
	unsigned char		data[IBMASM_EVENT_MAX_SIZE];
};

struct reverse_heartbeat {
	wait_queue_head_t	wait;
	unsigned int		stopped;
};

struct ibmasm_remote {
	struct input_dev *keybd_dev;
	struct input_dev *mouse_dev;
};

struct service_processor {
	struct list_head	node;
	spinlock_t		lock;
	void __iomem		*base_address;
	unsigned int		irq;
	struct command		*current_command;
	struct command		*heartbeat;
	struct list_head	command_queue;
	struct event_buffer	*event_buffer;
	char			dirname[IBMASM_NAME_SIZE];
	char			devname[IBMASM_NAME_SIZE];
	unsigned int		number;
	struct ibmasm_remote	remote;
	int			serial_line;
	struct device		*dev;
};

/* command processing */
struct command *ibmasm_new_command(struct service_processor *sp, size_t buffer_size);
void ibmasm_exec_command(struct service_processor *sp, struct command *cmd);
void ibmasm_wait_for_response(struct command *cmd, int timeout);
void ibmasm_receive_command_response(struct service_processor *sp, void *response,  size_t size);

/* event processing */
int ibmasm_event_buffer_init(struct service_processor *sp);
void ibmasm_event_buffer_exit(struct service_processor *sp);
void ibmasm_receive_event(struct service_processor *sp, void *data,  unsigned int data_size);
void ibmasm_event_reader_register(struct service_processor *sp, struct event_reader *reader);
void ibmasm_event_reader_unregister(struct service_processor *sp, struct event_reader *reader);
int ibmasm_get_next_event(struct service_processor *sp, struct event_reader *reader);
void ibmasm_cancel_next_event(struct event_reader *reader);

/* heartbeat - from SP to OS */
void ibmasm_register_panic_notifier(void);
void ibmasm_unregister_panic_notifier(void);
int ibmasm_heartbeat_init(struct service_processor *sp);
void ibmasm_heartbeat_exit(struct service_processor *sp);
void ibmasm_receive_heartbeat(struct service_processor *sp,  void *message, size_t size);

/* reverse heartbeat - from OS to SP */
void ibmasm_init_reverse_heartbeat(struct service_processor *sp, struct reverse_heartbeat *rhb);
int ibmasm_start_reverse_heartbeat(struct service_processor *sp, struct reverse_heartbeat *rhb);
void ibmasm_stop_reverse_heartbeat(struct reverse_heartbeat *rhb);

/* dot commands */
void ibmasm_receive_message(struct service_processor *sp, void *data, int data_size);
int ibmasm_send_driver_vpd(struct service_processor *sp);
int ibmasm_send_os_state(struct service_processor *sp, int os_state);

/* low level message processing */
int ibmasm_send_i2o_message(struct service_processor *sp);
irqreturn_t ibmasm_interrupt_handler(int irq, void * dev_id);

/* remote console */
void ibmasm_handle_mouse_interrupt(struct service_processor *sp);
int ibmasm_init_remote_input_dev(struct service_processor *sp);
void ibmasm_free_remote_input_dev(struct service_processor *sp);

/* file system */
int ibmasmfs_register(void);
void ibmasmfs_unregister(void);
void ibmasmfs_add_sp(struct service_processor *sp);

/* uart */
#ifdef CONFIG_SERIAL_8250
void ibmasm_register_uart(struct service_processor *sp);
void ibmasm_unregister_uart(struct service_processor *sp);
#else
#define ibmasm_register_uart(sp)	do { } while(0)
#define ibmasm_unregister_uart(sp)	do { } while(0)
#endif

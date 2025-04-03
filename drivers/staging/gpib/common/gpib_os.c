// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *    copyright            : (C) 2001, 2004 by Frank Mori Hess
 ***************************************************************************
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt

#include "ibsys.h"
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/fcntl.h>
#include <linux/kmod.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB base support");
MODULE_ALIAS_CHARDEV_MAJOR(GPIB_CODE);

static int board_type_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board, unsigned long arg);
static int read_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
		      unsigned long arg);
static int write_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
		       unsigned long arg);
static int command_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
			 unsigned long arg);
static int open_dev_ioctl(struct file *filep, struct gpib_board *board, unsigned long arg);
static int close_dev_ioctl(struct file *filep, struct gpib_board *board, unsigned long arg);
static int serial_poll_ioctl(struct gpib_board *board, unsigned long arg);
static int wait_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board, unsigned long arg);
static int parallel_poll_ioctl(struct gpib_board *board, unsigned long arg);
static int online_ioctl(struct gpib_board *board, unsigned long arg);
static int remote_enable_ioctl(struct gpib_board *board, unsigned long arg);
static int take_control_ioctl(struct gpib_board *board, unsigned long arg);
static int line_status_ioctl(struct gpib_board *board, unsigned long arg);
static int pad_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		     unsigned long arg);
static int sad_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		     unsigned long arg);
static int eos_ioctl(struct gpib_board *board, unsigned long arg);
static int request_service_ioctl(struct gpib_board *board, unsigned long arg);
static int request_service2_ioctl(struct gpib_board *board, unsigned long arg);
static int iobase_ioctl(gpib_board_config_t *config, unsigned long arg);
static int irq_ioctl(gpib_board_config_t *config, unsigned long arg);
static int dma_ioctl(gpib_board_config_t *config, unsigned long arg);
static int autospoll_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
			   unsigned long arg);
static int mutex_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		       unsigned long arg);
static int timeout_ioctl(struct gpib_board *board, unsigned long arg);
static int status_bytes_ioctl(struct gpib_board *board, unsigned long arg);
static int board_info_ioctl(const struct gpib_board *board, unsigned long arg);
static int ppc_ioctl(struct gpib_board *board, unsigned long arg);
static int set_local_ppoll_mode_ioctl(struct gpib_board *board, unsigned long arg);
static int get_local_ppoll_mode_ioctl(struct gpib_board *board, unsigned long arg);
static int query_board_rsv_ioctl(struct gpib_board *board, unsigned long arg);
static int interface_clear_ioctl(struct gpib_board *board, unsigned long arg);
static int select_pci_ioctl(gpib_board_config_t *config, unsigned long arg);
static int select_device_path_ioctl(gpib_board_config_t *config, unsigned long arg);
static int event_ioctl(struct gpib_board *board, unsigned long arg);
static int request_system_control_ioctl(struct gpib_board *board, unsigned long arg);
static int t1_delay_ioctl(struct gpib_board *board, unsigned long arg);

static int cleanup_open_devices(gpib_file_private_t *file_priv, struct gpib_board *board);

static int pop_gpib_event_nolock(struct gpib_board *board, gpib_event_queue_t *queue, short *event_type);

/*
 * Timer functions
 */

/* Watchdog timeout routine */

static void watchdog_timeout(struct timer_list *t)
{
	struct gpib_board *board = from_timer(board, t, timer);

	set_bit(TIMO_NUM, &board->status);
	wake_up_interruptible(&board->wait);
}

/* install timer interrupt handler */
void os_start_timer(struct gpib_board *board, unsigned int usec_timeout)
/* Starts the timeout task  */
{
	if (timer_pending(&board->timer)) {
		dev_err(board->gpib_dev, "bug! timer already running?\n");
		return;
	}
	clear_bit(TIMO_NUM, &board->status);

	if (usec_timeout > 0) {
		board->timer.function = watchdog_timeout;
		/* set number of ticks */
		mod_timer(&board->timer, jiffies + usec_to_jiffies(usec_timeout));
	}
}

void os_remove_timer(struct gpib_board *board)
/* Removes the timeout task */
{
	if (timer_pending(&board->timer))
		del_timer_sync(&board->timer);
}

int io_timed_out(struct gpib_board *board)
{
	if (test_bit(TIMO_NUM, &board->status))
		return 1;
	return 0;
}

/* this is a function instead of a constant because of Suse
 * defining HZ to be a function call to get_hz()
 */
static inline int pseudo_irq_period(void)
{
	return (HZ + 99) / 100;
}

static void pseudo_irq_handler(struct timer_list *t)
{
	struct gpib_pseudo_irq *pseudo_irq = from_timer(pseudo_irq, t, timer);

	if (pseudo_irq->handler)
		pseudo_irq->handler(0, pseudo_irq->board);
	else
		pr_err("gpib: bug! pseudo_irq.handler is NULL\n");

	if (atomic_read(&pseudo_irq->active))
		mod_timer(&pseudo_irq->timer, jiffies + pseudo_irq_period());
}

int gpib_request_pseudo_irq(struct gpib_board *board, irqreturn_t (*handler)(int, void *))
{
	if (timer_pending(&board->pseudo_irq.timer) || board->pseudo_irq.handler) {
		dev_err(board->gpib_dev, "only one pseudo interrupt per board allowed\n");
		return -1;
	}

	board->pseudo_irq.handler = handler;
	board->pseudo_irq.timer.function = pseudo_irq_handler;
	board->pseudo_irq.board = board;

	atomic_set(&board->pseudo_irq.active, 1);

	mod_timer(&board->pseudo_irq.timer, jiffies + pseudo_irq_period());

	return 0;
}
EXPORT_SYMBOL(gpib_request_pseudo_irq);

void gpib_free_pseudo_irq(struct gpib_board *board)
{
	atomic_set(&board->pseudo_irq.active, 0);

	del_timer_sync(&board->pseudo_irq.timer);
	board->pseudo_irq.handler = NULL;
}
EXPORT_SYMBOL(gpib_free_pseudo_irq);

static const unsigned int serial_timeout = 1000000;

unsigned int num_status_bytes(const gpib_status_queue_t *dev)
{
	if (!dev)
		return 0;
	return dev->num_status_bytes;
}

// push status byte onto back of status byte fifo
int push_status_byte(struct gpib_board *board, gpib_status_queue_t *device, u8 poll_byte)
{
	struct list_head *head = &device->status_bytes;
	status_byte_t *status;
	static const unsigned int max_num_status_bytes = 1024;
	int retval;

	if (num_status_bytes(device) >= max_num_status_bytes) {
		u8 lost_byte;

		device->dropped_byte = 1;
		retval = pop_status_byte(board, device, &lost_byte);
		if (retval < 0)
			return retval;
	}

	status = kmalloc(sizeof(status_byte_t), GFP_KERNEL);
	if (!status)
		return -ENOMEM;

	INIT_LIST_HEAD(&status->list);
	status->poll_byte = poll_byte;

	list_add_tail(&status->list, head);

	device->num_status_bytes++;

	dev_dbg(board->gpib_dev, "pushed status byte 0x%x, %i in queue\n",
		(int)poll_byte, num_status_bytes(device));

	return 0;
}

// pop status byte from front of status byte fifo
int pop_status_byte(struct gpib_board *board, gpib_status_queue_t *device, u8 *poll_byte)
{
	struct list_head *head = &device->status_bytes;
	struct list_head *front = head->next;
	status_byte_t *status;

	if (num_status_bytes(device) == 0)
		return -EIO;

	if (front == head)
		return -EIO;

	if (device->dropped_byte) {
		device->dropped_byte = 0;
		return -EPIPE;
	}

	status = list_entry(front, status_byte_t, list);
	*poll_byte = status->poll_byte;

	list_del(front);
	kfree(status);

	device->num_status_bytes--;

	dev_dbg(board->gpib_dev, "popped status byte 0x%x, %i in queue\n",
		(int)*poll_byte, num_status_bytes(device));

	return 0;
}

gpib_status_queue_t *get_gpib_status_queue(struct gpib_board *board, unsigned int pad, int sad)
{
	gpib_status_queue_t *device;
	struct list_head *list_ptr;
	const struct list_head *head = &board->device_list;

	for (list_ptr = head->next; list_ptr != head; list_ptr = list_ptr->next) {
		device = list_entry(list_ptr, gpib_status_queue_t, list);
		if (gpib_address_equal(device->pad, device->sad, pad, sad))
			return device;
	}

	return NULL;
}

int get_serial_poll_byte(struct gpib_board *board, unsigned int pad, int sad, unsigned int usec_timeout,
			 uint8_t *poll_byte)
{
	gpib_status_queue_t *device;

	device = get_gpib_status_queue(board, pad, sad);
	if (num_status_bytes(device))
		return pop_status_byte(board, device, poll_byte);
	else
		return dvrsp(board, pad, sad, usec_timeout, poll_byte);
}

int autopoll_all_devices(struct gpib_board *board)
{
	int retval;

	if (mutex_lock_interruptible(&board->user_mutex))
		return -ERESTARTSYS;
	if (mutex_lock_interruptible(&board->big_gpib_mutex)) {
		mutex_unlock(&board->user_mutex);
		return -ERESTARTSYS;
	}

	dev_dbg(board->gpib_dev, "autopoll has board lock\n");

	retval = serial_poll_all(board, serial_timeout);
	if (retval < 0)	{
		mutex_unlock(&board->big_gpib_mutex);
		mutex_unlock(&board->user_mutex);
		return retval;
	}

	dev_dbg(board->gpib_dev, "complete\n");
	/* need to wake wait queue in case someone is
	 * waiting on RQS
	 */
	wake_up_interruptible(&board->wait);
	mutex_unlock(&board->big_gpib_mutex);
	mutex_unlock(&board->user_mutex);

	return retval;
}

static int setup_serial_poll(struct gpib_board *board, unsigned int usec_timeout)
{
	u8 cmd_string[8];
	int i;
	size_t bytes_written;
	int ret;

	os_start_timer(board, usec_timeout);
	ret = ibcac(board, 1, 1);
	if (ret < 0) {
		os_remove_timer(board);
		return ret;
	}

	i = 0;
	cmd_string[i++] = UNL;
	cmd_string[i++] = MLA(board->pad);	/* controller's listen address */
	if (board->sad >= 0)
		cmd_string[i++] = MSA(board->sad);
	cmd_string[i++] = SPE;	//serial poll enable

	ret = board->interface->command(board, cmd_string, i, &bytes_written);
	if (ret < 0 || bytes_written < i) {
		dev_dbg(board->gpib_dev, "failed to setup serial poll\n");
		os_remove_timer(board);
		return -EIO;
	}
	os_remove_timer(board);

	return 0;
}

static int read_serial_poll_byte(struct gpib_board *board, unsigned int pad,
				 int sad, unsigned int usec_timeout, uint8_t *result)
{
	u8 cmd_string[8];
	int end_flag;
	int ret;
	int i;
	size_t nbytes;

	dev_dbg(board->gpib_dev, "entering  pad=%i sad=%i\n", pad, sad);

	os_start_timer(board, usec_timeout);
	ret = ibcac(board, 1, 1);
	if (ret < 0) {
		os_remove_timer(board);
		return ret;
	}

	i = 0;
	// send talk address
	cmd_string[i++] = MTA(pad);
	if (sad >= 0)
		cmd_string[i++] = MSA(sad);

	ret = board->interface->command(board, cmd_string, i, &nbytes);
	if (ret < 0 || nbytes < i) {
		dev_err(board->gpib_dev, "failed to setup serial poll\n");
		os_remove_timer(board);
		return -EIO;
	}

	ibgts(board);

	// read poll result
	ret = board->interface->read(board, result, 1, &end_flag, &nbytes);
	if (ret < 0 || nbytes < 1) {
		dev_err(board->gpib_dev, "serial poll failed\n");
		os_remove_timer(board);
		return -EIO;
	}
	os_remove_timer(board);

	return 0;
}

static int cleanup_serial_poll(struct gpib_board *board, unsigned int usec_timeout)
{
	u8 cmd_string[8];
	int ret;
	size_t bytes_written;

	os_start_timer(board, usec_timeout);
	ret = ibcac(board, 1, 1);
	if (ret < 0) {
		os_remove_timer(board);
		return ret;
	}

	cmd_string[0] = SPD;	/* disable serial poll bytes */
	cmd_string[1] = UNT;
	ret = board->interface->command(board, cmd_string, 2, &bytes_written);
	if (ret < 0 || bytes_written < 2) {
		dev_err(board->gpib_dev, "failed to disable serial poll\n");
		os_remove_timer(board);
		return -EIO;
	}
	os_remove_timer(board);

	return 0;
}

static int serial_poll_single(struct gpib_board *board, unsigned int pad, int sad,
			      unsigned int usec_timeout, uint8_t *result)
{
	int retval, cleanup_retval;

	retval = setup_serial_poll(board, usec_timeout);
	if (retval < 0)
		return retval;
	retval = read_serial_poll_byte(board, pad, sad, usec_timeout, result);
	cleanup_retval = cleanup_serial_poll(board, usec_timeout);
	if (retval < 0)
		return retval;
	if (cleanup_retval < 0)
		return retval;

	return 0;
}

int serial_poll_all(struct gpib_board *board, unsigned int usec_timeout)
{
	int retval = 0;
	struct list_head *cur;
	const struct list_head *head = NULL;
	gpib_status_queue_t *device;
	u8 result;
	unsigned int num_bytes = 0;

	head = &board->device_list;
	if (head->next == head)
		return 0;

	retval = setup_serial_poll(board, usec_timeout);
	if (retval < 0)
		return retval;

	for (cur = head->next; cur != head; cur = cur->next) {
		device = list_entry(cur, gpib_status_queue_t, list);
		retval = read_serial_poll_byte(board,
					       device->pad, device->sad, usec_timeout, &result);
		if (retval < 0)
			continue;
		if (result & request_service_bit) {
			retval = push_status_byte(board, device, result);
			if (retval < 0)
				continue;
			num_bytes++;
		}
	}

	retval = cleanup_serial_poll(board, usec_timeout);
	if (retval < 0)
		return retval;

	return num_bytes;
}

/*
 * DVRSP
 * This function performs a serial poll of the device with primary
 * address pad and secondary address sad. If the device has no
 * secondary address, pass a negative number in for this argument.  At the
 * end of a successful serial poll the response is returned in result.
 * SPD and UNT are sent at the completion of the poll.
 */

int dvrsp(struct gpib_board *board, unsigned int pad, int sad,
	  unsigned int usec_timeout, uint8_t *result)
{
	int status = ibstatus(board);
	int retval;

	if ((status & CIC) == 0) {
		dev_err(board->gpib_dev, "not CIC during serial poll\n");
		return -1;
	}

	if (pad > MAX_GPIB_PRIMARY_ADDRESS || sad > MAX_GPIB_SECONDARY_ADDRESS || sad < -1) {
		dev_err(board->gpib_dev, "bad address for serial poll");
		return -1;
	}

	retval = serial_poll_single(board, pad, sad, usec_timeout, result);
	if (io_timed_out(board))
		retval = -ETIMEDOUT;

	return retval;
}

static gpib_descriptor_t *handle_to_descriptor(const gpib_file_private_t *file_priv,
					       int handle)
{
	if (handle < 0 || handle >= GPIB_MAX_NUM_DESCRIPTORS) {
		pr_err("gpib: invalid handle %i\n", handle);
		return NULL;
	}

	return file_priv->descriptors[handle];
}

static int init_gpib_file_private(gpib_file_private_t *priv)
{
	memset(priv, 0, sizeof(*priv));
	atomic_set(&priv->holding_mutex, 0);
	priv->descriptors[0] = kmalloc(sizeof(gpib_descriptor_t), GFP_KERNEL);
	if (!priv->descriptors[0]) {
		pr_err("gpib: failed to allocate default board descriptor\n");
		return -ENOMEM;
	}
	init_gpib_descriptor(priv->descriptors[0]);
	priv->descriptors[0]->is_board = 1;
	mutex_init(&priv->descriptors_mutex);
	return 0;
}

int ibopen(struct inode *inode, struct file *filep)
{
	unsigned int minor = iminor(inode);
	struct gpib_board *board;
	gpib_file_private_t *priv;

	if (minor >= GPIB_MAX_NUM_BOARDS) {
		pr_err("gpib: invalid minor number of device file\n");
		return -ENXIO;
	}

	board = &board_array[minor];

	filep->private_data = kmalloc(sizeof(gpib_file_private_t), GFP_KERNEL);
	if (!filep->private_data)
		return -ENOMEM;

	priv = filep->private_data;
	init_gpib_file_private((gpib_file_private_t *)filep->private_data);

	if (board->use_count == 0) {
		int retval;

		retval = request_module("gpib%i", minor);
		if (retval)
			dev_dbg(board->gpib_dev, "request module returned %i\n", retval);
	}
	if (board->interface) {
		if (!try_module_get(board->provider_module)) {
			dev_err(board->gpib_dev, "try_module_get() failed\n");
			return -EIO;
		}
		board->use_count++;
		priv->got_module = 1;
	}
	return 0;
}

int ibclose(struct inode *inode, struct file *filep)
{
	unsigned int minor = iminor(inode);
	struct gpib_board *board;
	gpib_file_private_t *priv = filep->private_data;
	gpib_descriptor_t *desc;

	if (minor >= GPIB_MAX_NUM_BOARDS) {
		pr_err("gpib: invalid minor number of device file\n");
		return -ENODEV;
	}

	board = &board_array[minor];

	if (priv) {
		desc = handle_to_descriptor(priv, 0);
		if (desc) {
			if (desc->autopoll_enabled) {
				dev_dbg(board->gpib_dev, "decrementing autospollers\n");
				if (board->autospollers > 0)
					board->autospollers--;
				else
					dev_err(board->gpib_dev,
						"Attempt to decrement zero autospollers\n");
			}
		} else {
			dev_err(board->gpib_dev, "Unexpected null gpib_descriptor\n");
		}

		cleanup_open_devices(priv, board);

		if (atomic_read(&priv->holding_mutex))
			mutex_unlock(&board->user_mutex);

		if (priv->got_module && board->use_count) {
			module_put(board->provider_module);
			--board->use_count;
		}

		kfree(filep->private_data);
		filep->private_data = NULL;
	}

	return 0;
}

long ibioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	unsigned int minor = iminor(filep->f_path.dentry->d_inode);
	struct gpib_board *board;
	gpib_file_private_t *file_priv = filep->private_data;
	long retval = -ENOTTY;

	if (minor >= GPIB_MAX_NUM_BOARDS) {
		pr_err("gpib: invalid minor number of device file\n");
		return -ENODEV;
	}
	board = &board_array[minor];

	if (mutex_lock_interruptible(&board->big_gpib_mutex))
		return -ERESTARTSYS;

	dev_dbg(board->gpib_dev, "ioctl %d, interface=%s, use=%d, onl=%d\n",
		cmd & 0xff,
		board->interface ? board->interface->name : "",
		board->use_count,
		board->online);

	switch (cmd) {
	case CFCBOARDTYPE:
		retval = board_type_ioctl(file_priv, board, arg);
		goto done;
	case IBONL:
		retval = online_ioctl(board, arg);
		goto done;
	default:
		break;
	}
	if (!board->interface) {
		dev_err(board->gpib_dev, "no gpib board configured\n");
		retval = -ENODEV;
		goto done;
	}
	if (file_priv->got_module == 0)	{
		if (!try_module_get(board->provider_module)) {
			dev_err(board->gpib_dev, "try_module_get() failed\n");
			retval = -EIO;
			goto done;
		}
		file_priv->got_module = 1;
		board->use_count++;
	}
	switch (cmd) {
	case CFCBASE:
		retval = iobase_ioctl(&board->config, arg);
		goto done;
	case CFCIRQ:
		retval = irq_ioctl(&board->config, arg);
		goto done;
	case CFCDMA:
		retval = dma_ioctl(&board->config, arg);
		goto done;
	case IBAUTOSPOLL:
		retval = autospoll_ioctl(board, file_priv, arg);
		goto done;
	case IBBOARD_INFO:
		retval = board_info_ioctl(board, arg);
		goto done;
	case IBMUTEX:
		/* Need to unlock board->big_gpib_mutex before potentially locking board->user_mutex
		 *  to maintain consistent locking order
		 */
		mutex_unlock(&board->big_gpib_mutex);
		return mutex_ioctl(board, file_priv, arg);
	case IBPAD:
		retval = pad_ioctl(board, file_priv, arg);
		goto done;
	case IBSAD:
		retval = sad_ioctl(board, file_priv, arg);
		goto done;
	case IBSELECT_PCI:
		retval = select_pci_ioctl(&board->config, arg);
		goto done;
	case IBSELECT_DEVICE_PATH:
		retval = select_device_path_ioctl(&board->config, arg);
		goto done;
	default:
		break;
	}

	if (!board->online) {
		retval = -EINVAL;
		goto done;
	}

	switch (cmd) {
	case IBEVENT:
		retval = event_ioctl(board, arg);
		goto done;
	case IBCLOSEDEV:
		retval = close_dev_ioctl(filep, board, arg);
		goto done;
	case IBOPENDEV:
		retval = open_dev_ioctl(filep, board, arg);
		goto done;
	case IBSPOLL_BYTES:
		retval = status_bytes_ioctl(board, arg);
		goto done;
	case IBWAIT:
		retval = wait_ioctl(file_priv, board, arg);
		if (retval == -ERESTARTSYS)
			return retval;
		goto done;
	case IBLINES:
		retval = line_status_ioctl(board, arg);
		goto done;
	case IBLOC:
		board->interface->return_to_local(board);
		retval = 0;
		goto done;
	default:
		break;
	}

	spin_lock(&board->locking_pid_spinlock);
	if (current->pid != board->locking_pid)	{
		spin_unlock(&board->locking_pid_spinlock);
		retval = -EPERM;
		goto done;
	}
	spin_unlock(&board->locking_pid_spinlock);

	switch (cmd) {
	case IB_T1_DELAY:
		retval = t1_delay_ioctl(board, arg);
		goto done;
	case IBCAC:
		retval = take_control_ioctl(board, arg);
		goto done;
	case IBCMD:
		/* IO ioctls can take a long time, we need to unlock board->big_gpib_mutex
		 *  before we call them.
		 */
		mutex_unlock(&board->big_gpib_mutex);
		return command_ioctl(file_priv, board, arg);
	case IBEOS:
		retval = eos_ioctl(board, arg);
		goto done;
	case IBGTS:
		retval = ibgts(board);
		goto done;
	case IBPPC:
		retval = ppc_ioctl(board, arg);
		goto done;
	case IBPP2_SET:
		retval = set_local_ppoll_mode_ioctl(board, arg);
		goto done;
	case IBPP2_GET:
		retval = get_local_ppoll_mode_ioctl(board, arg);
		goto done;
	case IBQUERY_BOARD_RSV:
		retval = query_board_rsv_ioctl(board, arg);
		goto done;
	case IBRD:
		/* IO ioctls can take a long time, we need to unlock board->big_gpib_mutex
		 *  before we call them.
		 */
		mutex_unlock(&board->big_gpib_mutex);
		return read_ioctl(file_priv, board, arg);
	case IBRPP:
		retval = parallel_poll_ioctl(board, arg);
		goto done;
	case IBRSC:
		retval = request_system_control_ioctl(board, arg);
		goto done;
	case IBRSP:
		retval = serial_poll_ioctl(board, arg);
		goto done;
	case IBRSV:
		retval = request_service_ioctl(board, arg);
		goto done;
	case IBRSV2:
		retval = request_service2_ioctl(board, arg);
		goto done;
	case IBSIC:
		retval = interface_clear_ioctl(board, arg);
		goto done;
	case IBSRE:
		retval = remote_enable_ioctl(board, arg);
		goto done;
	case IBTMO:
		retval = timeout_ioctl(board, arg);
		goto done;
	case IBWRT:
		/* IO ioctls can take a long time, we need to unlock board->big_gpib_mutex
		 *  before we call them.
		 */
		mutex_unlock(&board->big_gpib_mutex);
		return write_ioctl(file_priv, board, arg);
	default:
		retval = -ENOTTY;
		goto done;
	}

done:
	mutex_unlock(&board->big_gpib_mutex);
	dev_dbg(board->gpib_dev, "ioctl done status = 0x%lx\n", board->status);
	return retval;
}

static int board_type_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board, unsigned long arg)
{
	struct list_head *list_ptr;
	board_type_ioctl_t cmd;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (board->online)
		return -EBUSY;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(board_type_ioctl_t));
	if (retval)
		return retval;

	for (list_ptr = registered_drivers.next; list_ptr != &registered_drivers;
	     list_ptr = list_ptr->next) {
		gpib_interface_list_t *entry;

		entry = list_entry(list_ptr, gpib_interface_list_t, list);
		if (strcmp(entry->interface->name, cmd.name) == 0) {
			int i;
			int had_module = file_priv->got_module;

			if (board->use_count) {
				for (i = 0; i < board->use_count; ++i)
					module_put(board->provider_module);
				board->interface = NULL;
				file_priv->got_module = 0;
			}
			board->interface = entry->interface;
			board->provider_module = entry->module;
			for (i = 0; i < board->use_count; ++i) {
				if (!try_module_get(entry->module)) {
					board->use_count = i;
					return -EIO;
				}
			}
			if (had_module == 0) {
				if (!try_module_get(entry->module))
					return -EIO;
				++board->use_count;
			}
			file_priv->got_module = 1;
			return 0;
		}
	}

	return -EINVAL;
}

static int read_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
		      unsigned long arg)
{
	read_write_ioctl_t read_cmd;
	u8 __user *userbuf;
	unsigned long remain;
	int end_flag = 0;
	int retval;
	ssize_t read_ret = 0;
	gpib_descriptor_t *desc;
	size_t nbytes;

	retval = copy_from_user(&read_cmd, (void __user *)arg, sizeof(read_cmd));
	if (retval)
		return -EFAULT;

	if (read_cmd.completed_transfer_count > read_cmd.requested_transfer_count)
		return -EINVAL;

	desc = handle_to_descriptor(file_priv, read_cmd.handle);
	if (!desc)
		return -EINVAL;

	if (WARN_ON_ONCE(sizeof(userbuf) > sizeof(read_cmd.buffer_ptr)))
		return -EFAULT;

	userbuf = (u8 __user *)(unsigned long)read_cmd.buffer_ptr;
	userbuf += read_cmd.completed_transfer_count;

	remain = read_cmd.requested_transfer_count - read_cmd.completed_transfer_count;

	/* Check write access to buffer */
	if (!access_ok(userbuf, remain))
		return -EFAULT;

	atomic_set(&desc->io_in_progress, 1);

	/* Read buffer loads till we fill the user supplied buffer */
	while (remain > 0 && end_flag == 0) {
		nbytes = 0;
		read_ret = ibrd(board, board->buffer, (board->buffer_length < remain) ?
				board->buffer_length : remain, &end_flag, &nbytes);
		if (nbytes == 0)
			break;
		retval = copy_to_user(userbuf, board->buffer, nbytes);
		if (retval) {
			retval = -EFAULT;
			break;
		}
		remain -= nbytes;
		userbuf += nbytes;
		if (read_ret < 0)
			break;
	}
	read_cmd.completed_transfer_count = read_cmd.requested_transfer_count - remain;
	read_cmd.end = end_flag;
	/* suppress errors (for example due to timeout or interruption by device clear)
	 * if all bytes got sent.  This prevents races that can occur in the various drivers
	 * if a device receives a device clear immediately after a transfer completes and
	 * the driver code wasn't careful enough to handle that case.
	 */
	if (remain == 0 || end_flag)
		read_ret = 0;
	if (retval == 0)
		retval = copy_to_user((void __user *)arg, &read_cmd, sizeof(read_cmd));

	atomic_set(&desc->io_in_progress, 0);

	wake_up_interruptible(&board->wait);
	if (retval)
		return -EFAULT;

	return read_ret;
}

static int command_ioctl(gpib_file_private_t *file_priv,
			 struct gpib_board *board, unsigned long arg)
{
	read_write_ioctl_t cmd;
	u8 __user *userbuf;
	unsigned long remain;
	int retval;
	int fault = 0;
	gpib_descriptor_t *desc;
	size_t bytes_written;
	int no_clear_io_in_prog;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	if (cmd.completed_transfer_count > cmd.requested_transfer_count)
		return -EINVAL;

	desc = handle_to_descriptor(file_priv, cmd.handle);
	if (!desc)
		return -EINVAL;

	userbuf = (u8 __user *)(unsigned long)cmd.buffer_ptr;
	userbuf += cmd.completed_transfer_count;

	no_clear_io_in_prog = cmd.end;
	cmd.end = 0;

	remain = cmd.requested_transfer_count - cmd.completed_transfer_count;

	/* Check read access to buffer */
	if (!access_ok(userbuf, remain))
		return -EFAULT;

	/* Write buffer loads till we empty the user supplied buffer.
	 *	Call drivers at least once, even if remain is zero, in
	 *	order to allow them to insure previous commands were
	 *	completely finished, in the case of a restarted ioctl.
	 */

	atomic_set(&desc->io_in_progress, 1);

	do {
		fault = copy_from_user(board->buffer, userbuf, (board->buffer_length < remain) ?
				       board->buffer_length : remain);
		if (fault) {
			retval = -EFAULT;
			bytes_written = 0;
		} else {
			retval = ibcmd(board, board->buffer, (board->buffer_length < remain) ?
				       board->buffer_length : remain, &bytes_written);
		}
		remain -= bytes_written;
		userbuf += bytes_written;
		if (retval < 0) {
			atomic_set(&desc->io_in_progress, 0);

			wake_up_interruptible(&board->wait);
			break;
		}
	} while (remain > 0);

	cmd.completed_transfer_count = cmd.requested_transfer_count - remain;

	if (fault == 0)
		fault = copy_to_user((void __user *)arg, &cmd, sizeof(cmd));

	/*
	 * no_clear_io_in_prog (cmd.end) is true when io_in_progress should
	 * not be set to zero because the cmd in progress is the address setup
	 * operation for an async read or write. This causes CMPL not to be set
	 * in general_ibstatus until the async read or write completes.
	 */
	if (!no_clear_io_in_prog || fault)
		atomic_set(&desc->io_in_progress, 0);

	wake_up_interruptible(&board->wait);
	if (fault)
		return -EFAULT;

	return retval;
}

static int write_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
		       unsigned long arg)
{
	read_write_ioctl_t write_cmd;
	u8 __user *userbuf;
	unsigned long remain;
	int retval = 0;
	int fault;
	gpib_descriptor_t *desc;

	fault = copy_from_user(&write_cmd, (void __user *)arg, sizeof(write_cmd));
	if (fault)
		return -EFAULT;

	if (write_cmd.completed_transfer_count > write_cmd.requested_transfer_count)
		return -EINVAL;

	desc = handle_to_descriptor(file_priv, write_cmd.handle);
	if (!desc)
		return -EINVAL;

	userbuf = (u8 __user *)(unsigned long)write_cmd.buffer_ptr;
	userbuf += write_cmd.completed_transfer_count;

	remain = write_cmd.requested_transfer_count - write_cmd.completed_transfer_count;

	/* Check read access to buffer */
	if (!access_ok(userbuf, remain))
		return -EFAULT;

	atomic_set(&desc->io_in_progress, 1);

	/* Write buffer loads till we empty the user supplied buffer */
	while (remain > 0) {
		int send_eoi;
		size_t bytes_written = 0;

		send_eoi = remain <= board->buffer_length && write_cmd.end;
		fault = copy_from_user(board->buffer, userbuf, (board->buffer_length < remain) ?
				       board->buffer_length : remain);
		if (fault) {
			retval = -EFAULT;
			break;
		}
		retval = ibwrt(board, board->buffer, (board->buffer_length < remain) ?
			       board->buffer_length : remain, send_eoi, &bytes_written);
		remain -= bytes_written;
		userbuf += bytes_written;
		if (retval < 0)
			break;
	}
	write_cmd.completed_transfer_count = write_cmd.requested_transfer_count - remain;
	/* suppress errors (for example due to timeout or interruption by device clear)
	 * if all bytes got sent.  This prevents races that can occur in the various drivers
	 * if a device receives a device clear immediately after a transfer completes and
	 * the driver code wasn't careful enough to handle that case.
	 */
	if (remain == 0)
		retval = 0;
	if (fault == 0)
		fault = copy_to_user((void __user *)arg, &write_cmd, sizeof(write_cmd));

	atomic_set(&desc->io_in_progress, 0);

	wake_up_interruptible(&board->wait);
	if (fault)
		return -EFAULT;

	return retval;
}

static int status_bytes_ioctl(struct gpib_board *board, unsigned long arg)
{
	gpib_status_queue_t *device;
	spoll_bytes_ioctl_t cmd;
	int retval;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	device = get_gpib_status_queue(board, cmd.pad, cmd.sad);
	if (!device)
		cmd.num_bytes = 0;
	else
		cmd.num_bytes = num_status_bytes(device);

	retval = copy_to_user((void __user *)arg, &cmd, sizeof(cmd));
	if (retval)
		return -EFAULT;

	return 0;
}

static int increment_open_device_count(struct gpib_board *board, struct list_head *head,
				       unsigned int pad, int sad)
{
	struct list_head *list_ptr;
	gpib_status_queue_t *device;

	/* first see if address has already been opened, then increment
	 * open count
	 */
	for (list_ptr = head->next; list_ptr != head; list_ptr = list_ptr->next) {
		device = list_entry(list_ptr, gpib_status_queue_t, list);
		if (gpib_address_equal(device->pad, device->sad, pad, sad)) {
			dev_dbg(board->gpib_dev, "incrementing open count for pad %i, sad %i\n",
				device->pad, device->sad);
			device->reference_count++;
			return 0;
		}
	}

	/* otherwise we need to allocate a new gpib_status_queue_t */
	device = kmalloc(sizeof(gpib_status_queue_t), GFP_ATOMIC);
	if (!device)
		return -ENOMEM;
	init_gpib_status_queue(device);
	device->pad = pad;
	device->sad = sad;
	device->reference_count = 1;

	list_add(&device->list, head);

	dev_dbg(board->gpib_dev, "opened pad %i, sad %i\n", device->pad, device->sad);

	return 0;
}

static int subtract_open_device_count(struct gpib_board *board, struct list_head *head,
				      unsigned int pad, int sad, unsigned int count)
{
	gpib_status_queue_t *device;
	struct list_head *list_ptr;

	for (list_ptr = head->next; list_ptr != head; list_ptr = list_ptr->next) {
		device = list_entry(list_ptr, gpib_status_queue_t, list);
		if (gpib_address_equal(device->pad, device->sad, pad, sad)) {
			dev_dbg(board->gpib_dev, "decrementing open count for pad %i, sad %i\n",
				device->pad, device->sad);
			if (count > device->reference_count) {
				dev_err(board->gpib_dev, "bug! in %s()\n", __func__);
				return -EINVAL;
			}
			device->reference_count -= count;
			if (device->reference_count == 0) {
				dev_dbg(board->gpib_dev, "closing pad %i, sad %i\n",
					device->pad, device->sad);
				list_del(list_ptr);
				kfree(device);
			}
			return 0;
		}
	}
	dev_err(board->gpib_dev, "bug! tried to close address that was never opened!\n");
	return -EINVAL;
}

static inline int decrement_open_device_count(struct gpib_board *board, struct list_head *head,
					      unsigned int pad, int sad)
{
	return subtract_open_device_count(board, head, pad, sad, 1);
}

static int cleanup_open_devices(gpib_file_private_t *file_priv, struct gpib_board *board)
{
	int retval = 0;
	int i;

	for (i = 0; i < GPIB_MAX_NUM_DESCRIPTORS; i++) {
		gpib_descriptor_t *desc;

		desc = file_priv->descriptors[i];
		if (!desc)
			continue;

		if (desc->is_board == 0) {
			retval = decrement_open_device_count(board, &board->device_list, desc->pad,
							     desc->sad);
			if (retval < 0)
				return retval;
		}
		kfree(desc);
		file_priv->descriptors[i] = NULL;
	}

	return 0;
}

static int open_dev_ioctl(struct file *filep, struct gpib_board *board, unsigned long arg)
{
	open_dev_ioctl_t open_dev_cmd;
	int retval;
	gpib_file_private_t *file_priv = filep->private_data;
	int i;

	retval = copy_from_user(&open_dev_cmd, (void __user *)arg, sizeof(open_dev_cmd));
	if (retval)
		return -EFAULT;

	if (mutex_lock_interruptible(&file_priv->descriptors_mutex))
		return -ERESTARTSYS;
	for (i = 0; i < GPIB_MAX_NUM_DESCRIPTORS; i++)
		if (!file_priv->descriptors[i])
			break;
	if (i == GPIB_MAX_NUM_DESCRIPTORS) {
		mutex_unlock(&file_priv->descriptors_mutex);
		return -ERANGE;
	}
	file_priv->descriptors[i] = kmalloc(sizeof(gpib_descriptor_t), GFP_KERNEL);
	if (!file_priv->descriptors[i]) {
		mutex_unlock(&file_priv->descriptors_mutex);
		return -ENOMEM;
	}
	init_gpib_descriptor(file_priv->descriptors[i]);

	file_priv->descriptors[i]->pad = open_dev_cmd.pad;
	file_priv->descriptors[i]->sad = open_dev_cmd.sad;
	file_priv->descriptors[i]->is_board = open_dev_cmd.is_board;
	mutex_unlock(&file_priv->descriptors_mutex);

	retval = increment_open_device_count(board, &board->device_list, open_dev_cmd.pad,
					     open_dev_cmd.sad);
	if (retval < 0)
		return retval;

	/* clear stuck srq state, since we may be able to find service request on
	 * the new device
	 */
	atomic_set(&board->stuck_srq, 0);

	open_dev_cmd.handle = i;
	retval = copy_to_user((void __user *)arg, &open_dev_cmd, sizeof(open_dev_cmd));
	if (retval)
		return -EFAULT;

	return 0;
}

static int close_dev_ioctl(struct file *filep, struct gpib_board *board, unsigned long arg)
{
	close_dev_ioctl_t cmd;
	gpib_file_private_t *file_priv = filep->private_data;
	int retval;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	if (cmd.handle >= GPIB_MAX_NUM_DESCRIPTORS)
		return -EINVAL;
	if (!file_priv->descriptors[cmd.handle])
		return -EINVAL;

	retval = decrement_open_device_count(board, &board->device_list,
					     file_priv->descriptors[cmd.handle]->pad,
					     file_priv->descriptors[cmd.handle]->sad);
	if (retval < 0)
		return retval;

	kfree(file_priv->descriptors[cmd.handle]);
	file_priv->descriptors[cmd.handle] = NULL;

	return 0;
}

static int serial_poll_ioctl(struct gpib_board *board, unsigned long arg)
{
	serial_poll_ioctl_t serial_cmd;
	int retval;

	retval = copy_from_user(&serial_cmd, (void __user *)arg, sizeof(serial_cmd));
	if (retval)
		return -EFAULT;

	retval = get_serial_poll_byte(board, serial_cmd.pad, serial_cmd.sad, board->usec_timeout,
				      &serial_cmd.status_byte);
	if (retval < 0)
		return retval;

	retval = copy_to_user((void __user *)arg, &serial_cmd, sizeof(serial_cmd));
	if (retval)
		return -EFAULT;

	return 0;
}

static int wait_ioctl(gpib_file_private_t *file_priv, struct gpib_board *board,
		      unsigned long arg)
{
	wait_ioctl_t wait_cmd;
	int retval;
	gpib_descriptor_t *desc;

	retval = copy_from_user(&wait_cmd, (void __user *)arg, sizeof(wait_cmd));
	if (retval)
		return -EFAULT;

	desc = handle_to_descriptor(file_priv, wait_cmd.handle);
	if (!desc)
		return -EINVAL;

	retval = ibwait(board, wait_cmd.wait_mask, wait_cmd.clear_mask,
			wait_cmd.set_mask, &wait_cmd.ibsta, wait_cmd.usec_timeout, desc);
	if (retval < 0)
		return retval;

	retval = copy_to_user((void __user *)arg, &wait_cmd, sizeof(wait_cmd));
	if (retval)
		return -EFAULT;

	return 0;
}

static int parallel_poll_ioctl(struct gpib_board *board, unsigned long arg)
{
	u8 poll_byte;
	int retval;

	retval = ibrpp(board, &poll_byte);
	if (retval < 0)
		return retval;

	retval = copy_to_user((void __user *)arg, &poll_byte, sizeof(poll_byte));
	if (retval)
		return -EFAULT;

	return 0;
}

static int online_ioctl(struct gpib_board *board, unsigned long arg)
{
	online_ioctl_t online_cmd;
	int retval;
	void __user *init_data = NULL;

	board->config.init_data = NULL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	retval = copy_from_user(&online_cmd, (void __user *)arg, sizeof(online_cmd));
	if (retval)
		return -EFAULT;
	if (online_cmd.init_data_length > 0) {
		board->config.init_data = vmalloc(online_cmd.init_data_length);
		if (!board->config.init_data)
			return -ENOMEM;
		if (WARN_ON_ONCE(sizeof(init_data) > sizeof(online_cmd.init_data_ptr)))
			return -EFAULT;
		init_data = (void __user *)(unsigned long)(online_cmd.init_data_ptr);
		retval = copy_from_user(board->config.init_data, init_data,
					online_cmd.init_data_length);
		if (retval) {
			vfree(board->config.init_data);
			return -EFAULT;
		}
		board->config.init_data_length = online_cmd.init_data_length;
	} else {
		board->config.init_data = NULL;
		board->config.init_data_length = 0;
	}
	if (online_cmd.online)
		retval = ibonline(board);
	else
		retval = iboffline(board);
	if (board->config.init_data) {
		vfree(board->config.init_data);
		board->config.init_data = NULL;
		board->config.init_data_length = 0;
	}
	return retval;
}

static int remote_enable_ioctl(struct gpib_board *board, unsigned long arg)
{
	int enable;
	int retval;

	retval = copy_from_user(&enable, (void __user *)arg, sizeof(enable));
	if (retval)
		return -EFAULT;

	return ibsre(board, enable);
}

static int take_control_ioctl(struct gpib_board *board, unsigned long arg)
{
	int synchronous;
	int retval;

	retval = copy_from_user(&synchronous, (void __user *)arg, sizeof(synchronous));
	if (retval)
		return -EFAULT;

	return ibcac(board, synchronous, 1);
}

static int line_status_ioctl(struct gpib_board *board, unsigned long arg)
{
	short lines;
	int retval;

	retval = iblines(board, &lines);
	if (retval < 0)
		return retval;

	retval = copy_to_user((void __user *)arg, &lines, sizeof(lines));
	if (retval)
		return -EFAULT;

	return 0;
}

static int pad_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		     unsigned long arg)
{
	pad_ioctl_t cmd;
	int retval;
	gpib_descriptor_t *desc;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	desc = handle_to_descriptor(file_priv, cmd.handle);
	if (!desc)
		return -EINVAL;

	if (desc->is_board) {
		retval = ibpad(board, cmd.pad);
		if (retval < 0)
			return retval;
	} else {
		retval = decrement_open_device_count(board, &board->device_list, desc->pad,
						     desc->sad);
		if (retval < 0)
			return retval;

		desc->pad = cmd.pad;

		retval = increment_open_device_count(board, &board->device_list, desc->pad,
						     desc->sad);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int sad_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		     unsigned long arg)
{
	sad_ioctl_t cmd;
	int retval;
	gpib_descriptor_t *desc;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	desc = handle_to_descriptor(file_priv, cmd.handle);
	if (!desc)
		return -EINVAL;

	if (desc->is_board) {
		retval = ibsad(board, cmd.sad);
		if (retval < 0)
			return retval;
	} else {
		retval = decrement_open_device_count(board, &board->device_list, desc->pad,
						     desc->sad);
		if (retval < 0)
			return retval;

		desc->sad = cmd.sad;

		retval = increment_open_device_count(board, &board->device_list, desc->pad,
						     desc->sad);
		if (retval < 0)
			return retval;
	}
	return 0;
}

static int eos_ioctl(struct gpib_board *board, unsigned long arg)
{
	eos_ioctl_t eos_cmd;
	int retval;

	retval = copy_from_user(&eos_cmd, (void __user *)arg, sizeof(eos_cmd));
	if (retval)
		return -EFAULT;

	return ibeos(board, eos_cmd.eos, eos_cmd.eos_flags);
}

static int request_service_ioctl(struct gpib_board *board, unsigned long arg)
{
	u8 status_byte;
	int retval;

	retval = copy_from_user(&status_byte, (void __user *)arg, sizeof(status_byte));
	if (retval)
		return -EFAULT;

	return ibrsv2(board, status_byte, status_byte & request_service_bit);
}

static int request_service2_ioctl(struct gpib_board *board, unsigned long arg)
{
	request_service2_t request_service2_cmd;
	int retval;

	retval = copy_from_user(&request_service2_cmd, (void __user *)arg,
				sizeof(request_service2_t));
	if (retval)
		return -EFAULT;

	return ibrsv2(board, request_service2_cmd.status_byte,
		      request_service2_cmd.new_reason_for_service);
}

static int iobase_ioctl(gpib_board_config_t *config, unsigned long arg)
{
	u64 base_addr;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	retval = copy_from_user(&base_addr, (void __user *)arg, sizeof(base_addr));
	if (retval)
		return -EFAULT;

	if (WARN_ON_ONCE(sizeof(void *) > sizeof(base_addr)))
		return -EFAULT;
	config->ibbase = base_addr;

	return 0;
}

static int irq_ioctl(gpib_board_config_t *config, unsigned long arg)
{
	unsigned int irq;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	retval = copy_from_user(&irq, (void __user *)arg, sizeof(irq));
	if (retval)
		return -EFAULT;

	config->ibirq = irq;

	return 0;
}

static int dma_ioctl(gpib_board_config_t *config, unsigned long arg)
{
	unsigned int dma_channel;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	retval = copy_from_user(&dma_channel, (void __user *)arg, sizeof(dma_channel));
	if (retval)
		return -EFAULT;

	config->ibdma = dma_channel;

	return 0;
}

static int autospoll_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
			   unsigned long arg)
{
	autospoll_ioctl_t enable;
	int retval;
	gpib_descriptor_t *desc;

	retval = copy_from_user(&enable, (void __user *)arg, sizeof(enable));
	if (retval)
		return -EFAULT;

	desc = handle_to_descriptor(file_priv, 0); /* board handle is 0 */

	if (enable) {
		if (!desc->autopoll_enabled) {
			board->autospollers++;
			desc->autopoll_enabled = 1;
		}
		retval = 0;
	} else {
		if (desc->autopoll_enabled) {
			desc->autopoll_enabled = 0;
			if (board->autospollers > 0) {
				board->autospollers--;
				retval = 0;
			} else {
				dev_err(board->gpib_dev,
					"tried to set number of autospollers negative\n");
				retval = -EINVAL;
			}
		} else {
			dev_err(board->gpib_dev, "autopoll disable requested before enable\n");
			retval = -EINVAL;
		}
	}
	return retval;
}

static int mutex_ioctl(struct gpib_board *board, gpib_file_private_t *file_priv,
		       unsigned long arg)
{
	int retval, lock_mutex;

	retval = copy_from_user(&lock_mutex, (void __user *)arg, sizeof(lock_mutex));
	if (retval)
		return -EFAULT;

	if (lock_mutex)	{
		retval = mutex_lock_interruptible(&board->user_mutex);
		if (retval)
			return -ERESTARTSYS;

		spin_lock(&board->locking_pid_spinlock);
		board->locking_pid = current->pid;
		spin_unlock(&board->locking_pid_spinlock);

		atomic_set(&file_priv->holding_mutex, 1);

		dev_dbg(board->gpib_dev, "locked board mutex\n");
	} else {
		spin_lock(&board->locking_pid_spinlock);
		if (current->pid != board->locking_pid) {
			dev_err(board->gpib_dev, "bug! pid %i tried to release mutex held by pid %i\n",
				current->pid, board->locking_pid);
			spin_unlock(&board->locking_pid_spinlock);
			return -EPERM;
		}
		board->locking_pid = 0;
		spin_unlock(&board->locking_pid_spinlock);

		atomic_set(&file_priv->holding_mutex, 0);

		mutex_unlock(&board->user_mutex);
		dev_dbg(board->gpib_dev, "unlocked board mutex\n");
	}
	return 0;
}

static int timeout_ioctl(struct gpib_board *board, unsigned long arg)
{
	unsigned int timeout;
	int retval;

	retval = copy_from_user(&timeout, (void __user *)arg, sizeof(timeout));
	if (retval)
		return -EFAULT;

	board->usec_timeout = timeout;
	dev_dbg(board->gpib_dev, "timeout set to %i usec\n", timeout);

	return 0;
}

static int ppc_ioctl(struct gpib_board *board, unsigned long arg)
{
	ppoll_config_ioctl_t cmd;
	int retval;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	if (cmd.set_ist) {
		board->ist = 1;
		board->interface->parallel_poll_response(board, board->ist);
	} else if (cmd.clear_ist) {
		board->ist = 0;
		board->interface->parallel_poll_response(board, board->ist);
	}

	if (cmd.config)	{
		retval = ibppc(board, cmd.config);
		if (retval < 0)
			return retval;
	}

	return 0;
}

static int set_local_ppoll_mode_ioctl(struct gpib_board *board, unsigned long arg)
{
	local_ppoll_mode_ioctl_t cmd;
	int retval;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	if (!board->interface->local_parallel_poll_mode)
		return -ENOENT;
	board->local_ppoll_mode = cmd != 0;
	board->interface->local_parallel_poll_mode(board, board->local_ppoll_mode);

	return 0;
}

static int get_local_ppoll_mode_ioctl(struct gpib_board *board, unsigned long arg)
{
	local_ppoll_mode_ioctl_t cmd;
	int retval;

	cmd = board->local_ppoll_mode;
	retval = copy_to_user((void __user *)arg, &cmd, sizeof(cmd));
	if (retval)
		return -EFAULT;

	return 0;
}

static int query_board_rsv_ioctl(struct gpib_board *board, unsigned long arg)
{
	int status;
	int retval;

	status = board->interface->serial_poll_status(board);

	retval = copy_to_user((void __user *)arg, &status, sizeof(status));
	if (retval)
		return -EFAULT;

	return 0;
}

static int board_info_ioctl(const struct gpib_board *board, unsigned long arg)
{
	board_info_ioctl_t info;
	int retval;

	info.pad = board->pad;
	info.sad = board->sad;
	info.parallel_poll_configuration = board->parallel_poll_configuration;
	info.is_system_controller = board->master;
	if (board->autospollers)
		info.autopolling = 1;
	else
		info.autopolling = 0;
	info.t1_delay = board->t1_nano_sec;
	info.ist = board->ist;
	info.no_7_bit_eos = board->interface->no_7_bit_eos;
	retval = copy_to_user((void __user *)arg, &info, sizeof(info));
	if (retval)
		return -EFAULT;

	return 0;
}

static int interface_clear_ioctl(struct gpib_board *board, unsigned long arg)
{
	unsigned int usec_duration;
	int retval;

	retval = copy_from_user(&usec_duration, (void __user *)arg, sizeof(usec_duration));
	if (retval)
		return -EFAULT;

	return ibsic(board, usec_duration);
}

static int select_pci_ioctl(gpib_board_config_t *config, unsigned long arg)
{
	select_pci_ioctl_t selection;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	retval = copy_from_user(&selection, (void __user *)arg, sizeof(selection));
	if (retval)
		return -EFAULT;

	config->pci_bus = selection.pci_bus;
	config->pci_slot = selection.pci_slot;

	return 0;
}

static int select_device_path_ioctl(gpib_board_config_t *config, unsigned long arg)
{
	select_device_path_ioctl_t *selection;
	int retval;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	selection = vmalloc(sizeof(select_device_path_ioctl_t));
	if (!selection)
		return -ENOMEM;

	retval = copy_from_user(selection, (void __user *)arg, sizeof(select_device_path_ioctl_t));
	if (retval) {
		vfree(selection);
		return -EFAULT;
	}

	selection->device_path[sizeof(selection->device_path) - 1] = '\0';
	kfree(config->device_path);
	config->device_path = NULL;
	if (strlen(selection->device_path) > 0)
		config->device_path = kstrdup(selection->device_path, GFP_KERNEL);

	vfree(selection);
	return 0;
}

unsigned int num_gpib_events(const gpib_event_queue_t *queue)
{
	return queue->num_events;
}

static int push_gpib_event_nolock(struct gpib_board *board, short event_type)
{
	gpib_event_queue_t *queue = &board->event_queue;
	struct list_head *head = &queue->event_head;
	gpib_event_t *event;
	static const unsigned int max_num_events = 1024;
	int retval;

	if (num_gpib_events(queue) >= max_num_events) {
		short lost_event;

		queue->dropped_event = 1;
		retval = pop_gpib_event_nolock(board, queue, &lost_event);
		if (retval < 0)
			return retval;
	}

	event = kmalloc(sizeof(gpib_event_t), GFP_ATOMIC);
	if (!event) {
		queue->dropped_event = 1;
		dev_err(board->gpib_dev, "failed to allocate memory for event\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&event->list);
	event->event_type = event_type;

	list_add_tail(&event->list, head);

	queue->num_events++;

	dev_dbg(board->gpib_dev, "pushed event %i, %i in queue\n",
		(int)event_type, num_gpib_events(queue));

	return 0;
}

// push event onto back of event queue
int push_gpib_event(struct gpib_board *board, short event_type)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&board->event_queue.lock, flags);
	retval = push_gpib_event_nolock(board, event_type);
	spin_unlock_irqrestore(&board->event_queue.lock, flags);

	if (event_type == EventDevTrg)
		board->status |= DTAS;
	if (event_type == EventDevClr)
		board->status |= DCAS;

	return retval;
}
EXPORT_SYMBOL(push_gpib_event);

static int pop_gpib_event_nolock(struct gpib_board *board, gpib_event_queue_t *queue, short *event_type)
{
	struct list_head *head = &queue->event_head;
	struct list_head *front = head->next;
	gpib_event_t *event;

	if (num_gpib_events(queue) == 0) {
		*event_type = EventNone;
		return 0;
	}

	if (front == head)
		return -EIO;

	if (queue->dropped_event) {
		queue->dropped_event = 0;
		return -EPIPE;
	}

	event = list_entry(front, gpib_event_t, list);
	*event_type = event->event_type;

	list_del(front);
	kfree(event);

	queue->num_events--;

	dev_dbg(board->gpib_dev, "popped event %i, %i in queue\n",
		(int)*event_type, num_gpib_events(queue));

	return 0;
}

// pop event from front of event queue
int pop_gpib_event(struct gpib_board *board, gpib_event_queue_t *queue, short *event_type)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&queue->lock, flags);
	retval = pop_gpib_event_nolock(board, queue, event_type);
	spin_unlock_irqrestore(&queue->lock, flags);
	return retval;
}

static int event_ioctl(struct gpib_board *board, unsigned long arg)
{
	event_ioctl_t user_event;
	int retval;
	short event;

	retval = pop_gpib_event(board, &board->event_queue, &event);
	if (retval < 0)
		return retval;

	user_event = event;

	retval = copy_to_user((void __user *)arg, &user_event, sizeof(user_event));
	if (retval)
		return -EFAULT;

	return 0;
}

static int request_system_control_ioctl(struct gpib_board *board, unsigned long arg)
{
	rsc_ioctl_t request_control;
	int retval;

	retval = copy_from_user(&request_control, (void __user *)arg, sizeof(request_control));
	if (retval)
		return -EFAULT;

	ibrsc(board, request_control);

	return 0;
}

static int t1_delay_ioctl(struct gpib_board *board, unsigned long arg)
{
	t1_delay_ioctl_t cmd;
	unsigned int delay;
	int retval;

	if (!board->interface->t1_delay)
		return -ENOENT;

	retval = copy_from_user(&cmd, (void __user *)arg, sizeof(cmd));
	if (retval)
		return -EFAULT;

	delay = cmd;

	retval = board->interface->t1_delay(board, delay);
	if (retval < 0)
		return retval;

	board->t1_nano_sec = retval;
	return 0;
}

static const struct file_operations ib_fops = {
	.owner = THIS_MODULE,
	.llseek = NULL,
	.unlocked_ioctl = &ibioctl,
	.compat_ioctl = &ibioctl,
	.open = &ibopen,
	.release = &ibclose,
};

struct gpib_board board_array[GPIB_MAX_NUM_BOARDS];

LIST_HEAD(registered_drivers);

void init_gpib_descriptor(gpib_descriptor_t *desc)
{
	desc->pad = 0;
	desc->sad = -1;
	desc->is_board = 0;
	desc->autopoll_enabled = 0;
	atomic_set(&desc->io_in_progress, 0);
}

int gpib_register_driver(gpib_interface_t *interface, struct module *provider_module)
{
	struct gpib_interface_list_struct *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->interface = interface;
	entry->module = provider_module;
	list_add(&entry->list, &registered_drivers);

	return 0;
}
EXPORT_SYMBOL(gpib_register_driver);

void gpib_unregister_driver(gpib_interface_t *interface)
{
	int i;
	struct list_head *list_ptr;

	for (i = 0; i < GPIB_MAX_NUM_BOARDS; i++) {
		struct gpib_board *board = &board_array[i];

		if (board->interface == interface) {
			if (board->use_count > 0)
				pr_warn("gpib: Warning: deregistered interface %s in use\n",
					interface->name);
			iboffline(board);
			board->interface = NULL;
		}
	}
	for (list_ptr = registered_drivers.next; list_ptr != &registered_drivers;) {
		gpib_interface_list_t *entry;

		entry = list_entry(list_ptr, gpib_interface_list_t, list);
		list_ptr = list_ptr->next;
		if (entry->interface == interface) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
}
EXPORT_SYMBOL(gpib_unregister_driver);

static void init_gpib_board_config(gpib_board_config_t *config)
{
	memset(config, 0, sizeof(gpib_board_config_t));
	config->pci_bus = -1;
	config->pci_slot = -1;
}

void init_gpib_board(struct gpib_board *board)
{
	board->interface = NULL;
	board->provider_module = NULL;
	board->buffer = NULL;
	board->buffer_length = 0;
	board->status = 0;
	init_waitqueue_head(&board->wait);
	mutex_init(&board->user_mutex);
	mutex_init(&board->big_gpib_mutex);
	board->locking_pid = 0;
	spin_lock_init(&board->locking_pid_spinlock);
	spin_lock_init(&board->spinlock);
	timer_setup(&board->timer, NULL, 0);
	board->dev = NULL;
	board->gpib_dev = NULL;
	init_gpib_board_config(&board->config);
	board->private_data = NULL;
	board->use_count = 0;
	INIT_LIST_HEAD(&board->device_list);
	board->pad = 0;
	board->sad = -1;
	board->usec_timeout = 3000000;
	board->parallel_poll_configuration = 0;
	board->online = 0;
	board->autospollers = 0;
	board->autospoll_task = NULL;
	init_event_queue(&board->event_queue);
	board->minor = -1;
	init_gpib_pseudo_irq(&board->pseudo_irq);
	board->master = 1;
	atomic_set(&board->stuck_srq, 0);
	board->local_ppoll_mode = 0;
}

int gpib_allocate_board(struct gpib_board *board)
{
	if (!board->buffer) {
		board->buffer_length = 0x4000;
		board->buffer = vmalloc(board->buffer_length);
		if (!board->buffer) {
			board->buffer_length = 0;
			return -ENOMEM;
		}
	}
	return 0;
}

void gpib_deallocate_board(struct gpib_board *board)
{
	short dummy;

	if (board->buffer) {
		vfree(board->buffer);
		board->buffer = NULL;
		board->buffer_length = 0;
	}
	while (num_gpib_events(&board->event_queue))
		pop_gpib_event(board, &board->event_queue, &dummy);
}

static void init_board_array(struct gpib_board *board_array, unsigned int length)
{
	int i;

	for (i = 0; i < length; i++) {
		init_gpib_board(&board_array[i]);
		board_array[i].minor = i;
	}
}

void init_gpib_status_queue(gpib_status_queue_t *device)
{
	INIT_LIST_HEAD(&device->list);
	INIT_LIST_HEAD(&device->status_bytes);
	device->num_status_bytes = 0;
	device->reference_count = 0;
	device->dropped_byte = 0;
}

static struct class *gpib_class;

static int __init gpib_common_init_module(void)
{
	int i;

	pr_info("GPIB core driver\n");
	init_board_array(board_array, GPIB_MAX_NUM_BOARDS);
	if (register_chrdev(GPIB_CODE, "gpib", &ib_fops)) {
		pr_err("gpib: can't get major %d\n", GPIB_CODE);
		return -EIO;
	}
	gpib_class = class_create("gpib_common");
	if (IS_ERR(gpib_class)) {
		pr_err("gpib: failed to create gpib class\n");
		unregister_chrdev(GPIB_CODE, "gpib");
		return PTR_ERR(gpib_class);
	}
	for (i = 0; i < GPIB_MAX_NUM_BOARDS; ++i)
		board_array[i].gpib_dev = device_create(gpib_class, NULL,
							MKDEV(GPIB_CODE, i), NULL, "gpib%i", i);

	return 0;
}

static void __exit gpib_common_exit_module(void)
{
	int i;

	for (i = 0; i < GPIB_MAX_NUM_BOARDS; ++i)
		device_destroy(gpib_class, MKDEV(GPIB_CODE, i));

	class_destroy(gpib_class);
	unregister_chrdev(GPIB_CODE, "gpib");
}

int gpib_match_device_path(struct device *dev, const char *device_path_in)
{
	if (device_path_in) {
		char *device_path;

		device_path = kobject_get_path(&dev->kobj, GFP_KERNEL);
		if (!device_path) {
			dev_err(dev, "kobject_get_path returned NULL.");
			return 0;
		}
		if (strcmp(device_path_in, device_path) != 0) {
			kfree(device_path);
			return 0;
		}
		kfree(device_path);
	}
	return 1;
}
EXPORT_SYMBOL(gpib_match_device_path);

struct pci_dev *gpib_pci_get_device(const gpib_board_config_t *config, unsigned int vendor_id,
				    unsigned int device_id, struct pci_dev *from)
{
	struct pci_dev *pci_device = from;

	while ((pci_device = pci_get_device(vendor_id, device_id, pci_device)))	{
		if (config->pci_bus >= 0 && config->pci_bus != pci_device->bus->number)
			continue;
		if (config->pci_slot >= 0 && config->pci_slot !=
		    PCI_SLOT(pci_device->devfn))
			continue;
		if (gpib_match_device_path(&pci_device->dev, config->device_path) == 0)
			continue;
		return pci_device;
	}
	return NULL;
}
EXPORT_SYMBOL(gpib_pci_get_device);

struct pci_dev *gpib_pci_get_subsys(const gpib_board_config_t *config, unsigned int vendor_id,
				    unsigned int device_id, unsigned int ss_vendor,
				    unsigned int ss_device,
				    struct pci_dev *from)
{
	struct pci_dev *pci_device = from;

	while ((pci_device = pci_get_subsys(vendor_id, device_id,
					    ss_vendor, ss_device, pci_device))) {
		if (config->pci_bus >= 0 && config->pci_bus != pci_device->bus->number)
			continue;
		if (config->pci_slot >= 0 && config->pci_slot !=
		    PCI_SLOT(pci_device->devfn))
			continue;
		if (gpib_match_device_path(&pci_device->dev, config->device_path) == 0)
			continue;
		return pci_device;
	}
	return NULL;
}
EXPORT_SYMBOL(gpib_pci_get_subsys);

module_init(gpib_common_init_module);
module_exit(gpib_common_exit_module);


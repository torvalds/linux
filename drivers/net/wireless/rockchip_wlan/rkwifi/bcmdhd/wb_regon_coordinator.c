/*
 * DHD BT WiFi Coex RegON Coordinator
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 */
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/eventpoll.h>
#include <linux/version.h>

#define DESCRIPTION "Broadcom WiFi BT Regon coordinator Driver"
#define AUTHOR "Broadcom Corporation"

#define DEVICE_NAME "wbrc"
#define CLASS_NAME "bcm"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0))
typedef unsigned int __poll_t;
#endif

/*
 * 4 byte message to sync with BT stack.
 * Byte 0 - header
 * Byte 1 - length of LTV now fixed to 2
 * Byte 2 - type
 * Byte 3 - command value
*/
#define WBRC_MSG_LEN	4u

/* Below defines to be mapped in the user space. */
/* TODO have these as enums and define new structure with members */

/* Byte 0 - Define header for direction of command */
#define HEADER_DIR_WL2BT 0x01
#define HEADER_DIR_BT2WL 0x02

/*
 * Byte 2 - Define Type of Command (Followed LTV format)
 * wifi/bt, signal/ack types
 */
#define TYPE_WIFI_CMD 0x01
#define TYPE_WIFI_ACK 0x02
#define TYPE_BT_CMD 0x03
#define TYPE_BT_ACK 0x04

/* Byte 3 - Define Value field: commands/acks */
#define CMD_RESET_WIFI 0x40
#define CMD_RESET_WIFI_WITH_ACK 0x41
#define CMD_RESET_BT 0x42
#define CMD_RESET_BT_WITH_ACK 0x43
#define ACK_RESET_WIFI_COMPLETE 0x80
#define ACK_RESET_BT_COMPLETE 0x81

struct wbrc_pvt_data {
	int wbrc_bt_dev_major_number;		/* BT char dev major number */
	struct class *wbrc_bt_dev_class;	/* BT char dev class */
	struct device *wbrc_bt_dev_device;	/* BT char dev */
	struct mutex wbrc_mutex;		/* mutex to synchronise */
	bool bt_dev_opened;			/* To check if bt dev open is called */
	wait_queue_head_t bt_reset_waitq;	/* waitq to wait till bt reset is done */
	unsigned int bt_reset_ack;		/* condition variable to be check for bt reset */
	wait_queue_head_t wlan_reset_waitq;	/* waitq to wait till wlan reset is done */
	unsigned int wlan_reset_ack;		/* condition variable to be check for wlan reset */
	wait_queue_head_t outmsg_waitq;		/* wait queue for poll */
	char wl2bt_message[WBRC_MSG_LEN];	/* message to communicate with Bt stack */
	bool read_data_available;		/* condition to check if read data is present */
};

static struct wbrc_pvt_data *g_wbrc_data;

#define WBRC_LOCK(wbrc_data)	{if (wbrc_data) mutex_lock(&(wbrc_data)->wbrc_mutex);}
#define WBRC_UNLOCK(wbrc_data)	{if (wbrc_data) mutex_unlock(&(wbrc_data)->wbrc_mutex);}

int wbrc_wl2bt_reset(void);
int wbrc_bt_reset_ack(struct wbrc_pvt_data *wbrc_data);

int wbrc_bt2wl_reset(void);
int wbrc_wl_reset_ack(struct wbrc_pvt_data *wbrc_data);

static int wbrc_bt_dev_open(struct inode *, struct file *);
static int wbrc_bt_dev_release(struct inode *, struct file *);
static ssize_t wbrc_bt_dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t wbrc_bt_dev_write(struct file *, const char *, size_t, loff_t *);
static __poll_t wbrc_bt_dev_poll(struct file *filep, poll_table *wait);

static struct file_operations wbrc_bt_dev_fops = {
	.open = wbrc_bt_dev_open,
	.read = wbrc_bt_dev_read,
	.write = wbrc_bt_dev_write,
	.release = wbrc_bt_dev_release,
	.poll = wbrc_bt_dev_poll,
};

static ssize_t wbrc_bt_dev_read(struct file *filep, char *buffer, size_t len,
                             loff_t *offset)
{
	struct wbrc_pvt_data *wbrc_data = filep->private_data;
	int err_count = 0;
	int ret = 0;

	WBRC_LOCK(wbrc_data);
	pr_info("%s\n", __func__);
	if (wbrc_data->read_data_available == FALSE) {
		goto exit;
	}
	if (len < WBRC_MSG_LEN) {
		pr_err("%s: invalid length:%d\n", __func__, (int)len);
		ret = -EFAULT;
		goto exit;
	}
	err_count = copy_to_user(buffer, &wbrc_data->wl2bt_message,
		sizeof(wbrc_data->wl2bt_message));
	if (err_count == 0) {
		pr_info("Sent %d bytes\n",
			(int)sizeof(wbrc_data->wl2bt_message));
		err_count = sizeof(wbrc_data->wl2bt_message);
	} else {
		pr_err("Failed to send %d bytes\n", err_count);
		ret = -EFAULT;
	}
	wbrc_data->read_data_available = FALSE;

exit:
	WBRC_UNLOCK(wbrc_data);
	return ret;
}

static ssize_t wbrc_bt_dev_write(struct file *filep, const char *buffer,
	size_t len, loff_t *offset)
{
	struct wbrc_pvt_data *wbrc_data = filep->private_data;
	int err_count = 0;
	int ret = 0;
	char message[WBRC_MSG_LEN] = {};

	WBRC_LOCK(wbrc_data);

	pr_info("%s Received %zu bytes\n", __func__, len);
	if (len < WBRC_MSG_LEN) {
		pr_err("%s: Received malformed packet:%d\n", __func__, (int)len);
		ret = -EFAULT;
		goto exit;
	}

	err_count = copy_from_user(message, buffer, len);
	if (err_count) {
		pr_err("%s: copy_from_user failed:%d\n", __func__, err_count);
		ret = -EFAULT;
		goto exit;
	}

	if (message[0] != HEADER_DIR_BT2WL) {
		pr_err("%s: invalid header:%d\n", __func__, message[0]);
		ret = -EFAULT;
		goto exit;
	}

	if (message[2] == TYPE_BT_CMD) {
		switch (message[3]) {
			case CMD_RESET_WIFI:
				pr_info("RCVD CMD_RESET_WIFI\n");
				break;
			case CMD_RESET_WIFI_WITH_ACK:
				pr_info("RCVD CMD_RESET_WIFI_WITH_ACK\n");
				break;
		}
	}

	if (message[2] == TYPE_BT_ACK && message[3] == ACK_RESET_BT_COMPLETE) {
		pr_info("RCVD ACK_RESET_BT_COMPLETE");
		wbrc_bt_reset_ack(wbrc_data);
	}

exit:
	WBRC_UNLOCK(wbrc_data);
	return ret;
}

static __poll_t wbrc_bt_dev_poll(struct file *filep, poll_table *wait)
{
	struct wbrc_pvt_data *wbrc_data = filep->private_data;
	__poll_t mask = 0;

	poll_wait(filep, &wbrc_data->outmsg_waitq, wait);

	if (wbrc_data->read_data_available)
		mask |= EPOLLIN | EPOLLRDNORM;

	if (!wbrc_data->bt_dev_opened)
		mask |= EPOLLHUP;

	return mask;
}

static int wbrc_bt_dev_open(struct inode *inodep, struct file *filep)
{
	struct wbrc_pvt_data *wbrc_data = g_wbrc_data;
	int ret = 0;
	WBRC_LOCK(wbrc_data);
	if (wbrc_data->bt_dev_opened) {
		pr_err("%s already opened\n", __func__);
		ret = -EFAULT;
		goto exit;
	}
	wbrc_data->bt_dev_opened = TRUE;
	pr_info("%s Device opened %d time(s)\n", __func__,
		wbrc_data->bt_dev_opened);
	filep->private_data = wbrc_data;

exit:
	WBRC_UNLOCK(wbrc_data);
	return ret;
}

static int wbrc_bt_dev_release(struct inode *inodep, struct file *filep)
{
	struct wbrc_pvt_data *wbrc_data = filep->private_data;
	WBRC_LOCK(wbrc_data);
	pr_info("%s Device closed %d\n", __func__, wbrc_data->bt_dev_opened);
	wbrc_data->bt_dev_opened = FALSE;
	WBRC_UNLOCK(wbrc_data);
	wake_up_interruptible(&wbrc_data->outmsg_waitq);
	return 0;
}

void wbrc_signal_bt_reset(struct wbrc_pvt_data *wbrc_data)
{
	pr_info("%s\n", __func__);

	/* Below message will be read by userspace using .read */
	wbrc_data->wl2bt_message[0] = HEADER_DIR_WL2BT;       // Minimal Header
	wbrc_data->wl2bt_message[1] = 2;                      // Length
	wbrc_data->wl2bt_message[2] = TYPE_WIFI_CMD;          // Type
	wbrc_data->wl2bt_message[3] = CMD_RESET_BT_WITH_ACK;  // Value
	wbrc_data->read_data_available = TRUE;
	smp_wmb();

	wake_up_interruptible(&wbrc_data->outmsg_waitq);
}

int wbrc_init(void)
{
	int err = 0;
	struct wbrc_pvt_data *wbrc_data;
	pr_info("%s\n", __func__);
	wbrc_data = kzalloc(sizeof(struct wbrc_pvt_data), GFP_KERNEL);
	if (wbrc_data == NULL) {
		return -ENOMEM;
	}
	mutex_init(&wbrc_data->wbrc_mutex);
	init_waitqueue_head(&wbrc_data->bt_reset_waitq);
	init_waitqueue_head(&wbrc_data->wlan_reset_waitq);
	init_waitqueue_head(&wbrc_data->outmsg_waitq);
	g_wbrc_data = wbrc_data;

	wbrc_data->wbrc_bt_dev_major_number = register_chrdev(0, DEVICE_NAME, &wbrc_bt_dev_fops);
	err = wbrc_data->wbrc_bt_dev_major_number;
	if (wbrc_data->wbrc_bt_dev_major_number < 0) {
		pr_alert("wbrc_sequencer failed to register a major number\n");
		goto err_register;
	}

	wbrc_data->wbrc_bt_dev_class = class_create(THIS_MODULE, CLASS_NAME);
	err = PTR_ERR(wbrc_data->wbrc_bt_dev_class);
	if (IS_ERR(wbrc_data->wbrc_bt_dev_class)) {
		pr_alert("Failed to register device class\n");
		goto err_class;
	}

	wbrc_data->wbrc_bt_dev_device = device_create(
		wbrc_data->wbrc_bt_dev_class, NULL, MKDEV(wbrc_data->wbrc_bt_dev_major_number, 0),
		NULL, DEVICE_NAME);
	err = PTR_ERR(wbrc_data->wbrc_bt_dev_device);
	if (IS_ERR(wbrc_data->wbrc_bt_dev_device)) {
		pr_alert("Failed to create the device\n");
		goto err_device;
	}
	pr_info("device class created correctly\n");

	return 0;

err_device:
	class_destroy(wbrc_data->wbrc_bt_dev_class);
err_class:
	unregister_chrdev(wbrc_data->wbrc_bt_dev_major_number, DEVICE_NAME);
err_register:
	kfree(wbrc_data);
	g_wbrc_data = NULL;
	return err;
}

void wbrc_exit(void)
{
	struct wbrc_pvt_data *wbrc_data = g_wbrc_data;
	pr_info("%s\n", __func__);
	wake_up_interruptible(&wbrc_data->outmsg_waitq);
	device_destroy(wbrc_data->wbrc_bt_dev_class, MKDEV(wbrc_data->wbrc_bt_dev_major_number, 0));
	class_destroy(wbrc_data->wbrc_bt_dev_class);
	unregister_chrdev(wbrc_data->wbrc_bt_dev_major_number, DEVICE_NAME);
	kfree(wbrc_data);
	g_wbrc_data = NULL;
}

#ifndef BCMDHD_MODULAR
/* Required only for Built-in DHD */
module_init(wbrc_init);
module_exit(wbrc_exit);
#endif /* BOARD_MODULAR */

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

/*
 * Wait until the condition *var == condition is met.
 * Returns 0 if the @condition evaluated to false after the timeout elapsed
 * Returns 1 if the @condition evaluated to true
 */
#define WBRC_RESET_WAIT_TIMEOUT 4000
int
wbrc_reset_wait_on_condition(wait_queue_head_t *reset_waitq, uint *var, uint condition)
{
	int timeout;

	/* Convert timeout in millsecond to jiffies */
	timeout = msecs_to_jiffies(WBRC_RESET_WAIT_TIMEOUT);

	timeout = wait_event_timeout(*reset_waitq, (*var == condition), timeout);

	return timeout;
}

/* WBRC_LOCK should be held from caller */
int wbrc_bt_reset_ack(struct wbrc_pvt_data *wbrc_data)
{
	pr_info("%s\n", __func__);
	wbrc_data->bt_reset_ack = TRUE;
	smp_wmb();
	wake_up(&wbrc_data->bt_reset_waitq);
	return 0;
}

int wbrc_wl2bt_reset(void)
{
	int ret = 0;
	struct wbrc_pvt_data *wbrc_data = g_wbrc_data;

	pr_info("%s\n", __func__);

	WBRC_LOCK(wbrc_data);
	if (!wbrc_data->bt_dev_opened) {
		pr_info("%s: no BT\n", __func__);
		WBRC_UNLOCK(wbrc_data);
		return ret;
	}

	wbrc_data->bt_reset_ack = FALSE;

	wbrc_signal_bt_reset(wbrc_data);

	WBRC_UNLOCK(wbrc_data);
	/* Wait till BT reset is done */
	wbrc_reset_wait_on_condition(&wbrc_data->bt_reset_waitq,
		&wbrc_data->bt_reset_ack, TRUE);
	if (wbrc_data->bt_reset_ack == FALSE) {
		pr_err("%s: BT reset timeout\n", __func__);
		ret = -1;
	}
	return ret;
}
EXPORT_SYMBOL(wbrc_wl2bt_reset);

int wbrc_signal_wlan_reset(struct wbrc_pvt_data *wbrc_data)
{
	/* TODO call dhd reset, right now just send ack from here */
	wbrc_wl_reset_ack(wbrc_data);
	return 0;
}

/* WBRC_LOCK should be held from caller, this will be called from DHD */
int wbrc_wl_reset_ack(struct wbrc_pvt_data *wbrc_data)
{
	pr_info("%s\n", __func__);
	wbrc_data->wlan_reset_ack = TRUE;
	smp_wmb();
	wake_up(&wbrc_data->wlan_reset_waitq);
	return 0;
}
EXPORT_SYMBOL(wbrc_wl_reset_ack);

int wbrc_bt2wl_reset(void)
{
	int ret = 0;
	struct wbrc_pvt_data *wbrc_data = g_wbrc_data;

	pr_info("%s\n", __func__);

	WBRC_LOCK(wbrc_data);
	wbrc_data->wlan_reset_ack = FALSE;
	wbrc_signal_wlan_reset(wbrc_data);
	/* Wait till WLAN reset is done */
	wbrc_reset_wait_on_condition(&wbrc_data->wlan_reset_waitq,
		&wbrc_data->wlan_reset_ack, TRUE);
	if (wbrc_data->wlan_reset_ack == FALSE) {
		pr_err("%s: WLAN reset timeout\n", __func__);
		ret = -1;
	}
	WBRC_UNLOCK(wbrc_data);
	return ret;
}
EXPORT_SYMBOL(wbrc_bt2wl_reset);

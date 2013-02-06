/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/diagchar.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <asm/current.h>
#ifdef CONFIG_DIAG_OVER_USB
#include <mach/usbdiag.h>
#endif
#include "diagchar_hdlc.h"
#include "diagmem.h"
#include "diagchar.h"
#include "diagfwd.h"
#include "diagfwd_sdio.h"

void __diag_sdio_send_req(void)
{
	int r = 0;
	void *buf = driver->buf_in_sdio;

	if (driver->sdio_ch && (!driver->in_busy_sdio)) {
		r = sdio_read_avail(driver->sdio_ch);

		if (r > IN_BUF_SIZE) {
			if (r < MAX_IN_BUF_SIZE) {
				pr_err("diag: SDIO sending"
					  " packets more than %d bytes\n", r);
				buf = krealloc(buf, r, GFP_KERNEL);
			} else {
				pr_err("diag: SDIO sending"
			  " in packets more than %d bytes\n", MAX_IN_BUF_SIZE);
				return;
			}
		}
		if (r > 0) {
			if (!buf)
				printk(KERN_INFO "Out of diagmem for SDIO\n");
			else {
				APPEND_DEBUG('i');
				sdio_read(driver->sdio_ch, buf, r);
				if (((!driver->usb_connected) && (driver->
					logging_mode == USB_MODE)) || (driver->
					logging_mode == NO_LOGGING_MODE)) {
					/* Drop the diag payload */
					driver->in_busy_sdio = 0;
					return;
				}
				APPEND_DEBUG('j');
				driver->write_ptr_mdm->length = r;
				driver->in_busy_sdio = 1;
				diag_device_write(buf, SDIO_DATA,
						 driver->write_ptr_mdm);
			}
		}
	}
}

static void diag_read_sdio_work_fn(struct work_struct *work)
{
	__diag_sdio_send_req();
}

static void diag_sdio_notify(void *ctxt, unsigned event)
{
	if (event == SDIO_EVENT_DATA_READ_AVAIL)
		queue_work(driver->diag_sdio_wq,
				 &(driver->diag_read_sdio_work));

	if (event == SDIO_EVENT_DATA_WRITE_AVAIL)
		wake_up_interruptible(&driver->wait_q);
}

static int diag_sdio_close(void)
{
	queue_work(driver->diag_sdio_wq, &(driver->diag_close_sdio_work));
	return 0;
}

static void diag_close_sdio_work_fn(struct work_struct *work)
{
	pr_debug("diag: sdio close called\n");
	if (sdio_close(driver->sdio_ch))
		pr_err("diag: could not close SDIO channel\n");
	else
		driver->sdio_ch = NULL; /* channel successfully closed */
}

int diagfwd_connect_sdio(void)
{
	int err;

	err = usb_diag_alloc_req(driver->mdm_ch, N_MDM_WRITE,
							 N_MDM_READ);
	if (err)
		pr_err("diag: unable to alloc USB req on mdm ch\n");

	driver->in_busy_sdio = 0;
	if (!driver->sdio_ch) {
		err = sdio_open("SDIO_DIAG", &driver->sdio_ch, driver,
							 diag_sdio_notify);
		if (err)
			pr_info("diag: could not open SDIO channel\n");
		else
			pr_info("diag: opened SDIO channel\n");
	} else {
		pr_info("diag: SDIO channel already open\n");
	}

	/* Poll USB channel to check for data*/
	queue_work(driver->diag_sdio_wq, &(driver->diag_read_mdm_work));
	/* Poll SDIO channel to check for data*/
	queue_work(driver->diag_sdio_wq, &(driver->diag_read_sdio_work));
	return 0;
}

int diagfwd_disconnect_sdio(void)
{
	usb_diag_free_req(driver->mdm_ch);
	if (driver->sdio_ch && (driver->logging_mode == USB_MODE)) {
		driver->in_busy_sdio = 1;
		diag_sdio_close();
	}
	return 0;
}

int diagfwd_write_complete_sdio(void)
{
	driver->in_busy_sdio = 0;
	APPEND_DEBUG('q');
	queue_work(driver->diag_sdio_wq, &(driver->diag_read_sdio_work));
	return 0;
}

int diagfwd_read_complete_sdio(void)
{
	queue_work(driver->diag_sdio_wq, &(driver->diag_read_mdm_work));
	return 0;
}

void diag_read_mdm_work_fn(struct work_struct *work)
{
	if (driver->sdio_ch) {
		wait_event_interruptible(driver->wait_q, ((sdio_write_avail
			(driver->sdio_ch) >= driver->read_len_mdm) ||
				 !(driver->sdio_ch)));
		if (!(driver->sdio_ch)) {
			pr_alert("diag: sdio channel not valid");
			return;
		}
		if (driver->sdio_ch && driver->usb_buf_mdm_out &&
						 (driver->read_len_mdm > 0))
			sdio_write(driver->sdio_ch, driver->usb_buf_mdm_out,
							 driver->read_len_mdm);
		APPEND_DEBUG('x');
		driver->usb_read_mdm_ptr->buf = driver->usb_buf_mdm_out;
		driver->usb_read_mdm_ptr->length = USB_MAX_OUT_BUF;
		usb_diag_read(driver->mdm_ch, driver->usb_read_mdm_ptr);
		APPEND_DEBUG('y');
	}
}

static int diag_sdio_probe(struct platform_device *pdev)
{
	int err;

	err = sdio_open("SDIO_DIAG", &driver->sdio_ch, driver,
							 diag_sdio_notify);
	if (err)
		printk(KERN_INFO "DIAG could not open SDIO channel");
	else {
		printk(KERN_INFO "DIAG opened SDIO channel");
		queue_work(driver->diag_sdio_wq, &(driver->diag_read_mdm_work));
	}

	return err;
}

static int diag_sdio_remove(struct platform_device *pdev)
{
	pr_debug("\n diag: sdio remove called");
	/* Disable SDIO channel to prevent further read/write */
	driver->sdio_ch = NULL;
	return 0;
}

static int diagfwd_sdio_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int diagfwd_sdio_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static const struct dev_pm_ops diagfwd_sdio_dev_pm_ops = {
	.runtime_suspend = diagfwd_sdio_runtime_suspend,
	.runtime_resume = diagfwd_sdio_runtime_resume,
};

static struct platform_driver msm_sdio_ch_driver = {
	.probe = diag_sdio_probe,
	.remove = diag_sdio_remove,
	.driver = {
		   .name = "SDIO_DIAG",
		   .owner = THIS_MODULE,
		   .pm   = &diagfwd_sdio_dev_pm_ops,
		   },
};

void diagfwd_sdio_init(void)
{
	int ret;

	driver->read_len_mdm = 0;
	if (driver->buf_in_sdio == NULL)
		driver->buf_in_sdio = kzalloc(IN_BUF_SIZE, GFP_KERNEL);
		if (driver->buf_in_sdio == NULL)
			goto err;
	if (driver->usb_buf_mdm_out  == NULL)
		driver->usb_buf_mdm_out = kzalloc(USB_MAX_OUT_BUF, GFP_KERNEL);
		if (driver->usb_buf_mdm_out == NULL)
			goto err;
	if (driver->write_ptr_mdm == NULL)
		driver->write_ptr_mdm = kzalloc(
			sizeof(struct diag_request), GFP_KERNEL);
		if (driver->write_ptr_mdm == NULL)
			goto err;
	if (driver->usb_read_mdm_ptr == NULL)
		driver->usb_read_mdm_ptr = kzalloc(
			sizeof(struct diag_request), GFP_KERNEL);
		if (driver->usb_read_mdm_ptr == NULL)
			goto err;
	driver->diag_sdio_wq = create_singlethread_workqueue("diag_sdio_wq");
#ifdef CONFIG_DIAG_OVER_USB
	driver->mdm_ch = usb_diag_open(DIAG_MDM, driver,
			diag_usb_legacy_notifier);
	if (IS_ERR(driver->mdm_ch)) {
		printk(KERN_ERR "Unable to open USB diag MDM channel\n");
		goto err;
	}
	INIT_WORK(&(driver->diag_read_mdm_work), diag_read_mdm_work_fn);
#endif
	INIT_WORK(&(driver->diag_read_sdio_work), diag_read_sdio_work_fn);
	INIT_WORK(&(driver->diag_close_sdio_work), diag_close_sdio_work_fn);
	ret = platform_driver_register(&msm_sdio_ch_driver);
	if (ret)
		printk(KERN_INFO "DIAG could not register SDIO device");
	else
		printk(KERN_INFO "DIAG registered SDIO device");

	return;
err:
		printk(KERN_INFO "\n Could not initialize diag buf for SDIO");
		kfree(driver->buf_in_sdio);
		kfree(driver->usb_buf_mdm_out);
		kfree(driver->write_ptr_mdm);
		kfree(driver->usb_read_mdm_ptr);
		if (driver->diag_sdio_wq)
			destroy_workqueue(driver->diag_sdio_wq);
}

void diagfwd_sdio_exit(void)
{
#ifdef CONFIG_DIAG_OVER_USB
	if (driver->usb_connected)
		usb_diag_free_req(driver->mdm_ch);
#endif
	platform_driver_unregister(&msm_sdio_ch_driver);
#ifdef CONFIG_DIAG_OVER_USB
	usb_diag_close(driver->mdm_ch);
#endif
	kfree(driver->buf_in_sdio);
	kfree(driver->usb_buf_mdm_out);
	kfree(driver->write_ptr_mdm);
	kfree(driver->usb_read_mdm_ptr);
	destroy_workqueue(driver->diag_sdio_wq);
}

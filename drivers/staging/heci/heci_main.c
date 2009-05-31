/*
 * Part of Intel(R) Manageability Engine Interface Linux driver
 *
 * Copyright (c) 2003 - 2008 Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/pci.h>
#include <linux/reboot.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/unistd.h>
#include <linux/kthread.h>

#include "heci.h"
#include "heci_interface.h"
#include "heci_version.h"


#define HECI_READ_TIMEOUT	45

#define HECI_DRIVER_NAME	"heci"

/*
 *  heci driver strings
 */
static char heci_driver_name[] = HECI_DRIVER_NAME;
static char heci_driver_string[] = "Intel(R) Management Engine Interface";
static char heci_driver_version[] = HECI_DRIVER_VERSION;
static char heci_copyright[] = "Copyright (c) 2003 - 2008 Intel Corporation.";


#ifdef HECI_DEBUG
int heci_debug = 1;
#else
int heci_debug;
#endif
MODULE_PARM_DESC(heci_debug,  "Debug enabled or not");
module_param(heci_debug, int, 0644);


#define HECI_DEV_NAME	"heci"

/* heci char device for registration */
static struct cdev heci_cdev;

/* major number for device */
static int heci_major;
/* The device pointer */
static struct pci_dev *heci_device;

static struct class *heci_class;


/* heci_pci_tbl - PCI Device ID Table */
static struct pci_device_id heci_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82946GZ)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82G35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82Q965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82G965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82GM965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_82GME965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_82Q35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_82G33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_82Q33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_82X38)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_3200)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_6)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_7)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_8)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9_10)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9M_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9M_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9M_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH9M_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH10_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH10_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH10_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, HECI_DEV_ID_ICH10_4)},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, heci_pci_tbl);

/*
 * Local Function Prototypes
 */
static int __init heci_init_module(void);
static void __exit heci_exit_module(void);
static int __devinit heci_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent);
static void __devexit heci_remove(struct pci_dev *pdev);
static int heci_open(struct inode *inode, struct file *file);
static int heci_release(struct inode *inode, struct file *file);
static ssize_t heci_read(struct file *file, char __user *ubuf,
			 size_t length, loff_t *offset);
static int heci_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long data);
static ssize_t heci_write(struct file *file, const char __user *ubuf,
			  size_t length, loff_t *offset);
static unsigned int heci_poll(struct file *file, poll_table *wait);
static struct heci_cb_private *find_read_list_entry(
		struct iamt_heci_device *dev,
		struct heci_file_private *file_ext);
#ifdef CONFIG_PM
static int heci_suspend(struct pci_dev *pdev, pm_message_t state);
static int heci_resume(struct pci_dev *pdev);
static __u16 g_sus_wd_timeout;
#else
#define heci_suspend NULL
#define heci_resume NULL
#endif
/*
 *  PCI driver structure
 */
static struct pci_driver heci_driver = {
	.name = heci_driver_name,
	.id_table = heci_pci_tbl,
	.probe = heci_probe,
	.remove = __devexit_p(heci_remove),
	.shutdown = __devexit_p(heci_remove),
	.suspend = heci_suspend,
	.resume = heci_resume
};

/*
 * file operations structure will be use heci char device.
 */
static const struct file_operations heci_fops = {
	.owner = THIS_MODULE,
	.read = heci_read,
	.ioctl = heci_ioctl,
	.open = heci_open,
	.release = heci_release,
	.write = heci_write,
	.poll = heci_poll,
};

/**
 * heci_registration_cdev - set up the cdev structure for heci device.
 *
 * @dev: char device struct
 * @hminor: minor number for registration char device
 * @fops: file operations structure
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_registration_cdev(struct cdev *dev, int hminor,
				  const struct file_operations *fops)
{
	int ret, devno = MKDEV(heci_major, hminor);

	cdev_init(dev, fops);
	dev->owner = THIS_MODULE;
	ret = cdev_add(dev, devno, 1);
	/* Fail gracefully if need be */
	if (ret) {
		printk(KERN_ERR "heci: Error %d registering heci device %d\n",
		       ret, hminor);
	}
	return ret;
}

/* Display the version of heci driver. */
static ssize_t version_show(struct class *dev, char *buf)
{
	return sprintf(buf, "%s %s.\n",
		       heci_driver_string, heci_driver_version);
}

static CLASS_ATTR(version, S_IRUGO, version_show, NULL);

/**
 * heci_register_cdev - registers heci char device
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_register_cdev(void)
{
	int ret;
	dev_t dev;

	/* registration of char devices */
	ret = alloc_chrdev_region(&dev, HECI_MINORS_BASE, HECI_MINORS_COUNT,
				  HECI_DRIVER_NAME);
	if (ret) {
		printk(KERN_ERR "heci: Error allocating char device region.\n");
		return ret;
	}

	heci_major = MAJOR(dev);

	ret = heci_registration_cdev(&heci_cdev, HECI_MINOR_NUMBER,
				     &heci_fops);
	if (ret)
		unregister_chrdev_region(MKDEV(heci_major, HECI_MINORS_BASE),
					 HECI_MINORS_COUNT);

	return ret;
}

/**
 * heci_unregister_cdev - unregisters heci char device
 */
static void heci_unregister_cdev(void)
{
	cdev_del(&heci_cdev);
	unregister_chrdev_region(MKDEV(heci_major, HECI_MINORS_BASE),
				 HECI_MINORS_COUNT);
}

#ifndef HECI_DEVICE_CREATE
#define HECI_DEVICE_CREATE device_create
#endif
/**
 * heci_sysfs_device_create - adds device entry to sysfs
 *
 * returns 0 on success, <0 on failure.
 */
static int heci_sysfs_device_create(void)
{
	struct class *class;
	void *tmphdev;
	int err = 0;

	class = class_create(THIS_MODULE, HECI_DRIVER_NAME);
	if (IS_ERR(class)) {
		err = PTR_ERR(class);
		printk(KERN_ERR "heci: Error creating heci class.\n");
		goto err_out;
	}

	err = class_create_file(class, &class_attr_version);
	if (err) {
		class_destroy(class);
		printk(KERN_ERR "heci: Error creating heci class file.\n");
		goto err_out;
	}

	tmphdev = HECI_DEVICE_CREATE(class, NULL, heci_cdev.dev, NULL,
					HECI_DEV_NAME);
	if (IS_ERR(tmphdev)) {
		err = PTR_ERR(tmphdev);
		class_remove_file(class, &class_attr_version);
		class_destroy(class);
		goto err_out;
	}

	heci_class = class;
err_out:
	return err;
}

/**
 * heci_sysfs_device_remove - unregisters the device entry on sysfs
 */
static void heci_sysfs_device_remove(void)
{
	if ((heci_class == NULL) || (IS_ERR(heci_class)))
		return;

	device_destroy(heci_class, heci_cdev.dev);
	class_remove_file(heci_class, &class_attr_version);
	class_destroy(heci_class);
}

/**
 * heci_init_module - Driver Registration Routine
 *
 * heci_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 *
 * returns 0 on success, <0 on failure.
 */
static int __init heci_init_module(void)
{
	int ret = 0;

	printk(KERN_INFO "heci: %s - version %s\n", heci_driver_string,
			heci_driver_version);
	printk(KERN_INFO "heci: %s\n", heci_copyright);

	/* init pci module */
	ret = pci_register_driver(&heci_driver);
	if (ret < 0) {
		printk(KERN_ERR "heci: Error registering driver.\n");
		goto end;
	}

	ret = heci_register_cdev();
	if (ret)
		goto unregister_pci;

	ret = heci_sysfs_device_create();
	if (ret)
		goto unregister_cdev;

	return ret;

unregister_cdev:
	heci_unregister_cdev();
unregister_pci:
	pci_unregister_driver(&heci_driver);
end:
	return ret;
}

module_init(heci_init_module);


/**
 * heci_exit_module - Driver Exit Cleanup Routine
 *
 * heci_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit heci_exit_module(void)
{
	pci_unregister_driver(&heci_driver);
	heci_sysfs_device_remove();
	heci_unregister_cdev();
}

module_exit(heci_exit_module);


/**
 * heci_probe - Device Initialization Routine
 *
 * @pdev: PCI device information struct
 * @ent: entry in kcs_pci_tbl
 *
 * returns 0 on success, <0 on failure.
 */
static int __devinit heci_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct iamt_heci_device *dev = NULL;
	int i, err = 0;

	if (heci_device) {
		err = -EEXIST;
		goto end;
	}
	/* enable pci dev */
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "heci: Failed to enable pci device.\n");
		goto end;
	}
	/* set PCI host mastering  */
	pci_set_master(pdev);
	/* pci request regions for heci driver */
	err = pci_request_regions(pdev, heci_driver_name);
	if (err) {
		printk(KERN_ERR "heci: Failed to get pci regions.\n");
		goto disable_device;
	}
	/* allocates and initializes the heci dev structure */
	dev = init_heci_device(pdev);
	if (!dev) {
		err = -ENOMEM;
		goto release_regions;
	}
	/* mapping  IO device memory */
	for (i = 0; i <= 5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			printk(KERN_ERR "heci: heci has IO ports.\n");
			goto free_device;
		} else if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			if (dev->mem_base) {
				printk(KERN_ERR
					"heci: Too many mem addresses.\n");
				goto free_device;
			}
			dev->mem_base = pci_resource_start(pdev, i);
			dev->mem_length = pci_resource_len(pdev, i);
		}
	}
	if (!dev->mem_base) {
		printk(KERN_ERR "heci: No address to use.\n");
		err = -ENODEV;
		goto free_device;
	}
	dev->mem_addr = ioremap_nocache(dev->mem_base,
			dev->mem_length);
	if (!dev->mem_addr) {
		printk(KERN_ERR "heci: Remap IO device memory failure.\n");
		err = -ENOMEM;
		goto free_device;
	}
	/* request and enable interrupt   */
	err = request_irq(pdev->irq, heci_isr_interrupt, IRQF_SHARED,
			heci_driver_name, dev);
	if (err) {
		printk(KERN_ERR "heci: Request_irq failure. irq = %d \n",
		       pdev->irq);
		goto unmap_memory;
	}

	if (heci_hw_init(dev)) {
		printk(KERN_ERR "heci: Init hw failure.\n");
		err = -ENODEV;
		goto release_irq;
	}
	init_timer(&dev->wd_timer);

	heci_initialize_clients(dev);
	if (dev->heci_state != HECI_ENABLED) {
		err = -ENODEV;
		goto release_hw;
	}

	spin_lock_bh(&dev->device_lock);
	heci_device = pdev;
	pci_set_drvdata(pdev, dev);
	spin_unlock_bh(&dev->device_lock);

	if (dev->wd_timeout)
		mod_timer(&dev->wd_timer, jiffies);

#ifdef CONFIG_PM
	g_sus_wd_timeout = 0;
#endif
	printk(KERN_INFO "heci driver initialization successful.\n");
	return 0;

release_hw:
	/* disable interrupts */
	dev->host_hw_state = read_heci_register(dev, H_CSR);
	heci_csr_disable_interrupts(dev);

	del_timer_sync(&dev->wd_timer);

	flush_scheduled_work();

release_irq:
	free_irq(pdev->irq, dev);
unmap_memory:
	if (dev->mem_addr)
		iounmap(dev->mem_addr);
free_device:
	kfree(dev);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
end:
	printk(KERN_ERR "heci driver initialization failed.\n");
	return err;
}

/**
 * heci_remove - Device Removal Routine
 *
 * @pdev: PCI device information struct
 *
 * heci_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void __devexit heci_remove(struct pci_dev *pdev)
{
	struct iamt_heci_device *dev = pci_get_drvdata(pdev);

	if (heci_device != pdev)
		return;

	if (dev == NULL)
		return;

	spin_lock_bh(&dev->device_lock);
	if (heci_device != pdev) {
		spin_unlock_bh(&dev->device_lock);
		return;
	}

	if (dev->reinit_tsk != NULL) {
		kthread_stop(dev->reinit_tsk);
		dev->reinit_tsk = NULL;
	}

	del_timer_sync(&dev->wd_timer);
	if (dev->wd_file_ext.state == HECI_FILE_CONNECTED
	    && dev->wd_timeout) {
		dev->wd_timeout = 0;
		dev->wd_due_counter = 0;
		memcpy(dev->wd_data, heci_stop_wd_params, HECI_WD_PARAMS_SIZE);
		dev->stop = 1;
		if (dev->host_buffer_is_empty &&
		    flow_ctrl_creds(dev, &dev->wd_file_ext)) {
			dev->host_buffer_is_empty = 0;

			if (!heci_send_wd(dev))
				DBG("send stop WD failed\n");
			else
				flow_ctrl_reduce(dev, &dev->wd_file_ext);

			dev->wd_pending = 0;
		} else {
			dev->wd_pending = 1;
		}
		dev->wd_stoped = 0;
		spin_unlock_bh(&dev->device_lock);

		wait_event_interruptible_timeout(dev->wait_stop_wd,
				(dev->wd_stoped), 10 * HZ);
		spin_lock_bh(&dev->device_lock);
		if (!dev->wd_stoped)
			DBG("stop wd failed to complete.\n");
		else
			DBG("stop wd complete.\n");

	}

	heci_device = NULL;
	spin_unlock_bh(&dev->device_lock);

	if (dev->iamthif_file_ext.state == HECI_FILE_CONNECTED) {
		dev->iamthif_file_ext.state = HECI_FILE_DISCONNECTING;
		heci_disconnect_host_client(dev,
					    &dev->iamthif_file_ext);
	}
	if (dev->wd_file_ext.state == HECI_FILE_CONNECTED) {
		dev->wd_file_ext.state = HECI_FILE_DISCONNECTING;
		heci_disconnect_host_client(dev,
					    &dev->wd_file_ext);
	}

	spin_lock_bh(&dev->device_lock);

	/* remove entry if already in list */
	DBG("list del iamthif and wd file list.\n");
	heci_remove_client_from_file_list(dev, dev->wd_file_ext.
					  host_client_id);
	heci_remove_client_from_file_list(dev,
			dev->iamthif_file_ext.host_client_id);

	dev->iamthif_current_cb = NULL;
	dev->iamthif_file_ext.file = NULL;
	dev->num_heci_me_clients = 0;

	spin_unlock_bh(&dev->device_lock);

	flush_scheduled_work();

	/* disable interrupts */
	heci_csr_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_set_drvdata(pdev, NULL);

	if (dev->mem_addr)
		iounmap(dev->mem_addr);

	kfree(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * heci_clear_list - remove all callbacks associated with file
 * 		from heci_cb_list
 *
 * @file: file information struct
 * @heci_cb_list: callbacks list
 *
 * heci_clear_list is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns 1 if callback removed from the list, 0 otherwise
 */
static int heci_clear_list(struct iamt_heci_device *dev,
		struct file *file, struct list_head *heci_cb_list)
{
	struct heci_cb_private *priv_cb_pos = NULL;
	struct heci_cb_private *priv_cb_next = NULL;
	struct file *file_temp;
	int rets = 0;

	/* list all list member */
	list_for_each_entry_safe(priv_cb_pos, priv_cb_next,
				 heci_cb_list, cb_list) {
		file_temp = (struct file *)priv_cb_pos->file_object;
		/* check if list member associated with a file */
		if (file_temp == file) {
			/* remove member from the list */
			list_del(&priv_cb_pos->cb_list);
			/* check if cb equal to current iamthif cb */
			if (dev->iamthif_current_cb == priv_cb_pos) {
				dev->iamthif_current_cb = NULL;
				/* send flow control to iamthif client */
				heci_send_flow_control(dev,
						       &dev->iamthif_file_ext);
			}
			/* free all allocated buffers */
			heci_free_cb_private(priv_cb_pos);
			rets = 1;
		}
	}
	return rets;
}

/**
 * heci_clear_lists - remove all callbacks associated with file
 *
 * @dev: device information struct
 * @file: file information struct
 *
 * heci_clear_lists is called to clear resources associated with file
 * when application calls close function or Ctrl-C was pressed
 *
 * returns 1 if callback removed from the list, 0 otherwise
 */
static int heci_clear_lists(struct iamt_heci_device *dev, struct file *file)
{
	int rets = 0;

	/* remove callbacks associated with a file */
	heci_clear_list(dev, file, &dev->pthi_cmd_list.heci_cb.cb_list);
	if (heci_clear_list(dev, file,
			    &dev->pthi_read_complete_list.heci_cb.cb_list))
		rets = 1;

	heci_clear_list(dev, file, &dev->ctrl_rd_list.heci_cb.cb_list);

	if (heci_clear_list(dev, file, &dev->ctrl_wr_list.heci_cb.cb_list))
		rets = 1;

	if (heci_clear_list(dev, file,
			    &dev->write_waiting_list.heci_cb.cb_list))
		rets = 1;

	if (heci_clear_list(dev, file, &dev->write_list.heci_cb.cb_list))
		rets = 1;

	/* check if iamthif_current_cb not NULL */
	if (dev->iamthif_current_cb && (!rets)) {
		/* check file and iamthif current cb association */
		if (dev->iamthif_current_cb->file_object == file) {
			/* remove cb */
			heci_free_cb_private(dev->iamthif_current_cb);
			dev->iamthif_current_cb = NULL;
			rets = 1;
		}
	}
	return rets;
}

/**
 * heci_open - the open function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * returns 0 on success, <0 on error
 */
static int heci_open(struct inode *inode, struct file *file)
{
	struct heci_file_private *file_ext;
	int if_num = iminor(inode);
	struct iamt_heci_device *dev;

	if (!heci_device)
		return -ENODEV;

	dev = pci_get_drvdata(heci_device);
	if ((if_num != HECI_MINOR_NUMBER) || (!dev))
		return -ENODEV;

	file_ext = heci_alloc_file_private(file);
	if (file_ext == NULL)
		return -ENOMEM;

	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state != HECI_ENABLED) {
		spin_unlock_bh(&dev->device_lock);
		kfree(file_ext);
		return -ENODEV;
	}
	if (dev->open_handle_count >= HECI_MAX_OPEN_HANDLE_COUNT) {
		spin_unlock_bh(&dev->device_lock);
		kfree(file_ext);
		return -ENFILE;
	};
	dev->open_handle_count++;
	list_add_tail(&file_ext->link, &dev->file_list);
	while ((dev->heci_host_clients[dev->current_host_client_id / 8]
		& (1 << (dev->current_host_client_id % 8))) != 0) {

		dev->current_host_client_id++; /* allow overflow */
		DBG("current_host_client_id = %d\n",
		    dev->current_host_client_id);
		DBG("dev->open_handle_count = %lu\n",
		    dev->open_handle_count);
	}
	DBG("current_host_client_id = %d\n", dev->current_host_client_id);
	file_ext->host_client_id = dev->current_host_client_id;
	dev->heci_host_clients[file_ext->host_client_id / 8] |=
		(1 << (file_ext->host_client_id % 8));
	spin_unlock_bh(&dev->device_lock);
	spin_lock(&file_ext->file_lock);
	file_ext->state = HECI_FILE_INITIALIZING;
	file_ext->sm_state = 0;

	file->private_data = file_ext;
	spin_unlock(&file_ext->file_lock);

	return 0;
}

/**
 * heci_release - the release function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * returns 0 on success, <0 on error
 */
static int heci_release(struct inode *inode, struct file *file)
{
	int rets = 0;
	int if_num = iminor(inode);
	struct heci_file_private *file_ext = file->private_data;
	struct heci_cb_private *priv_cb = NULL;
	struct iamt_heci_device *dev;

	if (!heci_device)
		return -ENODEV;

	dev = pci_get_drvdata(heci_device);
	if ((if_num != HECI_MINOR_NUMBER) || (!dev) || (!file_ext))
		return -ENODEV;

	if (file_ext != &dev->iamthif_file_ext) {
		spin_lock(&file_ext->file_lock);
		if (file_ext->state == HECI_FILE_CONNECTED) {
			file_ext->state = HECI_FILE_DISCONNECTING;
			spin_unlock(&file_ext->file_lock);
			DBG("disconnecting client host client = %d, "
			    "ME client = %d\n",
			    file_ext->host_client_id,
			    file_ext->me_client_id);
			rets = heci_disconnect_host_client(dev, file_ext);
			spin_lock(&file_ext->file_lock);
		}
		spin_lock_bh(&dev->device_lock);
		heci_flush_queues(dev, file_ext);
		DBG("remove client host client = %d, ME client = %d\n",
		    file_ext->host_client_id,
		    file_ext->me_client_id);

		if (dev->open_handle_count > 0) {
			dev->heci_host_clients[file_ext->host_client_id / 8] &=
			~(1 << (file_ext->host_client_id % 8));
			dev->open_handle_count--;
		}
		heci_remove_client_from_file_list(dev,
				file_ext->host_client_id);

		/* free read cb */
		if (file_ext->read_cb != NULL) {
			priv_cb = find_read_list_entry(dev, file_ext);
			/* Remove entry from read list */
			if (priv_cb != NULL)
				list_del(&priv_cb->cb_list);

			priv_cb = file_ext->read_cb;
			file_ext->read_cb = NULL;
		}

		spin_unlock_bh(&dev->device_lock);
		file->private_data = NULL;
		spin_unlock(&file_ext->file_lock);

		if (priv_cb != NULL)
			heci_free_cb_private(priv_cb);

		kfree(file_ext);
	} else {
		spin_lock_bh(&dev->device_lock);

		if (dev->open_handle_count > 0)
			dev->open_handle_count--;

		if (dev->iamthif_file_object == file
		    && dev->iamthif_state != HECI_IAMTHIF_IDLE) {
			DBG("pthi canceled iamthif state %d\n",
			    dev->iamthif_state);
			dev->iamthif_canceled = 1;
			if (dev->iamthif_state == HECI_IAMTHIF_READ_COMPLETE) {
				DBG("run next pthi iamthif cb\n");
				run_next_iamthif_cmd(dev);
			}
		}

		if (heci_clear_lists(dev, file))
			dev->iamthif_state = HECI_IAMTHIF_IDLE;

		spin_unlock_bh(&dev->device_lock);
	}
	return rets;
}

static struct heci_cb_private *find_read_list_entry(
		struct iamt_heci_device *dev,
		struct heci_file_private *file_ext)
{
	struct heci_cb_private *priv_cb_pos = NULL;
	struct heci_cb_private *priv_cb_next = NULL;
	struct heci_file_private *file_ext_list_temp;

	if (dev->read_list.status == 0
	    && !list_empty(&dev->read_list.heci_cb.cb_list)) {
		DBG("remove read_list CB \n");
		list_for_each_entry_safe(priv_cb_pos,
				priv_cb_next,
				&dev->read_list.heci_cb.cb_list, cb_list) {

			file_ext_list_temp = (struct heci_file_private *)
				priv_cb_pos->file_private;

			if ((file_ext_list_temp != NULL) &&
			    heci_fe_same_id(file_ext, file_ext_list_temp))
				return priv_cb_pos;

		}
	}
	return NULL;
}

/**
 * heci_read - the read client message function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * returns >=0 data length on success , <0 on error
 */
static ssize_t heci_read(struct file *file, char __user *ubuf,
			 size_t length, loff_t *offset)
{
	int i;
	int rets = 0, err = 0;
	int if_num = iminor(file->f_dentry->d_inode);
	struct heci_file_private *file_ext = file->private_data;
	struct heci_cb_private *priv_cb_pos = NULL;
	struct heci_cb_private *priv_cb = NULL;
	struct iamt_heci_device *dev;

	if (!heci_device)
		return -ENODEV;

	dev = pci_get_drvdata(heci_device);
	if ((if_num != HECI_MINOR_NUMBER) || (!dev) || (!file_ext))
		return -ENODEV;

	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state != HECI_ENABLED) {
		spin_unlock_bh(&dev->device_lock);
		return -ENODEV;
	}
	spin_unlock_bh(&dev->device_lock);

	spin_lock(&file_ext->file_lock);
	if ((file_ext->sm_state & HECI_WD_STATE_INDEPENDENCE_MSG_SENT) == 0) {
		spin_unlock(&file_ext->file_lock);
		/* Do not allow to read watchdog client */
		for (i = 0; i < dev->num_heci_me_clients; i++) {
			if (memcmp(&heci_wd_guid,
				   &dev->me_clients[i].props.protocol_name,
				   sizeof(struct guid)) == 0) {
				if (file_ext->me_client_id ==
				    dev->me_clients[i].client_id)
					return -EBADF;
			}
		}
	} else {
		file_ext->sm_state &= ~HECI_WD_STATE_INDEPENDENCE_MSG_SENT;
		spin_unlock(&file_ext->file_lock);
	}

	if (file_ext == &dev->iamthif_file_ext) {
		rets = pthi_read(dev, if_num, file, ubuf, length, offset);
		goto out;
	}

	if (file_ext->read_cb && file_ext->read_cb->information > *offset) {
		priv_cb = file_ext->read_cb;
		goto copy_buffer;
	} else if (file_ext->read_cb && file_ext->read_cb->information > 0 &&
		   file_ext->read_cb->information <= *offset) {
		priv_cb = file_ext->read_cb;
		rets = 0;
		goto free;
	} else if ((!file_ext->read_cb || file_ext->read_cb->information == 0)
		    && *offset > 0) {
		/*Offset needs to be cleaned for contingous reads*/
		*offset = 0;
		rets = 0;
		goto out;
	}

	err = heci_start_read(dev, if_num, file_ext);
	spin_lock_bh(&file_ext->read_io_lock);
	if (err != 0 && err != -EBUSY) {
		DBG("heci start read failure with status = %d\n", err);
		spin_unlock_bh(&file_ext->read_io_lock);
		rets = err;
		goto out;
	}
	if (HECI_READ_COMPLETE != file_ext->reading_state
			&& !waitqueue_active(&file_ext->rx_wait)) {
		if (file->f_flags & O_NONBLOCK) {
			rets = -EAGAIN;
			spin_unlock_bh(&file_ext->read_io_lock);
			goto out;
		}
		spin_unlock_bh(&file_ext->read_io_lock);

		if (wait_event_interruptible(file_ext->rx_wait,
			(HECI_READ_COMPLETE == file_ext->reading_state
			 || HECI_FILE_INITIALIZING == file_ext->state
			 || HECI_FILE_DISCONNECTED == file_ext->state
			 || HECI_FILE_DISCONNECTING == file_ext->state))) {
			if (signal_pending(current)) {
				rets = -EINTR;
				goto out;
			}
			return -ERESTARTSYS;
		}

		if (HECI_FILE_INITIALIZING == file_ext->state ||
		    HECI_FILE_DISCONNECTED == file_ext->state ||
		    HECI_FILE_DISCONNECTING == file_ext->state) {
			rets = -EBUSY;
			goto out;
		}
		spin_lock_bh(&file_ext->read_io_lock);
	}

	priv_cb = file_ext->read_cb;

	if (!priv_cb) {
		spin_unlock_bh(&file_ext->read_io_lock);
		return -ENODEV;
	}
	if (file_ext->reading_state != HECI_READ_COMPLETE) {
		spin_unlock_bh(&file_ext->read_io_lock);
		return 0;
	}
	spin_unlock_bh(&file_ext->read_io_lock);
	/* now copy the data to user space */
copy_buffer:
	DBG("priv_cb->response_buffer size - %d\n",
	    priv_cb->response_buffer.size);
	DBG("priv_cb->information - %lu\n",
	    priv_cb->information);
	if (length == 0 || ubuf == NULL ||
	    *offset > priv_cb->information) {
		rets = -EMSGSIZE;
		goto free;
	}

	/* length is being turncated to PAGE_SIZE, however, */
	/* information size may be longer */
	length = (length < (priv_cb->information - *offset) ?
			length : (priv_cb->information - *offset));

	if (copy_to_user(ubuf,
			 priv_cb->response_buffer.data + *offset,
			 length)) {
		rets = -EFAULT;
		goto free;
	}

	rets = length;
	*offset += length;
	if ((unsigned long)*offset < priv_cb->information)
		goto out;

free:
	spin_lock_bh(&dev->device_lock);
	priv_cb_pos = find_read_list_entry(dev, file_ext);
	/* Remove entry from read list */
	if (priv_cb_pos != NULL)
		list_del(&priv_cb_pos->cb_list);
	spin_unlock_bh(&dev->device_lock);
	heci_free_cb_private(priv_cb);
	spin_lock_bh(&file_ext->read_io_lock);
	file_ext->reading_state = HECI_IDLE;
	file_ext->read_cb = NULL;
	file_ext->read_pending = 0;
	spin_unlock_bh(&file_ext->read_io_lock);
out:	DBG("end heci read rets= %d\n", rets);
	return rets;
}

/**
 * heci_write - the write function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * returns >=0 data length on success , <0 on error
 */
static ssize_t heci_write(struct file *file, const char __user *ubuf,
			  size_t length, loff_t *offset)
{
	int rets = 0;
	__u8 i;
	int if_num = iminor(file->f_dentry->d_inode);
	struct heci_file_private *file_ext = file->private_data;
	struct heci_cb_private *priv_write_cb = NULL;
	struct heci_msg_hdr heci_hdr;
	struct iamt_heci_device *dev;
	unsigned long currtime = get_seconds();

	if (!heci_device)
		return -ENODEV;

	dev = pci_get_drvdata(heci_device);

	if ((if_num != HECI_MINOR_NUMBER) || (!dev) || (!file_ext))
		return -ENODEV;

	spin_lock_bh(&dev->device_lock);

	if (dev->heci_state != HECI_ENABLED) {
		spin_unlock_bh(&dev->device_lock);
		return -ENODEV;
	}
	if (file_ext == &dev->iamthif_file_ext) {
		priv_write_cb = find_pthi_read_list_entry(dev, file);
		if ((priv_write_cb != NULL) &&
		     (((currtime - priv_write_cb->read_time) >
			    IAMTHIF_READ_TIMER) ||
		      (file_ext->reading_state == HECI_READ_COMPLETE))) {
			(*offset) = 0;
			list_del(&priv_write_cb->cb_list);
			heci_free_cb_private(priv_write_cb);
			priv_write_cb = NULL;
		}
	}

	/* free entry used in read */
	if (file_ext->reading_state == HECI_READ_COMPLETE) {
		*offset = 0;
		priv_write_cb = find_read_list_entry(dev, file_ext);
		if (priv_write_cb != NULL) {
			list_del(&priv_write_cb->cb_list);
			heci_free_cb_private(priv_write_cb);
			priv_write_cb = NULL;
			spin_lock_bh(&file_ext->read_io_lock);
			file_ext->reading_state = HECI_IDLE;
			file_ext->read_cb = NULL;
			file_ext->read_pending = 0;
			spin_unlock_bh(&file_ext->read_io_lock);
		}
	} else if (file_ext->reading_state == HECI_IDLE &&
			file_ext->read_pending == 0)
		(*offset) = 0;

	spin_unlock_bh(&dev->device_lock);

	priv_write_cb = kzalloc(sizeof(struct heci_cb_private), GFP_KERNEL);
	if (!priv_write_cb)
		return -ENOMEM;

	priv_write_cb->file_object = file;
	priv_write_cb->file_private = file_ext;
	priv_write_cb->request_buffer.data = kmalloc(length, GFP_KERNEL);
	if (!priv_write_cb->request_buffer.data) {
		kfree(priv_write_cb);
		return -ENOMEM;
	}
	DBG("length =%d\n", (int) length);

	if (copy_from_user(priv_write_cb->request_buffer.data,
		ubuf, length)) {
		rets = -EFAULT;
		goto fail;
	}

	spin_lock(&file_ext->file_lock);
	file_ext->sm_state = 0;
	if ((length == 4) &&
	    ((memcmp(heci_wd_state_independence_msg[0],
				 priv_write_cb->request_buffer.data, 4) == 0) ||
	     (memcmp(heci_wd_state_independence_msg[1],
				 priv_write_cb->request_buffer.data, 4) == 0) ||
	     (memcmp(heci_wd_state_independence_msg[2],
				 priv_write_cb->request_buffer.data, 4) == 0)))
		file_ext->sm_state |= HECI_WD_STATE_INDEPENDENCE_MSG_SENT;
	spin_unlock(&file_ext->file_lock);

	INIT_LIST_HEAD(&priv_write_cb->cb_list);
	if (file_ext == &dev->iamthif_file_ext) {
		priv_write_cb->response_buffer.data =
		    kmalloc(IAMTHIF_MTU, GFP_KERNEL);
		if (!priv_write_cb->response_buffer.data) {
			rets = -ENOMEM;
			goto fail;
		}
		spin_lock_bh(&dev->device_lock);
		if (dev->heci_state != HECI_ENABLED) {
			spin_unlock_bh(&dev->device_lock);
			rets = -ENODEV;
			goto fail;
		}
		for (i = 0; i < dev->num_heci_me_clients; i++) {
			if (dev->me_clients[i].client_id ==
				dev->iamthif_file_ext.me_client_id)
				break;
		}

		BUG_ON(dev->me_clients[i].client_id != file_ext->me_client_id);
		if ((i == dev->num_heci_me_clients) ||
		    (dev->me_clients[i].client_id !=
		      dev->iamthif_file_ext.me_client_id)) {

			spin_unlock_bh(&dev->device_lock);
			rets = -ENODEV;
			goto fail;
		} else if ((length > dev->me_clients[i].props.max_msg_length)
			    || (length <= 0)) {
			spin_unlock_bh(&dev->device_lock);
			rets = -EMSGSIZE;
			goto fail;
		}


		priv_write_cb->response_buffer.size = IAMTHIF_MTU;
		priv_write_cb->major_file_operations = HECI_IOCTL;
		priv_write_cb->information = 0;
		priv_write_cb->request_buffer.size = length;
		if (dev->iamthif_file_ext.state != HECI_FILE_CONNECTED) {
			spin_unlock_bh(&dev->device_lock);
			rets = -ENODEV;
			goto fail;
		}

		if (!list_empty(&dev->pthi_cmd_list.heci_cb.cb_list)
				|| dev->iamthif_state != HECI_IAMTHIF_IDLE) {
			DBG("pthi_state = %d\n", (int) dev->iamthif_state);
			DBG("add PTHI cb to pthi cmd waiting list\n");
			list_add_tail(&priv_write_cb->cb_list,
					&dev->pthi_cmd_list.heci_cb.cb_list);
			rets = length;
		} else {
			DBG("call pthi write\n");
			rets = pthi_write(dev, priv_write_cb);

			if (rets != 0) {
				DBG("pthi write failed with status = %d\n",
				    rets);
				spin_unlock_bh(&dev->device_lock);
				goto fail;
			}
			rets = length;
		}
		spin_unlock_bh(&dev->device_lock);
		return rets;
	}

	priv_write_cb->major_file_operations = HECI_WRITE;
	/* make sure information is zero before we start */

	priv_write_cb->information = 0;
	priv_write_cb->request_buffer.size = length;

	spin_lock(&file_ext->write_io_lock);
	DBG("host client = %d, ME client = %d\n",
	    file_ext->host_client_id, file_ext->me_client_id);
	if (file_ext->state != HECI_FILE_CONNECTED) {
		rets = -ENODEV;
		DBG("host client = %d,  is not connected to ME client = %d",
		    file_ext->host_client_id,
		    file_ext->me_client_id);

		goto unlock;
	}
	for (i = 0; i < dev->num_heci_me_clients; i++) {
		if (dev->me_clients[i].client_id ==
		    file_ext->me_client_id)
			break;
	}
	BUG_ON(dev->me_clients[i].client_id != file_ext->me_client_id);
	if (i == dev->num_heci_me_clients) {
		rets = -ENODEV;
		goto unlock;
	}
	if (length > dev->me_clients[i].props.max_msg_length || length <= 0) {
		rets = -EINVAL;
		goto unlock;
	}
	priv_write_cb->file_private = file_ext;

	spin_lock_bh(&dev->device_lock);
	if (flow_ctrl_creds(dev, file_ext) &&
		dev->host_buffer_is_empty) {
		spin_unlock_bh(&dev->device_lock);
		dev->host_buffer_is_empty = 0;
		if (length > ((((dev->host_hw_state & H_CBD) >> 24) *
			sizeof(__u32)) - sizeof(struct heci_msg_hdr))) {

			heci_hdr.length =
				(((dev->host_hw_state & H_CBD) >> 24) *
				sizeof(__u32)) -
				sizeof(struct heci_msg_hdr);
			heci_hdr.msg_complete = 0;
		} else {
			heci_hdr.length = length;
			heci_hdr.msg_complete = 1;
		}
		heci_hdr.host_addr = file_ext->host_client_id;
		heci_hdr.me_addr = file_ext->me_client_id;
		heci_hdr.reserved = 0;
		DBG("call heci_write_message header=%08x.\n",
		    *((__u32 *) &heci_hdr));
		spin_unlock(&file_ext->write_io_lock);
		/*  protect heci low level write */
		spin_lock_bh(&dev->device_lock);
		if (!heci_write_message(dev, &heci_hdr,
			(unsigned char *) (priv_write_cb->request_buffer.data),
			heci_hdr.length)) {

			spin_unlock_bh(&dev->device_lock);
			heci_free_cb_private(priv_write_cb);
			rets = -ENODEV;
			priv_write_cb->information = 0;
			return rets;
		}
		file_ext->writing_state = HECI_WRITING;
		priv_write_cb->information = heci_hdr.length;
		if (heci_hdr.msg_complete) {
			flow_ctrl_reduce(dev, file_ext);
			list_add_tail(&priv_write_cb->cb_list,
				      &dev->write_waiting_list.heci_cb.cb_list);
		} else {
			list_add_tail(&priv_write_cb->cb_list,
				      &dev->write_list.heci_cb.cb_list);
		}
		spin_unlock_bh(&dev->device_lock);

	} else {

		spin_unlock_bh(&dev->device_lock);
		priv_write_cb->information = 0;
		file_ext->writing_state = HECI_WRITING;
		spin_unlock(&file_ext->write_io_lock);
		list_add_tail(&priv_write_cb->cb_list,
			      &dev->write_list.heci_cb.cb_list);
	}
	return length;

unlock:
	spin_unlock(&file_ext->write_io_lock);
fail:
	heci_free_cb_private(priv_write_cb);
	return rets;

}

/**
 * heci_ioctl - the IOCTL function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to heci message structure
 *
 * returns 0 on success , <0 on error
 */
static int heci_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long data)
{
	int rets = 0;
	int if_num = iminor(inode);
	struct heci_file_private *file_ext = file->private_data;
	/* in user space */
	struct heci_message_data __user *u_msg;
	struct heci_message_data k_msg;	/* all in kernel on the stack */
	struct iamt_heci_device *dev;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!heci_device)
		return -ENODEV;

	dev = pci_get_drvdata(heci_device);
	if ((if_num != HECI_MINOR_NUMBER) || (!dev) || (!file_ext))
		return -ENODEV;

	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state != HECI_ENABLED) {
		spin_unlock_bh(&dev->device_lock);
		return -ENODEV;
	}
	spin_unlock_bh(&dev->device_lock);

	/* first copy from user all data needed */
	u_msg = (struct heci_message_data __user *)data;
	if (copy_from_user(&k_msg, u_msg, sizeof(k_msg))) {
		DBG("first copy from user all data needed filled\n");
		return -EFAULT;
	}
	DBG("user message size is %d\n", k_msg.size);

	switch (cmd) {
	case IOCTL_HECI_GET_VERSION:
		DBG(": IOCTL_HECI_GET_VERSION\n");
		rets = heci_ioctl_get_version(dev, if_num, u_msg, k_msg,
					      file_ext);
		break;

	case IOCTL_HECI_CONNECT_CLIENT:
		DBG(": IOCTL_HECI_CONNECT_CLIENT.\n");
		rets = heci_ioctl_connect_client(dev, if_num, u_msg, k_msg,
						 file);
		break;

	case IOCTL_HECI_WD:
		DBG(": IOCTL_HECI_WD.\n");
		rets = heci_ioctl_wd(dev, if_num, k_msg, file_ext);
		break;

	case IOCTL_HECI_BYPASS_WD:
		DBG(": IOCTL_HECI_BYPASS_WD.\n");
		rets = heci_ioctl_bypass_wd(dev, if_num, k_msg, file_ext);
		break;

	default:
		rets = -EINVAL;
		break;
	}
	return rets;
}

/**
 * heci_poll - the poll function
 *
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * returns poll mask
 */
static unsigned int heci_poll(struct file *file, poll_table *wait)
{
	int if_num = iminor(file->f_dentry->d_inode);
	unsigned int mask = 0;
	struct heci_file_private *file_ext = file->private_data;
	struct iamt_heci_device *dev;

	if (!heci_device)
		return mask;

	dev = pci_get_drvdata(heci_device);

	if ((if_num != HECI_MINOR_NUMBER) || (!dev) || (!file_ext))
		return mask;

	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state != HECI_ENABLED) {
		spin_unlock_bh(&dev->device_lock);
		return mask;
	}
	spin_unlock_bh(&dev->device_lock);

	if (file_ext == &dev->iamthif_file_ext) {
		poll_wait(file, &dev->iamthif_file_ext.wait, wait);
		spin_lock(&dev->iamthif_file_ext.file_lock);
		if (dev->iamthif_state == HECI_IAMTHIF_READ_COMPLETE
		    && dev->iamthif_file_object == file) {
			mask |= (POLLIN | POLLRDNORM);
			spin_lock_bh(&dev->device_lock);
			DBG("run next pthi cb\n");
			run_next_iamthif_cmd(dev);
			spin_unlock_bh(&dev->device_lock);
		}
		spin_unlock(&dev->iamthif_file_ext.file_lock);

	} else{
		poll_wait(file, &file_ext->tx_wait, wait);
		spin_lock(&file_ext->write_io_lock);
		if (HECI_WRITE_COMPLETE == file_ext->writing_state)
			mask |= (POLLIN | POLLRDNORM);

		spin_unlock(&file_ext->write_io_lock);
	}

	return mask;
}

#ifdef CONFIG_PM
static int heci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct iamt_heci_device *dev = pci_get_drvdata(pdev);
	int err = 0;

	spin_lock_bh(&dev->device_lock);
	if (dev->reinit_tsk != NULL) {
		kthread_stop(dev->reinit_tsk);
		dev->reinit_tsk = NULL;
	}
	spin_unlock_bh(&dev->device_lock);

	/* Stop watchdog if exists */
	del_timer_sync(&dev->wd_timer);
	if (dev->wd_file_ext.state == HECI_FILE_CONNECTED
	    && dev->wd_timeout) {
		spin_lock_bh(&dev->device_lock);
		g_sus_wd_timeout = dev->wd_timeout;
		dev->wd_timeout = 0;
		dev->wd_due_counter = 0;
		memcpy(dev->wd_data, heci_stop_wd_params,
					HECI_WD_PARAMS_SIZE);
		dev->stop = 1;
		if (dev->host_buffer_is_empty &&
		    flow_ctrl_creds(dev, &dev->wd_file_ext)) {
			dev->host_buffer_is_empty = 0;
			if (!heci_send_wd(dev))
				DBG("send stop WD failed\n");
			else
				flow_ctrl_reduce(dev, &dev->wd_file_ext);

			dev->wd_pending = 0;
		} else {
			dev->wd_pending = 1;
		}
		spin_unlock_bh(&dev->device_lock);
		dev->wd_stoped = 0;

		err = wait_event_interruptible_timeout(dev->wait_stop_wd,
						       (dev->wd_stoped),
						       10 * HZ);
		if (!dev->wd_stoped)
			DBG("stop wd failed to complete.\n");
		else {
			DBG("stop wd complete %d.\n", err);
			err = 0;
		}
	}
	/* Set new heci state */
	spin_lock_bh(&dev->device_lock);
	if (dev->heci_state == HECI_ENABLED ||
	    dev->heci_state == HECI_RECOVERING_FROM_RESET) {
		dev->heci_state = HECI_POWER_DOWN;
		heci_reset(dev, 0);
	}
	spin_unlock_bh(&dev->device_lock);

	pci_save_state(pdev);

	pci_disable_device(pdev);
	free_irq(pdev->irq, dev);

	pci_set_power_state(pdev, PCI_D3hot);

	return err;
}

static int heci_resume(struct pci_dev *pdev)
{
	struct iamt_heci_device *dev;
	int err = 0;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	/* request and enable interrupt   */
	err = request_irq(pdev->irq, heci_isr_interrupt, IRQF_SHARED,
			heci_driver_name, dev);
	if (err) {
		printk(KERN_ERR "heci: Request_irq failure. irq = %d \n",
		       pdev->irq);
		return err;
	}

	spin_lock_bh(&dev->device_lock);
	dev->heci_state = HECI_POWER_UP;
	heci_reset(dev, 1);
	spin_unlock_bh(&dev->device_lock);

	/* Start watchdog if stopped in suspend */
	if (g_sus_wd_timeout != 0) {
		dev->wd_timeout = g_sus_wd_timeout;

		memcpy(dev->wd_data, heci_start_wd_params,
					HECI_WD_PARAMS_SIZE);
		memcpy(dev->wd_data + HECI_WD_PARAMS_SIZE,
		       &dev->wd_timeout, sizeof(__u16));
		dev->wd_due_counter = 1;

		if (dev->wd_timeout)
			mod_timer(&dev->wd_timer, jiffies);

		g_sus_wd_timeout = 0;
	}
	return err;
}
#endif

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Management Engine Interface");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(HECI_DRIVER_VERSION);

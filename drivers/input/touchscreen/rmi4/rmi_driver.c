/*
 * Copyright (c) 2011 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This driver adds support for generic RMI4 devices from Synpatics. It
 * implements the mandatory f01 RMI register and depends on the presence of
 * other required RMI functions.
 *
 * The RMI4 specification can be found here (URL split after files/ for
 * style reasons):
 * http://www.synaptics.com/sites/default/files/
 *           511-000136-01-Rev-E-RMI4%20Intrfacing%20Guide.pdf
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/rmi.h>
#include "rmi_driver.h"
#ifdef CONFIG_RMI4_DEBUG
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#endif

#define REGISTER_DEBUG 0

#define PDT_END_SCAN_LOCATION	0x0005
#define PDT_PROPERTIES_LOCATION 0x00EF
#define BSR_LOCATION 0x00FE
#define HAS_BSR_MASK 0x20
#define HAS_NONSTANDARD_PDT_MASK 0x40
#define RMI4_END_OF_PDT(id) ((id) == 0x00 || (id) == 0xff)
#define RMI4_MAX_PAGE 0xff
#define RMI4_PAGE_SIZE 0x100

#define RMI_DEVICE_RESET_CMD	0x01
#define DEFAULT_RESET_DELAY_MS	20

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rmi_driver_early_suspend(struct early_suspend *h);
static void rmi_driver_late_resume(struct early_suspend *h);
#endif

/* sysfs files for attributes for driver values. */
static ssize_t rmi_driver_bsr_show(struct device *dev,
				   struct device_attribute *attr, char *buf);

static ssize_t rmi_driver_bsr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count);

static ssize_t rmi_driver_enabled_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);

static ssize_t rmi_driver_enabled_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);

#if REGISTER_DEBUG
static ssize_t rmi_driver_reg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);
#endif

#ifdef CONFIG_RMI4_DEBUG

struct driver_debugfs_data {
	bool done;
	struct rmi_device *rmi_dev;
};

static int debug_open(struct inode *inodep, struct file *filp) {
	struct driver_debugfs_data *data;

	data = kzalloc(sizeof(struct driver_debugfs_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rmi_dev = inodep->i_private;
	filp->private_data = data;
	return 0;
}

static int debug_release(struct inode *inodep, struct file *filp) {
	kfree(filp->private_data);
	return 0;
}

#ifdef CONFIG_RMI4_SPI
#define DELAY_NAME "delay"

static ssize_t delay_read(struct file *filp, char __user *buffer, size_t size,
		    loff_t *offset) {
	struct driver_debugfs_data *data = filp->private_data;
	struct rmi_device_platform_data *pdata =
			data->rmi_dev->phys->dev->platform_data;
	int retval;
	char local_buf[size];

	if (data->done)
		return 0;

	data->done = 1;

	retval = snprintf(local_buf, size, "%d %d %d %d %d\n",
		pdata->spi_data.read_delay_us, pdata->spi_data.write_delay_us,
		pdata->spi_data.block_delay_us,
		pdata->spi_data.pre_delay_us, pdata->spi_data.post_delay_us);

	if (retval <= 0 || copy_to_user(buffer, local_buf, retval))
		return -EFAULT;

	return retval;
}

static ssize_t delay_write(struct file *filp, const char __user *buffer,
			   size_t size, loff_t *offset) {
	struct driver_debugfs_data *data = filp->private_data;
	struct rmi_device_platform_data *pdata =
			data->rmi_dev->phys->dev->platform_data;
	int retval;
	char local_buf[size];
	unsigned int new_read_delay;
	unsigned int new_write_delay;
	unsigned int new_block_delay;
	unsigned int new_pre_delay;
	unsigned int new_post_delay;

	retval = copy_from_user(local_buf, buffer, size);
	if (retval)
		return -EFAULT;

	retval = sscanf(local_buf, "%u %u %u %u %u", &new_read_delay,
			&new_write_delay, &new_block_delay,
			&new_pre_delay, &new_post_delay);
	if (retval != 5) {
		dev_err(&data->rmi_dev->dev,
			"Incorrect number of values provided for delay.");
		return -EINVAL;
	}
	if (new_read_delay < 0) {
		dev_err(&data->rmi_dev->dev,
			"Byte delay must be positive microseconds.\n");
		return -EINVAL;
	}
	if (new_write_delay < 0) {
		dev_err(&data->rmi_dev->dev,
			"Write delay must be positive microseconds.\n");
		return -EINVAL;
	}
	if (new_block_delay < 0) {
		dev_err(&data->rmi_dev->dev,
			"Block delay must be positive microseconds.\n");
		return -EINVAL;
	}
	if (new_pre_delay < 0) {
		dev_err(&data->rmi_dev->dev,
			"Pre-transfer delay must be positive microseconds.\n");
		return -EINVAL;
	}
	if (new_post_delay < 0) {
		dev_err(&data->rmi_dev->dev,
			"Post-transfer delay must be positive microseconds.\n");
		return -EINVAL;
	}

	dev_dbg(&data->rmi_dev->dev,
		 "Setting delays to %u %u %u %u %u.\n", new_read_delay,
		 new_write_delay, new_block_delay, new_pre_delay,
		 new_post_delay);
	pdata->spi_data.read_delay_us = new_read_delay;
	pdata->spi_data.write_delay_us = new_write_delay;
	pdata->spi_data.block_delay_us = new_block_delay;
	pdata->spi_data.pre_delay_us = new_pre_delay;
	pdata->spi_data.post_delay_us = new_post_delay;

	return size;
}

static struct file_operations delay_fops = {
	.owner = THIS_MODULE,
	.open = debug_open,
	.release = debug_release,
	.read = delay_read,
	.write = delay_write,
};
#endif /* CONFIG_RMI4_SPI */

#define PHYS_NAME "phys"

static ssize_t phys_read(struct file *filp, char __user *buffer, size_t size,
		    loff_t *offset) {
	struct driver_debugfs_data *data = filp->private_data;
	struct rmi_phys_info *info = &data->rmi_dev->phys->info;
	int retval;
	char local_buf[size];

	if (data->done)
		return 0;

	data->done = 1;

	retval = snprintf(local_buf, PAGE_SIZE,
		"%-5s %ld %ld %ld %ld %ld %ld %ld\n",
		 info->proto ? info->proto : "unk",
		 info->tx_count, info->tx_bytes, info->tx_errs,
		 info->rx_count, info->rx_bytes, info->rx_errs,
		 info->attn_count);
	if (retval <= 0 || copy_to_user(buffer, local_buf, retval))
		return -EFAULT;

	return retval;
}

static struct file_operations phys_fops = {
	.owner = THIS_MODULE,
	.open = debug_open,
	.release = debug_release,
	.read = phys_read,
};

static int setup_debugfs(struct rmi_device *rmi_dev) {
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
#ifdef CONFIG_RMI4_SPI
	struct rmi_phys_info *info = &rmi_dev->phys->info;
#endif
	int retval = 0;

	if (!rmi_dev->debugfs_root)
		return -ENODEV;

#ifdef CONFIG_RMI4_SPI
	if (!strncmp("spi", info->proto, 3)) {
		data->debugfs_delay = debugfs_create_file(DELAY_NAME, RMI_RW_ATTR,
					rmi_dev->debugfs_root, rmi_dev, &delay_fops);
		if (!data->debugfs_delay || IS_ERR(data->debugfs_delay)) {
			dev_warn(&rmi_dev->dev, "Failed to create debugfs delay.\n");
			data->debugfs_delay = NULL;
		}
	}
#endif

	data->debugfs_phys = debugfs_create_file(PHYS_NAME, RMI_RO_ATTR,
				rmi_dev->debugfs_root, rmi_dev, &phys_fops);
	if (!data->debugfs_phys || IS_ERR(data->debugfs_phys)) {
		dev_warn(&rmi_dev->dev, "Failed to create debugfs phys.\n");
		data->debugfs_phys = NULL;
	}

	return retval;
}

static void teardown_debugfs(struct rmi_device *rmi_dev) {
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

#ifdef CONFIG_RMI4_SPI
	if (!data->debugfs_delay)
		debugfs_remove(data->debugfs_delay);
#endif
	if (!data->debugfs_phys)
		debugfs_remove(data->debugfs_phys);
}
#endif

static int rmi_driver_process_reset_requests(struct rmi_device *rmi_dev);

static int rmi_driver_process_config_requests(struct rmi_device *rmi_dev);

static int rmi_driver_irq_restore(struct rmi_device *rmi_dev);

static struct device_attribute attrs[] = {
	__ATTR(enabled, RMI_RW_ATTR,
	       rmi_driver_enabled_show, rmi_driver_enabled_store),
#if REGISTER_DEBUG
	__ATTR(reg, RMI_WO_ATTR,
	       rmi_show_error, rmi_driver_reg_store),
#endif
};

static struct device_attribute bsr_attribute = __ATTR(bsr, RMI_RW_ATTR,
	       rmi_driver_bsr_show, rmi_driver_bsr_store);

/* Useful helper functions for u8* */

void u8_set_bit(u8 *target, int pos)
{
	target[pos/8] |= 1<<pos%8;
}

void u8_clear_bit(u8 *target, int pos)
{
	target[pos/8] &= ~(1<<pos%8);
}

bool u8_is_set(u8 *target, int pos)
{
	return target[pos/8] & 1<<pos%8;
}

bool u8_is_any_set(u8 *target, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (target[i])
			return true;
	}
	return false;
}

void u8_or(u8 *dest, u8 *target1, u8 *target2, int size)
{
	int i;
	for (i = 0; i < size; i++)
		dest[i] = target1[i] | target2[i];
}

void u8_and(u8 *dest, u8 *target1, u8 *target2, int size)
{
	int i;
	for (i = 0; i < size; i++)
		dest[i] = target1[i] & target2[i];
}

static bool has_bsr(struct rmi_driver_data *data)
{
	return (data->pdt_props & HAS_BSR_MASK) != 0;
}

/* Utility routine to set bits in a register. */
int rmi_set_bits(struct rmi_device *rmi_dev, unsigned short address,
		 unsigned char bits)
{
	unsigned char reg_contents;
	int retval;

	retval = rmi_read_block(rmi_dev, address, &reg_contents, 1);
	if (retval)
		return retval;
	reg_contents = reg_contents | bits;
	retval = rmi_write_block(rmi_dev, address, &reg_contents, 1);
	if (retval == 1)
		return 0;
	else if (retval == 0)
		return -EIO;
	return retval;
}
EXPORT_SYMBOL(rmi_set_bits);

/* Utility routine to clear bits in a register. */
int rmi_clear_bits(struct rmi_device *rmi_dev, unsigned short address,
		   unsigned char bits)
{
	unsigned char reg_contents;
	int retval;

	retval = rmi_read_block(rmi_dev, address, &reg_contents, 1);
	if (retval)
		return retval;
	reg_contents = reg_contents & ~bits;
	retval = rmi_write_block(rmi_dev, address, &reg_contents, 1);
	if (retval == 1)
		return 0;
	else if (retval == 0)
		return -EIO;
	return retval;
}
EXPORT_SYMBOL(rmi_clear_bits);

static void rmi_free_function_list(struct rmi_device *rmi_dev)
{
	struct rmi_function_container *entry, *n;
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	if (!data) {
		dev_err(&rmi_dev->dev, "WTF: No driver data in %s\n", __func__);
		return;
	}

	if (data->f01_container) {
		if (data->f01_container->fh && data->f01_container->fh->remove) {
			data->f01_container->fh->remove(data->f01_container);
		}
		device_unregister(&data->f01_container->dev);
		kfree(data->f01_container->irq_mask);
		kfree(data->f01_container);
		data->f01_container = NULL;
	}

	if (list_empty(&data->rmi_functions.list))
		return;

	list_for_each_entry_safe(entry, n, &data->rmi_functions.list, list) {
		if (entry->fh) {
			if (entry->fh->remove)
				entry->fh->remove(entry);
			device_unregister(&entry->dev);
		}
		kfree(entry->irq_mask);
		list_del(&entry->list);
		kfree(entry);
	}
}

static void no_op(struct device *dev)
{
	dev_dbg(dev, "REMOVING KOBJ!");
	kobject_put(&dev->kobj);
}

static int init_one_function(struct rmi_device *rmi_dev,
			     struct rmi_function_container *fc)
{
	int retval;

	if (!fc->fh) {
		struct rmi_function_handler *fh =
			rmi_get_function_handler(fc->fd.function_number);
		if (!fh) {
			dev_dbg(&rmi_dev->dev, "No handler for F%02X.\n",
				fc->fd.function_number);
			return 0;
		}
		fc->fh = fh;
	}

	if (!fc->fh->init)
		return 0;
	/* This memset might not be what we want to do... */
	memset(&(fc->dev), 0, sizeof(struct device));
	dev_set_name(&(fc->dev), "fn%02x", fc->fd.function_number);
	fc->dev.release = no_op;

	fc->dev.parent = &rmi_dev->dev;
	dev_dbg(&rmi_dev->dev, "%s: Register F%02X.\n", __func__,
			fc->fd.function_number);
	retval = device_register(&fc->dev);
	if (retval) {
		dev_err(&rmi_dev->dev, "Failed device_register for F%02X.\n",
			fc->fd.function_number);
		return retval;
	}

	retval = fc->fh->init(fc);
	if (retval < 0) {
		dev_err(&rmi_dev->dev, "Failed to initialize function F%02x\n",
			fc->fd.function_number);
		goto error_exit;
	}

	return 0;

error_exit:
	device_unregister(&fc->dev);
	return retval;
}

static void rmi_driver_fh_add(struct rmi_device *rmi_dev,
			      struct rmi_function_handler *fh)
{
	struct rmi_function_container *entry;
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	if (!data)
		return;
	if (fh->func == 0x01) {
		if (data->f01_container)
			data->f01_container->fh = fh;
	} else if (!list_empty(&data->rmi_functions.list)) {
		mutex_lock(&data->pdt_mutex);
		list_for_each_entry(entry, &data->rmi_functions.list, list)
			if (entry->fd.function_number == fh->func) {
				entry->fh = fh;
				if (init_one_function(rmi_dev, entry) < 0)
					entry->fh = NULL;
			}
		mutex_unlock(&data->pdt_mutex);
	}

}

static void rmi_driver_fh_remove(struct rmi_device *rmi_dev,
				 struct rmi_function_handler *fh)
{
	struct rmi_function_container *entry, *temp;
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	if (fh->func == 0x01) {
		/* don't call remove here since
		 * rmi_f01_initialize just get call one time */
		if (data->f01_container)
			data->f01_container->fh = NULL;
		return;
	}

	list_for_each_entry_safe(entry, temp, &data->rmi_functions.list,
									list) {
		if (entry->fh && entry->fd.function_number == fh->func) {
			if (fh->remove)
				fh->remove(entry);

			entry->fh = NULL;
			device_unregister(&entry->dev);
		}
	}
}

static int rmi_driver_process_reset_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct device *dev = &rmi_dev->dev;
	struct rmi_function_container *entry;
	int retval;

	/* Device control (F01) is handled before anything else. */

	if (data->f01_container && data->f01_container->fh &&
			data->f01_container->fh->reset) {
		retval = data->f01_container->fh->reset(data->f01_container);
		if (retval < 0) {
			dev_err(dev, "F%02x reset handler failed: %d.\n",
				data->f01_container->fh->func, retval);
			return retval;
		}
	}

	if (list_empty(&data->rmi_functions.list))
		return 0;

	list_for_each_entry(entry, &data->rmi_functions.list, list) {
		if (entry->fh && entry->fh->reset) {
			retval = entry->fh->reset(entry);
			if (retval < 0) {
				dev_err(dev, "F%02x reset handler failed: %d\n",
					entry->fh->func, retval);
				return retval;
			}
		}
	}

	return 0;
}

static int rmi_driver_process_config_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct device *dev = &rmi_dev->dev;
	struct rmi_function_container *entry;
	int retval;

	/* Device control (F01) is handled before anything else. */

	if (data->f01_container && data->f01_container->fh &&
			data->f01_container->fh->config) {
		retval = data->f01_container->fh->config(data->f01_container);
		if (retval < 0) {
			dev_err(dev, "F%02x config handler failed: %d.\n",
					data->f01_container->fh->func, retval);
			return retval;
		}
	}

	if (list_empty(&data->rmi_functions.list))
		return 0;

	list_for_each_entry(entry, &data->rmi_functions.list, list) {
		if (entry->fh && entry->fh->config) {
			retval = entry->fh->config(entry);
			if (retval < 0) {
				dev_err(dev, "F%02x config handler failed: %d.\n",
					entry->fh->func, retval);
				return retval;
			}
		}
	}

	return 0;
}

static void construct_mask(u8 *mask, int num, int pos)
{
	int i;

	for (i = 0; i < num; i++)
		u8_set_bit(mask, pos+i);
}

static int process_interrupt_requests(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct device *dev = &rmi_dev->dev;
	struct rmi_function_container *entry;
	u8 irq_status[data->num_of_irq_regs];
	u8 irq_bits[data->num_of_irq_regs];
	int error;

	error = rmi_read_block(rmi_dev,
				data->f01_container->fd.data_base_addr + 1,
				irq_status, data->num_of_irq_regs);
	if (error < 0) {
		dev_err(dev, "%s: failed to read irqs.", __func__);
		return error;
	}
	/* Device control (F01) is handled before anything else. */
	if (data->f01_container->irq_mask && data->f01_container->fh->attention) {
		u8_and(irq_bits, irq_status, data->f01_container->irq_mask,
				data->num_of_irq_regs);
		if (u8_is_any_set(irq_bits, data->num_of_irq_regs))
			data->f01_container->fh->attention(
					data->f01_container, irq_bits);
	}

	u8_and(irq_status, irq_status, data->current_irq_mask,
	       data->num_of_irq_regs);
	/* At this point, irq_status has all bits that are set in the
	 * interrupt status register and are enabled.
	 */

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->irq_mask && entry->fh && entry->fh->attention) {
			u8_and(irq_bits, irq_status, entry->irq_mask,
			       data->num_of_irq_regs);
			if (u8_is_any_set(irq_bits, data->num_of_irq_regs)) {
				error = entry->fh->attention(entry, irq_bits);
				if (error < 0)
					dev_err(dev, "%s: f%.2x"
						" attention handler failed:"
						" %d\n", __func__,
						entry->fh->func, error);
			}
		}

	return 0;
}

static int rmi_driver_irq_handler(struct rmi_device *rmi_dev, int irq)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	/* Can get called before the driver is fully ready to deal with
	 * interrupts.
	 */
	if (!data || !data->f01_container || !data->f01_container->fh) {
		dev_warn(&rmi_dev->dev,
			 "Not ready to handle interrupts yet!\n");
		return 0;
	}
	return process_interrupt_requests(rmi_dev);
}

static int rmi_driver_reset_handler(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	int error;

	/* Can get called before the driver is fully ready to deal with
	 * interrupts.
	 */
	if (!data || !data->f01_container || !data->f01_container->fh) {
		dev_warn(&rmi_dev->dev,
			 "Not ready to handle reset yet!\n");
		return 0;
	}

	error = rmi_driver_process_reset_requests(rmi_dev);
	if (error < 0)
		return error;


	error = rmi_driver_process_config_requests(rmi_dev);
	if (error < 0)
		return error;

	if (data->irq_stored) {
		error = rmi_driver_irq_restore(rmi_dev);
		if (error < 0)
			return error;
	}

	return 0;
}



/*
 * Construct a function's IRQ mask. This should
 * be called once and stored.
 */
static u8 *rmi_driver_irq_get_mask(struct rmi_device *rmi_dev,
				   struct rmi_function_container *fc) {
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	u8 *irq_mask = kzalloc(sizeof(u8) * data->num_of_irq_regs, GFP_KERNEL);
	if (irq_mask)
		construct_mask(irq_mask, fc->num_of_irqs, fc->irq_pos);

	return irq_mask;
}

/*
 * This pair of functions allows functions like function 54 to request to have
 * other interupts disabled until the restore function is called. Only one store
 * happens at a time.
 */
static int rmi_driver_irq_save(struct rmi_device *rmi_dev, u8 * new_ints)
{
	int retval = 0;
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct device *dev = &rmi_dev->dev;

	mutex_lock(&data->irq_mutex);
	if (!data->irq_stored) {
		/* Save current enabled interupts */
		retval = rmi_read_block(rmi_dev,
				data->f01_container->fd.control_base_addr+1,
				data->irq_mask_store, data->num_of_irq_regs);
		if (retval < 0) {
			dev_err(dev, "%s: Failed to read enabled interrupts!",
								__func__);
			goto error_unlock;
		}
		/*
		 * Disable every interupt except for function 54
		 * TODO:Will also want to not disable function 1-like functions.
		 * No need to take care of this now, since there's no good way
		 * to identify them.
		 */
		retval = rmi_write_block(rmi_dev,
				data->f01_container->fd.control_base_addr+1,
				new_ints, data->num_of_irq_regs);
		if (retval < 0) {
			dev_err(dev, "%s: Failed to change enabled interrupts!",
								__func__);
			goto error_unlock;
		}
		memcpy(data->current_irq_mask, new_ints,
					data->num_of_irq_regs * sizeof(u8));
		data->irq_stored = true;
	} else {
		retval = -ENOSPC; /* No space to store IRQs.*/
		dev_err(dev, "%s: Attempted to save values when"
						" already stored!", __func__);
	}

error_unlock:
	mutex_unlock(&data->irq_mutex);
	return retval;
}

static int rmi_driver_irq_restore(struct rmi_device *rmi_dev)
{
	int retval = 0;
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct device *dev = &rmi_dev->dev;
	mutex_lock(&data->irq_mutex);

	if (data->irq_stored) {
		retval = rmi_write_block(rmi_dev,
				data->f01_container->fd.control_base_addr+1,
				data->irq_mask_store, data->num_of_irq_regs);
		if (retval < 0) {
			dev_err(dev, "%s: Failed to write enabled interupts!",
								__func__);
			goto error_unlock;
		}
		memcpy(data->current_irq_mask, data->irq_mask_store,
					data->num_of_irq_regs * sizeof(u8));
		data->irq_stored = false;
	} else {
		retval = -EINVAL;
		dev_err(dev, "%s: Attempted to restore values when not stored!",
			__func__);
	}

error_unlock:
	mutex_unlock(&data->irq_mutex);
	return retval;
}

static int rmi_driver_fn_generic(struct rmi_device *rmi_dev,
				     struct pdt_entry *pdt_ptr,
				     int *current_irq_count,
				     u16 page_start)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct rmi_function_container *fc = NULL;
	int retval = 0;
	struct device *dev = &rmi_dev->dev;
	struct rmi_device_platform_data *pdata;

	pdata = to_rmi_platform_data(rmi_dev);

	dev_dbg(dev, "Initializing F%02X for %s.\n", pdt_ptr->function_number,
		pdata->sensor_name);

	fc = kzalloc(sizeof(struct rmi_function_container),
			GFP_KERNEL);
	if (!fc) {
		dev_err(dev, "Failed to allocate container for F%02X.\n",
			pdt_ptr->function_number);
		retval = -ENOMEM;
		goto error_free_data;
	}

	copy_pdt_entry_to_fd(pdt_ptr, &fc->fd, page_start);

	fc->rmi_dev = rmi_dev;
	fc->num_of_irqs = pdt_ptr->interrupt_source_count;
	fc->irq_pos = *current_irq_count;
	*current_irq_count += fc->num_of_irqs;

	retval = init_one_function(rmi_dev, fc);
	if (retval < 0) {
		dev_err(dev, "Failed to initialize F%.2x\n",
			pdt_ptr->function_number);
		goto error_free_data;
	}

	INIT_LIST_HEAD(&fc->list);
	list_add_tail(&fc->list, &data->rmi_functions.list);
	return 0;

error_free_data:
	kfree(fc);
	return retval;
}

/*
 * F01 was once handled very differently from all other functions.  It is
 * now only slightly special, and as the driver is refined we expect this
 * function to go away.
 */
static int rmi_driver_fn_01_specific(struct rmi_device *rmi_dev,
				     struct pdt_entry *pdt_ptr,
				     int *current_irq_count,
				     u16 page_start)
{
	struct rmi_driver_data *data = NULL;
	struct rmi_function_container *fc = NULL;
	union f01_device_status device_status;
	int retval = 0;
	struct device *dev = &rmi_dev->dev;
	struct rmi_function_handler *fh =
		rmi_get_function_handler(0x01);
	struct rmi_device_platform_data *pdata;

	pdata = to_rmi_platform_data(rmi_dev);
	data = rmi_get_driverdata(rmi_dev);

	retval = rmi_read(rmi_dev, pdt_ptr->data_base_addr, &device_status.reg);
	if (retval) {
		dev_err(dev, "Failed to read device status.\n");
		return retval;
	}

	dev_dbg(dev, "Initializing F01 for %s.\n", pdata->sensor_name);

	if (!fh)
		dev_dbg(dev, "%s: No function handler for F01?!", __func__);

	fc = kzalloc(sizeof(struct rmi_function_container), GFP_KERNEL);
	if (!fc) {
		retval = -ENOMEM;
		return retval;
	}

	copy_pdt_entry_to_fd(pdt_ptr, &fc->fd, page_start);
	fc->num_of_irqs = pdt_ptr->interrupt_source_count;
	fc->irq_pos = *current_irq_count;
	*current_irq_count += fc->num_of_irqs;

	fc->rmi_dev        = rmi_dev;
	fc->dev.parent     = &fc->rmi_dev->dev;
	fc->fh = fh;

	dev_set_name(&(fc->dev), "fn%02x", fc->fd.function_number);
	fc->dev.release = no_op;

	dev_dbg(dev, "%s: Register F01.\n", __func__);
	retval = device_register(&fc->dev);
	if (retval) {
		dev_err(dev, "%s: Failed device_register for F01.\n", __func__);
		goto error_free_data;
	}

	data->f01_container = fc;
	data->f01_bootloader_mode = device_status.flash_prog;
	if (device_status.flash_prog)
		dev_warn(dev, "WARNING: RMI4 device is in bootloader mode!\n");

	INIT_LIST_HEAD(&fc->list);

	return retval;

error_free_data:
	kfree(fc);
	return retval;
}

/*
 * Scan the PDT for F01 so we can force a reset before anything else
 * is done.  This forces the sensor into a known state, and also
 * forces application of any pending updates from reflashing the
 * firmware or configuration.  We have to do this before actually
 * building the PDT because the reflash might cause various registers
 * to move around.
 */
static int do_initial_reset(struct rmi_device *rmi_dev)
{
	struct pdt_entry pdt_entry;
	int page;
	struct device *dev = &rmi_dev->dev;
	bool done = false;
	int i;
	int retval;
	struct rmi_device_platform_data *pdata;

	dev_dbg(dev, "Initial reset.\n");
	pdata = to_rmi_platform_data(rmi_dev);
	for (page = 0; (page <= RMI4_MAX_PAGE) && !done; page++) {
		u16 page_start = RMI4_PAGE_SIZE * page;
		u16 pdt_start = page_start + PDT_START_SCAN_LOCATION;
		u16 pdt_end = page_start + PDT_END_SCAN_LOCATION;

		done = true;
		for (i = pdt_start; i >= pdt_end; i -= sizeof(pdt_entry)) {
			retval = rmi_read_block(rmi_dev, i, (u8 *)&pdt_entry,
					       sizeof(pdt_entry));
			if (retval != sizeof(pdt_entry)) {
				dev_err(dev, "Read PDT entry at 0x%04x"
					"failed, code = %d.\n", i, retval);
				return retval;
			}

			if (RMI4_END_OF_PDT(pdt_entry.function_number))
				break;
			done = false;

			if (pdt_entry.function_number == 0x01) {
				u16 cmd_addr = page_start +
					pdt_entry.command_base_addr;
				u8 cmd_buf = RMI_DEVICE_RESET_CMD;
				retval = rmi_write_block(rmi_dev, cmd_addr,
						&cmd_buf, 1);
				if (retval < 0) {
					dev_err(dev, "Initial reset failed. "
						"Code = %d.\n", retval);
					return retval;
				}
				mdelay(pdata->reset_delay_ms);
				return 0;
			}
		}
	}

	dev_warn(dev, "WARNING: Failed to find F01 for initial reset.\n");
	return -ENODEV;
}

static int rmi_scan_pdt(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data;
	struct pdt_entry pdt_entry;
	int page;
	struct device *dev = &rmi_dev->dev;
	int irq_count = 0;
	bool done = false;
	int i;
	int retval;
#ifdef CONFIG_RMI4_DEBUG
	dev_info(dev, "Scanning PDT...\n");
#endif
	data = rmi_get_driverdata(rmi_dev);
	mutex_lock(&data->pdt_mutex);

	for (page = 0; (page <= RMI4_MAX_PAGE) && !done; page++) {
		u16 page_start = RMI4_PAGE_SIZE * page;
		u16 pdt_start = page_start + PDT_START_SCAN_LOCATION;
		u16 pdt_end = page_start + PDT_END_SCAN_LOCATION;

		done = true;
		for (i = pdt_start; i >= pdt_end; i -= sizeof(pdt_entry)) {
			retval = rmi_read_block(rmi_dev, i, (u8 *)&pdt_entry,
					       sizeof(pdt_entry));
			if (retval != sizeof(pdt_entry)) {
				dev_err(dev, "Read PDT entry at 0x%04x "
					"failed.\n", i);
				goto error_exit;
			}

			if (RMI4_END_OF_PDT(pdt_entry.function_number))
				break;
#ifdef CONFIG_RMI4_DEBUG
			dev_info(dev, "%s: Found F%.2X on page 0x%02X\n",
				__func__, pdt_entry.function_number, page);
#endif
			done = false;

			if (pdt_entry.function_number == 0x01)
				retval = rmi_driver_fn_01_specific(rmi_dev,
						&pdt_entry, &irq_count,
						page_start);
			else
				retval = rmi_driver_fn_generic(rmi_dev,
						&pdt_entry, &irq_count,
						page_start);

			if (retval)
				goto error_exit;
#ifdef CONFIG_RMI4_DEBUG
			printk("command_base_addr:%x, control_base_addr:%x, data_base_addr:%x\n", pdt_entry.command_base_addr,
					pdt_entry.control_base_addr, pdt_entry.data_base_addr);
#else
			msleep(1);         //in this for(), enough delay is needed, or may cause system die
#endif
		}
		done = done || data->f01_bootloader_mode;
	}
	data->irq_count = irq_count;
	data->num_of_irq_regs = (irq_count + 7) / 8;
	dev_dbg(dev, "%s: Done with PDT scan.\n", __func__);
	retval = 0;

error_exit:
	mutex_unlock(&data->pdt_mutex);
	return retval;
}

static int rmi_driver_probe(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = NULL;
	struct rmi_function_container *fc;
	struct rmi_device_platform_data *pdata;
	int retval = 0;
	struct device *dev = &rmi_dev->dev;
	int attr_count = 0;

	dev_dbg(dev, "%s: Starting probe.\n", __func__);

	pdata = to_rmi_platform_data(rmi_dev);

	data = kzalloc(sizeof(struct rmi_driver_data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "%s: Failed to allocate driver data.\n", __func__);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&data->rmi_functions.list);
	rmi_set_driverdata(rmi_dev, data);
	mutex_init(&data->pdt_mutex);

	if (!pdata->reset_delay_ms)
		pdata->reset_delay_ms = DEFAULT_RESET_DELAY_MS;
	retval = do_initial_reset(rmi_dev);
	if (retval)
		dev_warn(dev, "RMI initial reset failed! Soldiering on.\n");


	retval = rmi_scan_pdt(rmi_dev);
	if (retval) {
		dev_err(dev, "PDT scan for %s failed with code %d.\n",
			pdata->sensor_name, retval);
		goto err_free_data;
	}

	if (!data->f01_container) {
		dev_err(dev, "missing F01 container!\n");
		retval = -EINVAL;
		goto err_free_data;
	}

	data->f01_container->irq_mask = kzalloc(
			sizeof(u8) * data->num_of_irq_regs, GFP_KERNEL);
	if (!data->f01_container->irq_mask) {
		dev_err(dev, "Failed to allocate F01 IRQ mask.\n");
		retval = -ENOMEM;
		goto err_free_data;
	}
	construct_mask(data->f01_container->irq_mask,
		       data->f01_container->num_of_irqs,
		       data->f01_container->irq_pos);
	list_for_each_entry(fc, &data->rmi_functions.list, list)
		fc->irq_mask = rmi_driver_irq_get_mask(rmi_dev, fc);

	retval = rmi_driver_f01_init(rmi_dev);
	if (retval < 0) {
		dev_err(dev, "Failed to initialize F01.\n");
		goto err_free_data;
	}

	retval = rmi_read(rmi_dev, PDT_PROPERTIES_LOCATION,
			 (char *) &data->pdt_props);
	if (retval < 0) {
		/* we'll print out a warning and continue since
		 * failure to get the PDT properties is not a cause to fail
		 */
		dev_warn(dev, "Could not read PDT properties from 0x%04x. "
			 "Assuming 0x00.\n", PDT_PROPERTIES_LOCATION);
	}

	dev_dbg(dev, "%s: Creating sysfs files.", __func__);
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = device_create_file(dev, &attrs[attr_count]);
		if (retval < 0) {
			dev_err(dev, "%s: Failed to create sysfs file %s.\n",
				__func__, attrs[attr_count].attr.name);
			goto err_free_data;
		}
	}
	if (has_bsr(data)) {
		retval = device_create_file(dev, &bsr_attribute);
		if (retval < 0) {
			dev_err(dev, "%s: Failed to create sysfs file bsr.\n",
				__func__);
			goto err_free_data;
		}
	}

	mutex_init(&data->irq_mutex);
	data->current_irq_mask = kzalloc(sizeof(u8) * data->num_of_irq_regs,
					 GFP_KERNEL);
	if (!data->current_irq_mask) {
		dev_err(dev, "Failed to allocate current_irq_mask.\n");
		retval = -ENOMEM;
		goto err_free_data;
	}
	retval = rmi_read_block(rmi_dev,
				data->f01_container->fd.control_base_addr+1,
				data->current_irq_mask, data->num_of_irq_regs);
	if (retval < 0) {
		dev_err(dev, "%s: Failed to read current IRQ mask.\n",
			__func__);
		goto err_free_data;
	}
	data->irq_mask_store = kzalloc(sizeof(u8) * data->num_of_irq_regs,
				       GFP_KERNEL);
	if (!data->irq_mask_store) {
		dev_err(dev, "Failed to allocate mask store.\n");
		retval = -ENOMEM;
		goto err_free_data;
	}

#ifdef	CONFIG_PM
	data->pm_data = pdata->pm_data;
	data->pre_suspend = pdata->pre_suspend;
	data->post_resume = pdata->post_resume;

	mutex_init(&data->suspend_mutex);

#ifdef CONFIG_HAS_EARLYSUSPEND
	rmi_dev->early_suspend_handler.level =
		EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	rmi_dev->early_suspend_handler.suspend = rmi_driver_early_suspend;
	rmi_dev->early_suspend_handler.resume = rmi_driver_late_resume;
	register_early_suspend(&rmi_dev->early_suspend_handler);
#endif /* CONFIG_HAS_EARLYSUSPEND */
#endif /* CONFIG_PM */
	data->enabled = true;

#ifdef CONFIG_RMI4_DEBUG
	retval = setup_debugfs(rmi_dev);
	if (retval < 0)
		dev_warn(&fc->dev, "Failed to setup debugfs. Code: %d.\n",
			 retval);
#endif

	return 0;

 err_free_data:
	rmi_free_function_list(rmi_dev);
	for (attr_count--; attr_count >= 0; attr_count--)
		device_remove_file(dev, &attrs[attr_count]);
	if (has_bsr(data))
		device_remove_file(dev, &bsr_attribute);
	if (data->f01_container)
		kfree(data->f01_container->irq_mask);
	kfree(data->irq_mask_store);
	kfree(data->current_irq_mask);
	kfree(data);
	rmi_set_driverdata(rmi_dev, NULL);
	return retval;
}

#ifdef CONFIG_PM
static int rmi_driver_suspend(struct device *dev)
{
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;
	struct rmi_function_container *entry;
	int retval = 0;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	mutex_lock(&data->suspend_mutex);
	if (data->suspended)
		goto exit;

#ifndef	CONFIG_HAS_EARLYSUSPEND
	if (data->pre_suspend) {
		retval = data->pre_suspend(data->pm_data);
		if (retval)
			goto exit;
	}
#endif  /* !CONFIG_HAS_EARLYSUSPEND */

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->fh && entry->fh->suspend) {
			retval = entry->fh->suspend(entry);
			if (retval < 0)
				goto exit;
		}

	if (data->f01_container && data->f01_container->fh
				&& data->f01_container->fh->suspend) {
		retval = data->f01_container->fh->suspend(data->f01_container);
		if (retval < 0)
			goto exit;
	}
	data->suspended = true;

exit:
	mutex_unlock(&data->suspend_mutex);
	return retval;
}

static int rmi_driver_resume(struct device *dev)
{
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;
	struct rmi_function_container *entry;
	int retval = 0;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	mutex_lock(&data->suspend_mutex);
	if (!data->suspended)
		goto exit;

	if (data->f01_container && data->f01_container->fh
				&& data->f01_container->fh->resume) {
		retval = data->f01_container->fh->resume(data->f01_container);
		if (retval < 0)
			goto exit;
	}

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->fh && entry->fh->resume) {
			retval = entry->fh->resume(entry);
			if (retval < 0)
				goto exit;
		}

#ifndef	CONFIG_HAS_EARLYSUSPEND
	if (data->post_resume) {
		retval = data->post_resume(data->pm_data);
		if (retval)
			goto exit;
	}
#endif  /* !CONFIG_HAS_EARLYSUSPEND */

	data->suspended = false;

exit:
	mutex_unlock(&data->suspend_mutex);
	return retval;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rmi_driver_early_suspend(struct early_suspend *h)
{
	struct rmi_device *rmi_dev =
	    container_of(h, struct rmi_device, early_suspend_handler);
	struct rmi_driver_data *data;
	struct rmi_function_container *entry;
	int retval = 0;

	data = rmi_get_driverdata(rmi_dev);
	dev_dbg(&rmi_dev->dev, "Early suspend.\n");

	mutex_lock(&data->suspend_mutex);
	if (data->suspended)
		goto exit;

	if (data->pre_suspend) {
		retval = data->pre_suspend(data->pm_data);
		if (retval)
			goto exit;
	}

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->fh && entry->fh->early_suspend) {
			retval = entry->fh->early_suspend(entry);
			if (retval < 0)
				goto exit;
		}

	if (data->f01_container && data->f01_container->fh
				&& data->f01_container->fh->early_suspend) {
		retval = data->f01_container->fh->early_suspend(
				data->f01_container);
		if (retval < 0)
			goto exit;
	}
	data->suspended = true;

exit:
	mutex_unlock(&data->suspend_mutex);
}

static void rmi_driver_late_resume(struct early_suspend *h)
{
	struct rmi_device *rmi_dev =
	    container_of(h, struct rmi_device, early_suspend_handler);
	struct rmi_driver_data *data;
	struct rmi_function_container *entry;
	int retval = 0;

	data = rmi_get_driverdata(rmi_dev);
	dev_dbg(&rmi_dev->dev, "Late resume.\n");

	mutex_lock(&data->suspend_mutex);
	if (!data->suspended)
		goto exit;

	if (data->f01_container && data->f01_container->fh
				&& data->f01_container->fh->late_resume) {
		retval = data->f01_container->fh->late_resume(
				data->f01_container);
		if (retval < 0)
			goto exit;
	}

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->fh && entry->fh->late_resume) {
			retval = entry->fh->late_resume(entry);
			if (retval < 0)
				goto exit;
		}

	if (data->post_resume) {
		retval = data->post_resume(data->pm_data);
		if (retval)
			goto exit;
	}

	data->suspended = false;

exit:
	mutex_unlock(&data->suspend_mutex);
}
#endif /* CONFIG_HAS_EARLYSUSPEND */
#endif /* CONFIG_PM */

static int __devexit rmi_driver_remove(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	struct rmi_function_container *entry;
	int i;

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&rmi_dev->early_suspend_handler);
#endif /* CONFIG_HAS_EARLYSUSPEND */
#ifdef	CONFIG_RMI4_DEBUG
	teardown_debugfs(rmi_dev);
#endif

	list_for_each_entry(entry, &data->rmi_functions.list, list)
		if (entry->fh && entry->fh->remove)
			entry->fh->remove(entry);

	rmi_free_function_list(rmi_dev);
	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		device_remove_file(&rmi_dev->dev, &attrs[i]);
	if (has_bsr(data))
		device_remove_file(&rmi_dev->dev, &bsr_attribute);
	kfree(data->f01_container->irq_mask);
	kfree(data->irq_mask_store);
	kfree(data->current_irq_mask);
	kfree(data);

	return 0;
}

#ifdef UNIVERSAL_DEV_PM_OPS
static UNIVERSAL_DEV_PM_OPS(rmi_driver_pm, rmi_driver_suspend,
			    rmi_driver_resume, NULL);
#endif

static struct rmi_driver sensor_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "rmi_generic",
#ifdef UNIVERSAL_DEV_PM_OPS
		.pm = &rmi_driver_pm,
#endif
	},
	.probe = rmi_driver_probe,
	.irq_handler = rmi_driver_irq_handler,
	.reset_handler = rmi_driver_reset_handler,
	.fh_add = rmi_driver_fh_add,
	.fh_remove = rmi_driver_fh_remove,
	.get_func_irq_mask = rmi_driver_irq_get_mask,
	.store_irq_mask = rmi_driver_irq_save,
	.restore_irq_mask = rmi_driver_irq_restore,
	.remove = __devexit_p(rmi_driver_remove)
};

/* sysfs show and store fns for driver attributes */

static ssize_t rmi_driver_bsr_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;
	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data->bsr);
}

static ssize_t rmi_driver_bsr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int retval;
	unsigned long val;
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	/* need to convert the string data to an actual value */
	retval = strict_strtoul(buf, 10, &val);
	if (retval < 0) {
		dev_err(dev, "Invalid value '%s' written to BSR.\n", buf);
		return -EINVAL;
	}

	retval = rmi_write(rmi_dev, BSR_LOCATION, (unsigned char)val);
	if (retval) {
		dev_err(dev, "%s : failed to write bsr %u to 0x%x\n",
			__func__, (unsigned int)val, BSR_LOCATION);
		return retval;
	}

	data->bsr = val;

	return count;
}

static void disable_sensor(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);

	rmi_dev->phys->disable_device(rmi_dev->phys);

	data->enabled = false;
}

static int enable_sensor(struct rmi_device *rmi_dev)
{
	struct rmi_driver_data *data = rmi_get_driverdata(rmi_dev);
	int retval = 0;

	retval = rmi_dev->phys->enable_device(rmi_dev->phys);
	/* non-zero means error occurred */
	if (retval)
		return retval;

	data->enabled = true;

	return 0;
}

static ssize_t rmi_driver_enabled_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data->enabled);
}

static ssize_t rmi_driver_enabled_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int retval;
	int new_value;
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	if (sysfs_streq(buf, "0"))
		new_value = false;
	else if (sysfs_streq(buf, "1"))
		new_value = true;
	else
		return -EINVAL;

	if (new_value) {
		retval = enable_sensor(rmi_dev);
		if (retval) {
			dev_err(dev, "Failed to enable sensor, code=%d.\n",
				retval);
			return -EIO;
		}
	} else {
		disable_sensor(rmi_dev);
	}

	return count;
}

#if REGISTER_DEBUG
static ssize_t rmi_driver_reg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int retval;
	unsigned int address;
	unsigned int bytes;
	struct rmi_device *rmi_dev;
	struct rmi_driver_data *data;
	u8 readbuf[128];
	unsigned char outbuf[512];
	unsigned char *bufptr = outbuf;
	int i;

	rmi_dev = to_rmi_device(dev);
	data = rmi_get_driverdata(rmi_dev);

	retval = sscanf(buf, "%x %u", &address, &bytes);
	if (retval != 2) {
		dev_err(dev, "Invalid input (code %d) for reg store: %s",
			retval, buf);
		return -EINVAL;
	}
	if (address < 0 || address > 0xFFFF) {
		dev_err(dev, "Invalid address for reg store '%#06x'.\n",
			address);
		return -EINVAL;
	}
	if (bytes < 0 || bytes >= sizeof(readbuf) || address+bytes > 65535) {
		dev_err(dev, "Invalid byte count for reg store '%d'.\n",
			bytes);
		return -EINVAL;
	}

	retval = rmi_read_block(rmi_dev, address, readbuf, bytes);
	if (retval != bytes) {
		dev_err(dev, "Failed to read %d registers at %#06x, code %d.\n",
			bytes, address, retval);
		return retval;
	}

	dev_info(dev, "Reading %d bytes from %#06x.\n", bytes, address);
	for (i = 0; i < bytes; i++) {
		retval = snprintf(bufptr, 4, "%02X ", readbuf[i]);
		if (retval < 0) {
			dev_err(dev, "Failed to format string. Code: %d",
				retval);
			return retval;
		}
		bufptr += retval;
	}
	dev_info(dev, "%s\n", outbuf);

	return count;
}
#endif

static int __init rmi_driver_init(void)
{
	return rmi_register_driver(&sensor_driver);
}

static void __exit rmi_driver_exit(void)
{
	rmi_unregister_driver(&sensor_driver);
}

module_init(rmi_driver_init);
module_exit(rmi_driver_exit);

MODULE_AUTHOR("Christopher Heiny <cheiny@synaptics.com");
MODULE_DESCRIPTION("RMI generic driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "debug.h"

/**
 * hw_register_read: reads all device registers (execute without interruption)
 * @buf:  destination buffer
 * @size: buffer size
 *
 * This function returns number of registers read
 */
static size_t hw_register_read(struct ci13xxx *ci, u32 *buf, size_t size)
{
	unsigned i;

	if (size > ci->hw_bank.size)
		size = ci->hw_bank.size;

	for (i = 0; i < size; i++)
		buf[i] = hw_read(ci, i * sizeof(u32), ~0);

	return size;
}

/**
 * hw_register_write: writes to register
 * @addr: register address
 * @data: register value
 *
 * This function returns an error code
 */
static int hw_register_write(struct ci13xxx *ci, u16 addr, u32 data)
{
	/* align */
	addr /= sizeof(u32);

	if (addr >= ci->hw_bank.size)
		return -EINVAL;

	/* align */
	addr *= sizeof(u32);

	hw_write(ci, addr, ~0, data);
	return 0;
}

/**
 * hw_intr_clear: disables interrupt & clears interrupt status (execute without
 *                interruption)
 * @n: interrupt bit
 *
 * This function returns an error code
 */
static int hw_intr_clear(struct ci13xxx *ci, int n)
{
	if (n >= REG_BITS)
		return -EINVAL;

	hw_write(ci, OP_USBINTR, BIT(n), 0);
	hw_write(ci, OP_USBSTS,  BIT(n), BIT(n));
	return 0;
}

/**
 * hw_intr_force: enables interrupt & forces interrupt status (execute without
 *                interruption)
 * @n: interrupt bit
 *
 * This function returns an error code
 */
static int hw_intr_force(struct ci13xxx *ci, int n)
{
	if (n >= REG_BITS)
		return -EINVAL;

	hw_write(ci, CAP_TESTMODE, TESTMODE_FORCE, TESTMODE_FORCE);
	hw_write(ci, OP_USBINTR,  BIT(n), BIT(n));
	hw_write(ci, OP_USBSTS,   BIT(n), BIT(n));
	hw_write(ci, CAP_TESTMODE, TESTMODE_FORCE, 0);
	return 0;
}

/**
 * show_device: prints information about device capabilities and status
 *
 * Check "device.h" for details
 */
static ssize_t show_device(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	struct usb_gadget *gadget = &ci->gadget;
	int n = 0;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		return 0;
	}

	n += scnprintf(buf + n, PAGE_SIZE - n, "speed             = %d\n",
		       gadget->speed);
	n += scnprintf(buf + n, PAGE_SIZE - n, "max_speed         = %d\n",
		       gadget->max_speed);
	n += scnprintf(buf + n, PAGE_SIZE - n, "is_otg            = %d\n",
		       gadget->is_otg);
	n += scnprintf(buf + n, PAGE_SIZE - n, "is_a_peripheral   = %d\n",
		       gadget->is_a_peripheral);
	n += scnprintf(buf + n, PAGE_SIZE - n, "b_hnp_enable      = %d\n",
		       gadget->b_hnp_enable);
	n += scnprintf(buf + n, PAGE_SIZE - n, "a_hnp_support     = %d\n",
		       gadget->a_hnp_support);
	n += scnprintf(buf + n, PAGE_SIZE - n, "a_alt_hnp_support = %d\n",
		       gadget->a_alt_hnp_support);
	n += scnprintf(buf + n, PAGE_SIZE - n, "name              = %s\n",
		       (gadget->name ? gadget->name : ""));

	return n;
}
static DEVICE_ATTR(device, S_IRUSR, show_device, NULL);

/**
 * show_driver: prints information about attached gadget (if any)
 *
 * Check "device.h" for details
 */
static ssize_t show_driver(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	struct usb_gadget_driver *driver = ci->driver;
	int n = 0;

	if (attr == NULL || buf == NULL) {
		dev_err(dev, "[%s] EINVAL\n", __func__);
		return 0;
	}

	if (driver == NULL)
		return scnprintf(buf, PAGE_SIZE,
				 "There is no gadget attached!\n");

	n += scnprintf(buf + n, PAGE_SIZE - n, "function  = %s\n",
		       (driver->function ? driver->function : ""));
	n += scnprintf(buf + n, PAGE_SIZE - n, "max speed = %d\n",
		       driver->max_speed);

	return n;
}
static DEVICE_ATTR(driver, S_IRUSR, show_driver, NULL);

/**
 * show_port_test: reads port test mode
 *
 * Check "device.h" for details
 */
static ssize_t show_port_test(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long flags;
	unsigned mode;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "EINVAL\n");
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	mode = hw_port_test_get(ci);
	spin_unlock_irqrestore(&ci->lock, flags);

	return scnprintf(buf, PAGE_SIZE, "mode = %u\n", mode);
}

/**
 * store_port_test: writes port test mode
 *
 * Check "device.h" for details
 */
static ssize_t store_port_test(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long flags;
	unsigned mode;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		goto done;
	}

	if (sscanf(buf, "%u", &mode) != 1) {
		dev_err(ci->dev, "<mode>: set port test mode");
		goto done;
	}

	spin_lock_irqsave(&ci->lock, flags);
	if (hw_port_test_set(ci, mode))
		dev_err(ci->dev, "invalid mode\n");
	spin_unlock_irqrestore(&ci->lock, flags);

 done:
	return count;
}
static DEVICE_ATTR(port_test, S_IRUSR | S_IWUSR,
		   show_port_test, store_port_test);

/**
 * show_qheads: DMA contents of all queue heads
 *
 * Check "device.h" for details
 */
static ssize_t show_qheads(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long flags;
	unsigned i, j, n = 0;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	for (i = 0; i < ci->hw_ep_max/2; i++) {
		struct ci13xxx_ep *mEpRx = &ci->ci13xxx_ep[i];
		struct ci13xxx_ep *mEpTx =
			&ci->ci13xxx_ep[i + ci->hw_ep_max/2];
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "EP=%02i: RX=%08X TX=%08X\n",
			       i, (u32)mEpRx->qh.dma, (u32)mEpTx->qh.dma);
		for (j = 0; j < (sizeof(struct ci13xxx_qh)/sizeof(u32)); j++) {
			n += scnprintf(buf + n, PAGE_SIZE - n,
				       " %04X:    %08X    %08X\n", j,
				       *((u32 *)mEpRx->qh.ptr + j),
				       *((u32 *)mEpTx->qh.ptr + j));
		}
	}
	spin_unlock_irqrestore(&ci->lock, flags);

	return n;
}
static DEVICE_ATTR(qheads, S_IRUSR, show_qheads, NULL);

/**
 * show_registers: dumps all registers
 *
 * Check "device.h" for details
 */
#define DUMP_ENTRIES	512
static ssize_t show_registers(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long flags;
	u32 *dump;
	unsigned i, k, n = 0;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		return 0;
	}

	dump = kmalloc(sizeof(u32) * DUMP_ENTRIES, GFP_KERNEL);
	if (!dump) {
		dev_err(ci->dev, "%s: out of memory\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	k = hw_register_read(ci, dump, DUMP_ENTRIES);
	spin_unlock_irqrestore(&ci->lock, flags);

	for (i = 0; i < k; i++) {
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "reg[0x%04X] = 0x%08X\n",
			       i * (unsigned)sizeof(u32), dump[i]);
	}
	kfree(dump);

	return n;
}

/**
 * store_registers: writes value to register address
 *
 * Check "device.h" for details
 */
static ssize_t store_registers(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long addr, data, flags;

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		goto done;
	}

	if (sscanf(buf, "%li %li", &addr, &data) != 2) {
		dev_err(ci->dev,
			"<addr> <data>: write data to register address\n");
		goto done;
	}

	spin_lock_irqsave(&ci->lock, flags);
	if (hw_register_write(ci, addr, data))
		dev_err(ci->dev, "invalid address range\n");
	spin_unlock_irqrestore(&ci->lock, flags);

 done:
	return count;
}
static DEVICE_ATTR(registers, S_IRUSR | S_IWUSR,
		   show_registers, store_registers);

/**
 * show_requests: DMA contents of all requests currently queued (all endpts)
 *
 * Check "device.h" for details
 */
static ssize_t show_requests(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ci13xxx *ci = container_of(dev, struct ci13xxx, gadget.dev);
	unsigned long flags;
	struct list_head   *ptr = NULL;
	struct ci13xxx_req *req = NULL;
	unsigned i, j, n = 0, qSize = sizeof(struct ci13xxx_td)/sizeof(u32);

	if (attr == NULL || buf == NULL) {
		dev_err(ci->dev, "[%s] EINVAL\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&ci->lock, flags);
	for (i = 0; i < ci->hw_ep_max; i++)
		list_for_each(ptr, &ci->ci13xxx_ep[i].qh.queue)
		{
			req = list_entry(ptr, struct ci13xxx_req, queue);

			n += scnprintf(buf + n, PAGE_SIZE - n,
					"EP=%02i: TD=%08X %s\n",
					i % ci->hw_ep_max/2, (u32)req->dma,
					((i < ci->hw_ep_max/2) ? "RX" : "TX"));

			for (j = 0; j < qSize; j++)
				n += scnprintf(buf + n, PAGE_SIZE - n,
						" %04X:    %08X\n", j,
						*((u32 *)req->ptr + j));
		}
	spin_unlock_irqrestore(&ci->lock, flags);

	return n;
}
static DEVICE_ATTR(requests, S_IRUSR, show_requests, NULL);

/**
 * dbg_create_files: initializes the attribute interface
 * @dev: device
 *
 * This function returns an error code
 */
int dbg_create_files(struct device *dev)
{
	int retval = 0;

	if (dev == NULL)
		return -EINVAL;
	retval = device_create_file(dev, &dev_attr_device);
	if (retval)
		goto done;
	retval = device_create_file(dev, &dev_attr_driver);
	if (retval)
		goto rm_device;
	retval = device_create_file(dev, &dev_attr_port_test);
	if (retval)
		goto rm_driver;
	retval = device_create_file(dev, &dev_attr_qheads);
	if (retval)
		goto rm_port_test;
	retval = device_create_file(dev, &dev_attr_registers);
	if (retval)
		goto rm_qheads;
	retval = device_create_file(dev, &dev_attr_requests);
	if (retval)
		goto rm_registers;
	return 0;

 rm_registers:
	device_remove_file(dev, &dev_attr_registers);
 rm_qheads:
	device_remove_file(dev, &dev_attr_qheads);
 rm_port_test:
	device_remove_file(dev, &dev_attr_port_test);
 rm_driver:
	device_remove_file(dev, &dev_attr_driver);
 rm_device:
	device_remove_file(dev, &dev_attr_device);
 done:
	return retval;
}

/**
 * dbg_remove_files: destroys the attribute interface
 * @dev: device
 *
 * This function returns an error code
 */
int dbg_remove_files(struct device *dev)
{
	if (dev == NULL)
		return -EINVAL;
	device_remove_file(dev, &dev_attr_requests);
	device_remove_file(dev, &dev_attr_registers);
	device_remove_file(dev, &dev_attr_qheads);
	device_remove_file(dev, &dev_attr_port_test);
	device_remove_file(dev, &dev_attr_driver);
	device_remove_file(dev, &dev_attr_device);
	return 0;
}

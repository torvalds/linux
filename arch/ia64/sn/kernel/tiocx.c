/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include <asm/sn/io.h>
#include <asm/sn/types.h>
#include <asm/sn/shubio.h>
#include <asm/sn/tiocx.h>
#include <asm/sn/l1.h>
#include <asm/sn/module.h>
#include "tio.h"
#include "xtalk/xwidgetdev.h"
#include "xtalk/hubdev.h"

#define CX_DEV_NONE 0
#define DEVICE_NAME "tiocx"
#define WIDGET_ID 0
#define TIOCX_DEBUG 0

#if TIOCX_DEBUG
#define DBG(fmt...)    printk(KERN_ALERT fmt)
#else
#define DBG(fmt...)
#endif

struct device_attribute dev_attr_cxdev_control;

/**
 * tiocx_match - Try to match driver id list with device.
 * @dev: device pointer
 * @drv: driver pointer
 *
 * Returns 1 if match, 0 otherwise.
 */
static int tiocx_match(struct device *dev, struct device_driver *drv)
{
	struct cx_dev *cx_dev = to_cx_dev(dev);
	struct cx_drv *cx_drv = to_cx_driver(drv);
	const struct cx_device_id *ids = cx_drv->id_table;

	if (!ids)
		return 0;

	while (ids->part_num) {
		if (ids->part_num == cx_dev->cx_id.part_num)
			return 1;
		ids++;
	}
	return 0;

}

static int tiocx_uevent(struct device *dev, char **envp, int num_envp,
			 char *buffer, int buffer_size)
{
	return -ENODEV;
}

static void tiocx_bus_release(struct device *dev)
{
	kfree(to_cx_dev(dev));
}

/**
 * cx_device_match - Find cx_device in the id table.
 * @ids: id table from driver
 * @cx_device: part/mfg id for the device
 *
 */
static const struct cx_device_id *cx_device_match(const struct cx_device_id
						  *ids,
						  struct cx_dev *cx_device)
{
	/*
	 * NOTES: We may want to check for CX_ANY_ID too.
	 *        Do we want to match against nasid too?
	 *        CX_DEV_NONE == 0, if the driver tries to register for
	 *        part/mfg == 0 we should return no-match (NULL) here.
	 */
	while (ids->part_num && ids->mfg_num) {
		if (ids->part_num == cx_device->cx_id.part_num &&
		    ids->mfg_num == cx_device->cx_id.mfg_num)
			return ids;
		ids++;
	}

	return NULL;
}

/**
 * cx_device_probe - Look for matching device.
 *			Call driver probe routine if found.
 * @cx_driver: driver table (cx_drv struct) from driver
 * @cx_device: part/mfg id for the device
 */
static int cx_device_probe(struct device *dev)
{
	const struct cx_device_id *id;
	struct cx_drv *cx_drv = to_cx_driver(dev->driver);
	struct cx_dev *cx_dev = to_cx_dev(dev);
	int error = 0;

	if (!cx_dev->driver && cx_drv->probe) {
		id = cx_device_match(cx_drv->id_table, cx_dev);
		if (id) {
			if ((error = cx_drv->probe(cx_dev, id)) < 0)
				return error;
			else
				cx_dev->driver = cx_drv;
		}
	}

	return error;
}

/**
 * cx_driver_remove - Remove driver from device struct.
 * @dev: device
 */
static int cx_driver_remove(struct device *dev)
{
	struct cx_dev *cx_dev = to_cx_dev(dev);
	struct cx_drv *cx_drv = cx_dev->driver;
	if (cx_drv->remove)
		cx_drv->remove(cx_dev);
	cx_dev->driver = NULL;
	return 0;
}

struct bus_type tiocx_bus_type = {
	.name = "tiocx",
	.match = tiocx_match,
	.uevent = tiocx_uevent,
	.probe = cx_device_probe,
	.remove = cx_driver_remove,
};

/**
 * cx_driver_register - Register the driver.
 * @cx_driver: driver table (cx_drv struct) from driver
 * 
 * Called from the driver init routine to register a driver.
 * The cx_drv struct contains the driver name, a pointer to
 * a table of part/mfg numbers and a pointer to the driver's
 * probe/attach routine.
 */
int cx_driver_register(struct cx_drv *cx_driver)
{
	cx_driver->driver.name = cx_driver->name;
	cx_driver->driver.bus = &tiocx_bus_type;

	return driver_register(&cx_driver->driver);
}

/**
 * cx_driver_unregister - Unregister the driver.
 * @cx_driver: driver table (cx_drv struct) from driver
 */
int cx_driver_unregister(struct cx_drv *cx_driver)
{
	driver_unregister(&cx_driver->driver);
	return 0;
}

/**
 * cx_device_register - Register a device.
 * @nasid: device's nasid
 * @part_num: device's part number
 * @mfg_num: device's manufacturer number
 * @hubdev: hub info associated with this device
 * @bt: board type of the device
 *
 */
int
cx_device_register(nasid_t nasid, int part_num, int mfg_num,
		   struct hubdev_info *hubdev, int bt)
{
	struct cx_dev *cx_dev;

	cx_dev = kzalloc(sizeof(struct cx_dev), GFP_KERNEL);
	DBG("cx_dev= 0x%p\n", cx_dev);
	if (cx_dev == NULL)
		return -ENOMEM;

	cx_dev->cx_id.part_num = part_num;
	cx_dev->cx_id.mfg_num = mfg_num;
	cx_dev->cx_id.nasid = nasid;
	cx_dev->hubdev = hubdev;
	cx_dev->bt = bt;

	cx_dev->dev.parent = NULL;
	cx_dev->dev.bus = &tiocx_bus_type;
	cx_dev->dev.release = tiocx_bus_release;
	snprintf(cx_dev->dev.bus_id, BUS_ID_SIZE, "%d",
		 cx_dev->cx_id.nasid);
	device_register(&cx_dev->dev);
	get_device(&cx_dev->dev);

	device_create_file(&cx_dev->dev, &dev_attr_cxdev_control);

	return 0;
}

/**
 * cx_device_unregister - Unregister a device.
 * @cx_dev: part/mfg id for the device
 */
int cx_device_unregister(struct cx_dev *cx_dev)
{
	put_device(&cx_dev->dev);
	device_unregister(&cx_dev->dev);
	return 0;
}

/**
 * cx_device_reload - Reload the device.
 * @nasid: device's nasid
 * @part_num: device's part number
 * @mfg_num: device's manufacturer number
 *
 * Remove the device associated with 'nasid' from device list and then
 * call device-register with the given part/mfg numbers.
 */
static int cx_device_reload(struct cx_dev *cx_dev)
{
	cx_device_unregister(cx_dev);
	return cx_device_register(cx_dev->cx_id.nasid, cx_dev->cx_id.part_num,
				  cx_dev->cx_id.mfg_num, cx_dev->hubdev,
				  cx_dev->bt);
}

static inline u64 tiocx_intr_alloc(nasid_t nasid, int widget,
					u64 sn_irq_info,
					int req_irq, nasid_t req_nasid,
					int req_slice)
{
	struct ia64_sal_retval rv;
	rv.status = 0;
	rv.v0 = 0;

	ia64_sal_oemcall_nolock(&rv, SN_SAL_IOIF_INTERRUPT,
				SAL_INTR_ALLOC, nasid,
				widget, sn_irq_info, req_irq,
				req_nasid, req_slice);
	return rv.status;
}

static inline void tiocx_intr_free(nasid_t nasid, int widget,
				   struct sn_irq_info *sn_irq_info)
{
	struct ia64_sal_retval rv;
	rv.status = 0;
	rv.v0 = 0;

	ia64_sal_oemcall_nolock(&rv, SN_SAL_IOIF_INTERRUPT,
				SAL_INTR_FREE, nasid,
				widget, sn_irq_info->irq_irq,
				sn_irq_info->irq_cookie, 0, 0);
}

struct sn_irq_info *tiocx_irq_alloc(nasid_t nasid, int widget, int irq,
				    nasid_t req_nasid, int slice)
{
	struct sn_irq_info *sn_irq_info;
	int status;
	int sn_irq_size = sizeof(struct sn_irq_info);

	if ((nasid & 1) == 0)
		return NULL;

	sn_irq_info = kzalloc(sn_irq_size, GFP_KERNEL);
	if (sn_irq_info == NULL)
		return NULL;

	status = tiocx_intr_alloc(nasid, widget, __pa(sn_irq_info), irq,
				  req_nasid, slice);
	if (status) {
		kfree(sn_irq_info);
		return NULL;
	} else {
		return sn_irq_info;
	}
}

void tiocx_irq_free(struct sn_irq_info *sn_irq_info)
{
	u64 bridge = (u64) sn_irq_info->irq_bridge;
	nasid_t nasid = NASID_GET(bridge);
	int widget;

	if (nasid & 1) {
		widget = TIO_SWIN_WIDGETNUM(bridge);
		tiocx_intr_free(nasid, widget, sn_irq_info);
		kfree(sn_irq_info);
	}
}

u64 tiocx_dma_addr(u64 addr)
{
	return PHYS_TO_TIODMA(addr);
}

u64 tiocx_swin_base(int nasid)
{
	return TIO_SWIN_BASE(nasid, TIOCX_CORELET);
}

EXPORT_SYMBOL(cx_driver_register);
EXPORT_SYMBOL(cx_driver_unregister);
EXPORT_SYMBOL(cx_device_register);
EXPORT_SYMBOL(cx_device_unregister);
EXPORT_SYMBOL(tiocx_irq_alloc);
EXPORT_SYMBOL(tiocx_irq_free);
EXPORT_SYMBOL(tiocx_bus_type);
EXPORT_SYMBOL(tiocx_dma_addr);
EXPORT_SYMBOL(tiocx_swin_base);

static void tio_conveyor_set(nasid_t nasid, int enable_flag)
{
	u64 ice_frz;
	u64 disable_cb = (1ull << 61);

	if (!(nasid & 1))
		return;

	ice_frz = REMOTE_HUB_L(nasid, TIO_ICE_FRZ_CFG);
	if (enable_flag) {
		if (!(ice_frz & disable_cb))	/* already enabled */
			return;
		ice_frz &= ~disable_cb;
	} else {
		if (ice_frz & disable_cb)	/* already disabled */
			return;
		ice_frz |= disable_cb;
	}
	DBG(KERN_ALERT "TIO_ICE_FRZ_CFG= 0x%lx\n", ice_frz);
	REMOTE_HUB_S(nasid, TIO_ICE_FRZ_CFG, ice_frz);
}

#define tio_conveyor_enable(nasid) tio_conveyor_set(nasid, 1)
#define tio_conveyor_disable(nasid) tio_conveyor_set(nasid, 0)

static void tio_corelet_reset(nasid_t nasid, int corelet)
{
	if (!(nasid & 1))
		return;

	REMOTE_HUB_S(nasid, TIO_ICE_PMI_TX_CFG, 1 << corelet);
	udelay(2000);
	REMOTE_HUB_S(nasid, TIO_ICE_PMI_TX_CFG, 0);
	udelay(2000);
}

static int is_fpga_tio(int nasid, int *bt)
{
	u16 ioboard_type;
	s64 rc;

	rc = ia64_sn_sysctl_ioboard_get(nasid, &ioboard_type);
	if (rc) {
		printk(KERN_WARNING "ia64_sn_sysctl_ioboard_get failed: %ld\n",
		       rc);
		return 0;
	}

	switch (ioboard_type) {
	case L1_BRICKTYPE_SA:
	case L1_BRICKTYPE_ATHENA:
	case L1_BOARDTYPE_DAYTONA:
		*bt = ioboard_type;
		return 1;
	}

	return 0;
}

static int bitstream_loaded(nasid_t nasid)
{
	u64 cx_credits;

	cx_credits = REMOTE_HUB_L(nasid, TIO_ICE_PMI_TX_DYN_CREDIT_STAT_CB3);
	cx_credits &= TIO_ICE_PMI_TX_DYN_CREDIT_STAT_CB3_CREDIT_CNT_MASK;
	DBG("cx_credits= 0x%lx\n", cx_credits);

	return (cx_credits == 0xf) ? 1 : 0;
}

static int tiocx_reload(struct cx_dev *cx_dev)
{
	int part_num = CX_DEV_NONE;
	int mfg_num = CX_DEV_NONE;
	nasid_t nasid = cx_dev->cx_id.nasid;

	if (bitstream_loaded(nasid)) {
		u64 cx_id;
		int rv;

		rv = ia64_sn_sysctl_tio_clock_reset(nasid);
		if (rv) {
			printk(KERN_ALERT "CX port JTAG reset failed.\n");
		} else {
			cx_id = *(volatile u64 *)
				(TIO_SWIN_BASE(nasid, TIOCX_CORELET) +
					  WIDGET_ID);
			part_num = XWIDGET_PART_NUM(cx_id);
			mfg_num = XWIDGET_MFG_NUM(cx_id);
			DBG("part= 0x%x, mfg= 0x%x\n", part_num, mfg_num);
			/* just ignore it if it's a CE */
			if (part_num == TIO_CE_ASIC_PARTNUM)
				return 0;
		}
	}

	cx_dev->cx_id.part_num = part_num;
	cx_dev->cx_id.mfg_num = mfg_num;

	/*
	 * Delete old device and register the new one.  It's ok if
	 * part_num/mfg_num == CX_DEV_NONE.  We want to register
	 * devices in the table even if a bitstream isn't loaded.
	 * That allows use to see that a bitstream isn't loaded via
	 * TIOCX_IOCTL_DEV_LIST.
	 */
	return cx_device_reload(cx_dev);
}

static ssize_t show_cxdev_control(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cx_dev *cx_dev = to_cx_dev(dev);

	return sprintf(buf, "0x%x 0x%x 0x%x 0x%x\n",
		       cx_dev->cx_id.nasid,
		       cx_dev->cx_id.part_num, cx_dev->cx_id.mfg_num,
		       cx_dev->bt);
}

static ssize_t store_cxdev_control(struct device *dev, struct device_attribute *attr, const char *buf,
				   size_t count)
{
	int n;
	struct cx_dev *cx_dev = to_cx_dev(dev);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (count <= 0)
		return 0;

	n = simple_strtoul(buf, NULL, 0);

	switch (n) {
	case 1:
		tio_corelet_reset(cx_dev->cx_id.nasid, TIOCX_CORELET);
		tiocx_reload(cx_dev);
		break;
	case 2:
		tiocx_reload(cx_dev);
		break;
	case 3:
		tio_corelet_reset(cx_dev->cx_id.nasid, TIOCX_CORELET);
		break;
	default:
		break;
	}

	return count;
}

DEVICE_ATTR(cxdev_control, 0644, show_cxdev_control, store_cxdev_control);

static int __init tiocx_init(void)
{
	cnodeid_t cnodeid;
	int found_tiocx_device = 0;

	if (!ia64_platform_is("sn2"))
		return 0;

	bus_register(&tiocx_bus_type);

	for (cnodeid = 0; cnodeid < num_cnodes; cnodeid++) {
		nasid_t nasid;
		int bt;

		nasid = cnodeid_to_nasid(cnodeid);

		if ((nasid & 0x1) && is_fpga_tio(nasid, &bt)) {
			struct hubdev_info *hubdev;
			struct xwidget_info *widgetp;

			DBG("Found TIO at nasid 0x%x\n", nasid);

			hubdev =
			    (struct hubdev_info *)(NODEPDA(cnodeid)->pdinfo);

			widgetp = &hubdev->hdi_xwidget_info[TIOCX_CORELET];

			/* The CE hangs off of the CX port but is not an FPGA */
			if (widgetp->xwi_hwid.part_num == TIO_CE_ASIC_PARTNUM)
				continue;

			tio_corelet_reset(nasid, TIOCX_CORELET);
			tio_conveyor_enable(nasid);

			if (cx_device_register
			    (nasid, widgetp->xwi_hwid.part_num,
			     widgetp->xwi_hwid.mfg_num, hubdev, bt) < 0)
				return -ENXIO;
			else
				found_tiocx_device++;
		}
	}

	/* It's ok if we find zero devices. */
	DBG("found_tiocx_device= %d\n", found_tiocx_device);

	return 0;
}

static int cx_remove_device(struct device * dev, void * data)
{
	struct cx_dev *cx_dev = to_cx_dev(dev);
	device_remove_file(dev, &dev_attr_cxdev_control);
	cx_device_unregister(cx_dev);
	return 0;
}

static void __exit tiocx_exit(void)
{
	DBG("tiocx_exit\n");

	/*
	 * Unregister devices.
	 */
	bus_for_each_dev(&tiocx_bus_type, NULL, NULL, cx_remove_device);
	bus_unregister(&tiocx_bus_type);
}

subsys_initcall(tiocx_init);
module_exit(tiocx_exit);

/************************************************************************
 * Module licensing and description
 ************************************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bruce Losure <blosure@sgi.com>");
MODULE_DESCRIPTION("TIOCX module");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);

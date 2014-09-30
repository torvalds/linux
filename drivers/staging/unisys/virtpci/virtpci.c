/* virtpci.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#define EXPORT_SYMTAB

#include <linux/kernel.h>
#ifdef CONFIG_MODVERSIONS
#include <config/modversions.h>
#endif
#include "uniklog.h"
#include "diagnostics/appos_subsystems.h"
#include "uisutils.h"
#include "vbuschannel.h"
#include "vbushelper.h"
#include <linux/types.h>
#include <linux/io.h>
#include <linux/uuid.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/if_ether.h>
#include <linux/version.h>
#include <linux/debugfs.h>
#include "version.h"
#include "guestlinuxdebug.h"
#include "timskmod.h"

struct driver_private {
	struct kobject kobj;
	struct klist klist_devices;
	struct klist_node knode_bus;
	struct module_kobject *mkobj;
	struct device_driver *driver;
};
#define to_driver(obj) container_of(obj, struct driver_private, kobj)

/* bus_id went away in 2.6.30 - the size was 20 bytes, so we'll define
 * it ourselves, and a macro to make getting the field a bit simpler.
 */
#ifndef BUS_ID_SIZE
#define BUS_ID_SIZE 20
#endif

#define BUS_ID(x) dev_name(x)

/* MAX_BUF = 4 busses x ( 32 devices/bus + 1 busline) x 80 characters
 *         = 10,560 bytes ~ 2^14 = 16,384 bytes
 */
#define MAX_BUF 16384

#include "virtpci.h"

/* this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages
 */
#define CURRENT_FILE_PC VIRT_PCI_PC_virtpci_c
#define __MYFILE__ "virtpci.c"

#define VIRTPCI_VERSION "01.00"

/*****************************************************/
/* Forward declarations                              */
/*****************************************************/

static int delete_vbus_device(struct device *vbus, void *data);
static int match_busid(struct device *dev, void *data);
static void virtpci_bus_release(struct device *dev);
static void virtpci_device_release(struct device *dev);
static int virtpci_device_add(struct device *parentbus, int devtype,
			      struct add_virt_guestpart *addparams,
			      struct scsi_adap_info *scsi,
			      struct net_adap_info *net);
static int virtpci_device_del(struct device *parentbus, int devtype,
			      struct vhba_wwnn *wwnn, unsigned char macaddr[]);
static int virtpci_device_serverdown(struct device *parentbus, int devtype,
				     struct vhba_wwnn *wwnn,
				     unsigned char macaddr[]);
static int virtpci_device_serverup(struct device *parentbus, int devtype,
				   struct vhba_wwnn *wwnn,
				   unsigned char macaddr[]);
static ssize_t virtpci_driver_attr_show(struct kobject *kobj,
					struct attribute *attr, char *buf);
static ssize_t virtpci_driver_attr_store(struct kobject *kobj,
					 struct attribute *attr,
					 const char *buf, size_t count);
static int virtpci_bus_match(struct device *dev, struct device_driver *drv);
static int virtpci_uevent(struct device *dev, struct kobj_uevent_env *env);
static int virtpci_device_suspend(struct device *dev, pm_message_t state);
static int virtpci_device_resume(struct device *dev);
static int virtpci_device_probe(struct device *dev);
static int virtpci_device_remove(struct device *dev);

static ssize_t info_debugfs_read(struct file *file, char __user *buf,
			      size_t len, loff_t *offset);

static const struct file_operations debugfs_info_fops = {
	.read = info_debugfs_read,
};

/*****************************************************/
/* Globals                                           */
/*****************************************************/

/* methods in bus_type struct allow the bus code to serve as an
 * intermediary between the device core and individual device core and
 * individual drivers
 */
static struct bus_type virtpci_bus_type = {
	.name = "uisvirtpci",
	.match = virtpci_bus_match,
	.uevent = virtpci_uevent,
	.suspend = virtpci_device_suspend,
	.resume = virtpci_device_resume,
};

static struct device virtpci_rootbus_device = {
	.init_name = "vbusroot",	/* root bus */
	.release = virtpci_bus_release
};

/* filled in with info about parent chipset driver when we register with it */
static ULTRA_VBUS_DEVICEINFO Chipset_DriverInfo;

static const struct sysfs_ops virtpci_driver_sysfs_ops = {
	.show = virtpci_driver_attr_show,
	.store = virtpci_driver_attr_store,
};

static struct kobj_type virtpci_driver_kobj_type = {
	.sysfs_ops = &virtpci_driver_sysfs_ops,
};

static struct virtpci_dev *VpcidevListHead;
static DEFINE_RWLOCK(VpcidevListLock);

/* filled in with info about this driver, wrt it servicing client busses */
static ULTRA_VBUS_DEVICEINFO Bus_DriverInfo;

/*****************************************************/
/* debugfs entries                                   */
/*****************************************************/
/* dentry is used to create the debugfs entry directory
 * for virtpci
 */
static struct dentry *virtpci_debugfs_dir;

struct virtpci_busdev {
	struct device virtpci_bus_device;
};

/*****************************************************/
/* Local functions                                   */
/*****************************************************/

static inline
int WAIT_FOR_IO_CHANNEL(ULTRA_IO_CHANNEL_PROTOCOL __iomem  *chanptr)
{
	int count = 120;

	while (count > 0) {

		if (ULTRA_CHANNEL_SERVER_READY(&chanptr->ChannelHeader))
			return 1;
		UIS_THREAD_WAIT_SEC(1);
		count--;
	}
	return 0;
}

/* Write the contents of <info> to the ULTRA_VBUS_CHANNEL_PROTOCOL.ChpInfo. */
static int write_vbus_chpInfo(ULTRA_VBUS_CHANNEL_PROTOCOL *chan,
			      ULTRA_VBUS_DEVICEINFO *info)
{
	int off;

	if (!chan) {
		LOGERR("vbus channel not present");
		return -1;
	}
	off = sizeof(ULTRA_CHANNEL_PROTOCOL) + chan->HdrInfo.chpInfoByteOffset;
	if (chan->HdrInfo.chpInfoByteOffset == 0) {
		LOGERR("vbus channel not used, because chpInfoByteOffset == 0");
		return -1;
	}
	memcpy(((u8 *) (chan)) + off, info, sizeof(*info));
	return 0;
}

/* Write the contents of <info> to the ULTRA_VBUS_CHANNEL_PROTOCOL.BusInfo. */
static int write_vbus_busInfo(ULTRA_VBUS_CHANNEL_PROTOCOL *chan,
			      ULTRA_VBUS_DEVICEINFO *info)
{
	int off;

	if (!chan) {
		LOGERR("vbus channel not present");
		return -1;
	}
	off = sizeof(ULTRA_CHANNEL_PROTOCOL) + chan->HdrInfo.busInfoByteOffset;
	if (chan->HdrInfo.busInfoByteOffset == 0) {
		LOGERR("vbus channel not used, because busInfoByteOffset == 0");
		return -1;
	}
	memcpy(((u8 *) (chan)) + off, info, sizeof(*info));
	return 0;
}

/* Write the contents of <info> to the
 * ULTRA_VBUS_CHANNEL_PROTOCOL.DevInfo[<devix>].
 */
static int
write_vbus_devInfo(ULTRA_VBUS_CHANNEL_PROTOCOL *chan,
		   ULTRA_VBUS_DEVICEINFO *info, int devix)
{
	int off;

	if (!chan) {
		LOGERR("vbus channel not present");
		return -1;
	}
	off =
	    (sizeof(ULTRA_CHANNEL_PROTOCOL) +
	     chan->HdrInfo.devInfoByteOffset) +
	    (chan->HdrInfo.deviceInfoStructBytes * devix);
	if (chan->HdrInfo.devInfoByteOffset == 0) {
		LOGERR("vbus channel not used, because devInfoByteOffset == 0");
		return -1;
	}
	memcpy(((u8 *) (chan)) + off, info, sizeof(*info));
	return 0;
}

/* adds a vbus
 * returns 0 failure, 1 success,
 */
static int add_vbus(struct add_vbus_guestpart *addparams)
{
	int ret;
	struct device *vbus;

	vbus = kzalloc(sizeof(struct device), GFP_ATOMIC);

	POSTCODE_LINUX_2(VPCI_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	if (!vbus)
		return 0;

	dev_set_name(vbus, "vbus%d", addparams->busNo);
	vbus->release = virtpci_bus_release;
	vbus->parent = &virtpci_rootbus_device;	/* root bus is parent */
	vbus->bus = &virtpci_bus_type;	/* bus type */
	vbus->platform_data = (__force void *)addparams->chanptr;

	/* register a virt bus device -
	 * this bus shows up under /sys/devices with .name value
	 * "virtpci%d" any devices added to this bus then show up under
	 * /sys/devices/virtpci0
	 */
	ret = device_register(vbus);
	if (ret) {
		LOGERR("device_register FAILED:%d\n", ret);
		POSTCODE_LINUX_2(VPCI_CREATE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		return 0;
	}
	write_vbus_chpInfo(vbus->platform_data /* chanptr */ ,
			   &Chipset_DriverInfo);
	write_vbus_busInfo(vbus->platform_data /* chanptr */ , &Bus_DriverInfo);
	LOGINF("Added vbus %d; device %s created successfully\n",
	       addparams->busNo, BUS_ID(vbus));
	POSTCODE_LINUX_2(VPCI_CREATE_EXIT_PC, POSTCODE_SEVERITY_INFO);
	return 1;
}

/* for CHANSOCK wwwnn/max are AUTO-GENERATED; for normal channels,
 * wwnn/max are in the channel header.
 */
#define GET_SCSIADAPINFO_FROM_CHANPTR(chanptr) {			\
	memcpy_fromio(&scsi.wwnn,					\
		      &((ULTRA_IO_CHANNEL_PROTOCOL __iomem *)		\
			chanptr)->vhba.wwnn,				\
		      sizeof(struct vhba_wwnn));			\
	memcpy_fromio(&scsi.max,					\
		      &((ULTRA_IO_CHANNEL_PROTOCOL __iomem *)		\
			chanptr)->vhba.max,				\
		      sizeof(struct vhba_config_max));			\
	}

/* find bus device with the busid that matches - match_busid matches bus_id */
#define GET_BUS_DEV(busno) { \
	sprintf(busid, "vbus%d", busno); \
	vbus = bus_find_device(&virtpci_bus_type, NULL, \
			       (void *)busid, match_busid);	\
	if (!vbus) { \
		LOGERR("**** FAILED to find vbus %s\n", busid); \
		return 0; \
	} \
}

/* adds a vhba
 * returns 0 failure, 1 success,
 */
static int add_vhba(struct add_virt_guestpart *addparams)
{
	int i;
	struct scsi_adap_info scsi;
	struct device *vbus;
	unsigned char busid[BUS_ID_SIZE];

	POSTCODE_LINUX_2(VPCI_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	if (!WAIT_FOR_IO_CHANNEL
	    ((ULTRA_IO_CHANNEL_PROTOCOL __iomem *) addparams->chanptr)) {
		LOGERR("Timed out.  Channel not ready\n");
		POSTCODE_LINUX_2(VPCI_CREATE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		return 0;
	}

	GET_SCSIADAPINFO_FROM_CHANPTR(addparams->chanptr);

	GET_BUS_DEV(addparams->busNo);

	LOGINF("Adding vhba wwnn:%x:%x config:%d-%d-%d-%d chanptr:%p\n",
	       scsi.wwnn.wwnn1, scsi.wwnn.wwnn2,
	       scsi.max.max_channel, scsi.max.max_id, scsi.max.max_lun,
	       scsi.max.cmd_per_lun, addparams->chanptr);
	i = virtpci_device_add(vbus, VIRTHBA_TYPE, addparams, &scsi, NULL);
	if (i) {
		LOGINF("Added vhba wwnn:%x:%x chanptr:%p\n", scsi.wwnn.wwnn1,
		       scsi.wwnn.wwnn2, addparams->chanptr);
		POSTCODE_LINUX_3(VPCI_CREATE_EXIT_PC, i,
				 POSTCODE_SEVERITY_INFO);
	}
	return i;

}

/* for CHANSOCK macaddr is AUTO-GENERATED; for normal channels,
 * macaddr is in the channel header.
 */
#define GET_NETADAPINFO_FROM_CHANPTR(chanptr) {				\
		memcpy_fromio(net.mac_addr,				\
		       ((ULTRA_IO_CHANNEL_PROTOCOL __iomem *)		\
			chanptr)->vnic.macaddr,				\
		       MAX_MACADDR_LEN);				\
		net.num_rcv_bufs =					\
			readl(&((ULTRA_IO_CHANNEL_PROTOCOL __iomem *)	\
				chanptr)->vnic.num_rcv_bufs);		\
		net.mtu = readl(&((ULTRA_IO_CHANNEL_PROTOCOL __iomem *) \
				  chanptr)->vnic.mtu);			\
		memcpy_fromio(&net.zoneGuid, \
			      &((ULTRA_IO_CHANNEL_PROTOCOL __iomem *)	\
				chanptr)->vnic.zoneGuid,		\
			      sizeof(uuid_le));				\
}

/* adds a vnic
 * returns 0 failure, 1 success,
 */
static int
add_vnic(struct add_virt_guestpart *addparams)
{
	int i;
	struct net_adap_info net;
	struct device *vbus;
	unsigned char busid[BUS_ID_SIZE];

	POSTCODE_LINUX_2(VPCI_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	if (!WAIT_FOR_IO_CHANNEL
	    ((ULTRA_IO_CHANNEL_PROTOCOL __iomem *) addparams->chanptr)) {
		LOGERR("Timed out, channel not ready\n");
		POSTCODE_LINUX_2(VPCI_CREATE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		return 0;
	}

	GET_NETADAPINFO_FROM_CHANPTR(addparams->chanptr);

	GET_BUS_DEV(addparams->busNo);

	LOGINF("Adding vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x rcvbufs:%d mtu:%d chanptr:%p%pUL\n",
	     net.mac_addr[0], net.mac_addr[1], net.mac_addr[2], net.mac_addr[3],
	     net.mac_addr[4], net.mac_addr[5], net.num_rcv_bufs, net.mtu,
	     addparams->chanptr, &net.zoneGuid);
	i = virtpci_device_add(vbus, VIRTNIC_TYPE, addparams, NULL, &net);
	if (i) {
		LOGINF("Added vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
		       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
		POSTCODE_LINUX_3(VPCI_CREATE_EXIT_PC, i,
				 POSTCODE_SEVERITY_INFO);
		return 1;
	}
	return 0;
}

/* delete vbus
 * returns 0 failure, 1 success,
 */
static int
delete_vbus(struct del_vbus_guestpart *delparams)
{
	struct device *vbus;
	unsigned char busid[BUS_ID_SIZE];

	GET_BUS_DEV(delparams->bus_no);
	/* ensure that bus has no devices? -- TBD */
	LOGINF("Deleting %s\n", BUS_ID(vbus));
	if (delete_vbus_device(vbus, NULL))
		return 0;	/* failure */
	LOGINF("Deleted vbus %d\n", delparams->bus_no);
	return 1;
}

static int
delete_vbus_device(struct device *vbus, void *data)
{
	int checkforroot = (data != NULL);
	struct device *pDev = &virtpci_rootbus_device;

	if ((checkforroot) && match_busid(vbus, (void *) BUS_ID(pDev))) {
		/* skip it - don't delete root bus */
		LOGINF("skipping root bus\n");
		return 0;	/* pretend no error */
	}
	LOGINF("Calling unregister for %s\n", BUS_ID(vbus));
	device_unregister(vbus);
	kfree(vbus);
	LOGINF("VBus unregister and freed\n");
	return 0;		/* no error */
}

/* pause vhba
* returns 0 failure, 1 success,
*/
static int pause_vhba(struct pause_virt_guestpart *pauseparams)
{
	int i;
	struct scsi_adap_info scsi;

	GET_SCSIADAPINFO_FROM_CHANPTR(pauseparams->chanptr);

	LOGINF("Pausing vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1, scsi.wwnn.wwnn2);
	i = virtpci_device_serverdown(NULL /*no parent bus */ , VIRTHBA_TYPE,
				      &scsi.wwnn, NULL);
	if (i)
		LOGINF("Paused vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1,
		       scsi.wwnn.wwnn2);
	return i;
}

/* pause vnic
 * returns 0 failure, 1 success,
 */
static int pause_vnic(struct pause_virt_guestpart *pauseparams)
{
	int i;
	struct net_adap_info net;

	GET_NETADAPINFO_FROM_CHANPTR(pauseparams->chanptr);

	LOGINF("Pausing vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
	       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
	       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	i = virtpci_device_serverdown(NULL /*no parent bus */ , VIRTNIC_TYPE,
				      NULL, net.mac_addr);
	if (i) {
		LOGINF(" Paused vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
		       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	}
	return i;
}

/* resume vhba
 * returns 0 failure, 1 success,
 */
static int resume_vhba(struct resume_virt_guestpart *resumeparams)
{
	int i;
	struct scsi_adap_info scsi;

	GET_SCSIADAPINFO_FROM_CHANPTR(resumeparams->chanptr);

	LOGINF("Resuming vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1, scsi.wwnn.wwnn2);
	i = virtpci_device_serverup(NULL /*no parent bus */ , VIRTHBA_TYPE,
				    &scsi.wwnn, NULL);
	if (i)
		LOGINF("Resumed vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1,
		       scsi.wwnn.wwnn2);
	return i;
}

/* resume vnic
* returns 0 failure, 1 success,
*/
static int
resume_vnic(struct resume_virt_guestpart *resumeparams)
{
	int i;
	struct net_adap_info net;

	GET_NETADAPINFO_FROM_CHANPTR(resumeparams->chanptr);

	LOGINF("Resuming vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
	       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
	       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	i = virtpci_device_serverup(NULL /*no parent bus */ , VIRTNIC_TYPE,
				    NULL, net.mac_addr);
	if (i) {
		LOGINF(" Resumed vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
		       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	}
	return i;
}

/* delete vhba
* returns 0 failure, 1 success,
*/
static int delete_vhba(struct del_virt_guestpart *delparams)
{
	int i;
	struct scsi_adap_info scsi;

	GET_SCSIADAPINFO_FROM_CHANPTR(delparams->chanptr);

	LOGINF("Deleting vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1, scsi.wwnn.wwnn2);
	i = virtpci_device_del(NULL /*no parent bus */ , VIRTHBA_TYPE,
			       &scsi.wwnn, NULL);
	if (i) {
		LOGINF("Deleted vhba wwnn:%x:%x\n", scsi.wwnn.wwnn1,
		       scsi.wwnn.wwnn2);
		return 1;
	}
	return 0;
}

/* deletes a vnic
 * returns 0 failure, 1 success,
 */
static int delete_vnic(struct del_virt_guestpart *delparams)
{
	int i;
	struct net_adap_info net;

	GET_NETADAPINFO_FROM_CHANPTR(delparams->chanptr);

	LOGINF("Deleting vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
	       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
	       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	i = virtpci_device_del(NULL /*no parent bus */ , VIRTNIC_TYPE, NULL,
			       net.mac_addr);
	if (i) {
		LOGINF("Deleted vnic macaddr:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       net.mac_addr[0], net.mac_addr[1], net.mac_addr[2],
		       net.mac_addr[3], net.mac_addr[4], net.mac_addr[5]);
	}
	return i;
}

#define DELETE_ONE_VPCIDEV(vpcidev) { \
	LOGINF("calling device_unregister:%p\n", &vpcidev->generic_dev); \
	device_unregister(&vpcidev->generic_dev); \
	LOGINF("Deleted %p\n", vpcidev); \
	kfree(vpcidev); \
}

/* deletes all vhbas and vnics
 * returns 0 failure, 1 success,
 */
static void delete_all(void)
{
	int count = 0;
	unsigned long flags;
	struct virtpci_dev *tmpvpcidev, *nextvpcidev;

	/* delete the entire vhba/vnic list in one shot */
	write_lock_irqsave(&VpcidevListLock, flags);
	tmpvpcidev = VpcidevListHead;
	VpcidevListHead = NULL;
	write_unlock_irqrestore(&VpcidevListLock, flags);

	/* delete one vhba/vnic at a time */
	while (tmpvpcidev) {
		nextvpcidev = tmpvpcidev->next;
		/* delete the vhba/vnic at tmpvpcidev */
		DELETE_ONE_VPCIDEV(tmpvpcidev);
		tmpvpcidev = nextvpcidev;
		count++;
	}
	LOGINF("Deleted %d vhbas/vnics.\n", count);

	/* now delete each vbus */
	if (bus_for_each_dev
	    (&virtpci_bus_type, NULL, (void *) 1, delete_vbus_device))
		LOGERR("delete of all vbus failed\n");
}

/* deletes all vnics or vhbas
 * returns 0 failure, 1 success,
 */
static int delete_all_virt(VIRTPCI_DEV_TYPE devtype, struct del_vbus_guestpart *delparams)
{
	int i;
	unsigned char busid[BUS_ID_SIZE];
	struct device *vbus;

	GET_BUS_DEV(delparams->bus_no);

	if ((devtype != VIRTHBA_TYPE) && (devtype != VIRTNIC_TYPE)) {
		LOGERR("**** FAILED to delete all devices; devtype:%d not vhba:%d or vnic:%d\n",
		     devtype, VIRTHBA_TYPE, VIRTNIC_TYPE);
		return 0;
	}

	LOGINF("Deleting all %s in vbus %s\n",
	       devtype == VIRTHBA_TYPE ? "vhbas" : "vnics", busid);
	/* delete all vhbas/vnics */
	i = virtpci_device_del(vbus, devtype, NULL, NULL);
	if (i > 0)
		LOGINF("Deleted %d %s\n", i,
		       devtype == VIRTHBA_TYPE ? "vhbas" : "vnics");
	return 1;
}

static int virtpci_ctrlchan_func(struct guest_msgs *msg)
{
	switch (msg->msgtype) {
	case GUEST_ADD_VBUS:
		return add_vbus(&msg->add_vbus);
	case GUEST_ADD_VHBA:
		return add_vhba(&msg->add_vhba);
	case GUEST_ADD_VNIC:
		return add_vnic(&msg->add_vnic);
	case GUEST_DEL_VBUS:
		return delete_vbus(&msg->del_vbus);
	case GUEST_DEL_VHBA:
		return delete_vhba(&msg->del_vhba);
	case GUEST_DEL_VNIC:
		return delete_vnic(&msg->del_vhba);
	case GUEST_DEL_ALL_VHBAS:
		return delete_all_virt(VIRTHBA_TYPE, &msg->del_all_vhbas);
	case GUEST_DEL_ALL_VNICS:
		return delete_all_virt(VIRTNIC_TYPE, &msg->del_all_vnics);
	case GUEST_DEL_ALL_VBUSES:
		delete_all();
		return 1;
	case GUEST_PAUSE_VHBA:
		return pause_vhba(&msg->pause_vhba);
	case GUEST_PAUSE_VNIC:
		return pause_vnic(&msg->pause_vnic);
	case GUEST_RESUME_VHBA:
		return resume_vhba(&msg->resume_vhba);
	case GUEST_RESUME_VNIC:
		return resume_vnic(&msg->resume_vnic);
	default:
		LOGERR("invalid message type %d.\n", msg->msgtype);
		return 0;
	}
}

/* same as driver_helper in bus.c linux */
static int match_busid(struct device *dev, void *data)
{
	const char *name = data;

	if (strcmp(name, BUS_ID(dev)) == 0)
		return 1;
	return 0;
}

/*****************************************************/
/*  Bus functions                                    */
/*****************************************************/

static const struct pci_device_id *
virtpci_match_device(const struct pci_device_id *ids,
		     const struct virtpci_dev *dev)
{
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		DBGINF("ids->vendor:%x dev->vendor:%x ids->device:%x dev->device:%x\n",
		     ids->vendor, dev->vendor, ids->device, dev->device);

		if ((ids->vendor == dev->vendor)
		    && (ids->device == dev->device))
			return ids;

		ids++;
	}
	return NULL;
}

/* NOTE: !!!!!!  This function is called when a new device is added
* for this bus.  Or, it is called for existing devices when a new
* driver is added for this bus.  It returns nonzero if a given device
* can be handled by the given driver.
*/
static int virtpci_bus_match(struct device *dev, struct device_driver *drv)
{
	struct virtpci_dev *virtpcidev = device_to_virtpci_dev(dev);
	struct virtpci_driver *virtpcidrv = driver_to_virtpci_driver(drv);
	int match = 0;

	DBGINF("In virtpci_bus_match dev->bus_id:%s drv->name:%s\n",
	       dev->bus_id, drv->name);

	/* check ids list for a match */
	if (virtpci_match_device(virtpcidrv->id_table, virtpcidev))
		match = 1;

	DBGINF("returning match:%d\n", match);
	return match;		/* 0 - no match; 1 - yes it matches */
}

static int virtpci_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	DBGINF("In virtpci_hotplug\n");
	/* add variables to the environment prior to the generation of
	 * hotplug events to user space
	 */
	if (add_uevent_var(env, "VIRTPCI_VERSION=%s", VIRTPCI_VERSION))
		return -ENOMEM;
	return 0;
}

static int virtpci_device_suspend(struct device *dev, pm_message_t state)
{
	DBGINF("In virtpci_device_suspend -NYI ****\n");
	return 0;
}

static int virtpci_device_resume(struct device *dev)
{
	DBGINF("In virtpci_device_resume -NYI ****\n");
	return 0;
}

/* For a child device just created on a client bus, fill in
 * information about the driver that is controlling this device into
 * the the appropriate slot within the vbus channel of the bus
 * instance.
 */
static void fix_vbus_devInfo(struct device *dev, int devNo, int devType,
			     struct virtpci_driver *virtpcidrv)
{
	struct device *vbus;
	void *pChan;
	ULTRA_VBUS_DEVICEINFO devInfo;
	const char *stype;

	if (!dev) {
		LOGERR("%s dev is NULL", __func__);
		return;
	}
	if (!virtpcidrv) {
		LOGERR("%s driver is NULL", __func__);
		return;
	}
	vbus = dev->parent;
	if (!vbus) {
		LOGERR("%s dev has no parent bus", __func__);
		return;
	}
	pChan = vbus->platform_data;
	if (!pChan) {
		LOGERR("%s dev bus has no channel", __func__);
		return;
	}
	switch (devType) {
	case PCI_DEVICE_ID_VIRTHBA:
		stype = "vHBA";
		break;
	case PCI_DEVICE_ID_VIRTNIC:
		stype = "vNIC";
		break;
	default:
		stype = "unknown";
		break;
	}
	BusDeviceInfo_Init(&devInfo, stype,
			   virtpcidrv->name,
			   virtpcidrv->version,
			   virtpcidrv->vertag);
	write_vbus_devInfo(pChan, &devInfo, devNo);

	/* Re-write bus+chipset info, because it is possible that this
	* was previously written by our good counterpart, visorbus.
	*/
	write_vbus_chpInfo(pChan, &Chipset_DriverInfo);
	write_vbus_busInfo(pChan, &Bus_DriverInfo);
}

/* This function is called to query the existence of a specific device
* and whether this driver can work with it.  It should return -ENODEV
* in case of failure.
*/
static int virtpci_device_probe(struct device *dev)
{
	struct virtpci_dev *virtpcidev = device_to_virtpci_dev(dev);
	struct virtpci_driver *virtpcidrv =
	    driver_to_virtpci_driver(dev->driver);
	const struct pci_device_id *id;
	int error = 0;

	LOGINF("In virtpci_device_probe dev:%p virtpcidev:%p virtpcidrv:%p\n",
	       dev, virtpcidev, virtpcidrv);	/* VERBOSE/DEBUG ? */
	POSTCODE_LINUX_2(VPCI_PROBE_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	/* static match and static probe vs dynamic match & dynamic
	 * probe - do we care?.
	 */
	if (!virtpcidrv->id_table)
		return -ENODEV;

	id = virtpci_match_device(virtpcidrv->id_table, virtpcidev);
	if (!id)
		return -ENODEV;

	/* increment reference count */
	get_device(dev);

	/* if virtpcidev is not already claimed & probe function is
	 * valid, probe it
	 */
	if (!virtpcidev->mydriver && virtpcidrv->probe) {
		/* call the probe function - virthba or virtnic probe
		 * is what it should be
		 */
		error = virtpcidrv->probe(virtpcidev, id);
		if (!error) {
			fix_vbus_devInfo(dev, virtpcidev->deviceNo,
					 virtpcidev->device, virtpcidrv);
			virtpcidev->mydriver = virtpcidrv;
			POSTCODE_LINUX_2(VPCI_PROBE_EXIT_PC,
					 POSTCODE_SEVERITY_INFO);
		} else
			put_device(dev);
	}
	POSTCODE_LINUX_2(VPCI_PROBE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
	return error;		/* -ENODEV for probe failure */
}

static int virtpci_device_remove(struct device *dev_)
{
	/* dev_ passed in is the HBA device which we called
	* generic_dev in our virtpcidev struct
	*/
	struct virtpci_dev *virtpcidev = device_to_virtpci_dev(dev_);
	struct virtpci_driver *virtpcidrv = virtpcidev->mydriver;

	LOGINF("In virtpci_device_remove bus_id:%s dev_:%p virtpcidev:%p dev->driver:%p drivername:%s\n",
	       BUS_ID(dev_), dev_, virtpcidev, dev_->driver,
	       dev_->driver->name);	/* VERBOSE/DEBUG */
	if (virtpcidrv) {
		/* TEMP: assuming we have only one such driver for now */
		if (virtpcidrv->remove)
			virtpcidrv->remove(virtpcidev);
		virtpcidev->mydriver = NULL;
	}

	DBGINF("calling putdevice\n");
	put_device(dev_);

	DBGINF("Leaving\n");
	return 0;
}

/*****************************************************/
/* Bus functions                                     */
/*****************************************************/

static void virtpci_bus_release(struct device *dev)
{
	/* this function is called when the last reference to the
	 * device is removed
	 */
	DBGINF("In virtpci_bus_release\n");
	/* what else is supposed to happen here? */
}

/*****************************************************/
/* Adapter functions                                 */
/*****************************************************/

static int virtpci_device_add(struct device *parentbus, int devtype,
			      struct add_virt_guestpart *addparams,
			      struct scsi_adap_info *scsi, /* NULL for VNIC add */
			      struct net_adap_info *net	/* NULL for VHBA add */)
{
	struct virtpci_dev *virtpcidev = NULL;
	struct virtpci_dev *tmpvpcidev = NULL, *prev;
	unsigned long flags;
	int ret;
	ULTRA_IO_CHANNEL_PROTOCOL __iomem *pIoChan = NULL;
	struct device *pDev;

	LOGINF("virtpci_device_add parentbus:%p chanptr:%p\n", parentbus,
	       addparams->chanptr);

	POSTCODE_LINUX_2(VPCI_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	if ((devtype != VIRTHBA_TYPE) && (devtype != VIRTNIC_TYPE)) {
		LOGERR("**** FAILED to add device; devtype:%d not vhba:%d or vnic:%d\n",
		     devtype, VIRTHBA_TYPE, VIRTNIC_TYPE);
		POSTCODE_LINUX_3(VPCI_CREATE_FAILURE_PC, devtype,
				 POSTCODE_SEVERITY_ERR);
		return 0;
	}

	/* add a Virtual Device */
	virtpcidev = kzalloc(sizeof(struct virtpci_dev), GFP_ATOMIC);
	if (virtpcidev == NULL) {
		LOGERR("can't add device - malloc FALLED\n");
		POSTCODE_LINUX_2(MALLOC_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		return 0;
	}

	/* initialize stuff unique to virtpci_dev struct */
	virtpcidev->devtype = devtype;
	if (devtype == VIRTHBA_TYPE) {
		virtpcidev->device = PCI_DEVICE_ID_VIRTHBA;
		virtpcidev->scsi = *scsi;
	} else {
		virtpcidev->device = PCI_DEVICE_ID_VIRTNIC;
		virtpcidev->net = *net;
	}
	virtpcidev->vendor = PCI_VENDOR_ID_UNISYS;
	virtpcidev->busNo = addparams->busNo;
	virtpcidev->deviceNo = addparams->deviceNo;

	virtpcidev->queueinfo.chan = addparams->chanptr;
	virtpcidev->queueinfo.send_int_if_needed = NULL;

	/* Set up safe queue... */
	pIoChan = (ULTRA_IO_CHANNEL_PROTOCOL __iomem *)
		virtpcidev->queueinfo.chan;

	virtpcidev->intr = addparams->intr;

	/* initialize stuff in the device portion of the struct */
	virtpcidev->generic_dev.bus = &virtpci_bus_type;
	virtpcidev->generic_dev.parent = parentbus;
	virtpcidev->generic_dev.release = virtpci_device_release;

	dev_set_name(&virtpcidev->generic_dev, "%x:%x",
		     addparams->busNo, addparams->deviceNo);

	/* add the vhba/vnic to virtpci device list - but check for
	 * duplicate wwnn/macaddr first
	 */
	write_lock_irqsave(&VpcidevListLock, flags);
	for (tmpvpcidev = VpcidevListHead; tmpvpcidev;
	     tmpvpcidev = tmpvpcidev->next) {
		if (devtype == VIRTHBA_TYPE) {
			if ((tmpvpcidev->scsi.wwnn.wwnn1 == scsi->wwnn.wwnn1) &&
			    (tmpvpcidev->scsi.wwnn.wwnn2 == scsi->wwnn.wwnn2)) {
				/* duplicate - already have vpcidev
				   with this wwnn */
				break;
			}
		} else
		    if (memcmp
			(tmpvpcidev->net.mac_addr, net->mac_addr,
			 MAX_MACADDR_LEN) == 0) {
			/* duplicate - already have vnic with this wwnn */
			break;
		}
	}
	if (tmpvpcidev) {
		/* found a vhba/vnic already in the list with same
		 * wwnn or macaddr - reject add
		 */
		write_unlock_irqrestore(&VpcidevListLock, flags);
		kfree(virtpcidev);
		LOGERR("**** FAILED vhba/vnic already exists in the list\n");
		POSTCODE_LINUX_2(VPCI_CREATE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		return 0;
	}

	/* add it at the head */
	if (!VpcidevListHead)
		VpcidevListHead = virtpcidev;
	else {
		/* insert virtpcidev at the head of our linked list of
		 * vpcidevs
		 */
		virtpcidev->next = VpcidevListHead;
		VpcidevListHead = virtpcidev;
	}

	write_unlock_irqrestore(&VpcidevListLock, flags);

	/* Must transition channel to ATTACHED state BEFORE
	 * registering the device, because polling of the channel
	 * queues can begin at any time after device_register().
	 */
	pDev = &virtpcidev->generic_dev;
	ULTRA_CHANNEL_CLIENT_TRANSITION(addparams->chanptr,
					BUS_ID(pDev),
					CHANNELCLI_ATTACHED, NULL);

	/* don't register until device has been added to
	* list. Otherwise, a device_unregister from this function can
	* cause a "scheduling while atomic".
	*/
	DBGINF("registering device:%p with bus_id:%s\n",
	       &virtpcidev->generic_dev, virtpcidev->generic_dev.bus_id);
	ret = device_register(&virtpcidev->generic_dev);
	/* NOTE: THIS IS CALLING HOTPLUG virtpci_hotplug!!!
	 * This call to device_register results in virtpci_bus_match
	 * being called !!!!!  And, if match returns success, then
	 * virtpcidev->generic_dev.driver is setup to core_driver,
	 * i.e., virtpci and the probe function
	 * virtpcidev->generic_dev.driver->probe is called which
	 * results in virtpci_device_probe being called. And if
	 * virtpci_device_probe is successful
	 */
	if (ret) {
		LOGERR("device_register returned %d\n", ret);
		pDev = &virtpcidev->generic_dev;
		ULTRA_CHANNEL_CLIENT_TRANSITION(addparams->chanptr,
						BUS_ID(pDev),
						CHANNELCLI_DETACHED, NULL);
		/* remove virtpcidev, the one we just added, from the list */
		write_lock_irqsave(&VpcidevListLock, flags);
		for (tmpvpcidev = VpcidevListHead, prev = NULL;
		     tmpvpcidev;
		     prev = tmpvpcidev, tmpvpcidev = tmpvpcidev->next) {
			if (tmpvpcidev == virtpcidev) {
				if (prev)
					prev->next = tmpvpcidev->next;
				else
					VpcidevListHead = tmpvpcidev->next;
				break;
			}
		}
		write_unlock_irqrestore(&VpcidevListLock, flags);
		kfree(virtpcidev);
		return 0;
	}

	LOGINF("Added %s:%d:%d &virtpcidev->generic_dev:%p\n",
	       (devtype == VIRTHBA_TYPE) ? "virthba" : "virtnic",
	       addparams->busNo, addparams->deviceNo, &virtpcidev->generic_dev);
	POSTCODE_LINUX_2(VPCI_CREATE_EXIT_PC, POSTCODE_SEVERITY_INFO);
	return 1;
}

static int virtpci_device_serverdown(struct device *parentbus,
				     int devtype,
				     struct vhba_wwnn *wwnn,
				     unsigned char macaddr[])
{
	int pausethisone = 0;
	bool found = false;
	struct virtpci_dev *tmpvpcidev, *prevvpcidev;
	struct virtpci_driver *vpcidriver;
	unsigned long flags;
	int rc = 0;

	if ((devtype != VIRTHBA_TYPE) && (devtype != VIRTNIC_TYPE)) {
		LOGERR("**** FAILED to pause device; devtype:%d not vhba:%d or vnic:%d\n",
		       devtype, VIRTHBA_TYPE, VIRTNIC_TYPE);
		return 0;
	}

	/* find the vhba or vnic in virtpci device list */
	write_lock_irqsave(&VpcidevListLock, flags);

	for (tmpvpcidev = VpcidevListHead, prevvpcidev = NULL;
	     (tmpvpcidev && !found);
	     prevvpcidev = tmpvpcidev, tmpvpcidev = tmpvpcidev->next) {
		if (tmpvpcidev->devtype != devtype)
			continue;

		if (devtype == VIRTHBA_TYPE) {
			pausethisone =
			    ((tmpvpcidev->scsi.wwnn.wwnn1 == wwnn->wwnn1) &&
			     (tmpvpcidev->scsi.wwnn.wwnn2 == wwnn->wwnn2));
			/* devtype is vhba, we're pausing vhba whose
			* wwnn matches the current device's wwnn
			*/
		} else {	/* VIRTNIC_TYPE */
			pausethisone =
			    memcmp(tmpvpcidev->net.mac_addr, macaddr,
				   MAX_MACADDR_LEN) == 0;
			/* devtype is vnic, we're pausing vnic whose
			* macaddr matches the current device's macaddr */
		}

		if (!pausethisone)
			continue;

		found = true;
		vpcidriver = tmpvpcidev->mydriver;
		rc = vpcidriver->suspend(tmpvpcidev, 0);
	}
	write_unlock_irqrestore(&VpcidevListLock, flags);

	if (!found) {
		LOGERR("**** FAILED to find vhba/vnic in the list\n");
		return 0;
	}

	return rc;
}

static int virtpci_device_serverup(struct device *parentbus,
				   int devtype,
				   struct vhba_wwnn *wwnn,
				   unsigned char macaddr[])
{
	int resumethisone = 0;
	bool found = false;
	struct virtpci_dev *tmpvpcidev, *prevvpcidev;
	struct virtpci_driver *vpcidriver;
	unsigned long flags;
	int rc = 0;

	if ((devtype != VIRTHBA_TYPE) && (devtype != VIRTNIC_TYPE)) {
		LOGERR("**** FAILED to resume device; devtype:%d not vhba:%d or vnic:%d\n",
		       devtype, VIRTHBA_TYPE, VIRTNIC_TYPE);
		return 0;
	}

	/* find the vhba or vnic in virtpci device list */
	write_lock_irqsave(&VpcidevListLock, flags);

	for (tmpvpcidev = VpcidevListHead, prevvpcidev = NULL;
	     (tmpvpcidev && !found);
	     prevvpcidev = tmpvpcidev, tmpvpcidev = tmpvpcidev->next) {
		if (tmpvpcidev->devtype != devtype)
			continue;

		if (devtype == VIRTHBA_TYPE) {
			resumethisone =
			    ((tmpvpcidev->scsi.wwnn.wwnn1 == wwnn->wwnn1) &&
			     (tmpvpcidev->scsi.wwnn.wwnn2 == wwnn->wwnn2));
			/* devtype is vhba, we're resuming vhba whose
			* wwnn matches the current device's wwnn */
		} else {	/* VIRTNIC_TYPE */
			resumethisone =
			    memcmp(tmpvpcidev->net.mac_addr, macaddr,
				   MAX_MACADDR_LEN) == 0;
			/* devtype is vnic, we're resuming vnic whose
			* macaddr matches the current device's macaddr */
		}

		if (!resumethisone)
			continue;

		found = true;
		vpcidriver = tmpvpcidev->mydriver;
		/* This should be done at BUS resume time, but an
		* existing problem prevents us from ever getting a bus
		* resume...  This hack would fail to work should we
		* ever have a bus that contains NO devices, since we
		* would never even get here in that case.
		*/
		fix_vbus_devInfo(&tmpvpcidev->generic_dev, tmpvpcidev->deviceNo,
				 tmpvpcidev->device, vpcidriver);
		rc = vpcidriver->resume(tmpvpcidev);
	}

	write_unlock_irqrestore(&VpcidevListLock, flags);

	if (!found) {
		LOGERR("**** FAILED to find vhba/vnic in the list\n");
		return 0;
	}

	return rc;
}

static int virtpci_device_del(struct device *parentbus,
			      int devtype, struct vhba_wwnn *wwnn,
			      unsigned char macaddr[])
{
	int count = 0, all = 0, delthisone;
	struct virtpci_dev *tmpvpcidev, *prevvpcidev, *dellist = NULL;
	unsigned long flags;

#define DEL_CONTINUE { \
	prevvpcidev = tmpvpcidev;\
	tmpvpcidev = tmpvpcidev->next;\
	continue; \
}

	if ((devtype != VIRTHBA_TYPE) && (devtype != VIRTNIC_TYPE)) {
		LOGERR("**** FAILED to delete device; devtype:%d not vhba:%d or vnic:%d\n",
		       devtype, VIRTHBA_TYPE, VIRTNIC_TYPE);
		return 0;
	}

	/* see if we are to delete all - NOTE: all implies we have a
	 * valid parentbus
	 */
	all = ((devtype == VIRTHBA_TYPE) && (wwnn == NULL)) ||
	    ((devtype == VIRTNIC_TYPE) && (macaddr == NULL));

	/* find all the vhba or vnic or both in virtpci device list
	* keep list of ones we are deleting so we can call
	* device_unregister after we release the lock; otherwise we
	* encounter "schedule while atomic"
	*/
	write_lock_irqsave(&VpcidevListLock, flags);
	for (tmpvpcidev = VpcidevListHead, prevvpcidev = NULL; tmpvpcidev;) {
		if (tmpvpcidev->devtype != devtype)
			DEL_CONTINUE;

		if (all) {
			delthisone =
			    (tmpvpcidev->generic_dev.parent == parentbus);
			/* we're deleting all vhbas or vnics on the
			 * specified parent bus
			 */
		} else if (devtype == VIRTHBA_TYPE) {
			delthisone =
			    ((tmpvpcidev->scsi.wwnn.wwnn1 == wwnn->wwnn1) &&
			     (tmpvpcidev->scsi.wwnn.wwnn2 == wwnn->wwnn2));
			/* devtype is vhba, we're deleting vhba whose
			 * wwnn matches the current device's wwnn
			 */
		} else {	/* VIRTNIC_TYPE */
			delthisone =
			    memcmp(tmpvpcidev->net.mac_addr, macaddr,
				   MAX_MACADDR_LEN) == 0;
			/* devtype is vnic, we're deleting vnic whose
			* macaddr matches the current device's macaddr
			*/
		}

		if (!delthisone)
			DEL_CONTINUE;

		/* take vhba/vnic out of the list */
		if (prevvpcidev)
			/* not at head */
			prevvpcidev->next = tmpvpcidev->next;
		else
			VpcidevListHead = tmpvpcidev->next;

		/* add it to our deletelist */
		tmpvpcidev->next = dellist;
		dellist = tmpvpcidev;

		count++;
		if (!all)
			break;	/* done */
		/* going to top of loop again - set tmpvpcidev to next
		 * one we're to process
		 */
		if (prevvpcidev)
			tmpvpcidev = prevvpcidev->next;
		else
			tmpvpcidev = VpcidevListHead;
	}
	write_unlock_irqrestore(&VpcidevListLock, flags);

	if (!all && (count == 0)) {
		LOGERR("**** FAILED to find vhba/vnic in the list\n");
		return 0;
	}

	/* now delete each one from delete list */
	while (dellist) {
		/* save next */
		tmpvpcidev = dellist->next;
		/* delete the vhba/vnic at dellist */
		DELETE_ONE_VPCIDEV(dellist);
		/* do next */
		dellist = tmpvpcidev;
	}

	return count;
}

static void virtpci_device_release(struct device *dev_)
{
	/* this function is called when the last reference to the
	 * device is removed
	 */
	LOGINF("In virtpci_device_release:%p - NOT YET IMPLEMENTED\n", dev_);
}

/*****************************************************/
/* Driver functions                                  */
/*****************************************************/

#define kobj_to_device_driver(obj) container_of(obj, struct device_driver, kobj)
#define attribute_to_driver_attribute(obj) \
	container_of(obj, struct driver_attribute, attr)

static ssize_t virtpci_driver_attr_show(struct kobject *kobj,
					struct attribute *attr,
					char *buf)
{
	struct driver_attribute *dattr = attribute_to_driver_attribute(attr);
	ssize_t ret = 0;

	struct driver_private *dprivate = to_driver(kobj);
	struct device_driver *driver;

	if (dprivate != NULL)
		driver = dprivate->driver;
	else
		driver = NULL;

	DBGINF("In virtpci_driver_attr_show driver->name:%s\n", driver->name);
	if (driver) {
		if (dattr->show)
			ret = dattr->show(driver, buf);
	}
	return ret;
}

static ssize_t virtpci_driver_attr_store(struct kobject *kobj,
					 struct attribute *attr,
					 const char *buf, size_t count)
{
	struct driver_attribute *dattr = attribute_to_driver_attribute(attr);
	ssize_t ret = 0;

	struct driver_private *dprivate = to_driver(kobj);
	struct device_driver *driver;

	if (dprivate != NULL)
		driver = dprivate->driver;
	else
		driver = NULL;

	DBGINF("In virtpci_driver_attr_store driver->name:%s\n", driver->name);

	if (driver) {
		if (dattr->store)
			ret = dattr->store(driver, buf, count);
	}
	return ret;
}

/* register a new virtpci driver */
int virtpci_register_driver(struct virtpci_driver *drv)
{
	int result = 0;

	DBGINF("In virtpci_register_driver\n");

	if (drv->id_table == NULL) {
		LOGERR("id_table missing\n");
		return 1;
	}
	/* initialize core driver fields needed to call driver_register */
	drv->core_driver.name = drv->name;	/* name of driver in sysfs */
	drv->core_driver.bus = &virtpci_bus_type;	/* type of bus this
							 * driver works with */
	drv->core_driver.probe = virtpci_device_probe;	/* called to query the
							 * existence of a
							 * specific device and
							 * whether this driver
							 *can work with it */
	drv->core_driver.remove = virtpci_device_remove; /* called when the
							  * device is removed
							  * from the system */
	/* register with core */
	result = driver_register(&drv->core_driver);
	/* calls bus_add_driver which calls driver_attach and
	 * module_add_driver
	 */
	if (result)
		return result;	/* failed */

	drv->core_driver.p->kobj.ktype = &virtpci_driver_kobj_type;

	return 0;
}
EXPORT_SYMBOL_GPL(virtpci_register_driver);

void virtpci_unregister_driver(struct virtpci_driver *drv)
{
	DBGINF("In virtpci_unregister_driver drv:%p\n", drv);
	driver_unregister(&drv->core_driver);
	/* driver_unregister calls bus_remove_driver
	 * bus_remove_driver calls device_detach
	 * device_detach calls device_release_driver for each of the
	 * driver's devices
	 * device_release driver calls drv->remove which is
	 * virtpci_device_remove
	 * virtpci_device_remove calls virthba_remove
	 */
	DBGINF("Leaving\n");
}
EXPORT_SYMBOL_GPL(virtpci_unregister_driver);

/*****************************************************/
/* debugfs filesystem functions                      */
/*****************************************************/
struct print_vbus_info {
	int *str_pos;
	char *buf;
	size_t *len;
};

static int print_vbus(struct device *vbus, void *data)
{
	struct print_vbus_info *p = (struct print_vbus_info *)data;

	*p->str_pos += scnprintf(p->buf + *p->str_pos, *p->len - *p->str_pos,
				"bus_id:%s\n", dev_name(vbus));
	return 0;
}

static ssize_t info_debugfs_read(struct file *file, char __user *buf,
			      size_t len, loff_t *offset)
{
	ssize_t bytes_read = 0;
	int str_pos = 0;
	struct virtpci_dev *tmpvpcidev;
	unsigned long flags;
	struct print_vbus_info printparam;
	char *vbuf;

	if (len > MAX_BUF)
		len = MAX_BUF;
	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	str_pos += scnprintf(vbuf + str_pos, len - str_pos,
			" Virtual PCI Bus devices\n");
	printparam.str_pos = &str_pos;
	printparam.buf = vbuf;
	printparam.len = &len;
	if (bus_for_each_dev(&virtpci_bus_type, NULL,
			     (void *) &printparam, print_vbus))
		LOGERR("Failed to find bus\n");

	str_pos += scnprintf(vbuf + str_pos, len - str_pos,
			"\n Virtual PCI devices\n");
	read_lock_irqsave(&VpcidevListLock, flags);
	tmpvpcidev = VpcidevListHead;
	while (tmpvpcidev) {
		if (tmpvpcidev->devtype == VIRTHBA_TYPE) {
			str_pos += scnprintf(vbuf + str_pos, len - str_pos,
					"[%d:%d] VHba:%08x:%08x max-config:%d-%d-%d-%d",
					tmpvpcidev->busNo, tmpvpcidev->deviceNo,
					tmpvpcidev->scsi.wwnn.wwnn1,
					tmpvpcidev->scsi.wwnn.wwnn2,
					tmpvpcidev->scsi.max.max_channel,
					tmpvpcidev->scsi.max.max_id,
					tmpvpcidev->scsi.max.max_lun,
					tmpvpcidev->scsi.max.cmd_per_lun);
		} else {
			str_pos += scnprintf(vbuf + str_pos, len - str_pos,
					"[%d:%d] VNic:%02x:%02x:%02x:%02x:%02x:%02x num_rcv_bufs:%d mtu:%d",
					tmpvpcidev->busNo, tmpvpcidev->deviceNo,
					tmpvpcidev->net.mac_addr[0],
					tmpvpcidev->net.mac_addr[1],
					tmpvpcidev->net.mac_addr[2],
					tmpvpcidev->net.mac_addr[3],
					tmpvpcidev->net.mac_addr[4],
					tmpvpcidev->net.mac_addr[5],
					tmpvpcidev->net.num_rcv_bufs,
					tmpvpcidev->net.mtu);
		}
		str_pos += scnprintf(vbuf + str_pos,
				len - str_pos, " chanptr:%p\n",
				tmpvpcidev->queueinfo.chan);
				tmpvpcidev = tmpvpcidev->next;
	}
	read_unlock_irqrestore(&VpcidevListLock, flags);

	str_pos += scnprintf(vbuf + str_pos, len - str_pos, "\n");
	bytes_read = simple_read_from_buffer(buf, len, offset, vbuf, str_pos);
	kfree(vbuf);
	return bytes_read;
}

/*****************************************************/
/* Module Init & Exit functions                      */
/*****************************************************/

static int __init virtpci_mod_init(void)
{
	int ret;


	if (!unisys_spar_platform)
		return -ENODEV;

	POSTCODE_LINUX_2(VPCI_CREATE_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	ret = bus_register(&virtpci_bus_type);
	/* creates /sys/bus/uisvirtpci which contains devices &
	 * drivers directory
	 */
	if (ret) {
		LOGERR("bus_register ****FAILED:%d\n", ret);
		POSTCODE_LINUX_3(VPCI_CREATE_FAILURE_PC, ret,
				 POSTCODE_SEVERITY_ERR);
		return ret;
	}
	DBGINF("bus_register successful\n");
	BusDeviceInfo_Init(&Bus_DriverInfo, "clientbus", "virtpci",
			   VERSION, NULL);

	/* create a root bus used to parent all the virtpci buses. */
	ret = device_register(&virtpci_rootbus_device);
	if (ret) {
		LOGERR("device_register FAILED:%d\n", ret);
		bus_unregister(&virtpci_bus_type);
		POSTCODE_LINUX_3(VPCI_CREATE_FAILURE_PC, ret,
				 POSTCODE_SEVERITY_ERR);
		return ret;
	}
	DBGINF("device_register successful ret:%x\n", ret);

	if (!uisctrl_register_req_handler(2, (void *) &virtpci_ctrlchan_func,
					  &Chipset_DriverInfo)) {
		LOGERR("uisctrl_register_req_handler ****FAILED.\n");
		POSTCODE_LINUX_2(VPCI_CREATE_FAILURE_PC, POSTCODE_SEVERITY_ERR);
		device_unregister(&virtpci_rootbus_device);
		bus_unregister(&virtpci_bus_type);
		return -1;
	}

	LOGINF("successfully registered virtpci_ctrlchan_func (0x%p) as callback.\n",
	     (void *) &virtpci_ctrlchan_func);
	/* create debugfs directory and info file inside. */
	virtpci_debugfs_dir = debugfs_create_dir("virtpci", NULL);
	debugfs_create_file("info", S_IRUSR, virtpci_debugfs_dir,
			NULL, &debugfs_info_fops);
	LOGINF("Leaving\n");
	POSTCODE_LINUX_2(VPCI_CREATE_EXIT_PC, POSTCODE_SEVERITY_INFO);
	return 0;
}

static void __exit virtpci_mod_exit(void)
{
	LOGINF("virtpci_mod_exit...\n");

	/* unregister the callback function */
	if (!uisctrl_register_req_handler(2, NULL, NULL))
		LOGERR("uisctrl_register_req_handler ****FAILED.\n");

	device_unregister(&virtpci_rootbus_device);
	bus_unregister(&virtpci_bus_type);
	debugfs_remove_recursive(virtpci_debugfs_dir);
	LOGINF("Leaving\n");

}

module_init(virtpci_mod_init);
module_exit(virtpci_mod_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Usha Srinivasan");
MODULE_ALIAS("uisvirtpci");


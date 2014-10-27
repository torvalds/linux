/* virtpci.h
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

/*
 * Unisys Virtual PCI driver header
 */

#ifndef __VIRTPCI_H__
#define __VIRTPCI_H__

#include "uisqueue.h"
#include <linux/version.h>
#include <linux/uuid.h>

#define PCI_DEVICE_ID_VIRTHBA 0xAA00
#define PCI_DEVICE_ID_VIRTNIC 0xAB00

struct scsi_adap_info {
	void *scsihost;		/* scsi host if this device is a scsi hba */
	struct vhba_wwnn wwnn;	/* the world wide node name of vhba */
	struct vhba_config_max max;	/* various max specifications used
					 * to config vhba */
};

struct net_adap_info {
	struct net_device *netdev;	/* network device if this
					 * device is a NIC */
	u8 mac_addr[MAX_MACADDR_LEN];
	int num_rcv_bufs;
	unsigned mtu;
	uuid_le zone_uuid;
};

enum virtpci_dev_type {
	VIRTHBA_TYPE = 0,
	VIRTNIC_TYPE = 1,
	VIRTBUS_TYPE = 6,
};

struct virtpci_dev {
	enum virtpci_dev_type devtype;	/* indicates type of the
					 * virtual pci device */
	struct virtpci_driver *mydriver;	/* which driver has allocated
						 * this device */
	unsigned short vendor;	/* vendor id for device */
	unsigned short device;	/* device id for device */
	u32 bus_no;		/* number of bus on which device exists */
	u32 device_no;		/* device's number on the bus */
	struct irq_info intr;	/* interrupt info */
	struct device generic_dev;	/* generic device */
	union {
		struct scsi_adap_info scsi;
		struct net_adap_info net;
	};

	struct uisqueue_info queueinfo;	/* holds ptr to channel where cmds &
					 * rsps are queued & retrieved */
	struct virtpci_dev *next;	/* points to next virtpci device */
};

struct virtpci_driver {
	struct list_head node;
	const char *name;	/* the name of the driver in sysfs */
	const char *version;
	const char *vertag;
	const struct pci_device_id *id_table;	/* must be non-NULL for probe
						 * to be called */
	int (*probe)(struct virtpci_dev *dev,
		     const struct pci_device_id *id); /* device inserted */
	void (*remove)(struct virtpci_dev *dev); /* Device removed (NULL if
						    * not a hot-plug capable
						    * driver) */
	int (*suspend)(struct virtpci_dev *dev,
		       u32 state);		   /* Device suspended */
	int (*resume)(struct virtpci_dev *dev);	/* Device woken up */
	int (*enable_wake)(struct virtpci_dev *dev,
			   u32 state, int enable);	/* Enable wake event */
	struct device_driver core_driver;	/* VIRTPCI core fills this in */
};

#define	driver_to_virtpci_driver(in_drv) \
	container_of(in_drv, struct virtpci_driver, core_driver)
#define device_to_virtpci_dev(in_dev) \
	container_of(in_dev, struct virtpci_dev, generic_dev)

int virtpci_register_driver(struct virtpci_driver *);
void virtpci_unregister_driver(struct virtpci_driver *);

#endif /* __VIRTPCI_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD HSMP Platform Driver
 * Copyright (c) 2024, AMD.
 * All Rights Reserved.
 *
 * Header file for HSMP driver
 */

#ifndef HSMP_H
#define HSMP_H

#include <linux/compiler_types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/sysfs.h>

#define HSMP_METRICS_TABLE_NAME	"metrics_bin"

#define HSMP_ATTR_GRP_NAME_SIZE	10

#define MAX_AMD_SOCKETS 8

#define HSMP_CDEV_NAME		"hsmp_cdev"
#define HSMP_DEVNODE_NAME	"hsmp"

struct hsmp_mbaddr_info {
	u32 base_addr;
	u32 msg_id_off;
	u32 msg_resp_off;
	u32 msg_arg_off;
	u32 size;
};

struct hsmp_socket {
	struct bin_attribute hsmp_attr;
	struct hsmp_mbaddr_info mbinfo;
	void __iomem *metric_tbl_addr;
	void __iomem *virt_base_addr;
	struct semaphore hsmp_sem;
	char name[HSMP_ATTR_GRP_NAME_SIZE];
	struct pci_dev *root;
	struct device *dev;
	u16 sock_ind;
	int (*amd_hsmp_rdwr)(struct hsmp_socket *sock, u32 off, u32 *val, bool rw);
};

struct hsmp_plat_device {
	struct miscdevice hsmp_device;
	struct hsmp_socket *sock;
	u32 proto_ver;
	u16 num_sockets;
	bool is_acpi_device;
	bool is_probed;
};

extern struct hsmp_plat_device plat_dev;

ssize_t hsmp_metric_tbl_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count);
int hsmp_create_non_acpi_sysfs_if(struct device *dev);
int hsmp_cache_proto_ver(u16 sock_ind);
umode_t hsmp_is_sock_attr_visible(struct kobject *kobj,
				  struct bin_attribute *battr, int id);
int hsmp_create_attr_list(struct attribute_group *attr_grp,
			  struct device *dev, u16 sock_ind);
int hsmp_test(u16 sock_ind, u32 value);
int init_platform_device(struct device *dev);
#endif /* HSMP_H */

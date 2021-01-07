/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#ifndef RNBD_SRV_H
#define RNBD_SRV_H

#include <linux/types.h>
#include <linux/idr.h>
#include <linux/kref.h>

#include <rtrs.h>
#include "rnbd-proto.h"
#include "rnbd-log.h"

struct rnbd_srv_session {
	/* Entry inside global sess_list */
	struct list_head        list;
	struct rtrs_srv		*rtrs;
	char			sessname[NAME_MAX];
	int			queue_depth;
	struct bio_set		sess_bio_set;

	struct xarray		index_idr;
	/* List of struct rnbd_srv_sess_dev */
	struct list_head        sess_dev_list;
	struct mutex		lock;
	u8			ver;
};

struct rnbd_srv_dev {
	/* Entry inside global dev_list */
	struct list_head                list;
	struct kobject                  dev_kobj;
	struct kobject                  *dev_sessions_kobj;
	struct kref                     kref;
	char				id[NAME_MAX];
	/* List of rnbd_srv_sess_dev structs */
	struct list_head		sess_dev_list;
	struct mutex			lock;
	int				open_write_cnt;
};

/* Structure which binds N devices and N sessions */
struct rnbd_srv_sess_dev {
	/* Entry inside rnbd_srv_dev struct */
	struct list_head		dev_list;
	/* Entry inside rnbd_srv_session struct */
	struct list_head		sess_list;
	struct rnbd_dev			*rnbd_dev;
	struct rnbd_srv_session		*sess;
	struct rnbd_srv_dev		*dev;
	struct kobject                  kobj;
	u32                             device_id;
	bool				keep_id;
	fmode_t                         open_flags;
	struct kref			kref;
	struct completion               *destroy_comp;
	char				pathname[NAME_MAX];
	enum rnbd_access_mode		access_mode;
};

void rnbd_srv_sess_dev_force_close(struct rnbd_srv_sess_dev *sess_dev);
/* rnbd-srv-sysfs.c */

int rnbd_srv_create_dev_sysfs(struct rnbd_srv_dev *dev,
			      struct block_device *bdev,
			      const char *dir_name);
void rnbd_srv_destroy_dev_sysfs(struct rnbd_srv_dev *dev);
int rnbd_srv_create_dev_session_sysfs(struct rnbd_srv_sess_dev *sess_dev);
void rnbd_srv_destroy_dev_session_sysfs(struct rnbd_srv_sess_dev *sess_dev);
int rnbd_srv_create_sysfs_files(void);
void rnbd_srv_destroy_sysfs_files(void);
void rnbd_destroy_sess_dev(struct rnbd_srv_sess_dev *sess_dev, bool keep_id);

#endif /* RNBD_SRV_H */

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_FW_CLIENT_H_
#define _ISSEI_FW_CLIENT_H_

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/types.h>
#include <linux/uuid.h>

struct issei_device;
struct issei_host_client;

/**
 * struct issei_fw_client - represents firmware queue
 * @kobj: associated kobject
 * @list: link in firmware clients list
 * @id: firmware client id
 * @ver: firmware client version
 * @uuid: firmware client protocol id
 * @mtu: firmware client maximum buffer size
 * @flags: firmware client flags
 * @cl: pointer to host client, if connected
 */
struct issei_fw_client {
	struct kobject kobj;
	struct list_head list;
	u16 id;
	u8 ver;
	uuid_t uuid;
	u32 mtu;
	u32 flags;
	struct issei_host_client *cl;
};
#define to_issei_fw_client(x) container_of(x, struct issei_fw_client, kobj)

struct issei_fw_client *issei_fw_cl_create(struct issei_device *idev, u16 id, u8 ver,
					   const uuid_t *uuid, u32 mtu, u32 flags);
void issei_fw_cl_remove_all(struct issei_device *idev);
struct issei_fw_client *issei_fw_cl_find_by_uuid(struct issei_device *idev, const uuid_t *uuid);

int issei_fw_cl_connect(struct issei_fw_client *fw_cl, struct issei_host_client *cl);
void issei_fw_cl_disconnect(struct issei_fw_client *fw_cl);

#endif /* _ISSEI_FW_CLIENT_H_ */

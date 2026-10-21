/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_HOST_CLIENT_H_
#define _ISSEI_HOST_CLIENT_H_

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/wait.h>

struct file;

struct issei_device;
struct issei_fw_client;

/**
 * enum issei_host_client_state - host client states
 * @ISSEI_HOST_CL_STATE_DISCONNECTED: host client is disconnected
 * @ISSEI_HOST_CL_STATE_CONNECTED: host client is connected
 */
enum issei_host_client_state {
	ISSEI_HOST_CL_STATE_DISCONNECTED,
	ISSEI_HOST_CL_STATE_CONNECTED,
};

/**
 * struct issei_host_client - represents host client
 * @list: link in host clients list
 * @idev: issei parent device
 * @id: host client id
 * @fp: file associated with client
 *
 * @write_wait: waitqueue for pending write data
 * @write_in_progress: indicator for write in process
 *
 * @state: host client state
 * @fw_cl: pointer to firmware client, if connected
 *
 * @read_wait: waitqueue for read object
 * @read_data: received data pointer
 * @read_data_size: received data size
 */
struct issei_host_client {
	struct list_head list;
	struct issei_device *idev;
	u16 id;
	const struct file *fp;

	wait_queue_head_t write_wait;
	bool write_in_progress;

	enum issei_host_client_state state;
	struct issei_fw_client *fw_cl;

	wait_queue_head_t read_wait;
	u8 *read_data;
	size_t read_data_size;
};

struct issei_host_client *issei_cl_create(struct issei_device *idev, struct file *fp);
void issei_cl_remove(struct issei_host_client *cl);

int issei_cl_connect(struct issei_host_client *cl, const uuid_t *uuid, u32 *mtu, u8 *ver,
		     u32 *flags);
int issei_cl_disconnect(struct issei_host_client *cl);
void issei_cl_all_disconnect(struct issei_device *idev);

ssize_t issei_cl_write(struct issei_host_client *cl, const u8 *buf, size_t buf_size);
int issei_cl_write_from_queue(struct issei_device *idev);
int issei_cl_read_buf(struct issei_device *idev, u16 fw_id, u16 host_id, u8 *buf, size_t buf_size);
ssize_t issei_cl_read(struct issei_host_client *cl, u8 **buf, size_t buf_size);
int issei_cl_check_read(struct issei_host_client *cl);
int issei_cl_check_write(struct issei_host_client *cl);
void issei_cl_clean_all_wbuf(struct issei_host_client *cl);

#endif /* ISSEI_HOST_CLIENT_H_ */

/* Copyright (c) 2008-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DIAGCHAR_H
#define DIAGCHAR_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <mach/msm_smd.h>
#include <linux/atomic.h>
#include <asm/mach-types.h>
/* Size of the USB buffers used for read and write*/
#define USB_MAX_OUT_BUF 4096
#define APPS_BUF_SIZE	2000
#define IN_BUF_SIZE		16384
#define MAX_IN_BUF_SIZE	32768
#define MAX_SYNC_OBJ_NAME_SIZE	32
/* Size of the buffer used for deframing a packet
  reveived from the PC tool*/
#define HDLC_MAX 4096
#define HDLC_OUT_BUF_SIZE	8192
#define POOL_TYPE_COPY		1
#define POOL_TYPE_HDLC		2
#define POOL_TYPE_WRITE_STRUCT	4
#define POOL_TYPE_ALL		7
#define MODEM_DATA		1
#define QDSP_DATA		2
#define APPS_DATA		3
#define SDIO_DATA		4
#define WCNSS_DATA		5
#define HSIC_DATA		6
#define MODEM_PROC		0
#define APPS_PROC		1
#define QDSP_PROC		2
#define WCNSS_PROC		3
#define MSG_MASK_SIZE 9500
#define LOG_MASK_SIZE 8000
#define EVENT_MASK_SIZE 1000
#define USER_SPACE_DATA 8000
#define PKT_SIZE 4096
#define MAX_EQUIP_ID 15
#define DIAG_CTRL_MSG_LOG_MASK	9
#define DIAG_CTRL_MSG_EVENT_MASK	10
#define DIAG_CTRL_MSG_F3_MASK	11
#define ZERO_CFG_SUBPACKET_MAX 15 // zero_pky.patch by jagadish

/* Maximum number of pkt reg supported at initialization*/
extern unsigned int diag_max_reg;
extern unsigned int diag_threshold_reg;

#define APPEND_DEBUG(ch) \
do {							\
	diag_debug_buf[diag_debug_buf_idx] = ch; \
	(diag_debug_buf_idx < 1023) ? \
	(diag_debug_buf_idx++) : (diag_debug_buf_idx = 0); \
} while (0)

struct diag_master_table {
	uint16_t cmd_code;
	uint16_t subsys_id;
	uint32_t client_id;
	uint16_t cmd_code_lo;
	uint16_t cmd_code_hi;
	int process_id;
};

struct bindpkt_params_per_process {
	/* Name of the synchronization object associated with this proc */
	char sync_obj_name[MAX_SYNC_OBJ_NAME_SIZE];
	uint32_t count;	/* Number of entries in this bind */
	struct bindpkt_params *params; /* first bind params */
};

struct bindpkt_params {
	uint16_t cmd_code;
	uint16_t subsys_id;
	uint16_t cmd_code_lo;
	uint16_t cmd_code_hi;
	/* For Central Routing, used to store Processor number */
	uint16_t proc_id;
	uint32_t event_id;
	uint32_t log_code;
	/* For Central Routing, used to store SMD channel pointer */
	uint32_t client_id;
};

struct diag_write_device {
	void *buf;
	int length;
};

struct diag_client_map {
	char name[20];
	int pid;
};

/* This structure is defined in USB header file */
#ifndef CONFIG_DIAG_OVER_USB
struct diag_request {
	char *buf;
	int length;
	int actual;
	int status;
	void *context;
};
#endif

struct diagchar_dev {

	/* State for the char driver */
	unsigned int major;
	unsigned int minor_start;
	int num;
	struct cdev *cdev;
	char *name;
	int dropped_count;
	struct class *diagchar_class;
	int ref_count;
	struct mutex diagchar_mutex;
	wait_queue_head_t wait_q;
	struct diag_client_map *client_map;
	int *data_ready;
	int num_clients;
	int polling_reg_flag;
	struct diag_write_device *buf_tbl;
	int use_device_tree;

	/* Memory pool parameters */
	unsigned int itemsize;
	unsigned int poolsize;
	unsigned int itemsize_hdlc;
	unsigned int poolsize_hdlc;
	unsigned int itemsize_write_struct;
	unsigned int poolsize_write_struct;
	unsigned int debug_flag;
	/* State for the mempool for the char driver */
	mempool_t *diagpool;
	mempool_t *diag_hdlc_pool;
	mempool_t *diag_write_struct_pool;
	struct mutex diagmem_mutex;
	int count;
	int count_hdlc_pool;
	int count_write_struct_pool;
	int used;
	/* Buffers for masks */
	struct diag_ctrl_event_mask *event_mask;
	struct diag_ctrl_log_mask *log_mask;
	struct diag_ctrl_msg_mask *msg_mask;
	/* State for diag forwarding */
	unsigned char *buf_in_1;
	unsigned char *buf_in_2;
	unsigned char *buf_in_cntl;
	unsigned char *buf_in_qdsp_1;
	unsigned char *buf_in_qdsp_2;
	unsigned char *buf_in_qdsp_cntl;
	unsigned char *buf_in_wcnss_1;
	unsigned char *buf_in_wcnss_2;
	unsigned char *buf_in_wcnss_cntl;
	unsigned char *usb_buf_out;
	unsigned char *apps_rsp_buf;
	unsigned char *user_space_data;
	/* buffer for updating mask to peripherals */
	unsigned char *buf_msg_mask_update;
	unsigned char *buf_log_mask_update;
	unsigned char *buf_event_mask_update;
	smd_channel_t *ch;
	smd_channel_t *ch_cntl;
	smd_channel_t *chqdsp;
	smd_channel_t *chqdsp_cntl;
	smd_channel_t *ch_wcnss;
	smd_channel_t *ch_wcnss_cntl;
	int in_busy_1;
	int in_busy_2;
	int in_busy_qdsp_1;
	int in_busy_qdsp_2;
	int in_busy_wcnss_1;
	int in_busy_wcnss_2;
	int read_len_legacy;
	unsigned char *hdlc_buf;
	unsigned hdlc_count;
	unsigned hdlc_escape;
#ifdef CONFIG_DIAG_OVER_USB
	int usb_connected;
	struct usb_diag_ch *legacy_ch;
	struct work_struct diag_proc_hdlc_work;
	struct work_struct diag_read_work;
#endif
	struct workqueue_struct *diag_wq;
	struct work_struct diag_drain_work;
	struct work_struct diag_read_smd_work;
	struct work_struct diag_read_smd_cntl_work;
	struct work_struct diag_read_smd_qdsp_work;
	struct work_struct diag_read_smd_qdsp_cntl_work;
	struct work_struct diag_read_smd_wcnss_work;
	struct work_struct diag_read_smd_wcnss_cntl_work;
	struct workqueue_struct *diag_cntl_wq;
	struct work_struct diag_modem_mask_update_work;
	struct work_struct diag_qdsp_mask_update_work;
	struct work_struct diag_wcnss_mask_update_work;
	uint8_t *msg_masks;
	uint8_t *log_masks;
	int log_masks_length;
	uint8_t *event_masks;
	struct diag_master_table *table;
	uint8_t *pkt_buf;
	int pkt_length;
	struct diag_request *write_ptr_1;
	struct diag_request *write_ptr_2;
	struct diag_request *usb_read_ptr;
	struct diag_request *write_ptr_svc;
	struct diag_request *write_ptr_qdsp_1;
	struct diag_request *write_ptr_qdsp_2;
	struct diag_request *write_ptr_wcnss_1;
	struct diag_request *write_ptr_wcnss_2;
	int logging_mode;
	int sub_logging_mode;
	int mask_check;
	int logging_process_id;
#ifdef CONFIG_DIAG_SDIO_PIPE
	unsigned char *buf_in_sdio;
	unsigned char *usb_buf_mdm_out;
	struct sdio_channel *sdio_ch;
	int read_len_mdm;
	int in_busy_sdio;
	struct usb_diag_ch *mdm_ch;
	struct work_struct diag_read_mdm_work;
	struct workqueue_struct *diag_sdio_wq;
	struct work_struct diag_read_sdio_work;
	struct work_struct diag_close_sdio_work;
	struct diag_request *usb_read_mdm_ptr;
	struct diag_request *write_ptr_mdm;
#endif
#ifdef CONFIG_DIAG_HSIC_PIPE
	unsigned char *buf_in_hsic;
	unsigned char *usb_buf_mdm_out;
	int hsic_initialized;
	int hsic_ch;
	int hsic_device_enabled;
	int hsic_device_opened;
	int hsic_suspend;
	int read_len_mdm;
	int in_busy_hsic_read_on_mdm;
	int in_busy_hsic_write_on_mdm;
	int in_busy_hsic_write;
	int in_busy_hsic_read;
	int usb_mdm_connected;
	unsigned int zero_cfg_mode; // zero_pky.patch by jagadish
	unsigned int zero_cfg_index; // zero_pky.patch by jagadish
	unsigned int zero_cfg_packet_lens_index; // zero_pky.patch by jagadish
	struct usb_diag_ch *mdm_ch;
	struct workqueue_struct *diag_hsic_wq;
	struct work_struct diag_read_mdm_work;
	struct work_struct diag_read_hsic_work;
	struct work_struct diag_zero_cfg_hsic_work; // zero_pky.patch by jagadish
	struct work_struct diag_disconnect_work;
	struct work_struct diag_usb_read_complete_work;
	struct diag_request *usb_read_mdm_ptr;
	struct diag_request *write_ptr_mdm;
	struct pid *silent_log_pid;
	#endif
};

extern struct diagchar_dev *driver;
#endif

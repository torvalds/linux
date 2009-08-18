/*
 * Part of Intel(R) Manageability Engine Interface Linux driver
 *
 * Copyright (c) 2003 - 2008 Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */

#ifndef _HECI_DATA_STRUCTURES_H_
#define _HECI_DATA_STRUCTURES_H_

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/aio.h>
#include <linux/types.h>

/*
 * error code definition
 */
#define     ESLOTS_OVERFLOW              1
#define     ECORRUPTED_MESSAGE_HEADER    1000
#define     ECOMPLETE_MESSAGE            1001

#define     HECI_FC_MESSAGE_RESERVED_LENGTH           5

/*
 * Number of queue lists used by this driver
 */
#define HECI_IO_LISTS_NUMBER        7

/*
 * Maximum transmission unit (MTU) of heci messages
 */
#define IAMTHIF_MTU 4160


/*
 * HECI HW Section
 */

/* HECI registers */
/* H_CB_WW - Host Circular Buffer (CB) Write Window register */
#define H_CB_WW    0
/* H_CSR - Host Control Status register */
#define H_CSR      4
/* ME_CB_RW - ME Circular Buffer Read Window register (read only) */
#define ME_CB_RW   8
/* ME_CSR_HA - ME Control Status Host Access register (read only) */
#define ME_CSR_HA  0xC


/* register bits of H_CSR (Host Control Status register) */
/* Host Circular Buffer Depth - maximum number of 32-bit entries in CB */
#define H_CBD             0xFF000000
/* Host Circular Buffer Write Pointer */
#define H_CBWP            0x00FF0000
/* Host Circular Buffer Read Pointer */
#define H_CBRP            0x0000FF00
/* Host Reset */
#define H_RST             0x00000010
/* Host Ready */
#define H_RDY             0x00000008
/* Host Interrupt Generate */
#define H_IG              0x00000004
/* Host Interrupt Status */
#define H_IS              0x00000002
/* Host Interrupt Enable */
#define H_IE              0x00000001


/* register bits of ME_CSR_HA (ME Control Status Host Access register) */
/* ME CB (Circular Buffer) Depth HRA (Host Read Access)
 *  - host read only access to ME_CBD */
#define ME_CBD_HRA        0xFF000000
/* ME CB Write Pointer HRA - host read only access to ME_CBWP */
#define ME_CBWP_HRA       0x00FF0000
/* ME CB Read Pointer HRA - host read only access to ME_CBRP */
#define ME_CBRP_HRA       0x0000FF00
/* ME Reset HRA - host read only access to ME_RST */
#define ME_RST_HRA        0x00000010
/* ME Ready HRA - host read only access to ME_RDY */
#define ME_RDY_HRA        0x00000008
/* ME Interrupt Generate HRA - host read only access to ME_IG */
#define ME_IG_HRA         0x00000004
/* ME Interrupt Status HRA - host read only access to ME_IS */
#define ME_IS_HRA         0x00000002
/* ME Interrupt Enable HRA - host read only access to ME_IE */
#define ME_IE_HRA         0x00000001

#define HECI_MINORS_BASE	1
#define HECI_MINORS_COUNT	1

#define  HECI_MINOR_NUMBER	1
#define  HECI_MAX_OPEN_HANDLE_COUNT	253

/*
 * debug kernel print macro define
 */
extern int heci_debug;

#define DBG(format, arg...) do { \
	if (heci_debug) \
		printk(KERN_INFO "heci: %s: " format, __func__, ## arg); \
} while (0)


/*
 * time to wait HECI become ready after init
 */
#define HECI_INTEROP_TIMEOUT    (HZ * 7)

/*
 * watch dog definition
 */
#define HECI_WATCHDOG_DATA_SIZE         16
#define HECI_START_WD_DATA_SIZE         20
#define HECI_WD_PARAMS_SIZE             4
#define HECI_WD_STATE_INDEPENDENCE_MSG_SENT       (1 << 0)

#define HECI_WD_HOST_CLIENT_ID          1
#define HECI_IAMTHIF_HOST_CLIENT_ID     2

struct guid {
	__u32 data1;
	__u16 data2;
	__u16 data3;
	__u8 data4[8];
};

/* File state */
enum file_state {
	HECI_FILE_INITIALIZING = 0,
	HECI_FILE_CONNECTING,
	HECI_FILE_CONNECTED,
	HECI_FILE_DISCONNECTING,
	HECI_FILE_DISCONNECTED
};

/* HECI device states */
enum heci_states {
	HECI_INITIALIZING = 0,
	HECI_ENABLED,
	HECI_RESETING,
	HECI_DISABLED,
	HECI_RECOVERING_FROM_RESET,
	HECI_POWER_DOWN,
	HECI_POWER_UP
};

enum iamthif_states {
	HECI_IAMTHIF_IDLE,
	HECI_IAMTHIF_WRITING,
	HECI_IAMTHIF_FLOW_CONTROL,
	HECI_IAMTHIF_READING,
	HECI_IAMTHIF_READ_COMPLETE
};

enum heci_file_transaction_states {
	HECI_IDLE,
	HECI_WRITING,
	HECI_WRITE_COMPLETE,
	HECI_FLOW_CONTROL,
	HECI_READING,
	HECI_READ_COMPLETE
};

/* HECI CB */
enum heci_cb_major_types {
	HECI_READ = 0,
	HECI_WRITE,
	HECI_IOCTL,
	HECI_OPEN,
	HECI_CLOSE
};

/* HECI user data struct */
struct heci_message_data {
	__u32 size;
	char *data;
} __attribute__((packed));

#define HECI_CONNECT_TIMEOUT             3	/* at least 2 seconds */

#define IAMTHIF_STALL_TIMER              12	/* seconds */
#define IAMTHIF_READ_TIMER               15	/* seconds */

struct heci_cb_private {
	struct list_head cb_list;
	enum heci_cb_major_types major_file_operations;
	void *file_private;
	struct heci_message_data request_buffer;
	struct heci_message_data response_buffer;
	unsigned long information;
	unsigned long read_time;
	struct file *file_object;
};

/* Private file struct */
struct heci_file_private {
	struct list_head link;
	struct file *file;
	enum file_state state;
	wait_queue_head_t tx_wait;
	wait_queue_head_t rx_wait;
	wait_queue_head_t wait;
	spinlock_t file_lock; /* file lock */
	spinlock_t read_io_lock; /* read lock */
	spinlock_t write_io_lock; /* write lock */
	int read_pending;
	int status;
	/* ID of client connected */
	__u8 host_client_id;
	__u8 me_client_id;
	__u8 flow_ctrl_creds;
	__u8 timer_count;
	enum heci_file_transaction_states reading_state;
	enum heci_file_transaction_states writing_state;
	int sm_state;
	struct heci_cb_private *read_cb;
};

struct io_heci_list {
	struct heci_cb_private heci_cb;
	int status;
	struct iamt_heci_device *device_extension;
};

struct heci_driver_version {
	__u8 major;
	__u8 minor;
	__u8 hotfix;
	__u16 build;
} __attribute__((packed));


struct heci_client {
	__u32 max_msg_length;
	__u8 protocol_version;
} __attribute__((packed));

/*
 *  HECI BUS Interface Section
 */
struct heci_msg_hdr {
	__u32 me_addr:8;
	__u32 host_addr:8;
	__u32 length:9;
	__u32 reserved:6;
	__u32 msg_complete:1;
} __attribute__((packed));


struct hbm_cmd {
	__u8 cmd:7;
	__u8 is_response:1;
} __attribute__((packed));


struct heci_bus_message {
	struct hbm_cmd cmd;
	__u8 command_specific_data[];
} __attribute__((packed));

struct hbm_version {
	__u8 minor_version;
	__u8 major_version;
} __attribute__((packed));

struct hbm_host_version_request {
	struct hbm_cmd cmd;
	__u8 reserved;
	struct hbm_version host_version;
} __attribute__((packed));

struct hbm_host_version_response {
	struct hbm_cmd cmd;
	int host_version_supported;
	struct hbm_version me_max_version;
} __attribute__((packed));

struct hbm_host_stop_request {
	struct hbm_cmd cmd;
	__u8 reason;
	__u8 reserved[2];
} __attribute__((packed));

struct hbm_host_stop_response {
	struct hbm_cmd cmd;
	__u8 reserved[3];
} __attribute__((packed));

struct hbm_me_stop_request {
	struct hbm_cmd cmd;
	__u8 reason;
	__u8 reserved[2];
} __attribute__((packed));

struct hbm_host_enum_request {
	struct hbm_cmd cmd;
	__u8 reserved[3];
} __attribute__((packed));

struct hbm_host_enum_response {
	struct hbm_cmd cmd;
	__u8 reserved[3];
	__u8 valid_addresses[32];
} __attribute__((packed));

struct heci_client_properties {
	struct guid protocol_name;
	__u8 protocol_version;
	__u8 max_number_of_connections;
	__u8 fixed_address;
	__u8 single_recv_buf;
	__u32 max_msg_length;
} __attribute__((packed));

struct hbm_props_request {
	struct hbm_cmd cmd;
	__u8 address;
	__u8 reserved[2];
} __attribute__((packed));


struct hbm_props_response {
	struct hbm_cmd cmd;
	__u8 address;
	__u8 status;
	__u8 reserved[1];
	struct heci_client_properties client_properties;
} __attribute__((packed));

struct hbm_client_connect_request {
	struct hbm_cmd cmd;
	__u8 me_addr;
	__u8 host_addr;
	__u8 reserved;
} __attribute__((packed));

struct hbm_client_connect_response {
	struct hbm_cmd cmd;
	__u8 me_addr;
	__u8 host_addr;
	__u8 status;
} __attribute__((packed));

struct hbm_client_disconnect_request {
	struct hbm_cmd cmd;
	__u8 me_addr;
	__u8 host_addr;
	__u8 reserved[1];
} __attribute__((packed));

struct hbm_flow_control {
	struct hbm_cmd cmd;
	__u8 me_addr;
	__u8 host_addr;
	__u8 reserved[HECI_FC_MESSAGE_RESERVED_LENGTH];
} __attribute__((packed));

struct heci_me_client {
	struct heci_client_properties props;
	__u8 client_id;
	__u8 flow_ctrl_creds;
} __attribute__((packed));

/* private device struct */
struct iamt_heci_device {
	struct pci_dev *pdev;	/* pointer to pci device struct */
	/*
	 * lists of queues
	 */
	 /* array of pointers to  aio lists */
	struct io_heci_list *io_list_array[HECI_IO_LISTS_NUMBER];
	struct io_heci_list read_list;	/* driver read queue */
	struct io_heci_list write_list;	/* driver write queue */
	struct io_heci_list write_waiting_list;	/* write waiting queue */
	struct io_heci_list ctrl_wr_list;	/* managed write IOCTL list */
	struct io_heci_list ctrl_rd_list;	/* managed read IOCTL list */
	struct io_heci_list pthi_cmd_list;	/* PTHI list for cmd waiting */

	/* driver managed PTHI list for reading completed pthi cmd data */
	struct io_heci_list pthi_read_complete_list;
	/*
	 * list of files
	 */
	struct list_head file_list;
	/*
	 * memory of device
	 */
	unsigned int mem_base;
	unsigned int mem_length;
	void __iomem *mem_addr;
	/*
	 * lock for the device
	 */
	spinlock_t device_lock; /* device lock*/
	struct work_struct work;
	int recvd_msg;

	struct task_struct *reinit_tsk;

	struct timer_list wd_timer;
	/*
	 * hw states of host and fw(ME)
	 */
	__u32 host_hw_state;
	__u32 me_hw_state;
	/*
	 * waiting queue for receive message from FW
	 */
	wait_queue_head_t wait_recvd_msg;
	wait_queue_head_t wait_stop_wd;
	/*
	 * heci device  states
	 */
	enum heci_states heci_state;
	int stop;

	__u32 extra_write_index;
	__u32 rd_msg_buf[128];	/* used for control messages */
	__u32 wr_msg_buf[128];	/* used for control messages */
	__u32 ext_msg_buf[8];	/* for control responses    */
	__u32 rd_msg_hdr;

	struct hbm_version version;

	int host_buffer_is_empty;
	struct heci_file_private wd_file_ext;
	struct heci_me_client *me_clients; /* Note: memory has to be allocated*/
	__u8 heci_me_clients[32];	/* list of existing clients */
	__u8 num_heci_me_clients;
	__u8 heci_host_clients[32];	/* list of existing clients */
	__u8 current_host_client_id;

	int wd_pending;
	int wd_stoped;
	__u16 wd_timeout;	/* seconds ((wd_data[1] << 8) + wd_data[0]) */
	unsigned char wd_data[HECI_START_WD_DATA_SIZE];


	__u16 wd_due_counter;
	int asf_mode;
	int wd_bypass;	/* if 1, don't refresh watchdog ME client */

	struct file *iamthif_file_object;
	struct heci_file_private iamthif_file_ext;
	int iamthif_ioctl;
	int iamthif_canceled;
	__u32 iamthif_timer;
	__u32 iamthif_stall_timer;
	unsigned char iamthif_msg_buf[IAMTHIF_MTU];
	__u32 iamthif_msg_buf_size;
	__u32 iamthif_msg_buf_index;
	int iamthif_flow_control_pending;
	enum iamthif_states iamthif_state;

	struct heci_cb_private *iamthif_current_cb;
	__u8 write_hang;
	int need_reset;
	long open_handle_count;

};

/**
 * read_heci_register - Read a byte from the heci device
 *
 * @dev: the device structure
 * @offset: offset from which to read the data
 *
 * returns  the byte read.
 */
static inline __u32 read_heci_register(struct iamt_heci_device *dev,
					unsigned long offset)
{
	return readl(dev->mem_addr + offset);
}

/**
 * write_heci_register - Write  4 bytes to the heci device
 *
 * @dev: the device structure
 * @offset: offset from which to write the data
 * @value: the byte to write
 */
static inline void write_heci_register(struct iamt_heci_device *dev,
					unsigned long offset,  __u32 value)
{
	writel(value, dev->mem_addr + offset);
}

#endif /* _HECI_DATA_STRUCTURES_H_ */

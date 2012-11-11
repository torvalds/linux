/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _MEI_DEV_H_
#define _MEI_DEV_H_

#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/mei.h>
#include "hw.h"

/*
 * watch dog definition
 */
#define MEI_WD_HDR_SIZE       4
#define MEI_WD_STOP_MSG_SIZE  MEI_WD_HDR_SIZE
#define MEI_WD_START_MSG_SIZE (MEI_WD_HDR_SIZE + 16)

#define MEI_WD_DEFAULT_TIMEOUT   120  /* seconds */
#define MEI_WD_MIN_TIMEOUT       120  /* seconds */
#define MEI_WD_MAX_TIMEOUT     65535  /* seconds */

#define MEI_WD_STOP_TIMEOUT      10 /* msecs */

#define MEI_WD_STATE_INDEPENDENCE_MSG_SENT       (1 << 0)

#define MEI_RD_MSG_BUF_SIZE           (128 * sizeof(u32))


/*
 * AMTHI Client UUID
 */
extern const uuid_le mei_amthi_guid;

/*
 * Watchdog Client UUID
 */
extern const uuid_le mei_wd_guid;

/*
 * Watchdog independence state message
 */
extern const u8 mei_wd_state_independence_msg[3][4];

/*
 * Number of Maximum MEI Clients
 */
#define MEI_CLIENTS_MAX 256

/*
 * Number of File descriptors/handles
 * that can be opened to the driver.
 *
 * Limit to 253: 256 Total Clients
 * minus internal client for MEI Bus Messags
 * minus internal client for AMTHI
 * minus internal client for Watchdog
 */
#define  MEI_MAX_OPEN_HANDLE_COUNT (MEI_CLIENTS_MAX - 3)


/* File state */
enum file_state {
	MEI_FILE_INITIALIZING = 0,
	MEI_FILE_CONNECTING,
	MEI_FILE_CONNECTED,
	MEI_FILE_DISCONNECTING,
	MEI_FILE_DISCONNECTED
};

/* MEI device states */
enum mei_dev_state {
	MEI_DEV_INITIALIZING = 0,
	MEI_DEV_INIT_CLIENTS,
	MEI_DEV_ENABLED,
	MEI_DEV_RESETING,
	MEI_DEV_DISABLED,
	MEI_DEV_RECOVERING_FROM_RESET,
	MEI_DEV_POWER_DOWN,
	MEI_DEV_POWER_UP
};

const char *mei_dev_state_str(int state);

/* init clients states*/
enum mei_init_clients_states {
	MEI_START_MESSAGE = 0,
	MEI_ENUM_CLIENTS_MESSAGE,
	MEI_CLIENT_PROPERTIES_MESSAGE
};

enum iamthif_states {
	MEI_IAMTHIF_IDLE,
	MEI_IAMTHIF_WRITING,
	MEI_IAMTHIF_FLOW_CONTROL,
	MEI_IAMTHIF_READING,
	MEI_IAMTHIF_READ_COMPLETE
};

enum mei_file_transaction_states {
	MEI_IDLE,
	MEI_WRITING,
	MEI_WRITE_COMPLETE,
	MEI_FLOW_CONTROL,
	MEI_READING,
	MEI_READ_COMPLETE
};

enum mei_wd_states {
	MEI_WD_IDLE,
	MEI_WD_RUNNING,
	MEI_WD_STOPPING,
};

/* MEI CB */
enum mei_cb_major_types {
	MEI_READ = 0,
	MEI_WRITE,
	MEI_IOCTL,
	MEI_OPEN,
	MEI_CLOSE
};

/*
 * Intel MEI message data struct
 */
struct mei_message_data {
	u32 size;
	unsigned char *data;
};


struct mei_cl;

/*
 * struct mei_cl_cb - file operation callback structure
 *
 * @cl - file client who is running this operation
 */
struct mei_cl_cb {
	struct list_head list;
	struct mei_cl *cl;
	enum mei_cb_major_types major_file_operations;
	struct mei_message_data request_buffer;
	struct mei_message_data response_buffer;
	unsigned long buf_idx;
	unsigned long read_time;
	struct file *file_object;
};

/* MEI client instance carried as file->pirvate_data*/
struct mei_cl {
	struct list_head link;
	struct mei_device *dev;
	enum file_state state;
	wait_queue_head_t tx_wait;
	wait_queue_head_t rx_wait;
	wait_queue_head_t wait;
	int read_pending;
	int status;
	/* ID of client connected */
	u8 host_client_id;
	u8 me_client_id;
	u8 mei_flow_ctrl_creds;
	u8 timer_count;
	enum mei_file_transaction_states reading_state;
	enum mei_file_transaction_states writing_state;
	int sm_state;
	struct mei_cl_cb *read_cb;
};

/**
 * struct mei_deive -  MEI private device struct
 * @hbuf_depth - depth of host(write) buffer
 */
struct mei_device {
	struct pci_dev *pdev;	/* pointer to pci device struct */
	/*
	 * lists of queues
	 */
	/* array of pointers to aio lists */
	struct mei_cl_cb read_list;		/* driver read queue */
	struct mei_cl_cb write_list;		/* driver write queue */
	struct mei_cl_cb write_waiting_list;	/* write waiting queue */
	struct mei_cl_cb ctrl_wr_list;		/* managed write IOCTL list */
	struct mei_cl_cb ctrl_rd_list;		/* managed read IOCTL list */

	/*
	 * list of files
	 */
	struct list_head file_list;
	long open_handle_count;
	/*
	 * memory of device
	 */
	unsigned int mem_base;
	unsigned int mem_length;
	void __iomem *mem_addr;
	/*
	 * lock for the device
	 */
	struct mutex device_lock; /* device lock */
	struct delayed_work timer_work;	/* MEI timer delayed work (timeouts) */
	bool recvd_msg;
	/*
	 * hw states of host and fw(ME)
	 */
	u32 host_hw_state;
	u32 me_hw_state;
	u8  hbuf_depth;
	/*
	 * waiting queue for receive message from FW
	 */
	wait_queue_head_t wait_recvd_msg;
	wait_queue_head_t wait_stop_wd;

	/*
	 * mei device  states
	 */
	enum mei_dev_state dev_state;
	enum mei_init_clients_states init_clients_state;
	u16 init_clients_timer;
	bool need_reset;

	u32 extra_write_index;
	unsigned char rd_msg_buf[MEI_RD_MSG_BUF_SIZE];	/* control messages */
	u32 wr_msg_buf[128];	/* used for control messages */
	u32 ext_msg_buf[8];	/* for control responses */
	u32 rd_msg_hdr;

	struct hbm_version version;

	struct mei_me_client *me_clients; /* Note: memory has to be allocated */
	DECLARE_BITMAP(me_clients_map, MEI_CLIENTS_MAX);
	DECLARE_BITMAP(host_clients_map, MEI_CLIENTS_MAX);
	u8 me_clients_num;
	u8 me_client_presentation_num;
	u8 me_client_index;
	bool mei_host_buffer_is_empty;

	struct mei_cl wd_cl;
	enum mei_wd_states wd_state;
	bool wd_pending;
	u16 wd_timeout;
	unsigned char wd_data[MEI_WD_START_MSG_SIZE];


	/* amthif list for cmd waiting */
	struct mei_cl_cb amthif_cmd_list;
	/* driver managed amthif list for reading completed amthif cmd data */
	struct mei_cl_cb amthif_rd_complete_list;
	struct file *iamthif_file_object;
	struct mei_cl iamthif_cl;
	struct mei_cl_cb *iamthif_current_cb;
	int iamthif_mtu;
	unsigned long iamthif_timer;
	u32 iamthif_stall_timer;
	unsigned char *iamthif_msg_buf; /* Note: memory has to be allocated */
	u32 iamthif_msg_buf_size;
	u32 iamthif_msg_buf_index;
	enum iamthif_states iamthif_state;
	bool iamthif_flow_control_pending;
	bool iamthif_ioctl;
	bool iamthif_canceled;
};

static inline unsigned long mei_secs_to_jiffies(unsigned long sec)
{
	return msecs_to_jiffies(sec * MSEC_PER_SEC);
}


/*
 * mei init function prototypes
 */
struct mei_device *mei_device_init(struct pci_dev *pdev);
void mei_reset(struct mei_device *dev, int interrupts);
int mei_hw_init(struct mei_device *dev);
int mei_task_initialize_clients(void *data);
int mei_initialize_clients(struct mei_device *dev);
int mei_disconnect_host_client(struct mei_device *dev, struct mei_cl *cl);
void mei_remove_client_from_file_list(struct mei_device *dev, u8 host_client_id);
void mei_allocate_me_clients_storage(struct mei_device *dev);


int mei_me_cl_update_filext(struct mei_device *dev, struct mei_cl *cl,
			const uuid_le *cguid, u8 host_client_id);
int mei_me_cl_by_uuid(const struct mei_device *dev, const uuid_le *cuuid);
int mei_me_cl_by_id(struct mei_device *dev, u8 client_id);

/*
 * MEI IO Functions
 */
struct mei_cl_cb *mei_io_cb_init(struct mei_cl *cl, struct file *fp);
void mei_io_cb_free(struct mei_cl_cb *priv_cb);
int mei_io_cb_alloc_req_buf(struct mei_cl_cb *cb, size_t length);
int mei_io_cb_alloc_resp_buf(struct mei_cl_cb *cb, size_t length);


/**
 * mei_io_list_init - Sets up a queue list.
 *
 * @list: An instance cl callback structure
 */
static inline void mei_io_list_init(struct mei_cl_cb *list)
{
	INIT_LIST_HEAD(&list->list);
}
void mei_io_list_flush(struct mei_cl_cb *list, struct mei_cl *cl);

/*
 * MEI ME Client Functions
 */

struct mei_cl *mei_cl_allocate(struct mei_device *dev);
void mei_cl_init(struct mei_cl *cl, struct mei_device *dev);
int mei_cl_flush_queues(struct mei_cl *cl);
/**
 * mei_cl_cmp_id - tells if file private data have same id
 *
 * @fe1: private data of 1. file object
 * @fe2: private data of 2. file object
 *
 * returns true  - if ids are the same and not NULL
 */
static inline bool mei_cl_cmp_id(const struct mei_cl *cl1,
				const struct mei_cl *cl2)
{
	return cl1 && cl2 &&
		(cl1->host_client_id == cl2->host_client_id) &&
		(cl1->me_client_id == cl2->me_client_id);
}



/*
 * MEI Host Client Functions
 */
void mei_host_start_message(struct mei_device *dev);
void mei_host_enum_clients_message(struct mei_device *dev);
int mei_host_client_properties(struct mei_device *dev);

/*
 *  MEI interrupt functions prototype
 */
irqreturn_t mei_interrupt_quick_handler(int irq, void *dev_id);
irqreturn_t mei_interrupt_thread_handler(int irq, void *dev_id);
void mei_timer(struct work_struct *work);

/*
 *  MEI input output function prototype
 */
int mei_ioctl_connect_client(struct file *file,
			struct mei_connect_client_data *data);

int mei_start_read(struct mei_device *dev, struct mei_cl *cl);


/*
 * AMTHIF - AMT Host Interface Functions
 */
void mei_amthif_reset_params(struct mei_device *dev);

void mei_amthif_host_init(struct mei_device *dev);

int mei_amthif_write(struct mei_device *dev, struct mei_cl_cb *priv_cb);

int mei_amthif_read(struct mei_device *dev, struct file *file,
	      char __user *ubuf, size_t length, loff_t *offset);

struct mei_cl_cb *mei_amthif_find_read_list_entry(struct mei_device *dev,
						struct file *file);

void mei_amthif_run_next_cmd(struct mei_device *dev);


int mei_amthif_read_message(struct mei_cl_cb *complete_list,
		struct mei_device *dev, struct mei_msg_hdr *mei_hdr);

int mei_amthif_irq_process_completed(struct mei_device *dev, s32 *slots,
			struct mei_cl_cb *cb_pos,
			struct mei_cl *cl,
			struct mei_cl_cb *cmpl_list);

void mei_amthif_complete(struct mei_device *dev, struct mei_cl_cb *cb);
int mei_amthif_irq_read_message(struct mei_cl_cb *complete_list,
		struct mei_device *dev, struct mei_msg_hdr *mei_hdr);
int mei_amthif_irq_read(struct mei_device *dev, s32 *slots);

/*
 * Register Access Function
 */

/**
 * mei_reg_read - Reads 32bit data from the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to read the data
 *
 * returns register value (u32)
 */
static inline u32 mei_reg_read(const struct mei_device *dev,
			       unsigned long offset)
{
	return ioread32(dev->mem_addr + offset);
}

/**
 * mei_reg_write - Writes 32bit data to the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to write the data
 * @value: register value to write (u32)
 */
static inline void mei_reg_write(const struct mei_device *dev,
				 unsigned long offset, u32 value)
{
	iowrite32(value, dev->mem_addr + offset);
}

/**
 * mei_hcsr_read - Reads 32bit data from the host CSR
 *
 * @dev: the device structure
 *
 * returns the byte read.
 */
static inline u32 mei_hcsr_read(const struct mei_device *dev)
{
	return mei_reg_read(dev, H_CSR);
}

/**
 * mei_mecsr_read - Reads 32bit data from the ME CSR
 *
 * @dev: the device structure
 *
 * returns ME_CSR_HA register value (u32)
 */
static inline u32 mei_mecsr_read(const struct mei_device *dev)
{
	return mei_reg_read(dev, ME_CSR_HA);
}

/**
 * get_me_cb_rw - Reads 32bit data from the mei ME_CB_RW register
 *
 * @dev: the device structure
 *
 * returns ME_CB_RW register value (u32)
 */
static inline u32 mei_mecbrw_read(const struct mei_device *dev)
{
	return mei_reg_read(dev, ME_CB_RW);
}


/*
 * mei interface function prototypes
 */
void mei_hcsr_set(struct mei_device *dev);
void mei_csr_clear_his(struct mei_device *dev);

void mei_enable_interrupts(struct mei_device *dev);
void mei_disable_interrupts(struct mei_device *dev);

#endif

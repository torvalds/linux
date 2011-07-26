/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2011, Intel Corporation.
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
#include "mei.h"
#include "hw.h"

/*
 * MEI Char Driver Minors
 */
#define MEI_MINORS_BASE	1
#define MEI_MINORS_COUNT	1
#define MEI_MINOR_NUMBER	1

/*
 * watch dog definition
 */
#define MEI_WATCHDOG_DATA_SIZE         16
#define MEI_START_WD_DATA_SIZE         20
#define MEI_WD_PARAMS_SIZE             4
#define MEI_WD_STATE_INDEPENDENCE_MSG_SENT       (1 << 0)

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
 * Number of File descriptors/handles
 * that can be opened to the driver.
 *
 * Limit to 253: 255 Total Clients
 * minus internal client for AMTHI
 * minus internal client for Watchdog
 */
#define  MEI_MAX_OPEN_HANDLE_COUNT	253

/*
 * Number of Maximum MEI Clients
 */
#define MEI_CLIENTS_MAX 255

/* File state */
enum file_state {
	MEI_FILE_INITIALIZING = 0,
	MEI_FILE_CONNECTING,
	MEI_FILE_CONNECTED,
	MEI_FILE_DISCONNECTING,
	MEI_FILE_DISCONNECTED
};

/* MEI device states */
enum mei_states {
	MEI_INITIALIZING = 0,
	MEI_INIT_CLIENTS,
	MEI_ENABLED,
	MEI_RESETING,
	MEI_DISABLED,
	MEI_RECOVERING_FROM_RESET,
	MEI_POWER_DOWN,
	MEI_POWER_UP
};

/* init clients  states*/
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
	char *data;
} __packed;


struct mei_cl_cb {
	struct list_head cb_list;
	enum mei_cb_major_types major_file_operations;
	void *file_private;
	struct mei_message_data request_buffer;
	struct mei_message_data response_buffer;
	unsigned long information;
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

struct mei_io_list {
	struct mei_cl_cb mei_cb;
	int status;
};

/* MEI private device struct */
struct mei_device {
	struct pci_dev *pdev;	/* pointer to pci device struct */
	/*
	 * lists of queues
	 */
	 /* array of pointers to aio lists */
	struct mei_io_list read_list;		/* driver read queue */
	struct mei_io_list write_list;		/* driver write queue */
	struct mei_io_list write_waiting_list;	/* write waiting queue */
	struct mei_io_list ctrl_wr_list;	/* managed write IOCTL list */
	struct mei_io_list ctrl_rd_list;	/* managed read IOCTL list */
	struct mei_io_list amthi_cmd_list;	/* amthi list for cmd waiting */

	/* driver managed amthi list for reading completed amthi cmd data */
	struct mei_io_list amthi_read_complete_list;
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
	struct delayed_work wd_work;	/* watch dog deleye work */
	bool recvd_msg;
	/*
	 * hw states of host and fw(ME)
	 */
	u32 host_hw_state;
	u32 me_hw_state;
	/*
	 * waiting queue for receive message from FW
	 */
	wait_queue_head_t wait_recvd_msg;
	wait_queue_head_t wait_stop_wd;

	/*
	 * mei device  states
	 */
	enum mei_states mei_state;
	enum mei_init_clients_states init_clients_state;
	u16 init_clients_timer;
	bool stop;
	bool need_reset;

	u32 extra_write_index;
	u32 rd_msg_buf[128];	/* used for control messages */
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
	bool wd_pending;
	bool wd_stopped;
	bool wd_bypass;	/* if false, don't refresh watchdog ME client */
	u16 wd_timeout;	/* seconds ((wd_data[1] << 8) + wd_data[0]) */
	u16 wd_due_counter;
	unsigned char wd_data[MEI_START_WD_DATA_SIZE];



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
void mei_host_init_iamthif(struct mei_device *dev);
void mei_allocate_me_clients_storage(struct mei_device *dev);


u8 mei_find_me_client_update_filext(struct mei_device *dev,
				struct mei_cl *priv,
				const uuid_le *cguid, u8 client_id);

/*
 * MEI IO List Functions
 */
void mei_io_list_init(struct mei_io_list *list);
void mei_io_list_flush(struct mei_io_list *list, struct mei_cl *cl);

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
void mei_host_client_properties(struct mei_device *dev);

/*
 *  MEI interrupt functions prototype
 */
irqreturn_t mei_interrupt_quick_handler(int irq, void *dev_id);
irqreturn_t mei_interrupt_thread_handler(int irq, void *dev_id);
void mei_wd_timer(struct work_struct *work);

/*
 *  MEI input output function prototype
 */
int mei_ioctl_connect_client(struct file *file,
			struct mei_connect_client_data *data);

int mei_start_read(struct mei_device *dev, struct mei_cl *cl);

int amthi_write(struct mei_device *dev, struct mei_cl_cb *priv_cb);

int amthi_read(struct mei_device *dev, struct file *file,
	      char __user *ubuf, size_t length, loff_t *offset);

struct mei_cl_cb *find_amthi_read_list_entry(struct mei_device *dev,
						struct file *file);

void mei_run_next_iamthif_cmd(struct mei_device *dev);

void mei_free_cb_private(struct mei_cl_cb *priv_cb);

int mei_find_me_client_index(const struct mei_device *dev, uuid_le cuuid);

/*
 * Register Access Function
 */

/**
 * mei_reg_read - Reads 32bit data from the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to read the data
 *
 * returns the byte read.
 */
static inline u32 mei_reg_read(struct mei_device *dev, unsigned long offset)
{
	return ioread32(dev->mem_addr + offset);
}

/**
 * mei_reg_write - Writes 32bit data to the mei device
 *
 * @dev: the device structure
 * @offset: offset from which to write the data
 * @value: the byte to write
 */
static inline void mei_reg_write(struct mei_device *dev,
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
static inline u32 mei_hcsr_read(struct mei_device *dev)
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
static inline u32 mei_mecsr_read(struct mei_device *dev)
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
static inline u32 mei_mecbrw_read(struct mei_device *dev)
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

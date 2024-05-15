/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QAIC_H_
#define _QAIC_H_

#include <linux/interrupt.h>
#include <linux/kref.h>
#include <linux/mhi.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>

#define QAIC_DBC_BASE		SZ_128K
#define QAIC_DBC_SIZE		SZ_4K

#define QAIC_NO_PARTITION	-1

#define QAIC_DBC_OFF(i)		((i) * QAIC_DBC_SIZE + QAIC_DBC_BASE)

#define to_qaic_bo(obj) container_of(obj, struct qaic_bo, base)
#define to_qaic_drm_device(dev) container_of(dev, struct qaic_drm_device, drm)
#define to_drm(qddev) (&(qddev)->drm)
#define to_accel_kdev(qddev) (to_drm(qddev)->accel->kdev) /* Return Linux device of accel node */
#define to_qaic_device(dev) (to_qaic_drm_device((dev))->qdev)

enum __packed dev_states {
	/* Device is offline or will be very soon */
	QAIC_OFFLINE,
	/* Device is booting, not clear if it's in a usable state */
	QAIC_BOOT,
	/* Device is fully operational */
	QAIC_ONLINE,
};

extern bool datapath_polling;

struct qaic_user {
	/* Uniquely identifies this user for the device */
	int			handle;
	struct kref		ref_count;
	/* Char device opened by this user */
	struct qaic_drm_device	*qddev;
	/* Node in list of users that opened this drm device */
	struct list_head	node;
	/* SRCU used to synchronize this user during cleanup */
	struct srcu_struct	qddev_lock;
	atomic_t		chunk_id;
};

struct dma_bridge_chan {
	/* Pointer to device strcut maintained by driver */
	struct qaic_device	*qdev;
	/* ID of this DMA bridge channel(DBC) */
	unsigned int		id;
	/* Synchronizes access to xfer_list */
	spinlock_t		xfer_lock;
	/* Base address of request queue */
	void			*req_q_base;
	/* Base address of response queue */
	void			*rsp_q_base;
	/*
	 * Base bus address of request queue. Response queue bus address can be
	 * calculated by adding request queue size to this variable
	 */
	dma_addr_t		dma_addr;
	/* Total size of request and response queue in byte */
	u32			total_size;
	/* Capacity of request/response queue */
	u32			nelem;
	/* The user that opened this DBC */
	struct qaic_user	*usr;
	/*
	 * Request ID of next memory handle that goes in request queue. One
	 * memory handle can enqueue more than one request elements, all
	 * this requests that belong to same memory handle have same request ID
	 */
	u16			next_req_id;
	/* true: DBC is in use; false: DBC not in use */
	bool			in_use;
	/*
	 * Base address of device registers. Used to read/write request and
	 * response queue's head and tail pointer of this DBC.
	 */
	void __iomem		*dbc_base;
	/* Head of list where each node is a memory handle queued in request queue */
	struct list_head	xfer_list;
	/* Synchronizes DBC readers during cleanup */
	struct srcu_struct	ch_lock;
	/*
	 * When this DBC is released, any thread waiting on this wait queue is
	 * woken up
	 */
	wait_queue_head_t	dbc_release;
	/* Head of list where each node is a bo associated with this DBC */
	struct list_head	bo_lists;
	/* The irq line for this DBC. Used for polling */
	unsigned int		irq;
	/* Polling work item to simulate interrupts */
	struct work_struct	poll_work;
};

struct qaic_device {
	/* Pointer to base PCI device struct of our physical device */
	struct pci_dev		*pdev;
	/* Req. ID of request that will be queued next in MHI control device */
	u32			next_seq_num;
	/* Base address of bar 0 */
	void __iomem		*bar_0;
	/* Base address of bar 2 */
	void __iomem		*bar_2;
	/* Controller structure for MHI devices */
	struct mhi_controller	*mhi_cntrl;
	/* MHI control channel device */
	struct mhi_device	*cntl_ch;
	/* List of requests queued in MHI control device */
	struct list_head	cntl_xfer_list;
	/* Synchronizes MHI control device transactions and its xfer list */
	struct mutex		cntl_mutex;
	/* Array of DBC struct of this device */
	struct dma_bridge_chan	*dbc;
	/* Work queue for tasks related to MHI control device */
	struct workqueue_struct	*cntl_wq;
	/* Synchronizes all the users of device during cleanup */
	struct srcu_struct	dev_lock;
	/* Track the state of the device during resets */
	enum dev_states		dev_state;
	/* true: single MSI is used to operate device */
	bool			single_msi;
	/*
	 * true: A tx MHI transaction has failed and a rx buffer is still queued
	 * in control device. Such a buffer is considered lost rx buffer
	 * false: No rx buffer is lost in control device
	 */
	bool			cntl_lost_buf;
	/* Maximum number of DBC supported by this device */
	u32			num_dbc;
	/* Reference to the drm_device for this device when it is created */
	struct qaic_drm_device	*qddev;
	/* Generate the CRC of a control message */
	u32 (*gen_crc)(void *msg);
	/* Validate the CRC of a control message */
	bool (*valid_crc)(void *msg);
	/* MHI "QAIC_TIMESYNC" channel device */
	struct mhi_device	*qts_ch;
	/* Work queue for tasks related to MHI "QAIC_TIMESYNC" channel */
	struct workqueue_struct	*qts_wq;
	/* Head of list of page allocated by MHI bootlog device */
	struct list_head        bootlog;
	/* MHI bootlog channel device */
	struct mhi_device       *bootlog_ch;
	/* Work queue for tasks related to MHI bootlog device */
	struct workqueue_struct *bootlog_wq;
	/* Synchronizes access of pages in MHI bootlog device */
	struct mutex            bootlog_mutex;
};

struct qaic_drm_device {
	/* The drm device struct of this drm device */
	struct drm_device	drm;
	/* Pointer to the root device struct driven by this driver */
	struct qaic_device	*qdev;
	/*
	 * The physical device can be partition in number of logical devices.
	 * And each logical device is given a partition id. This member stores
	 * that id. QAIC_NO_PARTITION is a sentinel used to mark that this drm
	 * device is the actual physical device
	 */
	s32			partition_id;
	/* Head in list of users who have opened this drm device */
	struct list_head	users;
	/* Synchronizes access to users list */
	struct mutex		users_mutex;
};

struct qaic_bo {
	struct drm_gem_object	base;
	/* Scatter/gather table for allocate/imported BO */
	struct sg_table		*sgt;
	/* Head in list of slices of this BO */
	struct list_head	slices;
	/* Total nents, for all slices of this BO */
	int			total_slice_nents;
	/*
	 * Direction of transfer. It can assume only two value DMA_TO_DEVICE and
	 * DMA_FROM_DEVICE.
	 */
	int			dir;
	/* The pointer of the DBC which operates on this BO */
	struct dma_bridge_chan	*dbc;
	/* Number of slice that belongs to this buffer */
	u32			nr_slice;
	/* Number of slice that have been transferred by DMA engine */
	u32			nr_slice_xfer_done;
	/*
	 * If true then user has attached slicing information to this BO by
	 * calling DRM_IOCTL_QAIC_ATTACH_SLICE_BO ioctl.
	 */
	bool			sliced;
	/* Request ID of this BO if it is queued for execution */
	u16			req_id;
	/* Handle assigned to this BO */
	u32			handle;
	/* Wait on this for completion of DMA transfer of this BO */
	struct completion	xfer_done;
	/*
	 * Node in linked list where head is dbc->xfer_list.
	 * This link list contain BO's that are queued for DMA transfer.
	 */
	struct list_head	xfer_list;
	/*
	 * Node in linked list where head is dbc->bo_lists.
	 * This link list contain BO's that are associated with the DBC it is
	 * linked to.
	 */
	struct list_head	bo_list;
	struct {
		/*
		 * Latest timestamp(ns) at which kernel received a request to
		 * execute this BO
		 */
		u64		req_received_ts;
		/*
		 * Latest timestamp(ns) at which kernel enqueued requests of
		 * this BO for execution in DMA queue
		 */
		u64		req_submit_ts;
		/*
		 * Latest timestamp(ns) at which kernel received a completion
		 * interrupt for requests of this BO
		 */
		u64		req_processed_ts;
		/*
		 * Number of elements already enqueued in DMA queue before
		 * enqueuing requests of this BO
		 */
		u32		queue_level_before;
	} perf_stats;
	/* Synchronizes BO operations */
	struct mutex		lock;
};

struct bo_slice {
	/* Mapped pages */
	struct sg_table		*sgt;
	/* Number of requests required to queue in DMA queue */
	int			nents;
	/* See enum dma_data_direction */
	int			dir;
	/* Actual requests that will be copied in DMA queue */
	struct dbc_req		*reqs;
	struct kref		ref_count;
	/* true: No DMA transfer required */
	bool			no_xfer;
	/* Pointer to the parent BO handle */
	struct qaic_bo		*bo;
	/* Node in list of slices maintained by parent BO */
	struct list_head	slice;
	/* Size of this slice in bytes */
	u64			size;
	/* Offset of this slice in buffer */
	u64			offset;
};

int get_dbc_req_elem_size(void);
int get_dbc_rsp_elem_size(void);
int get_cntl_version(struct qaic_device *qdev, struct qaic_user *usr, u16 *major, u16 *minor);
int qaic_manage_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
void qaic_mhi_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result);

void qaic_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result);

int qaic_control_open(struct qaic_device *qdev);
void qaic_control_close(struct qaic_device *qdev);
void qaic_release_usr(struct qaic_device *qdev, struct qaic_user *usr);

irqreturn_t dbc_irq_threaded_fn(int irq, void *data);
irqreturn_t dbc_irq_handler(int irq, void *data);
int disable_dbc(struct qaic_device *qdev, u32 dbc_id, struct qaic_user *usr);
void enable_dbc(struct qaic_device *qdev, u32 dbc_id, struct qaic_user *usr);
void wakeup_dbc(struct qaic_device *qdev, u32 dbc_id);
void release_dbc(struct qaic_device *qdev, u32 dbc_id);
void qaic_data_get_fifo_info(struct dma_bridge_chan *dbc, u32 *head, u32 *tail);

void wake_all_cntl(struct qaic_device *qdev);
void qaic_dev_reset_clean_local_state(struct qaic_device *qdev);

struct drm_gem_object *qaic_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);

int qaic_create_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_mmap_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_attach_slice_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_execute_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_partial_execute_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_wait_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_perf_stats_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int qaic_detach_slice_bo_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
void irq_polling_work(struct work_struct *work);

#endif /* _QAIC_H_ */

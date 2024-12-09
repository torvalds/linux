/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ISHTP bus definitions
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 */
#ifndef _LINUX_ISHTP_CL_BUS_H
#define _LINUX_ISHTP_CL_BUS_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/intel-ish-client-if.h>

struct ishtp_cl;
struct ishtp_cl_device;
struct ishtp_device;
struct ishtp_msg_hdr;

/**
 * struct ishtp_cl_device - ISHTP device handle
 * @dev:	device pointer
 * @ishtp_dev:	pointer to ishtp device structure to primarily to access
 *		hw device operation callbacks and properties
 * @fw_client:	fw_client pointer to get fw information like protocol name
 *		max message length etc.
 * @device_link: Link to next client in the list on a bus
 * @event_work:	Used to schedule rx event for client
 * @driver_data: Storage driver private data
 * @reference_count:	Used for get/put device
 * @event_cb:	Callback to driver to send events
 *
 * An ishtp_cl_device pointer is returned from ishtp_add_device()
 * and links ISHTP bus clients to their actual host client pointer.
 * Drivers for ISHTP devices will get an ishtp_cl_device pointer
 * when being probed and shall use it for doing bus I/O.
 */
struct ishtp_cl_device {
	struct device		dev;
	struct ishtp_device	*ishtp_dev;
	struct ishtp_fw_client	*fw_client;
	struct list_head	device_link;
	struct work_struct	event_work;
	void			*driver_data;
	int			reference_count;
	void (*event_cb)(struct ishtp_cl_device *device);
};

int	ishtp_bus_new_client(struct ishtp_device *dev);
int	ishtp_cl_device_bind(struct ishtp_cl *cl);
void	ishtp_cl_bus_rx_event(struct ishtp_cl_device *device);

/* Write a multi-fragment message */
int	ishtp_send_msg(struct ishtp_device *dev,
		       struct ishtp_msg_hdr *hdr, void *msg,
		       void (*ipc_send_compl)(void *),
		       void *ipc_send_compl_prm);

/* Write a single-fragment message */
int	ishtp_write_message(struct ishtp_device *dev,
			    struct ishtp_msg_hdr *hdr,
			    void *buf);

/* Use DMA to send/receive messages */
int ishtp_use_dma_transfer(void);

/* Exported functions */
void	ishtp_bus_remove_all_clients(struct ishtp_device *ishtp_dev,
				     bool warm_reset);

void	ishtp_recv(struct ishtp_device *dev);
void	ishtp_reset_handler(struct ishtp_device *dev);
void	ishtp_reset_compl_handler(struct ishtp_device *dev);

int	ishtp_fw_cl_by_uuid(struct ishtp_device *dev, const guid_t *cuuid);
#endif /* _LINUX_ISHTP_CL_BUS_H */

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


#ifndef _HECI_INTERFACE_H_
#define _HECI_INTERFACE_H_

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/aio.h>
#include <linux/types.h>
#include "heci_data_structures.h"


#define HBM_MINOR_VERSION                   0
#define HBM_MAJOR_VERSION                   1
#define HBM_TIMEOUT                         1	/* 1 second */


#define HOST_START_REQ_CMD                  0x01
#define HOST_START_RES_CMD                  0x81

#define HOST_STOP_REQ_CMD                   0x02
#define HOST_STOP_RES_CMD                   0x82

#define ME_STOP_REQ_CMD                     0x03

#define HOST_ENUM_REQ_CMD                   0x04
#define HOST_ENUM_RES_CMD                   0x84

#define HOST_CLIENT_PROPERTEIS_REQ_CMD      0x05
#define HOST_CLIENT_PROPERTEIS_RES_CMD      0x85

#define CLIENT_CONNECT_REQ_CMD              0x06
#define CLIENT_CONNECT_RES_CMD              0x86

#define CLIENT_DISCONNECT_REQ_CMD           0x07
#define CLIENT_DISCONNECT_RES_CMD           0x87

#define HECI_FLOW_CONTROL_CMD               0x08


#define AMT_WD_VALUE 120	/* seconds */

#define HECI_WATCHDOG_DATA_SIZE         16
#define HECI_START_WD_DATA_SIZE         20
#define HECI_WD_PARAMS_SIZE             4

/* IOCTL commands */
#define IOCTL_HECI_GET_VERSION \
    _IOWR('H' , 0x0, struct heci_message_data)
#define IOCTL_HECI_CONNECT_CLIENT \
    _IOWR('H' , 0x01, struct heci_message_data)
#define IOCTL_HECI_WD \
    _IOWR('H' , 0x02, struct heci_message_data)
#define IOCTL_HECI_BYPASS_WD \
    _IOWR('H' , 0x10, struct heci_message_data)

enum heci_stop_reason_types{
	DRIVER_STOP_REQUEST = 0x00,
	DEVICE_D1_ENTRY = 0x01,
	DEVICE_D2_ENTRY = 0x02,
	DEVICE_D3_ENTRY = 0x03,
	SYSTEM_S1_ENTRY = 0x04,
	SYSTEM_S2_ENTRY = 0x05,
	SYSTEM_S3_ENTRY = 0x06,
	SYSTEM_S4_ENTRY = 0x07,
	SYSTEM_S5_ENTRY = 0x08
};

enum me_stop_reason_types{
	FW_UPDATE = 0x00
};

enum client_connect_status_types{
	CCS_SUCCESS = 0x00,
	CCS_NOT_FOUND = 0x01,
	CCS_ALREADY_STARTED = 0x02,
	CCS_OUT_OF_RESOURCES = 0x03,
	CCS_MESSAGE_SMALL = 0x04
};

enum client_disconnect_status_types{
	CDS_SUCCESS = 0x00
};


/*
 * heci interface function prototypes
 */
void heci_set_csr_register(struct iamt_heci_device *dev);
void heci_csr_enable_interrupts(struct iamt_heci_device *dev);
void heci_csr_disable_interrupts(struct iamt_heci_device *dev);
void heci_csr_clear_his(struct iamt_heci_device *dev);

void heci_read_slots(struct iamt_heci_device *dev,
		     unsigned char *buffer, unsigned long buffer_length);

int heci_write_message(struct iamt_heci_device *dev,
			     struct heci_msg_hdr *header,
			     unsigned char *write_buffer,
			     unsigned long write_length);

int host_buffer_is_empty(struct iamt_heci_device *dev);

__s32 count_full_read_slots(struct iamt_heci_device *dev);

__s32 count_empty_write_slots(const struct iamt_heci_device *dev);

int flow_ctrl_creds(struct iamt_heci_device *dev,
				   struct heci_file_private *file_ext);

int heci_send_wd(struct iamt_heci_device *dev);

void flow_ctrl_reduce(struct iamt_heci_device *dev,
			 struct heci_file_private *file_ext);

int heci_send_flow_control(struct iamt_heci_device *dev,
				 struct heci_file_private *file_ext);

int heci_disconnect(struct iamt_heci_device *dev,
			  struct heci_file_private *file_ext);
int other_client_is_connecting(struct iamt_heci_device *dev,
				     struct heci_file_private *file_ext);
int heci_connect(struct iamt_heci_device *dev,
		       struct heci_file_private *file_ext);

#endif /* _HECI_INTERFACE_H_ */

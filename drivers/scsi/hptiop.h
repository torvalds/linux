/*
 * HighPoint RR3xxx controller driver for Linux
 * Copyright (C) 2006-2007 HighPoint Technologies, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Please report bugs/comments/suggestions to linux@highpoint-tech.com
 *
 * For more information, visit http://www.highpoint-tech.com
 */
#ifndef _HPTIOP_H_
#define _HPTIOP_H_

struct hpt_iopmu
{
	__le32 resrved0[4];
	__le32 inbound_msgaddr0;
	__le32 inbound_msgaddr1;
	__le32 outbound_msgaddr0;
	__le32 outbound_msgaddr1;
	__le32 inbound_doorbell;
	__le32 inbound_intstatus;
	__le32 inbound_intmask;
	__le32 outbound_doorbell;
	__le32 outbound_intstatus;
	__le32 outbound_intmask;
	__le32 reserved1[2];
	__le32 inbound_queue;
	__le32 outbound_queue;
};

#define IOPMU_QUEUE_EMPTY            0xffffffff
#define IOPMU_QUEUE_MASK_HOST_BITS   0xf0000000
#define IOPMU_QUEUE_ADDR_HOST_BIT    0x80000000
#define IOPMU_QUEUE_REQUEST_SIZE_BIT    0x40000000
#define IOPMU_QUEUE_REQUEST_RESULT_BIT   0x40000000

#define IOPMU_OUTBOUND_INT_MSG0      1
#define IOPMU_OUTBOUND_INT_MSG1      2
#define IOPMU_OUTBOUND_INT_DOORBELL  4
#define IOPMU_OUTBOUND_INT_POSTQUEUE 8
#define IOPMU_OUTBOUND_INT_PCI       0x10

#define IOPMU_INBOUND_INT_MSG0       1
#define IOPMU_INBOUND_INT_MSG1       2
#define IOPMU_INBOUND_INT_DOORBELL   4
#define IOPMU_INBOUND_INT_ERROR      8
#define IOPMU_INBOUND_INT_POSTQUEUE  0x10

enum hpt_iopmu_message {
	/* host-to-iop messages */
	IOPMU_INBOUND_MSG0_NOP = 0,
	IOPMU_INBOUND_MSG0_RESET,
	IOPMU_INBOUND_MSG0_FLUSH,
	IOPMU_INBOUND_MSG0_SHUTDOWN,
	IOPMU_INBOUND_MSG0_STOP_BACKGROUND_TASK,
	IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK,
	IOPMU_INBOUND_MSG0_MAX = 0xff,
	/* iop-to-host messages */
	IOPMU_OUTBOUND_MSG0_REGISTER_DEVICE_0 = 0x100,
	IOPMU_OUTBOUND_MSG0_REGISTER_DEVICE_MAX = 0x1ff,
	IOPMU_OUTBOUND_MSG0_UNREGISTER_DEVICE_0 = 0x200,
	IOPMU_OUTBOUND_MSG0_UNREGISTER_DEVICE_MAX = 0x2ff,
	IOPMU_OUTBOUND_MSG0_REVALIDATE_DEVICE_0 = 0x300,
	IOPMU_OUTBOUND_MSG0_REVALIDATE_DEVICE_MAX = 0x3ff,
};

struct hpt_iop_request_header
{
	__le32 size;
	__le32 type;
	__le32 flags;
	__le32 result;
	__le32 context; /* host context */
	__le32 context_hi32;
};

#define IOP_REQUEST_FLAG_SYNC_REQUEST 1
#define IOP_REQUEST_FLAG_BIST_REQUEST 2
#define IOP_REQUEST_FLAG_REMAPPED     4
#define IOP_REQUEST_FLAG_OUTPUT_CONTEXT 8

enum hpt_iop_request_type {
	IOP_REQUEST_TYPE_GET_CONFIG = 0,
	IOP_REQUEST_TYPE_SET_CONFIG,
	IOP_REQUEST_TYPE_BLOCK_COMMAND,
	IOP_REQUEST_TYPE_SCSI_COMMAND,
	IOP_REQUEST_TYPE_IOCTL_COMMAND,
	IOP_REQUEST_TYPE_MAX
};

enum hpt_iop_result_type {
	IOP_RESULT_PENDING = 0,
	IOP_RESULT_SUCCESS,
	IOP_RESULT_FAIL,
	IOP_RESULT_BUSY,
	IOP_RESULT_RESET,
	IOP_RESULT_INVALID_REQUEST,
	IOP_RESULT_BAD_TARGET,
	IOP_RESULT_MODE_SENSE_CHECK_CONDITION,
};

struct hpt_iop_request_get_config
{
	struct hpt_iop_request_header header;
	__le32 interface_version;
	__le32 firmware_version;
	__le32 max_requests;
	__le32 request_size;
	__le32 max_sg_count;
	__le32 data_transfer_length;
	__le32 alignment_mask;
	__le32 max_devices;
	__le32 sdram_size;
};

struct hpt_iop_request_set_config
{
	struct hpt_iop_request_header header;
	__le32 iop_id;
	__le16 vbus_id;
	__le16 max_host_request_size;
	__le32 reserve[6];
};

struct hpt_iopsg
{
	__le32 size;
	__le32 eot; /* non-zero: end of table */
	__le64 pci_address;
};

struct hpt_iop_request_block_command
{
	struct hpt_iop_request_header header;
	u8     channel;
	u8     target;
	u8     lun;
	u8     pad1;
	__le16 command; /* IOP_BLOCK_COMMAND_{READ,WRITE} */
	__le16 sectors;
	__le64 lba;
	struct hpt_iopsg sg_list[1];
};

#define IOP_BLOCK_COMMAND_READ     1
#define IOP_BLOCK_COMMAND_WRITE    2
#define IOP_BLOCK_COMMAND_VERIFY   3
#define IOP_BLOCK_COMMAND_FLUSH    4
#define IOP_BLOCK_COMMAND_SHUTDOWN 5

struct hpt_iop_request_scsi_command
{
	struct hpt_iop_request_header header;
	u8     channel;
	u8     target;
	u8     lun;
	u8     pad1;
	u8     cdb[16];
	__le32 dataxfer_length;
	struct hpt_iopsg sg_list[1];
};

struct hpt_iop_request_ioctl_command
{
	struct hpt_iop_request_header header;
	__le32 ioctl_code;
	__le32 inbuf_size;
	__le32 outbuf_size;
	__le32 bytes_returned;
	u8     buf[1];
	/* out data should be put at buf[(inbuf_size+3)&~3] */
};

#define HPTIOP_MAX_REQUESTS  256u

struct hptiop_request {
	struct hptiop_request * next;
	void *                  req_virt;
	u32                     req_shifted_phy;
	struct scsi_cmnd *      scp;
	int                     index;
};

struct hpt_scsi_pointer {
	int mapped;
	int sgcnt;
	dma_addr_t dma_handle;
};

#define HPT_SCP(scp) ((struct hpt_scsi_pointer *)&(scp)->SCp)

struct hptiop_hba {
	struct hpt_iopmu __iomem * iop;
	struct Scsi_Host * host;
	struct pci_dev * pcidev;

	/* IOP config info */
	u32     interface_version;
	u32     firmware_version;
	u32     sdram_size;
	u32     max_devices;
	u32     max_requests;
	u32     max_request_size;
	u32     max_sg_descriptors;

	u32     req_size; /* host-allocated request buffer size */

	int     iopintf_v2: 1;
	int     initialized: 1;
	int     msg_done: 1;

	struct hptiop_request * req_list;
	struct hptiop_request reqs[HPTIOP_MAX_REQUESTS];

	/* used to free allocated dma area */
	void *      dma_coherent;
	dma_addr_t  dma_coherent_handle;

	atomic_t    reset_count;
	atomic_t    resetting;

	wait_queue_head_t reset_wq;
	wait_queue_head_t ioctl_wq;
};

struct hpt_ioctl_k
{
	struct hptiop_hba * hba;
	u32    ioctl_code;
	u32    inbuf_size;
	u32    outbuf_size;
	void * inbuf;
	void * outbuf;
	u32  * bytes_returned;
	void (*done)(struct hpt_ioctl_k *);
	int    result; /* HPT_IOCTL_RESULT_ */
};

#define HPT_IOCTL_RESULT_OK         0
#define HPT_IOCTL_RESULT_FAILED     (-1)

#if 0
#define dprintk(fmt, args...) do { printk(fmt, ##args); } while(0)
#else
#define dprintk(fmt, args...)
#endif

#endif

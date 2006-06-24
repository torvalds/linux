/*
 * HighPoint RR3xxx controller driver for Linux
 * Copyright (C) 2006 HighPoint Technologies, Inc. All Rights Reserved.
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

/*
 * logical device type.
 * Identify array (logical device) and physical device.
 */
#define LDT_ARRAY   1
#define LDT_DEVICE  2

/*
 * Array types
 */
#define AT_UNKNOWN  0
#define AT_RAID0    1
#define AT_RAID1    2
#define AT_RAID5    3
#define AT_RAID6    4
#define AT_JBOD     7

#define MAX_NAME_LENGTH     36
#define MAX_ARRAYNAME_LEN   16

#define MAX_ARRAY_MEMBERS_V1 8
#define MAX_ARRAY_MEMBERS_V2 16

/* keep definition for source code compatiblity */
#define MAX_ARRAY_MEMBERS MAX_ARRAY_MEMBERS_V1

/*
 * array flags
 */
#define ARRAY_FLAG_DISABLED         0x00000001 /* The array is disabled */
#define ARRAY_FLAG_NEEDBUILDING     0x00000002 /* need to be rebuilt */
#define ARRAY_FLAG_REBUILDING       0x00000004 /* in rebuilding process */
#define ARRAY_FLAG_BROKEN           0x00000008 /* broken but still working */
#define ARRAY_FLAG_BOOTDISK         0x00000010 /* has a active partition */
#define ARRAY_FLAG_BOOTMARK         0x00000040 /* array has boot mark set */
#define ARRAY_FLAG_NEED_AUTOREBUILD 0x00000080 /* auto-rebuild should start */
#define ARRAY_FLAG_VERIFYING        0x00000100 /* is being verified */
#define ARRAY_FLAG_INITIALIZING     0x00000200 /* is being initialized */
#define ARRAY_FLAG_TRANSFORMING     0x00000400 /* tranform in progress */
#define ARRAY_FLAG_NEEDTRANSFORM    0x00000800 /* array need tranform */
#define ARRAY_FLAG_NEEDINITIALIZING 0x00001000 /* initialization not done */
#define ARRAY_FLAG_BROKEN_REDUNDANT 0x00002000 /* broken but redundant */

/*
 * device flags
 */
#define DEVICE_FLAG_DISABLED        0x00000001 /* device is disabled */
#define DEVICE_FLAG_UNINITIALIZED   0x00010000 /* device is not initialized */
#define DEVICE_FLAG_LEGACY          0x00020000 /* lagacy drive */
#define DEVICE_FLAG_IS_SPARE        0x80000000 /* is a spare disk */

/*
 * ioctl codes
 */
#define HPT_CTL_CODE(x) (x+0xFF00)
#define HPT_CTL_CODE_LINUX_TO_IOP(x) ((x)-0xff00)

#define HPT_IOCTL_GET_CONTROLLER_INFO       HPT_CTL_CODE(2)
#define HPT_IOCTL_GET_CHANNEL_INFO          HPT_CTL_CODE(3)
#define HPT_IOCTL_GET_LOGICAL_DEVICES       HPT_CTL_CODE(4)
#define HPT_IOCTL_GET_DRIVER_CAPABILITIES   HPT_CTL_CODE(19)
#define HPT_IOCTL_GET_DEVICE_INFO_V3        HPT_CTL_CODE(46)
#define HPT_IOCTL_GET_CONTROLLER_INFO_V2    HPT_CTL_CODE(47)

/*
 * Controller information.
 */
struct hpt_controller_info {
	u8      chip_type;                    /* chip type */
	u8      interrupt_level;              /* IRQ level */
	u8      num_buses;                    /* bus count */
	u8      chip_flags;

	u8      product_id[MAX_NAME_LENGTH];/* product name */
	u8      vendor_id[MAX_NAME_LENGTH]; /* vendor name */
}
__attribute__((packed));

/*
 * Channel information.
 */
struct hpt_channel_info {
	__le32  io_port;         /* IDE Base Port Address */
	__le32  control_port;    /* IDE Control Port Address */
	__le32  devices[2];      /* device connected to this channel */
}
__attribute__((packed));

/*
 * Array information.
 */
struct hpt_array_info_v3 {
	u8      name[MAX_ARRAYNAME_LEN]; /* array name */
	u8      description[64];         /* array description */
	u8      create_manager[16];      /* who created it */
	__le32  create_time;             /* when created it */

	u8      array_type;              /* array type */
	u8      block_size_shift;        /* stripe size */
	u8      ndisk;                   /* Number of ID in Members[] */
	u8      reserved;

	__le32  flags;                   /* working flags, see ARRAY_FLAG_XXX */
	__le32  members[MAX_ARRAY_MEMBERS_V2];  /* member array/disks */

	__le32  rebuilding_progress;
	__le64  rebuilt_sectors; /* rebuilding point (LBA) for single member */

	__le32  transform_source;
	__le32  transform_target;    /* destination device ID */
	__le32  transforming_progress;
	__le32  signature;              /* persistent identification*/
	__le16  critical_members;       /* bit mask of critical members */
	__le16  reserve2;
	__le32  reserve;
}
__attribute__((packed));

/*
 * physical device information.
 */
#define MAX_PARENTS_PER_DISK    8

struct hpt_device_info_v2 {
	u8   ctlr_id;             /* controller id */
	u8   path_id;             /* bus */
	u8   target_id;           /* id */
	u8   device_mode_setting; /* Current Data Transfer mode: 0-4 PIO0-4 */
				  /* 5-7 MW DMA0-2, 8-13 UDMA0-5 */
	u8   device_type;         /* device type */
	u8   usable_mode;         /* highest usable mode */

#ifdef __BIG_ENDIAN_BITFIELD
	u8   NCQ_enabled: 1;
	u8   NCQ_supported: 1;
	u8   TCQ_enabled: 1;
	u8   TCQ_supported: 1;
	u8   write_cache_enabled: 1;
	u8   write_cache_supported: 1;
	u8   read_ahead_enabled: 1;
	u8   read_ahead_supported: 1;
	u8   reserved6: 6;
	u8   spin_up_mode: 2;
#else
	u8   read_ahead_supported: 1;
	u8   read_ahead_enabled: 1;
	u8   write_cache_supported: 1;
	u8   write_cache_enabled: 1;
	u8   TCQ_supported: 1;
	u8   TCQ_enabled: 1;
	u8   NCQ_supported: 1;
	u8   NCQ_enabled: 1;
	u8   spin_up_mode: 2;
	u8   reserved6: 6;
#endif

	__le32  flags;         /* working flags, see DEVICE_FLAG_XXX */
	u8      ident[150];    /* (partitial) Identify Data of this device */

	__le64  total_free;
	__le64  max_free;
	__le64  bad_sectors;
	__le32  parent_arrays[MAX_PARENTS_PER_DISK];
}
__attribute__((packed));

/*
 * Logical device information.
 */
#define INVALID_TARGET_ID   0xFF
#define INVALID_BUS_ID      0xFF

struct hpt_logical_device_info_v3 {
	u8       type;                   /* LDT_ARRAY or LDT_DEVICE */
	u8       cache_policy;           /* refer to CACHE_POLICY_xxx */
	u8       vbus_id;                /* vbus sequence in vbus_list */
	u8       target_id;              /* OS target id. 0xFF is invalid */
					 /* OS name: DISK $VBusId_$TargetId */
	__le64   capacity;               /* array capacity */
	__le32   parent_array;           /* don't use this field for physical
					    device. use ParentArrays field in
					    hpt_device_info_v2 */
	/* reserved statistic fields */
	__le32   stat1;
	__le32   stat2;
	__le32   stat3;
	__le32   stat4;

	union {
		struct hpt_array_info_v3 array;
		struct hpt_device_info_v2 device;
	} __attribute__((packed)) u;

}
__attribute__((packed));

/*
 * ioctl structure
 */
#define HPT_IOCTL_MAGIC   0xA1B2C3D4

struct hpt_ioctl_u {
	u32   magic;            /* used to check if it's a valid ioctl packet */
	u32   ioctl_code;       /* operation control code */
	void __user *inbuf;     /* input data buffer */
	u32   inbuf_size;       /* size of input data buffer */
	void __user *outbuf;    /* output data buffer */
	u32   outbuf_size;      /* size of output data buffer */
	void __user *bytes_returned;   /* count of bytes returned */
}
__attribute__((packed));


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
	__le32 vbus_id;
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

	struct list_head link;

	/* IOP config info */
	u32     firmware_version;
	u32     sdram_size;
	u32     max_devices;
	u32     max_requests;
	u32     max_request_size;
	u32     max_sg_descriptors;

	u32     req_size; /* host-allocated request buffer size */
	int     initialized;
	int     msg_done;

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

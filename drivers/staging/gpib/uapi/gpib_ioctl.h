/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright            : (C) 2002 by Frank Mori Hess
 ***************************************************************************/

#ifndef _GPIB_IOCTL_H
#define _GPIB_IOCTL_H

#include <asm/ioctl.h>
#include <linux/types.h>

#define GPIB_CODE 160

struct gpib_board_type_ioctl {
	char name[100];
};

/* argument for read/write/command ioctls */
struct gpib_read_write_ioctl {
	__u64 buffer_ptr;
	__u32 requested_transfer_count;
	__u32 completed_transfer_count;
	__s32 end; /* end flag return for reads, end io suppression request for cmd*/
	__s32 handle;
};

struct gpib_open_dev_ioctl {
	__u32 handle;
	__u32 pad;
	__s32 sad;
	__u32 is_board;
};

struct gpib_close_dev_ioctl {
	__u32 handle;
};

struct gpib_serial_poll_ioctl {
	__u32 pad;
	__s32 sad;
	__u8 status_byte;
	__u8 padding[3];   // align to 32 bit boundary
};

struct gpib_eos_ioctl {
	__s32 eos;
	__s32 eos_flags;
};

struct gpib_wait_ioctl {
	__s32 handle;
	__s32 wait_mask;
	__s32 clear_mask;
	__s32 set_mask;
	__s32 ibsta;
	__s32 pad;
	__s32 sad;
	__u32 usec_timeout;
};

struct gpib_online_ioctl {
	__u64 init_data_ptr;
	__s32 init_data_length;
	__s32 online;
};

struct gpib_spoll_bytes_ioctl {
	__u32 num_bytes;
	__u32 pad;
	__s32 sad;
};

struct gpib_board_info_ioctl {
	__u32 pad;
	__s32 sad;
	__s32 parallel_poll_configuration;
	__s32 autopolling;
	__s32 is_system_controller;
	__u32 t1_delay;
	unsigned ist : 1;
	unsigned no_7_bit_eos : 1;
	unsigned padding :30; // align to 32 bit boundary
};

struct gpib_select_pci_ioctl {
	__s32 pci_bus;
	__s32 pci_slot;
};

struct gpib_ppoll_config_ioctl {
	__u8 config;
	unsigned set_ist : 1;
	unsigned clear_ist : 1;
	unsigned padding :22; // align to 32 bit boundary
};

struct gpib_pad_ioctl {
	__u32 handle;
	__u32 pad;
};

struct gpib_sad_ioctl {
	__u32 handle;
	__s32 sad;
};

// select a piece of hardware to attach by its sysfs device path
struct gpib_select_device_path_ioctl {
	char device_path[0x1000];
};

// update status byte and request service
struct gpib_request_service2 {
	__u8 status_byte;
	__u8 padding[3]; // align to 32 bit boundary
	__s32 new_reason_for_service;
};

/* Standard functions. */
enum gpib_ioctl {
	IBRD = _IOWR(GPIB_CODE, 100, struct gpib_read_write_ioctl),
	IBWRT = _IOWR(GPIB_CODE, 101, struct gpib_read_write_ioctl),
	IBCMD = _IOWR(GPIB_CODE, 102, struct gpib_read_write_ioctl),
	IBOPENDEV = _IOWR(GPIB_CODE, 3, struct gpib_open_dev_ioctl),
	IBCLOSEDEV = _IOW(GPIB_CODE, 4, struct gpib_close_dev_ioctl),
	IBWAIT = _IOWR(GPIB_CODE, 5, struct gpib_wait_ioctl),
	IBRPP = _IOWR(GPIB_CODE, 6, __u8),

	IBSIC = _IOW(GPIB_CODE, 9, __u32),
	IBSRE = _IOW(GPIB_CODE, 10, __s32),
	IBGTS = _IO(GPIB_CODE, 11),
	IBCAC = _IOW(GPIB_CODE, 12, __s32),
	IBLINES = _IOR(GPIB_CODE, 14, __s16),
	IBPAD = _IOW(GPIB_CODE, 15, struct gpib_pad_ioctl),
	IBSAD = _IOW(GPIB_CODE, 16, struct gpib_sad_ioctl),
	IBTMO = _IOW(GPIB_CODE, 17, __u32),
	IBRSP = _IOWR(GPIB_CODE, 18, struct gpib_serial_poll_ioctl),
	IBEOS = _IOW(GPIB_CODE, 19, struct gpib_eos_ioctl),
	IBRSV = _IOW(GPIB_CODE, 20, __u8),
	CFCBASE = _IOW(GPIB_CODE, 21, __u64),
	CFCIRQ = _IOW(GPIB_CODE, 22, __u32),
	CFCDMA = _IOW(GPIB_CODE, 23, __u32),
	CFCBOARDTYPE = _IOW(GPIB_CODE, 24, struct gpib_board_type_ioctl),

	IBMUTEX = _IOW(GPIB_CODE, 26, __s32),
	IBSPOLL_BYTES = _IOWR(GPIB_CODE, 27, struct gpib_spoll_bytes_ioctl),
	IBPPC = _IOW(GPIB_CODE, 28, struct gpib_ppoll_config_ioctl),
	IBBOARD_INFO = _IOR(GPIB_CODE, 29, struct gpib_board_info_ioctl),

	IBQUERY_BOARD_RSV = _IOR(GPIB_CODE, 31, __s32),
	IBSELECT_PCI = _IOWR(GPIB_CODE, 32, struct gpib_select_pci_ioctl),
	IBEVENT = _IOR(GPIB_CODE, 33, __s16),
	IBRSC = _IOW(GPIB_CODE, 34, __s32),
	IB_T1_DELAY = _IOW(GPIB_CODE, 35, __u32),
	IBLOC = _IO(GPIB_CODE, 36),

	IBAUTOSPOLL = _IOW(GPIB_CODE, 38, __s16),
	IBONL = _IOW(GPIB_CODE, 39, struct gpib_online_ioctl),
	IBPP2_SET = _IOW(GPIB_CODE, 40, __s16),
	IBPP2_GET = _IOR(GPIB_CODE, 41, __s16),
	IBSELECT_DEVICE_PATH = _IOW(GPIB_CODE, 43, struct gpib_select_device_path_ioctl),
	// 44 was IBSELECT_SERIAL_NUMBER
	IBRSV2 = _IOW(GPIB_CODE, 45, struct gpib_request_service2)
};

#endif	/* _GPIB_IOCTL_H */

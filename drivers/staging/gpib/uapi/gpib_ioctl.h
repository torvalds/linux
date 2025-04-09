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
	uint64_t buffer_ptr;
	unsigned int requested_transfer_count;
	unsigned int completed_transfer_count;
	int end; /* end flag return for reads, end io suppression request for cmd*/
	int handle;
};

struct gpib_open_dev_ioctl {
	unsigned int handle;
	unsigned int pad;
	int sad;
	unsigned is_board : 1;
};

struct gpib_close_dev_ioctl {
	unsigned int handle;
};

struct gpib_serial_poll_ioctl {
	unsigned int pad;
	int sad;
	uint8_t status_byte;
};

struct gpib_eos_ioctl {
	int eos;
	int eos_flags;
};

struct gpib_wait_ioctl {
	int handle;
	int wait_mask;
	int clear_mask;
	int set_mask;
	int ibsta;
	int pad;
	int sad;
	unsigned int usec_timeout;
};

struct gpib_online_ioctl {
	uint64_t init_data_ptr;
	int init_data_length;
	int online;
};

struct gpib_spoll_bytes_ioctl {
	unsigned int num_bytes;
	unsigned int pad;
	int sad;
};

struct gpib_board_info_ioctl {
	unsigned int pad;
	int sad;
	int parallel_poll_configuration;
	int autopolling;
	int is_system_controller;
	unsigned int t1_delay;
	unsigned ist : 1;
	unsigned no_7_bit_eos : 1;
};

struct gpib_select_pci_ioctl {
	int pci_bus;
	int pci_slot;
};

struct gpib_ppoll_config_ioctl {
	uint8_t config;
	unsigned set_ist : 1;
	unsigned clear_ist : 1;
};

struct gpib_pad_ioctl {
	unsigned int handle;
	unsigned int pad;
};

typedef struct {
	unsigned int handle;
	int sad;
} sad_ioctl_t;

// select a piece of hardware to attach by its sysfs device path
typedef struct {
	char device_path[0x1000];
} select_device_path_ioctl_t;

typedef short event_ioctl_t;
typedef int rsc_ioctl_t;
typedef unsigned int t1_delay_ioctl_t;
typedef short autospoll_ioctl_t;
typedef short local_ppoll_mode_ioctl_t;

// update status byte and request service
typedef struct {
	uint8_t status_byte;
	int new_reason_for_service;
} request_service2_t;

/* Standard functions. */
enum gpib_ioctl {
	IBRD = _IOWR(GPIB_CODE, 100, struct gpib_read_write_ioctl),
	IBWRT = _IOWR(GPIB_CODE, 101, struct gpib_read_write_ioctl),
	IBCMD = _IOWR(GPIB_CODE, 102, struct gpib_read_write_ioctl),
	IBOPENDEV = _IOWR(GPIB_CODE, 3, struct gpib_open_dev_ioctl),
	IBCLOSEDEV = _IOW(GPIB_CODE, 4, struct gpib_close_dev_ioctl),
	IBWAIT = _IOWR(GPIB_CODE, 5, struct gpib_wait_ioctl),
	IBRPP = _IOWR(GPIB_CODE, 6, uint8_t),

	IBSIC = _IOW(GPIB_CODE, 9, unsigned int),
	IBSRE = _IOW(GPIB_CODE, 10, int),
	IBGTS = _IO(GPIB_CODE, 11),
	IBCAC = _IOW(GPIB_CODE, 12, int),
	IBLINES = _IOR(GPIB_CODE, 14, short),
	IBPAD = _IOW(GPIB_CODE, 15, struct gpib_pad_ioctl),
	IBSAD = _IOW(GPIB_CODE, 16, sad_ioctl_t),
	IBTMO = _IOW(GPIB_CODE, 17, unsigned int),
	IBRSP = _IOWR(GPIB_CODE, 18, struct gpib_serial_poll_ioctl),
	IBEOS = _IOW(GPIB_CODE, 19, struct gpib_eos_ioctl),
	IBRSV = _IOW(GPIB_CODE, 20, uint8_t),
	CFCBASE = _IOW(GPIB_CODE, 21, uint64_t),
	CFCIRQ = _IOW(GPIB_CODE, 22, unsigned int),
	CFCDMA = _IOW(GPIB_CODE, 23, unsigned int),
	CFCBOARDTYPE = _IOW(GPIB_CODE, 24, struct gpib_board_type_ioctl),

	IBMUTEX = _IOW(GPIB_CODE, 26, int),
	IBSPOLL_BYTES = _IOWR(GPIB_CODE, 27, struct gpib_spoll_bytes_ioctl),
	IBPPC = _IOW(GPIB_CODE, 28, struct gpib_ppoll_config_ioctl),
	IBBOARD_INFO = _IOR(GPIB_CODE, 29, struct gpib_board_info_ioctl),

	IBQUERY_BOARD_RSV = _IOR(GPIB_CODE, 31, int),
	IBSELECT_PCI = _IOWR(GPIB_CODE, 32, struct gpib_select_pci_ioctl),
	IBEVENT = _IOR(GPIB_CODE, 33, event_ioctl_t),
	IBRSC = _IOW(GPIB_CODE, 34, rsc_ioctl_t),
	IB_T1_DELAY = _IOW(GPIB_CODE, 35, t1_delay_ioctl_t),
	IBLOC = _IO(GPIB_CODE, 36),

	IBAUTOSPOLL = _IOW(GPIB_CODE, 38, autospoll_ioctl_t),
	IBONL = _IOW(GPIB_CODE, 39, struct gpib_online_ioctl),
	IBPP2_SET = _IOW(GPIB_CODE, 40, local_ppoll_mode_ioctl_t),
	IBPP2_GET = _IOR(GPIB_CODE, 41, local_ppoll_mode_ioctl_t),
	IBSELECT_DEVICE_PATH = _IOW(GPIB_CODE, 43, select_device_path_ioctl_t),
	// 44 was IBSELECT_SERIAL_NUMBER
	IBRSV2 = _IOW(GPIB_CODE, 45, request_service2_t)
};

#endif	/* _GPIB_IOCTL_H */

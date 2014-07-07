/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __GUESTLINUXDEBUG_H__
#define __GUESTLINUXDEBUG_H__

/*
* This file contains supporting interface for "vmcallinterface.h", particularly
* regarding adding additional structure and functionality to linux
* ISSUE_IO_VMCALL_POSTCODE_SEVERITY */


/******* INFO ON ISSUE_POSTCODE_LINUX() BELOW *******/
#include "vmcallinterface.h"
typedef enum {		/* POSTCODE driver identifier tuples */
	/* visorchipset driver files */
	VISOR_CHIPSET_PC = 0xA0,
	VISOR_CHIPSET_PC_controlvm_c = 0xA1,
	VISOR_CHIPSET_PC_controlvm_cm2 = 0xA2,
	VISOR_CHIPSET_PC_controlvm_direct_c = 0xA3,
	VISOR_CHIPSET_PC_file_c = 0xA4,
	VISOR_CHIPSET_PC_parser_c = 0xA5,
	VISOR_CHIPSET_PC_testing_c = 0xA6,
	VISOR_CHIPSET_PC_visorchipset_main_c = 0xA7,
	VISOR_CHIPSET_PC_visorswitchbus_c = 0xA8,
	/* visorbus driver files */
	VISOR_BUS_PC = 0xB0,
	VISOR_BUS_PC_businst_attr_c = 0xB1,
	VISOR_BUS_PC_channel_attr_c = 0xB2,
	VISOR_BUS_PC_devmajorminor_attr_c = 0xB3,
	VISOR_BUS_PC_visorbus_main_c = 0xB4,
	/* visorclientbus driver files */
	VISOR_CLIENT_BUS_PC = 0xC0,
	VISOR_CLIENT_BUS_PC_visorclientbus_main_c = 0xC1,
	/* virt hba driver files */
	VIRT_HBA_PC = 0xC2,
	VIRT_HBA_PC_virthba_c = 0xC3,
	/* virtpci driver files */
	VIRT_PCI_PC = 0xC4,
	VIRT_PCI_PC_virtpci_c = 0xC5,
	/* virtnic driver files */
	VIRT_NIC_PC = 0xC6,
	VIRT_NIC_P_virtnic_c = 0xC7,
	/* uislib driver files */
	UISLIB_PC = 0xD0,
	UISLIB_PC_uislib_c = 0xD1,
	UISLIB_PC_uisqueue_c = 0xD2,
	UISLIB_PC_uisthread_c = 0xD3,
	UISLIB_PC_uisutils_c = 0xD4,
} DRIVER_PC;

typedef enum {			/* POSTCODE event identifier tuples */
	ATTACH_PORT_ENTRY_PC = 0x001,
	ATTACH_PORT_FAILURE_PC = 0x002,
	ATTACH_PORT_SUCCESS_PC = 0x003,
	BUS_FAILURE_PC = 0x004,
	BUS_CREATE_ENTRY_PC = 0x005,
	BUS_CREATE_FAILURE_PC = 0x006,
	BUS_CREATE_EXIT_PC = 0x007,
	BUS_CONFIGURE_ENTRY_PC = 0x008,
	BUS_CONFIGURE_FAILURE_PC = 0x009,
	BUS_CONFIGURE_EXIT_PC = 0x00A,
	CHIPSET_INIT_ENTRY_PC = 0x00B,
	CHIPSET_INIT_SUCCESS_PC = 0x00C,
	CHIPSET_INIT_FAILURE_PC = 0x00D,
	CHIPSET_INIT_EXIT_PC = 0x00E,
	CREATE_WORKQUEUE_PC = 0x00F,
	CREATE_WORKQUEUE_FAILED_PC = 0x0A0,
	CONTROLVM_INIT_FAILURE_PC = 0x0A1,
	DEVICE_CREATE_ENTRY_PC = 0x0A2,
	DEVICE_CREATE_FAILURE_PC = 0x0A3,
	DEVICE_CREATE_SUCCESS_PC = 0x0A4,
	DEVICE_CREATE_EXIT_PC = 0x0A5,
	DEVICE_ADD_PC = 0x0A6,
	DEVICE_REGISTER_FAILURE_PC = 0x0A7,
	DEVICE_CHANGESTATE_ENTRY_PC = 0x0A8,
	DEVICE_CHANGESTATE_FAILURE_PC = 0x0A9,
	DEVICE_CHANGESTATE_EXIT_PC = 0x0AA,
	DRIVER_ENTRY_PC = 0x0AB,
	DRIVER_EXIT_PC = 0x0AC,
	MALLOC_FAILURE_PC = 0x0AD,
	QUEUE_DELAYED_WORK_PC = 0x0AE,
	UISLIB_THREAD_FAILURE_PC = 0x0B7,
	VBUS_CHANNEL_ENTRY_PC = 0x0B8,
	VBUS_CHANNEL_FAILURE_PC = 0x0B9,
	VBUS_CHANNEL_EXIT_PC = 0x0BA,
	VHBA_CREATE_ENTRY_PC = 0x0BB,
	VHBA_CREATE_FAILURE_PC = 0x0BC,
	VHBA_CREATE_EXIT_PC = 0x0BD,
	VHBA_CREATE_SUCCESS_PC = 0x0BE,
	VHBA_COMMAND_HANDLER_PC = 0x0BF,
	VHBA_PROBE_ENTRY_PC = 0x0C0,
	VHBA_PROBE_FAILURE_PC = 0x0C1,
	VHBA_PROBE_EXIT_PC = 0x0C2,
	VNIC_CREATE_ENTRY_PC = 0x0C3,
	VNIC_CREATE_FAILURE_PC = 0x0C4,
	VNIC_CREATE_SUCCESS_PC = 0x0C5,
	VNIC_PROBE_ENTRY_PC = 0x0C6,
	VNIC_PROBE_FAILURE_PC = 0x0C7,
	VNIC_PROBE_EXIT_PC = 0x0C8,
	VPCI_CREATE_ENTRY_PC = 0x0C9,
	VPCI_CREATE_FAILURE_PC = 0x0CA,
	VPCI_CREATE_EXIT_PC = 0x0CB,
	VPCI_PROBE_ENTRY_PC = 0x0CC,
	VPCI_PROBE_FAILURE_PC = 0x0CD,
	VPCI_PROBE_EXIT_PC = 0x0CE,
	CRASH_DEV_ENTRY_PC = 0x0CF,
	CRASH_DEV_EXIT_PC = 0x0D0,
	CRASH_DEV_HADDR_NULL = 0x0D1,
	CRASH_DEV_CONTROLVM_NULL = 0x0D2,
	CRASH_DEV_RD_BUS_FAIULRE_PC = 0x0D3,
	CRASH_DEV_RD_DEV_FAIULRE_PC = 0x0D4,
	CRASH_DEV_BUS_NULL_FAILURE_PC = 0x0D5,
	CRASH_DEV_DEV_NULL_FAILURE_PC = 0x0D6,
	CRASH_DEV_CTRL_RD_FAILURE_PC = 0x0D7,
	CRASH_DEV_COUNT_FAILURE_PC = 0x0D8,
	SAVE_MSG_BUS_FAILURE_PC = 0x0D9,
	SAVE_MSG_DEV_FAILURE_PC = 0x0DA,
	CALLHOME_INIT_FAILURE_PC = 0x0DB
} EVENT_PC;

#ifdef __GNUC__

#define POSTCODE_SEVERITY_ERR DIAG_SEVERITY_ERR
#define POSTCODE_SEVERITY_WARNING DIAG_SEVERITY_WARNING
#define POSTCODE_SEVERITY_INFO DIAG_SEVERITY_PRINT	/* TODO-> Info currently
							 * doesnt show, so we
							 * set info=warning */
/* example call of POSTCODE_LINUX_2(VISOR_CHIPSET_PC, POSTCODE_SEVERITY_ERR);
 * Please also note that the resulting postcode is in hex, so if you are
 * searching for the __LINE__ number, convert it first to decimal.  The line
 * number combined with driver and type of call, will allow you to track down
 * exactly what line an error occurred on, or where the last driver
 * entered/exited from.
 */

/* BASE FUNCTIONS */
#define POSTCODE_LINUX_A(DRIVER_PC, EVENT_PC, pc32bit, severity)	\
do {									\
	unsigned long long post_code_temp;				\
	post_code_temp = (((U64)DRIVER_PC) << 56) | (((U64)EVENT_PC) << 44) | \
		((((U64)__LINE__) & 0xFFF) << 32) |			\
		(((U64)pc32bit) & 0xFFFFFFFF);				\
	ISSUE_IO_VMCALL_POSTCODE_SEVERITY(post_code_temp, severity);	\
} while (0)

#define POSTCODE_LINUX_B(DRIVER_PC, EVENT_PC, pc16bit1, pc16bit2, severity) \
do {									\
	unsigned long long post_code_temp;				\
	post_code_temp = (((U64)DRIVER_PC) << 56) | (((U64)EVENT_PC) << 44) | \
		((((U64)__LINE__) & 0xFFF) << 32) |			\
		((((U64)pc16bit1) & 0xFFFF) << 16) |			\
		(((U64)pc16bit2) & 0xFFFF);				\
	ISSUE_IO_VMCALL_POSTCODE_SEVERITY(post_code_temp, severity);	\
} while (0)

/* MOST COMMON */
#define POSTCODE_LINUX_2(EVENT_PC, severity)				\
	POSTCODE_LINUX_A(CURRENT_FILE_PC, EVENT_PC, 0x0000, severity);

#define POSTCODE_LINUX_3(EVENT_PC, pc32bit, severity)			\
	POSTCODE_LINUX_A(CURRENT_FILE_PC, EVENT_PC, pc32bit, severity);


#define POSTCODE_LINUX_4(EVENT_PC, pc16bit1, pc16bit2, severity)	\
	POSTCODE_LINUX_B(CURRENT_FILE_PC, EVENT_PC, pc16bit1,		\
			 pc16bit2, severity);

#endif
#endif

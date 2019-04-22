/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * EFI Test driver Header
 *
 * Copyright(C) 2012-2016 Canonical Ltd.
 *
 */

#ifndef _DRIVERS_FIRMWARE_EFI_TEST_H_
#define _DRIVERS_FIRMWARE_EFI_TEST_H_

#include <linux/efi.h>

struct efi_getvariable {
	efi_char16_t	*variable_name;
	efi_guid_t	*vendor_guid;
	u32		*attributes;
	unsigned long	*data_size;
	void		*data;
	efi_status_t	*status;
} __packed;

struct efi_setvariable {
	efi_char16_t	*variable_name;
	efi_guid_t	*vendor_guid;
	u32		attributes;
	unsigned long	data_size;
	void		*data;
	efi_status_t	*status;
} __packed;

struct efi_getnextvariablename {
	unsigned long	*variable_name_size;
	efi_char16_t	*variable_name;
	efi_guid_t	*vendor_guid;
	efi_status_t	*status;
} __packed;

struct efi_queryvariableinfo {
	u32		attributes;
	u64		*maximum_variable_storage_size;
	u64		*remaining_variable_storage_size;
	u64		*maximum_variable_size;
	efi_status_t	*status;
} __packed;

struct efi_gettime {
	efi_time_t	*time;
	efi_time_cap_t	*capabilities;
	efi_status_t	*status;
} __packed;

struct efi_settime {
	efi_time_t	*time;
	efi_status_t	*status;
} __packed;

struct efi_getwakeuptime {
	efi_bool_t	*enabled;
	efi_bool_t	*pending;
	efi_time_t	*time;
	efi_status_t	*status;
} __packed;

struct efi_setwakeuptime {
	efi_bool_t	enabled;
	efi_time_t	*time;
	efi_status_t	*status;
} __packed;

struct efi_getnexthighmonotoniccount {
	u32		*high_count;
	efi_status_t	*status;
} __packed;

struct efi_querycapsulecapabilities {
	efi_capsule_header_t	**capsule_header_array;
	unsigned long		capsule_count;
	u64			*maximum_capsule_size;
	int			*reset_type;
	efi_status_t		*status;
} __packed;

struct efi_resetsystem {
	int			reset_type;
	efi_status_t		status;
	unsigned long		data_size;
	efi_char16_t		*data;
} __packed;

#define EFI_RUNTIME_GET_VARIABLE \
	_IOWR('p', 0x01, struct efi_getvariable)
#define EFI_RUNTIME_SET_VARIABLE \
	_IOW('p', 0x02, struct efi_setvariable)

#define EFI_RUNTIME_GET_TIME \
	_IOR('p', 0x03, struct efi_gettime)
#define EFI_RUNTIME_SET_TIME \
	_IOW('p', 0x04, struct efi_settime)

#define EFI_RUNTIME_GET_WAKETIME \
	_IOR('p', 0x05, struct efi_getwakeuptime)
#define EFI_RUNTIME_SET_WAKETIME \
	_IOW('p', 0x06, struct efi_setwakeuptime)

#define EFI_RUNTIME_GET_NEXTVARIABLENAME \
	_IOWR('p', 0x07, struct efi_getnextvariablename)

#define EFI_RUNTIME_QUERY_VARIABLEINFO \
	_IOR('p', 0x08, struct efi_queryvariableinfo)

#define EFI_RUNTIME_GET_NEXTHIGHMONOTONICCOUNT \
	_IOR('p', 0x09, struct efi_getnexthighmonotoniccount)

#define EFI_RUNTIME_QUERY_CAPSULECAPABILITIES \
	_IOR('p', 0x0A, struct efi_querycapsulecapabilities)

#define EFI_RUNTIME_RESET_SYSTEM \
	_IOW('p', 0x0B, struct efi_resetsystem)

#endif /* _DRIVERS_FIRMWARE_EFI_TEST_H_ */

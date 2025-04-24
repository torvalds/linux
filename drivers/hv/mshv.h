/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Microsoft Corporation.
 */

#ifndef _MSHV_H_
#define _MSHV_H_

#include <linux/stddef.h>
#include <linux/string.h>
#include <hyperv/hvhdk.h>

#define mshv_field_nonzero(STRUCT, MEMBER) \
	memchr_inv(&((STRUCT).MEMBER), \
		   0, sizeof_field(typeof(STRUCT), MEMBER))

int hv_call_get_vp_registers(u32 vp_index, u64 partition_id, u16 count,
			     union hv_input_vtl input_vtl,
			     struct hv_register_assoc *registers);

int hv_call_set_vp_registers(u32 vp_index, u64 partition_id, u16 count,
			     union hv_input_vtl input_vtl,
			     struct hv_register_assoc *registers);

int hv_call_get_partition_property(u64 partition_id, u64 property_code,
				   u64 *property_value);

int mshv_do_pre_guest_mode_work(ulong th_flags);

#endif /* _MSHV_H */

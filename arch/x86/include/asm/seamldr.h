/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SEAMLDR_H
#define _ASM_X86_SEAMLDR_H

#include <linux/types.h>

/*
 * This is the "SEAMLDR_INFO" data structure defined in the
 * "SEAM Loader (SEAMLDR) Interface Specification".
 *
 * Must be aligned to a 256-byte boundary.
 */
struct seamldr_info {
	u32	version;
	u32	attributes;
	u32	vendor_id;
	u32	build_date;
	u16	build_num;
	u16	minor_version;
	u16	major_version;
	u16	update_version;
	u32	acm_x2apicid;
	u32	num_remaining_updates;
	u8	seam_info[128];
	u8	seam_ready;
	u8	seam_debug;
	u8	p_seam_ready;
	u8	reserved[93];
} __packed __aligned(256);

static_assert(sizeof(struct seamldr_info) == 256);

int seamldr_get_info(struct seamldr_info *seamldr_info);
int seamldr_install_module(const u8 *data, u32 data_len);
void seamldr_lock_module_update(void);
void seamldr_unlock_module_update(void);

#endif /* _ASM_X86_SEAMLDR_H */

// SPDX-License-Identifier: GPL-2.0
#include <asm/uv.h>
#include <asm/boot_data.h>
#include <asm/facility.h>
#include <asm/sections.h>

#include "boot.h"
#include "uv.h"

/* will be used in arch/s390/kernel/uv.c */
int __bootdata_preserved(prot_virt_guest);
int __bootdata_preserved(prot_virt_host);
struct uv_info __bootdata_preserved(uv_info);

void uv_query_info(void)
{
	struct uv_cb_qui uvcb = {
		.header.cmd = UVC_CMD_QUI,
		.header.len = sizeof(uvcb)
	};

	if (!test_facility(158))
		return;

	/* Ignore that there might be more data we do not process */
	if (uv_call(0, (uint64_t)&uvcb) && uvcb.header.rc != UVC_RC_MORE_DATA)
		return;

	if (IS_ENABLED(CONFIG_KVM)) {
		memcpy(uv_info.inst_calls_list, uvcb.inst_calls_list, sizeof(uv_info.inst_calls_list));
		uv_info.uv_base_stor_len = uvcb.uv_base_stor_len;
		uv_info.guest_base_stor_len = uvcb.conf_base_phys_stor_len;
		uv_info.guest_virt_base_stor_len = uvcb.conf_base_virt_stor_len;
		uv_info.guest_virt_var_stor_len = uvcb.conf_virt_var_stor_len;
		uv_info.guest_cpu_stor_len = uvcb.cpu_stor_len;
		uv_info.max_sec_stor_addr = ALIGN(uvcb.max_guest_stor_addr, PAGE_SIZE);
		uv_info.max_num_sec_conf = uvcb.max_num_sec_conf;
		uv_info.max_guest_cpu_id = uvcb.max_guest_cpu_id;
		uv_info.uv_feature_indications = uvcb.uv_feature_indications;
		uv_info.supp_se_hdr_ver = uvcb.supp_se_hdr_versions;
		uv_info.supp_se_hdr_pcf = uvcb.supp_se_hdr_pcf;
		uv_info.conf_dump_storage_state_len = uvcb.conf_dump_storage_state_len;
		uv_info.conf_dump_finalize_len = uvcb.conf_dump_finalize_len;
		uv_info.supp_att_req_hdr_ver = uvcb.supp_att_req_hdr_ver;
		uv_info.supp_att_pflags = uvcb.supp_att_pflags;
		uv_info.supp_add_secret_req_ver = uvcb.supp_add_secret_req_ver;
		uv_info.supp_add_secret_pcf = uvcb.supp_add_secret_pcf;
		uv_info.supp_secret_types = uvcb.supp_secret_types;
		uv_info.max_assoc_secrets = uvcb.max_assoc_secrets;
		uv_info.max_retr_secrets = uvcb.max_retr_secrets;
	}

	if (test_bit_inv(BIT_UVC_CMD_SET_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list) &&
	    test_bit_inv(BIT_UVC_CMD_REMOVE_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list))
		prot_virt_guest = 1;
}

unsigned long adjust_to_uv_max(unsigned long limit)
{
	if (is_prot_virt_host() && uv_info.max_sec_stor_addr)
		limit = min_t(unsigned long, limit, uv_info.max_sec_stor_addr);
	return limit;
}

static int is_prot_virt_host_capable(void)
{
	/* disable if no prot_virt=1 given on command-line */
	if (!is_prot_virt_host())
		return 0;
	/* disable if protected guest virtualization is enabled */
	if (is_prot_virt_guest())
		return 0;
	/* disable if no hardware support */
	if (!test_facility(158))
		return 0;
	/* disable if kdump */
	if (oldmem_data.start)
		return 0;
	/* disable if stand-alone dump */
	if (ipl_block_valid && is_ipl_block_dump())
		return 0;
	return 1;
}

void sanitize_prot_virt_host(void)
{
	prot_virt_host = is_prot_virt_host_capable();
}

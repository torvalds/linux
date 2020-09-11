// SPDX-License-Identifier: GPL-2.0
#include <asm/uv.h>
#include <asm/facility.h>
#include <asm/sections.h>

/* will be used in arch/s390/kernel/uv.c */
#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
int __bootdata_preserved(prot_virt_guest);
#endif
#if IS_ENABLED(CONFIG_KVM)
int __bootdata_preserved(prot_virt_host);
#endif
struct uv_info __bootdata_preserved(uv_info);

void uv_query_info(void)
{
	struct uv_cb_qui uvcb = {
		.header.cmd = UVC_CMD_QUI,
		.header.len = sizeof(uvcb)
	};

	if (!test_facility(158))
		return;

	/* rc==0x100 means that there is additional data we do not process */
	if (uv_call(0, (uint64_t)&uvcb) && uvcb.header.rc != 0x100)
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
		uv_info.max_guest_cpus = uvcb.max_guest_cpus;
	}

#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
	if (test_bit_inv(BIT_UVC_CMD_SET_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list) &&
	    test_bit_inv(BIT_UVC_CMD_REMOVE_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list))
		prot_virt_guest = 1;
#endif
}

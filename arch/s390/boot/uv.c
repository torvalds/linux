// SPDX-License-Identifier: GPL-2.0
#include <asm/uv.h>
#include <asm/facility.h>
#include <asm/sections.h>

int __bootdata_preserved(prot_virt_guest);

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

	if (test_bit_inv(BIT_UVC_CMD_SET_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list) &&
	    test_bit_inv(BIT_UVC_CMD_REMOVE_SHARED_ACCESS, (unsigned long *)uvcb.inst_calls_list))
		prot_virt_guest = 1;
}

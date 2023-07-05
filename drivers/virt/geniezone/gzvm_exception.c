// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/device.h>
#include <linux/gzvm_drv.h>

/**
 * gzvm_handle_guest_exception() - Handle guest exception
 * @vcpu: Pointer to struct gzvm_vcpu_run in userspace
 * Return:
 * * true - This exception has been processed, no need to back to VMM.
 * * false - This exception has not been processed, require userspace.
 */
bool gzvm_handle_guest_exception(struct gzvm_vcpu *vcpu)
{
	int ret;

	for (int i = 0; i < ARRAY_SIZE(vcpu->run->exception.reserved); i++) {
		if (vcpu->run->exception.reserved[i])
			return -EINVAL;
	}

	switch (vcpu->run->exception.exception) {
	case GZVM_EXCEPTION_PAGE_FAULT:
		ret = gzvm_handle_page_fault(vcpu);
		break;
	case GZVM_EXCEPTION_UNKNOWN:
		fallthrough;
	default:
		ret = -EFAULT;
	}

	if (!ret)
		return true;
	else
		return false;
}

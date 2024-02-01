// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Virtual PTP 1588 clock for use with KVM guests
 *  Copyright (C) 2019 ARM Ltd.
 *  All Rights Reserved
 */

#include <linux/arm-smccc.h>
#include <linux/ptp_kvm.h>

#include <asm/arch_timer.h>
#include <asm/hypervisor.h>

int kvm_arch_ptp_init(void)
{
	int ret;

	ret = kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_PTP);
	if (ret <= 0)
		return -EOPNOTSUPP;

	return 0;
}

void kvm_arch_ptp_exit(void)
{
}

int kvm_arch_ptp_get_clock(struct timespec64 *ts)
{
	return kvm_arch_ptp_get_crosststamp(NULL, ts, NULL);
}

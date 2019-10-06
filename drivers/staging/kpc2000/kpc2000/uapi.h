/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef KP2000_CDEV_UAPI_H_
#define KP2000_CDEV_UAPI_H_
#include <linux/types.h>
#include <linux/ioctl.h>

struct kp2000_regs {
	__u32 card_id;
	__u32 build_version;
	__u32 build_datestamp;
	__u32 build_timestamp;
	__u32 hw_rev;
	__u64 ssid;
	__u64 ddna;
	__u64 cpld_reg;
};

#define KP2000_IOCTL_GET_CPLD_REG		_IOR('k', 9, __u32)
#define KP2000_IOCTL_GET_PCIE_ERROR_REG		_IOR('k', 11, __u32)
#define KP2000_IOCTL_GET_EVERYTHING		_IOR('k', 8, struct kp2000_regs*)

#endif /* KP2000_CDEV_UAPI_H_ */

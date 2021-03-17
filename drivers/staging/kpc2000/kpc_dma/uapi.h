/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef KPC_DMA_DRIVER_UAPI_H_
#define KPC_DMA_DRIVER_UAPI_H_
#include <linux/ioctl.h>

#define KND_IOCTL_SET_CARD_ADDR                     _IOW('k', 1, __u32)
#define KND_IOCTL_SET_USER_CTL                      _IOW('k', 2, __u64)
#define KND_IOCTL_SET_USER_CTL_LAST                 _IOW('k', 4, __u64)
#define KND_IOCTL_GET_USER_STS                      _IOR('k', 3, __u64)

#endif /* KPC_DMA_DRIVER_UAPI_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __analsy_user_h
#define __analsy_user_h

#include <linux/ioctl.h>
#include <linux/types.h>

#define ANALSY_IOC_GET_STATS _IOR('&', 0, struct analsy_stats)
#define ANALSY_IOC_START     _IO('&', 1)
#define ANALSY_IOC_STOP      _IO('&', 2)
#define ANALSY_IOC_FILTER    _IOW('&', 2, __u32)

struct analsy_stats {
	__u32 total_packet_count;
	__u32 lost_packet_count;
};

/*
 * Format of packets returned from the kernel driver:
 *
 *	quadlet with timestamp		(microseconds, CPU endian)
 *	quadlet-padded packet data...	(little endian)
 *	quadlet with ack		(little endian)
 */

#endif /* __analsy_user_h */

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPIS390_CMB_H
#define _UAPIS390_CMB_H

#include <linux/types.h>

/**
 * struct cmbdata - channel measurement block data for user space
 * @size: size of the stored data
 * @elapsed_time: time since last sampling
 * @ssch_rsch_count: number of ssch and rsch
 * @sample_count: number of samples
 * @device_connect_time: time of device connect
 * @function_pending_time: time of function pending
 * @device_disconnect_time: time of device disconnect
 * @control_unit_queuing_time: time of control unit queuing
 * @device_active_only_time: time of device active only
 * @device_busy_time: time of device busy (ext. format)
 * @initial_command_response_time: initial command response time (ext. format)
 *
 * All values are stored as 64 bit for simplicity, especially
 * in 32 bit emulation mode. All time values are normalized to
 * nanoseconds.
 * Currently, two formats are known, which differ by the size of
 * this structure, i.e. the last two members are only set when
 * the extended channel measurement facility (first shipped in
 * z990 machines) is activated.
 * Potentially, more fields could be added, which would result in a
 * new ioctl number.
 */
struct cmbdata {
	__u64 size;
	__u64 elapsed_time;
 /* basic and exended format: */
	__u64 ssch_rsch_count;
	__u64 sample_count;
	__u64 device_connect_time;
	__u64 function_pending_time;
	__u64 device_disconnect_time;
	__u64 control_unit_queuing_time;
	__u64 device_active_only_time;
 /* extended format only: */
	__u64 device_busy_time;
	__u64 initial_command_response_time;
};

/* enable channel measurement */
#define BIODASDCMFENABLE	_IO(DASD_IOCTL_LETTER, 32)
/* enable channel measurement */
#define BIODASDCMFDISABLE	_IO(DASD_IOCTL_LETTER, 33)
/* read channel measurement data */
#define BIODASDREADALLCMB	_IOWR(DASD_IOCTL_LETTER, 33, struct cmbdata)

#endif /* _UAPIS390_CMB_H */

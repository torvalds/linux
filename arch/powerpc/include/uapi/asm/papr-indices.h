/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_INDICES_H_
#define _UAPI_PAPR_INDICES_H_

#include <linux/types.h>
#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

#define LOC_CODE_SIZE			80
#define RTAS_GET_INDICES_BUF_SIZE	SZ_4K

struct papr_indices_io_block {
	union {
		struct {
			__u8 is_sensor; /* 0 for indicator and 1 for sensor */
			__u32 indice_type;
		} indices;
		struct {
			__u32 token; /* Sensor or indicator token */
			__u32 state; /* get / set state */
			/*
			 * PAPR+ 12.3.2.4 Converged Location Code Rules - Length
			 * Restrictions. 79 characters plus null.
			 */
			char location_code_str[LOC_CODE_SIZE]; /* location code */
		} dynamic_param;
	};
};

/*
 * ioctls for /dev/papr-indices.
 * PAPR_INDICES_IOC_GET: Returns a get-indices handle fd to read data
 * PAPR_DYNAMIC_SENSOR_IOC_GET: Gets the state of the input sensor
 * PAPR_DYNAMIC_INDICATOR_IOC_SET: Sets the new state for the input indicator
 */
#define PAPR_INDICES_IOC_GET		_IOW(PAPR_MISCDEV_IOC_ID, 3, struct papr_indices_io_block)
#define PAPR_DYNAMIC_SENSOR_IOC_GET	_IOWR(PAPR_MISCDEV_IOC_ID, 4, struct papr_indices_io_block)
#define PAPR_DYNAMIC_INDICATOR_IOC_SET	_IOW(PAPR_MISCDEV_IOC_ID, 5, struct papr_indices_io_block)


#endif /* _UAPI_PAPR_INDICES_H_ */

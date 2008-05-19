#ifndef __smscharioctl_h__
#define __smscharioctl_h__

#include <linux/ioctl.h>

typedef struct _smschar_buffer_t
{
	unsigned long offset;		// offset in common buffer (mapped to user space)
	int size;
} smschar_buffer_t;

#define SMSCHAR_SET_DEVICE_MODE	_IOW('K', 0, int)
#define SMSCHAR_GET_DEVICE_MODE	_IOR('K', 1, int)
#define SMSCHAR_GET_BUFFER_SIZE	_IOR('K', 2, int)
#define SMSCHAR_WAIT_GET_BUFFER	_IOR('K', 3, smschar_buffer_t)

#endif // __smscharioctl_h__

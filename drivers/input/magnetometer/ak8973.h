/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for ak8973 compass chip.
 */
#ifndef AKM8973_H
#define AKM8973_H

#include <linux/ioctl.h>

#define AKM8973_I2C_NAME "ak8973"

/* Compass device dependent definition */
#define AKECS_MODE_MEASURE	0x00	/* Starts measurement. Please use AKECS_MODE_MEASURE_SNG */
					/* or AKECS_MODE_MEASURE_SEQ instead of this. */
#define AKECS_MODE_E2P_READ	0x02	/* E2P access mode (read). */
#define AKECS_MODE_POWERDOWN	0x03	/* Power down mode */

#define RBUFF_SIZE		4	/* Rx buffer size */

/* AK8973 register address */
#define AKECS_REG_ST			0xC0
#define AKECS_REG_TMPS			0xC1
#define AKECS_REG_MS1			0xE0

#define AKMIO				0xA1

/* IOCTLs for AKM library */
#define ECS_IOCTL_WRITE                 _IOW(AKMIO, 0x01, char[5])
#define ECS_IOCTL_READ                  _IOWR(AKMIO, 0x02, char[5])
#define ECS_IOCTL_RESET      	          _IO(AKMIO, 0x03)
#define ECS_IOCTL_SET_MODE              _IOW(AKMIO, 0x04, short)
#define ECS_IOCTL_GETDATA               _IOR(AKMIO, 0x05, char[RBUFF_SIZE+1])
#define ECS_IOCTL_SET_YPR               _IOW(AKMIO, 0x06, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS       _IOR(AKMIO, 0x07, int)
#define ECS_IOCTL_GET_CLOSE_STATUS      _IOR(AKMIO, 0x08, int)
#define ECS_IOCTL_GET_DELAY             _IOR(AKMIO, 0x30, short)

/* IOCTLs for APPs */
#define ECS_IOCTL_APP_SET_MODE		_IOW(AKMIO, 0x10, short)
#define ECS_IOCTL_APP_SET_MFLAG		_IOW(AKMIO, 0x11, short)
#define ECS_IOCTL_APP_GET_MFLAG		_IOW(AKMIO, 0x12, short)
#define ECS_IOCTL_APP_SET_AFLAG		_IOW(AKMIO, 0x13, short)
#define ECS_IOCTL_APP_GET_AFLAG		_IOR(AKMIO, 0x14, short)
#define ECS_IOCTL_APP_SET_TFLAG		_IOR(AKMIO, 0x15, short)
#define ECS_IOCTL_APP_GET_TFLAG		_IOR(AKMIO, 0x16, short)
#define ECS_IOCTL_APP_RESET_PEDOMETER   _IO(AKMIO, 0x17)
#define ECS_IOCTL_APP_SET_DELAY		_IOW(AKMIO, 0x18, short)
#define ECS_IOCTL_APP_GET_DELAY		ECS_IOCTL_GET_DELAY
#define ECS_IOCTL_APP_SET_MVFLAG	_IOW(AKMIO, 0x19, short)	/* Set raw magnetic vector flag */
#define ECS_IOCTL_APP_GET_MVFLAG	_IOR(AKMIO, 0x1A, short)	/* Get raw magnetic vector flag */


#endif


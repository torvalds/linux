/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Definitions for talking to the CUDA.  The CUDA is a microcontroller
 * which controls the ADB, system power, RTC, and various other things.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

#ifndef _LINUX_CUDA_H
#define _LINUX_CUDA_H

/* CUDA commands (2nd byte) */
#define CUDA_WARM_START		0
#define CUDA_AUTOPOLL		1
#define CUDA_GET_6805_ADDR	2
#define CUDA_GET_TIME		3
#define CUDA_GET_PRAM		7
#define CUDA_SET_6805_ADDR	8
#define CUDA_SET_TIME		9
#define CUDA_POWERDOWN		0xa
#define CUDA_POWERUP_TIME	0xb
#define CUDA_SET_PRAM		0xc
#define CUDA_MS_RESET		0xd
#define CUDA_SEND_DFAC		0xe
#define CUDA_RESET_SYSTEM	0x11
#define CUDA_SET_IPL		0x12
#define CUDA_SET_AUTO_RATE	0x14
#define CUDA_GET_AUTO_RATE	0x16
#define CUDA_SET_DEVICE_LIST	0x19
#define CUDA_GET_DEVICE_LIST	0x1a
#define CUDA_GET_SET_IIC	0x22


#endif /* _LINUX_CUDA_H */

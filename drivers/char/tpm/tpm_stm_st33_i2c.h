/*
 * STMicroelectronics TPM I2C Linux driver for TPM ST33ZP24
 * Copyright (C) 2009, 2010  STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * STMicroelectronics version 1.2.0, Copyright (C) 2010
 * STMicroelectronics comes with ABSOLUTELY NO WARRANTY.
 * This is free software, and you are welcome to redistribute it
 * under certain conditions.
 *
 * @Author: Christophe RICARD tpmsupport@st.com
 *
 * @File: stm_st33_tpm_i2c.h
 *
 * @Date: 09/15/2010
 */
#ifndef __STM_ST33_TPM_I2C_MAIN_H__
#define __STM_ST33_TPM_I2C_MAIN_H__

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "tpm.h"

#define MINOR_NUM_I2C		224

#define TPM_ACCESS			(0x0)
#define TPM_STS				(0x18)
#define TPM_HASH_END			(0x20)
#define TPM_DATA_FIFO			(0x24)
#define TPM_HASH_DATA			(0x24)
#define TPM_HASH_START			(0x28)
#define TPM_INTF_CAPABILITY		(0x14)
#define TPM_INT_STATUS			(0x10)
#define TPM_INT_ENABLE			(0x08)

#define TPM_DUMMY_BYTE			0xAA
#define TPM_WRITE_DIRECTION		0x80
#define TPM_HEADER_SIZE			10
#define TPM_BUFSIZE			2048

#define LOCALITY0		0

struct st_tpm_hash {
	int size;
	u8 *data;
};

#endif /* __STM_ST33_TPM_I2C_MAIN_H__ */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_PWR_OS_H__
#define __NVKM_PWR_OS_H__

/* Process names */
#define PROC_KERN 0x52544e49
#define PROC_IDLE 0x454c4449
#define PROC_HOST 0x54534f48
#define PROC_MEMX 0x584d454d
#define PROC_PERF 0x46524550
#define PROC_I2C_ 0x5f433249
#define PROC_TEST 0x54534554

/* KERN: message identifiers */
#define KMSG_FIFO   0x00000000
#define KMSG_ALARM  0x00000001

/* MEMX: message identifiers */
#define MEMX_MSG_INFO 0
#define MEMX_MSG_EXEC 1

/* MEMX: info types */
#define MEMX_INFO_DATA  0
#define MEMX_INFO_TRAIN 1

/* MEMX: script opcode definitions */
#define MEMX_ENTER  1
#define MEMX_LEAVE  2
#define MEMX_WR32   3
#define MEMX_WAIT   4
#define MEMX_DELAY  5
#define MEMX_VBLANK 6
#define MEMX_TRAIN  7

/* I2C_: message identifiers */
#define I2C__MSG_RD08 0
#define I2C__MSG_WR08 1

#define I2C__MSG_DATA0_PORT 24:31
#define I2C__MSG_DATA0_ADDR 14:23

#define I2C__MSG_DATA0_RD08_PORT I2C__MSG_DATA0_PORT
#define I2C__MSG_DATA0_RD08_ADDR I2C__MSG_DATA0_ADDR
#define I2C__MSG_DATA0_RD08_REG 0:7
#define I2C__MSG_DATA1_RD08_VAL 0:7

#define I2C__MSG_DATA0_WR08_PORT I2C__MSG_DATA0_PORT
#define I2C__MSG_DATA0_WR08_ADDR I2C__MSG_DATA0_ADDR
#define I2C__MSG_DATA0_WR08_SYNC 8:8
#define I2C__MSG_DATA0_WR08_REG 0:7
#define I2C__MSG_DATA1_WR08_VAL 0:7

#endif

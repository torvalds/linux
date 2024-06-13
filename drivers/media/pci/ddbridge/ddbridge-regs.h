/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-regs.h: Digital Devices PCIe bridge driver
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 */

#ifndef __DDBRIDGE_REGS_H__
#define __DDBRIDGE_REGS_H__

/* ------------------------------------------------------------------------- */
/* SPI Controller */

#define SPI_CONTROL     0x10
#define SPI_DATA        0x14

/* ------------------------------------------------------------------------- */
/* GPIO */

#define GPIO_OUTPUT      0x20
#define GPIO_INPUT       0x24
#define GPIO_DIRECTION   0x28

/* ------------------------------------------------------------------------- */

#define BOARD_CONTROL    0x30

/* ------------------------------------------------------------------------- */

/* Interrupt controller
 * How many MSI's are available depends on HW (Min 2 max 8)
 * How many are usable also depends on Host platform
 */

#define INTERRUPT_BASE   (0x40)

#define INTERRUPT_ENABLE (INTERRUPT_BASE + 0x00)
#define MSI1_ENABLE      (INTERRUPT_BASE + 0x04)
#define MSI2_ENABLE      (INTERRUPT_BASE + 0x08)
#define MSI3_ENABLE      (INTERRUPT_BASE + 0x0C)
#define MSI4_ENABLE      (INTERRUPT_BASE + 0x10)
#define MSI5_ENABLE      (INTERRUPT_BASE + 0x14)
#define MSI6_ENABLE      (INTERRUPT_BASE + 0x18)
#define MSI7_ENABLE      (INTERRUPT_BASE + 0x1C)

#define INTERRUPT_STATUS (INTERRUPT_BASE + 0x20)
#define INTERRUPT_ACK    (INTERRUPT_BASE + 0x20)

/* Temperature Monitor ( 2x LM75A @ 0x90,0x92 I2c ) */
#define TEMPMON_BASE			(0x1c0)
#define TEMPMON_CONTROL			(TEMPMON_BASE + 0x00)

#define TEMPMON_CONTROL_AUTOSCAN	(0x00000002)
#define TEMPMON_CONTROL_INTENABLE	(0x00000004)
#define TEMPMON_CONTROL_OVERTEMP	(0x00008000)

/* SHORT Temperature in Celsius x 256 */
#define TEMPMON_SENSOR0			(TEMPMON_BASE + 0x04)
#define TEMPMON_SENSOR1			(TEMPMON_BASE + 0x08)

#define TEMPMON_FANCONTROL		(TEMPMON_BASE + 0x10)

/* ------------------------------------------------------------------------- */
/* I2C Master Controller */

#define I2C_COMMAND     (0x00)
#define I2C_TIMING      (0x04)
#define I2C_TASKLENGTH  (0x08)     /* High read, low write */
#define I2C_TASKADDRESS (0x0C)     /* High read, low write */
#define I2C_MONITOR     (0x1C)

#define I2C_SPEED_400   (0x04030404)
#define I2C_SPEED_100   (0x13121313)

/* ------------------------------------------------------------------------- */
/* DMA  Controller */

#define DMA_BASE_WRITE        (0x100)
#define DMA_BASE_READ         (0x140)

#define TS_CONTROL(_io)         ((_io)->regs + 0x00)
#define TS_CONTROL2(_io)        ((_io)->regs + 0x04)

/* ------------------------------------------------------------------------- */
/* DMA  Buffer */

#define DMA_BUFFER_CONTROL(_dma)       ((_dma)->regs + 0x00)
#define DMA_BUFFER_ACK(_dma)           ((_dma)->regs + 0x04)
#define DMA_BUFFER_CURRENT(_dma)       ((_dma)->regs + 0x08)
#define DMA_BUFFER_SIZE(_dma)          ((_dma)->regs + 0x0c)

/* ------------------------------------------------------------------------- */
/* CI Interface (only CI-Bridge) */

#define CI_BASE                         (0x400)
#define CI_CONTROL(i)                   (CI_BASE + (i) * 32 + 0x00)

#define CI_DO_ATTRIBUTE_RW(i)           (CI_BASE + (i) * 32 + 0x04)
#define CI_DO_IO_RW(i)                  (CI_BASE + (i) * 32 + 0x08)
#define CI_READDATA(i)                  (CI_BASE + (i) * 32 + 0x0c)
#define CI_DO_READ_ATTRIBUTES(i)        (CI_BASE + (i) * 32 + 0x10)

#define CI_RESET_CAM                    (0x00000001)
#define CI_POWER_ON                     (0x00000002)
#define CI_ENABLE                       (0x00000004)
#define CI_BYPASS_DISABLE               (0x00000010)

#define CI_CAM_READY                    (0x00010000)
#define CI_CAM_DETECT                   (0x00020000)
#define CI_READY                        (0x80000000)

#define CI_READ_CMD                     (0x40000000)
#define CI_WRITE_CMD                    (0x80000000)

#define CI_BUFFER_BASE                  (0x3000)
#define CI_BUFFER_SIZE                  (0x0800)

#define CI_BUFFER(i)                    (CI_BUFFER_BASE + (i) * CI_BUFFER_SIZE)

/* ------------------------------------------------------------------------- */
/* LNB commands (mxl5xx / Max S8) */

#define LNB_BASE			(0x400)
#define LNB_CONTROL(i)			(LNB_BASE + (i) * 0x20 + 0x00)

#define LNB_CMD				(7ULL << 0)
#define LNB_CMD_NOP			0
#define LNB_CMD_INIT			1
#define LNB_CMD_LOW			3
#define LNB_CMD_HIGH			4
#define LNB_CMD_OFF			5
#define LNB_CMD_DISEQC			6

#define LNB_BUSY			BIT_ULL(4)
#define LNB_TONE			BIT_ULL(15)

#define LNB_BUF_LEVEL(i)		(LNB_BASE + (i) * 0x20 + 0x10)
#define LNB_BUF_WRITE(i)		(LNB_BASE + (i) * 0x20 + 0x14)

#endif /* __DDBRIDGE_REGS_H__ */

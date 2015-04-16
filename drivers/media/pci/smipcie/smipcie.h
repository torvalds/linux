/*
 * SMI PCIe driver for DVBSky cards.
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#ifndef _SMI_PCIE_H_
#define _SMI_PCIE_H_

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <media/rc-core.h>

#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"

/* -------- Register Base -------- */
#define    MSI_CONTROL_REG_BASE                 0x0800
#define    SYSTEM_CONTROL_REG_BASE              0x0880
#define    PCIE_EP_DEBUG_REG_BASE               0x08C0
#define    IR_CONTROL_REG_BASE                  0x0900
#define    I2C_A_CONTROL_REG_BASE               0x0940
#define    I2C_B_CONTROL_REG_BASE               0x0980
#define    ATV_PORTA_CONTROL_REG_BASE           0x09C0
#define    DTV_PORTA_CONTROL_REG_BASE           0x0A00
#define    AES_PORTA_CONTROL_REG_BASE           0x0A80
#define    DMA_PORTA_CONTROL_REG_BASE           0x0AC0
#define    ATV_PORTB_CONTROL_REG_BASE           0x0B00
#define    DTV_PORTB_CONTROL_REG_BASE           0x0B40
#define    AES_PORTB_CONTROL_REG_BASE           0x0BC0
#define    DMA_PORTB_CONTROL_REG_BASE           0x0C00
#define    UART_A_REGISTER_BASE                 0x0C40
#define    UART_B_REGISTER_BASE                 0x0C80
#define    GPS_CONTROL_REG_BASE                 0x0CC0
#define    DMA_PORTC_CONTROL_REG_BASE           0x0D00
#define    DMA_PORTD_CONTROL_REG_BASE           0x0D00
#define    AES_RANDOM_DATA_BASE                 0x0D80
#define    AES_KEY_IN_BASE                      0x0D90
#define    RANDOM_DATA_LIB_BASE                 0x0E00
#define    IR_DATA_BUFFER_BASE                  0x0F00
#define    PORTA_TS_BUFFER_BASE                 0x1000
#define    PORTA_I2S_BUFFER_BASE                0x1400
#define    PORTB_TS_BUFFER_BASE                 0x1800
#define    PORTB_I2S_BUFFER_BASE                0x1C00

/* -------- MSI control and state register -------- */
#define MSI_DELAY_TIMER             (MSI_CONTROL_REG_BASE + 0x00)
#define MSI_INT_STATUS              (MSI_CONTROL_REG_BASE + 0x08)
#define MSI_INT_STATUS_CLR          (MSI_CONTROL_REG_BASE + 0x0C)
#define MSI_INT_STATUS_SET          (MSI_CONTROL_REG_BASE + 0x10)
#define MSI_INT_ENA                 (MSI_CONTROL_REG_BASE + 0x14)
#define MSI_INT_ENA_CLR             (MSI_CONTROL_REG_BASE + 0x18)
#define MSI_INT_ENA_SET             (MSI_CONTROL_REG_BASE + 0x1C)
#define MSI_SOFT_RESET              (MSI_CONTROL_REG_BASE + 0x20)
#define MSI_CFG_SRC0                (MSI_CONTROL_REG_BASE + 0x24)

/* -------- Hybird Controller System Control register -------- */
#define MUX_MODE_CTRL         (SYSTEM_CONTROL_REG_BASE + 0x00)
	#define rbPaMSMask        0x07
	#define rbPaMSDtvNoGpio   0x00 /*[2:0], DTV Simple mode */
	#define rbPaMSDtv4bitGpio 0x01 /*[2:0], DTV TS2 Serial mode)*/
	#define rbPaMSDtv7bitGpio 0x02 /*[2:0], DTV TS0 Serial mode*/
	#define rbPaMS8bitGpio    0x03 /*[2:0], GPIO mode selected;(8bit GPIO)*/
	#define rbPaMSAtv         0x04 /*[2:0], 3'b1xx: ATV mode select*/
	#define rbPbMSMask        0x38
	#define rbPbMSDtvNoGpio   0x00 /*[5:3], DTV Simple mode */
	#define rbPbMSDtv4bitGpio 0x08 /*[5:3], DTV TS2 Serial mode*/
	#define rbPbMSDtv7bitGpio 0x10 /*[5:3], DTV TS0 Serial mode*/
	#define rbPbMS8bitGpio    0x18 /*[5:3], GPIO mode selected;(8bit GPIO)*/
	#define rbPbMSAtv         0x20 /*[5:3], 3'b1xx: ATV mode select*/
	#define rbPaAESEN         0x40 /*[6], port A AES enable bit*/
	#define rbPbAESEN         0x80 /*[7], port B AES enable bit*/

#define INTERNAL_RST                (SYSTEM_CONTROL_REG_BASE + 0x04)
#define PERIPHERAL_CTRL             (SYSTEM_CONTROL_REG_BASE + 0x08)
#define GPIO_0to7_CTRL              (SYSTEM_CONTROL_REG_BASE + 0x0C)
#define GPIO_8to15_CTRL             (SYSTEM_CONTROL_REG_BASE + 0x10)
#define GPIO_16to24_CTRL            (SYSTEM_CONTROL_REG_BASE + 0x14)
#define GPIO_INT_SRC_CFG            (SYSTEM_CONTROL_REG_BASE + 0x18)
#define SYS_BUF_STATUS              (SYSTEM_CONTROL_REG_BASE + 0x1C)
#define PCIE_IP_REG_ACS             (SYSTEM_CONTROL_REG_BASE + 0x20)
#define PCIE_IP_REG_ACS_ADDR        (SYSTEM_CONTROL_REG_BASE + 0x24)
#define PCIE_IP_REG_ACS_DATA        (SYSTEM_CONTROL_REG_BASE + 0x28)

/* -------- IR Control register -------- */
#define   IR_Init_Reg         (IR_CONTROL_REG_BASE + 0x00)
#define   IR_Idle_Cnt_Low     (IR_CONTROL_REG_BASE + 0x04)
#define   IR_Idle_Cnt_High    (IR_CONTROL_REG_BASE + 0x05)
#define   IR_Unit_Cnt_Low     (IR_CONTROL_REG_BASE + 0x06)
#define   IR_Unit_Cnt_High    (IR_CONTROL_REG_BASE + 0x07)
#define   IR_Data_Cnt         (IR_CONTROL_REG_BASE + 0x08)
#define   rbIRen            0x80
#define   rbIRhighidle      0x10
#define   rbIRlowidle       0x00
#define   rbIRVld           0x04

/* -------- I2C A control and state register -------- */
#define I2C_A_CTL_STATUS                 (I2C_A_CONTROL_REG_BASE + 0x00)
#define I2C_A_ADDR                       (I2C_A_CONTROL_REG_BASE + 0x04)
#define I2C_A_SW_CTL                     (I2C_A_CONTROL_REG_BASE + 0x08)
#define I2C_A_TIME_OUT_CNT               (I2C_A_CONTROL_REG_BASE + 0x0C)
#define I2C_A_FIFO_STATUS                (I2C_A_CONTROL_REG_BASE + 0x10)
#define I2C_A_FS_EN                      (I2C_A_CONTROL_REG_BASE + 0x14)
#define I2C_A_FIFO_DATA                  (I2C_A_CONTROL_REG_BASE + 0x20)

/* -------- I2C B control and state register -------- */
#define I2C_B_CTL_STATUS                 (I2C_B_CONTROL_REG_BASE + 0x00)
#define I2C_B_ADDR                       (I2C_B_CONTROL_REG_BASE + 0x04)
#define I2C_B_SW_CTL                     (I2C_B_CONTROL_REG_BASE + 0x08)
#define I2C_B_TIME_OUT_CNT               (I2C_B_CONTROL_REG_BASE + 0x0C)
#define I2C_B_FIFO_STATUS                (I2C_B_CONTROL_REG_BASE + 0x10)
#define I2C_B_FS_EN                      (I2C_B_CONTROL_REG_BASE + 0x14)
#define I2C_B_FIFO_DATA                  (I2C_B_CONTROL_REG_BASE + 0x20)

#define VIDEO_CTRL_STATUS_A	(ATV_PORTA_CONTROL_REG_BASE + 0x04)

/* -------- Digital TV control register, Port A -------- */
#define MPEG2_CTRL_A		(DTV_PORTA_CONTROL_REG_BASE + 0x00)
#define SERIAL_IN_ADDR_A	(DTV_PORTA_CONTROL_REG_BASE + 0x4C)
#define VLD_CNT_ADDR_A		(DTV_PORTA_CONTROL_REG_BASE + 0x60)
#define ERR_CNT_ADDR_A		(DTV_PORTA_CONTROL_REG_BASE + 0x64)
#define BRD_CNT_ADDR_A		(DTV_PORTA_CONTROL_REG_BASE + 0x68)

/* -------- DMA Control Register, Port A  -------- */
#define DMA_PORTA_CHAN0_ADDR_LOW        (DMA_PORTA_CONTROL_REG_BASE + 0x00)
#define DMA_PORTA_CHAN0_ADDR_HI         (DMA_PORTA_CONTROL_REG_BASE + 0x04)
#define DMA_PORTA_CHAN0_TRANS_STATE     (DMA_PORTA_CONTROL_REG_BASE + 0x08)
#define DMA_PORTA_CHAN0_CONTROL         (DMA_PORTA_CONTROL_REG_BASE + 0x0C)
#define DMA_PORTA_CHAN1_ADDR_LOW        (DMA_PORTA_CONTROL_REG_BASE + 0x10)
#define DMA_PORTA_CHAN1_ADDR_HI         (DMA_PORTA_CONTROL_REG_BASE + 0x14)
#define DMA_PORTA_CHAN1_TRANS_STATE     (DMA_PORTA_CONTROL_REG_BASE + 0x18)
#define DMA_PORTA_CHAN1_CONTROL         (DMA_PORTA_CONTROL_REG_BASE + 0x1C)
#define DMA_PORTA_MANAGEMENT            (DMA_PORTA_CONTROL_REG_BASE + 0x20)
#define VIDEO_CTRL_STATUS_B             (ATV_PORTB_CONTROL_REG_BASE + 0x04)

/* -------- Digital TV control register, Port B -------- */
#define MPEG2_CTRL_B		(DTV_PORTB_CONTROL_REG_BASE + 0x00)
#define SERIAL_IN_ADDR_B	(DTV_PORTB_CONTROL_REG_BASE + 0x4C)
#define VLD_CNT_ADDR_B		(DTV_PORTB_CONTROL_REG_BASE + 0x60)
#define ERR_CNT_ADDR_B		(DTV_PORTB_CONTROL_REG_BASE + 0x64)
#define BRD_CNT_ADDR_B		(DTV_PORTB_CONTROL_REG_BASE + 0x68)

/* -------- AES control register, Port B -------- */
#define AES_CTRL_B		(AES_PORTB_CONTROL_REG_BASE + 0x00)
#define AES_KEY_BASE_B	(AES_PORTB_CONTROL_REG_BASE + 0x04)

/* -------- DMA Control Register, Port B  -------- */
#define DMA_PORTB_CHAN0_ADDR_LOW        (DMA_PORTB_CONTROL_REG_BASE + 0x00)
#define DMA_PORTB_CHAN0_ADDR_HI         (DMA_PORTB_CONTROL_REG_BASE + 0x04)
#define DMA_PORTB_CHAN0_TRANS_STATE     (DMA_PORTB_CONTROL_REG_BASE + 0x08)
#define DMA_PORTB_CHAN0_CONTROL         (DMA_PORTB_CONTROL_REG_BASE + 0x0C)
#define DMA_PORTB_CHAN1_ADDR_LOW        (DMA_PORTB_CONTROL_REG_BASE + 0x10)
#define DMA_PORTB_CHAN1_ADDR_HI         (DMA_PORTB_CONTROL_REG_BASE + 0x14)
#define DMA_PORTB_CHAN1_TRANS_STATE     (DMA_PORTB_CONTROL_REG_BASE + 0x18)
#define DMA_PORTB_CHAN1_CONTROL         (DMA_PORTB_CONTROL_REG_BASE + 0x1C)
#define DMA_PORTB_MANAGEMENT            (DMA_PORTB_CONTROL_REG_BASE + 0x20)

#define DMA_TRANS_UNIT_188 (0x00000007)

/* -------- Macro define of 24 interrupt resource --------*/
#define DMA_A_CHAN0_DONE_INT   (0x00000001)
#define DMA_A_CHAN1_DONE_INT   (0x00000002)
#define DMA_B_CHAN0_DONE_INT   (0x00000004)
#define DMA_B_CHAN1_DONE_INT   (0x00000008)
#define DMA_C_CHAN0_DONE_INT   (0x00000010)
#define DMA_C_CHAN1_DONE_INT   (0x00000020)
#define DMA_D_CHAN0_DONE_INT   (0x00000040)
#define DMA_D_CHAN1_DONE_INT   (0x00000080)
#define DATA_BUF_OVERFLOW_INT  (0x00000100)
#define UART_0_X_INT           (0x00000200)
#define UART_1_X_INT           (0x00000400)
#define IR_X_INT               (0x00000800)
#define GPIO_0_INT             (0x00001000)
#define GPIO_1_INT             (0x00002000)
#define GPIO_2_INT             (0x00004000)
#define GPIO_3_INT             (0x00008000)
#define ALL_INT                (0x0000FFFF)

/* software I2C bit mask */
#define SW_I2C_MSK_MODE         0x01
#define SW_I2C_MSK_CLK_OUT      0x02
#define SW_I2C_MSK_DAT_OUT      0x04
#define SW_I2C_MSK_CLK_EN       0x08
#define SW_I2C_MSK_DAT_EN       0x10
#define SW_I2C_MSK_DAT_IN       0x40
#define SW_I2C_MSK_CLK_IN       0x80

#define SMI_VID		0x1ADE
#define SMI_PID		0x3038
#define SMI_TS_DMA_BUF_SIZE	(1024 * 188)

struct smi_cfg_info {
#define SMI_DVBSKY_S952         0
#define SMI_DVBSKY_S950         1
#define SMI_DVBSKY_T9580        2
#define SMI_DVBSKY_T982         3
	int type;
	char *name;
#define SMI_TS_NULL             0
#define SMI_TS_DMA_SINGLE       1
#define SMI_TS_DMA_BOTH         3
/* SMI_TS_NULL: not use;
 * SMI_TS_DMA_SINGLE: use DMA 0 only;
 * SMI_TS_DMA_BOTH:use DMA 0 and 1.*/
	int ts_0;
	int ts_1;
#define DVBSKY_FE_NULL          0
#define DVBSKY_FE_M88RS6000     1
#define DVBSKY_FE_M88DS3103     2
#define DVBSKY_FE_SIT2          3
	int fe_0;
	int fe_1;
};

struct smi_port {
	struct smi_dev *dev;
	int idx;
	int enable;
	int fe_type;
	/* regs */
	u32 DMA_CHAN0_ADDR_LOW;
	u32 DMA_CHAN0_ADDR_HI;
	u32 DMA_CHAN0_TRANS_STATE;
	u32 DMA_CHAN0_CONTROL;
	u32 DMA_CHAN1_ADDR_LOW;
	u32 DMA_CHAN1_ADDR_HI;
	u32 DMA_CHAN1_TRANS_STATE;
	u32 DMA_CHAN1_CONTROL;
	u32 DMA_MANAGEMENT;
	/* dma */
	dma_addr_t dma_addr[2];
	u8 *cpu_addr[2];
	u32 _dmaInterruptCH0;
	u32 _dmaInterruptCH1;
	u32 _int_status;
	struct tasklet_struct tasklet;
	/* dvb */
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dmxdev dmxdev;
	struct dvb_adapter dvb_adapter;
	struct dvb_demux demux;
	struct dvb_net dvbnet;
	int users;
	struct dvb_frontend *fe;
	/* frontend i2c module */
	struct i2c_client *i2c_client_demod;
	struct i2c_client *i2c_client_tuner;
};

struct smi_dev {
	int nr;
	struct smi_cfg_info *info;

	/* pcie */
	struct pci_dev *pci_dev;
	u32 __iomem *lmmio;

	/* ts port */
	struct smi_port ts_port[2];

	/* i2c */
	struct i2c_adapter i2c_bus[2];
	struct i2c_algo_bit_data i2c_bit[2];
};

#define smi_read(reg)             readl(dev->lmmio + ((reg)>>2))
#define smi_write(reg, value)     writel((value), dev->lmmio + ((reg)>>2))

#define smi_andor(reg, mask, value) \
	writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
	((value) & (mask)), dev->lmmio+((reg)>>2))

#define smi_set(reg, bit)          smi_andor((reg), (bit), (bit))
#define smi_clear(reg, bit)        smi_andor((reg), (bit), 0)

#endif /* #ifndef _SMI_PCIE_H_ */

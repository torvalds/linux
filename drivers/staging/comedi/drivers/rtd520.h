/*
    comedi/drivers/rtd520.h
    Comedi driver defines for Real Time Devices (RTD) PCI4520/DM7520

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    Created by Dan Christian, NASA Ames Research Center.
    See board notes in rtd520.c
*/

/*
 * Local Address Space 0 Offsets
 */
#define LAS0_USER_IO		0x0008	/* User I/O */
#define LAS0_ADC		0x0010	/* FIFO Status/Software A/D Start */
#define LAS0_DAC1		0x0014	/* Software D/A1 Update (w) */
#define LAS0_DAC2		0x0018	/* Software D/A2 Update (w) */
#define LAS0_DAC		0x0024	/* Software Simultaneous Update (w) */
#define LAS0_PACER		0x0028	/* Software Pacer Start/Stop */
#define LAS0_TIMER		0x002c	/* Timer Status/HDIN Software Trig. */
#define LAS0_IT			0x0030	/* Interrupt Status/Enable */
#define LAS0_CLEAR		0x0034	/* Clear/Set Interrupt Clear Mask */
#define LAS0_OVERRUN		0x0038	/* Pending interrupts/Clear Overrun */
#define LAS0_PCLK		0x0040	/* Pacer Clock (24bit) */
#define LAS0_BCLK		0x0044	/* Burst Clock (10bit) */
#define LAS0_ADC_SCNT		0x0048	/* A/D Sample counter (10bit) */
#define LAS0_DAC1_UCNT		0x004c	/* D/A1 Update counter (10 bit) */
#define LAS0_DAC2_UCNT		0x0050	/* D/A2 Update counter (10 bit) */
#define LAS0_DCNT		0x0054	/* Delay counter (16 bit) */
#define LAS0_ACNT		0x0058	/* About counter (16 bit) */
#define LAS0_DAC_CLK		0x005c	/* DAC clock (16bit) */
#define LAS0_UTC0		0x0060	/* 8254 TC Counter 0 */
#define LAS0_UTC1		0x0064	/* 8254 TC Counter 1 */
#define LAS0_UTC2		0x0068	/* 8254 TC Counter 2 */
#define LAS0_UTC_CTRL		0x006c	/* 8254 TC Control */
#define LAS0_DIO0		0x0070	/* Digital I/O Port 0 */
#define LAS0_DIO1		0x0074	/* Digital I/O Port 1 */
#define LAS0_DIO0_CTRL		0x0078	/* Digital I/O Control */
#define LAS0_DIO_STATUS		0x007c	/* Digital I/O Status */
#define LAS0_BOARD_RESET	0x0100	/* Board reset */
#define LAS0_DMA0_SRC		0x0104	/* DMA 0 Sources select */
#define LAS0_DMA1_SRC		0x0108	/* DMA 1 Sources select */
#define LAS0_ADC_CONVERSION	0x010c	/* A/D Conversion Signal select */
#define LAS0_BURST_START	0x0110	/* Burst Clock Start Trigger select */
#define LAS0_PACER_START	0x0114	/* Pacer Clock Start Trigger select */
#define LAS0_PACER_STOP		0x0118	/* Pacer Clock Stop Trigger select */
#define LAS0_ACNT_STOP_ENABLE	0x011c	/* About Counter Stop Enable */
#define LAS0_PACER_REPEAT	0x0120	/* Pacer Start Trigger Mode select */
#define LAS0_DIN_START		0x0124	/* HiSpd DI Sampling Signal select */
#define LAS0_DIN_FIFO_CLEAR	0x0128	/* Digital Input FIFO Clear */
#define LAS0_ADC_FIFO_CLEAR	0x012c	/* A/D FIFO Clear */
#define LAS0_CGT_WRITE		0x0130	/* Channel Gain Table Write */
#define LAS0_CGL_WRITE		0x0134	/* Channel Gain Latch Write */
#define LAS0_CG_DATA		0x0138	/* Digital Table Write */
#define LAS0_CGT_ENABLE		0x013c	/* Channel Gain Table Enable */
#define LAS0_CG_ENABLE		0x0140	/* Digital Table Enable */
#define LAS0_CGT_PAUSE		0x0144	/* Table Pause Enable */
#define LAS0_CGT_RESET		0x0148	/* Reset Channel Gain Table */
#define LAS0_CGT_CLEAR		0x014c	/* Clear Channel Gain Table */
#define LAS0_DAC1_CTRL		0x0150	/* D/A1 output type/range */
#define LAS0_DAC1_SRC		0x0154	/* D/A1 update source */
#define LAS0_DAC1_CYCLE		0x0158	/* D/A1 cycle mode */
#define LAS0_DAC1_RESET		0x015c	/* D/A1 FIFO reset */
#define LAS0_DAC1_FIFO_CLEAR	0x0160	/* D/A1 FIFO clear */
#define LAS0_DAC2_CTRL		0x0164	/* D/A2 output type/range */
#define LAS0_DAC2_SRC		0x0168	/* D/A2 update source */
#define LAS0_DAC2_CYCLE		0x016c	/* D/A2 cycle mode */
#define LAS0_DAC2_RESET		0x0170	/* D/A2 FIFO reset */
#define LAS0_DAC2_FIFO_CLEAR	0x0174	/* D/A2 FIFO clear */
#define LAS0_ADC_SCNT_SRC	0x0178	/* A/D Sample Counter Source select */
#define LAS0_PACER_SELECT	0x0180	/* Pacer Clock select */
#define LAS0_SBUS0_SRC		0x0184	/* SyncBus 0 Source select */
#define LAS0_SBUS0_ENABLE	0x0188	/* SyncBus 0 enable */
#define LAS0_SBUS1_SRC		0x018c	/* SyncBus 1 Source select */
#define LAS0_SBUS1_ENABLE	0x0190	/* SyncBus 1 enable */
#define LAS0_SBUS2_SRC		0x0198	/* SyncBus 2 Source select */
#define LAS0_SBUS2_ENABLE	0x019c	/* SyncBus 2 enable */
#define LAS0_ETRG_POLARITY	0x01a4	/* Ext. Trigger polarity select */
#define LAS0_EINT_POLARITY	0x01a8	/* Ext. Interrupt polarity select */
#define LAS0_UTC0_CLOCK		0x01ac	/* UTC0 Clock select */
#define LAS0_UTC0_GATE		0x01b0	/* UTC0 Gate select */
#define LAS0_UTC1_CLOCK		0x01b4	/* UTC1 Clock select */
#define LAS0_UTC1_GATE		0x01b8	/* UTC1 Gate select */
#define LAS0_UTC2_CLOCK		0x01bc	/* UTC2 Clock select */
#define LAS0_UTC2_GATE		0x01c0	/* UTC2 Gate select */
#define LAS0_UOUT0_SELECT	0x01c4	/* User Output 0 source select */
#define LAS0_UOUT1_SELECT	0x01c8	/* User Output 1 source select */
#define LAS0_DMA0_RESET		0x01cc	/* DMA0 Request state machine reset */
#define LAS0_DMA1_RESET		0x01d0	/* DMA1 Request state machine reset */

/*
 * Local Address Space 1 Offsets
 */
#define LAS1_ADC_FIFO		0x0000	/* A/D FIFO (16bit) */
#define LAS1_HDIO_FIFO		0x0004	/* HiSpd DI FIFO (16bit) */
#define LAS1_DAC1_FIFO		0x0008	/* D/A1 FIFO (16bit) */
#define LAS1_DAC2_FIFO		0x000c	/* D/A2 FIFO (16bit) */

/*  FIFO Status Word Bits (RtdFifoStatus) */
#define FS_DAC1_NOT_EMPTY	(1 << 0)  /* DAC1 FIFO not empty */
#define FS_DAC1_HEMPTY		(1 << 1)  /* DAC1 FIFO half empty */
#define FS_DAC1_NOT_FULL	(1 << 2)  /* DAC1 FIFO not full */
#define FS_DAC2_NOT_EMPTY	(1 << 4)  /* DAC2 FIFO not empty */
#define FS_DAC2_HEMPTY		(1 << 5)  /* DAC2 FIFO half empty */
#define FS_DAC2_NOT_FULL	(1 << 6)  /* DAC2 FIFO not full */
#define FS_ADC_NOT_EMPTY	(1 << 8)  /* ADC FIFO not empty */
#define FS_ADC_HEMPTY		(1 << 9)  /* ADC FIFO half empty */
#define FS_ADC_NOT_FULL		(1 << 10) /* ADC FIFO not full */
#define FS_DIN_NOT_EMPTY	(1 << 12) /* DIN FIFO not empty */
#define FS_DIN_HEMPTY		(1 << 13) /* DIN FIFO half empty */
#define FS_DIN_NOT_FULL		(1 << 14) /* DIN FIFO not full */

/*  Interrupt Source Masks (SetITMask, ClearITMask, GetITStatus) */
#define IRQM_ADC_FIFO_WRITE        0x0001	/*  ADC FIFO Write */
#define IRQM_CGT_RESET             0x0002	/*  Reset CGT */
#define IRQM_CGT_PAUSE             0x0008	/*  Pause CGT */
#define IRQM_ADC_ABOUT_CNT         0x0010	/*  About Counter out */
#define IRQM_ADC_DELAY_CNT         0x0020	/*  Delay Counter out */
#define IRQM_ADC_SAMPLE_CNT	   0x0040	/*  ADC Sample Counter */
#define IRQM_DAC1_UCNT             0x0080	/*  DAC1 Update Counter */
#define IRQM_DAC2_UCNT             0x0100	/*  DAC2 Update Counter */
#define IRQM_UTC1                  0x0200	/*  User TC1 out */
#define IRQM_UTC1_INV              0x0400	/*  User TC1 out, inverted */
#define IRQM_UTC2                  0x0800	/*  User TC2 out */
#define IRQM_DIGITAL_IT            0x1000	/*  Digital Interrupt */
#define IRQM_EXTERNAL_IT           0x2000	/*  External Interrupt */
#define IRQM_ETRIG_RISING          0x4000	/*  External Trigger rising-edge */
#define IRQM_ETRIG_FALLING         0x8000	/*  External Trigger falling-edge */

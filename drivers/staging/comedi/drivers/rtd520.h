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

/*
 * PLX 9080 local config & runtime registers
 */
#define LCFG_ITCSR		0x0068	/* Interrupt Control/Status */
#define LCFG_DMAMODE0		0x0080	/* DMA0 Mode */
#define LCFG_DMAPADR0		0x0084	/* DMA0 PCI Address */
#define LCFG_DMALADR0		0x0088	/* DMA0 Local Address */
#define LCFG_DMASIZ0		0x008c	/* DMA0 Transfer Size (Bytes) */
#define LCFG_DMADPR0		0x0090	/* DMA0 Descriptor Pointer */
#define LCFG_DMAMODE1		0x0094	/* DMA1 Mode */
#define LCFG_DMAPADR1		0x0098	/* DMA1 PCI Address */
#define LCFG_DMALADR1		0x009c	/* DMA1 Local Address */
#define LCFG_DMASIZ1		0x00a0	/* DMA1 Transfer Size (Bytes) */
#define LCFG_DMADPR1		0x00a4	/* DMA1 Descriptor Pointer */
#define LCFG_DMACSR0		0x00a8	/* DMA0 Command/Status */
#define LCFG_DMACSR1		0x00a9	/* DMA0 Command/Status */
#define LCFG_DMAARB		0x00ac	/* DMA Arbitration */
#define LCFG_DMATHR		0x00b0	/* DMA Threshold */

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

/*  Timer Status Word Bits (GetTimerStatus) */
#define TS_PCLK_GATE		(1 << 0)  /* Pacer Clock Gate enabled */
#define TS_BCLK_GATE		(1 << 1)  /* Burst Clock Gate running */
#define TS_DCNT_GATE		(1 << 2)  /* Pacer Clock Delayed Start Trig. */
#define TS_ACNT_GATE		(1 << 3)  /* Pacer Clock About Trig. */
#define TS_PCLK_RUN		(1 << 4)  /* Pacer Clock Shutdown Flag */

/*  External Trigger polarity select */
/*  External Interrupt polarity select */
#define POL_POSITIVE         0x0	/*  positive edge */
#define POL_NEGATIVE         0x1	/*  negative edge */

/*  User Output Signal select (SetUout0Source, SetUout1Source) */
#define UOUT_ADC                0x0	/*  A/D Conversion Signal */
#define UOUT_DAC1               0x1	/*  D/A1 Update */
#define UOUT_DAC2               0x2	/*  D/A2 Update */
#define UOUT_SOFTWARE           0x3	/*  Software Programmable */

/*  Pacer clock select (SetPacerSource) */
#define PCLK_INTERNAL           1	/*  Internal Pacer Clock */
#define PCLK_EXTERNAL           0	/*  External Pacer Clock */

/*  A/D Sample Counter Sources (SetAdcntSource, SetupSampleCounter) */
#define ADC_SCNT_CGT_RESET         0x0	/*  needs restart with StartPacer */
#define ADC_SCNT_FIFO_WRITE        0x1

/*  A/D Conversion Signal Select (for SetConversionSelect) */
#define ADC_START_SOFTWARE         0x0	/*  Software A/D Start */
#define ADC_START_PCLK             0x1	/*  Pacer Clock (Ext. Int. see Func.509) */
#define ADC_START_BCLK             0x2	/*  Burst Clock */
#define ADC_START_DIGITAL_IT       0x3	/*  Digital Interrupt */
#define ADC_START_DAC1_MARKER1     0x4	/*  D/A 1 Data Marker 1 */
#define ADC_START_DAC2_MARKER1     0x5	/*  D/A 2 Data Marker 1 */
#define ADC_START_SBUS0            0x6	/*  SyncBus 0 */
#define ADC_START_SBUS1            0x7	/*  SyncBus 1 */
#define ADC_START_SBUS2            0x8	/*  SyncBus 2 */

/*  Burst Clock start trigger select (SetBurstStart) */
#define BCLK_START_SOFTWARE        0x0	/*  Software A/D Start (StartBurst) */
#define BCLK_START_PCLK            0x1	/*  Pacer Clock */
#define BCLK_START_ETRIG           0x2	/*  External Trigger */
#define BCLK_START_DIGITAL_IT      0x3	/*  Digital Interrupt */
#define BCLK_START_SBUS0           0x4	/*  SyncBus 0 */
#define BCLK_START_SBUS1           0x5	/*  SyncBus 1 */
#define BCLK_START_SBUS2           0x6	/*  SyncBus 2 */

/*  Pacer Clock start trigger select (SetPacerStart) */
#define PCLK_START_SOFTWARE        0x0	/*  Software Pacer Start (StartPacer) */
#define PCLK_START_ETRIG           0x1	/*  External trigger */
#define PCLK_START_DIGITAL_IT      0x2	/*  Digital interrupt */
#define PCLK_START_UTC2            0x3	/*  User TC 2 out */
#define PCLK_START_SBUS0           0x4	/*  SyncBus 0 */
#define PCLK_START_SBUS1           0x5	/*  SyncBus 1 */
#define PCLK_START_SBUS2           0x6	/*  SyncBus 2 */
#define PCLK_START_D_SOFTWARE      0x8	/*  Delayed Software Pacer Start */
#define PCLK_START_D_ETRIG         0x9	/*  Delayed external trigger */
#define PCLK_START_D_DIGITAL_IT    0xA	/*  Delayed digital interrupt */
#define PCLK_START_D_UTC2          0xB	/*  Delayed User TC 2 out */
#define PCLK_START_D_SBUS0         0xC	/*  Delayed SyncBus 0 */
#define PCLK_START_D_SBUS1         0xD	/*  Delayed SyncBus 1 */
#define PCLK_START_D_SBUS2         0xE	/*  Delayed SyncBus 2 */
#define PCLK_START_ETRIG_GATED     0xF	/*  External Trigger Gated controlled mode */

/*  Pacer Clock Stop Trigger select (SetPacerStop) */
#define PCLK_STOP_SOFTWARE         0x0	/*  Software Pacer Stop (StopPacer) */
#define PCLK_STOP_ETRIG            0x1	/*  External Trigger */
#define PCLK_STOP_DIGITAL_IT       0x2	/*  Digital Interrupt */
#define PCLK_STOP_ACNT             0x3	/*  About Counter */
#define PCLK_STOP_UTC2             0x4	/*  User TC2 out */
#define PCLK_STOP_SBUS0            0x5	/*  SyncBus 0 */
#define PCLK_STOP_SBUS1            0x6	/*  SyncBus 1 */
#define PCLK_STOP_SBUS2            0x7	/*  SyncBus 2 */
#define PCLK_STOP_A_SOFTWARE       0x8	/*  About Software Pacer Stop */
#define PCLK_STOP_A_ETRIG          0x9	/*  About External Trigger */
#define PCLK_STOP_A_DIGITAL_IT     0xA	/*  About Digital Interrupt */
#define PCLK_STOP_A_UTC2           0xC	/*  About User TC2 out */
#define PCLK_STOP_A_SBUS0          0xD	/*  About SyncBus 0 */
#define PCLK_STOP_A_SBUS1          0xE	/*  About SyncBus 1 */
#define PCLK_STOP_A_SBUS2          0xF	/*  About SyncBus 2 */

/*  About Counter Stop Enable */
#define ACNT_STOP                  0x0	/*  stop enable */
#define ACNT_NO_STOP               0x1	/*  stop disabled */

/*  DAC update source (SetDAC1Start & SetDAC2Start) */
#define DAC_START_SOFTWARE         0x0	/*  Software Update */
#define DAC_START_CGT              0x1	/*  CGT controlled Update */
#define DAC_START_DAC_CLK          0x2	/*  D/A Clock */
#define DAC_START_EPCLK            0x3	/*  External Pacer Clock */
#define DAC_START_SBUS0            0x4	/*  SyncBus 0 */
#define DAC_START_SBUS1            0x5	/*  SyncBus 1 */
#define DAC_START_SBUS2            0x6	/*  SyncBus 2 */

/*  DAC Cycle Mode (SetDAC1Cycle, SetDAC2Cycle, SetupDAC) */
#define DAC_CYCLE_SINGLE           0x0	/*  not cycle */
#define DAC_CYCLE_MULTI            0x1	/*  cycle */

/*  8254 Operation Modes (Set8254Mode, SetupTimerCounter) */
#define M8254_EVENT_COUNTER        0	/*  Event Counter */
#define M8254_HW_ONE_SHOT          1	/*  Hardware-Retriggerable One-Shot */
#define M8254_RATE_GENERATOR       2	/*  Rate Generator */
#define M8254_SQUARE_WAVE          3	/*  Square Wave Mode */
#define M8254_SW_STROBE            4	/*  Software Triggered Strobe */
#define M8254_HW_STROBE            5	/*  Hardware Triggered Strobe (Retriggerable) */

/*  User Timer/Counter 0 Clock Select (SetUtc0Clock) */
#define CUTC0_8MHZ                 0x0	/*  8MHz */
#define CUTC0_EXT_TC_CLOCK1        0x1	/*  Ext. TC Clock 1 */
#define CUTC0_EXT_TC_CLOCK2        0x2	/*  Ext. TC Clock 2 */
#define CUTC0_EXT_PCLK             0x3	/*  Ext. Pacer Clock */

/*  User Timer/Counter 1 Clock Select (SetUtc1Clock) */
#define CUTC1_8MHZ                 0x0	/*  8MHz */
#define CUTC1_EXT_TC_CLOCK1        0x1	/*  Ext. TC Clock 1 */
#define CUTC1_EXT_TC_CLOCK2        0x2	/*  Ext. TC Clock 2 */
#define CUTC1_EXT_PCLK             0x3	/*  Ext. Pacer Clock */
#define CUTC1_UTC0_OUT             0x4	/*  User Timer/Counter 0 out */
#define CUTC1_DIN_SIGNAL           0x5	/*  High-Speed Digital Input   Sampling signal */

/*  User Timer/Counter 2 Clock Select (SetUtc2Clock) */
#define CUTC2_8MHZ                 0x0	/*  8MHz */
#define CUTC2_EXT_TC_CLOCK1        0x1	/*  Ext. TC Clock 1 */
#define CUTC2_EXT_TC_CLOCK2        0x2	/*  Ext. TC Clock 2 */
#define CUTC2_EXT_PCLK             0x3	/*  Ext. Pacer Clock */
#define CUTC2_UTC1_OUT             0x4	/*  User Timer/Counter 1 out */

/*  User Timer/Counter 0 Gate Select (SetUtc0Gate) */
#define GUTC0_NOT_GATED            0x0	/*  Not gated */
#define GUTC0_GATED                0x1	/*  Gated */
#define GUTC0_EXT_TC_GATE1         0x2	/*  Ext. TC Gate 1 */
#define GUTC0_EXT_TC_GATE2         0x3	/*  Ext. TC Gate 2 */

/*  User Timer/Counter 1 Gate Select (SetUtc1Gate) */
#define GUTC1_NOT_GATED            0x0	/*  Not gated */
#define GUTC1_GATED                0x1	/*  Gated */
#define GUTC1_EXT_TC_GATE1         0x2	/*  Ext. TC Gate 1 */
#define GUTC1_EXT_TC_GATE2         0x3	/*  Ext. TC Gate 2 */
#define GUTC1_UTC0_OUT             0x4	/*  User Timer/Counter 0 out */

/*  User Timer/Counter 2 Gate Select (SetUtc2Gate) */
#define GUTC2_NOT_GATED            0x0	/*  Not gated */
#define GUTC2_GATED                0x1	/*  Gated */
#define GUTC2_EXT_TC_GATE1         0x2	/*  Ext. TC Gate 1 */
#define GUTC2_EXT_TC_GATE2         0x3	/*  Ext. TC Gate 2 */
#define GUTC2_UTC1_OUT             0x4	/*  User Timer/Counter 1 out */

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

/*  DMA Request Sources (LAS0) */
#define DMAS_DISABLED              0x0	/*  DMA Disabled */
#define DMAS_ADC_SCNT              0x1	/*  ADC Sample Counter */
#define DMAS_DAC1_UCNT             0x2	/*  D/A1 Update Counter */
#define DMAS_DAC2_UCNT             0x3	/*  D/A2 Update Counter */
#define DMAS_UTC1                  0x4	/*  User TC1 out */
#define DMAS_ADFIFO_HALF_FULL      0x8	/*  A/D FIFO half full */
#define DMAS_DAC1_FIFO_HALF_EMPTY  0x9	/*  D/A1 FIFO half empty */
#define DMAS_DAC2_FIFO_HALF_EMPTY  0xA	/*  D/A2 FIFO half empty */

/*  DMA Local Addresses   (0x40000000+LAS1 offset) */
#define DMALADDR_ADC       0x40000000	/*  A/D FIFO */
#define DMALADDR_HDIN      0x40000004	/*  High Speed Digital Input FIFO */
#define DMALADDR_DAC1      0x40000008	/*  D/A1 FIFO */
#define DMALADDR_DAC2      0x4000000C	/*  D/A2 FIFO */

/*  Port 0 compare modes (SetDIO0CompareMode) */
#define DIO_MODE_EVENT     0	/*  Event Mode */
#define DIO_MODE_MATCH     1	/*  Match Mode */

/*  Digital Table Enable (Port 1 disable) */
#define DTBL_DISABLE       0	/*  Enable Digital Table */
#define DTBL_ENABLE        1	/*  Disable Digital Table */

/*  Sampling Signal for High Speed Digital Input (SetHdinStart) */
#define HDIN_SOFTWARE      0x0	/*  Software Trigger */
#define HDIN_ADC           0x1	/*  A/D Conversion Signal */
#define HDIN_UTC0          0x2	/*  User TC out 0 */
#define HDIN_UTC1          0x3	/*  User TC out 1 */
#define HDIN_UTC2          0x4	/*  User TC out 2 */
#define HDIN_EPCLK         0x5	/*  External Pacer Clock */
#define HDIN_ETRG          0x6	/*  External Trigger */

/*  Channel Gain Table / Channel Gain Latch */
#define CSC_LATCH          0	/*  Channel Gain Latch mode */
#define CSC_CGT            1	/*  Channel Gain Table mode */

/*  Channel Gain Table Pause Enable */
#define CGT_PAUSE_DISABLE  0	/*  Channel Gain Table Pause Disable */
#define CGT_PAUSE_ENABLE   1	/*  Channel Gain Table Pause Enable */

/*  DAC output type/range (p63) */
#define AOUT_UNIP5         0	/*  0..+5 Volt */
#define AOUT_UNIP10        1	/*  0..+10 Volt */
#define AOUT_BIP5          2	/*  -5..+5 Volt */
#define AOUT_BIP10         3	/*  -10..+10 Volt */

/*  Ghannel Gain Table field definitions (p61) */
/*  Gain */
#define GAIN1              0
#define GAIN2              1
#define GAIN4              2
#define GAIN8              3
#define GAIN16             4
#define GAIN32             5
#define GAIN64             6
#define GAIN128            7

/*  Input range/polarity */
#define AIN_BIP5           0	/*  -5..+5 Volt */
#define AIN_BIP10          1	/*  -10..+10 Volt */
#define AIN_UNIP10         2	/*  0..+10 Volt */

/*  non referenced single ended select bit */
#define NRSE_AGND          0	/*  AGND referenced SE input */
#define NRSE_AINS          1	/*  AIN SENSE referenced SE input */

/*  single ended vs differential */
#define GND_SE		0	/*  Single-Ended */
#define GND_DIFF	1	/*  Differential */

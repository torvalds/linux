/*
 *  Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/sched.h>
#include <linux/interrupt.h>

#define LOBYTE(W)	(unsigned char)((W) & 0xFF)
#define HIBYTE(W)	(unsigned char)(((W) >> 8) & 0xFF)
#define MAKEWORD(H, L)	(unsigned short)((L) | ((H) << 8))
#define LOWORD(W)	(unsigned short)((W) & 0xFFFF)
#define HIWORD(W)	(unsigned short)(((W) >> 16) & 0xFFFF)
#define MAKEDWORD(H, L)	(unsigned int)((L) | ((H) << 16))

#define ADDI_ENABLE		1
#define ADDI_DISABLE		0
#define APCI1710_SAVE_INTERRUPT	1

#define ADDIDATA_EEPROM		1
#define ADDIDATA_NO_EEPROM	0
#define ADDIDATA_93C76		"93C76"
#define ADDIDATA_S5920		"S5920"
#define ADDIDATA_S5933		"S5933"

/* ADDIDATA Enable Disable */
#define ADDIDATA_ENABLE		1
#define ADDIDATA_DISABLE	0

/* Structures */

/* structure for the boardtype */
struct addi_board {
	const char *pc_DriverName;	/*  driver name */
	int i_IorangeBase1;
	int i_PCIEeprom;	/*  eeprom present or not */
	char *pc_EepromChip;	/*  type of chip */
	int i_NbrAiChannel;	/*  num of A/D chans */
	int i_NbrAiChannelDiff;	/*  num of A/D chans in diff mode */
	int i_AiChannelList;	/*  len of chanlist */
	int i_NbrAoChannel;	/*  num of D/A chans */
	int i_AiMaxdata;	/*  resolution of A/D */
	int i_AoMaxdata;	/*  resolution of D/A */
	const struct comedi_lrange *pr_AiRangelist;	/* rangelist for A/D */
	const struct comedi_lrange *pr_AoRangelist;	/* rangelist for D/A */

	int i_NbrDiChannel;	/*  Number of DI channels */
	int i_NbrDoChannel;	/*  Number of DO channels */
	int i_DoMaxdata;	/*  data to set all channels high */

	int i_NbrTTLChannel;	/*  Number of TTL channels */

	int i_Dma;		/*  dma present or not */
	int i_Timer;		/*    timer subdevice present or not */
	unsigned char b_AvailableConvertUnit;
	unsigned int ui_MinAcquisitiontimeNs;	/*  Minimum Acquisition in Nano secs */
	unsigned int ui_MinDelaytimeNs;	/*  Minimum Delay in Nano secs */

	/* interrupt and reset */
	void (*interrupt)(int irq, void *d);
	int (*reset)(struct comedi_device *);

	/* Subdevice functions */

	/* ANALOG INPUT */
	int (*ai_config)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*ai_read)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);
	int (*ai_write)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*ai_bits)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);
	int (*ai_cmdtest)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_cmd *);
	int (*ai_cmd)(struct comedi_device *, struct comedi_subdevice *);
	int (*ai_cancel)(struct comedi_device *, struct comedi_subdevice *);

	/* Analog Output */
	int (*ao_config)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*ao_write)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*ao_bits)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);

	/* Digital Input */
	int (*di_config)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*di_read)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);
	int (*di_write)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*di_bits)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);

	/* Digital Output */
	int (*do_config)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
	int (*do_write)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*do_bits)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);
	int (*do_read)(struct comedi_device *, struct comedi_subdevice *,
		       struct comedi_insn *, unsigned int *);

	/* TIMER */
	int (*timer_config)(struct comedi_device *, struct comedi_subdevice *,
			    struct comedi_insn *, unsigned int *);
	int (*timer_write)(struct comedi_device *, struct comedi_subdevice *,
			   struct comedi_insn *, unsigned int *);
	int (*timer_read)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);
	int (*timer_bits)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);

	/* TTL IO */
	int (*ttl_config)(struct comedi_device *, struct comedi_subdevice *,
			  struct comedi_insn *, unsigned int *);
	int (*ttl_bits)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*ttl_read)(struct comedi_device *, struct comedi_subdevice *,
			struct comedi_insn *, unsigned int *);
	int (*ttl_write)(struct comedi_device *, struct comedi_subdevice *,
			 struct comedi_insn *, unsigned int *);
};

/* MODULE INFO STRUCTURE */

union str_ModuleInfo {
	/* Incremental counter infos */
	struct {
		union {
			struct {
				unsigned char b_ModeRegister1;
				unsigned char b_ModeRegister2;
				unsigned char b_ModeRegister3;
				unsigned char b_ModeRegister4;
			} s_ByteModeRegister;
			unsigned int dw_ModeRegister1_2_3_4;
		} s_ModeRegister;

		struct {
			unsigned int b_IndexInit:1;
			unsigned int b_CounterInit:1;
			unsigned int b_ReferenceInit:1;
			unsigned int b_IndexInterruptOccur:1;
			unsigned int b_CompareLogicInit:1;
			unsigned int b_FrequencyMeasurementInit:1;
			unsigned int b_FrequencyMeasurementEnable:1;
		} s_InitFlag;

	} s_SiemensCounterInfo;

	/* SSI infos */
	struct {
		unsigned char b_SSIProfile;
		unsigned char b_PositionTurnLength;
		unsigned char b_TurnCptLength;
		unsigned char b_SSIInit;
	} s_SSICounterInfo;

	/* TTL I/O infos */
	struct {
		unsigned char b_TTLInit;
		unsigned char b_PortConfiguration[4];
	} s_TTLIOInfo;

	/* Digital I/O infos */
	struct {
		unsigned char b_DigitalInit;
		unsigned char b_ChannelAMode;
		unsigned char b_ChannelBMode;
		unsigned char b_OutputMemoryEnabled;
		unsigned int dw_OutputMemory;
	} s_DigitalIOInfo;

      /*********************/
	/* 82X54 timer infos */
      /*********************/

	struct {
		struct {
			unsigned char b_82X54Init;
			unsigned char b_InputClockSelection;
			unsigned char b_InputClockLevel;
			unsigned char b_OutputLevel;
			unsigned char b_HardwareGateLevel;
			unsigned int dw_ConfigurationWord;
		} s_82X54TimerInfo[3];
		unsigned char b_InterruptMask;
	} s_82X54ModuleInfo;

      /*********************/
	/* Chronometer infos */
      /*********************/

	struct {
		unsigned char b_ChronoInit;
		unsigned char b_InterruptMask;
		unsigned char b_PCIInputClock;
		unsigned char b_TimingUnit;
		unsigned char b_CycleMode;
		double d_TimingInterval;
		unsigned int dw_ConfigReg;
	} s_ChronoModuleInfo;

      /***********************/
	/* Pulse encoder infos */
      /***********************/

	struct {
		struct {
			unsigned char b_PulseEncoderInit;
		} s_PulseEncoderInfo[4];
		unsigned int dw_SetRegister;
		unsigned int dw_ControlRegister;
		unsigned int dw_StatusRegister;
	} s_PulseEncoderModuleInfo;

	/* Tor conter infos */
	struct {
		struct {
			unsigned char b_TorCounterInit;
			unsigned char b_TimingUnit;
			unsigned char b_InterruptEnable;
			double d_TimingInterval;
			unsigned int ul_RealTimingInterval;
		} s_TorCounterInfo[2];
		unsigned char b_PCIInputClock;
	} s_TorCounterModuleInfo;

	/* PWM infos */
	struct {
		struct {
			unsigned char b_PWMInit;
			unsigned char b_TimingUnit;
			unsigned char b_InterruptEnable;
			double d_LowTiming;
			double d_HighTiming;
			unsigned int ul_RealLowTiming;
			unsigned int ul_RealHighTiming;
		} s_PWMInfo[2];
		unsigned char b_ClockSelection;
	} s_PWMModuleInfo;

	/* ETM infos */
	struct {
		struct {
			unsigned char b_ETMEnable;
			unsigned char b_ETMInterrupt;
		} s_ETMInfo[2];
		unsigned char b_ETMInit;
		unsigned char b_TimingUnit;
		unsigned char b_ClockSelection;
		double d_TimingInterval;
		unsigned int ul_Timing;
	} s_ETMModuleInfo;

	/* CDA infos */
	struct {
		unsigned char b_CDAEnable;
		unsigned char b_CDAInterrupt;
		unsigned char b_CDAInit;
		unsigned char b_FctSelection;
		unsigned char b_CDAReadFIFOOverflow;
	} s_CDAModuleInfo;

};

/* Private structure for the addi_apci3120 driver */
struct addi_private {
	int iobase;
	int i_IobaseAmcc;	/*  base+size for AMCC chip */
	int i_IobaseAddon;	/* addon base address */
	int i_IobaseReserved;
	unsigned char b_AiContinuous;	/*  we do unlimited AI */
	unsigned int ui_AiActualScan;	/* how many scans we finished */
	unsigned int ui_AiNbrofChannels;	/*  how many channels is measured */
	unsigned int ui_AiScanLength;	/*  Length of actual scanlist */
	unsigned int *pui_AiChannelList;	/*  actual chanlist */
	unsigned int ui_AiChannelList[32];	/*  actual chanlist */
	unsigned int ui_AiReadData[32];
	unsigned int ui_AiTimer0;	/* Timer Constant for Timer0 */
	unsigned int ui_AiTimer1;	/* Timer constant for Timer1 */
	unsigned int ui_AiFlags;
	unsigned int ui_AiDataLength;
	unsigned int ui_AiNbrofScans;	/*  number of scans to do */
	unsigned short us_UseDma;	/*  To use Dma or not */
	unsigned char b_DmaDoubleBuffer;	/*  we can use double buffering */
	unsigned int ui_DmaActualBuffer;	/*  which buffer is used now */
	short *ul_DmaBufferVirtual[2];	/*  pointers to begin of DMA buffer */
	unsigned int ul_DmaBufferHw[2];	/*  hw address of DMA buff */
	unsigned int ui_DmaBufferSize[2];	/*  size of dma buffer in bytes */
	unsigned int ui_DmaBufferUsesize[2];	/*  which size we may now used for transfer */
	unsigned int ui_DmaBufferPages[2];	/*  number of pages in buffer */
	unsigned char b_DigitalOutputRegister;	/*  Digital Output Register */
	unsigned char b_OutputMemoryStatus;
	unsigned char b_TimerSelectMode;	/*  Contain data written at iobase + 0C */
	unsigned char b_ModeSelectRegister;	/*  Contain data written at iobase + 0E */
	unsigned short us_OutputRegister;	/*  Contain data written at iobase + 0 */
	unsigned char b_Timer2Mode;	/*  Specify the timer 2 mode */
	unsigned char b_Timer2Interrupt;	/* Timer2  interrupt enable or disable */
	unsigned char b_AiCyclicAcquisition;	/*  indicate cyclic acquisition */
	unsigned char b_InterruptMode;	/*  eoc eos or dma */
	unsigned char b_EocEosInterrupt;	/*  Enable disable eoc eos interrupt */
	unsigned int ui_EocEosConversionTime;
	unsigned char b_SingelDiff;
	unsigned char b_ExttrigEnable;	/* To enable or disable external trigger */

	/* Pointer to the current process */
	struct task_struct *tsk_Current;

	/* Hardware board infos for 1710 */
	struct {
		unsigned int ui_Address;	/* Board address */
		unsigned int ui_FlashAddress;
		unsigned char b_InterruptNbr;	/* Board interrupt number */
		unsigned char b_SlotNumber;	/* PCI slot number */
		unsigned char b_BoardVersion;
		unsigned int dw_MolduleConfiguration[4];	/* Module config */
	} s_BoardInfos;

	/* Interrupt infos */
	struct {
		unsigned int ul_InterruptOccur;	/* 0   : No interrupt occur */
						/* > 0 : Interrupt occur */
		unsigned int ui_Read;	/* Read FIFO */
		unsigned int ui_Write;	/* Write FIFO */
		struct {
			unsigned char b_OldModuleMask;
			unsigned int ul_OldInterruptMask;	/* Interrupt mask */
			unsigned int ul_OldCounterLatchValue;	/* Interrupt counter value */
		} s_FIFOInterruptParameters[APCI1710_SAVE_INTERRUPT];
	} s_InterruptParameters;

	union str_ModuleInfo s_ModuleInfo[4];

	/* Parameters read from EEPROM overriding static board info */
	struct {
		int i_NbrAiChannel;	/*  num of A/D chans */
		int i_NbrAoChannel;	/*  num of D/A chans */
		int i_AiMaxdata;	/*  resolution of A/D */
		int i_AoMaxdata;	/*  resolution of D/A */
		int i_NbrDiChannel;	/*  Number of DI channels */
		int i_NbrDoChannel;	/*  Number of DO channels */
		int i_DoMaxdata;	/*  data to set all channels high */
		int i_Dma;		/*  dma present or not */
		int i_Timer;		/*  timer subdevice present or not */
		unsigned int ui_MinAcquisitiontimeNs;
					/*  Minimum Acquisition in Nano secs */
		unsigned int ui_MinDelaytimeNs;
					/*  Minimum Delay in Nano secs */
	} s_EeParameters;
};


/* hwdrv_apci3120.h */

/*
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data-com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* comedi related defines */

/* ANALOG INPUT RANGE */
static const struct comedi_lrange range_apci3120_ai = { 8, {
						     BIP_RANGE(10),
						     BIP_RANGE(5),
						     BIP_RANGE(2),
						     BIP_RANGE(1),
						     UNI_RANGE(10),
						     UNI_RANGE(5),
						     UNI_RANGE(2),
						     UNI_RANGE(1)
						     }
};

/* ANALOG OUTPUT RANGE */
static const struct comedi_lrange range_apci3120_ao = { 2, {
						     BIP_RANGE(10),
						     UNI_RANGE(10)
						     }
};

#define APCI3120_BIPOLAR_RANGES	4	/*  used for test on mixture of BIP/UNI ranges */

#define APCI3120_BOARD_VENDOR_ID                 0x10E8
#define APCI3120_ADDRESS_RANGE            			16

#define APCI3120_DISABLE                         0
#define APCI3120_ENABLE                          1

#define APCI3120_START                           1
#define APCI3120_STOP                            0

#define     APCI3120_EOC_MODE         1
#define     APCI3120_EOS_MODE         2
#define     APCI3120_DMA_MODE         3

/* DIGITAL INPUT-OUTPUT DEFINE */

#define APCI3120_DIGITAL_OUTPUT                  	0x0D
#define APCI3120_RD_STATUS                       	0x02
#define APCI3120_RD_FIFO                     		0x00

/* digital output insn_write ON /OFF selection */
#define	APCI3120_SET4DIGITALOUTPUTON				1
#define APCI3120_SET4DIGITALOUTPUTOFF				0

/* analog output SELECT BIT */
#define APCI3120_ANALOG_OP_CHANNEL_1   0x0000
#define APCI3120_ANALOG_OP_CHANNEL_2   0x4000
#define APCI3120_ANALOG_OP_CHANNEL_3   0x8000
#define APCI3120_ANALOG_OP_CHANNEL_4   0xC000
#define APCI3120_ANALOG_OP_CHANNEL_5   0x0000
#define APCI3120_ANALOG_OP_CHANNEL_6   0x4000
#define APCI3120_ANALOG_OP_CHANNEL_7   0x8000
#define APCI3120_ANALOG_OP_CHANNEL_8   0xC000

/* Enable external trigger bit in nWrAddress */
#define APCI3120_ENABLE_EXT_TRIGGER    0x8000

/* ANALOG OUTPUT AND INPUT DEFINE */
#define APCI3120_UNIPOLAR 0x80	/* $$ RAM sequence polarity BIT */
#define APCI3120_BIPOLAR  0x00	/* $$ RAM sequence polarity BIT */
#define APCI3120_ANALOG_OUTPUT_1 0x08	/*  (ADDRESS ) */
#define APCI3120_ANALOG_OUTPUT_2 0x0A	/*  (ADDRESS ) */
#define APCI3120_1_GAIN              0x00	/* $$ RAM sequence Gain Bits for gain 1 */
#define APCI3120_2_GAIN              0x10	/* $$ RAM sequence Gain Bits for gain 2 */
#define APCI3120_5_GAIN              0x20	/* $$ RAM sequence Gain Bits for gain 5 */
#define APCI3120_10_GAIN             0x30	/* $$ RAM sequence Gain Bits for gain 10 */
#define APCI3120_SEQ_RAM_ADDRESS        0x06	/* $$ EARLIER NAMED APCI3120_FIFO_ADDRESS */
#define APCI3120_RESET_FIFO          0x0C	/* (ADDRESS) */
#define APCI3120_TIMER_0_MODE_2      0x01	/* $$ Bits for timer mode */
#define APCI3120_TIMER_0_MODE_4       0x2
#define APCI3120_SELECT_TIMER_0_WORD 0x00
#define APCI3120_ENABLE_TIMER0     0x1000	/* $$Gatebit 0 in nWrAddress */
#define APCI3120_CLEAR_PR          0xF0FF
#define APCI3120_CLEAR_PA          0xFFF0
#define APCI3120_CLEAR_PA_PR       (APCI3120_CLEAR_PR & APCI3120_CLEAR_PA)

/* nWrMode_Select */
#define APCI3120_ENABLE_SCAN          0x8	/* $$ bit in nWrMode_Select */
#define APCI3120_DISABLE_SCAN      (~APCI3120_ENABLE_SCAN)
#define APCI3120_ENABLE_EOS_INT       0x2	/* $$ bit in nWrMode_Select */

#define APCI3120_DISABLE_EOS_INT   (~APCI3120_ENABLE_EOS_INT)
#define APCI3120_ENABLE_EOC_INT       0x1
#define APCI3120_DISABLE_EOC_INT   (~APCI3120_ENABLE_EOC_INT)
#define APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER   (APCI3120_DISABLE_EOS_INT & APCI3120_DISABLE_EOC_INT)
#define APCI3120_DISABLE_ALL_INTERRUPT   (APCI3120_DISABLE_TIMER_INT & APCI3120_DISABLE_EOS_INT & APCI3120_DISABLE_EOC_INT)

/* status register bits */
#define APCI3120_EOC                     0x8000
#define APCI3120_EOS                     0x2000

/* software trigger dummy register */
#define APCI3120_START_CONVERSION        0x02	/* (ADDRESS) */

/* TIMER DEFINE */
#define APCI3120_QUARTZ_A				  70
#define APCI3120_QUARTZ_B				  50
#define APCI3120_TIMER                            1
#define APCI3120_WATCHDOG                         2
#define APCI3120_TIMER_DISABLE                    0
#define APCI3120_TIMER_ENABLE                     1
#define APCI3120_ENABLE_TIMER2                    0x4000	/* $$ gatebit 2 in nWrAddress */
#define APCI3120_DISABLE_TIMER2                   (~APCI3120_ENABLE_TIMER2)
#define APCI3120_ENABLE_TIMER_INT                 0x04	/* $$ ENAIRQ_FC_Bit in nWrModeSelect */
#define APCI3120_DISABLE_TIMER_INT                (~APCI3120_ENABLE_TIMER_INT)
#define APCI3120_WRITE_MODE_SELECT                0x0E	/*  (ADDRESS) */
#define APCI3120_SELECT_TIMER_0_WORD  0x00
#define APCI3120_SELECT_TIMER_1_WORD  0x01
#define APCI3120_TIMER_1_MODE_2       0x4

/* $$ BIT FOR MODE IN nCsTimerCtr1 */
#define APCI3120_TIMER_2_MODE_0                   0x0
#define APCI3120_TIMER_2_MODE_2                   0x10
#define APCI3120_TIMER_2_MODE_5                   0x30

/* $$ BIT FOR MODE IN nCsTimerCtr0 */
#define APCI3120_SELECT_TIMER_2_LOW_WORD          0x02
#define APCI3120_SELECT_TIMER_2_HIGH_WORD         0x03

#define APCI3120_TIMER_CRT0                       0x0D	/* (ADDRESS for cCsTimerCtr0) */
#define APCI3120_TIMER_CRT1                       0x0C	/* (ADDRESS for cCsTimerCtr1) */

#define APCI3120_TIMER_VALUE                      0x04	/* ADDRESS for nCsTimerWert */
#define APCI3120_TIMER_STATUS_REGISTER            0x0D	/* ADDRESS for delete timer 2 interrupt */
#define APCI3120_RD_STATUS                        0x02	/* ADDRESS */
#define APCI3120_WR_ADDRESS                       0x00	/* ADDRESS */
#define APCI3120_ENABLE_WATCHDOG                  0x20	/* $$BIT in nWrMode_Select */
#define APCI3120_DISABLE_WATCHDOG                 (~APCI3120_ENABLE_WATCHDOG)
#define APCI3120_ENABLE_TIMER_COUNTER    		  0x10	/* $$BIT in nWrMode_Select */
#define APCI3120_DISABLE_TIMER_COUNTER            (~APCI3120_ENABLE_TIMER_COUNTER)
#define APCI3120_FC_TIMER                         0x1000	/* bit in  status register */
#define APCI3120_ENABLE_TIMER0                    0x1000
#define APCI3120_ENABLE_TIMER1                    0x2000
#define APCI3120_ENABLE_TIMER2                    0x4000
#define APCI3120_DISABLE_TIMER0			          (~APCI3120_ENABLE_TIMER0)
#define APCI3120_DISABLE_TIMER1		              (~APCI3120_ENABLE_TIMER1)
#define APCI3120_DISABLE_TIMER2	                  (~APCI3120_ENABLE_TIMER2)

#define APCI3120_TIMER2_SELECT_EOS                0xC0	/*  ADDED on 20-6 */
#define APCI3120_COUNTER                          3	/*  on 20-6 */
#define APCI3120_DISABLE_ALL_TIMER                ( APCI3120_DISABLE_TIMER0 & APCI3120_DISABLE_TIMER1 & APCI3120_DISABLE_TIMER2 )	/*  on 20-6 */

#define MAX_ANALOGINPUT_CHANNELS    32

struct str_AnalogReadInformation {

	unsigned char b_Type;		/* EOC or EOS */
	unsigned char b_InterruptFlag;	/* Interrupt use or not                    */
	unsigned int ui_ConvertTiming;	/* Selection of the convertion time        */
	unsigned char b_NbrOfChannel;	/* Number of channel to read               */
	unsigned int ui_ChannelList[MAX_ANALOGINPUT_CHANNELS];	/* Number of the channel to be read        */
	unsigned int ui_RangeList[MAX_ANALOGINPUT_CHANNELS];	/* Gain of each channel                    */

};


/* Function Declaration For APCI-3120 */

/* Internal functions */
int i_APCI3120_SetupChannelList(struct comedi_device *dev, struct comedi_subdevice *s,
				int n_chan, unsigned int *chanlist, char check);
int i_APCI3120_ExttrigEnable(struct comedi_device *dev);
int i_APCI3120_ExttrigDisable(struct comedi_device *dev);
int i_APCI3120_StopCyclicAcquisition(struct comedi_device *dev, struct comedi_subdevice *s);
int i_APCI3120_Reset(struct comedi_device *dev);
int i_APCI3120_CyclicAnalogInput(int mode, struct comedi_device *dev,
				 struct comedi_subdevice *s);
/* Interrupt functions */
void v_APCI3120_Interrupt(int irq, void *d);
/* UPDATE-0.7.57->0.7.68 void v_APCI3120_InterruptDmaMoveBlock16bit(struct comedi_device *dev,struct comedi_subdevice *s,short *dma,short *data,int n); */
void v_APCI3120_InterruptDmaMoveBlock16bit(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   short *dma_buffer,
					   unsigned int num_samples);
int i_APCI3120_InterruptHandleEos(struct comedi_device *dev);
void v_APCI3120_InterruptDma(int irq, void *d);

/* TIMER */

int i_APCI3120_InsnConfigTimer(struct comedi_device *dev, struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_InsnWriteTimer(struct comedi_device *dev, struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_InsnReadTimer(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data);

/*
* DI for di read
*/

int i_APCI3120_InsnBitsDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_InsnReadDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);

/* DO */
/* int i_APCI3120_WriteDigitalOutput(struct comedi_device *dev,
 * unsigned char data);
 */
int i_APCI3120_InsnConfigDigitalOutput(struct comedi_device *dev,
				       struct comedi_subdevice *s, struct comedi_insn *insn,
				       unsigned int *data);
int i_APCI3120_InsnBitsDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				     struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_InsnWriteDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				      struct comedi_insn *insn, unsigned int *data);

/* AO */
/* int i_APCI3120_Write1AnalogValue(struct comedi_device *dev,UINT ui_Range,
 * UINT ui_Channel,UINT data );
 */

int i_APCI3120_InsnWriteAnalogOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				     struct comedi_insn *insn, unsigned int *data);

/* AI HArdware layer */

int i_APCI3120_InsnConfigAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				     struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_InsnReadAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);
int i_APCI3120_CommandTestAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				      struct comedi_cmd *cmd);
int i_APCI3120_CommandAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s);
/* int i_APCI3120_CancelAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s); */
int i_APCI3120_StopCyclicAcquisition(struct comedi_device *dev, struct comedi_subdevice *s);

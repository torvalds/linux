/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

        ADDI-DATA GmbH
        Dieselstrasse 3
        D-77833 Ottersweier
        Tel: +19(0)7223/9493-0
        Fax: +49(0)7223/9493-92
        http://www.addi-data-com
        info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-------------------------------+---------------------------------------+
  | Project     : APCI-1710       | Compiler   : GCC                      |
  | Module name : hwdrv_apci1710.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Acces For APCI-1710                    |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +----------+-----------+------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          |           |                                                |
  |          |           |                                                |
  |          |           |                                                |
  +----------+-----------+------------------------------------------------+
*/
#include "hwdrv_APCI1710.h"
#include "APCI1710_Inp_cpt.c"

#include "APCI1710_Ssi.c"
#include "APCI1710_Tor.c"
#include "APCI1710_Ttl.c"
#include "APCI1710_Dig_io.c"
#include "APCI1710_82x54.c"
#include "APCI1710_Chrono.c"
#include "APCI1710_Pwm.c"
#include "APCI1710_INCCPT.c"

void i_ADDI_AttachPCI1710(struct comedi_device * dev)
{
	struct comedi_subdevice *s;
	int ret = 0;
	int n_subdevices = 9;

	//Update-0.7.57->0.7.68dev->n_subdevices = 9;
	if ((ret = alloc_subdevices(dev, n_subdevices)) < 0)
		return;

	// Allocate and Initialise Timer Subdevice Structures
	s = dev->subdevices + 0;

	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 3;
	s->maxdata = 0;
	s->len_chanlist = 3;
	s->range_table = &range_digital;
	s->insn_write = i_APCI1710_InsnWriteEnableDisableTimer;
	s->insn_read = i_APCI1710_InsnReadAllTimerValue;
	s->insn_config = i_APCI1710_InsnConfigInitTimer;
	s->insn_bits = i_APCI1710_InsnBitsTimer;

	// Allocate and Initialise DIO Subdevice Structures
	s = dev->subdevices + 1;

	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 7;
	s->maxdata = 1;
	s->len_chanlist = 7;
	s->range_table = &range_digital;
	s->insn_config = i_APCI1710_InsnConfigDigitalIO;
	s->insn_read = i_APCI1710_InsnReadDigitalIOChlValue;
	s->insn_bits = i_APCI1710_InsnBitsDigitalIOPortOnOff;
	s->insn_write = i_APCI1710_InsnWriteDigitalIOChlOnOff;

	// Allocate and Initialise Chrono Subdevice Structures
	s = dev->subdevices + 2;

	s->type = COMEDI_SUBD_CHRONO;
	s->subdev_flags = SDF_WRITEABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 0;
	s->len_chanlist = 4;
	s->range_table = &range_digital;
	s->insn_write = i_APCI1710_InsnWriteEnableDisableChrono;
	s->insn_read = i_APCI1710_InsnReadChrono;
	s->insn_config = i_APCI1710_InsnConfigInitChrono;
	s->insn_bits = i_APCI1710_InsnBitsChronoDigitalIO;

	// Allocate and Initialise PWM Subdevice Structures
	s = dev->subdevices + 3;
	s->type = COMEDI_SUBD_PWM;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 3;
	s->maxdata = 1;
	s->len_chanlist = 3;
	s->range_table = &range_digital;
	s->io_bits = 0;		//all bits input
	s->insn_config = i_APCI1710_InsnConfigPWM;
	s->insn_read = i_APCI1710_InsnReadGetPWMStatus;
	s->insn_write = i_APCI1710_InsnWritePWM;
	s->insn_bits = i_APCI1710_InsnBitsReadPWMInterrupt;

	// Allocate and Initialise TTLIO Subdevice Structures
	s = dev->subdevices + 4;
	s->type = COMEDI_SUBD_TTLIO;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 8;
	s->maxdata = 1;
	s->len_chanlist = 8;
	s->range_table = &range_apci1710_ttl;	// to pass arguments in range
	s->insn_config = i_APCI1710_InsnConfigInitTTLIO;
	s->insn_bits = i_APCI1710_InsnBitsReadTTLIO;
	s->insn_write = i_APCI1710_InsnWriteSetTTLIOChlOnOff;
	s->insn_read = i_APCI1710_InsnReadTTLIOAllPortValue;

	// Allocate and Initialise TOR Subdevice Structures
	s = dev->subdevices + 5;
	s->type = COMEDI_SUBD_TOR;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 8;
	s->maxdata = 1;
	s->len_chanlist = 8;
	s->range_table = &range_digital;
	s->io_bits = 0;		//all bits input
	s->insn_config = i_APCI1710_InsnConfigInitTorCounter;
	s->insn_read = i_APCI1710_InsnReadGetTorCounterInitialisation;
	s->insn_write = i_APCI1710_InsnWriteEnableDisableTorCounter;
	s->insn_bits = i_APCI1710_InsnBitsGetTorCounterProgressStatusAndValue;

	// Allocate and Initialise SSI Subdevice Structures
	s = dev->subdevices + 6;
	s->type = COMEDI_SUBD_SSI;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->len_chanlist = 4;
	s->range_table = &range_apci1710_ssi;
	s->insn_config = i_APCI1710_InsnConfigInitSSI;
	s->insn_read = i_APCI1710_InsnReadSSIValue;
	s->insn_bits = i_APCI1710_InsnBitsSSIDigitalIO;

	// Allocate and Initialise PULSEENCODER Subdevice Structures
	s = dev->subdevices + 7;
	s->type = COMEDI_SUBD_PULSEENCODER;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->len_chanlist = 4;
	s->range_table = &range_digital;
	s->insn_config = i_APCI1710_InsnConfigInitPulseEncoder;
	s->insn_write = i_APCI1710_InsnWriteEnableDisablePulseEncoder;
	s->insn_bits = i_APCI1710_InsnBitsReadWritePulseEncoder;
	s->insn_read = i_APCI1710_InsnReadInterruptPulseEncoder;

	// Allocate and Initialise INCREMENTALCOUNTER Subdevice Structures
	s = dev->subdevices + 8;
	s->type = COMEDI_SUBD_INCREMENTALCOUNTER;
	s->subdev_flags =
		SDF_WRITEABLE | SDF_READABLE | SDF_RT | SDF_GROUND | SDF_COMMON;
	s->n_chan = 500;
	s->maxdata = 1;
	s->len_chanlist = 500;
	s->range_table = &range_apci1710_inccpt;
	s->insn_config = i_APCI1710_InsnConfigINCCPT;
	s->insn_write = i_APCI1710_InsnWriteINCCPT;
	s->insn_read = i_APCI1710_InsnReadINCCPT;
	s->insn_bits = i_APCI1710_InsnBitsINCCPT;
}

int i_APCI1710_Reset(struct comedi_device * dev);
void v_APCI1710_Interrupt(int irq, void *d);
//for 1710

int i_APCI1710_Reset(struct comedi_device * dev)
{
	int ret;
	unsigned int dw_Dummy;

	/*********************************/
	/* Read all module configuration */
	/*********************************/
	ret = inl(devpriv->s_BoardInfos.ui_Address + 60);
	devpriv->s_BoardInfos.dw_MolduleConfiguration[0] = ret;

	ret = inl(devpriv->s_BoardInfos.ui_Address + 124);
	devpriv->s_BoardInfos.dw_MolduleConfiguration[1] = ret;

	ret = inl(devpriv->s_BoardInfos.ui_Address + 188);
	devpriv->s_BoardInfos.dw_MolduleConfiguration[2] = ret;

	ret = inl(devpriv->s_BoardInfos.ui_Address + 252);
	devpriv->s_BoardInfos.dw_MolduleConfiguration[3] = ret;

	// outl(0x80808082,devpriv->s_BoardInfos.ui_Address+0x60);
	outl(0x83838383, devpriv->s_BoardInfos.ui_Address + 0x60);

	devpriv->s_BoardInfos.b_BoardVersion = 1;

	// Enable the interrupt for the controler
	dw_Dummy = inl(devpriv->s_BoardInfos.ui_Address + 0x38);
	outl(dw_Dummy | 0x2000, devpriv->s_BoardInfos.ui_Address + 0x38);

	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function's Name   : __void__ v_APCI1710_InterruptFunction                  |
|				(unsigned char b_Interrupt, __CPPARGS)                |
+----------------------------------------------------------------------------+
| Task              : APCI-1710 interrupt function                           |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char b_Interrupt : Interrupt number                    |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0 : OK                                                 |
|                    -1 : Error                                              |
+----------------------------------------------------------------------------+
*/

void v_APCI1710_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned char b_ModuleCpt = 0;
	unsigned char b_InterruptFlag = 0;
	unsigned char b_PWMCpt = 0;
	unsigned char b_TorCounterCpt = 0;
	unsigned char b_PulseIncoderCpt = 0;
	unsigned int ui_16BitValue;
	unsigned int ul_InterruptLatchReg = 0;
	unsigned int ul_LatchRegisterValue = 0;
	unsigned int ul_82X54InterruptStatus;
	unsigned int ul_StatusRegister;

	union str_ModuleInfo *ps_ModuleInfo;

	printk("APCI1710 Interrupt\n");
	for (b_ModuleCpt = 0; b_ModuleCpt < 4; b_ModuleCpt++, ps_ModuleInfo++) {

		 /**************************/
		/* 1199/0225 to 0100/0226 */
		 /**************************/
		ps_ModuleInfo = &devpriv->s_ModuleInfo[b_ModuleCpt];

		 /***********************/
		/* Test if 82X54 timer */
		 /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_82X54_TIMER) {

			//printk("TIMER Interrupt Occurred\n");
			ul_82X54InterruptStatus = inl(devpriv->s_BoardInfos.
				ui_Address + 12 + (64 * b_ModuleCpt));

		    /***************************/
			/* Test if interrupt occur */
		    /***************************/

			if ((ul_82X54InterruptStatus & ps_ModuleInfo->
					s_82X54ModuleInfo.
					b_InterruptMask) != 0) {
				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].
					ul_OldInterruptMask =
					(ul_82X54InterruptStatus &
					ps_ModuleInfo->s_82X54ModuleInfo.
					b_InterruptMask) << 4;

				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].
					b_OldModuleMask = 1 << b_ModuleCpt;

				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].ul_OldCounterLatchValue = 0;

				devpriv->
					s_InterruptParameters.
					ul_InterruptOccur++;

		       /****************************/
				/* Increment the write FIFO */
		       /****************************/

				devpriv->
					s_InterruptParameters.
					ui_Write = (devpriv->
					s_InterruptParameters.
					ui_Write + 1) % APCI1710_SAVE_INTERRUPT;

				b_InterruptFlag = 1;

			     /**********************/
				/* Call user function */
			     /**********************/
				//Send a signal to from kernel to user space
				send_sig(SIGIO, devpriv->tsk_Current, 0);

			}	// if ((ul_82X54InterruptStatus & 0x7) != 0)
		}		// 82X54 timer

		 /***************************/
		/* Test if increm. counter */
		 /***************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_INCREMENTAL_COUNTER) {

			ul_InterruptLatchReg = inl(devpriv->s_BoardInfos.
				ui_Address + (64 * b_ModuleCpt));

		    /*********************/
			/* Test if interrupt */
		    /*********************/

			if ((ul_InterruptLatchReg & 0x22) && (ps_ModuleInfo->
					s_SiemensCounterInfo.
					s_ModeRegister.
					s_ByteModeRegister.
					b_ModeRegister2 & 0x80)) {
		       /************************************/
				/* Test if strobe latch I interrupt */
		       /************************************/

				if (ul_InterruptLatchReg & 2) {
					ul_LatchRegisterValue =
						inl(devpriv->s_BoardInfos.
						ui_Address + 4 +
						(64 * b_ModuleCpt));

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						1UL;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue =
						ul_LatchRegisterValue;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* 0899/0224 to 1199/0225   */
			  /****************************/
					/* Increment the write FIFO */
		      /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

				/**********************/
					/* Call user function */
				/**********************/
					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}

		       /*************************************/
				/* Test if strobe latch II interrupt */
		       /*************************************/

				if (ul_InterruptLatchReg & 0x20) {

					ul_LatchRegisterValue =
						inl(devpriv->s_BoardInfos.
						ui_Address + 8 +
						(64 * b_ModuleCpt));

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						2UL;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue =
						ul_LatchRegisterValue;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* 0899/0224 to 1199/0225   */
			  /****************************/
					/* Increment the write FIFO */
			  /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

			    /**********************/
					/* Call user function */
				/**********************/
					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}
			}

			ul_InterruptLatchReg = inl(devpriv->s_BoardInfos.
				ui_Address + 24 + (64 * b_ModuleCpt));

		    /***************************/
			/* Test if index interrupt */
		    /***************************/

			if (ul_InterruptLatchReg & 0x8) {
				ps_ModuleInfo->
					s_SiemensCounterInfo.
					s_InitFlag.b_IndexInterruptOccur = 1;

				if (ps_ModuleInfo->
					s_SiemensCounterInfo.
					s_ModeRegister.
					s_ByteModeRegister.
					b_ModeRegister2 &
					APCI1710_INDEX_AUTO_MODE) {

					outl(ps_ModuleInfo->
						s_SiemensCounterInfo.
						s_ModeRegister.
						dw_ModeRegister1_2_3_4,
						devpriv->s_BoardInfos.
						ui_Address + 20 +
						(64 * b_ModuleCpt));
				}

		       /*****************************/
				/* Test if interrupt enabled */
		       /*****************************/

				if ((ps_ModuleInfo->
						s_SiemensCounterInfo.
						s_ModeRegister.
						s_ByteModeRegister.
						b_ModeRegister3 &
						APCI1710_ENABLE_INDEX_INT) ==
					APCI1710_ENABLE_INDEX_INT) {
					devpriv->s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						4UL;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue =
						ul_LatchRegisterValue;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* 0899/0224 to 1199/0225   */
			  /****************************/
					/* Increment the write FIFO */
			  /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

				/**********************/
					/* Call user function */
				/**********************/
					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}
			}

		    /*****************************/
			/* Test if compare interrupt */
		    /*****************************/

			if (ul_InterruptLatchReg & 0x10) {
		       /*****************************/
				/* Test if interrupt enabled */
		       /*****************************/

				if ((ps_ModuleInfo->
						s_SiemensCounterInfo.
						s_ModeRegister.
						s_ByteModeRegister.
						b_ModeRegister3 &
						APCI1710_ENABLE_COMPARE_INT) ==
					APCI1710_ENABLE_COMPARE_INT) {
					devpriv->s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						8UL;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue =
						ul_LatchRegisterValue;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* 0899/0224 to 1199/0225   */
			  /****************************/
					/* Increment the write FIFO */
		      /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

				/**********************/
					/* Call user function */
				/**********************/
					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}
			}

		    /*******************************************/
			/* Test if frequency measurement interrupt */
		    /*******************************************/

			if (ul_InterruptLatchReg & 0x20) {
		       /*******************/
				/* Read the status */
		       /*******************/

				ul_StatusRegister = inl(devpriv->s_BoardInfos.
					ui_Address + 32 + (64 * b_ModuleCpt));

		       /******************/
				/* Read the value */
		       /******************/

				ul_LatchRegisterValue =
					inl(devpriv->s_BoardInfos.ui_Address +
					28 + (64 * b_ModuleCpt));

				switch ((ul_StatusRegister >> 1) & 3) {
				case 0:
			       /*************************/
					/* Test the counter mode */
			       /*************************/

					if ((devpriv->s_ModuleInfo[b_ModuleCpt].
							s_SiemensCounterInfo.
							s_ModeRegister.
							s_ByteModeRegister.
							b_ModeRegister1 &
							APCI1710_16BIT_COUNTER)
						== APCI1710_16BIT_COUNTER) {
				  /****************************************/
						/* Test if 16-bit counter 1 pulse occur */
				  /****************************************/

						if ((ul_LatchRegisterValue &
								0xFFFFU) != 0) {
							ui_16BitValue =
								(unsigned int)
								ul_LatchRegisterValue
								& 0xFFFFU;
							ul_LatchRegisterValue =
								(ul_LatchRegisterValue
								& 0xFFFF0000UL)
								| (0xFFFFU -
								ui_16BitValue);
						}

				  /****************************************/
						/* Test if 16-bit counter 2 pulse occur */
				  /****************************************/

						if ((ul_LatchRegisterValue &
								0xFFFF0000UL) !=
							0) {
							ui_16BitValue =
								(unsigned int) (
								(ul_LatchRegisterValue
									>> 16) &
								0xFFFFU);
							ul_LatchRegisterValue =
								(ul_LatchRegisterValue
								& 0xFFFFUL) |
								((0xFFFFU -
									ui_16BitValue)
								<< 16);
						}
					} else {
						if (ul_LatchRegisterValue != 0) {
							ul_LatchRegisterValue =
								0xFFFFFFFFUL -
								ul_LatchRegisterValue;
						}
					}
					break;

				case 1:
			       /****************************************/
					/* Test if 16-bit counter 2 pulse occur */
			       /****************************************/

					if ((ul_LatchRegisterValue &
							0xFFFF0000UL) != 0) {
						ui_16BitValue =
							(unsigned int) (
							(ul_LatchRegisterValue
								>> 16) &
							0xFFFFU);
						ul_LatchRegisterValue =
							(ul_LatchRegisterValue &
							0xFFFFUL) | ((0xFFFFU -
								ui_16BitValue)
							<< 16);
					}
					break;

				case 2:
			       /****************************************/
					/* Test if 16-bit counter 1 pulse occur */
			       /****************************************/

					if ((ul_LatchRegisterValue & 0xFFFFU) !=
						0) {
						ui_16BitValue =
							(unsigned int)
							ul_LatchRegisterValue &
							0xFFFFU;
						ul_LatchRegisterValue =
							(ul_LatchRegisterValue &
							0xFFFF0000UL) | (0xFFFFU
							- ui_16BitValue);
					}
					break;
				}

				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].
					ul_OldInterruptMask = 0x10000UL;

				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].
					b_OldModuleMask = 1 << b_ModuleCpt;

				devpriv->
					s_InterruptParameters.
					s_FIFOInterruptParameters[devpriv->
					s_InterruptParameters.
					ui_Write].
					ul_OldCounterLatchValue =
					ul_LatchRegisterValue;

				devpriv->
					s_InterruptParameters.
					ul_InterruptOccur++;

		       /****************************/
				/* 0899/0224 to 1199/0225   */
		       /****************************/
				/* Increment the write FIFO */
		       /****************************/

				devpriv->
					s_InterruptParameters.
					ui_Write = (devpriv->
					s_InterruptParameters.
					ui_Write + 1) % APCI1710_SAVE_INTERRUPT;

				b_InterruptFlag = 1;

			     /**********************/
				/* Call user function */
			     /**********************/
				//Send a signal to from kernel to user space
				send_sig(SIGIO, devpriv->tsk_Current, 0);

			}
		}		// Incremental counter

		 /***************/
		/* Test if CDA */
		 /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_CDA) {
		    /******************************************/
			/* Test if CDA enable and functionality 0 */
		    /******************************************/

			if ((devpriv->s_ModuleInfo[b_ModuleCpt].
					s_CDAModuleInfo.
					b_CDAEnable == APCI1710_ENABLE)
				&& (devpriv->s_ModuleInfo[b_ModuleCpt].
					s_CDAModuleInfo.b_FctSelection == 0)) {
		       /****************************/
				/* Get the interrupt status */
		       /****************************/

				ul_StatusRegister = inl(devpriv->s_BoardInfos.
					ui_Address + 16 + (64 * b_ModuleCpt));
		       /***************************/
				/* Test if interrupt occur */
		       /***************************/

				if (ul_StatusRegister & 1) {
					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						0x80000UL;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue = 0;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* Increment the write FIFO */
			  /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

				/**********************/
					/* Call user function */
				/**********************/

					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}	// if (ul_StatusRegister & 1)

			}
		}		// CDA

		 /***********************/
		/* Test if PWM counter */
		 /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_PWM) {
			for (b_PWMCpt = 0; b_PWMCpt < 2; b_PWMCpt++) {
		       /*************************************/
				/* Test if PWM interrupt initialised */
		       /*************************************/

				if (devpriv->
					s_ModuleInfo[b_ModuleCpt].
					s_PWMModuleInfo.
					s_PWMInfo[b_PWMCpt].
					b_InterruptEnable == APCI1710_ENABLE) {
			  /*****************************/
					/* Read the interrupt status */
			  /*****************************/

					ul_StatusRegister =
						inl(devpriv->s_BoardInfos.
						ui_Address + 16 +
						(20 * b_PWMCpt) +
						(64 * b_ModuleCpt));

			  /***************************/
					/* Test if interrupt occur */
			  /***************************/

					if (ul_StatusRegister & 0x1) {
						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							ul_OldInterruptMask =
							0x4000UL << b_PWMCpt;

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							b_OldModuleMask =
							1 << b_ModuleCpt;

						devpriv->
							s_InterruptParameters.
							ul_InterruptOccur++;

			     /****************************/
						/* Increment the write FIFO */
			     /****************************/

						devpriv->
							s_InterruptParameters.
							ui_Write = (devpriv->
							s_InterruptParameters.
							ui_Write +
							1) %
							APCI1710_SAVE_INTERRUPT;

						b_InterruptFlag = 1;

				   /**********************/
						/* Call user function */
				   /**********************/
						//Send a signal to from kernel to user space
						send_sig(SIGIO,
							devpriv->tsk_Current,
							0);

					}	// if (ul_StatusRegister & 0x1)
				}	// if (APCI1710_ENABLE)
			}	// for (b_PWMCpt == 0; b_PWMCpt < 0; b_PWMCpt ++)
		}		// PWM counter

		 /***********************/
		/* Test if tor counter */
		 /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_TOR_COUNTER) {
			for (b_TorCounterCpt = 0; b_TorCounterCpt < 2;
				b_TorCounterCpt++) {
		       /*************************************/
				/* Test if tor interrupt initialised */
		       /*************************************/

				if (devpriv->
					s_ModuleInfo[b_ModuleCpt].
					s_TorCounterModuleInfo.
					s_TorCounterInfo[b_TorCounterCpt].
					b_InterruptEnable == APCI1710_ENABLE) {
			  /*****************************/
					/* Read the interrupt status */
			  /*****************************/

					ul_StatusRegister =
						inl(devpriv->s_BoardInfos.
						ui_Address + 12 +
						(16 * b_TorCounterCpt) +
						(64 * b_ModuleCpt));

			  /***************************/
					/* Test if interrupt occur */
			  /***************************/

					if (ul_StatusRegister & 0x1) {
			     /******************************/
						/* Read the tor counter value */
			     /******************************/

						ul_LatchRegisterValue =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 0 +
							(16 * b_TorCounterCpt) +
							(64 * b_ModuleCpt));

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							ul_OldInterruptMask =
							0x1000UL <<
							b_TorCounterCpt;

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							b_OldModuleMask =
							1 << b_ModuleCpt;

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							ul_OldCounterLatchValue
							= ul_LatchRegisterValue;

						devpriv->
							s_InterruptParameters.
							ul_InterruptOccur++;

			     /****************************/
						/* Increment the write FIFO */
			     /****************************/

						devpriv->
							s_InterruptParameters.
							ui_Write = (devpriv->
							s_InterruptParameters.
							ui_Write +
							1) %
							APCI1710_SAVE_INTERRUPT;

						b_InterruptFlag = 1;

				   /**********************/
						/* Call user function */
				   /**********************/

						//Send a signal to from kernel to user space
						send_sig(SIGIO,
							devpriv->tsk_Current,
							0);
					}	// if (ul_StatusRegister & 0x1)
				}	// if (APCI1710_ENABLE)
			}	// for (b_TorCounterCpt == 0; b_TorCounterCpt < 0; b_TorCounterCpt ++)
		}		// Tor counter

		 /***********************/
		/* Test if chronometer */
		 /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {

			//printk("APCI1710 Chrono Interrupt\n");
		    /*****************************/
			/* Read the interrupt status */
		    /*****************************/

			ul_InterruptLatchReg = inl(devpriv->s_BoardInfos.
				ui_Address + 12 + (64 * b_ModuleCpt));

		    /***************************/
			/* Test if interrupt occur */
		    /***************************/

			if ((ul_InterruptLatchReg & 0x8) == 0x8) {
		       /****************************/
				/* Clear the interrupt flag */
		       /****************************/

				outl(0, devpriv->s_BoardInfos.
					ui_Address + 32 + (64 * b_ModuleCpt));

		       /***************************/
				/* Test if continuous mode */
		       /***************************/

				if (ps_ModuleInfo->
					s_ChronoModuleInfo.
					b_CycleMode == APCI1710_ENABLE) {
			  /********************/
					/* Clear the status */
			  /********************/

					outl(0, devpriv->s_BoardInfos.
						ui_Address + 36 +
						(64 * b_ModuleCpt));
				}

		       /*************************/
				/* Read the timing value */
		       /*************************/

				ul_LatchRegisterValue =
					inl(devpriv->s_BoardInfos.ui_Address +
					4 + (64 * b_ModuleCpt));

		       /*****************************/
				/* Test if interrupt enabled */
		       /*****************************/

				if (ps_ModuleInfo->
					s_ChronoModuleInfo.b_InterruptMask) {
					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].ul_OldInterruptMask =
						0x80;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].b_OldModuleMask =
						1 << b_ModuleCpt;

					devpriv->
						s_InterruptParameters.
						s_FIFOInterruptParameters
						[devpriv->s_InterruptParameters.
						ui_Write].
						ul_OldCounterLatchValue =
						ul_LatchRegisterValue;

					devpriv->
						s_InterruptParameters.
						ul_InterruptOccur++;

			  /****************************/
					/* Increment the write FIFO */
		      /****************************/

					devpriv->
						s_InterruptParameters.
						ui_Write = (devpriv->
						s_InterruptParameters.
						ui_Write +
						1) % APCI1710_SAVE_INTERRUPT;

					b_InterruptFlag = 1;

				/**********************/
					/* Call user function */
				/**********************/
					//Send a signal to from kernel to user space
					send_sig(SIGIO, devpriv->tsk_Current,
						0);

				}
			}
		}		// Chronometer

		 /*************************/
		/* Test if pulse encoder */
		 /*************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModuleCpt] &
				0xFFFF0000UL) == APCI1710_PULSE_ENCODER) {
		    /****************************/
			/* Read the status register */
		    /****************************/

			ul_StatusRegister = inl(devpriv->s_BoardInfos.
				ui_Address + 20 + (64 * b_ModuleCpt));

			if (ul_StatusRegister & 0xF) {
				for (b_PulseIncoderCpt = 0;
					b_PulseIncoderCpt < 4;
					b_PulseIncoderCpt++) {
			  /*************************************/
					/* Test if pulse encoder initialised */
			  /*************************************/

					if ((ps_ModuleInfo->
							s_PulseEncoderModuleInfo.
							s_PulseEncoderInfo
							[b_PulseIncoderCpt].
							b_PulseEncoderInit == 1)
						&& (((ps_ModuleInfo->s_PulseEncoderModuleInfo.dw_SetRegister >> b_PulseIncoderCpt) & 1) == 1) && (((ul_StatusRegister >> (b_PulseIncoderCpt)) & 1) == 1)) {
						devpriv->s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							ul_OldInterruptMask =
							0x100UL <<
							b_PulseIncoderCpt;

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							b_OldModuleMask =
							1 << b_ModuleCpt;

						devpriv->
							s_InterruptParameters.
							s_FIFOInterruptParameters
							[devpriv->
							s_InterruptParameters.
							ui_Write].
							ul_OldCounterLatchValue
							= ul_LatchRegisterValue;

						devpriv->
							s_InterruptParameters.
							ul_InterruptOccur++;

			     /****************************/
						/* 0899/0224 to 1199/0225   */
			     /****************************/
						/* Increment the write FIFO */
			     /****************************/

						devpriv->
							s_InterruptParameters.
							ui_Write = (devpriv->
							s_InterruptParameters.
							ui_Write +
							1) %
							APCI1710_SAVE_INTERRUPT;

						b_InterruptFlag = 1;

				   /**********************/
						/* Call user function */
				   /**********************/
						//Send a signal to from kernel to user space
						send_sig(SIGIO,
							devpriv->tsk_Current,
							0);

					}
				}
			}
		}		//pulse encoder

	}
	return;

}

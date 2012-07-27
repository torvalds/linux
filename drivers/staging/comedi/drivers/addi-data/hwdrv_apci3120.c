/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You should also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : APCI-3120       | Compiler   : GCC                      |
  | Module name : hwdrv_apci3120.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-----------------------------------------------------------------------+
  | Description :APCI3120 Module.  Hardware abstraction Layer for APCI3120|
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          | 		 | 						  |
  |          |           |						  |
  +----------+-----------+------------------------------------------------+
*/

#include "hwdrv_apci3120.h"
static unsigned int ui_Temp;

/* FUNCTION DEFINITIONS */

/*
+----------------------------------------------------------------------------+
|                           ANALOG INPUT SUBDEVICE   		                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnConfigAnalogInput(struct comedi_device *dev,|
|  struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)					 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : Calls card specific function  					     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data      					         		 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnConfigAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned int i;

	if ((data[0] != APCI3120_EOC_MODE) && (data[0] != APCI3120_EOS_MODE))
		return -1;

	/*  Check for Conversion time to be added ?? */
	devpriv->ui_EocEosConversionTime = data[2];

	if (data[0] == APCI3120_EOS_MODE) {

		/* Test the number of the channel */
		for (i = 0; i < data[3]; i++) {

			if (CR_CHAN(data[4 + i]) >=
				devpriv->s_EeParameters.i_NbrAiChannel) {
				printk("bad channel list\n");
				return -2;
			}
		}

		devpriv->b_InterruptMode = APCI3120_EOS_MODE;

		if (data[1])
			devpriv->b_EocEosInterrupt = APCI3120_ENABLE;
		else
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
		/*  Copy channel list and Range List to devpriv */

		devpriv->ui_AiNbrofChannels = data[3];
		for (i = 0; i < devpriv->ui_AiNbrofChannels; i++)
			devpriv->ui_AiChannelList[i] = data[4 + i];

	} else {			/*  EOC */
		devpriv->b_InterruptMode = APCI3120_EOC_MODE;
		if (data[1])
			devpriv->b_EocEosInterrupt = APCI3120_ENABLE;
		else
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
	}

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnReadAnalogInput(struct comedi_device *dev,  |
|			struct comedi_subdevice *s,struct comedi_insn *insn, unsigned int *data)	 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              :  card specific function								 |
|				Reads analog input in synchronous mode               |
|			  EOC and EOS is selected as per configured              |
|                     if no conversion time is set uses default conversion   |
|			  time 10 microsec.					      				 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data     									 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnReadAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned short us_ConvertTiming, us_TmpValue, i;
	unsigned char b_Tmp;

	/*  fix conversion time to 10 us */
	if (!devpriv->ui_EocEosConversionTime) {
		printk("No timer0 Value using 10 us\n");
		us_ConvertTiming = 10;
	} else
		us_ConvertTiming = (unsigned short) (devpriv->ui_EocEosConversionTime / 1000);	/*  nano to useconds */

	/*  this_board->ai_read(dev,us_ConvertTiming,insn->n,&insn->chanspec,data,insn->unused[0]); */

	/*  Clear software registers */
	devpriv->b_TimerSelectMode = 0;
	devpriv->b_ModeSelectRegister = 0;
	devpriv->us_OutputRegister = 0;
/* devpriv->b_DigitalOutputRegister=0; */

	if (insn->unused[0] == 222) {	/*  second insn read */
		for (i = 0; i < insn->n; i++)
			data[i] = devpriv->ui_AiReadData[i];
	} else {
		devpriv->tsk_Current = current;	/*  Save the current process task structure */
/*
 * Testing if board have the new Quartz and calculate the time value
 * to set in the timer
 */

		us_TmpValue =
			(unsigned short) inw(devpriv->iobase + APCI3120_RD_STATUS);

		/* EL250804: Testing if board APCI3120 have the new Quartz or if it is an APCI3001 */
		if ((us_TmpValue & 0x00B0) == 0x00B0
			|| !strcmp(this_board->pc_DriverName, "apci3001")) {
			us_ConvertTiming = (us_ConvertTiming * 2) - 2;
		} else {
			us_ConvertTiming =
				((us_ConvertTiming * 12926) / 10000) - 1;
		}

		us_TmpValue = (unsigned short) devpriv->b_InterruptMode;

		switch (us_TmpValue) {

		case APCI3120_EOC_MODE:

/*
 * Testing the interrupt flag and set the EOC bit Clears the FIFO
 */
			inw(devpriv->iobase + APCI3120_RESET_FIFO);

			/*  Initialize the sequence array */

			/* if (!i_APCI3120_SetupChannelList(dev,s,1,chanlist,0))  return -EINVAL; */

			if (!i_APCI3120_SetupChannelList(dev, s, 1,
					&insn->chanspec, 0))
				return -EINVAL;

			/* Initialize Timer 0 mode 4 */
			devpriv->b_TimerSelectMode =
				(devpriv->
				b_TimerSelectMode & 0xFC) |
				APCI3120_TIMER_0_MODE_4;
			outb(devpriv->b_TimerSelectMode,
				devpriv->iobase + APCI3120_TIMER_CRT1);

			/*  Reset the scan bit and Disables the  EOS, DMA, EOC interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_SCAN;

			if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {

				/* Disables the EOS,DMA and enables the EOC interrupt */
				devpriv->b_ModeSelectRegister =
					(devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_EOS_INT) |
					APCI3120_ENABLE_EOC_INT;
				inw(devpriv->iobase);

			} else {
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER;
			}

			outb(devpriv->b_ModeSelectRegister,
				devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

			/*  Sets gate 0 */
			devpriv->us_OutputRegister =
				(devpriv->
				us_OutputRegister & APCI3120_CLEAR_PA_PR) |
				APCI3120_ENABLE_TIMER0;
			outw(devpriv->us_OutputRegister,
				devpriv->iobase + APCI3120_WR_ADDRESS);

			/*  Select Timer 0 */
			b_Tmp = ((devpriv->
					b_DigitalOutputRegister) & 0xF0) |
				APCI3120_SELECT_TIMER_0_WORD;
			outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

			/* Set the conversion time */
			outw(us_ConvertTiming,
				devpriv->iobase + APCI3120_TIMER_VALUE);

			us_TmpValue =
				(unsigned short) inw(dev->iobase + APCI3120_RD_STATUS);

			if (devpriv->b_EocEosInterrupt == APCI3120_DISABLE) {

				do {
					/*  Waiting for the end of conversion */
					us_TmpValue =
						inw(devpriv->iobase +
						APCI3120_RD_STATUS);
				} while ((us_TmpValue & APCI3120_EOC) ==
					APCI3120_EOC);

				/* Read the result in FIFO  and put it in insn data pointer */
				us_TmpValue = inw(devpriv->iobase + 0);
				*data = us_TmpValue;

				inw(devpriv->iobase + APCI3120_RESET_FIFO);
			}

			break;

		case APCI3120_EOS_MODE:

			inw(devpriv->iobase);
			/*  Clears the FIFO */
			inw(devpriv->iobase + APCI3120_RESET_FIFO);
			/*  clear PA PR  and disable timer 0 */

			devpriv->us_OutputRegister =
				(devpriv->
				us_OutputRegister & APCI3120_CLEAR_PA_PR) |
				APCI3120_DISABLE_TIMER0;

			outw(devpriv->us_OutputRegister,
				devpriv->iobase + APCI3120_WR_ADDRESS);

			if (!i_APCI3120_SetupChannelList(dev, s,
					devpriv->ui_AiNbrofChannels,
					devpriv->ui_AiChannelList, 0))
				return -EINVAL;

			/* Initialize Timer 0 mode 2 */
			devpriv->b_TimerSelectMode =
				(devpriv->
				b_TimerSelectMode & 0xFC) |
				APCI3120_TIMER_0_MODE_2;
			outb(devpriv->b_TimerSelectMode,
				devpriv->iobase + APCI3120_TIMER_CRT1);

			/* Select Timer 0 */
			b_Tmp = ((devpriv->
					b_DigitalOutputRegister) & 0xF0) |
				APCI3120_SELECT_TIMER_0_WORD;
			outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

			/* Set the conversion time */
			outw(us_ConvertTiming,
				devpriv->iobase + APCI3120_TIMER_VALUE);

			/* Set the scan bit */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister | APCI3120_ENABLE_SCAN;
			outb(devpriv->b_ModeSelectRegister,
				devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

			/* If Interrupt function is loaded */
			if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {
				/* Disables the EOC,DMA and enables the EOS interrupt */
				devpriv->b_ModeSelectRegister =
					(devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_EOC_INT) |
					APCI3120_ENABLE_EOS_INT;
				inw(devpriv->iobase);

			} else
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister &
					APCI3120_DISABLE_ALL_INTERRUPT_WITHOUT_TIMER;

			outb(devpriv->b_ModeSelectRegister,
				devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

			inw(devpriv->iobase + APCI3120_RD_STATUS);

			/* Sets gate 0 */

			devpriv->us_OutputRegister =
				devpriv->
				us_OutputRegister | APCI3120_ENABLE_TIMER0;
			outw(devpriv->us_OutputRegister,
				devpriv->iobase + APCI3120_WR_ADDRESS);

			/* Start conversion */
			outw(0, devpriv->iobase + APCI3120_START_CONVERSION);

			/* Waiting of end of conversion if interrupt is not installed */
			if (devpriv->b_EocEosInterrupt == APCI3120_DISABLE) {
				/* Waiting the end of conversion */
				do {
					us_TmpValue =
						inw(devpriv->iobase +
						APCI3120_RD_STATUS);
				} while ((us_TmpValue & APCI3120_EOS) !=
					 APCI3120_EOS);

				for (i = 0; i < devpriv->ui_AiNbrofChannels;
					i++) {
					/* Read the result in FIFO and write them in shared memory */
					us_TmpValue = inw(devpriv->iobase);
					data[i] = (unsigned int) us_TmpValue;
				}

				devpriv->b_InterruptMode = APCI3120_EOC_MODE;	/*  Restore defaults. */
			}
			break;

		default:
			printk("inputs wrong\n");

		}
		devpriv->ui_EocEosConversionTime = 0;	/*  re initializing the variable; */
	}

	return insn->n;

}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_StopCyclicAcquisition(struct comedi_device *dev,|
| 											     struct comedi_subdevice *s)|
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Stops Cyclic acquisition  						     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :0              					                     |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_StopCyclicAcquisition(struct comedi_device *dev, struct comedi_subdevice *s)
{
	/*  Disable A2P Fifo write and AMWEN signal */
	outw(0, devpriv->i_IobaseAddon + 4);

	/* Disable Bus Master ADD ON */
	outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->i_IobaseAddon + 0);
	outw(0, devpriv->i_IobaseAddon + 2);
	outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->i_IobaseAddon + 0);
	outw(0, devpriv->i_IobaseAddon + 2);

	/* Disable BUS Master PCI */
	outl(0, devpriv->i_IobaseAmcc + AMCC_OP_REG_MCSR);

	/* outl(inl(devpriv->i_IobaseAmcc+AMCC_OP_REG_INTCSR)&(~AINT_WRITE_COMPL),
	 * devpriv->i_IobaseAmcc+AMCC_OP_REG_INTCSR);  stop amcc irqs */

	/* outl(inl(devpriv->i_IobaseAmcc+AMCC_OP_REG_MCSR)&(~EN_A2P_TRANSFERS),
	 * devpriv->i_IobaseAmcc+AMCC_OP_REG_MCSR);  stop DMA */

	/* Disable ext trigger */
	i_APCI3120_ExttrigDisable(dev);

	devpriv->us_OutputRegister = 0;
	/* stop  counters */
	outw(devpriv->
		us_OutputRegister & APCI3120_DISABLE_TIMER0 &
		APCI3120_DISABLE_TIMER1, dev->iobase + APCI3120_WR_ADDRESS);

	outw(APCI3120_DISABLE_ALL_TIMER, dev->iobase + APCI3120_WR_ADDRESS);

	/* DISABLE_ALL_INTERRUPT */
	outb(APCI3120_DISABLE_ALL_INTERRUPT,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);
	/* Flush FIFO */
	inb(dev->iobase + APCI3120_RESET_FIFO);
	inw(dev->iobase + APCI3120_RD_STATUS);
	devpriv->ui_AiActualScan = 0;
	devpriv->ui_AiActualScanPosition = 0;
	s->async->cur_chan = 0;
	devpriv->ui_AiBufferPtr = 0;
	devpriv->b_AiContinuous = 0;
	devpriv->ui_DmaActualBuffer = 0;

	devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;
	devpriv->b_InterruptMode = APCI3120_EOC_MODE;
	devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
	i_APCI3120_Reset(dev);
	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_CommandTestAnalogInput(struct comedi_device *dev|
|			,struct comedi_subdevice *s,struct comedi_cmd *cmd)					 |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Test validity for a command for cyclic anlog input     |
|                       acquisition  						     			 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_cmd *cmd              					         |
+----------------------------------------------------------------------------+
| Return Value      :0              					                     |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_CommandTestAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;		/*  divisor1,divisor2; */

	/*  step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_EXT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_FOLLOW;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible */

	if (cmd->start_src != TRIG_NOW && cmd->start_src != TRIG_EXT)
		err++;

	if (cmd->scan_begin_src != TRIG_TIMER &&
		cmd->scan_begin_src != TRIG_FOLLOW)
		err++;

	if (cmd->convert_src != TRIG_TIMER)
		err++;

	if (cmd->scan_end_src != TRIG_COUNT) {
		cmd->scan_end_src = TRIG_COUNT;
		err++;
	}

	if (cmd->stop_src != TRIG_NONE && cmd->stop_src != TRIG_COUNT)
		err++;

	if (err)
		return 2;

	/*  step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {	/*  Test Delay timing */
		if (cmd->scan_begin_arg <
				devpriv->s_EeParameters.ui_MinDelaytimeNs) {
			cmd->scan_begin_arg =
				devpriv->s_EeParameters.ui_MinDelaytimeNs;
			err++;
		}
	}

	if (cmd->convert_src == TRIG_TIMER) {	/*  Test Acquisition timing */
		if (cmd->scan_begin_src == TRIG_TIMER) {
			if ((cmd->convert_arg)
				&& (cmd->convert_arg <
					devpriv->s_EeParameters.
						ui_MinAcquisitiontimeNs)) {
				cmd->convert_arg = devpriv->s_EeParameters.
					ui_MinAcquisitiontimeNs;
				err++;
			}
		} else {
			if (cmd->convert_arg <
				devpriv->s_EeParameters.ui_MinAcquisitiontimeNs
				) {
				cmd->convert_arg = devpriv->s_EeParameters.
					ui_MinAcquisitiontimeNs;
				err++;

			}
		}
	}

	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 1;
		err++;
	}
	if (cmd->chanlist_len > this_board->i_AiChannelList) {
		cmd->chanlist_len = this_board->i_AiChannelList;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
	} else {		/*  TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	/*  step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {

		if (cmd->scan_begin_src == TRIG_TIMER &&
			cmd->scan_begin_arg <
			cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
				cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function name     : int i_APCI3120_CommandAnalogInput(struct comedi_device *dev,  |
|												struct comedi_subdevice *s) |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Does asynchronous acquisition                          |
|                     Determines the mode 1 or 2.						     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     														 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_CommandAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	/* loading private structure with cmd structure inputs */
	devpriv->ui_AiFlags = cmd->flags;
	devpriv->ui_AiNbrofChannels = cmd->chanlist_len;
	devpriv->ui_AiScanLength = cmd->scan_end_arg;
	devpriv->pui_AiChannelList = cmd->chanlist;

	/* UPDATE-0.7.57->0.7.68devpriv->AiData=s->async->data; */
	devpriv->AiData = s->async->prealloc_buf;
	/* UPDATE-0.7.57->0.7.68devpriv->ui_AiDataLength=s->async->data_len; */
	devpriv->ui_AiDataLength = s->async->prealloc_bufsz;

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->ui_AiNbrofScans = cmd->stop_arg;
	else
		devpriv->ui_AiNbrofScans = 0;

	devpriv->ui_AiTimer0 = 0;	/*  variables changed to timer0,timer1 */
	devpriv->ui_AiTimer1 = 0;
	if ((devpriv->ui_AiNbrofScans == 0) || (devpriv->ui_AiNbrofScans == -1))
		devpriv->b_AiContinuous = 1;	/*  user want neverending analog acquisition */
	/*  stopped using cancel */

	if (cmd->start_src == TRIG_EXT)
		devpriv->b_ExttrigEnable = APCI3120_ENABLE;
	else
		devpriv->b_ExttrigEnable = APCI3120_DISABLE;

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/*  mode 1 or 3 */
		if (cmd->convert_src == TRIG_TIMER) {
			/*  mode 1 */

			devpriv->ui_AiTimer0 = cmd->convert_arg;	/*  timer constant in nano seconds */
			/* return this_board->ai_cmd(1,dev,s); */
			return i_APCI3120_CyclicAnalogInput(1, dev, s);
		}

	}
	if ((cmd->scan_begin_src == TRIG_TIMER)
		&& (cmd->convert_src == TRIG_TIMER)) {
		/*  mode 2 */
		devpriv->ui_AiTimer1 = cmd->scan_begin_arg;
		devpriv->ui_AiTimer0 = cmd->convert_arg;	/*  variable changed timer2 to timer0 */
		/* return this_board->ai_cmd(2,dev,s); */
		return i_APCI3120_CyclicAnalogInput(2, dev, s);
	}
	return -1;
}

/*
+----------------------------------------------------------------------------+
| Function name     :  int i_APCI3120_CyclicAnalogInput(int mode,            |
|		 	   struct comedi_device * dev,struct comedi_subdevice * s)			 |
+----------------------------------------------------------------------------+
| Task              : This is used for analog input cyclic acquisition       |
|			  Performs the command operations.                       |
|			  If DMA is configured does DMA initialization           |
|			  otherwise does the acquisition with EOS interrupt.     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 														 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_CyclicAnalogInput(int mode, struct comedi_device *dev,
	struct comedi_subdevice *s)
{
	unsigned char b_Tmp;
	unsigned int ui_Tmp, ui_DelayTiming = 0, ui_TimerValue1 = 0, dmalen0 =
		0, dmalen1 = 0, ui_TimerValue2 =
		0, ui_TimerValue0, ui_ConvertTiming;
	unsigned short us_TmpValue;

	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	/* devpriv->b_AiCyclicAcquisition=APCI3120_ENABLE; */
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	/*******************/
	/* Resets the FIFO */
	/*******************/
	inb(dev->iobase + APCI3120_RESET_FIFO);

	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	/* inw(dev->iobase+APCI3120_RD_STATUS); */
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	/***************************/
	/* Acquisition initialized */
	/***************************/
	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	devpriv->b_AiCyclicAcquisition = APCI3120_ENABLE;
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	/*  clear software  registers */
	devpriv->b_TimerSelectMode = 0;
	devpriv->us_OutputRegister = 0;
	devpriv->b_ModeSelectRegister = 0;
	/* devpriv->b_DigitalOutputRegister=0; */

	/* COMMENT JK 07.05.04: Followings calls are in i_APCI3120_StartAnalogInputAcquisition */

	/****************************/
	/* Clear Timer Write TC int */
	/****************************/
	outl(APCI3120_CLEAR_WRITE_TC_INT,
		devpriv->i_IobaseAmcc + APCI3120_AMCC_OP_REG_INTCSR);

	/************************************/
	/* Clears the timer status register */
	/************************************/

	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	/* inw(dev->iobase+APCI3120_TIMER_STATUS_REGISTER); */
	/* inb(dev->iobase + APCI3120_TIMER_STATUS_REGISTER); */
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	/**************************/
	/* Disables All Timer     */
	/* Sets PR and PA to 0    */
	/**************************/
	devpriv->us_OutputRegister = devpriv->us_OutputRegister &
		APCI3120_DISABLE_TIMER0 &
		APCI3120_DISABLE_TIMER1 & APCI3120_CLEAR_PA_PR;

	outw(devpriv->us_OutputRegister, dev->iobase + APCI3120_WR_ADDRESS);

	/*******************/
	/* Resets the FIFO */
	/*******************/
	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	inb(devpriv->iobase + APCI3120_RESET_FIFO);
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	devpriv->ui_AiActualScan = 0;
	devpriv->ui_AiActualScanPosition = 0;
	s->async->cur_chan = 0;
	devpriv->ui_AiBufferPtr = 0;
	devpriv->ui_DmaActualBuffer = 0;

	/*  value for timer2  minus -2 has to be done .....dunno y?? */
	ui_TimerValue2 = devpriv->ui_AiNbrofScans - 2;
	ui_ConvertTiming = devpriv->ui_AiTimer0;

	if (mode == 2)
		ui_DelayTiming = devpriv->ui_AiTimer1;

   /**********************************/
	/* Initializes the sequence array */
   /**********************************/
	if (!i_APCI3120_SetupChannelList(dev, s, devpriv->ui_AiNbrofChannels,
			devpriv->pui_AiChannelList, 0))
		return -EINVAL;

	us_TmpValue = (unsigned short) inw(dev->iobase + APCI3120_RD_STATUS);
/*** EL241003 : add this section in comment because floats must not be used
	if((us_TmpValue & 0x00B0)==0x00B0)
	 {
		f_ConvertValue=(((float)ui_ConvertTiming * 0.002) - 2);
		ui_TimerValue0=(unsigned int)f_ConvertValue;
		if (mode==2)
		{
			f_DelayValue     = (((float)ui_DelayTiming * 0.00002) - 2);
			ui_TimerValue1  =   (unsigned int) f_DelayValue;
		}
	 }
	else
	 {
		f_ConvertValue=(((float)ui_ConvertTiming * 0.0012926) - 1);
		ui_TimerValue0=(unsigned int)f_ConvertValue;
		if (mode == 2)
		{
		     f_DelayValue     = (((float)ui_DelayTiming * 0.000012926) - 1);
		     ui_TimerValue1  =   (unsigned int) f_DelayValue;
		}
	}
***********************************************************************************************/
/*** EL241003 Begin : add this section to replace floats calculation by integer calculations **/
	/* EL250804: Testing if board APCI3120 have the new Quartz or if it is an APCI3001 */
	if ((us_TmpValue & 0x00B0) == 0x00B0
		|| !strcmp(this_board->pc_DriverName, "apci3001")) {
		ui_TimerValue0 = ui_ConvertTiming * 2 - 2000;
		ui_TimerValue0 = ui_TimerValue0 / 1000;

		if (mode == 2) {
			ui_DelayTiming = ui_DelayTiming / 1000;
			ui_TimerValue1 = ui_DelayTiming * 2 - 200;
			ui_TimerValue1 = ui_TimerValue1 / 100;
		}
	} else {
		ui_ConvertTiming = ui_ConvertTiming / 1000;
		ui_TimerValue0 = ui_ConvertTiming * 12926 - 10000;
		ui_TimerValue0 = ui_TimerValue0 / 10000;

		if (mode == 2) {
			ui_DelayTiming = ui_DelayTiming / 1000;
			ui_TimerValue1 = ui_DelayTiming * 12926 - 1;
			ui_TimerValue1 = ui_TimerValue1 / 1000000;
		}
	}
/*** EL241003 End ******************************************************************************/

	if (devpriv->b_ExttrigEnable == APCI3120_ENABLE)
		i_APCI3120_ExttrigEnable(dev);	/*  activate EXT trigger */
	switch (mode) {
	case 1:
		/*  init timer0 in mode 2 */
		devpriv->b_TimerSelectMode =
			(devpriv->
			b_TimerSelectMode & 0xFC) | APCI3120_TIMER_0_MODE_2;
		outb(devpriv->b_TimerSelectMode,
			dev->iobase + APCI3120_TIMER_CRT1);

		/* Select Timer 0 */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_0_WORD;
		outb(b_Tmp, dev->iobase + APCI3120_TIMER_CRT0);
		/* Set the conversion time */
		outw(((unsigned short) ui_TimerValue0),
			dev->iobase + APCI3120_TIMER_VALUE);
		break;

	case 2:
		/*  init timer1 in mode 2 */
		devpriv->b_TimerSelectMode =
			(devpriv->
			b_TimerSelectMode & 0xF3) | APCI3120_TIMER_1_MODE_2;
		outb(devpriv->b_TimerSelectMode,
			dev->iobase + APCI3120_TIMER_CRT1);

		/* Select Timer 1 */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_1_WORD;
		outb(b_Tmp, dev->iobase + APCI3120_TIMER_CRT0);
		/* Set the conversion time */
		outw(((unsigned short) ui_TimerValue1),
			dev->iobase + APCI3120_TIMER_VALUE);

		/*  init timer0 in mode 2 */
		devpriv->b_TimerSelectMode =
			(devpriv->
			b_TimerSelectMode & 0xFC) | APCI3120_TIMER_0_MODE_2;
		outb(devpriv->b_TimerSelectMode,
			dev->iobase + APCI3120_TIMER_CRT1);

		/* Select Timer 0 */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_0_WORD;
		outb(b_Tmp, dev->iobase + APCI3120_TIMER_CRT0);

		/* Set the conversion time */
		outw(((unsigned short) ui_TimerValue0),
			dev->iobase + APCI3120_TIMER_VALUE);
		break;

	}
	/*    ##########common for all modes################# */

	/***********************/
	/* Clears the SCAN bit */
	/***********************/

	/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
	/* devpriv->b_ModeSelectRegister=devpriv->b_ModeSelectRegister | APCI3120_DISABLE_SCAN; */

	devpriv->b_ModeSelectRegister = devpriv->b_ModeSelectRegister &
		APCI3120_DISABLE_SCAN;
	/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

	outb(devpriv->b_ModeSelectRegister,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);

	/*  If DMA is disabled */
	if (devpriv->us_UseDma == APCI3120_DISABLE) {
		/*  disable EOC and enable EOS */
		devpriv->b_InterruptMode = APCI3120_EOS_MODE;
		devpriv->b_EocEosInterrupt = APCI3120_ENABLE;

		devpriv->b_ModeSelectRegister =
			(devpriv->
			b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT) |
			APCI3120_ENABLE_EOS_INT;
		outb(devpriv->b_ModeSelectRegister,
			dev->iobase + APCI3120_WRITE_MODE_SELECT);

		if (!devpriv->b_AiContinuous) {
/*
 * configure Timer2 For counting EOS Reset gate 2 of Timer 2 to
 * disable it (Set Bit D14 to 0)
 */
			devpriv->us_OutputRegister =
				devpriv->
				us_OutputRegister & APCI3120_DISABLE_TIMER2;
			outw(devpriv->us_OutputRegister,
				dev->iobase + APCI3120_WR_ADDRESS);

			/*  DISABLE TIMER intERRUPT */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT & 0xEF;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

			/* (1) Init timer 2 in mode 0 and write timer value */
			devpriv->b_TimerSelectMode =
				(devpriv->
				b_TimerSelectMode & 0x0F) |
				APCI3120_TIMER_2_MODE_0;
			outb(devpriv->b_TimerSelectMode,
				dev->iobase + APCI3120_TIMER_CRT1);

			/* Writing LOW unsigned short */
			b_Tmp = ((devpriv->
					b_DigitalOutputRegister) & 0xF0) |
				APCI3120_SELECT_TIMER_2_LOW_WORD;
			outb(b_Tmp, dev->iobase + APCI3120_TIMER_CRT0);
			outw(LOWORD(ui_TimerValue2),
				dev->iobase + APCI3120_TIMER_VALUE);

			/* Writing HIGH unsigned short */
			b_Tmp = ((devpriv->
					b_DigitalOutputRegister) & 0xF0) |
				APCI3120_SELECT_TIMER_2_HIGH_WORD;
			outb(b_Tmp, dev->iobase + APCI3120_TIMER_CRT0);
			outw(HIWORD(ui_TimerValue2),
				dev->iobase + APCI3120_TIMER_VALUE);

			/* (2) Reset FC_TIMER BIT  Clearing timer status register */
			inb(dev->iobase + APCI3120_TIMER_STATUS_REGISTER);
			/*  enable timer counter and disable watch dog */
			devpriv->b_ModeSelectRegister =
				(devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_COUNTER) &
				APCI3120_DISABLE_WATCHDOG;
			/*  select EOS clock input for timer 2 */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_TIMER2_SELECT_EOS;
			/*  Enable timer2  interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);
			devpriv->b_Timer2Mode = APCI3120_COUNTER;
			devpriv->b_Timer2Interrupt = APCI3120_ENABLE;
		}
	} else {
		/* If DMA Enabled */

		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		/* inw(dev->iobase+0); reset EOC bit */
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */
		devpriv->b_InterruptMode = APCI3120_DMA_MODE;

		/************************************/
		/* Disables the EOC, EOS interrupt  */
		/************************************/
		devpriv->b_ModeSelectRegister = devpriv->b_ModeSelectRegister &
			APCI3120_DISABLE_EOC_INT & APCI3120_DISABLE_EOS_INT;

		outb(devpriv->b_ModeSelectRegister,
			dev->iobase + APCI3120_WRITE_MODE_SELECT);

		dmalen0 = devpriv->ui_DmaBufferSize[0];
		dmalen1 = devpriv->ui_DmaBufferSize[1];

		if (!devpriv->b_AiContinuous) {

			if (dmalen0 > (devpriv->ui_AiNbrofScans * devpriv->ui_AiScanLength * 2)) {	/*  must we fill full first buffer? */
				dmalen0 =
					devpriv->ui_AiNbrofScans *
					devpriv->ui_AiScanLength * 2;
			} else if (dmalen1 > (devpriv->ui_AiNbrofScans * devpriv->ui_AiScanLength * 2 - dmalen0))	/*  and must we fill full second buffer when first is once filled? */
				dmalen1 =
					devpriv->ui_AiNbrofScans *
					devpriv->ui_AiScanLength * 2 - dmalen0;
		}

		if (devpriv->ui_AiFlags & TRIG_WAKE_EOS) {
			/*  don't we want wake up every scan? */
			if (dmalen0 > (devpriv->ui_AiScanLength * 2)) {
				dmalen0 = devpriv->ui_AiScanLength * 2;
				if (devpriv->ui_AiScanLength & 1)
					dmalen0 += 2;
			}
			if (dmalen1 > (devpriv->ui_AiScanLength * 2)) {
				dmalen1 = devpriv->ui_AiScanLength * 2;
				if (devpriv->ui_AiScanLength & 1)
					dmalen1 -= 2;
				if (dmalen1 < 4)
					dmalen1 = 4;
			}
		} else {	/*  isn't output buff smaller that our DMA buff? */
			if (dmalen0 > (devpriv->ui_AiDataLength))
				dmalen0 = devpriv->ui_AiDataLength;
			if (dmalen1 > (devpriv->ui_AiDataLength))
				dmalen1 = devpriv->ui_AiDataLength;
		}
		devpriv->ui_DmaBufferUsesize[0] = dmalen0;
		devpriv->ui_DmaBufferUsesize[1] = dmalen1;

		/* Initialize DMA */

/*
 * Set Transfer count enable bit and A2P_fifo reset bit in AGCSTS
 * register 1
 */
		ui_Tmp = AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO;
		outl(ui_Tmp, devpriv->i_IobaseAmcc + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		/*********************/
		/* ENABLE BUS MASTER */
		/*********************/
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW,
			devpriv->i_IobaseAddon + 2);

		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH,
			devpriv->i_IobaseAddon + 2);

/*
 * TO VERIFIED BEGIN JK 07.05.04: Comparison between WIN32 and Linux
 * driver
 */
		outw(0x1000, devpriv->i_IobaseAddon + 2);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

		/* 2 No change */
		/* A2P FIFO MANAGEMENT */
		/* A2P fifo reset & transfer control enable */

		/***********************/
		/* A2P FIFO MANAGEMENT */
		/***********************/
		outl(APCI3120_A2P_FIFO_MANAGEMENT, devpriv->i_IobaseAmcc +
			APCI3120_AMCC_OP_MCSR);

/*
 * 3
 * beginning address of dma buf The 32 bit address of dma buffer
 * is converted into two 16 bit addresses Can done by using _attach
 * and put into into an array array used may be for differnet pages
 */

		/*  DMA Start Address Low */
		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->i_IobaseAddon + 0);
		outw((devpriv->ul_DmaBufferHw[0] & 0xFFFF),
			devpriv->i_IobaseAddon + 2);

		/*************************/
		/* DMA Start Address High */
		/*************************/
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->i_IobaseAddon + 0);
		outw((devpriv->ul_DmaBufferHw[0] / 65536),
			devpriv->i_IobaseAddon + 2);

/*
 * 4
 * amount of bytes to be transferred set transfer count used ADDON
 * MWTC register commented testing
 * outl(devpriv->ui_DmaBufferUsesize[0],
 * devpriv->i_IobaseAddon+AMCC_OP_REG_AMWTC);
 */

		/**************************/
		/* Nbr of acquisition LOW */
		/**************************/
		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->i_IobaseAddon + 0);
		outw((devpriv->ui_DmaBufferUsesize[0] & 0xFFFF),
			devpriv->i_IobaseAddon + 2);

		/***************************/
		/* Nbr of acquisition HIGH */
		/***************************/
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->i_IobaseAddon + 0);
		outw((devpriv->ui_DmaBufferUsesize[0] / 65536),
			devpriv->i_IobaseAddon + 2);

/*
 * 5
 * To configure A2P FIFO testing outl(
 * FIFO_ADVANCE_ON_BYTE_2,devpriv->i_IobaseAmcc+AMCC_OP_REG_INTCSR);
 */

		/******************/
		/* A2P FIFO RESET */
		/******************/
/*
 * TO VERIFY BEGIN JK 07.05.04: Comparison between WIN32 and Linux
 * driver
 */
		outl(0x04000000UL, devpriv->i_IobaseAmcc + AMCC_OP_REG_MCSR);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

/*
 * 6
 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN AMWEN_ENABLE |
 * A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
 */

		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		/* outw(3,devpriv->i_IobaseAddon + 4); */
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

/*
 * 7
 * initialise end of dma interrupt AINT_WRITE_COMPL =
 * ENABLE_WRITE_TC_INT(ADDI)
 */
		/***************************************************/
		/* A2P FIFO CONFIGURATE, END OF DMA intERRUPT INIT */
		/***************************************************/
		outl((APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
				APCI3120_ENABLE_WRITE_TC_INT),
			devpriv->i_IobaseAmcc + AMCC_OP_REG_INTCSR);

		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		/******************************************/
		/* ENABLE A2P FIFO WRITE AND ENABLE AMWEN */
		/******************************************/
		outw(3, devpriv->i_IobaseAddon + 4);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */

		/******************/
		/* A2P FIFO RESET */
		/******************/
		/* BEGIN JK 07.05.04: Comparison between WIN32 and Linux driver */
		outl(0x04000000UL,
			devpriv->i_IobaseAmcc + APCI3120_AMCC_OP_MCSR);
		/* END JK 07.05.04: Comparison between WIN32 and Linux driver */
	}

	if ((devpriv->us_UseDma == APCI3120_DISABLE)
		&& !devpriv->b_AiContinuous) {
		/*  set gate 2   to start conversion */
		devpriv->us_OutputRegister =
			devpriv->us_OutputRegister | APCI3120_ENABLE_TIMER2;
		outw(devpriv->us_OutputRegister,
			dev->iobase + APCI3120_WR_ADDRESS);
	}

	switch (mode) {
	case 1:
		/*  set gate 0   to start conversion */
		devpriv->us_OutputRegister =
			devpriv->us_OutputRegister | APCI3120_ENABLE_TIMER0;
		outw(devpriv->us_OutputRegister,
			dev->iobase + APCI3120_WR_ADDRESS);
		break;
	case 2:
		/*  set  gate 0 and gate 1 */
		devpriv->us_OutputRegister =
			devpriv->us_OutputRegister | APCI3120_ENABLE_TIMER1;
		devpriv->us_OutputRegister =
			devpriv->us_OutputRegister | APCI3120_ENABLE_TIMER0;
		outw(devpriv->us_OutputRegister,
			dev->iobase + APCI3120_WR_ADDRESS);
		break;

	}

	return 0;

}

/*
+----------------------------------------------------------------------------+
| 			intERNAL FUNCTIONS						                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     : int i_APCI3120_Reset(struct comedi_device *dev)               |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : Hardware reset function   						     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	struct comedi_device *dev									 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_Reset(struct comedi_device *dev)
{
	unsigned int i;
	unsigned short us_TmpValue;

	devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;
	devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
	devpriv->b_InterruptMode = APCI3120_EOC_MODE;
	devpriv->ui_EocEosConversionTime = 0;	/*  set eoc eos conv time to 0 */
	devpriv->b_OutputMemoryStatus = 0;

	/*  variables used in timer subdevice */
	devpriv->b_Timer2Mode = 0;
	devpriv->b_Timer2Interrupt = 0;
	devpriv->b_ExttrigEnable = 0;	/*  Disable ext trigger */

	/* Disable all interrupts, watchdog for the anolog output */
	devpriv->b_ModeSelectRegister = 0;
	outb(devpriv->b_ModeSelectRegister,
		dev->iobase + APCI3120_WRITE_MODE_SELECT);

	/*  Disables all counters, ext trigger and clears PA, PR */
	devpriv->us_OutputRegister = 0;
	outw(devpriv->us_OutputRegister, dev->iobase + APCI3120_WR_ADDRESS);

/*
 * Code to set the all anolog o/p channel to 0v 8191 is decimal
 * value for zero(0 v)volt in bipolar mode(default)
 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_1, dev->iobase + APCI3120_ANALOG_OUTPUT_1);	/* channel 1 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_2, dev->iobase + APCI3120_ANALOG_OUTPUT_1);	/* channel 2 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_3, dev->iobase + APCI3120_ANALOG_OUTPUT_1);	/* channel 3 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_4, dev->iobase + APCI3120_ANALOG_OUTPUT_1);	/* channel 4 */

	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_5, dev->iobase + APCI3120_ANALOG_OUTPUT_2);	/* channel 5 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_6, dev->iobase + APCI3120_ANALOG_OUTPUT_2);	/* channel 6 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_7, dev->iobase + APCI3120_ANALOG_OUTPUT_2);	/* channel 7 */
	outw(8191 | APCI3120_ANALOG_OP_CHANNEL_8, dev->iobase + APCI3120_ANALOG_OUTPUT_2);	/* channel 8 */

	/*   Reset digital output to L0W */

/* ES05  outb(0x0,dev->iobase+APCI3120_DIGITAL_OUTPUT); */
	udelay(10);

	inw(dev->iobase + 0);	/* make a dummy read */
	inb(dev->iobase + APCI3120_RESET_FIFO);	/*  flush FIFO */
	inw(dev->iobase + APCI3120_RD_STATUS);	/*  flush A/D status register */

	/* code to reset the RAM sequence */
	for (i = 0; i < 16; i++) {
		us_TmpValue = i << 8;	/* select the location */
		outw(us_TmpValue, dev->iobase + APCI3120_SEQ_RAM_ADDRESS);
	}
	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function name     : int i_APCI3120_SetupChannelList(struct comedi_device * dev,   |
|                     struct comedi_subdevice * s, int n_chan,unsigned int *chanlist|
|			  ,char check)											 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              :This function will first check channel list is ok or not|
|and then initialize the sequence RAM with the polarity, Gain,Channel number |
|If the last argument of function "check"is 1 then it only checks the channel|
|list is ok or not.												     		 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device * dev									 |
|                     struct comedi_subdevice * s									 |
|                     int n_chan                   					         |
			  unsigned int *chanlist
			  char check
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_SetupChannelList(struct comedi_device *dev, struct comedi_subdevice *s,
	int n_chan, unsigned int *chanlist, char check)
{
	unsigned int i;		/* , differencial=0, bipolar=0; */
	unsigned int gain;
	unsigned short us_TmpValue;

	/* correct channel and range number check itself comedi/range.c */
	if (n_chan < 1) {
		if (!check)
			comedi_error(dev, "range/channel list is empty!");
		return 0;
	}
	/*  All is ok, so we can setup channel/range list */
	if (check)
		return 1;

	/* Code  to set the PA and PR...Here it set PA to 0.. */
	devpriv->us_OutputRegister =
		devpriv->us_OutputRegister & APCI3120_CLEAR_PA_PR;
	devpriv->us_OutputRegister = ((n_chan - 1) & 0xf) << 8;
	outw(devpriv->us_OutputRegister, dev->iobase + APCI3120_WR_ADDRESS);

	for (i = 0; i < n_chan; i++) {
		/*  store range list to card */
		us_TmpValue = CR_CHAN(chanlist[i]);	/*  get channel number; */

		if (CR_RANGE(chanlist[i]) < APCI3120_BIPOLAR_RANGES)
			us_TmpValue &= ((~APCI3120_UNIPOLAR) & 0xff);	/*  set bipolar */
		else
			us_TmpValue |= APCI3120_UNIPOLAR;	/*  enable unipolar...... */

		gain = CR_RANGE(chanlist[i]);	/*  get gain number */
		us_TmpValue |= ((gain & 0x03) << 4);	/* <<4 for G0 and G1 bit in RAM */
		us_TmpValue |= i << 8;	/* To select the RAM LOCATION.... */
		outw(us_TmpValue, dev->iobase + APCI3120_SEQ_RAM_ADDRESS);

		printk("\n Gain = %i",
			(((unsigned char)CR_RANGE(chanlist[i]) & 0x03) << 2));
		printk("\n Channel = %i", CR_CHAN(chanlist[i]));
		printk("\n Polarity = %i", us_TmpValue & APCI3120_UNIPOLAR);
	}
	return 1;		/*  we can serve this with scan logic */
}

/*
+----------------------------------------------------------------------------+
| Function name     :	int i_APCI3120_ExttrigEnable(struct comedi_device * dev)    |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : 	Enable the external trigger						     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	struct comedi_device * dev									 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :      0        					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_ExttrigEnable(struct comedi_device *dev)
{

	devpriv->us_OutputRegister |= APCI3120_ENABLE_EXT_TRIGGER;
	outw(devpriv->us_OutputRegister, dev->iobase + APCI3120_WR_ADDRESS);
	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function name     : 	int i_APCI3120_ExttrigDisable(struct comedi_device * dev)   |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : 	Disables the external trigger					     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	struct comedi_device * dev									 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :    0          					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_ExttrigDisable(struct comedi_device *dev)
{
	devpriv->us_OutputRegister &= ~APCI3120_ENABLE_EXT_TRIGGER;
	outw(devpriv->us_OutputRegister, dev->iobase + APCI3120_WR_ADDRESS);
	return 0;
}

/*
+----------------------------------------------------------------------------+
|                    intERRUPT FUNCTIONS		    		                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     : void v_APCI3120_Interrupt(int irq, void *d) 								 |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              :Interrupt handler for APCI3120                        	 |
|			 When interrupt occurs this gets called.                 |
|			 First it finds which interrupt has been generated and   |
|			 handles  corresponding interrupt                        |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	int irq 											 |
|                        void *d											 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      : void         					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

void v_APCI3120_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned short int_daq;

	unsigned int int_amcc, ui_Check, i;
	unsigned short us_TmpValue;
	unsigned char b_DummyRead;

	struct comedi_subdevice *s = dev->subdevices + 0;
	ui_Check = 1;

	int_daq = inw(dev->iobase + APCI3120_RD_STATUS) & 0xf000;	/*  get IRQ reasons */
	int_amcc = inl(devpriv->i_IobaseAmcc + AMCC_OP_REG_INTCSR);	/*  get AMCC int register */

	if ((!int_daq) && (!(int_amcc & ANY_S593X_INT))) {
		comedi_error(dev, "IRQ from unknown source");
		return;
	}

	outl(int_amcc | 0x00ff0000, devpriv->i_IobaseAmcc + AMCC_OP_REG_INTCSR);	/*  shutdown IRQ reasons in AMCC */

	int_daq = (int_daq >> 12) & 0xF;

	if (devpriv->b_ExttrigEnable == APCI3120_ENABLE) {
		/* Disable ext trigger */
		i_APCI3120_ExttrigDisable(dev);
		devpriv->b_ExttrigEnable = APCI3120_DISABLE;
	}
	/* clear the timer 2 interrupt */
	inb(devpriv->i_IobaseAmcc + APCI3120_TIMER_STATUS_REGISTER);

	if (int_amcc & MASTER_ABORT_INT)
		comedi_error(dev, "AMCC IRQ - MASTER DMA ABORT!");
	if (int_amcc & TARGET_ABORT_INT)
		comedi_error(dev, "AMCC IRQ - TARGET DMA ABORT!");

	/*  Ckeck if EOC interrupt */
	if (((int_daq & 0x8) == 0)
		&& (devpriv->b_InterruptMode == APCI3120_EOC_MODE)) {
		if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {

			/*  Read the AI Value */

			devpriv->ui_AiReadData[0] =
				(unsigned int) inw(devpriv->iobase + 0);
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
			send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */
		} else {
			/* Disable EOC Interrupt */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT;
			outb(devpriv->b_ModeSelectRegister,
				devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

		}
	}

	/*  Check If EOS interrupt */
	if ((int_daq & 0x2) && (devpriv->b_InterruptMode == APCI3120_EOS_MODE)) {

		if (devpriv->b_EocEosInterrupt == APCI3120_ENABLE) {	/*  enable this in without DMA ??? */

			if (devpriv->b_AiCyclicAcquisition == APCI3120_ENABLE) {
				ui_Check = 0;
				i_APCI3120_InterruptHandleEos(dev);
				devpriv->ui_AiActualScan++;
				devpriv->b_ModeSelectRegister =
					devpriv->
					b_ModeSelectRegister |
					APCI3120_ENABLE_EOS_INT;
				outb(devpriv->b_ModeSelectRegister,
					dev->iobase +
					APCI3120_WRITE_MODE_SELECT);
			} else {
				ui_Check = 0;
				for (i = 0; i < devpriv->ui_AiNbrofChannels;
					i++) {
					us_TmpValue = inw(devpriv->iobase + 0);
					devpriv->ui_AiReadData[i] =
						(unsigned int) us_TmpValue;
				}
				devpriv->b_EocEosInterrupt = APCI3120_DISABLE;
				devpriv->b_InterruptMode = APCI3120_EOC_MODE;

				send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */

			}

		} else {
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOS_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);
			devpriv->b_EocEosInterrupt = APCI3120_DISABLE;	/* Default settings */
			devpriv->b_InterruptMode = APCI3120_EOC_MODE;
		}

	}
	/* Timer2 interrupt */
	if (int_daq & 0x1) {

		switch (devpriv->b_Timer2Mode) {
		case APCI3120_COUNTER:

			devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister & APCI3120_DISABLE_EOS_INT;
			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

			/*  stop timer 2 */
			devpriv->us_OutputRegister =
				devpriv->
				us_OutputRegister & APCI3120_DISABLE_ALL_TIMER;
			outw(devpriv->us_OutputRegister,
				dev->iobase + APCI3120_WR_ADDRESS);

			/* stop timer 0 and timer 1 */
			i_APCI3120_StopCyclicAcquisition(dev, s);
			devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;

			/* UPDATE-0.7.57->0.7.68comedi_done(dev,s); */
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);

			break;

		case APCI3120_TIMER:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		case APCI3120_WATCHDOG:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		default:

			/*  disable Timer Interrupt */

			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT;

			outb(devpriv->b_ModeSelectRegister,
				dev->iobase + APCI3120_WRITE_MODE_SELECT);

		}

		b_DummyRead = inb(dev->iobase + APCI3120_TIMER_STATUS_REGISTER);

	}

	if ((int_daq & 0x4) && (devpriv->b_InterruptMode == APCI3120_DMA_MODE)) {
		if (devpriv->b_AiCyclicAcquisition == APCI3120_ENABLE) {

			/****************************/
			/* Clear Timer Write TC int */
			/****************************/

			outl(APCI3120_CLEAR_WRITE_TC_INT,
				devpriv->i_IobaseAmcc +
				APCI3120_AMCC_OP_REG_INTCSR);

			/************************************/
			/* Clears the timer status register */
			/************************************/
			inw(dev->iobase + APCI3120_TIMER_STATUS_REGISTER);
			v_APCI3120_InterruptDma(irq, d);	/*  do some data transfer */
		} else {
			/* Stops the Timer */
			outw(devpriv->
				us_OutputRegister & APCI3120_DISABLE_TIMER0 &
				APCI3120_DISABLE_TIMER1,
				dev->iobase + APCI3120_WR_ADDRESS);
		}

	}

	return;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InterruptHandleEos(struct comedi_device *dev)   |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : This function handles EOS interrupt.                   |
|                     This function copies the acquired data(from FIFO)      |
|				to Comedi buffer.		 							 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     														 |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      : 0            					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/


int i_APCI3120_InterruptHandleEos(struct comedi_device *dev)
{
	int n_chan, i;
	struct comedi_subdevice *s = dev->subdevices + 0;
	int err = 1;

	n_chan = devpriv->ui_AiNbrofChannels;

	s->async->events = 0;

	for (i = 0; i < n_chan; i++)
		err &= comedi_buf_put(s->async, inw(dev->iobase + 0));

	s->async->events |= COMEDI_CB_EOS;

	if (err == 0)
		s->async->events |= COMEDI_CB_OVERFLOW;

	comedi_event(dev, s);

	return 0;
}

/*
+----------------------------------------------------------------------------+
| Function name     : void v_APCI3120_InterruptDma(int irq, void *d) 									 |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : This is a handler for the DMA interrupt                |
|			  This function copies the data to Comedi Buffer.        |
|			  For continuous DMA it reinitializes the DMA operation. |
|			  For single mode DMA it stop the acquisition.           |
|													     			 |
+----------------------------------------------------------------------------+
| Input Parameters  : int irq, void *d				 |
|                     														 |
+----------------------------------------------------------------------------+
| Return Value      :  void        					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

void v_APCI3120_InterruptDma(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 0;
	unsigned int next_dma_buf, samplesinbuf;
	unsigned long low_word, high_word, var;

	unsigned int ui_Tmp;
	samplesinbuf =
		devpriv->ui_DmaBufferUsesize[devpriv->ui_DmaActualBuffer] -
		inl(devpriv->i_IobaseAmcc + AMCC_OP_REG_MWTC);

	if (samplesinbuf <
		devpriv->ui_DmaBufferUsesize[devpriv->ui_DmaActualBuffer]) {
		comedi_error(dev, "Interrupted DMA transfer!");
	}
	if (samplesinbuf & 1) {
		comedi_error(dev, "Odd count of bytes in DMA ring!");
		i_APCI3120_StopCyclicAcquisition(dev, s);
		devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;

		return;
	}
	samplesinbuf = samplesinbuf >> 1;	/*  number of received samples */
	if (devpriv->b_DmaDoubleBuffer) {
		/*  switch DMA buffers if is used double buffering */
		next_dma_buf = 1 - devpriv->ui_DmaActualBuffer;

		ui_Tmp = AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO;
		outl(ui_Tmp, devpriv->i_IobaseAddon + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW,
			devpriv->i_IobaseAddon + 2);
		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH, devpriv->i_IobaseAddon + 2);	/*  0x1000 is out putted in windows driver */

		var = devpriv->ul_DmaBufferHw[next_dma_buf];
		low_word = var & 0xffff;
		var = devpriv->ul_DmaBufferHw[next_dma_buf];
		high_word = var / 65536;

		/* DMA Start Address Low */
		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->i_IobaseAddon + 0);
		outw(low_word, devpriv->i_IobaseAddon + 2);

		/* DMA Start Address High */
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->i_IobaseAddon + 0);
		outw(high_word, devpriv->i_IobaseAddon + 2);

		var = devpriv->ui_DmaBufferUsesize[next_dma_buf];
		low_word = var & 0xffff;
		var = devpriv->ui_DmaBufferUsesize[next_dma_buf];
		high_word = var / 65536;

		/* Nbr of acquisition LOW */
		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->i_IobaseAddon + 0);
		outw(low_word, devpriv->i_IobaseAddon + 2);

		/* Nbr of acquisition HIGH */
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->i_IobaseAddon + 0);
		outw(high_word, devpriv->i_IobaseAddon + 2);

/*
 * To configure A2P FIFO
 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
 */
		outw(3, devpriv->i_IobaseAddon + 4);
		/* initialise end of dma interrupt  AINT_WRITE_COMPL = ENABLE_WRITE_TC_INT(ADDI) */
		outl((APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
				APCI3120_ENABLE_WRITE_TC_INT),
			devpriv->i_IobaseAmcc + AMCC_OP_REG_INTCSR);

	}
	if (samplesinbuf) {
		v_APCI3120_InterruptDmaMoveBlock16bit(dev, s,
			devpriv->ul_DmaBufferVirtual[devpriv->
				ui_DmaActualBuffer], samplesinbuf);

		if (!(devpriv->ui_AiFlags & TRIG_WAKE_EOS)) {
			s->async->events |= COMEDI_CB_EOS;
			comedi_event(dev, s);
		}
	}
	if (!devpriv->b_AiContinuous)
		if (devpriv->ui_AiActualScan >= devpriv->ui_AiNbrofScans) {
			/*  all data sampled */
			i_APCI3120_StopCyclicAcquisition(dev, s);
			devpriv->b_AiCyclicAcquisition = APCI3120_DISABLE;
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			return;
		}

	if (devpriv->b_DmaDoubleBuffer) {	/*  switch dma buffers */
		devpriv->ui_DmaActualBuffer = 1 - devpriv->ui_DmaActualBuffer;
	} else {
/*
 * restart DMA if is not used double buffering
 * ADDED REINITIALISE THE DMA
 */
		ui_Tmp = AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO;
		outl(ui_Tmp, devpriv->i_IobaseAddon + AMCC_OP_REG_AGCSTS);

		/*  changed  since 16 bit interface for add on */
		outw(APCI3120_ADD_ON_AGCSTS_LOW, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_LOW,
			devpriv->i_IobaseAddon + 2);
		outw(APCI3120_ADD_ON_AGCSTS_HIGH, devpriv->i_IobaseAddon + 0);
		outw(APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH, devpriv->i_IobaseAddon + 2);	/*  */
/*
 * A2P FIFO MANAGEMENT
 * A2P fifo reset & transfer control enable
 */
		outl(APCI3120_A2P_FIFO_MANAGEMENT,
			devpriv->i_IobaseAmcc + AMCC_OP_REG_MCSR);

		var = devpriv->ul_DmaBufferHw[0];
		low_word = var & 0xffff;
		var = devpriv->ul_DmaBufferHw[0];
		high_word = var / 65536;
		outw(APCI3120_ADD_ON_MWAR_LOW, devpriv->i_IobaseAddon + 0);
		outw(low_word, devpriv->i_IobaseAddon + 2);
		outw(APCI3120_ADD_ON_MWAR_HIGH, devpriv->i_IobaseAddon + 0);
		outw(high_word, devpriv->i_IobaseAddon + 2);

		var = devpriv->ui_DmaBufferUsesize[0];
		low_word = var & 0xffff;	/* changed */
		var = devpriv->ui_DmaBufferUsesize[0];
		high_word = var / 65536;
		outw(APCI3120_ADD_ON_MWTC_LOW, devpriv->i_IobaseAddon + 0);
		outw(low_word, devpriv->i_IobaseAddon + 2);
		outw(APCI3120_ADD_ON_MWTC_HIGH, devpriv->i_IobaseAddon + 0);
		outw(high_word, devpriv->i_IobaseAddon + 2);

/*
 * To configure A2P FIFO
 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
 */
		outw(3, devpriv->i_IobaseAddon + 4);
		/* initialise end of dma interrupt  AINT_WRITE_COMPL = ENABLE_WRITE_TC_INT(ADDI) */
		outl((APCI3120_FIFO_ADVANCE_ON_BYTE_2 |
				APCI3120_ENABLE_WRITE_TC_INT),
			devpriv->i_IobaseAmcc + AMCC_OP_REG_INTCSR);
	}
}

/*
+----------------------------------------------------------------------------+
| Function name     :void v_APCI3120_InterruptDmaMoveBlock16bit(comedi_device|
|*dev,struct comedi_subdevice *s,short *dma,short *data,int n)				     |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : This function copies the data from DMA buffer to the   |
|				 Comedi buffer  									 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     short *dma											 |
|                     short *data,int n          					         |
+----------------------------------------------------------------------------+
| Return Value      : void         					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

void v_APCI3120_InterruptDmaMoveBlock16bit(struct comedi_device *dev,
	struct comedi_subdevice *s, short *dma_buffer, unsigned int num_samples)
{
	devpriv->ui_AiActualScan +=
		(s->async->cur_chan + num_samples) / devpriv->ui_AiScanLength;
	s->async->cur_chan += num_samples;
	s->async->cur_chan %= devpriv->ui_AiScanLength;

	cfc_write_array_to_buffer(s, dma_buffer, num_samples * sizeof(short));
}

/*
+----------------------------------------------------------------------------+
|                           TIMER SUBDEVICE   		                         |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnConfigTimer(struct comedi_device *dev,          |
|	struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data) 			     |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              :Configure Timer 2  								     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
|                     														 |
|                      data[0]= TIMER  configure as timer                    |
|              				 = WATCHDOG configure as watchdog				 |
|       			  data[1] = Timer constant							 |
|       			  data[2] = Timer2 interrupt (1)enable or(0) disable |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnConfigTimer(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{

	unsigned int ui_Timervalue2;
	unsigned short us_TmpValue;
	unsigned char b_Tmp;

	if (!data[1])
		comedi_error(dev, "config:No timer constant !");

	devpriv->b_Timer2Interrupt = (unsigned char) data[2];	/*  save info whether to enable or disable interrupt */

	ui_Timervalue2 = data[1] / 1000;	/*  convert nano seconds  to u seconds */

	/* this_board->timer_config(dev, ui_Timervalue2,(unsigned char)data[0]); */
	us_TmpValue = (unsigned short) inw(devpriv->iobase + APCI3120_RD_STATUS);

/*
 * EL250804: Testing if board APCI3120 have the new Quartz or if it
 * is an APCI3001 and calculate the time value to set in the timer
 */
	if ((us_TmpValue & 0x00B0) == 0x00B0
		|| !strcmp(this_board->pc_DriverName, "apci3001")) {
		/* Calculate the time value to set in the timer */
		ui_Timervalue2 = ui_Timervalue2 / 50;
	} else {
		/* Calculate the time value to set in the timer */
		ui_Timervalue2 = ui_Timervalue2 / 70;
	}

	/* Reset gate 2 of Timer 2 to disable it (Set Bit D14 to 0) */
	devpriv->us_OutputRegister =
		devpriv->us_OutputRegister & APCI3120_DISABLE_TIMER2;
	outw(devpriv->us_OutputRegister, devpriv->iobase + APCI3120_WR_ADDRESS);

	/*  Disable TIMER Interrupt */
	devpriv->b_ModeSelectRegister =
		devpriv->
		b_ModeSelectRegister & APCI3120_DISABLE_TIMER_INT & 0xEF;

	/*  Disable Eoc and Eos Interrupts */
	devpriv->b_ModeSelectRegister =
		devpriv->
		b_ModeSelectRegister & APCI3120_DISABLE_EOC_INT &
		APCI3120_DISABLE_EOS_INT;
	outb(devpriv->b_ModeSelectRegister,
		devpriv->iobase + APCI3120_WRITE_MODE_SELECT);
	if (data[0] == APCI3120_TIMER) {	/* initialize timer */
		/* devpriv->b_ModeSelectRegister=devpriv->b_ModeSelectRegister |
		 * APCI3120_ENABLE_TIMER_INT; */

		/* outb(devpriv->b_ModeSelectRegister,devpriv->iobase+APCI3120_WRITE_MODE_SELECT); */

		/* Set the Timer 2 in mode 2(Timer) */
		devpriv->b_TimerSelectMode =
			(devpriv->
			b_TimerSelectMode & 0x0F) | APCI3120_TIMER_2_MODE_2;
		outb(devpriv->b_TimerSelectMode,
			devpriv->iobase + APCI3120_TIMER_CRT1);

/*
 * Configure the timer 2 for writing the LOW unsigned short of timer
 * is Delay value You must make a b_tmp variable with
 * DigitalOutPutRegister because at Address_1+APCI3120_TIMER_CRT0
 * you can set the digital output and configure the timer 2,and if
 * you don't make this, digital output are erase (Set to 0)
 */

		/* Writing LOW unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_LOW_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);
		outw(LOWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);

		/* Writing HIGH unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_HIGH_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);
		outw(HIWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);
		/*  timer2 in Timer mode enabled */
		devpriv->b_Timer2Mode = APCI3120_TIMER;

	} else {			/*  Initialize Watch dog */

		/* Set the Timer 2 in mode 5(Watchdog) */

		devpriv->b_TimerSelectMode =
			(devpriv->
			b_TimerSelectMode & 0x0F) | APCI3120_TIMER_2_MODE_5;
		outb(devpriv->b_TimerSelectMode,
			devpriv->iobase + APCI3120_TIMER_CRT1);

/*
 * Configure the timer 2 for writing the LOW unsigned short of timer
 * is Delay value You must make a b_tmp variable with
 * DigitalOutPutRegister because at Address_1+APCI3120_TIMER_CRT0
 * you can set the digital output and configure the timer 2,and if
 * you don't make this, digital output are erase (Set to 0)
 */

		/* Writing LOW unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_LOW_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);
		outw(LOWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);

		/* Writing HIGH unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_HIGH_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

		outw(HIWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);
		/* watchdog enabled */
		devpriv->b_Timer2Mode = APCI3120_WATCHDOG;

	}

	return insn->n;

}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnWriteTimer(struct comedi_device *dev,           |
|                    struct comedi_subdevice *s, struct comedi_insn *insn,unsigned int *data)  |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              :    To start and stop the timer		                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data                                         |
|                                                                            |
|				data[0] = 1 (start)                                  |
|				data[0] = 0 (stop )                                  |
|	 			data[0] = 2  (write new value)                       |
|	   			data[1]= new value                                   |
|                                                                            |
|    				devpriv->b_Timer2Mode =  0 DISABLE                   |
|	                     					 1 Timer                     |
|					 					 2 Watch dog			     |
|                                                 					         |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnWriteTimer(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{

	unsigned int ui_Timervalue2 = 0;
	unsigned short us_TmpValue;
	unsigned char b_Tmp;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		comedi_error(dev, "\nwrite:timer2  not configured ");
		return -EINVAL;
	}

	if (data[0] == 2) {	/*  write new value */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			comedi_error(dev,
				"write :timer2  not configured  in TIMER MODE");
			return -EINVAL;
		}

		if (data[1])
			ui_Timervalue2 = data[1];
		else
			ui_Timervalue2 = 0;
	}

	/* this_board->timer_write(dev,data[0],ui_Timervalue2); */

	switch (data[0]) {
	case APCI3120_START:

		/*  Reset FC_TIMER BIT */
		inb(devpriv->iobase + APCI3120_TIMER_STATUS_REGISTER);
		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {	/* start timer */
			/* Enable Timer */
			devpriv->b_ModeSelectRegister =
				devpriv->b_ModeSelectRegister & 0x0B;
		} else {		/* start watch dog */
			/* Enable WatchDog */
			devpriv->b_ModeSelectRegister =
				(devpriv->
				b_ModeSelectRegister & 0x0B) |
				APCI3120_ENABLE_WATCHDOG;
		}

		/* enable disable interrupt */
		if ((devpriv->b_Timer2Interrupt) == APCI3120_ENABLE) {

			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister |
				APCI3120_ENABLE_TIMER_INT;
			/*  save the task structure to pass info to user */
			devpriv->tsk_Current = current;
		} else {

			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_INT;
		}
		outb(devpriv->b_ModeSelectRegister,
			devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {	/* start timer */
			/* For Timer mode is  Gate2 must be activated   **timer started */
			devpriv->us_OutputRegister =
				devpriv->
				us_OutputRegister | APCI3120_ENABLE_TIMER2;
			outw(devpriv->us_OutputRegister,
				devpriv->iobase + APCI3120_WR_ADDRESS);
		}

		break;

	case APCI3120_STOP:
		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {
			/* Disable timer */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_TIMER_COUNTER;
		} else {
			/* Disable WatchDog */
			devpriv->b_ModeSelectRegister =
				devpriv->
				b_ModeSelectRegister &
				APCI3120_DISABLE_WATCHDOG;
		}
		/*  Disable timer interrupt */
		devpriv->b_ModeSelectRegister =
			devpriv->
			b_ModeSelectRegister & APCI3120_DISABLE_TIMER_INT;

		/*  Write above states  to register */
		outb(devpriv->b_ModeSelectRegister,
			devpriv->iobase + APCI3120_WRITE_MODE_SELECT);

		/*  Reset Gate 2 */
		devpriv->us_OutputRegister =
			devpriv->us_OutputRegister & APCI3120_DISABLE_TIMER_INT;
		outw(devpriv->us_OutputRegister,
			devpriv->iobase + APCI3120_WR_ADDRESS);

		/*  Reset FC_TIMER BIT */
		inb(devpriv->iobase + APCI3120_TIMER_STATUS_REGISTER);

		/* Disable timer */
		/* devpriv->b_Timer2Mode=APCI3120_DISABLE;  */

		break;

	case 2:		/* write new value to Timer */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			comedi_error(dev,
				"write :timer2  not configured  in TIMER MODE");
			return -EINVAL;
		}
		/*  ui_Timervalue2=data[1]; // passed as argument */
		us_TmpValue =
			(unsigned short) inw(devpriv->iobase + APCI3120_RD_STATUS);

/*
 * EL250804: Testing if board APCI3120 have the new Quartz or if it
 * is an APCI3001 and calculate the time value to set in the timer
 */
		if ((us_TmpValue & 0x00B0) == 0x00B0
			|| !strcmp(this_board->pc_DriverName, "apci3001")) {
			/* Calculate the time value to set in the timer */
			ui_Timervalue2 = ui_Timervalue2 / 50;
		} else {
			/* Calculate the time value to set in the timer */
			ui_Timervalue2 = ui_Timervalue2 / 70;
		}
		/* Writing LOW unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_LOW_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

		outw(LOWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);

		/* Writing HIGH unsigned short */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_HIGH_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

		outw(HIWORD(ui_Timervalue2),
			devpriv->iobase + APCI3120_TIMER_VALUE);

		break;
	default:
		return -EINVAL;	/*  Not a valid input */
	}

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function name     : int i_APCI3120_InsnReadTimer(struct comedi_device *dev,           |
|		struct comedi_subdevice *s,struct comedi_insn *insn, unsigned int *data) 		 |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : read the Timer value 				                 	 |
+----------------------------------------------------------------------------+
| Input Parameters  : 	struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
|                     														 |
+----------------------------------------------------------------------------+
| Return Value      :   													 |
|			for Timer:	data[0]= Timer constant						 |
|																	 |
|         		for watchdog: data[0]=0 (still running)                  |
|	               			  data[0]=1  (run down)            			 |
|                    													     |
+----------------------------------------------------------------------------+
*/
int i_APCI3120_InsnReadTimer(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned char b_Tmp;
	unsigned short us_TmpValue, us_TmpValue_2, us_StatusValue;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		comedi_error(dev, "\nread:timer2  not configured ");
	}

	/* this_board->timer_read(dev,data); */
	if (devpriv->b_Timer2Mode == APCI3120_TIMER) {

		/* Read the LOW unsigned short of Timer 2 register */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_LOW_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

		us_TmpValue = inw(devpriv->iobase + APCI3120_TIMER_VALUE);

		/* Read the HIGH unsigned short of Timer 2 register */
		b_Tmp = ((devpriv->
				b_DigitalOutputRegister) & 0xF0) |
			APCI3120_SELECT_TIMER_2_HIGH_WORD;
		outb(b_Tmp, devpriv->iobase + APCI3120_TIMER_CRT0);

		us_TmpValue_2 = inw(devpriv->iobase + APCI3120_TIMER_VALUE);

		/*  combining both words */
		data[0] = (unsigned int) ((us_TmpValue) | ((us_TmpValue_2) << 16));

	} else {			/*  Read watch dog status */

		us_StatusValue = inw(devpriv->iobase + APCI3120_RD_STATUS);
		us_StatusValue =
			((us_StatusValue & APCI3120_FC_TIMER) >> 12) & 1;
		if (us_StatusValue == 1) {
			/*  RESET FC_TIMER BIT */
			inb(devpriv->iobase + APCI3120_TIMER_STATUS_REGISTER);
		}
		data[0] = us_StatusValue;	/*  when data[0] = 1 then the watch dog has rundown */
	}
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
|                           DIGITAL INPUT SUBDEVICE   		                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnReadDigitalInput(struct comedi_device *dev,     |
|			struct comedi_subdevice *s, struct comedi_insn *insn,unsigned int *data)   |
|                                        									 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              : Reads the value of the specified  Digital input channel|
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnReadDigitalInput(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned int ui_Chan, ui_TmpValue;

	ui_Chan = CR_CHAN(insn->chanspec);	/*  channel specified */

	/* this_board->di_read(dev,ui_Chan,data); */
	if (ui_Chan <= 3) {
		ui_TmpValue = (unsigned int) inw(devpriv->iobase + APCI3120_RD_STATUS);

/*
 * since only 1 channel reqd to bring it to last bit it is rotated 8
 * +(chan - 1) times then ANDed with 1 for last bit.
 */
		*data = (ui_TmpValue >> (ui_Chan + 8)) & 1;
		/* return 0; */
	} else {
		/*       comedi_error(dev," chan spec wrong"); */
		return -EINVAL;	/*  "sorry channel spec wrong " */
	}
	return insn->n;

}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnBitsDigitalInput(struct comedi_device *dev, |
|struct comedi_subdevice *s, struct comedi_insn *insn,unsigned int *data)                      |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Reads the value of the Digital input Port i.e.4channels|
|   value is returned in data[0]											 |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/
int i_APCI3120_InsnBitsDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned int ui_TmpValue;
	ui_TmpValue = (unsigned int) inw(devpriv->iobase + APCI3120_RD_STATUS);
	/*****	state of 4 channels  in the 11, 10, 9, 8   bits of status reg
			rotated right 8 times to bring them to last four bits
			ANDed with oxf for  value.
	*****/

	*data = (ui_TmpValue >> 8) & 0xf;
	/* this_board->di_bits(dev,data); */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
|                           DIGITAL OUTPUT SUBDEVICE   		                 |
+----------------------------------------------------------------------------+
*/
/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnConfigDigitalOutput(struct comedi_device    |
| *dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)				 |
|                                            						         |
+----------------------------------------------------------------------------+
| Task              :Configure the output memory ON or OFF				     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  :struct comedi_device *dev									 	 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnConfigDigitalOutput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}
	if (data[0]) {
		devpriv->b_OutputMemoryStatus = APCI3120_ENABLE;

	} else {
		devpriv->b_OutputMemoryStatus = APCI3120_DISABLE;
		devpriv->b_DigitalOutputRegister = 0;
	}
	if (!devpriv->b_OutputMemoryStatus)
		ui_Temp = 0;
				/* if(!devpriv->b_OutputMemoryStatus ) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnBitsDigitalOutput(struct comedi_device *dev,    |
|		struct comedi_subdevice *s, struct comedi_insn *insn,unsigned int *data) 		 |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : write diatal output port							     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data 										 |
|                      data[0]     Value to be written
|                      data[1]    :1 Set digital o/p ON
|                      data[1]     2 Set digital o/p OFF with memory ON
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnBitsDigitalOutput(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	if ((data[0] > devpriv->s_EeParameters.i_DoMaxdata) || (data[0] < 0)) {

		comedi_error(dev, "Data is not valid !!! \n");
		return -EINVAL;
	}

	switch (data[1]) {
	case 1:
		data[0] = (data[0] << 4) | devpriv->b_DigitalOutputRegister;
		break;

	case 2:
		data[0] = data[0];
		break;
	default:
		printk("\nThe parameter passed is in error \n");
		return -EINVAL;
	}			/*  switch(data[1]) */
	outb(data[0], devpriv->iobase + APCI3120_DIGITAL_OUTPUT);

	devpriv->b_DigitalOutputRegister = data[0] & 0xF0;

	return insn->n;

}

/*
+----------------------------------------------------------------------------+
| Function name		:int i_APCI3120_InsnWriteDigitalOutput(struct comedi_device *dev,|
|struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)	|
|										|
+----------------------------------------------------------------------------+
| Task			: Write digiatl output					|
| 										|
+----------------------------------------------------------------------------+
| Input Parameters	: struct comedi_device *dev				|
|			struct comedi_subdevice *s			 	|
|			struct comedi_insn *insn				|
|			unsigned int *data					|
			data[0]     Value to be written
			data[1]    :1 Set digital o/p ON
			data[1]     2 Set digital o/p OFF with memory ON
+----------------------------------------------------------------------------+
| Return Value		:							|
|										|
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnWriteDigitalOutput(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{

	unsigned int ui_Temp1;

	unsigned int ui_NoOfChannel = CR_CHAN(insn->chanspec);	/*  get the channel */

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}
	if (ui_NoOfChannel > devpriv->s_EeParameters.i_NbrDoChannel - 1) {
		comedi_error(dev,
			"This board doesn't have specified channel !!! \n");
		return -EINVAL;
	}

	switch (data[1]) {
	case 1:
		data[0] = (data[0] << ui_NoOfChannel);
/* ES05                   data[0]=(data[0]<<4)|ui_Temp; */
		data[0] = (data[0] << 4) | devpriv->b_DigitalOutputRegister;
		break;

	case 2:
		data[0] = ~data[0] & 0x1;
		ui_Temp1 = 1;
		ui_Temp1 = ui_Temp1 << ui_NoOfChannel;
		ui_Temp1 = ui_Temp1 << 4;
/* ES05                   ui_Temp=ui_Temp|ui_Temp1; */
		devpriv->b_DigitalOutputRegister =
			devpriv->b_DigitalOutputRegister | ui_Temp1;

		data[0] = (data[0] << ui_NoOfChannel) ^ 0xf;
		data[0] = data[0] << 4;
/* ES05                   data[0]=data[0]& ui_Temp; */
		data[0] = data[0] & devpriv->b_DigitalOutputRegister;
		break;
	default:
		printk("\nThe parameter passed is in error \n");
		return -EINVAL;
	}			/*  switch(data[1]) */
	outb(data[0], devpriv->iobase + APCI3120_DIGITAL_OUTPUT);

/* ES05        ui_Temp=data[0] & 0xf0; */
	devpriv->b_DigitalOutputRegister = data[0] & 0xf0;
	return insn->n;

}

/*
+----------------------------------------------------------------------------+
|                            ANALOG OUTPUT SUBDEVICE                         |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3120_InsnWriteAnalogOutput(struct comedi_device *dev,|
|struct comedi_subdevice *s, struct comedi_insn *insn,unsigned int *data)			             |
|                                        									 |
+----------------------------------------------------------------------------+
| Task              : Write  analog output   							     |
|                     										                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev									 |
|                     struct comedi_subdevice *s									 |
|                     struct comedi_insn *insn                                      |
|                     unsigned int *data  										 |
+----------------------------------------------------------------------------+
| Return Value      :              					                         |
|                    													     |
+----------------------------------------------------------------------------+
*/

int i_APCI3120_InsnWriteAnalogOutput(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	unsigned int ui_Range, ui_Channel;
	unsigned short us_TmpValue;

	ui_Range = CR_RANGE(insn->chanspec);
	ui_Channel = CR_CHAN(insn->chanspec);

	/* this_board->ao_write(dev, ui_Range, ui_Channel,data[0]); */
	if (ui_Range) {		/*  if 1 then unipolar */

		if (data[0] != 0)
			data[0] =
				((((ui_Channel & 0x03) << 14) & 0xC000) | (1 <<
					13) | (data[0] + 8191));
		else
			data[0] =
				((((ui_Channel & 0x03) << 14) & 0xC000) | (1 <<
					13) | 8192);

	} else {			/*  if 0 then   bipolar */
		data[0] =
			((((ui_Channel & 0x03) << 14) & 0xC000) | (0 << 13) |
			data[0]);

	}

/*
 * out put n values at the given channel. printk("\nwaiting for
 * DA_READY BIT");
 */
	do {			/* Waiting of DA_READY BIT */
		us_TmpValue =
			((unsigned short) inw(devpriv->iobase +
				APCI3120_RD_STATUS)) & 0x0001;
	} while (us_TmpValue != 0x0001);

	if (ui_Channel <= 3)
/*
 * for channel 0-3 out at the register 1 (wrDac1-8) data[i]
 * typecasted to ushort since word write is to be done
 */
		outw((unsigned short) data[0],
			devpriv->iobase + APCI3120_ANALOG_OUTPUT_1);
	else
/*
 * for channel 4-7 out at the register 2 (wrDac5-8) data[i]
 * typecasted to ushort since word write is to be done
 */
		outw((unsigned short) data[0],
			devpriv->iobase + APCI3120_ANALOG_OUTPUT_2);

	return insn->n;
}

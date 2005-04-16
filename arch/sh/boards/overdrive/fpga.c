/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * This file handles programming up the Altera Flex10K that interfaces to
 * the Galileo, and does the PS/2 keyboard and mouse
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>


#include <asm/overdriver/gt64111.h>
#include <asm/overdrive/overdrive.h>
#include <asm/overdrive/fpga.h>

#define FPGA_NotConfigHigh()  (*FPGA_ControlReg) = (*FPGA_ControlReg) | ENABLE_FPGA_BIT
#define FPGA_NotConfigLow()   (*FPGA_ControlReg) = (*FPGA_ControlReg) & RESET_FPGA_MASK

/* I need to find out what (if any) the real delay factor here is */
/* The delay is definately not critical */
#define long_delay() {int i;for(i=0;i<10000;i++);}
#define short_delay() {int i;for(i=0;i<100;i++);}

static void __init program_overdrive_fpga(const unsigned char *fpgacode,
					  int size)
{
	int timeout = 0;
	int i, j;
	unsigned char b;
	static volatile unsigned char *FPGA_ControlReg =
	    (volatile unsigned char *) (OVERDRIVE_CTRL);
	static volatile unsigned char *FPGA_ProgramReg =
	    (volatile unsigned char *) (FPGA_DCLK_ADDRESS);

	printk("FPGA:  Commencing FPGA Programming\n");

	/* The PCI reset but MUST be low when programming the FPGA !!! */
	b = (*FPGA_ControlReg) & RESET_PCI_MASK;

	(*FPGA_ControlReg) = b;

	/* Prepare FPGA to program */

	FPGA_NotConfigHigh();
	long_delay();

	FPGA_NotConfigLow();
	short_delay();

	while ((*FPGA_ProgramReg & FPGA_NOT_STATUS) != 0) {
		printk("FPGA:  Waiting for NotStatus to go Low ... \n");
	}

	FPGA_NotConfigHigh();

	/* Wait for FPGA "ready to be programmed" signal */
	printk("FPGA:  Waiting for NotStatus to go high (FPGA ready)... \n");

	for (timeout = 0;
	     (((*FPGA_ProgramReg & FPGA_NOT_STATUS) == 0)
	      && (timeout < FPGA_TIMEOUT)); timeout++);

	/* Check if timeout condition occured - i.e. an error */

	if (timeout == FPGA_TIMEOUT) {
		printk
		    ("FPGA:  Failed to program - Timeout waiting for notSTATUS to go high\n");
		return;
	}

	printk("FPGA:  Copying data to FPGA ... %d bytes\n", size);

	/* Copy array to FPGA - bit at a time */

	for (i = 0; i < size; i++) {
		volatile unsigned w = 0;

		for (j = 0; j < 8; j++) {
			*FPGA_ProgramReg = (fpgacode[i] >> j) & 0x01;
			short_delay();
		}
		if ((i & 0x3ff) == 0) {
			printk(".");
		}
	}

	/* Waiting for CONFDONE to go high - means the program is complete  */

	for (timeout = 0;
	     (((*FPGA_ProgramReg & FPGA_CONFDONE) == 0)
	      && (timeout < FPGA_TIMEOUT)); timeout++) {

		*FPGA_ProgramReg = 0x0;
		long_delay();
	}

	if (timeout == FPGA_TIMEOUT) {
		printk
		    ("FPGA:  Failed to program - Timeout waiting for CONFDONE to go high\n");
		return;
	} else {		/* Clock another 10 times - gets the device into a working state      */
		for (i = 0; i < 10; i++) {
			*FPGA_ProgramReg = 0x0;
			short_delay();
		}
	}

	printk("FPGA:  Programming complete\n");
}


static const unsigned char __init fpgacode[] = {
#include "./overdrive.ttf"	/* Code from maxplus2 compiler */
	, 0, 0
};


int __init init_overdrive_fpga(void)
{
	program_overdrive_fpga(fpgacode, sizeof(fpgacode));

	return 0;
}

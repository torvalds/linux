/*
 * SMC 37C669 initialization code
 */
#include <linux/kernel.h>

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <asm/hwrpb.h>
#include <asm/io.h>
#include <asm/segment.h>

#if 0
# define DBG_DEVS(args)         printk args
#else
# define DBG_DEVS(args)
#endif

#define KB              1024
#define MB              (1024*KB)
#define GB              (1024*MB)

#define SMC_DEBUG   0

/* File:	smcc669_def.h
 *
 * Copyright (C) 1997 by
 * Digital Equipment Corporation, Maynard, Massachusetts.
 * All rights reserved.
 *
 * This software is furnished under a license and may be used and copied
 * only  in  accordance  of  the  terms  of  such  license  and with the
 * inclusion of the above copyright notice. This software or  any  other
 * copies thereof may not be provided or otherwise made available to any
 * other person.  No title to and  ownership of the  software is  hereby
 * transferred.
 *
 * The information in this software is  subject to change without notice
 * and  should  not  be  construed  as a commitment by Digital Equipment
 * Corporation.
 *
 * Digital assumes no responsibility for the use  or  reliability of its
 * software on equipment which is not supplied by Digital.
 *
 *
 * Abstract:	
 *
 *	This file contains header definitions for the SMC37c669 
 *	Super I/O controller. 
 *
 * Author:	
 *
 *	Eric Rasmussen
 *
 * Modification History:
 *
 *	er	28-Jan-1997	Initial Entry
 */

#ifndef __SMC37c669_H
#define __SMC37c669_H

/*
** Macros for handling device IRQs
**
** The mask acts as a flag used in mapping actual ISA IRQs (0 - 15) 
** to device IRQs (A - H).
*/
#define SMC37c669_DEVICE_IRQ_MASK	0x80000000
#define SMC37c669_DEVICE_IRQ( __i )	\
	((SMC37c669_DEVICE_IRQ_MASK) | (__i))
#define SMC37c669_IS_DEVICE_IRQ(__i)	\
	(((__i) & (SMC37c669_DEVICE_IRQ_MASK)) == (SMC37c669_DEVICE_IRQ_MASK))
#define SMC37c669_RAW_DEVICE_IRQ(__i)	\
	((__i) & ~(SMC37c669_DEVICE_IRQ_MASK))

/*
** Macros for handling device DRQs
**
** The mask acts as a flag used in mapping actual ISA DMA
** channels to device DMA channels (A - C).
*/
#define SMC37c669_DEVICE_DRQ_MASK	0x80000000
#define SMC37c669_DEVICE_DRQ(__d)	\
	((SMC37c669_DEVICE_DRQ_MASK) | (__d))
#define SMC37c669_IS_DEVICE_DRQ(__d)	\
	(((__d) & (SMC37c669_DEVICE_DRQ_MASK)) == (SMC37c669_DEVICE_DRQ_MASK))
#define SMC37c669_RAW_DEVICE_DRQ(__d)	\
	((__d) & ~(SMC37c669_DEVICE_DRQ_MASK))

#define SMC37c669_DEVICE_ID	0x3

/*
** SMC37c669 Device Function Definitions
*/
#define SERIAL_0	0
#define SERIAL_1	1
#define PARALLEL_0	2
#define FLOPPY_0	3
#define IDE_0		4
#define NUM_FUNCS	5

/*
** Default Device Function Mappings
*/
#define COM1_BASE	0x3F8
#define COM1_IRQ	4
#define COM2_BASE	0x2F8
#define COM2_IRQ	3
#define PARP_BASE	0x3BC
#define PARP_IRQ	7
#define PARP_DRQ	3
#define FDC_BASE	0x3F0
#define FDC_IRQ		6
#define FDC_DRQ		2

/*
** Configuration On/Off Key Definitions
*/
#define SMC37c669_CONFIG_ON_KEY		0x55
#define SMC37c669_CONFIG_OFF_KEY	0xAA

/*
** SMC 37c669 Device IRQs
*/
#define SMC37c669_DEVICE_IRQ_A	    ( SMC37c669_DEVICE_IRQ( 0x01 ) )
#define SMC37c669_DEVICE_IRQ_B	    ( SMC37c669_DEVICE_IRQ( 0x02 ) )
#define SMC37c669_DEVICE_IRQ_C	    ( SMC37c669_DEVICE_IRQ( 0x03 ) )
#define SMC37c669_DEVICE_IRQ_D	    ( SMC37c669_DEVICE_IRQ( 0x04 ) )
#define SMC37c669_DEVICE_IRQ_E	    ( SMC37c669_DEVICE_IRQ( 0x05 ) )
#define SMC37c669_DEVICE_IRQ_F	    ( SMC37c669_DEVICE_IRQ( 0x06 ) )
/*      SMC37c669_DEVICE_IRQ_G	    *** RESERVED ***/
#define SMC37c669_DEVICE_IRQ_H	    ( SMC37c669_DEVICE_IRQ( 0x08 ) )

/*
** SMC 37c669 Device DMA Channel Definitions
*/
#define SMC37c669_DEVICE_DRQ_A		    ( SMC37c669_DEVICE_DRQ( 0x01 ) )
#define SMC37c669_DEVICE_DRQ_B		    ( SMC37c669_DEVICE_DRQ( 0x02 ) )
#define SMC37c669_DEVICE_DRQ_C		    ( SMC37c669_DEVICE_DRQ( 0x03 ) )

/*
** Configuration Register Index Definitions
*/
#define SMC37c669_CR00_INDEX	    0x00
#define SMC37c669_CR01_INDEX	    0x01
#define SMC37c669_CR02_INDEX	    0x02
#define SMC37c669_CR03_INDEX	    0x03
#define SMC37c669_CR04_INDEX	    0x04
#define SMC37c669_CR05_INDEX	    0x05
#define SMC37c669_CR06_INDEX	    0x06
#define SMC37c669_CR07_INDEX	    0x07
#define SMC37c669_CR08_INDEX	    0x08
#define SMC37c669_CR09_INDEX	    0x09
#define SMC37c669_CR0A_INDEX	    0x0A
#define SMC37c669_CR0B_INDEX	    0x0B
#define SMC37c669_CR0C_INDEX	    0x0C
#define SMC37c669_CR0D_INDEX	    0x0D
#define SMC37c669_CR0E_INDEX	    0x0E
#define SMC37c669_CR0F_INDEX	    0x0F
#define SMC37c669_CR10_INDEX	    0x10
#define SMC37c669_CR11_INDEX	    0x11
#define SMC37c669_CR12_INDEX	    0x12
#define SMC37c669_CR13_INDEX	    0x13
#define SMC37c669_CR14_INDEX	    0x14
#define SMC37c669_CR15_INDEX	    0x15
#define SMC37c669_CR16_INDEX	    0x16
#define SMC37c669_CR17_INDEX	    0x17
#define SMC37c669_CR18_INDEX	    0x18
#define SMC37c669_CR19_INDEX	    0x19
#define SMC37c669_CR1A_INDEX	    0x1A
#define SMC37c669_CR1B_INDEX	    0x1B
#define SMC37c669_CR1C_INDEX	    0x1C
#define SMC37c669_CR1D_INDEX	    0x1D
#define SMC37c669_CR1E_INDEX	    0x1E
#define SMC37c669_CR1F_INDEX	    0x1F
#define SMC37c669_CR20_INDEX	    0x20
#define SMC37c669_CR21_INDEX	    0x21
#define SMC37c669_CR22_INDEX	    0x22
#define SMC37c669_CR23_INDEX	    0x23
#define SMC37c669_CR24_INDEX	    0x24
#define SMC37c669_CR25_INDEX	    0x25
#define SMC37c669_CR26_INDEX	    0x26
#define SMC37c669_CR27_INDEX	    0x27
#define SMC37c669_CR28_INDEX	    0x28
#define SMC37c669_CR29_INDEX	    0x29

/*
** Configuration Register Alias Definitions
*/
#define SMC37c669_DEVICE_ID_INDEX		    SMC37c669_CR0D_INDEX
#define SMC37c669_DEVICE_REVISION_INDEX		    SMC37c669_CR0E_INDEX
#define SMC37c669_FDC_BASE_ADDRESS_INDEX	    SMC37c669_CR20_INDEX
#define SMC37c669_IDE_BASE_ADDRESS_INDEX	    SMC37c669_CR21_INDEX
#define SMC37c669_IDE_ALTERNATE_ADDRESS_INDEX	    SMC37c669_CR22_INDEX
#define SMC37c669_PARALLEL0_BASE_ADDRESS_INDEX	    SMC37c669_CR23_INDEX
#define SMC37c669_SERIAL0_BASE_ADDRESS_INDEX	    SMC37c669_CR24_INDEX
#define SMC37c669_SERIAL1_BASE_ADDRESS_INDEX	    SMC37c669_CR25_INDEX
#define SMC37c669_PARALLEL_FDC_DRQ_INDEX	    SMC37c669_CR26_INDEX
#define SMC37c669_PARALLEL_FDC_IRQ_INDEX	    SMC37c669_CR27_INDEX
#define SMC37c669_SERIAL_IRQ_INDEX		    SMC37c669_CR28_INDEX

/*
** Configuration Register Definitions
**
** The INDEX (write only) and DATA (read/write) ports are effective 
** only when the chip is in the Configuration State.
*/
typedef struct _SMC37c669_CONFIG_REGS {
    unsigned char index_port;
    unsigned char data_port;
} SMC37c669_CONFIG_REGS;

/*
** CR00 - default value 0x28
**
**  IDE_EN (CR00<1:0>):
**	0x - 30ua pull-ups on nIDEEN, nHDCS0, NHDCS1
**	11 - IRQ_H available as IRQ output,
**	     IRRX2, IRTX2 available as alternate IR pins
**	10 - nIDEEN, nHDCS0, nHDCS1 used to control IDE
**
**  VALID (CR00<7>):
**	A high level on this software controlled bit can
**	be used to indicate that a valid configuration
**	cycle has occurred.  The control software must
**	take care to set this bit at the appropriate times.
**	Set to zero after power up.  This bit has no
**	effect on any other hardware in the chip.
**
*/
typedef union _SMC37c669_CR00 {
    unsigned char as_uchar;
    struct {
    	unsigned ide_en : 2;	    /* See note above		*/
	unsigned reserved1 : 1;	    /* RAZ			*/
	unsigned fdc_pwr : 1;	    /* 1 = supply power to FDC  */
	unsigned reserved2 : 3;	    /* Read as 010b		*/
	unsigned valid : 1;	    /* See note above		*/
    }	by_field;
} SMC37c669_CR00;

/*
** CR01 - default value 0x9C
*/
typedef union _SMC37c669_CR01 {
    unsigned char as_uchar;
    struct {
    	unsigned reserved1 : 2;	    /* RAZ			    */
	unsigned ppt_pwr : 1;	    /* 1 = supply power to PPT	    */
	unsigned ppt_mode : 1;	    /* 1 = Printer mode, 0 = EPP    */
	unsigned reserved2 : 1;	    /* Read as 1		    */
	unsigned reserved3 : 2;	    /* RAZ			    */
	unsigned lock_crx: 1;	    /* Lock CR00 - CR18		    */
    }	by_field;
} SMC37c669_CR01;

/*
** CR02 - default value 0x88
*/
typedef union _SMC37c669_CR02 {
    unsigned char as_uchar;
    struct {
    	unsigned reserved1 : 3;	    /* RAZ			    */
	unsigned uart1_pwr : 1;	    /* 1 = supply power to UART1    */
	unsigned reserved2 : 3;	    /* RAZ			    */
	unsigned uart2_pwr : 1;	    /* 1 = supply power to UART2    */
    }	by_field;
} SMC37c669_CR02;

/*
** CR03 - default value 0x78
**
**  CR03<7>	CR03<2>	    Pin 94
**  -------	-------	    ------
**     0	   X	    DRV2 (input)
**     1	   0	    ADRX
**     1	   1	    IRQ_B
**
**  CR03<6>	CR03<5>	    Op Mode
**  -------	-------	    -------
**     0	   0	    Model 30
**     0	   1	    PS/2
**     1	   0	    Reserved
**     1	   1	    AT Mode
*/
typedef union _SMC37c669_CR03 {
    unsigned char as_uchar;
    struct {
    	unsigned pwrgd_gamecs : 1;  /* 1 = PWRGD, 0 = GAMECS	    */
	unsigned fdc_mode2 : 1;	    /* 1 = Enhanced Mode 2	    */
	unsigned pin94_0 : 1;	    /* See note above		    */
	unsigned reserved1 : 1;	    /* RAZ			    */
	unsigned drvden : 1;	    /* 1 = high, 0 - output	    */
	unsigned op_mode : 2;	    /* See note above		    */
	unsigned pin94_1 : 1;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR03;

/*
** CR04 - default value 0x00
**
**  PP_EXT_MODE:
**	If CR01<PP_MODE> = 0 and PP_EXT_MODE =
**	    00 - Standard and Bidirectional
**	    01 - EPP mode and SPP
**	    10 - ECP mode
**		 In this mode, 2 drives can be supported
**		 directly, 3 or 4 drives must use external
**		 4 drive support.  SPP can be selected
**		 through the ECR register of ECP as mode 000.
**	    11 - ECP mode and EPP mode
**		 In this mode, 2 drives can be supported
**		 directly, 3 or 4 drives must use external
**		 4 drive support.  SPP can be selected
**		 through the ECR register of ECP as mode 000.
**		 In this mode, EPP can be selected through
**		 the ECR register of ECP as mode 100.
**
**  PP_FDC:
**	00 - Normal
**	01 - PPFD1
**	10 - PPFD2
**	11 - Reserved
**
**  MIDI1:
**	Serial Clock Select: 
**	    A low level on this bit disables MIDI support,
**	    clock = divide by 13.  A high level on this 
**	    bit enables MIDI support, clock = divide by 12.
**
**	MIDI operates at 31.25 Kbps which can be derived 
**	from 125 KHz (24 MHz / 12 = 2 MHz, 2 MHz / 16 = 125 KHz)
**
**  ALT_IO:
**	0 - Use pins IRRX, IRTX
**	1 - Use pins IRRX2, IRTX2
**
**	If this bit is set, the IR receive and transmit
**	functions will not be available on pins 25 and 26
**	unless CR00<IDE_EN> = 11.
*/
typedef union _SMC37c669_CR04 {
    unsigned char as_uchar;
    struct {
    	unsigned ppt_ext_mode : 2;  /* See note above		    */
	unsigned ppt_fdc : 2;	    /* See note above		    */
	unsigned midi1 : 1;	    /* See note above		    */
	unsigned midi2 : 1;	    /* See note above		    */
	unsigned epp_type : 1;	    /* 0 = EPP 1.9, 1 = EPP 1.7	    */
	unsigned alt_io : 1;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR04;

/*
** CR05 - default value 0x00
**
**  DEN_SEL:
**	00 - Densel output normal
**	01 - Reserved
**	10 - Densel output 1
**	11 - Densel output 0
**
*/
typedef union _SMC37c669_CR05 {
    unsigned char as_uchar;
    struct {
    	unsigned reserved1 : 2;	    /* RAZ					*/
	unsigned fdc_dma_mode : 1;  /* 0 = burst, 1 = non-burst			*/
	unsigned den_sel : 2;	    /* See note above				*/
	unsigned swap_drv : 1;	    /* Swap the FDC motor selects		*/
	unsigned extx4 : 1;	    /* 0 = 2 drive, 1 = external 4 drive decode	*/
	unsigned reserved2 : 1;	    /* RAZ					*/
    }	by_field;
} SMC37c669_CR05;

/*
** CR06 - default value 0xFF
*/
typedef union _SMC37c669_CR06 {
    unsigned char as_uchar;
    struct {
    	unsigned floppy_a : 2;	    /* Type of floppy drive A	    */
	unsigned floppy_b : 2;	    /* Type of floppy drive B	    */
	unsigned floppy_c : 2;	    /* Type of floppy drive C	    */
	unsigned floppy_d : 2;	    /* Type of floppy drive D	    */
    }	by_field;
} SMC37c669_CR06;

/*
** CR07 - default value 0x00
**
**  Auto Power Management CR07<7:4>:
**	0 - Auto Powerdown disabled (default)
**	1 - Auto Powerdown enabled
**
**	This bit is reset to the default state by POR or
**	a hardware reset.
**
*/
typedef union _SMC37c669_CR07 {
    unsigned char as_uchar;
    struct {
    	unsigned floppy_boot : 2;   /* 0 = A:, 1 = B:		    */
	unsigned reserved1 : 2;	    /* RAZ			    */
	unsigned ppt_en : 1;	    /* See note above		    */
	unsigned uart1_en : 1;	    /* See note above		    */
	unsigned uart2_en : 1;	    /* See note above		    */
	unsigned fdc_en : 1;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR07;

/*
** CR08 - default value 0x00
*/
typedef union _SMC37c669_CR08 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 4;	    /* 0			    */
	unsigned addrx7_4 : 4;	    /* ADR<7:3> for ADRx decode	    */
    }	by_field;
} SMC37c669_CR08;

/*
** CR09 - default value 0x00
**
**  ADRx_CONFIG:
**	00 - ADRx disabled
**	01 - 1 byte decode A<3:0> = 0000b
**	10 - 8 byte block decode A<3:0> = 0XXXb
**	11 - 16 byte block decode A<3:0> = XXXXb
**
*/
typedef union _SMC37c669_CR09 {
    unsigned char as_uchar;
    struct {
    	unsigned adra8 : 3;	    /* ADR<10:8> for ADRx decode    */
	unsigned reserved1 : 3;
	unsigned adrx_config : 2;   /* See note above		    */
    }	by_field;
} SMC37c669_CR09;

/*
** CR0A - default value 0x00
*/
typedef union _SMC37c669_CR0A {
    unsigned char as_uchar;
    struct {
    	unsigned ecp_fifo_threshold : 4;
	unsigned reserved1 : 4;
    }	by_field;
} SMC37c669_CR0A;

/*
** CR0B - default value 0x00
*/
typedef union _SMC37c669_CR0B {
    unsigned char as_uchar;
    struct {
    	unsigned fdd0_drtx : 2;	    /* FDD0 Data Rate Table	    */
	unsigned fdd1_drtx : 2;	    /* FDD1 Data Rate Table	    */
	unsigned fdd2_drtx : 2;	    /* FDD2 Data Rate Table	    */
	unsigned fdd3_drtx : 2;	    /* FDD3 Data Rate Table	    */
    }	by_field;
} SMC37c669_CR0B;

/*
** CR0C - default value 0x00
**
**  UART2_MODE:
**	000 - Standard (default)
**	001 - IrDA (HPSIR)
**	010 - Amplitude Shift Keyed IR @500 KHz
**	011 - Reserved
**	1xx - Reserved
**
*/
typedef union _SMC37c669_CR0C {
    unsigned char as_uchar;
    struct {
    	unsigned uart2_rcv_polarity : 1;    /* 1 = invert RX		*/
	unsigned uart2_xmit_polarity : 1;   /* 1 = invert TX		*/
	unsigned uart2_duplex : 1;	    /* 1 = full, 0 = half	*/
	unsigned uart2_mode : 3;	    /* See note above		*/
	unsigned uart1_speed : 1;	    /* 1 = high speed enabled	*/
	unsigned uart2_speed : 1;	    /* 1 = high speed enabled	*/
    }	by_field;
} SMC37c669_CR0C;

/*
** CR0D - default value 0x03
**
**  Device ID Register - read only
*/
typedef union _SMC37c669_CR0D {
    unsigned char as_uchar;
    struct {
    	unsigned device_id : 8;	    /* Returns 0x3 in this field    */
    }	by_field;
} SMC37c669_CR0D;

/*
** CR0E - default value 0x02
**
**  Device Revision Register - read only
*/
typedef union _SMC37c669_CR0E {
    unsigned char as_uchar;
    struct {
    	unsigned device_rev : 8;    /* Returns 0x2 in this field    */
    }	by_field;
} SMC37c669_CR0E;

/*
** CR0F - default value 0x00
*/
typedef union _SMC37c669_CR0F {
    unsigned char as_uchar;
    struct {
    	unsigned test0 : 1;	    /* Reserved - set to 0	    */
	unsigned test1 : 1;	    /* Reserved - set to 0	    */
	unsigned test2 : 1;	    /* Reserved - set to 0	    */
	unsigned test3 : 1;	    /* Reserved - set t0 0	    */
	unsigned test4 : 1;	    /* Reserved - set to 0	    */
	unsigned test5 : 1;	    /* Reserved - set t0 0	    */
	unsigned test6 : 1;	    /* Reserved - set t0 0	    */
	unsigned test7 : 1;	    /* Reserved - set to 0	    */
    }	by_field;
} SMC37c669_CR0F;

/*
** CR10 - default value 0x00
*/
typedef union _SMC37c669_CR10 {
    unsigned char as_uchar;
    struct {
    	unsigned reserved1 : 3;	     /* RAZ			    */
	unsigned pll_gain : 1;	     /* 1 = 3V, 2 = 5V operation    */
	unsigned pll_stop : 1;	     /* 1 = stop PLLs		    */
	unsigned ace_stop : 1;	     /* 1 = stop UART clocks	    */
	unsigned pll_clock_ctrl : 1; /* 0 = 14.318 MHz, 1 = 24 MHz  */
	unsigned ir_test : 1;	     /* Enable IR test mode	    */
    }	by_field;
} SMC37c669_CR10;

/*
** CR11 - default value 0x00
*/
typedef union _SMC37c669_CR11 {
    unsigned char as_uchar;
    struct {
    	unsigned ir_loopback : 1;   /* Internal IR loop back		    */
	unsigned test_10ms : 1;	    /* Test 10ms autopowerdown FDC timeout  */
	unsigned reserved1 : 6;	    /* RAZ				    */
    }	by_field;
} SMC37c669_CR11;

/*
** CR12 - CR1D are reserved registers
*/

/*
** CR1E - default value 0x80
**
**  GAMECS:
**	00 - GAMECS disabled
**	01 - 1 byte decode ADR<3:0> = 0001b
**	10 - 8 byte block decode ADR<3:0> = 0XXXb
**	11 - 16 byte block decode ADR<3:0> = XXXXb
**
*/
typedef union _SMC37c66_CR1E {
    unsigned char as_uchar;
    struct {
    	unsigned gamecs_config: 2;   /* See note above		    */
	unsigned gamecs_addr9_4 : 6; /* GAMECS Addr<9:4>	    */
    }	by_field;
} SMC37c669_CR1E;

/*
** CR1F - default value 0x00
**
**  DT0 DT1 DRVDEN0 DRVDEN1 Drive Type
**  --- --- ------- ------- ----------
**   0   0  DENSEL  DRATE0  4/2/1 MB 3.5"
**                          2/1 MB 5.25"
**                          2/1.6/1 MB 3.5" (3-mode)
**   0   1  DRATE1  DRATE0
**   1   0  nDENSEL DRATE0  PS/2
**   1   1  DRATE0  DRATE1
**
**  Note: DENSEL, DRATE1, and DRATE0 map onto two output
**	  pins - DRVDEN0 and DRVDEN1.
**
*/
typedef union _SMC37c669_CR1F {
    unsigned char as_uchar;
    struct {
    	unsigned fdd0_drive_type : 2;	/* FDD0 drive type	    */
	unsigned fdd1_drive_type : 2;	/* FDD1 drive type	    */
	unsigned fdd2_drive_type : 2;	/* FDD2 drive type	    */
	unsigned fdd3_drive_type : 2;	/* FDD3 drive type	    */
    }	by_field;
} SMC37c669_CR1F;

/*
** CR20 - default value 0x3C
**
**  FDC Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0, A<3:0> = 0XXXb to access.
**
*/
typedef union _SMC37c669_CR20 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 2;	    /* 0			    */
	unsigned addr9_4 : 6;	    /* FDC Addr<9:4>		    */
    }	by_field;
} SMC37c669_CR20;

/*
** CR21 - default value 0x3C
**
**  IDE Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0, A<3:0> = 0XXXb to access.
**
*/
typedef union _SMC37c669_CR21 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 2;	    /* 0			    */
	unsigned addr9_4 : 6;	    /* IDE Addr<9:4>		    */
    }	by_field;
} SMC37c669_CR21;

/*
** CR22 - default value 0x3D
**
**  IDE Alternate Status Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0, A<3:0> = 0110b to access.
**
*/
typedef union _SMC37c669_CR22 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 2;	    /* 0			    */
	unsigned addr9_4 : 6;	    /* IDE Alt Status Addr<9:4>	    */
    }	by_field;
} SMC37c669_CR22;

/*
** CR23 - default value 0x00
**
**  Parallel Port Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0 to access.
**	- If EPP is enabled, A<2:0> = XXXb to access.
**	  If EPP is NOT enabled, A<1:0> = XXb to access
**
*/
typedef union _SMC37c669_CR23 {
    unsigned char as_uchar;
    struct {
	unsigned addr9_2 : 8;	    /* Parallel Port Addr<9:2>	    */
    }	by_field;
} SMC37c669_CR23;

/*
** CR24 - default value 0x00
**
**  UART1 Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0, A<2:0> = XXXb to access.
**
*/
typedef union _SMC37c669_CR24 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 1;	    /* 0			    */
	unsigned addr9_3 : 7;	    /* UART1 Addr<9:3>		    */
    }	by_field;
} SMC37c669_CR24;

/*
** CR25 - default value 0x00
**
**  UART2 Base Address Register
**	- To disable this decode set Addr<9:8> = 0
**	- A<10> = 0, A<2:0> = XXXb to access.
**
*/
typedef union _SMC37c669_CR25 {
    unsigned char as_uchar;
    struct {
    	unsigned zero : 1;	    /* 0			    */
	unsigned addr9_3 : 7;	    /* UART2 Addr<9:3>		    */
    }	by_field;
} SMC37c669_CR25;

/*
** CR26 - default value 0x00
**
**  Parallel Port / FDC DMA Select Register
**
**  D3 - D0	  DMA
**  D7 - D4	Selected
**  -------	--------
**   0000	 None
**   0001	 DMA_A
**   0010	 DMA_B
**   0011	 DMA_C
**
*/
typedef union _SMC37c669_CR26 {
    unsigned char as_uchar;
    struct {
    	unsigned ppt_drq : 4;	    /* See note above		    */
	unsigned fdc_drq : 4;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR26;

/*
** CR27 - default value 0x00
**
**  Parallel Port / FDC IRQ Select Register
**
**  D3 - D0	  IRQ
**  D7 - D4	Selected
**  -------	--------
**   0000	 None
**   0001	 IRQ_A
**   0010	 IRQ_B
**   0011	 IRQ_C
**   0100	 IRQ_D
**   0101	 IRQ_E
**   0110	 IRQ_F
**   0111	 Reserved
**   1000	 IRQ_H
**
**  Any unselected IRQ REQ is in tristate
**
*/
typedef union _SMC37c669_CR27 {
    unsigned char as_uchar;
    struct {
    	unsigned ppt_irq : 4;	    /* See note above		    */
	unsigned fdc_irq : 4;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR27;

/*
** CR28 - default value 0x00
**
**  UART IRQ Select Register
**
**  D3 - D0	  IRQ
**  D7 - D4	Selected
**  -------	--------
**   0000	 None
**   0001	 IRQ_A
**   0010	 IRQ_B
**   0011	 IRQ_C
**   0100	 IRQ_D
**   0101	 IRQ_E
**   0110	 IRQ_F
**   0111	 Reserved
**   1000	 IRQ_H
**   1111	 share with UART1 (only for UART2)
**
**  Any unselected IRQ REQ is in tristate
**
**  To share an IRQ between UART1 and UART2, set
**  UART1 to use the desired IRQ and set UART2 to
**  0xF to enable sharing mechanism.
**
*/
typedef union _SMC37c669_CR28 {
    unsigned char as_uchar;
    struct {
    	unsigned uart2_irq : 4;	    /* See note above		    */
	unsigned uart1_irq : 4;	    /* See note above		    */
    }	by_field;
} SMC37c669_CR28;

/*
** CR29 - default value 0x00
**
**  IRQIN IRQ Select Register
**
**  D3 - D0	  IRQ
**  D7 - D4	Selected
**  -------	--------
**   0000	 None
**   0001	 IRQ_A
**   0010	 IRQ_B
**   0011	 IRQ_C
**   0100	 IRQ_D
**   0101	 IRQ_E
**   0110	 IRQ_F
**   0111	 Reserved
**   1000	 IRQ_H
**
**  Any unselected IRQ REQ is in tristate
**
*/
typedef union _SMC37c669_CR29 {
    unsigned char as_uchar;
    struct {
    	unsigned irqin_irq : 4;	    /* See note above		    */
	unsigned reserved1 : 4;	    /* RAZ			    */
    }	by_field;
} SMC37c669_CR29;

/*
** Aliases of Configuration Register formats (should match
** the set of index aliases).
**
** Note that CR24 and CR25 have the same format and are the
** base address registers for UART1 and UART2.  Because of
** this we only define 1 alias here - for CR24 - as the serial
** base address register.
**
** Note that CR21 and CR22 have the same format and are the
** base address and alternate status address registers for
** the IDE controller.  Because of this we only define 1 alias
** here - for CR21 - as the IDE address register.
**
*/
typedef SMC37c669_CR0D SMC37c669_DEVICE_ID_REGISTER;
typedef SMC37c669_CR0E SMC37c669_DEVICE_REVISION_REGISTER;
typedef SMC37c669_CR20 SMC37c669_FDC_BASE_ADDRESS_REGISTER;
typedef SMC37c669_CR21 SMC37c669_IDE_ADDRESS_REGISTER;
typedef SMC37c669_CR23 SMC37c669_PARALLEL_BASE_ADDRESS_REGISTER;
typedef SMC37c669_CR24 SMC37c669_SERIAL_BASE_ADDRESS_REGISTER;
typedef SMC37c669_CR26 SMC37c669_PARALLEL_FDC_DRQ_REGISTER;
typedef SMC37c669_CR27 SMC37c669_PARALLEL_FDC_IRQ_REGISTER;
typedef SMC37c669_CR28 SMC37c669_SERIAL_IRQ_REGISTER;

/*
** ISA/Device IRQ Translation Table Entry Definition
*/
typedef struct _SMC37c669_IRQ_TRANSLATION_ENTRY {
    int device_irq;
    int isa_irq;
} SMC37c669_IRQ_TRANSLATION_ENTRY;

/*
** ISA/Device DMA Translation Table Entry Definition
*/
typedef struct _SMC37c669_DRQ_TRANSLATION_ENTRY {
    int device_drq;
    int isa_drq;
} SMC37c669_DRQ_TRANSLATION_ENTRY;

/*
** External Interface Function Prototype Declarations
*/

SMC37c669_CONFIG_REGS *SMC37c669_detect( 
    int
);

unsigned int SMC37c669_enable_device( 
    unsigned int func 
);

unsigned int SMC37c669_disable_device( 
    unsigned int func 
);

unsigned int SMC37c669_configure_device( 
    unsigned int func, 
    int port, 
    int irq, 
    int drq 
);

void SMC37c669_display_device_info( 
    void 
);

#endif	/* __SMC37c669_H */

/* file:	smcc669.c
 *
 * Copyright (C) 1997 by
 * Digital Equipment Corporation, Maynard, Massachusetts.
 * All rights reserved.
 *
 * This software is furnished under a license and may be used and copied
 * only  in  accordance  of  the  terms  of  such  license  and with the
 * inclusion of the above copyright notice. This software or  any  other
 * copies thereof may not be provided or otherwise made available to any
 * other person.  No title to and  ownership of the  software is  hereby
 * transferred.
 *
 * The information in this software is  subject to change without notice
 * and  should  not  be  construed  as a commitment by digital equipment
 * corporation.
 *
 * Digital assumes no responsibility for the use  or  reliability of its
 * software on equipment which is not supplied by digital.
 */

/*
 *++
 *  FACILITY:
 *
 *      Alpha SRM Console Firmware
 *
 *  MODULE DESCRIPTION:
 *
 *	SMC37c669 Super I/O controller configuration routines.
 *
 *  AUTHORS:
 *
 *	Eric Rasmussen
 *
 *  CREATION DATE:
 *  
 *	28-Jan-1997
 *
 *  MODIFICATION HISTORY:
 *	
 *	er	01-May-1997	Fixed pointer conversion errors in 
 *				SMC37c669_get_device_config().
 *      er	28-Jan-1997	Initial version.
 *
 *--
 */
#if 0
/* $INCLUDE_OPTIONS$ */
#include    "cp$inc:platform_io.h"
/* $INCLUDE_OPTIONS_END$ */
#include    "cp$src:common.h"
#include    "cp$inc:prototypes.h"
#include    "cp$src:kernel_def.h"
#include    "cp$src:msg_def.h"
#include    "cp$src:smcc669_def.h"
/* Platform-specific includes */
#include    "cp$src:platform.h"
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define wb( _x_, _y_ )	outb( _y_, (unsigned int)((unsigned long)_x_) )
#define rb( _x_ )	inb( (unsigned int)((unsigned long)_x_) )

/*
** Local storage for device configuration information.
**
** Since the SMC37c669 does not provide an explicit
** mechanism for enabling/disabling individual device 
** functions, other than unmapping the device, local 
** storage for device configuration information is 
** allocated here for use in implementing our own 
** function enable/disable scheme.
*/
static struct DEVICE_CONFIG {
    unsigned int port1;
    unsigned int port2;
    int irq;
    int drq;
} local_config [NUM_FUNCS];

/*
** List of all possible addresses for the Super I/O chip
*/
static unsigned long SMC37c669_Addresses[] __initdata =
    {
	0x3F0UL,	    /* Primary address	    */
	0x370UL,	    /* Secondary address    */
	0UL		    /* End of list	    */
    };

/*
** Global Pointer to the Super I/O device
*/
static SMC37c669_CONFIG_REGS *SMC37c669 __initdata = NULL;

/*
** IRQ Translation Table
**
** The IRQ translation table is a list of SMC37c669 device 
** and standard ISA IRQs.
**
*/
static SMC37c669_IRQ_TRANSLATION_ENTRY *SMC37c669_irq_table __initdata; 

/*
** The following definition is for the default IRQ 
** translation table.
*/
static SMC37c669_IRQ_TRANSLATION_ENTRY SMC37c669_default_irq_table[]
__initdata = 
    { 
	{ SMC37c669_DEVICE_IRQ_A, -1 }, 
	{ SMC37c669_DEVICE_IRQ_B, -1 }, 
	{ SMC37c669_DEVICE_IRQ_C, 7 }, 
	{ SMC37c669_DEVICE_IRQ_D, 6 }, 
	{ SMC37c669_DEVICE_IRQ_E, 4 }, 
	{ SMC37c669_DEVICE_IRQ_F, 3 }, 
	{ SMC37c669_DEVICE_IRQ_H, -1 }, 
	{ -1, -1 } /* End of table */
    };

/*
** The following definition is for the MONET (XP1000) IRQ 
** translation table.
*/
static SMC37c669_IRQ_TRANSLATION_ENTRY SMC37c669_monet_irq_table[]
__initdata = 
    { 
	{ SMC37c669_DEVICE_IRQ_A, -1 }, 
	{ SMC37c669_DEVICE_IRQ_B, -1 }, 
	{ SMC37c669_DEVICE_IRQ_C, 6 }, 
	{ SMC37c669_DEVICE_IRQ_D, 7 }, 
	{ SMC37c669_DEVICE_IRQ_E, 4 }, 
	{ SMC37c669_DEVICE_IRQ_F, 3 }, 
	{ SMC37c669_DEVICE_IRQ_H, -1 }, 
	{ -1, -1 } /* End of table */
    };

static SMC37c669_IRQ_TRANSLATION_ENTRY *SMC37c669_irq_tables[] __initdata =
    {
	SMC37c669_default_irq_table,
	SMC37c669_monet_irq_table
    }; 

/*
** DRQ Translation Table
**
** The DRQ translation table is a list of SMC37c669 device and
** ISA DMA channels.
**
*/
static SMC37c669_DRQ_TRANSLATION_ENTRY *SMC37c669_drq_table __initdata;

/*
** The following definition is the default DRQ
** translation table.
*/
static SMC37c669_DRQ_TRANSLATION_ENTRY SMC37c669_default_drq_table[]
__initdata = 
    { 
	{ SMC37c669_DEVICE_DRQ_A, 2 }, 
	{ SMC37c669_DEVICE_DRQ_B, 3 }, 
	{ SMC37c669_DEVICE_DRQ_C, -1 }, 
	{ -1, -1 } /* End of table */
    };

/*
** Local Function Prototype Declarations
*/

static unsigned int SMC37c669_is_device_enabled( 
    unsigned int func 
);

#if 0
static unsigned int SMC37c669_get_device_config( 
    unsigned int func, 
    int *port, 
    int *irq, 
    int *drq 
);
#endif

static void SMC37c669_config_mode( 
    unsigned int enable 
);

static unsigned char SMC37c669_read_config( 
    unsigned char index 
);

static void SMC37c669_write_config( 
    unsigned char index, 
    unsigned char data 
);

static void SMC37c669_init_local_config( void );

static struct DEVICE_CONFIG *SMC37c669_get_config(
    unsigned int func
);

static int SMC37c669_xlate_irq(
    int irq 
);

static int SMC37c669_xlate_drq(
    int drq 
);

static  __cacheline_aligned DEFINE_SPINLOCK(smc_lock);

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function detects the presence of an SMC37c669 Super I/O
**	controller.
**
**  FORMAL PARAMETERS:
**
**	None
**
**  RETURN VALUE:
**
**      Returns a pointer to the device if found, otherwise,
**	the NULL pointer is returned.
**
**  SIDE EFFECTS:
**
**      None
**
**--
*/
SMC37c669_CONFIG_REGS * __init SMC37c669_detect( int index )
{
    int i;
    SMC37c669_DEVICE_ID_REGISTER id;

    for ( i = 0;  SMC37c669_Addresses[i] != 0;  i++ ) {
/*
** Initialize the device pointer even though we don't yet know if
** the controller is at this address.  The support functions access
** the controller through this device pointer so we need to set it
** even when we are looking ...
*/
    	SMC37c669 = ( SMC37c669_CONFIG_REGS * )SMC37c669_Addresses[i];
/*
** Enter configuration mode
*/
	SMC37c669_config_mode( TRUE );
/*
** Read the device id
*/
	id.as_uchar = SMC37c669_read_config( SMC37c669_DEVICE_ID_INDEX );
/*
** Exit configuration mode
*/
	SMC37c669_config_mode( FALSE );
/*
** Does the device id match?  If so, assume we have found an
** SMC37c669 controller at this address.
*/
	if ( id.by_field.device_id == SMC37c669_DEVICE_ID ) {
/*
** Initialize the IRQ and DRQ translation tables.
*/
    	    SMC37c669_irq_table = SMC37c669_irq_tables[ index ];
	    SMC37c669_drq_table = SMC37c669_default_drq_table;
/*
** erfix
**
** If the platform can't use the IRQ and DRQ defaults set up in this 
** file, it should call a platform-specific external routine at this 
** point to reset the IRQ and DRQ translation table pointers to point 
** at the appropriate tables for the platform.  If the defaults are 
** acceptable, then the external routine should do nothing.
*/

/*
** Put the chip back into configuration mode
*/
	    SMC37c669_config_mode( TRUE );
/*
** Initialize local storage for configuration information
*/
	    SMC37c669_init_local_config( );
/*
** Exit configuration mode
*/
	    SMC37c669_config_mode( FALSE );
/*
** SMC37c669 controller found, break out of search loop
*/
	    break;
	}
	else {
/*
** Otherwise, we did not find an SMC37c669 controller at this
** address so set the device pointer to NULL.
*/
	    SMC37c669 = NULL;
	}
    }
    return SMC37c669;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function enables an SMC37c669 device function.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which device function to enable
**
**  RETURN VALUE:
**
**      Returns TRUE is the device function was enabled, otherwise, FALSE
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**  DESIGN:
**
**      Enabling a device function in the SMC37c669 controller involves
**	setting all of its mappings (port, irq, drq ...).  A local 
**	"shadow" copy of the device configuration is kept so we can
**	just set each mapping to what the local copy says.
**
**	This function ALWAYS updates the local shadow configuration of
**	the device function being enabled, even if the device is always
**	enabled.  To avoid replication of code, functions such as
**	configure_device set up the local copy and then call this 
**	function to the update the real device.
**
**--
*/
unsigned int __init SMC37c669_enable_device ( unsigned int func )
{
    unsigned int ret_val = FALSE;
/*
** Put the device into configuration mode
*/
    SMC37c669_config_mode( TRUE );
    switch ( func ) {
    	case SERIAL_0:
	    {
	    	SMC37c669_SERIAL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_SERIAL_IRQ_REGISTER irq;
/*
** Enable the serial 1 IRQ mapping
*/
	    	irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_SERIAL_IRQ_INDEX );

		irq.by_field.uart1_irq =
		    SMC37c669_RAW_DEVICE_IRQ(
			SMC37c669_xlate_irq( local_config[ func ].irq )
		    );

		SMC37c669_write_config( SMC37c669_SERIAL_IRQ_INDEX, irq.as_uchar );
/*
** Enable the serial 1 port base address mapping
*/
		base_addr.as_uchar = 0;
		base_addr.by_field.addr9_3 = local_config[ func ].port1 >> 3;

		SMC37c669_write_config( 
		    SMC37c669_SERIAL0_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case SERIAL_1:
	    {
	    	SMC37c669_SERIAL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_SERIAL_IRQ_REGISTER irq;
/*
** Enable the serial 2 IRQ mapping
*/
	    	irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_SERIAL_IRQ_INDEX );

		irq.by_field.uart2_irq =
		    SMC37c669_RAW_DEVICE_IRQ(
			SMC37c669_xlate_irq( local_config[ func ].irq )
		    );

		SMC37c669_write_config( SMC37c669_SERIAL_IRQ_INDEX, irq.as_uchar );
/*
** Enable the serial 2 port base address mapping
*/
		base_addr.as_uchar = 0;
		base_addr.by_field.addr9_3 = local_config[ func ].port1 >> 3;

		SMC37c669_write_config( 
		    SMC37c669_SERIAL1_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case PARALLEL_0:
	    {
	    	SMC37c669_PARALLEL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_PARALLEL_FDC_IRQ_REGISTER irq;
		SMC37c669_PARALLEL_FDC_DRQ_REGISTER drq;
/*
** Enable the parallel port DMA channel mapping
*/
	    	drq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_DRQ_INDEX );

		drq.by_field.ppt_drq = 
		    SMC37c669_RAW_DEVICE_DRQ(
			SMC37c669_xlate_drq( local_config[ func ].drq )
		    );

		SMC37c669_write_config(
		    SMC37c669_PARALLEL_FDC_DRQ_INDEX,
		    drq.as_uchar
		);
/*
** Enable the parallel port IRQ mapping
*/
		irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_IRQ_INDEX );

		irq.by_field.ppt_irq =
		    SMC37c669_RAW_DEVICE_IRQ(
			SMC37c669_xlate_irq( local_config[ func ].irq )
		    );

		SMC37c669_write_config( 
		    SMC37c669_PARALLEL_FDC_IRQ_INDEX,
		    irq.as_uchar
		);
/*
** Enable the parallel port base address mapping
*/
		base_addr.as_uchar = 0;
		base_addr.by_field.addr9_2 = local_config[ func ].port1 >> 2;

		SMC37c669_write_config(
		    SMC37c669_PARALLEL0_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case FLOPPY_0:
	    {
	    	SMC37c669_FDC_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_PARALLEL_FDC_IRQ_REGISTER irq;
		SMC37c669_PARALLEL_FDC_DRQ_REGISTER drq;
/*
** Enable the floppy controller DMA channel mapping
*/
	    	drq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_DRQ_INDEX );
		 
		drq.by_field.fdc_drq =
		    SMC37c669_RAW_DEVICE_DRQ(
			SMC37c669_xlate_drq( local_config[ func ].drq )
		    );
		 
		SMC37c669_write_config( 
		    SMC37c669_PARALLEL_FDC_DRQ_INDEX,
		    drq.as_uchar
		);
/*
** Enable the floppy controller IRQ mapping
*/
		irq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_IRQ_INDEX );
		 
		irq.by_field.fdc_irq =
		    SMC37c669_RAW_DEVICE_IRQ(
			SMC37c669_xlate_irq( local_config[ func ].irq )
		    );
		 
		SMC37c669_write_config(
		    SMC37c669_PARALLEL_FDC_IRQ_INDEX,
		    irq.as_uchar
		);
/*
** Enable the floppy controller base address mapping
*/
		base_addr.as_uchar = 0;
		base_addr.by_field.addr9_4 = local_config[ func ].port1 >> 4;
		 
		SMC37c669_write_config(
		    SMC37c669_FDC_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case IDE_0:
	    {
	    	SMC37c669_IDE_ADDRESS_REGISTER ide_addr;
/*
** Enable the IDE alternate status base address mapping
*/
	    	ide_addr.as_uchar = 0;
		ide_addr.by_field.addr9_4 = local_config[ func ].port2 >> 4;
		 
		SMC37c669_write_config(
		    SMC37c669_IDE_ALTERNATE_ADDRESS_INDEX,
		    ide_addr.as_uchar
		);
/*
** Enable the IDE controller base address mapping
*/
		ide_addr.as_uchar = 0;
		ide_addr.by_field.addr9_4 = local_config[ func ].port1 >> 4;
		 
		SMC37c669_write_config(
		    SMC37c669_IDE_BASE_ADDRESS_INDEX,
		    ide_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
    }
/*
** Exit configuration mode and return
*/
    SMC37c669_config_mode( FALSE );

    return ret_val;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function disables a device function within the
**	SMC37c669 Super I/O controller.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which function to disable
**
**  RETURN VALUE:
**
**      Return TRUE if the device function was disabled, otherwise, FALSE
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**  DESIGN:
**
**      Disabling a function in the SMC37c669 device involves
**	disabling all the function's mappings (port, irq, drq ...).
**	A shadow copy of the device configuration is maintained
**	in local storage so we won't worry aboving saving the
**	current configuration information.
**
**--
*/
unsigned int __init SMC37c669_disable_device ( unsigned int func )
{
    unsigned int ret_val = FALSE;

/*
** Put the device into configuration mode
*/
    SMC37c669_config_mode( TRUE );
    switch ( func ) {
    	case SERIAL_0:
	    {
	    	SMC37c669_SERIAL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_SERIAL_IRQ_REGISTER irq;
/*
** Disable the serial 1 IRQ mapping
*/
	    	irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_SERIAL_IRQ_INDEX );

		irq.by_field.uart1_irq = 0;

		SMC37c669_write_config( SMC37c669_SERIAL_IRQ_INDEX, irq.as_uchar );
/*
** Disable the serial 1 port base address mapping
*/
		base_addr.as_uchar = 0;
		SMC37c669_write_config( 
		    SMC37c669_SERIAL0_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case SERIAL_1:
	    {
	    	SMC37c669_SERIAL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_SERIAL_IRQ_REGISTER irq;
/*
** Disable the serial 2 IRQ mapping
*/
	    	irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_SERIAL_IRQ_INDEX );

		irq.by_field.uart2_irq = 0;

		SMC37c669_write_config( SMC37c669_SERIAL_IRQ_INDEX, irq.as_uchar );
/*
** Disable the serial 2 port base address mapping
*/
		base_addr.as_uchar = 0;

		SMC37c669_write_config( 
		    SMC37c669_SERIAL1_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case PARALLEL_0:
	    {
	    	SMC37c669_PARALLEL_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_PARALLEL_FDC_IRQ_REGISTER irq;
		SMC37c669_PARALLEL_FDC_DRQ_REGISTER drq;
/*
** Disable the parallel port DMA channel mapping
*/
	    	drq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_DRQ_INDEX );

		drq.by_field.ppt_drq = 0;

		SMC37c669_write_config(
		    SMC37c669_PARALLEL_FDC_DRQ_INDEX,
		    drq.as_uchar
		);
/*
** Disable the parallel port IRQ mapping
*/
		irq.as_uchar = 
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_IRQ_INDEX );

		irq.by_field.ppt_irq = 0;

		SMC37c669_write_config( 
		    SMC37c669_PARALLEL_FDC_IRQ_INDEX,
		    irq.as_uchar
		);
/*
** Disable the parallel port base address mapping
*/
		base_addr.as_uchar = 0;

		SMC37c669_write_config(
		    SMC37c669_PARALLEL0_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case FLOPPY_0:
	    {
	    	SMC37c669_FDC_BASE_ADDRESS_REGISTER base_addr;
		SMC37c669_PARALLEL_FDC_IRQ_REGISTER irq;
		SMC37c669_PARALLEL_FDC_DRQ_REGISTER drq;
/*
** Disable the floppy controller DMA channel mapping
*/
	    	drq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_DRQ_INDEX );
		 
		drq.by_field.fdc_drq = 0;
		 
		SMC37c669_write_config( 
		    SMC37c669_PARALLEL_FDC_DRQ_INDEX,
		    drq.as_uchar
		);
/*
** Disable the floppy controller IRQ mapping
*/
		irq.as_uchar =
		    SMC37c669_read_config( SMC37c669_PARALLEL_FDC_IRQ_INDEX );
		 
		irq.by_field.fdc_irq = 0;
		 
		SMC37c669_write_config(
		    SMC37c669_PARALLEL_FDC_IRQ_INDEX,
		    irq.as_uchar
		);
/*
** Disable the floppy controller base address mapping
*/
		base_addr.as_uchar = 0;
		 
		SMC37c669_write_config(
		    SMC37c669_FDC_BASE_ADDRESS_INDEX,
		    base_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
	case IDE_0:
	    {
	    	SMC37c669_IDE_ADDRESS_REGISTER ide_addr;
/*
** Disable the IDE alternate status base address mapping
*/
	    	ide_addr.as_uchar = 0;
		 
		SMC37c669_write_config(
		    SMC37c669_IDE_ALTERNATE_ADDRESS_INDEX,
		    ide_addr.as_uchar
		);
/*
** Disable the IDE controller base address mapping
*/
		ide_addr.as_uchar = 0;
		 
		SMC37c669_write_config(
		    SMC37c669_IDE_BASE_ADDRESS_INDEX,
		    ide_addr.as_uchar
		);
		ret_val = TRUE;
		break;
	    }
    }
/*
** Exit configuration mode and return
*/
    SMC37c669_config_mode( FALSE );

    return ret_val;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function configures a device function within the 
**	SMC37c669 Super I/O controller.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which device function
**       
**      port:
**          I/O port for the function to use
**	 
**      irq:
**          IRQ for the device function to use
**	 
**      drq:
**          DMA channel for the device function to use
**
**  RETURN VALUE:
**
**      Returns TRUE if the device function was configured, 
**	otherwise, FALSE.
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**  DESIGN:
**
**	If this function returns TRUE, the local shadow copy of
**	the configuration is also updated.  If the device function
**	is currently disabled, only the local shadow copy is 
**	updated and the actual device function will be updated
**	if/when it is enabled.
**
**--
*/
unsigned int __init SMC37c669_configure_device (
    unsigned int func,
    int port,
    int irq,
    int drq )
{
    struct DEVICE_CONFIG *cp;

/*
** Check for a valid configuration
*/
    if ( ( cp = SMC37c669_get_config ( func ) ) != NULL ) {
/*
** Configuration is valid, update the local shadow copy
*/
    	if ( ( drq & ~0xFF ) == 0 ) {
	    cp->drq = drq;
	}
	if ( ( irq & ~0xFF ) == 0 ) {
	    cp->irq = irq;
	}
	if ( ( port & ~0xFFFF ) == 0 ) {
	    cp->port1 = port;
	}
/*
** If the device function is enabled, update the actual
** device configuration.
*/
	if ( SMC37c669_is_device_enabled( func ) ) {
	    SMC37c669_enable_device( func );
	}
	return TRUE;
    }
    return FALSE;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function determines whether a device function
**	within the SMC37c669 controller is enabled.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which device function
**
**  RETURN VALUE:
**
**      Returns TRUE if the device function is enabled, otherwise, FALSE
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**  DESIGN:
**
**      To check whether a device is enabled we will only look at 
**	the port base address mapping.  According to the SMC37c669
**	specification, all of the port base address mappings are
**	disabled if the addr<9:8> (bits <7:6> of the register) are
**	zero.
**
**--
*/
static unsigned int __init SMC37c669_is_device_enabled ( unsigned int func )
{
    unsigned char base_addr = 0;
    unsigned int dev_ok = FALSE;
    unsigned int ret_val = FALSE;
/*
** Enter configuration mode
*/
    SMC37c669_config_mode( TRUE );
     
    switch ( func ) {
    	case SERIAL_0:
	    base_addr =
		SMC37c669_read_config( SMC37c669_SERIAL0_BASE_ADDRESS_INDEX );
	    dev_ok = TRUE;
	    break;
	case SERIAL_1:
	    base_addr =
		SMC37c669_read_config( SMC37c669_SERIAL1_BASE_ADDRESS_INDEX );
	    dev_ok = TRUE;
	    break;
	case PARALLEL_0:
	    base_addr =
		SMC37c669_read_config( SMC37c669_PARALLEL0_BASE_ADDRESS_INDEX );
	    dev_ok = TRUE;
	    break;
	case FLOPPY_0:
	    base_addr =
		SMC37c669_read_config( SMC37c669_FDC_BASE_ADDRESS_INDEX );
	    dev_ok = TRUE;
	    break;
	case IDE_0:
	    base_addr =
		SMC37c669_read_config( SMC37c669_IDE_BASE_ADDRESS_INDEX );
	    dev_ok = TRUE;
	    break;
    }
/*
** If we have a valid device, check base_addr<7:6> to see if the
** device is enabled (mapped).
*/
    if ( ( dev_ok ) && ( ( base_addr & 0xC0 ) != 0 ) ) {
/*
** The mapping is not disabled, so assume that the function is 
** enabled.
*/
    	ret_val = TRUE;
    }
/*
** Exit configuration mode 
*/
    SMC37c669_config_mode( FALSE );

    return ret_val;
}


#if 0
/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function retrieves the configuration information of a 
**	device function within the SMC37c699 Super I/O controller.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which device function
**       
**      port:
**          I/O port returned
**	 
**      irq:
**          IRQ returned
**	 
**      drq:
**          DMA channel returned
**
**  RETURN VALUE:
**
**      Returns TRUE if the device configuration was successfully
**	retrieved, otherwise, FALSE.
**
**  SIDE EFFECTS:
**
**      The data pointed to by the port, irq, and drq parameters
**	my be modified even if the configuration is not successfully
**	retrieved.
**
**  DESIGN:
**
**      The device configuration is fetched from the local shadow
**	copy.  Any unused parameters will be set to -1.  Any
**	parameter which is not desired can specify the NULL
**	pointer.
**
**--
*/
static unsigned int __init SMC37c669_get_device_config (
    unsigned int func,
    int *port,
    int *irq,
    int *drq )
{
    struct DEVICE_CONFIG *cp;
    unsigned int ret_val = FALSE;
/*
** Check for a valid device configuration
*/
    if ( ( cp = SMC37c669_get_config( func ) ) != NULL ) {
    	if ( drq != NULL ) {
	    *drq = cp->drq;
	    ret_val = TRUE;
	}
	if ( irq != NULL ) {
	    *irq = cp->irq;
	    ret_val = TRUE;
	}
	if ( port != NULL ) {
	    *port = cp->port1;
	    ret_val = TRUE;
	}
    }
    return ret_val;
}
#endif


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function displays the current state of the SMC37c699
**	Super I/O controller's device functions.
**
**  FORMAL PARAMETERS:
**
**      None
**
**  RETURN VALUE:
**
**      None
**
**  SIDE EFFECTS:
**
**      None
**
**--
*/
void __init SMC37c669_display_device_info ( void )
{
    if ( SMC37c669_is_device_enabled( SERIAL_0 ) ) {
    	printk( "  Serial 0:    Enabled [ Port 0x%x, IRQ %d ]\n",
		 local_config[ SERIAL_0 ].port1,
		 local_config[ SERIAL_0 ].irq
	);
    }
    else {
    	printk( "  Serial 0:    Disabled\n" );
    }

    if ( SMC37c669_is_device_enabled( SERIAL_1 ) ) {
    	printk( "  Serial 1:    Enabled [ Port 0x%x, IRQ %d ]\n",
		 local_config[ SERIAL_1 ].port1,
		 local_config[ SERIAL_1 ].irq
	);
    }
    else {
    	printk( "  Serial 1:    Disabled\n" );
    }

    if ( SMC37c669_is_device_enabled( PARALLEL_0 ) ) {
    	printk( "  Parallel:    Enabled [ Port 0x%x, IRQ %d/%d ]\n",
		 local_config[ PARALLEL_0 ].port1,
		 local_config[ PARALLEL_0 ].irq,
		 local_config[ PARALLEL_0 ].drq
	);
    }
    else {
    	printk( "  Parallel:    Disabled\n" );
    }

    if ( SMC37c669_is_device_enabled( FLOPPY_0 ) ) {
    	printk( "  Floppy Ctrl: Enabled [ Port 0x%x, IRQ %d/%d ]\n",
		 local_config[ FLOPPY_0 ].port1,
		 local_config[ FLOPPY_0 ].irq,
		 local_config[ FLOPPY_0 ].drq
	);
    }
    else {
    	printk( "  Floppy Ctrl: Disabled\n" );
    }

    if ( SMC37c669_is_device_enabled( IDE_0 ) ) {
    	printk( "  IDE 0:       Enabled [ Port 0x%x, IRQ %d ]\n",
		 local_config[ IDE_0 ].port1,
		 local_config[ IDE_0 ].irq
	);
    }
    else {
    	printk( "  IDE 0:       Disabled\n" );
    }
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function puts the SMC37c669 Super I/O controller into,
**	and takes it out of, configuration mode.
**
**  FORMAL PARAMETERS:
**
**      enable:
**          TRUE to enter configuration mode, FALSE to exit.
**
**  RETURN VALUE:
**
**      None
**
**  SIDE EFFECTS:
**
**      The SMC37c669 controller may be left in configuration mode.
**
**--
*/
static void __init SMC37c669_config_mode( 
    unsigned int enable )
{
    if ( enable ) {
/*
** To enter configuration mode, two writes in succession to the index
** port are required.  If a write to another address or port occurs
** between these two writes, the chip does not enter configuration
** mode.  Therefore, a spinlock is placed around the two writes to 
** guarantee that they complete uninterrupted.
*/
	spin_lock(&smc_lock);
    	wb( &SMC37c669->index_port, SMC37c669_CONFIG_ON_KEY );
    	wb( &SMC37c669->index_port, SMC37c669_CONFIG_ON_KEY );
	spin_unlock(&smc_lock);
    }
    else {
    	wb( &SMC37c669->index_port, SMC37c669_CONFIG_OFF_KEY );
    }
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function reads an SMC37c669 Super I/O controller
**	configuration register.  This function assumes that the
**	device is already in configuration mode.
**
**  FORMAL PARAMETERS:
**
**      index:
**          Index value of configuration register to read
**
**  RETURN VALUE:
**
**      Data read from configuration register
**
**  SIDE EFFECTS:
**
**      None
**
**--
*/
static unsigned char __init SMC37c669_read_config( 
    unsigned char index )
{
    unsigned char data;

    wb( &SMC37c669->index_port, index );
    data = rb( &SMC37c669->data_port );
    return data;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function writes an SMC37c669 Super I/O controller
**	configuration register.  This function assumes that the
**	device is already in configuration mode.
**
**  FORMAL PARAMETERS:
**
**      index:
**          Index of configuration register to write
**       
**      data:
**          Data to be written
**
**  RETURN VALUE:
**
**      None
**
**  SIDE EFFECTS:
**
**      None
**
**--
*/
static void __init SMC37c669_write_config( 
    unsigned char index, 
    unsigned char data )
{
    wb( &SMC37c669->index_port, index );
    wb( &SMC37c669->data_port, data );
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function initializes the local device
**	configuration storage.  This function assumes
**	that the device is already in configuration
**	mode.
**
**  FORMAL PARAMETERS:
**
**      None
**
**  RETURN VALUE:
**
**      None
**
**  SIDE EFFECTS:
**
**      Local storage for device configuration information
**	is initialized.
**
**--
*/
static void __init SMC37c669_init_local_config ( void )
{
    SMC37c669_SERIAL_BASE_ADDRESS_REGISTER uart_base;
    SMC37c669_SERIAL_IRQ_REGISTER uart_irqs;
    SMC37c669_PARALLEL_BASE_ADDRESS_REGISTER ppt_base;
    SMC37c669_PARALLEL_FDC_IRQ_REGISTER ppt_fdc_irqs;
    SMC37c669_PARALLEL_FDC_DRQ_REGISTER ppt_fdc_drqs;
    SMC37c669_FDC_BASE_ADDRESS_REGISTER fdc_base;
    SMC37c669_IDE_ADDRESS_REGISTER ide_base;
    SMC37c669_IDE_ADDRESS_REGISTER ide_alt;

/*
** Get serial port 1 base address 
*/
    uart_base.as_uchar = 
	SMC37c669_read_config( SMC37c669_SERIAL0_BASE_ADDRESS_INDEX );
/*
** Get IRQs for serial ports 1 & 2
*/
    uart_irqs.as_uchar = 
	SMC37c669_read_config( SMC37c669_SERIAL_IRQ_INDEX );
/*
** Store local configuration information for serial port 1
*/
    local_config[SERIAL_0].port1 = uart_base.by_field.addr9_3 << 3;
    local_config[SERIAL_0].irq = 
	SMC37c669_xlate_irq( 
	    SMC37c669_DEVICE_IRQ( uart_irqs.by_field.uart1_irq ) 
	);
/*
** Get serial port 2 base address
*/
    uart_base.as_uchar = 
	SMC37c669_read_config( SMC37c669_SERIAL1_BASE_ADDRESS_INDEX );
/*
** Store local configuration information for serial port 2
*/
    local_config[SERIAL_1].port1 = uart_base.by_field.addr9_3 << 3;
    local_config[SERIAL_1].irq = 
	SMC37c669_xlate_irq( 
	    SMC37c669_DEVICE_IRQ( uart_irqs.by_field.uart2_irq ) 
	);
/*
** Get parallel port base address
*/
    ppt_base.as_uchar =
	SMC37c669_read_config( SMC37c669_PARALLEL0_BASE_ADDRESS_INDEX );
/*
** Get IRQs for parallel port and floppy controller
*/
    ppt_fdc_irqs.as_uchar =
	SMC37c669_read_config( SMC37c669_PARALLEL_FDC_IRQ_INDEX );
/*
** Get DRQs for parallel port and floppy controller
*/
    ppt_fdc_drqs.as_uchar =
	SMC37c669_read_config( SMC37c669_PARALLEL_FDC_DRQ_INDEX );
/*
** Store local configuration information for parallel port
*/
    local_config[PARALLEL_0].port1 = ppt_base.by_field.addr9_2 << 2;
    local_config[PARALLEL_0].irq =
	SMC37c669_xlate_irq(
	    SMC37c669_DEVICE_IRQ( ppt_fdc_irqs.by_field.ppt_irq )
	);
    local_config[PARALLEL_0].drq =
	SMC37c669_xlate_drq(
	    SMC37c669_DEVICE_DRQ( ppt_fdc_drqs.by_field.ppt_drq )
	);
/*
** Get floppy controller base address
*/
    fdc_base.as_uchar = 
	SMC37c669_read_config( SMC37c669_FDC_BASE_ADDRESS_INDEX );
/*
** Store local configuration information for floppy controller
*/
    local_config[FLOPPY_0].port1 = fdc_base.by_field.addr9_4 << 4;
    local_config[FLOPPY_0].irq =
	SMC37c669_xlate_irq(
	    SMC37c669_DEVICE_IRQ( ppt_fdc_irqs.by_field.fdc_irq )
	);
    local_config[FLOPPY_0].drq =
	SMC37c669_xlate_drq(
	    SMC37c669_DEVICE_DRQ( ppt_fdc_drqs.by_field.fdc_drq )
	);
/*
** Get IDE controller base address
*/
    ide_base.as_uchar =
	SMC37c669_read_config( SMC37c669_IDE_BASE_ADDRESS_INDEX );
/*
** Get IDE alternate status base address
*/
    ide_alt.as_uchar =
	SMC37c669_read_config( SMC37c669_IDE_ALTERNATE_ADDRESS_INDEX );
/*
** Store local configuration information for IDE controller
*/
    local_config[IDE_0].port1 = ide_base.by_field.addr9_4 << 4;
    local_config[IDE_0].port2 = ide_alt.by_field.addr9_4 << 4;
    local_config[IDE_0].irq = 14;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function returns a pointer to the local shadow
**	configuration of the requested device function.
**
**  FORMAL PARAMETERS:
**
**      func:
**          Which device function
**
**  RETURN VALUE:
**
**      Returns a pointer to the DEVICE_CONFIG structure for the
**	requested function, otherwise, NULL.
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**--
*/
static struct DEVICE_CONFIG * __init SMC37c669_get_config( unsigned int func )
{
    struct DEVICE_CONFIG *cp = NULL;

    switch ( func ) {
    	case SERIAL_0:
	    cp = &local_config[ SERIAL_0 ];
	    break;
	case SERIAL_1:
	    cp = &local_config[ SERIAL_1 ];
	    break;
	case PARALLEL_0:
	    cp = &local_config[ PARALLEL_0 ];
	    break;
	case FLOPPY_0:
	    cp = &local_config[ FLOPPY_0 ];
	    break;
	case IDE_0:
	    cp = &local_config[ IDE_0 ];
	    break;
    }
    return cp;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function translates IRQs back and forth between ISA
**	IRQs and SMC37c669 device IRQs.
**
**  FORMAL PARAMETERS:
**
**      irq:
**          The IRQ to translate
**
**  RETURN VALUE:
**
**      Returns the translated IRQ, otherwise, returns -1.
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**--
*/
static int __init SMC37c669_xlate_irq ( int irq )
{
    int i, translated_irq = -1;

    if ( SMC37c669_IS_DEVICE_IRQ( irq ) ) {
/*
** We are translating a device IRQ to an ISA IRQ
*/
    	for ( i = 0; ( SMC37c669_irq_table[i].device_irq != -1 ) || ( SMC37c669_irq_table[i].isa_irq != -1 ); i++ ) {
	    if ( irq == SMC37c669_irq_table[i].device_irq ) {
	    	translated_irq = SMC37c669_irq_table[i].isa_irq;
		break;
	    }
	}
    }
    else {
/*
** We are translating an ISA IRQ to a device IRQ
*/
    	for ( i = 0; ( SMC37c669_irq_table[i].isa_irq != -1 ) || ( SMC37c669_irq_table[i].device_irq != -1 ); i++ ) {
	    if ( irq == SMC37c669_irq_table[i].isa_irq ) {
	    	translated_irq = SMC37c669_irq_table[i].device_irq;
		break;
	    }
	}
    }
    return translated_irq;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**
**      This function translates DMA channels back and forth between
**	ISA DMA channels and SMC37c669 device DMA channels.
**
**  FORMAL PARAMETERS:
**
**      drq:
**          The DMA channel to translate
**
**  RETURN VALUE:
**
**      Returns the translated DMA channel, otherwise, returns -1
**
**  SIDE EFFECTS:
**
**      {@description or none@}
**
**--
*/
static int __init SMC37c669_xlate_drq ( int drq )
{
    int i, translated_drq = -1;

    if ( SMC37c669_IS_DEVICE_DRQ( drq ) ) {
/*
** We are translating a device DMA channel to an ISA DMA channel
*/
    	for ( i = 0; ( SMC37c669_drq_table[i].device_drq != -1 ) || ( SMC37c669_drq_table[i].isa_drq != -1 ); i++ ) {
	    if ( drq == SMC37c669_drq_table[i].device_drq ) {
	    	translated_drq = SMC37c669_drq_table[i].isa_drq;
		break;
	    }
	}
    }
    else {
/*
** We are translating an ISA DMA channel to a device DMA channel
*/
    	for ( i = 0; ( SMC37c669_drq_table[i].isa_drq != -1 ) || ( SMC37c669_drq_table[i].device_drq != -1 ); i++ ) {
	    if ( drq == SMC37c669_drq_table[i].isa_drq ) {
	    	translated_drq = SMC37c669_drq_table[i].device_drq;
		break;
	    }
	}
    }
    return translated_drq;
}

#if 0
int __init smcc669_init ( void )
{
    struct INODE *ip;

    allocinode( smc_ddb.name, 1, &ip );
    ip->dva = &smc_ddb;
    ip->attr = ATTR$M_WRITE | ATTR$M_READ;
    ip->len[0] = 0x30;
    ip->misc = 0;
    INODE_UNLOCK( ip );

    return msg_success;
}

int __init smcc669_open( struct FILE *fp, char *info, char *next, char *mode )
{
    struct INODE *ip;
/*
** Allow multiple readers but only one writer.  ip->misc keeps track
** of the number of writers
*/
    ip = fp->ip;
    INODE_LOCK( ip );
    if ( fp->mode & ATTR$M_WRITE ) {
	if ( ip->misc ) {
	    INODE_UNLOCK( ip );
	    return msg_failure;	    /* too many writers */
	}
	ip->misc++;
    }
/*
** Treat the information field as a byte offset
*/
    *fp->offset = xtoi( info );
    INODE_UNLOCK( ip );

    return msg_success;
}

int __init smcc669_close( struct FILE *fp )
{
    struct INODE *ip;

    ip = fp->ip;
    if ( fp->mode & ATTR$M_WRITE ) {
	INODE_LOCK( ip );
	ip->misc--;
	INODE_UNLOCK( ip );
    }
    return msg_success;
}

int __init smcc669_read( struct FILE *fp, int size, int number, unsigned char *buf )
{
    int i;
    int length;
    int nbytes;
    struct INODE *ip;

/*
** Always access a byte at a time
*/
    ip = fp->ip;
    length = size * number;
    nbytes = 0;

    SMC37c669_config_mode( TRUE );
    for ( i = 0; i < length; i++ ) {
	if ( !inrange( *fp->offset, 0, ip->len[0] ) ) 
	    break;
	*buf++ = SMC37c669_read_config( *fp->offset );
	*fp->offset += 1;
	nbytes++;
    }
    SMC37c669_config_mode( FALSE );
    return nbytes;
}

int __init smcc669_write( struct FILE *fp, int size, int number, unsigned char *buf )
{
    int i;
    int length;
    int nbytes;
    struct INODE *ip;
/*
** Always access a byte at a time
*/
    ip = fp->ip;
    length = size * number;
    nbytes = 0;

    SMC37c669_config_mode( TRUE );
    for ( i = 0; i < length; i++ ) {
	if ( !inrange( *fp->offset, 0, ip->len[0] ) ) 
	    break;
	SMC37c669_write_config( *fp->offset, *buf );
	*fp->offset += 1;
	buf++;
	nbytes++;
    }
    SMC37c669_config_mode( FALSE );
    return nbytes;
}
#endif

void __init
SMC37c669_dump_registers(void)
{
  int i;
  for (i = 0; i <= 0x29; i++)
    printk("-- CR%02x : %02x\n", i, SMC37c669_read_config(i));
}
/*+
 * ============================================================================
 * = SMC_init - SMC37c669 Super I/O controller initialization                 =
 * ============================================================================
 *
 * OVERVIEW:
 *
 *      This routine configures and enables device functions on the
 *      SMC37c669 Super I/O controller.
 *
 * FORM OF CALL:
 *
 *      SMC_init( );
 *
 * RETURNS:
 *
 *      Nothing
 *
 * ARGUMENTS:
 *
 *      None
 *
 * SIDE EFFECTS:
 *
 *      None
 *
 */
void __init SMC669_Init ( int index )
{
    SMC37c669_CONFIG_REGS *SMC_base;
    unsigned long flags;

    local_irq_save(flags);
    if ( ( SMC_base = SMC37c669_detect( index ) ) != NULL ) {
#if SMC_DEBUG
	SMC37c669_config_mode( TRUE );
	SMC37c669_dump_registers( );
	SMC37c669_config_mode( FALSE );
        SMC37c669_display_device_info( );
#endif
        SMC37c669_disable_device( SERIAL_0 );
        SMC37c669_configure_device(
            SERIAL_0,
            COM1_BASE,
            COM1_IRQ,
            -1
        );
        SMC37c669_enable_device( SERIAL_0 );

        SMC37c669_disable_device( SERIAL_1 );
        SMC37c669_configure_device(
            SERIAL_1,
            COM2_BASE,
            COM2_IRQ,
            -1
        );
        SMC37c669_enable_device( SERIAL_1 );

        SMC37c669_disable_device( PARALLEL_0 );
        SMC37c669_configure_device(
            PARALLEL_0,
            PARP_BASE,
            PARP_IRQ,
            PARP_DRQ
        );
        SMC37c669_enable_device( PARALLEL_0 );

        SMC37c669_disable_device( FLOPPY_0 );
        SMC37c669_configure_device(
            FLOPPY_0,
            FDC_BASE,
            FDC_IRQ,
            FDC_DRQ
        );
        SMC37c669_enable_device( FLOPPY_0 );
          
	/* Wake up sometimes forgotten floppy, especially on DP264. */
	outb(0xc, 0x3f2);

        SMC37c669_disable_device( IDE_0 );

#if SMC_DEBUG
	SMC37c669_config_mode( TRUE );
	SMC37c669_dump_registers( );
	SMC37c669_config_mode( FALSE );
        SMC37c669_display_device_info( );
#endif
	local_irq_restore(flags);
        printk( "SMC37c669 Super I/O Controller found @ 0x%p\n",
		SMC_base );
    }
    else {
	local_irq_restore(flags);
#if SMC_DEBUG
        printk( "No SMC37c669 Super I/O Controller found\n" );
#endif
    }
}

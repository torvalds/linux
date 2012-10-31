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

/* Header file for AMCC  s 5933 */

#ifndef _AMCC_S5933_H_
#define _AMCC_S5933_H_

#include "../../comedidev.h"

/* written on base0 */
#define FIFO_ADVANCE_ON_BYTE_2	0x20000000

/* added for step 6 dma written on base2 */
#define AMWEN_ENABLE		0x02

#define A2P_FIFO_WRITE_ENABLE	0x01

/* for transfer count enable bit */
#define AGCSTS_TC_ENABLE	0x10000000

/*
 * ADDON RELATED ADDITIONS
 */
/* Constant */
#define APCI3120_ENABLE_TRANSFER_ADD_ON_LOW		0x00
#define APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH		0x1200
#define APCI3120_A2P_FIFO_MANAGEMENT			0x04000400L
#define APCI3120_AMWEN_ENABLE				0x02
#define APCI3120_A2P_FIFO_WRITE_ENABLE			0x01
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2			0x20000000L
#define APCI3120_ENABLE_WRITE_TC_INT			0x00004000L
#define APCI3120_CLEAR_WRITE_TC_INT			0x00040000L
#define APCI3120_DISABLE_AMWEN_AND_A2P_FIFO_WRITE	0x0
#define APCI3120_DISABLE_BUS_MASTER_ADD_ON		0x0
#define APCI3120_DISABLE_BUS_MASTER_PCI			0x0

/* ADD_ON ::: this needed since apci supports 16 bit interface to add on */
#define APCI3120_ADD_ON_AGCSTS_LOW	0x3C
#define APCI3120_ADD_ON_AGCSTS_HIGH	(APCI3120_ADD_ON_AGCSTS_LOW + 2)
#define APCI3120_ADD_ON_MWAR_LOW	0x24
#define APCI3120_ADD_ON_MWAR_HIGH	(APCI3120_ADD_ON_MWAR_LOW + 2)
#define APCI3120_ADD_ON_MWTC_LOW	0x058
#define APCI3120_ADD_ON_MWTC_HIGH	(APCI3120_ADD_ON_MWTC_LOW + 2)

/* AMCC */
#define APCI3120_AMCC_OP_MCSR		0x3C
#define APCI3120_AMCC_OP_REG_INTCSR	0x38

/*
 * AMCC Operation Register Offsets - PCI
 */
#define AMCC_OP_REG_OMB1		0x00
#define AMCC_OP_REG_OMB2		0x04
#define AMCC_OP_REG_OMB3		0x08
#define AMCC_OP_REG_OMB4		0x0c
#define AMCC_OP_REG_IMB1		0x10
#define AMCC_OP_REG_IMB2		0x14
#define AMCC_OP_REG_IMB3		0x18
#define AMCC_OP_REG_IMB4		0x1c
#define AMCC_OP_REG_FIFO		0x20
#define AMCC_OP_REG_MWAR		0x24
#define AMCC_OP_REG_MWTC		0x28
#define AMCC_OP_REG_MRAR		0x2c
#define AMCC_OP_REG_MRTC		0x30
#define AMCC_OP_REG_MBEF		0x34
#define AMCC_OP_REG_INTCSR		0x38
/* int source */
#define  AMCC_OP_REG_INTCSR_SRC		(AMCC_OP_REG_INTCSR + 2)
/* FIFO ctrl */
#define  AMCC_OP_REG_INTCSR_FEC		(AMCC_OP_REG_INTCSR + 3)
#define AMCC_OP_REG_MCSR		0x3c
/* Data in byte 2 */
#define  AMCC_OP_REG_MCSR_NVDATA	(AMCC_OP_REG_MCSR + 2)
/* Command in byte 3 */
#define  AMCC_OP_REG_MCSR_NVCMD		(AMCC_OP_REG_MCSR + 3)

#define AMCC_FIFO_DEPTH_DWORD	8
#define AMCC_FIFO_DEPTH_BYTES	(8 * sizeof(u32))

/*
 * AMCC Operation Registers Size - PCI
 */
#define AMCC_OP_REG_SIZE	 64	/* in bytes */

/*
 * AMCC Operation Register Offsets - Add-on
 */
#define AMCC_OP_REG_AIMB1	0x00
#define AMCC_OP_REG_AIMB2	0x04
#define AMCC_OP_REG_AIMB3	0x08
#define AMCC_OP_REG_AIMB4	0x0c
#define AMCC_OP_REG_AOMB1	0x10
#define AMCC_OP_REG_AOMB2	0x14
#define AMCC_OP_REG_AOMB3	0x18
#define AMCC_OP_REG_AOMB4	0x1c
#define AMCC_OP_REG_AFIFO	0x20
#define AMCC_OP_REG_AMWAR	0x24
#define AMCC_OP_REG_APTA	0x28
#define AMCC_OP_REG_APTD	0x2c
#define AMCC_OP_REG_AMRAR	0x30
#define AMCC_OP_REG_AMBEF	0x34
#define AMCC_OP_REG_AINT	0x38
#define AMCC_OP_REG_AGCSTS	0x3c
#define AMCC_OP_REG_AMWTC	0x58
#define AMCC_OP_REG_AMRTC	0x5c

/*
 * AMCC - Add-on General Control/Status Register
 */
#define AGCSTS_CONTROL_MASK	0xfffff000
#define  AGCSTS_NV_ACC_MASK	0xe0000000
#define  AGCSTS_RESET_MASK	0x0e000000
#define  AGCSTS_NV_DA_MASK	0x00ff0000
#define  AGCSTS_BIST_MASK	0x0000f000
#define AGCSTS_STATUS_MASK	0x000000ff
#define  AGCSTS_TCZERO_MASK	0x000000c0
#define  AGCSTS_FIFO_ST_MASK	0x0000003f

#define AGCSTS_RESET_MBFLAGS	0x08000000
#define AGCSTS_RESET_P2A_FIFO	0x04000000
#define AGCSTS_RESET_A2P_FIFO	0x02000000
#define AGCSTS_RESET_FIFOS	(AGCSTS_RESET_A2P_FIFO | AGCSTS_RESET_P2A_FIFO)

#define AGCSTS_A2P_TCOUNT	0x00000080
#define AGCSTS_P2A_TCOUNT	0x00000040

#define AGCSTS_FS_P2A_EMPTY	0x00000020
#define AGCSTS_FS_P2A_HALF	0x00000010
#define AGCSTS_FS_P2A_FULL	0x00000008

#define AGCSTS_FS_A2P_EMPTY	0x00000004
#define AGCSTS_FS_A2P_HALF	0x00000002
#define AGCSTS_FS_A2P_FULL	0x00000001

/*
 * AMCC - Add-on Interrupt Control/Status Register
 */
#define AINT_INT_MASK		0x00ff0000
#define AINT_SEL_MASK		0x0000ffff
#define  AINT_IS_ENSEL_MASK	0x00001f1f

#define AINT_INT_ASSERTED	0x00800000
#define AINT_BM_ERROR		0x00200000
#define AINT_BIST_INT		0x00100000

#define AINT_RT_COMPLETE	0x00080000
#define AINT_WT_COMPLETE	0x00040000

#define AINT_OUT_MB_INT		0x00020000
#define AINT_IN_MB_INT		0x00010000

#define AINT_READ_COMPL		0x00008000
#define AINT_WRITE_COMPL	0x00004000

#define AINT_OMB_ENABLE 	0x00001000
#define AINT_OMB_SELECT 	0x00000c00
#define AINT_OMB_BYTE		0x00000300

#define AINT_IMB_ENABLE 	0x00000010
#define AINT_IMB_SELECT 	0x0000000c
#define AINT_IMB_BYTE		0x00000003

/* Enable Bus Mastering */
#define EN_A2P_TRANSFERS	0x00000400
/* FIFO Flag Reset */
#define RESET_A2P_FLAGS		0x04000000L
/* FIFO Relative Priority */
#define A2P_HI_PRIORITY		0x00000100L
/* Identify Interrupt Sources */
#define ANY_S593X_INT		0x00800000L
#define READ_TC_INT		0x00080000L
#define WRITE_TC_INT		0x00040000L
#define IN_MB_INT		0x00020000L
#define MASTER_ABORT_INT	0x00100000L
#define TARGET_ABORT_INT	0x00200000L
#define BUS_MASTER_INT		0x00200000L

#endif

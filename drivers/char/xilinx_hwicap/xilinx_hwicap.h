/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     (c) Copyright 2003-2007 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#ifndef XILINX_HWICAP_H_	/* prevent circular inclusions */
#define XILINX_HWICAP_H_	/* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

#include <linux/io.h>

struct hwicap_drvdata {
	u32 write_buffer_in_use;  /* Always in [0,3] */
	u8 write_buffer[4];
	u32 read_buffer_in_use;	  /* Always in [0,3] */
	u8 read_buffer[4];
	resource_size_t mem_start;/* phys. address of the control registers */
	resource_size_t mem_end;  /* phys. address of the control registers */
	resource_size_t mem_size;
	void __iomem *base_address;/* virt. address of the control registers */

	struct device *dev;
	struct cdev cdev;	/* Char device structure */
	dev_t devt;

	const struct hwicap_driver_config *config;
	const struct config_registers *config_regs;
	void *private_data;
	bool is_open;
	struct mutex sem;
};

struct hwicap_driver_config {
	/* Read configuration data given by size into the data buffer.
	   Return 0 if successful. */
	int (*get_configuration)(struct hwicap_drvdata *drvdata, u32 *data,
			u32 size);
	/* Write configuration data given by size from the data buffer.
	   Return 0 if successful. */
	int (*set_configuration)(struct hwicap_drvdata *drvdata, u32 *data,
			u32 size);
	/* Get the status register, bit pattern given by:
	 * D8 - 0 = configuration error
	 * D7 - 1 = alignment found
	 * D6 - 1 = readback in progress
	 * D5 - 0 = abort in progress
	 * D4 - Always 1
	 * D3 - Always 1
	 * D2 - Always 1
	 * D1 - Always 1
	 * D0 - 1 = operation completed
	 */
	u32 (*get_status)(struct hwicap_drvdata *drvdata);
	/* Reset the hw */
	void (*reset)(struct hwicap_drvdata *drvdata);
};

/* Number of times to poll the done register. This has to be large
 * enough to allow an entire configuration to complete. If an entire
 * page (4kb) is configured at once, that could take up to 4k cycles
 * with a byte-wide icap interface. In most cases, this driver is
 * used with a much smaller fifo, but this should be sufficient in the
 * worst case.
 */
#define XHI_MAX_RETRIES     5000

/************ Constant Definitions *************/

#define XHI_PAD_FRAMES              0x1

/* Mask for calculating configuration packet headers */
#define XHI_WORD_COUNT_MASK_TYPE_1  0x7FFUL
#define XHI_WORD_COUNT_MASK_TYPE_2  0x1FFFFFUL
#define XHI_TYPE_MASK               0x7
#define XHI_REGISTER_MASK           0xF
#define XHI_OP_MASK                 0x3

#define XHI_TYPE_SHIFT              29
#define XHI_REGISTER_SHIFT          13
#define XHI_OP_SHIFT                27

#define XHI_TYPE_1                  1
#define XHI_TYPE_2                  2
#define XHI_OP_WRITE                2
#define XHI_OP_READ                 1

/* Address Block Types */
#define XHI_FAR_CLB_BLOCK           0
#define XHI_FAR_BRAM_BLOCK          1
#define XHI_FAR_BRAM_INT_BLOCK      2

struct config_registers {
	u32 CRC;
	u32 FAR;
	u32 FDRI;
	u32 FDRO;
	u32 CMD;
	u32 CTL;
	u32 MASK;
	u32 STAT;
	u32 LOUT;
	u32 COR;
	u32 MFWR;
	u32 FLR;
	u32 KEY;
	u32 CBC;
	u32 IDCODE;
	u32 AXSS;
	u32 C0R_1;
	u32 CSOB;
	u32 WBSTAR;
	u32 TIMER;
	u32 BOOTSTS;
	u32 CTL_1;
};

/* Configuration Commands */
#define XHI_CMD_NULL                0
#define XHI_CMD_WCFG                1
#define XHI_CMD_MFW                 2
#define XHI_CMD_DGHIGH              3
#define XHI_CMD_RCFG                4
#define XHI_CMD_START               5
#define XHI_CMD_RCAP                6
#define XHI_CMD_RCRC                7
#define XHI_CMD_AGHIGH              8
#define XHI_CMD_SWITCH              9
#define XHI_CMD_GRESTORE            10
#define XHI_CMD_SHUTDOWN            11
#define XHI_CMD_GCAPTURE            12
#define XHI_CMD_DESYNCH             13
#define XHI_CMD_IPROG               15 /* Only in Virtex5 */
#define XHI_CMD_CRCC                16 /* Only in Virtex5 */
#define XHI_CMD_LTIMER              17 /* Only in Virtex5 */

/* Packet constants */
#define XHI_SYNC_PACKET             0xAA995566UL
#define XHI_DUMMY_PACKET            0xFFFFFFFFUL
#define XHI_NOOP_PACKET             (XHI_TYPE_1 << XHI_TYPE_SHIFT)
#define XHI_TYPE_2_READ ((XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
			(XHI_OP_READ << XHI_OP_SHIFT))

#define XHI_TYPE_2_WRITE ((XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
			(XHI_OP_WRITE << XHI_OP_SHIFT))

#define XHI_TYPE2_CNT_MASK          0x07FFFFFF

#define XHI_TYPE_1_PACKET_MAX_WORDS 2047UL
#define XHI_TYPE_1_HEADER_BYTES     4
#define XHI_TYPE_2_HEADER_BYTES     8

/* Constant to use for CRC check when CRC has been disabled */
#define XHI_DISABLED_AUTO_CRC       0x0000DEFCUL

/* Meanings of the bits returned by get_status */
#define XHI_SR_CFGERR_N_MASK 0x00000100 /* Config Error Mask */
#define XHI_SR_DALIGN_MASK 0x00000080 /* Data Alignment Mask */
#define XHI_SR_RIP_MASK 0x00000040 /* Read back Mask */
#define XHI_SR_IN_ABORT_N_MASK 0x00000020 /* Select Map Abort Mask */
#define XHI_SR_DONE_MASK 0x00000001 /* Done bit Mask  */

/**
 * hwicap_type_1_read - Generates a Type 1 read packet header.
 * @reg: is the address of the register to be read back.
 *
 * Return:
 * Generates a Type 1 read packet header, which is used to indirectly
 * read registers in the configuration logic.  This packet must then
 * be sent through the icap device, and a return packet received with
 * the information.
 */
static inline u32 hwicap_type_1_read(u32 reg)
{
	return (XHI_TYPE_1 << XHI_TYPE_SHIFT) |
		(reg << XHI_REGISTER_SHIFT) |
		(XHI_OP_READ << XHI_OP_SHIFT);
}

/**
 * hwicap_type_1_write - Generates a Type 1 write packet header
 * @reg: is the address of the register to be read back.
 *
 * Return: Type 1 write packet header
 */
static inline u32 hwicap_type_1_write(u32 reg)
{
	return (XHI_TYPE_1 << XHI_TYPE_SHIFT) |
		(reg << XHI_REGISTER_SHIFT) |
		(XHI_OP_WRITE << XHI_OP_SHIFT);
}

#endif

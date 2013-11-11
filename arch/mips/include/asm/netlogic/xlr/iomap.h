/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_NLM_IOMAP_H
#define _ASM_NLM_IOMAP_H

#define DEFAULT_NETLOGIC_IO_BASE	   CKSEG1ADDR(0x1ef00000)
#define NETLOGIC_IO_DDR2_CHN0_OFFSET	   0x01000
#define NETLOGIC_IO_DDR2_CHN1_OFFSET	   0x02000
#define NETLOGIC_IO_DDR2_CHN2_OFFSET	   0x03000
#define NETLOGIC_IO_DDR2_CHN3_OFFSET	   0x04000
#define NETLOGIC_IO_PIC_OFFSET		   0x08000
#define NETLOGIC_IO_UART_0_OFFSET	   0x14000
#define NETLOGIC_IO_UART_1_OFFSET	   0x15100

#define NETLOGIC_IO_SIZE		   0x1000

#define NETLOGIC_IO_BRIDGE_OFFSET	   0x00000

#define NETLOGIC_IO_RLD2_CHN0_OFFSET	   0x05000
#define NETLOGIC_IO_RLD2_CHN1_OFFSET	   0x06000

#define NETLOGIC_IO_SRAM_OFFSET		   0x07000

#define NETLOGIC_IO_PCIX_OFFSET		   0x09000
#define NETLOGIC_IO_HT_OFFSET		   0x0A000

#define NETLOGIC_IO_SECURITY_OFFSET	   0x0B000

#define NETLOGIC_IO_GMAC_0_OFFSET	   0x0C000
#define NETLOGIC_IO_GMAC_1_OFFSET	   0x0D000
#define NETLOGIC_IO_GMAC_2_OFFSET	   0x0E000
#define NETLOGIC_IO_GMAC_3_OFFSET	   0x0F000

/* XLS devices */
#define NETLOGIC_IO_GMAC_4_OFFSET	   0x20000
#define NETLOGIC_IO_GMAC_5_OFFSET	   0x21000
#define NETLOGIC_IO_GMAC_6_OFFSET	   0x22000
#define NETLOGIC_IO_GMAC_7_OFFSET	   0x23000

#define NETLOGIC_IO_PCIE_0_OFFSET	   0x1E000
#define NETLOGIC_IO_PCIE_1_OFFSET	   0x1F000
#define NETLOGIC_IO_SRIO_0_OFFSET	   0x1E000
#define NETLOGIC_IO_SRIO_1_OFFSET	   0x1F000

#define NETLOGIC_IO_USB_0_OFFSET	   0x24000
#define NETLOGIC_IO_USB_1_OFFSET	   0x25000

#define NETLOGIC_IO_COMP_OFFSET		   0x1D000
/* end XLS devices */

/* XLR devices */
#define NETLOGIC_IO_SPI4_0_OFFSET	   0x10000
#define NETLOGIC_IO_XGMAC_0_OFFSET	   0x11000
#define NETLOGIC_IO_SPI4_1_OFFSET	   0x12000
#define NETLOGIC_IO_XGMAC_1_OFFSET	   0x13000
/* end XLR devices */

#define NETLOGIC_IO_I2C_0_OFFSET	   0x16000
#define NETLOGIC_IO_I2C_1_OFFSET	   0x17000

#define NETLOGIC_IO_GPIO_OFFSET		   0x18000
#define NETLOGIC_IO_FLASH_OFFSET	   0x19000
#define NETLOGIC_IO_TB_OFFSET		   0x1C000

#define NETLOGIC_CPLD_OFFSET		   KSEG1ADDR(0x1d840000)

/*
 * Base Address (Virtual) of the PCI Config address space
 * For now, choose 256M phys in kseg1 = 0xA0000000 + (1<<28)
 * Config space spans 256 (num of buses) * 256 (num functions) * 256 bytes
 * ie 1<<24 = 16M
 */
#define DEFAULT_PCI_CONFIG_BASE		0x18000000
#define DEFAULT_HT_TYPE0_CFG_BASE	0x16000000
#define DEFAULT_HT_TYPE1_CFG_BASE	0x17000000

#endif

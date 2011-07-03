/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SATA_H_
#define _SATA_H_

#include <linux/types.h>

/**
 * This file defines all of the SATA releated constants, enumerations, and
 *    types. Please note that this file does not necessarily contain an
 *    exhaustive list of all contants and commands.
 *
 *
 */

/**
 *
 *
 * SATA FIS Types These constants depict the various SATA FIS types devined in
 * the serial ATA specification.
 */
#define SATA_FIS_TYPE_REGH2D          0x27
#define SATA_FIS_TYPE_REGD2H          0x34
#define SATA_FIS_TYPE_SETDEVBITS      0xA1
#define SATA_FIS_TYPE_DMA_ACTIVATE    0x39
#define SATA_FIS_TYPE_DMA_SETUP       0x41
#define SATA_FIS_TYPE_BIST_ACTIVATE   0x58
#define SATA_FIS_TYPE_PIO_SETUP       0x5F
#define SATA_FIS_TYPE_DATA            0x46

#define SATA_REGISTER_FIS_SIZE 0x20

/**
 * struct sata_fis_header - This is the common definition for a SATA FIS Header
 *    word.  A different header word is defined for any FIS type that does not
 *    use the standard header.
 *
 *
 */
struct sata_fis_header {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved:1;
	u32 direction_flag:1;       /* direction */
	u32 interrupt_flag:1;
	u32 command_flag:1;       /* command, auto_activate, or notification */
	u32 status:8;
	u32 error:8;
};


/**
 * struct sata_fis_reg_h2d - This is the definition for a SATA Host to Device
 *    Register FIS.
 *
 *
 */
struct sata_fis_reg_h2d {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved0:3;
	u32 command_flag:1;
	u32 command:8;
	u32 features:8;
	u32 lba_low:8; /* word 1 */
	u32 lba_mid:8;
	u32 lba_high:8;
	u32 device:8;
	u32 lba_low_exp:8; /* word 2 */
	u32 lba_mid_exp:8;
	u32 lba_high_exp:8;
	u32 features_exp:8;
	u32 sector_count:8; /* word 3 */
	u32 sector_count_exp:8;
	u32 reserved1:8;
	u32 control:8;
	u32 reserved2;          /* word 4 */
};

/**
 * struct sata_fis_reg_d2h - SATA Device To Host FIS
 *
 *
 */
struct sata_fis_reg_d2h {
	u32 fis_type:8;   /* word 0 */
	u32 pm_port:4;
	u32 reserved0:2;
	u32 irq:1;
	u32 reserved1:1;
	u32 status:8;
	u32 error:8;
	u8 lba_low;          /* word 1 */
	u8 lba_mid;
	u8 lba_high;
	u8 device;
	u8 lba_low_exp;      /* word 2 */
	u8 lba_mid_exp;
	u8 lba_high_exp;
	u8 reserved;
	u8 sector_count;     /* word 3 */
	u8 sector_count_exp;
	u16 reserved2;
	u32 reserved3;
};

/**
 *
 *
 * Status field bit definitions
 */
#define SATA_FIS_STATUS_DEVBITS_MASK  (0x77)

/**
 * struct sata_fis_set_dev_bits - SATA Set Device Bits FIS
 *
 *
 */
struct sata_fis_set_dev_bits {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved0:2;
	u32 irq:1;
	u32 notification:1;
	u32 status_low:4;
	u32 status_high:4;
	u32 error:8;
	u32 s_active;      /* word 1 */
};

/**
 * struct sata_fis_dma_activate - SATA DMA Activate FIS
 *
 *
 */
struct sata_fis_dma_activate {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved0:24;
};

/**
 *
 *
 * The lower 5 bits in the DMA Buffer ID Low field of the DMA Setup are used to
 * communicate the command tag.
 */
#define SATA_DMA_SETUP_TAG_ENABLE      0x1F

#define SATA_DMA_SETUP_AUTO_ACT_ENABLE 0x80

/**
 * struct sata_fis_dma_setup - SATA DMA Setup FIS
 *
 *
 */
struct sata_fis_dma_setup {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved_00:1;
	u32 direction:1;
	u32 irq:1;
	u32 auto_activate:1;
	u32 reserved_01:16;
	u32 dma_buffer_id_low;          /* word 1 */
	u32 dma_buffer_id_high;         /* word 2 */
	u32 reserved0;                  /* word 3 */
	u32 dma_buffer_offset;          /* word 4 */
	u32 dma_transfer_count;         /* word 5 */
	u32 reserved1;                  /* word 6 */
};

/**
 * struct sata_fis_bist_activate - SATA BIST Activate FIS
 *
 *
 */
struct sata_fis_bist_activate {
	u32 fis_type:8; /* word 0 */
	u32 reserved0:8;
	u32 pattern_definition:8;
	u32 reserved1:8;
	u32 data1;                      /* word 1 */
	u32 data2;                      /* word 1 */
};

/*
 *  SATA PIO Setup FIS
 */
struct sata_fis_pio_setup {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved_00:1;
	u32 direction:1;
	u32 irq:1;
	u32 reserved_01:1;
	u32 status:8;
	u32 error:8;
	u32 lba_low:8; /* word 1 */
	u32 lba_mid:8;
	u32 lba_high:8;
	u32 device:8;
	u32 lba_low_exp:8; /* word 2 */
	u32 lba_mid_exp:8;
	u32 lba_high_exp:8;
	u32 reserved:8;
	u32 sector_count:8; /* word 3 */
	u32 sector_count_exp:8;
	u32 reserved1:8;
	u32 ending_status:8;
	u32 transfter_count:16; /* word 4 */
	u32 reserved3:16;
};

/**
 * struct sata_fis_data - SATA Data FIS
 *
 *
 */
struct sata_fis_data {
	u32 fis_type:8; /* word 0 */
	u32 pm_port:4;
	u32 reserved0:24;
	u8 data[4];        /* word 1 */
};

#endif /* _SATA_H_ */

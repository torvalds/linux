/*
 * eisa_eeprom.h - provide support for EISA adapters in PA-RISC machines
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Copyright (c) 2001, 2002 Daniel Engstrom <5116@telia.com>
 *
 */

#ifndef ASM_EISA_EEPROM_H
#define ASM_EISA_EEPROM_H

extern void __iomem *eisa_eeprom_addr;

#define HPEE_MAX_LENGTH       0x2000	/* maximum eeprom length */

#define HPEE_SLOT_INFO(slot) (20+(48*slot))

struct eeprom_header 
{
   
	u_int32_t num_writes;       /* number of writes */
 	u_int8_t  flags;            /* flags, usage? */
	u_int8_t  ver_maj;
	u_int8_t  ver_min;
	u_int8_t  num_slots;        /* number of EISA slots in system */
	u_int16_t csum;             /* checksum, I don't know how to calculate this */
	u_int8_t  pad[10];
} __attribute__ ((packed));


struct eeprom_eisa_slot_info
{
	u_int32_t eisa_slot_id;
	u_int32_t config_data_offset;
	u_int32_t num_writes;
	u_int16_t csum;
	u_int16_t num_functions;
	u_int16_t config_data_length;
	
	/* bits 0..3 are the duplicate slot id */ 
#define HPEE_SLOT_INFO_EMBEDDED  0x10
#define HPEE_SLOT_INFO_VIRTUAL   0x20
#define HPEE_SLOT_INFO_NO_READID 0x40
#define HPEE_SLOT_INFO_DUPLICATE 0x80
	u_int8_t slot_info;
	
#define HPEE_SLOT_FEATURES_ENABLE         0x01
#define HPEE_SLOT_FEATURES_IOCHK          0x02
#define HPEE_SLOT_FEATURES_CFG_INCOMPLETE 0x80
	u_int8_t slot_features;
	
	u_int8_t  ver_min;
	u_int8_t  ver_maj;
	
#define HPEE_FUNCTION_INFO_HAVE_TYPE      0x01
#define HPEE_FUNCTION_INFO_HAVE_MEMORY    0x02
#define HPEE_FUNCTION_INFO_HAVE_IRQ       0x04
#define HPEE_FUNCTION_INFO_HAVE_DMA       0x08
#define HPEE_FUNCTION_INFO_HAVE_PORT      0x10
#define HPEE_FUNCTION_INFO_HAVE_PORT_INIT 0x20
/* I think there are two slighty different 
 * versions of the function_info field 
 * one int the fixed header and one optional 
 * in the parsed slot data area */
#define HPEE_FUNCTION_INFO_HAVE_FUNCTION  0x01
#define HPEE_FUNCTION_INFO_F_DISABLED     0x80
#define HPEE_FUNCTION_INFO_CFG_FREE_FORM  0x40
	u_int8_t  function_info;

#define HPEE_FLAG_BOARD_IS_ISA		  0x01 /* flag and minor version for isa board */
	u_int8_t  flags;
	u_int8_t  pad[24];
} __attribute__ ((packed));


#define HPEE_MEMORY_MAX_ENT   9
/* memory descriptor: byte 0 */
#define HPEE_MEMORY_WRITABLE  0x01
#define HPEE_MEMORY_CACHABLE  0x02
#define HPEE_MEMORY_TYPE_MASK 0x18
#define HPEE_MEMORY_TYPE_SYS  0x00
#define HPEE_MEMORY_TYPE_EXP  0x08
#define HPEE_MEMORY_TYPE_VIR  0x10
#define HPEE_MEMORY_TYPE_OTH  0x18
#define HPEE_MEMORY_SHARED    0x20
#define HPEE_MEMORY_MORE      0x80

/* memory descriptor: byte 1 */
#define HPEE_MEMORY_WIDTH_MASK 0x03
#define HPEE_MEMORY_WIDTH_BYTE 0x00
#define HPEE_MEMORY_WIDTH_WORD 0x01
#define HPEE_MEMORY_WIDTH_DWORD 0x02
#define HPEE_MEMORY_DECODE_MASK 0x0c
#define HPEE_MEMORY_DECODE_20BITS 0x00
#define HPEE_MEMORY_DECODE_24BITS 0x04
#define HPEE_MEMORY_DECODE_32BITS 0x08
/* byte 2 and 3 are a 16bit LE value
 * containging the memory size in kilobytes */
/* byte 4,5,6 are a 24bit LE value
 * containing the memory base address */


#define HPEE_IRQ_MAX_ENT      7
/* Interrupt entry: byte 0 */
#define HPEE_IRQ_CHANNEL_MASK 0xf
#define HPEE_IRQ_TRIG_LEVEL   0x20
#define HPEE_IRQ_MORE         0x80
/* byte 1 seems to be unused */

#define HPEE_DMA_MAX_ENT     4

/* dma entry: byte 0 */
#define HPEE_DMA_CHANNEL_MASK 7
#define HPEE_DMA_SIZE_MASK	0xc
#define HPEE_DMA_SIZE_BYTE	0x0
#define HPEE_DMA_SIZE_WORD	0x4
#define HPEE_DMA_SIZE_DWORD	0x8
#define HPEE_DMA_SHARED      0x40
#define HPEE_DMA_MORE        0x80

/* dma entry: byte 1 */
#define HPEE_DMA_TIMING_MASK 0x30
#define HPEE_DMA_TIMING_ISA	0x0
#define HPEE_DMA_TIMING_TYPEA 0x10
#define HPEE_DMA_TIMING_TYPEB 0x20
#define HPEE_DMA_TIMING_TYPEC 0x30

#define HPEE_PORT_MAX_ENT 20
/* port entry byte 0 */
#define HPEE_PORT_SIZE_MASK 0x1f
#define HPEE_PORT_SHARED    0x40
#define HPEE_PORT_MORE      0x80
/* byte 1 and 2 is a 16bit LE value
 * conating the start port number */

#define HPEE_PORT_INIT_MAX_LEN     60 /* in bytes here */
/* port init entry byte 0 */
#define HPEE_PORT_INIT_WIDTH_MASK  0x3
#define HPEE_PORT_INIT_WIDTH_BYTE  0x0
#define HPEE_PORT_INIT_WIDTH_WORD  0x1
#define HPEE_PORT_INIT_WIDTH_DWORD 0x2
#define HPEE_PORT_INIT_MASK        0x4
#define HPEE_PORT_INIT_MORE        0x80

#define HPEE_SELECTION_MAX_ENT 26

#define HPEE_TYPE_MAX_LEN    80

#endif

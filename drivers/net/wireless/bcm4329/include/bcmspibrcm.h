/*
 * SD-SPI Protocol Conversion - BCMSDH->gSPI Translation Layer
 *
 * Copyright (C) 2010, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: bcmspibrcm.h,v 1.4.4.1.4.3.6.1 2008/09/27 17:03:25 Exp $
 */

/* global msglevel for debug messages - bitvals come from sdiovar.h */

#define sd_err(x)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)

#define sd_log(x)

#define SDIOH_ASSERT(exp) \
	do { if (!(exp)) \
		printf("!!!ASSERT fail: file %s lines %d", __FILE__, __LINE__); \
	} while (0)

#define BLOCK_SIZE_F1		64
#define BLOCK_SIZE_F2 		2048
#define BLOCK_SIZE_F3 		2048

/* internal return code */
#define SUCCESS	0
#undef ERROR
#define ERROR	1
#define ERROR_UF	2
#define ERROR_OF	3

/* private bus modes */
#define SDIOH_MODE_SPI		0

#define USE_BLOCKMODE		0x2	/* Block mode can be single block or multi */
#define USE_MULTIBLOCK		0x4

struct sdioh_info {
	uint 		cfg_bar;		/* pci cfg address for bar */
	uint32		caps;			/* cached value of capabilities reg */
	void		*bar0;			/* BAR0 for PCI Device */
	osl_t 		*osh;			/* osh handler */
	void		*controller;	/* Pointer to SPI Controller's private data struct */

	uint		lockcount; 		/* nest count of spi_lock() calls */
	bool		client_intr_enabled;	/* interrupt connnected flag */
	bool		intr_handler_valid;	/* client driver interrupt handler valid */
	sdioh_cb_fn_t	intr_handler;		/* registered interrupt handler */
	void		*intr_handler_arg;	/* argument to call interrupt handler */
	bool		initialized;		/* card initialized */
	uint32		target_dev;		/* Target device ID */
	uint32		intmask;		/* Current active interrupts */
	void		*sdos_info;		/* Pointer to per-OS private data */

	uint32		controller_type;	/* Host controller type */
	uint8		version;		/* Host Controller Spec Compliance Version */
	uint 		irq;			/* Client irq */
	uint32 		intrcount;		/* Client interrupts */
	uint32 		local_intrcount;	/* Controller interrupts */
	bool 		host_init_done;		/* Controller initted */
	bool 		card_init_done;		/* Client SDIO interface initted */
	bool 		polled_mode;		/* polling for command completion */

	bool		sd_use_dma;		/* DMA on CMD53 */
	bool 		sd_blockmode;		/* sd_blockmode == FALSE => 64 Byte Cmd 53s. */
						/*  Must be on for sd_multiblock to be effective */
	bool 		use_client_ints;	/* If this is false, make sure to restore */
						/*  polling hack in wl_linux.c:wl_timer() */
	int 		adapter_slot;		/* Maybe dealing with multiple slots/controllers */
	int 		sd_mode;		/* SD1/SD4/SPI */
	int 		client_block_size[SPI_MAX_IOFUNCS];		/* Blocksize */
	uint32 		data_xfer_count;	/* Current transfer */
	uint16 		card_rca;		/* Current Address */
	uint8 		num_funcs;		/* Supported funcs on client */
	uint32 		card_dstatus;		/* 32bit device status */
	uint32 		com_cis_ptr;
	uint32 		func_cis_ptr[SPI_MAX_IOFUNCS];
	void		*dma_buf;
	ulong		dma_phys;
	int 		r_cnt;			/* rx count */
	int 		t_cnt;			/* tx_count */
	uint32		wordlen;			/* host processor 16/32bits */
	uint32		prev_fun;
	uint32		chip;
	uint32		chiprev;
	bool		resp_delay_all;
	bool		dwordmode;

	struct spierrstats_t spierrstats;
};

/************************************************************
 * Internal interfaces: per-port references into bcmspibrcm.c
 */

/* Global message bits */
extern uint sd_msglevel;

/**************************************************************
 * Internal interfaces: bcmspibrcm.c references to per-port code
 */

/* Interrupt (de)registration routines */
extern int spi_register_irq(sdioh_info_t *sd, uint irq);
extern void spi_free_irq(uint irq, sdioh_info_t *sd);

/* OS-specific interrupt wrappers (atomic interrupt enable/disable) */
extern void spi_lock(sdioh_info_t *sd);
extern void spi_unlock(sdioh_info_t *sd);

/* Allocate/init/free per-OS private data */
extern int spi_osinit(sdioh_info_t *sd);
extern void spi_osfree(sdioh_info_t *sd);

#define SPI_RW_FLAG_M			BITFIELD_MASK(1)	/* Bit [31] - R/W Command Bit */
#define SPI_RW_FLAG_S			31
#define SPI_ACCESS_M			BITFIELD_MASK(1)	/* Bit [30] - Fixed/Incr Access */
#define SPI_ACCESS_S			30
#define SPI_FUNCTION_M			BITFIELD_MASK(2)	/* Bit [29:28] - Function Number */
#define SPI_FUNCTION_S			28
#define SPI_REG_ADDR_M			BITFIELD_MASK(17)	/* Bit [27:11] - Address */
#define SPI_REG_ADDR_S			11
#define SPI_LEN_M			BITFIELD_MASK(11)	/* Bit [10:0] - Packet length */
#define SPI_LEN_S			0

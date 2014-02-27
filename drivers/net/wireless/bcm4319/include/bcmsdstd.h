/*
 *  'Standard' SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmsdstd.h,v 13.16.18.1.16.3 2009/12/10 01:09:23 Exp $
 */

/* global msglevel for debug messages - bitvals come from sdiovar.h */

#define sd_err(x)	do { if (sd_msglevel & SDH_ERROR_VAL) printf x; } while (0)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)
#define sd_dma(x)

#define sd_sync_dma(sd, read, nbytes)
#define sd_init_dma(sd)
#define sd_ack_intr(sd)
#define sd_wakeup(sd);
/* Allocate/init/free per-OS private data */
extern int sdstd_osinit(sdioh_info_t *sd);
extern void sdstd_osfree(sdioh_info_t *sd);

#define sd_log(x)

#define SDIOH_ASSERT(exp) \
	do { if (!(exp)) \
		printf("!!!ASSERT fail: file %s lines %d", __FILE__, __LINE__); \
	} while (0)

#define BLOCK_SIZE_4318 64
#define BLOCK_SIZE_4328 512

/* internal return code */
#define SUCCESS	0
#define ERROR	1

/* private bus modes */
#define SDIOH_MODE_SPI		0
#define SDIOH_MODE_SD1		1
#define SDIOH_MODE_SD4		2

#define MAX_SLOTS 6 	/* For PCI: Only 6 BAR entries => 6 slots */
#define SDIOH_REG_WINSZ	0x100 /* Number of registers in Standard Host Controller */

#define SDIOH_TYPE_ARASAN_HDK	1
#define SDIOH_TYPE_BCM27XX	2
#define SDIOH_TYPE_TI_PCIXX21	4	/* TI PCIxx21 Standard Host Controller */
#define SDIOH_TYPE_RICOH_R5C822	5	/* Ricoh Co Ltd R5C822 SD/SDIO/MMC/MS/MSPro Host Adapter */
#define SDIOH_TYPE_JMICRON	6	/* JMicron Standard SDIO Host Controller */

/* For linux, allow yielding for dongle */
#define BCMSDYIELD

/* Expected card status value for CMD7 */
#define SDIOH_CMD7_EXP_STATUS   0x00001E00

#define RETRIES_LARGE 100000
#define RETRIES_SMALL 100


#define USE_BLOCKMODE		0x2	/* Block mode can be single block or multi */
#define USE_MULTIBLOCK		0x4

#define USE_FIFO		0x8	/* Fifo vs non-fifo */

#define CLIENT_INTR 		0x100	/* Get rid of this! */


struct sdioh_info {
	uint cfg_bar;                   	/* pci cfg address for bar */
	uint32 caps;                    	/* cached value of capabilities reg */
	uint32 curr_caps;                    	/* max current capabilities reg */

	osl_t 		*osh;			/* osh handler */
	volatile char 	*mem_space;		/* pci device memory va */
	uint		lockcount; 		/* nest count of sdstd_lock() calls */
	bool		client_intr_enabled;	/* interrupt connnected flag */
	bool		intr_handler_valid;	/* client driver interrupt handler valid */
	sdioh_cb_fn_t	intr_handler;		/* registered interrupt handler */
	void		*intr_handler_arg;	/* argument to call interrupt handler */
	bool		initialized;		/* card initialized */
	uint		target_dev;		/* Target device ID */
	uint16		intmask;		/* Current active interrupts */
	void		*sdos_info;		/* Pointer to per-OS private data */

	uint32		controller_type;	/* Host controller type */
	uint8		version;		/* Host Controller Spec Compliance Version */
	uint 		irq;			/* Client irq */
	int 		intrcount;		/* Client interrupts */
	int 		local_intrcount;	/* Controller interrupts */
	bool 		host_init_done;		/* Controller initted */
	bool 		card_init_done;		/* Client SDIO interface initted */
	bool 		polled_mode;		/* polling for command completion */

	bool 		sd_blockmode;		/* sd_blockmode == FALSE => 64 Byte Cmd 53s. */
						/*  Must be on for sd_multiblock to be effective */
	bool 		use_client_ints;	/* If this is false, make sure to restore */
						/*  polling hack in wl_linux.c:wl_timer() */
	int 		adapter_slot;		/* Maybe dealing with multiple slots/controllers */
	int 		sd_mode;		/* SD1/SD4/SPI */
	int 		client_block_size[SDIOD_MAX_IOFUNCS];		/* Blocksize */
	uint32 		data_xfer_count;	/* Current transfer */
	uint16 		card_rca;		/* Current Address */
	int8		sd_dma_mode;		/* DMA Mode (PIO, SDMA, ... ADMA2) on CMD53 */
	uint8 		num_funcs;		/* Supported funcs on client */
	uint32 		com_cis_ptr;
	uint32 		func_cis_ptr[SDIOD_MAX_IOFUNCS];
	void		*dma_buf;		/* DMA Buffer virtual address */
	ulong		dma_phys;		/* DMA Buffer physical address */
	void		*adma2_dscr_buf;	/* ADMA2 Descriptor Buffer virtual address */
	ulong		adma2_dscr_phys;	/* ADMA2 Descriptor Buffer physical address */

	/* adjustments needed to make the dma align properly */
	void		*dma_start_buf;
	ulong		dma_start_phys;
	uint		alloced_dma_size;
	void		*adma2_dscr_start_buf;
	ulong		adma2_dscr_start_phys;
	uint		alloced_adma2_dscr_size;

	int 		r_cnt;			/* rx count */
	int 		t_cnt;			/* tx_count */
	bool		got_hcint;		/* local interrupt flag */
	uint16		last_intrstatus;	/* to cache intrstatus */
};

#define DMA_MODE_NONE	0
#define DMA_MODE_SDMA	1
#define DMA_MODE_ADMA1	2
#define DMA_MODE_ADMA2	3
#define DMA_MODE_ADMA2_64 4
#define DMA_MODE_AUTO	-1

#define USE_DMA(sd)		((bool)((sd->sd_dma_mode > 0) ? TRUE : FALSE))

/* SDIO Host Control Register DMA Mode Definitions */
#define SDIOH_SDMA_MODE			0
#define SDIOH_ADMA1_MODE		1
#define SDIOH_ADMA2_MODE		2
#define SDIOH_ADMA2_64_MODE		3

#define ADMA2_ATTRIBUTE_VALID		(1 << 0)	/* ADMA Descriptor line valid */
#define ADMA2_ATTRIBUTE_END			(1 << 1)	/* End of Descriptor */
#define ADMA2_ATTRIBUTE_INT			(1 << 2)	/* Interrupt when line is done */
#define ADMA2_ATTRIBUTE_ACT_NOP		(0 << 4)	/* Skip current line, go to next. */
#define ADMA2_ATTRIBUTE_ACT_RSV		(1 << 4)	/* Same as NOP */
#define ADMA1_ATTRIBUTE_ACT_SET		(1 << 4)	/* ADMA1 Only - set transfer length */
#define ADMA2_ATTRIBUTE_ACT_TRAN	(2 << 4)	/* Transfer Data of one descriptor line. */
#define ADMA2_ATTRIBUTE_ACT_LINK	(3 << 4)	/* Link Descriptor */

/* ADMA2 Descriptor Table Entry for 32-bit Address */
typedef struct adma2_dscr_32b {
	uint32 len_attr;
	uint32 phys_addr;
} adma2_dscr_32b_t;

/* ADMA1 Descriptor Table Entry */
typedef struct adma1_dscr {
	uint32 phys_addr_attr;
} adma1_dscr_t;

/************************************************************
 * Internal interfaces: per-port references into bcmsdstd.c
 */

/* Global message bits */
extern uint sd_msglevel;

/* OS-independent interrupt handler */
extern bool check_client_intr(sdioh_info_t *sd);

/* Core interrupt enable/disable of device interrupts */
extern void sdstd_devintr_on(sdioh_info_t *sd);
extern void sdstd_devintr_off(sdioh_info_t *sd);

/* Enable/disable interrupts for local controller events */
extern void sdstd_intrs_on(sdioh_info_t *sd, uint16 norm, uint16 err);
extern void sdstd_intrs_off(sdioh_info_t *sd, uint16 norm, uint16 err);

/* Wait for specified interrupt and error bits to be set */
extern void sdstd_spinbits(sdioh_info_t *sd, uint16 norm, uint16 err);


/**************************************************************
 * Internal interfaces: bcmsdstd.c references to per-port code
 */

/* Register mapping routines */
extern uint32 *sdstd_reg_map(osl_t *osh, int32 addr, int size);
extern void sdstd_reg_unmap(osl_t *osh, int32 addr, int size);

/* Interrupt (de)registration routines */
extern int sdstd_register_irq(sdioh_info_t *sd, uint irq);
extern void sdstd_free_irq(uint irq, sdioh_info_t *sd);

/* OS-specific interrupt wrappers (atomic interrupt enable/disable) */
extern void sdstd_lock(sdioh_info_t *sd);
extern void sdstd_unlock(sdioh_info_t *sd);

/* OS-specific wait-for-interrupt-or-status */
extern uint16 sdstd_waitbits(sdioh_info_t *sd, uint16 norm, uint16 err, bool yield);

/*
 *  'Standard' SDIO HOST CONTROLLER driver
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 * $Id: bcmsdstd.h 324797 2012-03-30 11:02:00Z $
 */
#ifndef	_BCM_SD_STD_H
#define	_BCM_SD_STD_H

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
#define sdstd_os_yield(sd)	do {} while (0)
#define RETRIES_SMALL 100


#define USE_BLOCKMODE		0x2	/* Block mode can be single block or multi */
#define USE_MULTIBLOCK		0x4

#define USE_FIFO		0x8	/* Fifo vs non-fifo */

#define CLIENT_INTR 		0x100	/* Get rid of this! */

#define HC_INTR_RETUNING	0x1000


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
	int 	host_UHSISupported;		/* whether UHSI is supported for HC. */
	int 	card_UHSI_voltage_Supported; 	/* whether UHSI is supported for
						 * Card in terms of Voltage [1.8 or 3.3].
						 */
	int	global_UHSI_Supp;	/* type of UHSI support in both host and card.
					 * HOST_SDR_UNSUPP: capabilities not supported/matched
					 * HOST_SDR_12_25: SDR12 and SDR25 supported
					 * HOST_SDR_50_104_DDR: one of SDR50/SDR104 or DDR50 supptd
					 */
	volatile int	sd3_dat_state; 		/* data transfer state used for retuning check */
	volatile int	sd3_tun_state; 		/* tuning state used for retuning check */
	bool	sd3_tuning_reqd; 	/* tuning requirement parameter */
	uint32	caps3;			/* cached value of 32 MSbits capabilities reg (SDIO 3.0) */
};

#define DMA_MODE_NONE	0
#define DMA_MODE_SDMA	1
#define DMA_MODE_ADMA1	2
#define DMA_MODE_ADMA2	3
#define DMA_MODE_ADMA2_64 4
#define DMA_MODE_AUTO	-1

#define USE_DMA(sd)		((bool)((sd->sd_dma_mode > 0) ? TRUE : FALSE))

/* States for Tuning and corr data */
#define TUNING_IDLE 			0
#define TUNING_START 			1
#define TUNING_START_AFTER_DAT 	2
#define TUNING_ONGOING 			3

#define DATA_TRANSFER_IDLE 		0
#define DATA_TRANSFER_ONGOING	1

#define CHECK_TUNING_PRE_DATA	1
#define CHECK_TUNING_POST_DATA	2

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
extern void sdstd_waitlockfree(sdioh_info_t *sd);

/* OS-specific wait-for-interrupt-or-status */
extern int sdstd_waitbits(sdioh_info_t *sd, uint16 norm, uint16 err, bool yield, uint16 *bits);

/* used by bcmsdstd_linux [implemented in sdstd] */
extern void sdstd_3_enable_retuning_int(sdioh_info_t *sd);
extern void sdstd_3_disable_retuning_int(sdioh_info_t *sd);
extern bool sdstd_3_is_retuning_int_set(sdioh_info_t *sd);
extern void sdstd_3_check_and_do_tuning(sdioh_info_t *sd, int tuning_param);
extern bool sdstd_3_check_and_set_retuning(sdioh_info_t *sd);
extern int sdstd_3_get_tune_state(sdioh_info_t *sd);
extern int sdstd_3_get_data_state(sdioh_info_t *sd);
extern void sdstd_3_set_tune_state(sdioh_info_t *sd, int state);
extern void sdstd_3_set_data_state(sdioh_info_t *sd, int state);
extern uint8 sdstd_3_get_tuning_exp(sdioh_info_t *sd);
extern uint32 sdstd_3_get_uhsi_clkmode(sdioh_info_t *sd);
extern int sdstd_3_clk_tuning(sdioh_info_t *sd, uint32 sd3ClkMode);

/* used by sdstd [implemented in bcmsdstd_linux/ndis] */
extern void sdstd_3_start_tuning(sdioh_info_t *sd);
extern void sdstd_3_osinit_tuning(sdioh_info_t *sd);
extern void sdstd_3_osclean_tuning(sdioh_info_t *sd);

#endif /* _BCM_SD_STD_H */

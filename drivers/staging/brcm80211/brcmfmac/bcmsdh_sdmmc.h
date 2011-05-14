/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __BCMSDH_SDMMC_H__
#define __BCMSDH_SDMMC_H__

#ifdef BCMDBG
#define sd_err(x)	\
	do { \
		if ((sd_msglevel & SDH_ERROR_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_trace(x)	\
	do { \
		if ((sd_msglevel & SDH_TRACE_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_info(x)	\
	do { \
		if ((sd_msglevel & SDH_INFO_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_debug(x)	\
	do { \
		if ((sd_msglevel & SDH_DEBUG_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_data(x)	\
	do { \
		if ((sd_msglevel & SDH_DATA_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_ctrl(x)	\
	do { \
		if ((sd_msglevel & SDH_CTRL_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#else
#define sd_err(x)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)
#endif

/* Allocate/init/free per-OS private data */
extern int sdioh_sdmmc_osinit(sdioh_info_t *sd);
extern void sdioh_sdmmc_osfree(sdioh_info_t *sd);

#define BLOCK_SIZE_64 64
#define BLOCK_SIZE_512 512
#define BLOCK_SIZE_4318 64
#define BLOCK_SIZE_4328 512

/* internal return code */
#define SUCCESS	0
#define ERROR	1

/* private bus modes */
#define SDIOH_MODE_SD4		2
#define CLIENT_INTR 		0x100	/* Get rid of this! */

struct sdioh_info {
	struct osl_info *osh;		/* osh handler */
	bool client_intr_enabled;	/* interrupt connnected flag */
	bool intr_handler_valid;	/* client driver interrupt handler valid */
	sdioh_cb_fn_t intr_handler;	/* registered interrupt handler */
	void *intr_handler_arg;	/* argument to call interrupt handler */
	u16 intmask;		/* Current active interrupts */
	void *sdos_info;	/* Pointer to per-OS private data */

	uint irq;		/* Client irq */
	int intrcount;		/* Client interrupts */
	bool sd_use_dma;	/* DMA on CMD53 */
	bool sd_blockmode;	/* sd_blockmode == false => 64 Byte Cmd 53s. */
	/*  Must be on for sd_multiblock to be effective */
	bool use_client_ints;	/* If this is false, make sure to restore */
	int sd_mode;		/* SD1/SD4/SPI */
	int client_block_size[SDIOD_MAX_IOFUNCS];	/* Blocksize */
	u8 num_funcs;	/* Supported funcs on client */
	u32 com_cis_ptr;
	u32 func_cis_ptr[SDIOD_MAX_IOFUNCS];
	uint max_dma_len;
	uint max_dma_descriptors;	/* DMA Descriptors supported by this controller. */
	/*	SDDMA_DESCRIPTOR	SGList[32]; *//* Scatter/Gather DMA List */
};

/************************************************************
 * Internal interfaces: per-port references into bcmsdh_sdmmc.c
 */

/* Global message bits */
extern uint sd_msglevel;

/* OS-independent interrupt handler */
extern bool check_client_intr(sdioh_info_t *sd);

/* Core interrupt enable/disable of device interrupts */
extern void sdioh_sdmmc_devintr_on(sdioh_info_t *sd);
extern void sdioh_sdmmc_devintr_off(sdioh_info_t *sd);

/**************************************************************
 * Internal interfaces: bcmsdh_sdmmc.c references to per-port code
 */

/* Register mapping routines */
extern u32 *sdioh_sdmmc_reg_map(s32 addr, int size);
extern void sdioh_sdmmc_reg_unmap(s32 addr, int size);

/* Interrupt (de)registration routines */
extern int sdioh_sdmmc_register_irq(sdioh_info_t *sd, uint irq);
extern void sdioh_sdmmc_free_irq(uint irq, sdioh_info_t *sd);

typedef struct _BCMSDH_SDMMC_INSTANCE {
	sdioh_info_t *sd;
	struct sdio_func *func[SDIOD_MAX_IOFUNCS];
	u32 host_claimed;
} BCMSDH_SDMMC_INSTANCE, *PBCMSDH_SDMMC_INSTANCE;

#endif				/* __BCMSDH_SDMMC_H__ */

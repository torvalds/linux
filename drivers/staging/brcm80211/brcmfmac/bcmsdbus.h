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

#ifndef	_sdio_api_h_
#define	_sdio_api_h_

#include "sdio_host.h"

#define SDIOH_READ              0	/* Read request */
#define SDIOH_WRITE             1	/* Write request */

#define SDIOH_DATA_FIX          0	/* Fixed addressing */
#define SDIOH_DATA_INC          1	/* Incremental addressing */

#define SDIOH_CMD_TYPE_NORMAL   0	/* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1	/* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2	/* Cut-through command */

#define SDIOH_DATA_PIO          0	/* PIO mode */
#define SDIOH_DATA_DMA          1	/* DMA mode */

/* internal return code */
#define SUCCESS	0
#define ERROR	1

/* Common msglevel constants */
#define SDH_ERROR_VAL		0x0001	/* Error */
#define SDH_TRACE_VAL		0x0002	/* Trace */
#define SDH_INFO_VAL		0x0004	/* Info */
#define SDH_DEBUG_VAL		0x0008	/* Debug */
#define SDH_DATA_VAL		0x0010	/* Data */
#define SDH_CTRL_VAL		0x0020	/* Control Regs */
#define SDH_LOG_VAL		0x0040	/* Enable bcmlog */
#define SDH_DMA_VAL		0x0080	/* DMA */

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

struct brcmf_sdreg {
	int func;
	int offset;
	int value;
};

/* callback function, taking one arg */
typedef void (*sdioh_cb_fn_t) (void *);

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
	bool sd_blockmode;	/* sd_blockmode == false => 64 Byte Cmd 53s. */
	/*  Must be on for sd_multiblock to be effective */
	bool use_client_ints;	/* If this is false, make sure to restore */
	int client_block_size[SDIOD_MAX_IOFUNCS];	/* Blocksize */
	u8 num_funcs;	/* Supported funcs on client */
	u32 com_cis_ptr;
	u32 func_cis_ptr[SDIOD_MAX_IOFUNCS];
	uint max_dma_len;
	uint max_dma_descriptors;	/* DMA Descriptors supported by this controller. */
	/*	SDDMA_DESCRIPTOR	SGList[32]; *//* Scatter/Gather DMA List */
};

struct brcmf_sdmmc_instance {
	struct sdioh_info *sd;
	struct sdio_func *func[SDIOD_MAX_IOFUNCS];
	u32 host_claimed;
};

/* Allocate/init/free per-OS private data */
extern int  brcmf_sdioh_osinit(struct sdioh_info *sd);
extern void brcmf_sdioh_osfree(struct sdioh_info *sd);

/* OS-independent interrupt handler */
extern bool brcmf_sdioh_check_client_intr(struct sdioh_info *sd);

/* Core interrupt enable/disable of device interrupts */
extern void brcmf_sdioh_dev_intr_on(struct sdioh_info *sd);
extern void brcmf_sdioh_dev_intr_off(struct sdioh_info *sd);

/* Register mapping routines */
extern u32 *brcmf_sdioh_reg_map(s32 addr, int size);
extern void brcmf_sdioh_reg_unmap(s32 addr, int size);

/* Interrupt (de)registration routines */
extern int  brcmf_sdioh_register_irq(struct sdioh_info *sd, uint irq);
extern void brcmf_sdioh_free_irq(uint irq, struct sdioh_info *sd);

/* attach, return handler on success, NULL if failed.
 *  The handler shall be provided by all subsequent calls. No local cache
 *  cfghdl points to the starting address of pci device mapped memory
 */
extern struct sdioh_info *brcmf_sdioh_attach(void *cfghdl, uint irq);
extern int brcmf_sdioh_detach(struct sdioh_info *si);
extern int brcmf_sdioh_interrupt_register(struct sdioh_info *si,
					     sdioh_cb_fn_t fn, void *argh);
extern int brcmf_sdioh_interrupt_deregister(struct sdioh_info *si);

/* query whether SD interrupt is enabled or not */
extern int
brcmf_sdioh_interrupt_query(struct sdioh_info *si, bool *onoff);

/* enable or disable SD interrupt */
extern int
brcmf_sdioh_interrupt_set(struct sdioh_info *si, bool enable_disable);

/* read or write one byte using cmd52 */
extern int
brcmf_sdioh_request_byte(struct sdioh_info *si, uint rw, uint fnc, uint addr,
			 u8 *byte);

/* read or write 2/4 bytes using cmd53 */
extern int
brcmf_sdioh_request_word(struct sdioh_info *si, uint cmd_type,
			 uint rw, uint fnc, uint addr,
			 u32 *word, uint nbyte);

/* read or write any buffer using cmd53 */
extern int
brcmf_sdioh_request_buffer(struct sdioh_info *si, uint pio_dma,
			   uint fix_inc, uint rw, uint fnc_num,
			   u32 addr, uint regwidth,
			   u32 buflen, u8 *buffer, struct sk_buff *pkt);

/* get cis data */
extern int
brcmf_sdioh_cis_read(struct sdioh_info *si, uint fuc, u8 *cis, u32 length);

extern int
brcmf_sdioh_cfg_read(struct sdioh_info *si, uint fuc, u32 addr, u8 *data);
extern int
brcmf_sdioh_cfg_write(struct sdioh_info *si, uint fuc, u32 addr, u8 *data);

/* query number of io functions */
extern uint brcmf_sdioh_query_iofnum(struct sdioh_info *si);

/* handle iovars */
extern int brcmf_sdioh_iovar_op(struct sdioh_info *si, const char *name,
			  void *params, int plen, void *arg, int len, bool set);

/* Issue abort to the specified function and clear controller as needed */
extern int brcmf_sdioh_abort(struct sdioh_info *si, uint fnc);

/* Helper function */
void *brcmf_sdcard_get_sdioh(struct brcmf_sdio_card *card);

/* Watchdog timer interface for pm ops */
extern void brcmf_sdio_wdtmr_enable(bool enable);

extern uint sd_msglevel;	/* Debug message level */

extern struct brcmf_sdmmc_instance *gInstance;

#endif				/* _sdio_api_h_ */

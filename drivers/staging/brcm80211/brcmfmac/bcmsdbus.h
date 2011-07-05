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

/* callback function, taking one arg */
typedef void (*sdioh_cb_fn_t) (void *);

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

extern int brcmf_sdioh_claim_host_and_lock(struct sdioh_info *si);
extern int brcmf_sdioh_release_host_and_unlock(struct sdioh_info *si);

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

#endif				/* _sdio_api_h_ */

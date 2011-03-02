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

#define SDIOH_API_RC_SUCCESS                          (0x00)
#define SDIOH_API_RC_FAIL	                      (0x01)
#define SDIOH_API_SUCCESS(status) (status == 0)

#define SDIOH_READ              0	/* Read request */
#define SDIOH_WRITE             1	/* Write request */

#define SDIOH_DATA_FIX          0	/* Fixed addressing */
#define SDIOH_DATA_INC          1	/* Incremental addressing */

#define SDIOH_CMD_TYPE_NORMAL   0	/* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1	/* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2	/* Cut-through command */

#define SDIOH_DATA_PIO          0	/* PIO mode */
#define SDIOH_DATA_DMA          1	/* DMA mode */

typedef int SDIOH_API_RC;

/* SDio Host structure */
typedef struct sdioh_info sdioh_info_t;

/* callback function, taking one arg */
typedef void (*sdioh_cb_fn_t) (void *);

/* attach, return handler on success, NULL if failed.
 *  The handler shall be provided by all subsequent calls. No local cache
 *  cfghdl points to the starting address of pci device mapped memory
 */
extern sdioh_info_t *sdioh_attach(void *cfghdl, uint irq);
extern SDIOH_API_RC sdioh_detach(sdioh_info_t *si);
extern SDIOH_API_RC sdioh_interrupt_register(sdioh_info_t *si,
					     sdioh_cb_fn_t fn, void *argh);
extern SDIOH_API_RC sdioh_interrupt_deregister(sdioh_info_t *si);

/* query whether SD interrupt is enabled or not */
extern SDIOH_API_RC sdioh_interrupt_query(sdioh_info_t *si, bool *onoff);

/* enable or disable SD interrupt */
extern SDIOH_API_RC sdioh_interrupt_set(sdioh_info_t *si, bool enable_disable);

#if defined(DHD_DEBUG)
extern bool sdioh_interrupt_pending(sdioh_info_t *si);
#endif

extern int sdioh_claim_host_and_lock(sdioh_info_t *si);
extern int sdioh_release_host_and_unlock(sdioh_info_t *si);

/* read or write one byte using cmd52 */
extern SDIOH_API_RC sdioh_request_byte(sdioh_info_t *si, uint rw, uint fnc,
				       uint addr, u8 *byte);

/* read or write 2/4 bytes using cmd53 */
extern SDIOH_API_RC sdioh_request_word(sdioh_info_t *si, uint cmd_type,
				       uint rw, uint fnc, uint addr,
				       u32 *word, uint nbyte);

/* read or write any buffer using cmd53 */
extern SDIOH_API_RC sdioh_request_buffer(sdioh_info_t *si, uint pio_dma,
					 uint fix_inc, uint rw, uint fnc_num,
					 u32 addr, uint regwidth,
					 u32 buflen, u8 *buffer,
					 struct sk_buff *pkt);

/* get cis data */
extern SDIOH_API_RC sdioh_cis_read(sdioh_info_t *si, uint fuc, u8 *cis,
				   u32 length);

extern SDIOH_API_RC sdioh_cfg_read(sdioh_info_t *si, uint fuc, u32 addr,
				   u8 *data);
extern SDIOH_API_RC sdioh_cfg_write(sdioh_info_t *si, uint fuc, u32 addr,
				    u8 *data);

/* query number of io functions */
extern uint sdioh_query_iofnum(sdioh_info_t *si);

/* handle iovars */
extern int sdioh_iovar_op(sdioh_info_t *si, const char *name,
			  void *params, int plen, void *arg, int len, bool set);

/* Issue abort to the specified function and clear controller as needed */
extern int sdioh_abort(sdioh_info_t *si, uint fnc);

/* Start and Stop SDIO without re-enumerating the SD card. */
extern int sdioh_start(sdioh_info_t *si, int stage);
extern int sdioh_stop(sdioh_info_t *si);

/* Reset and re-initialize the device */
extern int sdioh_sdio_reset(sdioh_info_t *si);

/* Helper function */
void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);

#endif				/* _sdio_api_h_ */

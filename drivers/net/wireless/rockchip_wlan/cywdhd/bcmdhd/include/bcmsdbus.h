/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for API from sdio common code (bcmsdh) to individual
 * host controller drivers.
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmsdbus.h 514727 2014-11-12 03:02:48Z $
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

#define SDIOH_CMD_TYPE_NORMAL   0       /* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1       /* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2       /* Cut-through command */

#define SDIOH_DATA_PIO          0       /* PIO mode */
#define SDIOH_DATA_DMA          1       /* DMA mode */

/* Max number of glommed pkts */
#ifdef CUSTOM_MAX_TXGLOM_SIZE
#define SDPCM_MAXGLOM_SIZE  CUSTOM_MAX_TXGLOM_SIZE
#else
#define SDPCM_MAXGLOM_SIZE	40
#endif /* CUSTOM_MAX_TXGLOM_SIZE */

#define SDPCM_TXGLOM_CPY 0			/* SDIO 2.0 should use copy mode */
#define SDPCM_TXGLOM_MDESC	1		/* SDIO 3.0 should use multi-desc mode */

#ifdef CUSTOM_DEF_TXGLOM_SIZE
#define SDPCM_DEFGLOM_SIZE  CUSTOM_DEF_TXGLOM_SIZE
#else
#define SDPCM_DEFGLOM_SIZE SDPCM_MAXGLOM_SIZE
#endif /* CUSTOM_DEF_TXGLOM_SIZE */

#if SDPCM_DEFGLOM_SIZE > SDPCM_MAXGLOM_SIZE
#warning "SDPCM_DEFGLOM_SIZE cannot be higher than SDPCM_MAXGLOM_SIZE!!"
#undef SDPCM_DEFGLOM_SIZE
#define SDPCM_DEFGLOM_SIZE SDPCM_MAXGLOM_SIZE
#endif

typedef int SDIOH_API_RC;

/* SDio Host structure */
typedef struct sdioh_info sdioh_info_t;

/* callback function, taking one arg */
typedef void (*sdioh_cb_fn_t)(void *);

extern SDIOH_API_RC sdioh_interrupt_register(sdioh_info_t *si, sdioh_cb_fn_t fn, void *argh);
extern SDIOH_API_RC sdioh_interrupt_deregister(sdioh_info_t *si);

/* query whether SD interrupt is enabled or not */
extern SDIOH_API_RC sdioh_interrupt_query(sdioh_info_t *si, bool *onoff);

/* enable or disable SD interrupt */
extern SDIOH_API_RC sdioh_interrupt_set(sdioh_info_t *si, bool enable_disable);

#if defined(DHD_DEBUG)
extern bool sdioh_interrupt_pending(sdioh_info_t *si);
#endif

/* read or write one byte using cmd52 */
extern SDIOH_API_RC sdioh_request_byte(sdioh_info_t *si, uint rw, uint fnc, uint addr, uint8 *byte);

/* read or write 2/4 bytes using cmd53 */
extern SDIOH_API_RC sdioh_request_word(sdioh_info_t *si, uint cmd_type, uint rw, uint fnc,
	uint addr, uint32 *word, uint nbyte);

/* read or write any buffer using cmd53 */
extern SDIOH_API_RC sdioh_request_buffer(sdioh_info_t *si, uint pio_dma, uint fix_inc,
	uint rw, uint fnc_num, uint32 addr, uint regwidth, uint32 buflen, uint8 *buffer,
	void *pkt);

/* get cis data */
extern SDIOH_API_RC sdioh_cis_read(sdioh_info_t *si, uint fuc, uint8 *cis, uint32 length);

extern SDIOH_API_RC sdioh_cfg_read(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);
extern SDIOH_API_RC sdioh_cfg_write(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);

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

/* Wait system lock free */
extern int sdioh_waitlockfree(sdioh_info_t *si);

/* Reset and re-initialize the device */
extern int sdioh_sdio_reset(sdioh_info_t *si);



#if defined(BCMSDIOH_STD)
	#define SDIOH_SLEEP_ENABLED
#endif
extern SDIOH_API_RC sdioh_sleep(sdioh_info_t *si, bool enab);

/* GPIO support */
extern SDIOH_API_RC sdioh_gpio_init(sdioh_info_t *sd);
extern bool sdioh_gpioin(sdioh_info_t *sd, uint32 gpio);
extern SDIOH_API_RC sdioh_gpioouten(sdioh_info_t *sd, uint32 gpio);
extern SDIOH_API_RC sdioh_gpioout(sdioh_info_t *sd, uint32 gpio, bool enab);

#endif /* _sdio_api_h_ */

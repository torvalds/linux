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

#ifndef	_BRCM_SDH_H_
#define	_BRCM_SDH_H_

#include <linux/skbuff.h>
#define BRCMF_SD_ERROR_VAL	0x0001	/* Error */
#define BRCMF_SD_INFO_VAL		0x0002	/* Info */
extern const uint brcmf_sdio_msglevel;

#ifdef BCMDBG
#define BRCMF_SD_ERROR(x) \
	do { \
		if ((brcmf_sdio_msglevel & BRCMF_SD_ERROR_VAL) && \
		    net_ratelimit()) \
			printk x; \
	} while (0)
#define BRCMF_SD_INFO(x)	\
	do { \
		if ((brcmf_sdio_msglevel & BRCMF_SD_INFO_VAL) && \
		    net_ratelimit()) \
			printk x; \
	} while (0)
#else				/* BCMDBG */
#define BRCMF_SD_ERROR(x)
#define BRCMF_SD_INFO(x)
#endif				/* BCMDBG */

#define SDIO_FUNC_0		0
#define SDIO_FUNC_1		1
#define SDIO_FUNC_2		2

#define SDIOD_FBR_SIZE		0x100

/* io_en */
#define SDIO_FUNC_ENABLE_1	0x02
#define SDIO_FUNC_ENABLE_2	0x04

/* io_rdys */
#define SDIO_FUNC_READY_1	0x02
#define SDIO_FUNC_READY_2	0x04

/* intr_status */
#define INTR_STATUS_FUNC1	0x2
#define INTR_STATUS_FUNC2	0x4

/* Maximum number of I/O funcs */
#define SDIOD_MAX_IOFUNCS	7

/* forward declarations */
struct brcmf_sdio_card;
typedef void (*brcmf_sdiocard_cb_fn_t) (void *);

/* Attach and build an interface to the underlying SD host driver.
 *  - Allocates resources (structs, arrays, mem, OS handles, etc) needed by
 *    brcmf_sdcard.
 *  - Returns the sdio card handle and virtual address base for register access.
 *    The returned handle should be used in all subsequent calls, but the bcmsh
 *    implementation may maintain a single "default" handle (e.g. the first or
 *    most recent one) to enable single-instance implementations to pass NULL.
 */
extern struct brcmf_sdio_card *brcmf_sdcard_attach(void *cfghdl, void **regsva,
					  uint irq);

/* Detach - freeup resources allocated in attach */
extern int brcmf_sdcard_detach(struct brcmf_sdio_card *card);

/* Query if SD device interrupts are enabled */
extern bool brcmf_sdcard_intr_query(struct brcmf_sdio_card *card);

/* Enable/disable SD interrupt */
extern int brcmf_sdcard_intr_enable(struct brcmf_sdio_card *card);
extern int brcmf_sdcard_intr_disable(struct brcmf_sdio_card *card);

/* Register/deregister device interrupt handler. */
extern int
brcmf_sdcard_intr_reg(struct brcmf_sdio_card *card, brcmf_sdiocard_cb_fn_t fn,
		      void *argh);

extern int brcmf_sdcard_intr_dereg(struct brcmf_sdio_card *card);

/* Register a callback to be called on device removal.
 * No-op in the case of non-removable/hardwired devices.
 */
extern int
brcmf_sdcard_devremove_reg(struct brcmf_sdio_card *card,
			   brcmf_sdiocard_cb_fn_t fn,
			   void *argh);

/* Access SDIO address space (e.g. CCCR) using CMD52 (single-byte interface).
 *   fn:   function number
 *   addr: unmodified SDIO-space address
 *   data: data byte to write
 *   err:  pointer to error code (or NULL)
 */
extern u8 brcmf_sdcard_cfg_read(struct brcmf_sdio_card *card, uint func,
				u32 addr, int *err);
extern void brcmf_sdcard_cfg_write(struct brcmf_sdio_card *card, uint func,
				   u32 addr, u8 data, int *err);

/* Read/Write 4bytes from/to cfg space */
extern u32
brcmf_sdcard_cfg_read_word(struct brcmf_sdio_card *card, uint fnc_num,
			   u32 addr, int *err);

extern void brcmf_sdcard_cfg_write_word(struct brcmf_sdio_card *card,
					uint fnc_num, u32 addr,
					u32 data, int *err);

/* Read CIS content for specified function.
 *   fn:     function whose CIS is being requested (0 is common CIS)
 *   cis:    pointer to memory location to place results
 *   length: number of bytes to read
 * Internally, this routine uses the values from the cis base regs (0x9-0xB)
 * to form an SDIO-space address to read the data from.
 */
extern int brcmf_sdcard_cis_read(struct brcmf_sdio_card *card, uint func,
				 u8 *cis, uint length);

/* Synchronous access to device (client) core registers via CMD53 to F1.
 *   addr: backplane address (i.e. >= regsva from attach)
 *   size: register width in bytes (2 or 4)
 *   data: data for register write
 */
extern u32
brcmf_sdcard_reg_read(struct brcmf_sdio_card *card, u32 addr, uint size);

extern u32
brcmf_sdcard_reg_write(struct brcmf_sdio_card *card, u32 addr, uint size,
		       u32 data);

/* Indicate if last reg read/write failed */
extern bool brcmf_sdcard_regfail(struct brcmf_sdio_card *card);

/* Buffer transfer to/from device (client) core via cmd53.
 *   fn:       function number
 *   addr:     backplane address (i.e. >= regsva from attach)
 *   flags:    backplane width, address increment, sync/async
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 *   pkt:      pointer to packet associated with buf (if any)
 *   complete: callback function for command completion (async only)
 *   handle:   handle for completion callback (first arg in callback)
 * Returns 0 or error code.
 * NOTE: Async operation is not currently supported.
 */
typedef void (*brcmf_sdio_cmplt_fn_t)
		(void *handle, int status, bool sync_waiting);
extern int
brcmf_sdcard_send_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, void *pkt,
		      brcmf_sdio_cmplt_fn_t complete, void *handle);
extern int
brcmf_sdcard_recv_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, struct sk_buff *pkt,
		      brcmf_sdio_cmplt_fn_t complete, void *handle);

/* Flags bits */
#define SDIO_REQ_4BYTE	0x1	/* Four-byte target (backplane) width (vs. two-byte) */
#define SDIO_REQ_FIXED	0x2	/* Fixed address (FIFO) (vs. incrementing address) */
#define SDIO_REQ_ASYNC	0x4	/* Async request (vs. sync request) */

/* Pending (non-error) return code */
#define BCME_PENDING	1

/* Read/write to memory block (F1, no FIFO) via CMD53 (sync only).
 *   rw:       read or write (0/1)
 *   addr:     direct SDIO address
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 * Returns 0 or error code.
 */
extern int brcmf_sdcard_rwdata(struct brcmf_sdio_card *card, uint rw, u32 addr,
			       u8 *buf, uint nbytes);

/* Issue an abort to the specified function */
extern int brcmf_sdcard_abort(struct brcmf_sdio_card *card, uint fn);

/* Start SDIO Host Controller communication */
extern int brcmf_sdcard_start(struct brcmf_sdio_card *card, int stage);

/* Stop SDIO Host Controller communication */
extern int brcmf_sdcard_stop(struct brcmf_sdio_card *card);

/* Returns the "Device ID" of target device on the SDIO bus. */
extern int brcmf_sdcard_query_device(struct brcmf_sdio_card *card);

/* Returns the number of IO functions reported by the device */
extern uint brcmf_sdcard_query_iofnum(struct brcmf_sdio_card *card);

/* Miscellaneous knob tweaker. */
extern int brcmf_sdcard_iovar_op(struct brcmf_sdio_card *card, const char *name,
				 void *params, int plen, void *arg, int len,
				 bool set);

/* helper functions */

extern void *brcmf_sdcard_get_sdioh(struct brcmf_sdio_card *card);

/* callback functions */
struct brcmf_sdioh_driver {
	/* attach to device */
	void *(*attach) (u16 vend_id, u16 dev_id, u16 bus, u16 slot,
			 u16 func, uint bustype, void *regsva, void *param);
	/* detach from device */
	void (*detach) (void *ch);
};

struct sdioh_info;

/* platform specific/high level functions */
extern int brcmf_sdio_function_init(void);
extern int brcmf_sdio_register(struct brcmf_sdioh_driver *driver);
extern void brcmf_sdio_unregister(void);
extern bool brcmf_sdio_chipmatch(u16 vendor, u16 device);
extern void brcmf_sdio_device_remove(void *card);
extern void brcmf_sdio_function_cleanup(void);

extern void brcmf_sdioh_dev_intr_off(struct sdioh_info *sd);
extern void brcmf_sdioh_dev_intr_on(struct sdioh_info *sd);
extern int brcmf_sdio_probe(struct device *dev);
extern int brcmf_sdio_remove(struct device *dev);

/* Function to pass device-status bits to DHD. */
extern u32 brcmf_sdcard_get_dstatus(struct brcmf_sdio_card *card);

/* Function to return current window addr */
extern u32 brcmf_sdcard_cur_sbwad(struct brcmf_sdio_card *card);

/* Function to pass chipid and rev to lower layers for controlling pr's */
extern void brcmf_sdcard_chipinfo(struct brcmf_sdio_card *card, u32 chip,
				  u32 chiprev);

#endif				/* _BRCM_SDH_H_ */

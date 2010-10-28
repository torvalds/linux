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

#ifndef	_bcmsdh_h_
#define	_bcmsdh_h_

#define BCMSDH_ERROR_VAL	0x0001	/* Error */
#define BCMSDH_INFO_VAL		0x0002	/* Info */
extern const uint bcmsdh_msglevel;

#ifdef BCMDBG
#define BCMSDH_ERROR(x)	do { if ((bcmsdh_msglevel & BCMSDH_ERROR_VAL) && net_ratelimit()) printf x; } while (0)
#define BCMSDH_INFO(x)	do { if ((bcmsdh_msglevel & BCMSDH_INFO_VAL) && net_ratelimit()) printf x; } while (0)
#else				/* BCMDBG */
#define BCMSDH_ERROR(x)
#define BCMSDH_INFO(x)
#endif				/* BCMDBG */

/* forward declarations */
typedef struct bcmsdh_info bcmsdh_info_t;
typedef void (*bcmsdh_cb_fn_t) (void *);

/* Attach and build an interface to the underlying SD host driver.
 *  - Allocates resources (structs, arrays, mem, OS handles, etc) needed by bcmsdh.
 *  - Returns the bcmsdh handle and virtual address base for register access.
 *    The returned handle should be used in all subsequent calls, but the bcmsh
 *    implementation may maintain a single "default" handle (e.g. the first or
 *    most recent one) to enable single-instance implementations to pass NULL.
 */
extern bcmsdh_info_t *bcmsdh_attach(osl_t *osh, void *cfghdl, void **regsva,
				    uint irq);

/* Detach - freeup resources allocated in attach */
extern int bcmsdh_detach(osl_t *osh, void *sdh);

/* Query if SD device interrupts are enabled */
extern bool bcmsdh_intr_query(void *sdh);

/* Enable/disable SD interrupt */
extern int bcmsdh_intr_enable(void *sdh);
extern int bcmsdh_intr_disable(void *sdh);

/* Register/deregister device interrupt handler. */
extern int bcmsdh_intr_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);
extern int bcmsdh_intr_dereg(void *sdh);

#if defined(BCMDBG)
/* Query pending interrupt status from the host controller */
extern bool bcmsdh_intr_pending(void *sdh);
#endif
extern int bcmsdh_claim_host_and_lock(void *sdh);
extern int bcmsdh_release_host_and_unlock(void *sdh);

/* Register a callback to be called if and when bcmsdh detects
 * device removal. No-op in the case of non-removable/hardwired devices.
 */
extern int bcmsdh_devremove_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);

/* Access SDIO address space (e.g. CCCR) using CMD52 (single-byte interface).
 *   fn:   function number
 *   addr: unmodified SDIO-space address
 *   data: data byte to write
 *   err:  pointer to error code (or NULL)
 */
extern u8 bcmsdh_cfg_read(void *sdh, uint func, u32 addr, int *err);
extern void bcmsdh_cfg_write(void *sdh, uint func, u32 addr, u8 data,
			     int *err);

/* Read/Write 4bytes from/to cfg space */
extern u32 bcmsdh_cfg_read_word(void *sdh, uint fnc_num, u32 addr,
				   int *err);
extern void bcmsdh_cfg_write_word(void *sdh, uint fnc_num, u32 addr,
				  u32 data, int *err);

/* Read CIS content for specified function.
 *   fn:     function whose CIS is being requested (0 is common CIS)
 *   cis:    pointer to memory location to place results
 *   length: number of bytes to read
 * Internally, this routine uses the values from the cis base regs (0x9-0xB)
 * to form an SDIO-space address to read the data from.
 */
extern int bcmsdh_cis_read(void *sdh, uint func, u8 *cis, uint length);

/* Synchronous access to device (client) core registers via CMD53 to F1.
 *   addr: backplane address (i.e. >= regsva from attach)
 *   size: register width in bytes (2 or 4)
 *   data: data for register write
 */
extern u32 bcmsdh_reg_read(void *sdh, u32 addr, uint size);
extern u32 bcmsdh_reg_write(void *sdh, u32 addr, uint size, u32 data);

/* Indicate if last reg read/write failed */
extern bool bcmsdh_regfail(void *sdh);

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
typedef void (*bcmsdh_cmplt_fn_t) (void *handle, int status, bool sync_waiting);
extern int bcmsdh_send_buf(void *sdh, u32 addr, uint fn, uint flags,
			   u8 *buf, uint nbytes, void *pkt,
			   bcmsdh_cmplt_fn_t complete, void *handle);
extern int bcmsdh_recv_buf(void *sdh, u32 addr, uint fn, uint flags,
			   u8 *buf, uint nbytes, void *pkt,
			   bcmsdh_cmplt_fn_t complete, void *handle);

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
extern int bcmsdh_rwdata(void *sdh, uint rw, u32 addr, u8 *buf,
			 uint nbytes);

/* Issue an abort to the specified function */
extern int bcmsdh_abort(void *sdh, uint fn);

/* Start SDIO Host Controller communication */
extern int bcmsdh_start(void *sdh, int stage);

/* Stop SDIO Host Controller communication */
extern int bcmsdh_stop(void *sdh);

/* Returns the "Device ID" of target device on the SDIO bus. */
extern int bcmsdh_query_device(void *sdh);

/* Returns the number of IO functions reported by the device */
extern uint bcmsdh_query_iofnum(void *sdh);

/* Miscellaneous knob tweaker. */
extern int bcmsdh_iovar_op(void *sdh, const char *name,
			   void *params, int plen, void *arg, int len,
			   bool set);

/* Reset and reinitialize the device */
extern int bcmsdh_reset(bcmsdh_info_t *sdh);

/* helper functions */

extern void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);

/* callback functions */
typedef struct {
	/* attach to device */
	void *(*attach) (u16 vend_id, u16 dev_id, u16 bus, u16 slot,
			 u16 func, uint bustype, void *regsva, osl_t *osh,
			 void *param);
	/* detach from device */
	void (*detach) (void *ch);
} bcmsdh_driver_t;

/* platform specific/high level functions */
extern int bcmsdh_register(bcmsdh_driver_t *driver);
extern void bcmsdh_unregister(void);
extern bool bcmsdh_chipmatch(u16 vendor, u16 device);
extern void bcmsdh_device_remove(void *sdh);

/* Function to pass device-status bits to DHD. */
extern u32 bcmsdh_get_dstatus(void *sdh);

/* Function to return current window addr */
extern u32 bcmsdh_cur_sbwad(void *sdh);

/* Function to pass chipid and rev to lower layers for controlling pr's */
extern void bcmsdh_chipinfo(void *sdh, u32 chip, u32 chiprev);

#endif				/* _bcmsdh_h_ */

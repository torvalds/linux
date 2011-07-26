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
extern const uint brcmf_sdio_msglevel;

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

#define SBSDIO_NUM_FUNCTION		3	/* as of sdiod rev 0, supports 3 functions */

/* function 1 miscellaneous registers */
#define SBSDIO_SPROM_CS			0x10000	/* sprom command and status */
#define SBSDIO_SPROM_INFO		0x10001	/* sprom info register */
#define SBSDIO_SPROM_DATA_LOW		0x10002	/* sprom indirect access data byte 0 */
#define SBSDIO_SPROM_DATA_HIGH		0x10003	/* sprom indirect access data byte 1 */
#define SBSDIO_SPROM_ADDR_LOW		0x10004	/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_HIGH		0x10005	/* sprom indirect access addr byte 0 */
#define SBSDIO_CHIP_CTRL_DATA		0x10006	/* xtal_pu (gpio) output */
#define SBSDIO_CHIP_CTRL_EN		0x10007	/* xtal_pu (gpio) enable */
#define SBSDIO_WATERMARK		0x10008	/* rev < 7, watermark for sdio device */
#define SBSDIO_DEVICE_CTL		0x10009	/* control busy signal generation */

/* registers introduced in rev 8, some content (mask/bits) defs in sbsdpcmdev.h */
#define SBSDIO_FUNC1_SBADDRLOW		0x1000A	/* SB Address Window Low (b15) */
#define SBSDIO_FUNC1_SBADDRMID		0x1000B	/* SB Address Window Mid (b23:b16) */
#define SBSDIO_FUNC1_SBADDRHIGH		0x1000C	/* SB Address Window High (b31:b24)    */
#define SBSDIO_FUNC1_FRAMECTRL		0x1000D	/* Frame Control (frame term/abort) */
#define SBSDIO_FUNC1_CHIPCLKCSR		0x1000E	/* ChipClockCSR (ALP/HT ctl/status) */
#define SBSDIO_FUNC1_SDIOPULLUP		0x1000F	/* SdioPullUp (on cmd, d0-d2) */
#define SBSDIO_FUNC1_WFRAMEBCLO		0x10019	/* Write Frame Byte Count Low */
#define SBSDIO_FUNC1_WFRAMEBCHI		0x1001A	/* Write Frame Byte Count High */
#define SBSDIO_FUNC1_RFRAMEBCLO		0x1001B	/* Read Frame Byte Count Low */
#define SBSDIO_FUNC1_RFRAMEBCHI		0x1001C	/* Read Frame Byte Count High */

#define SBSDIO_FUNC1_MISC_REG_START	0x10000	/* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT	0x1001C	/* f1 misc register end */

/* function 1 OCP space */
#define SBSDIO_SB_OFT_ADDR_MASK		0x07FFF	/* sb offset addr is <= 15 bits, 32k */
#define SBSDIO_SB_OFT_ADDR_LIMIT	0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG	0x08000	/* with b15, maps to 32-bit SB access */

/* some duplication with sbsdpcmdev.h here */
/* valid bits in SBSDIO_FUNC1_SBADDRxxx regs */
#define SBSDIO_SBADDRLOW_MASK		0x80	/* Valid bits in SBADDRLOW */
#define SBSDIO_SBADDRMID_MASK		0xff	/* Valid bits in SBADDRMID */
#define SBSDIO_SBADDRHIGH_MASK		0xffU	/* Valid bits in SBADDRHIGH */
#define SBSDIO_SBWINDOW_MASK		0xffff8000	/* Address bits from SBADDR regs */

#define SDIOH_READ              0	/* Read request */
#define SDIOH_WRITE             1	/* Write request */

#define SDIOH_DATA_FIX          0	/* Fixed addressing */
#define SDIOH_DATA_INC          1	/* Incremental addressing */

/* internal return code */
#define SUCCESS	0
#define ERROR	1

/* forward declarations */
struct brcmf_sdio_card;

struct brcmf_sdreg {
	int func;
	int offset;
	int value;
};

struct sdioh_info {
	struct osl_info *osh;		/* osh handler */
	bool client_intr_enabled;	/* interrupt connnected flag */
	bool intr_handler_valid;	/* client driver interrupt handler valid */
	void (*intr_handler)(void *);	/* registered interrupt handler */
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

/* Attach and build an interface to the underlying SD host driver.
 *  - Allocates resources (structs, arrays, mem, OS handles, etc) needed by
 *    brcmf_sdcard.
 *  - Returns the sdio card handle and virtual address base for register access.
 *    The returned handle should be used in all subsequent calls, but the bcmsh
 *    implementation may maintain a single "default" handle (e.g. the first or
 *    most recent one) to enable single-instance implementations to pass NULL.
 */
extern struct brcmf_sdio_card*
brcmf_sdcard_attach(void *cfghdl, u32 *regsva, uint irq);

/* Detach - freeup resources allocated in attach */
extern int brcmf_sdcard_detach(struct brcmf_sdio_card *card);

/* Enable/disable SD interrupt */
extern int brcmf_sdcard_intr_enable(struct brcmf_sdio_card *card);
extern int brcmf_sdcard_intr_disable(struct brcmf_sdio_card *card);

/* Register/deregister device interrupt handler. */
extern int
brcmf_sdcard_intr_reg(struct brcmf_sdio_card *card,
		      void (*fn)(void *), void *argh);

extern int brcmf_sdcard_intr_dereg(struct brcmf_sdio_card *card);

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
extern int
brcmf_sdcard_send_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, void *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle);
extern int
brcmf_sdcard_recv_buf(struct brcmf_sdio_card *card, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes, struct sk_buff *pkt,
		      void (*complete)(void *handle, int status,
				       bool sync_waiting),
		      void *handle);

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

/* Returns the "Device ID" of target device on the SDIO bus. */
extern int brcmf_sdcard_query_device(struct brcmf_sdio_card *card);

/* Miscellaneous knob tweaker. */
extern int brcmf_sdcard_iovar_op(struct brcmf_sdio_card *card, const char *name,
				 void *params, int plen, void *arg, int len,
				 bool set);

/* helper functions */

/* callback functions */
struct brcmf_sdioh_driver {
	/* attach to device */
	void *(*attach) (u16 vend_id, u16 dev_id, u16 bus, u16 slot,
			 u16 func, uint bustype, u32 regsva, void *param);
	/* detach from device */
	void (*detach) (void *ch);
};

struct sdioh_info;

/* platform specific/high level functions */
extern int brcmf_sdio_function_init(void);
extern int brcmf_sdio_register(struct brcmf_sdioh_driver *driver);
extern void brcmf_sdio_unregister(void);
extern void brcmf_sdio_function_cleanup(void);
extern int brcmf_sdio_probe(struct device *dev);
extern int brcmf_sdio_remove(struct device *dev);

/* Function to return current window addr */
extern u32 brcmf_sdcard_cur_sbwad(struct brcmf_sdio_card *card);

/* Allocate/init/free per-OS private data */
extern int  brcmf_sdioh_osinit(struct sdioh_info *sd);
extern void brcmf_sdioh_osfree(struct sdioh_info *sd);

/* Core interrupt enable/disable of device interrupts */
extern void brcmf_sdioh_dev_intr_on(struct sdioh_info *sd);
extern void brcmf_sdioh_dev_intr_off(struct sdioh_info *sd);

/* attach, return handler on success, NULL if failed.
 *  The handler shall be provided by all subsequent calls. No local cache
 *  cfghdl points to the starting address of pci device mapped memory
 */
extern struct sdioh_info *brcmf_sdioh_attach(void *cfghdl, uint irq);
extern int brcmf_sdioh_detach(struct sdioh_info *si);

extern int
brcmf_sdioh_interrupt_register(struct sdioh_info *si,
			       void (*sdioh_cb_fn)(void *), void *argh);

extern int brcmf_sdioh_interrupt_deregister(struct sdioh_info *si);

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

/* handle iovars */
extern int brcmf_sdioh_iovar_op(struct sdioh_info *si, const char *name,
			  void *params, int plen, void *arg, int len, bool set);

/* Issue abort to the specified function and clear controller as needed */
extern int brcmf_sdioh_abort(struct sdioh_info *si, uint fnc);

/* Watchdog timer interface for pm ops */
extern void brcmf_sdio_wdtmr_enable(bool enable);

extern uint sd_msglevel;	/* Debug message level */

extern struct brcmf_sdmmc_instance *gInstance;

#endif				/* _BRCM_SDH_H_ */

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

/* as of sdiod rev 0, supports 3 functions */
#define SBSDIO_NUM_FUNCTION		3

/* function 0 vendor specific CCCR registers */
#define SDIO_CCCR_BRCM_SEPINT		0xf2

#define  SDIO_SEPINT_MASK		0x01
#define  SDIO_SEPINT_OE			0x02
#define  SDIO_SEPINT_ACT_HI		0x04

/* function 1 miscellaneous registers */

/* sprom command and status */
#define SBSDIO_SPROM_CS			0x10000
/* sprom info register */
#define SBSDIO_SPROM_INFO		0x10001
/* sprom indirect access data byte 0 */
#define SBSDIO_SPROM_DATA_LOW		0x10002
/* sprom indirect access data byte 1 */
#define SBSDIO_SPROM_DATA_HIGH		0x10003
/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_LOW		0x10004
/* sprom indirect access addr byte 0 */
#define SBSDIO_SPROM_ADDR_HIGH		0x10005
/* xtal_pu (gpio) output */
#define SBSDIO_CHIP_CTRL_DATA		0x10006
/* xtal_pu (gpio) enable */
#define SBSDIO_CHIP_CTRL_EN		0x10007
/* rev < 7, watermark for sdio device */
#define SBSDIO_WATERMARK		0x10008
/* control busy signal generation */
#define SBSDIO_DEVICE_CTL		0x10009

/* SB Address Window Low (b15) */
#define SBSDIO_FUNC1_SBADDRLOW		0x1000A
/* SB Address Window Mid (b23:b16) */
#define SBSDIO_FUNC1_SBADDRMID		0x1000B
/* SB Address Window High (b31:b24)    */
#define SBSDIO_FUNC1_SBADDRHIGH		0x1000C
/* Frame Control (frame term/abort) */
#define SBSDIO_FUNC1_FRAMECTRL		0x1000D
/* ChipClockCSR (ALP/HT ctl/status) */
#define SBSDIO_FUNC1_CHIPCLKCSR		0x1000E
/* SdioPullUp (on cmd, d0-d2) */
#define SBSDIO_FUNC1_SDIOPULLUP		0x1000F
/* Write Frame Byte Count Low */
#define SBSDIO_FUNC1_WFRAMEBCLO		0x10019
/* Write Frame Byte Count High */
#define SBSDIO_FUNC1_WFRAMEBCHI		0x1001A
/* Read Frame Byte Count Low */
#define SBSDIO_FUNC1_RFRAMEBCLO		0x1001B
/* Read Frame Byte Count High */
#define SBSDIO_FUNC1_RFRAMEBCHI		0x1001C

#define SBSDIO_FUNC1_MISC_REG_START	0x10000	/* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT	0x1001C	/* f1 misc register end */

/* function 1 OCP space */

/* sb offset addr is <= 15 bits, 32k */
#define SBSDIO_SB_OFT_ADDR_MASK		0x07FFF
#define SBSDIO_SB_OFT_ADDR_LIMIT	0x08000
/* with b15, maps to 32-bit SB access */
#define SBSDIO_SB_ACCESS_2_4B_FLAG	0x08000

/* valid bits in SBSDIO_FUNC1_SBADDRxxx regs */

#define SBSDIO_SBADDRLOW_MASK		0x80	/* Valid bits in SBADDRLOW */
#define SBSDIO_SBADDRMID_MASK		0xff	/* Valid bits in SBADDRMID */
#define SBSDIO_SBADDRHIGH_MASK		0xffU	/* Valid bits in SBADDRHIGH */
/* Address bits from SBADDR regs */
#define SBSDIO_SBWINDOW_MASK		0xffff8000

#define SDIOH_READ              0	/* Read request */
#define SDIOH_WRITE             1	/* Write request */

#define SDIOH_DATA_FIX          0	/* Fixed addressing */
#define SDIOH_DATA_INC          1	/* Incremental addressing */

/* internal return code */
#define SUCCESS	0
#define ERROR	1

/* Packet alignment for most efficient SDIO (can change based on platform) */
#define BRCMF_SDALIGN	(1 << 6)

/* watchdog polling interval in ms */
#define BRCMF_WD_POLL_MS	10

struct brcmf_sdreg {
	int func;
	int offset;
	int value;
};

struct brcmf_sdio;

struct brcmf_sdio_dev {
	struct sdio_func *func[SDIO_MAX_FUNCS];
	u8 num_funcs;			/* Supported funcs on client */
	u32 func_cis_ptr[SDIOD_MAX_IOFUNCS];
	u32 sbwad;			/* Save backplane window address */
	bool regfail;			/* status of last reg_r/w call */
	void *bus;
	atomic_t suspend;		/* suspend flag */
	wait_queue_head_t request_byte_wait;
	wait_queue_head_t request_word_wait;
	wait_queue_head_t request_chain_wait;
	wait_queue_head_t request_buffer_wait;
	struct device *dev;
	struct brcmf_bus *bus_if;
#ifdef CONFIG_BRCMFMAC_SDIO_OOB
	unsigned int irq;		/* oob interrupt number */
	unsigned long irq_flags;	/* board specific oob flags */
	bool irq_en;			/* irq enable flags */
	spinlock_t irq_en_lock;
	bool irq_wake;			/* irq wake enable flags */
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */
};

/* Register/deregister interrupt handler. */
extern int brcmf_sdio_intr_register(struct brcmf_sdio_dev *sdiodev);
extern int brcmf_sdio_intr_unregister(struct brcmf_sdio_dev *sdiodev);

/* Access SDIO address space (e.g. CCCR) using CMD52 (single-byte interface).
 *   fn:   function number
 *   addr: unmodified SDIO-space address
 *   data: data byte to write
 *   err:  pointer to error code (or NULL)
 */
extern u8 brcmf_sdcard_cfg_read(struct brcmf_sdio_dev *sdiodev, uint func,
				u32 addr, int *err);
extern void brcmf_sdcard_cfg_write(struct brcmf_sdio_dev *sdiodev, uint func,
				   u32 addr, u8 data, int *err);

/* Synchronous access to device (client) core registers via CMD53 to F1.
 *   addr: backplane address (i.e. >= regsva from attach)
 *   size: register width in bytes (2 or 4)
 *   data: data for register write
 */
extern u32
brcmf_sdcard_reg_read(struct brcmf_sdio_dev *sdiodev, u32 addr);

extern u32
brcmf_sdcard_reg_write(struct brcmf_sdio_dev *sdiodev, u32 addr, u32 data);

/* Indicate if last reg read/write failed */
extern bool brcmf_sdcard_regfail(struct brcmf_sdio_dev *sdiodev);

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
brcmf_sdcard_send_pkt(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, struct sk_buff *pkt);
extern int
brcmf_sdcard_send_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes);

extern int
brcmf_sdcard_recv_pkt(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, struct sk_buff *pkt);
extern int
brcmf_sdcard_recv_buf(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
		      uint flags, u8 *buf, uint nbytes);
extern int
brcmf_sdcard_recv_chain(struct brcmf_sdio_dev *sdiodev, u32 addr, uint fn,
			uint flags, struct sk_buff_head *pktq);

/* Flags bits */

/* Four-byte target (backplane) width (vs. two-byte) */
#define SDIO_REQ_4BYTE	0x1
/* Fixed address (FIFO) (vs. incrementing address) */
#define SDIO_REQ_FIXED	0x2
/* Async request (vs. sync request) */
#define SDIO_REQ_ASYNC	0x4

/* Read/write to memory block (F1, no FIFO) via CMD53 (sync only).
 *   rw:       read or write (0/1)
 *   addr:     direct SDIO address
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 * Returns 0 or error code.
 */
extern int brcmf_sdcard_rwdata(struct brcmf_sdio_dev *sdiodev, uint rw,
			       u32 addr, u8 *buf, uint nbytes);

/* Issue an abort to the specified function */
extern int brcmf_sdcard_abort(struct brcmf_sdio_dev *sdiodev, uint fn);

/* platform specific/high level functions */
extern int brcmf_sdio_probe(struct brcmf_sdio_dev *sdiodev);
extern int brcmf_sdio_remove(struct brcmf_sdio_dev *sdiodev);

extern int brcmf_sdcard_set_sbaddr_window(struct brcmf_sdio_dev *sdiodev,
					  u32 address);

/* attach, return handler on success, NULL if failed.
 *  The handler shall be provided by all subsequent calls. No local cache
 *  cfghdl points to the starting address of pci device mapped memory
 */
extern int brcmf_sdioh_attach(struct brcmf_sdio_dev *sdiodev);
extern void brcmf_sdioh_detach(struct brcmf_sdio_dev *sdiodev);

/* read or write one byte using cmd52 */
extern int brcmf_sdioh_request_byte(struct brcmf_sdio_dev *sdiodev, uint rw,
				    uint fnc, uint addr, u8 *byte);

/* read or write 2/4 bytes using cmd53 */
extern int
brcmf_sdioh_request_word(struct brcmf_sdio_dev *sdiodev,
			 uint rw, uint fnc, uint addr,
			 u32 *word, uint nbyte);

/* read or write any buffer using cmd53 */
extern int
brcmf_sdioh_request_buffer(struct brcmf_sdio_dev *sdiodev,
			   uint fix_inc, uint rw, uint fnc_num, u32 addr,
			   struct sk_buff *pkt);
extern int
brcmf_sdioh_request_chain(struct brcmf_sdio_dev *sdiodev, uint fix_inc,
			  uint write, uint func, uint addr,
			  struct sk_buff_head *pktq);

/* Watchdog timer interface for pm ops */
extern void brcmf_sdio_wdtmr_enable(struct brcmf_sdio_dev *sdiodev,
				    bool enable);

extern void *brcmf_sdbrcm_probe(u32 regsva, struct brcmf_sdio_dev *sdiodev);
extern void brcmf_sdbrcm_disconnect(void *ptr);
extern void brcmf_sdbrcm_isr(void *arg);

extern void brcmf_sdbrcm_wd_timer(struct brcmf_sdio *bus, uint wdtick);
#endif				/* _BRCM_SDH_H_ */

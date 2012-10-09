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

#ifndef _BRCMF_DBG_H_
#define _BRCMF_DBG_H_

/* message levels */
#define BRCMF_ERROR_VAL	0x0001
#define BRCMF_TRACE_VAL	0x0002
#define BRCMF_INFO_VAL	0x0004
#define BRCMF_DATA_VAL	0x0008
#define BRCMF_CTL_VAL	0x0010
#define BRCMF_TIMER_VAL	0x0020
#define BRCMF_HDRS_VAL	0x0040
#define BRCMF_BYTES_VAL	0x0080
#define BRCMF_INTR_VAL	0x0100
#define BRCMF_GLOM_VAL	0x0400
#define BRCMF_EVENT_VAL	0x0800
#define BRCMF_BTA_VAL	0x1000
#define BRCMF_ISCAN_VAL 0x2000

#if defined(DEBUG)

#define brcmf_dbg(level, fmt, ...)					\
do {									\
	if (BRCMF_ERROR_VAL == BRCMF_##level##_VAL) {			\
		if (brcmf_msg_level & BRCMF_##level##_VAL) {		\
			if (net_ratelimit())				\
				pr_debug("%s: " fmt,			\
					 __func__, ##__VA_ARGS__);	\
		}							\
	} else {							\
		if (brcmf_msg_level & BRCMF_##level##_VAL) {		\
			pr_debug("%s: " fmt,				\
				 __func__, ##__VA_ARGS__);		\
		}							\
	}								\
} while (0)

#define BRCMF_DATA_ON()		(brcmf_msg_level & BRCMF_DATA_VAL)
#define BRCMF_CTL_ON()		(brcmf_msg_level & BRCMF_CTL_VAL)
#define BRCMF_HDRS_ON()		(brcmf_msg_level & BRCMF_HDRS_VAL)
#define BRCMF_BYTES_ON()	(brcmf_msg_level & BRCMF_BYTES_VAL)
#define BRCMF_GLOM_ON()		(brcmf_msg_level & BRCMF_GLOM_VAL)

#else	/* (defined DEBUG) || (defined DEBUG) */

#define brcmf_dbg(level, fmt, ...) no_printk(fmt, ##__VA_ARGS__)

#define BRCMF_DATA_ON()		0
#define BRCMF_CTL_ON()		0
#define BRCMF_HDRS_ON()		0
#define BRCMF_BYTES_ON()	0
#define BRCMF_GLOM_ON()		0

#endif				/* defined(DEBUG) */

#define brcmf_dbg_hex_dump(test, data, len, fmt, ...)			\
do {									\
	if (test)							\
		brcmu_dbg_hex_dump(data, len, fmt, ##__VA_ARGS__);	\
} while (0)

extern int brcmf_msg_level;

/*
 * hold counter variables used in brcmfmac sdio driver.
 */
struct brcmf_sdio_count {
	uint intrcount;		/* Count of device interrupt callbacks */
	uint lastintrs;		/* Count as of last watchdog timer */
	uint pollcnt;		/* Count of active polls */
	uint regfails;		/* Count of R_REG failures */
	uint tx_sderrs;		/* Count of tx attempts with sd errors */
	uint fcqueued;		/* Tx packets that got queued */
	uint rxrtx;		/* Count of rtx requests (NAK to dongle) */
	uint rx_toolong;	/* Receive frames too long to receive */
	uint rxc_errors;	/* SDIO errors when reading control frames */
	uint rx_hdrfail;	/* SDIO errors on header reads */
	uint rx_badhdr;		/* Bad received headers (roosync?) */
	uint rx_badseq;		/* Mismatched rx sequence number */
	uint fc_rcvd;		/* Number of flow-control events received */
	uint fc_xoff;		/* Number which turned on flow-control */
	uint fc_xon;		/* Number which turned off flow-control */
	uint rxglomfail;	/* Failed deglom attempts */
	uint rxglomframes;	/* Number of glom frames (superframes) */
	uint rxglompkts;	/* Number of packets from glom frames */
	uint f2rxhdrs;		/* Number of header reads */
	uint f2rxdata;		/* Number of frame data reads */
	uint f2txdata;		/* Number of f2 frame writes */
	uint f1regdata;		/* Number of f1 register accesses */
	uint tickcnt;		/* Number of watchdog been schedule */
	ulong tx_ctlerrs;	/* Err of sending ctrl frames */
	ulong tx_ctlpkts;	/* Ctrl frames sent to dongle */
	ulong rx_ctlerrs;	/* Err of processing rx ctrl frames */
	ulong rx_ctlpkts;	/* Ctrl frames processed from dongle */
	ulong rx_readahead_cnt;	/* packets where header read-ahead was used */
};

struct brcmf_pub;
#ifdef DEBUG
void brcmf_debugfs_init(void);
void brcmf_debugfs_exit(void);
int brcmf_debugfs_attach(struct brcmf_pub *drvr);
void brcmf_debugfs_detach(struct brcmf_pub *drvr);
struct dentry *brcmf_debugfs_get_devdir(struct brcmf_pub *drvr);
void brcmf_debugfs_create_sdio_count(struct brcmf_pub *drvr,
				     struct brcmf_sdio_count *sdcnt);
#else
static inline void brcmf_debugfs_init(void)
{
}
static inline void brcmf_debugfs_exit(void)
{
}
static inline int brcmf_debugfs_attach(struct brcmf_pub *drvr)
{
	return 0;
}
static inline void brcmf_debugfs_detach(struct brcmf_pub *drvr)
{
}
#endif

#endif				/* _BRCMF_DBG_H_ */

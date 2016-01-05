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

#ifndef	BRCMFMAC_SDIO_H
#define	BRCMFMAC_SDIO_H

#include <linux/skbuff.h>
#include <linux/firmware.h>
#include "firmware.h"

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

/* mask of register map */
#define REG_F0_REG_MASK		0x7FF
#define REG_F1_MISC_MASK	0x1FFFF

/* as of sdiod rev 0, supports 3 functions */
#define SBSDIO_NUM_FUNCTION		3

/* function 0 vendor specific CCCR registers */
#define SDIO_CCCR_BRCM_CARDCAP			0xf0
#define SDIO_CCCR_BRCM_CARDCAP_CMD14_SUPPORT	0x02
#define SDIO_CCCR_BRCM_CARDCAP_CMD14_EXT	0x04
#define SDIO_CCCR_BRCM_CARDCAP_CMD_NODEC	0x08
#define SDIO_CCCR_BRCM_CARDCTRL		0xf1
#define SDIO_CCCR_BRCM_CARDCTRL_WLANRESET	0x02
#define SDIO_CCCR_BRCM_SEPINT			0xf2

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
/* gpio select */
#define SBSDIO_GPIO_SELECT		0x10005
/* gpio output */
#define SBSDIO_GPIO_OUT			0x10006
/* gpio enable */
#define SBSDIO_GPIO_EN			0x10007
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
/* MesBusyCtl (rev 11) */
#define SBSDIO_FUNC1_MESBUSYCTRL	0x1001D
/* Sdio Core Rev 12 */
#define SBSDIO_FUNC1_WAKEUPCTRL		0x1001E
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_MASK		0x1
#define SBSDIO_FUNC1_WCTRL_ALPWAIT_SHIFT	0
#define SBSDIO_FUNC1_WCTRL_HTWAIT_MASK		0x2
#define SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT		1
#define SBSDIO_FUNC1_SLEEPCSR		0x1001F
#define SBSDIO_FUNC1_SLEEPCSR_KSO_MASK		0x1
#define SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT		0
#define SBSDIO_FUNC1_SLEEPCSR_KSO_EN		1
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK	0x2
#define SBSDIO_FUNC1_SLEEPCSR_DEVON_SHIFT	1

#define SBSDIO_FUNC1_MISC_REG_START	0x10000	/* f1 misc register start */
#define SBSDIO_FUNC1_MISC_REG_LIMIT	0x1001F	/* f1 misc register end */

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

/**
 * enum brcmf_sdiod_state - the state of the bus.
 *
 * @BRCMF_SDIOD_DOWN: Device can be accessed, no DPC.
 * @BRCMF_SDIOD_DATA: Ready for data transfers, DPC enabled.
 * @BRCMF_SDIOD_NOMEDIUM: No medium access to dongle possible.
 */
enum brcmf_sdiod_state {
	BRCMF_SDIOD_DOWN,
	BRCMF_SDIOD_DATA,
	BRCMF_SDIOD_NOMEDIUM
};

struct brcmf_sdreg {
	int func;
	int offset;
	int value;
};

struct brcmf_sdio;
struct brcmf_sdiod_freezer;

struct brcmf_sdio_dev {
	struct sdio_func *func[SDIO_MAX_FUNCS];
	u8 num_funcs;			/* Supported funcs on client */
	u32 sbwad;			/* Save backplane window address */
	struct brcmf_sdio *bus;
	struct device *dev;
	struct brcmf_bus *bus_if;
	struct brcmfmac_sdio_platform_data *pdata;
	bool oob_irq_requested;
	bool irq_en;			/* irq enable flags */
	spinlock_t irq_en_lock;
	bool irq_wake;			/* irq wake enable flags */
	bool sg_support;
	uint max_request_size;
	ushort max_segment_count;
	uint max_segment_size;
	uint txglomsz;
	struct sg_table sgtable;
	char fw_name[BRCMF_FW_NAME_LEN];
	char nvram_name[BRCMF_FW_NAME_LEN];
	bool wowl_enabled;
	enum brcmf_sdiod_state state;
	struct brcmf_sdiod_freezer *freezer;
};

/* sdio core registers */
struct sdpcmd_regs {
	u32 corecontrol;		/* 0x00, rev8 */
	u32 corestatus;			/* rev8 */
	u32 PAD[1];
	u32 biststatus;			/* rev8 */

	/* PCMCIA access */
	u16 pcmciamesportaladdr;	/* 0x010, rev8 */
	u16 PAD[1];
	u16 pcmciamesportalmask;	/* rev8 */
	u16 PAD[1];
	u16 pcmciawrframebc;		/* rev8 */
	u16 PAD[1];
	u16 pcmciaunderflowtimer;	/* rev8 */
	u16 PAD[1];

	/* interrupt */
	u32 intstatus;			/* 0x020, rev8 */
	u32 hostintmask;		/* rev8 */
	u32 intmask;			/* rev8 */
	u32 sbintstatus;		/* rev8 */
	u32 sbintmask;			/* rev8 */
	u32 funcintmask;		/* rev4 */
	u32 PAD[2];
	u32 tosbmailbox;		/* 0x040, rev8 */
	u32 tohostmailbox;		/* rev8 */
	u32 tosbmailboxdata;		/* rev8 */
	u32 tohostmailboxdata;		/* rev8 */

	/* synchronized access to registers in SDIO clock domain */
	u32 sdioaccess;			/* 0x050, rev8 */
	u32 PAD[3];

	/* PCMCIA frame control */
	u8 pcmciaframectrl;		/* 0x060, rev8 */
	u8 PAD[3];
	u8 pcmciawatermark;		/* rev8 */
	u8 PAD[155];

	/* interrupt batching control */
	u32 intrcvlazy;			/* 0x100, rev8 */
	u32 PAD[3];

	/* counters */
	u32 cmd52rd;			/* 0x110, rev8 */
	u32 cmd52wr;			/* rev8 */
	u32 cmd53rd;			/* rev8 */
	u32 cmd53wr;			/* rev8 */
	u32 abort;			/* rev8 */
	u32 datacrcerror;		/* rev8 */
	u32 rdoutofsync;		/* rev8 */
	u32 wroutofsync;		/* rev8 */
	u32 writebusy;			/* rev8 */
	u32 readwait;			/* rev8 */
	u32 readterm;			/* rev8 */
	u32 writeterm;			/* rev8 */
	u32 PAD[40];
	u32 clockctlstatus;		/* rev8 */
	u32 PAD[7];

	u32 PAD[128];			/* DMA engines */

	/* SDIO/PCMCIA CIS region */
	char cis[512];			/* 0x400-0x5ff, rev6 */

	/* PCMCIA function control registers */
	char pcmciafcr[256];		/* 0x600-6ff, rev6 */
	u16 PAD[55];

	/* PCMCIA backplane access */
	u16 backplanecsr;		/* 0x76E, rev6 */
	u16 backplaneaddr0;		/* rev6 */
	u16 backplaneaddr1;		/* rev6 */
	u16 backplaneaddr2;		/* rev6 */
	u16 backplaneaddr3;		/* rev6 */
	u16 backplanedata0;		/* rev6 */
	u16 backplanedata1;		/* rev6 */
	u16 backplanedata2;		/* rev6 */
	u16 backplanedata3;		/* rev6 */
	u16 PAD[31];

	/* sprom "size" & "blank" info */
	u16 spromstatus;		/* 0x7BE, rev2 */
	u32 PAD[464];

	u16 PAD[0x80];
};

/* Register/deregister interrupt handler. */
int brcmf_sdiod_intr_register(struct brcmf_sdio_dev *sdiodev);
int brcmf_sdiod_intr_unregister(struct brcmf_sdio_dev *sdiodev);

/* sdio device register access interface */
u8 brcmf_sdiod_regrb(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret);
u32 brcmf_sdiod_regrl(struct brcmf_sdio_dev *sdiodev, u32 addr, int *ret);
void brcmf_sdiod_regwb(struct brcmf_sdio_dev *sdiodev, u32 addr, u8 data,
		       int *ret);
void brcmf_sdiod_regwl(struct brcmf_sdio_dev *sdiodev, u32 addr, u32 data,
		       int *ret);

/* Buffer transfer to/from device (client) core via cmd53.
 *   fn:       function number
 *   flags:    backplane width, address increment, sync/async
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 *   pkt:      pointer to packet associated with buf (if any)
 *   complete: callback function for command completion (async only)
 *   handle:   handle for completion callback (first arg in callback)
 * Returns 0 or error code.
 * NOTE: Async operation is not currently supported.
 */
int brcmf_sdiod_send_pkt(struct brcmf_sdio_dev *sdiodev,
			 struct sk_buff_head *pktq);
int brcmf_sdiod_send_buf(struct brcmf_sdio_dev *sdiodev, u8 *buf, uint nbytes);

int brcmf_sdiod_recv_pkt(struct brcmf_sdio_dev *sdiodev, struct sk_buff *pkt);
int brcmf_sdiod_recv_buf(struct brcmf_sdio_dev *sdiodev, u8 *buf, uint nbytes);
int brcmf_sdiod_recv_chain(struct brcmf_sdio_dev *sdiodev,
			   struct sk_buff_head *pktq, uint totlen);

/* Flags bits */

/* Four-byte target (backplane) width (vs. two-byte) */
#define SDIO_REQ_4BYTE	0x1
/* Fixed address (FIFO) (vs. incrementing address) */
#define SDIO_REQ_FIXED	0x2

/* Read/write to memory block (F1, no FIFO) via CMD53 (sync only).
 *   rw:       read or write (0/1)
 *   addr:     direct SDIO address
 *   buf:      pointer to memory data buffer
 *   nbytes:   number of bytes to transfer to/from buf
 * Returns 0 or error code.
 */
int brcmf_sdiod_ramrw(struct brcmf_sdio_dev *sdiodev, bool write, u32 address,
		      u8 *data, uint size);

/* Issue an abort to the specified function */
int brcmf_sdiod_abort(struct brcmf_sdio_dev *sdiodev, uint fn);
void brcmf_sdiod_change_state(struct brcmf_sdio_dev *sdiodev,
			      enum brcmf_sdiod_state state);
#ifdef CONFIG_PM_SLEEP
bool brcmf_sdiod_freezing(struct brcmf_sdio_dev *sdiodev);
void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev *sdiodev);
void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev *sdiodev);
void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev *sdiodev);
#else
static inline bool brcmf_sdiod_freezing(struct brcmf_sdio_dev *sdiodev)
{
	return false;
}
static inline void brcmf_sdiod_try_freeze(struct brcmf_sdio_dev *sdiodev)
{
}
static inline void brcmf_sdiod_freezer_count(struct brcmf_sdio_dev *sdiodev)
{
}
static inline void brcmf_sdiod_freezer_uncount(struct brcmf_sdio_dev *sdiodev)
{
}
#endif /* CONFIG_PM_SLEEP */

struct brcmf_sdio *brcmf_sdio_probe(struct brcmf_sdio_dev *sdiodev);
void brcmf_sdio_remove(struct brcmf_sdio *bus);
void brcmf_sdio_isr(struct brcmf_sdio *bus);

void brcmf_sdio_wd_timer(struct brcmf_sdio *bus, bool active);
void brcmf_sdio_wowl_config(struct device *dev, bool enabled);
int brcmf_sdio_sleep(struct brcmf_sdio *bus, bool sleep);
void brcmf_sdio_trigger_dpc(struct brcmf_sdio *bus);

#endif /* BRCMFMAC_SDIO_H */

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

#ifndef	_bcmsdpcm_h_
#define	_bcmsdpcm_h_

/*
 * Software allocation of To SB Mailbox resources
 */

/* intstatus bits */
#define I_SMB_NAK	I_SMB_SW0	/* To SB Mailbox Frame NAK */
#define I_SMB_INT_ACK	I_SMB_SW1	/* To SB Mailbox Host Interrupt ACK */
#define I_SMB_USE_OOB	I_SMB_SW2	/* To SB Mailbox Use OOB Wakeup */
#define I_SMB_DEV_INT	I_SMB_SW3	/* To SB Mailbox Miscellaneous Interrupt */

#define I_TOSBMAIL      (I_SMB_NAK | I_SMB_INT_ACK | I_SMB_USE_OOB | I_SMB_DEV_INT)

/* tosbmailbox bits corresponding to intstatus bits */
#define SMB_NAK		(1 << 0)	/* To SB Mailbox Frame NAK */
#define SMB_INT_ACK	(1 << 1)	/* To SB Mailbox Host Interrupt ACK */
#define SMB_USE_OOB	(1 << 2)	/* To SB Mailbox Use OOB Wakeup */
#define SMB_DEV_INT	(1 << 3)	/* To SB Mailbox Miscellaneous Interrupt */
#define SMB_MASK	0x0000000f	/* To SB Mailbox Mask */

/* tosbmailboxdata */
#define SMB_DATA_VERSION_MASK	0x00ff0000	/* host protocol version (sent with F2 enable) */
#define SMB_DATA_VERSION_SHIFT	16	/* host protocol version (sent with F2 enable) */

/*
 * Software allocation of To Host Mailbox resources
 */

/* intstatus bits */
#define I_HMB_FC_STATE	I_HMB_SW0	/* To Host Mailbox Flow Control State */
#define I_HMB_FC_CHANGE	I_HMB_SW1	/* To Host Mailbox Flow Control State Changed */
#define I_HMB_FRAME_IND	I_HMB_SW2	/* To Host Mailbox Frame Indication */
#define I_HMB_HOST_INT	I_HMB_SW3	/* To Host Mailbox Miscellaneous Interrupt */

#define I_TOHOSTMAIL    (I_HMB_FC_CHANGE | I_HMB_FRAME_IND | I_HMB_HOST_INT)

/* tohostmailbox bits corresponding to intstatus bits */
#define HMB_FC_ON	(1 << 0)	/* To Host Mailbox Flow Control State */
#define HMB_FC_CHANGE	(1 << 1)	/* To Host Mailbox Flow Control State Changed */
#define HMB_FRAME_IND	(1 << 2)	/* To Host Mailbox Frame Indication */
#define HMB_HOST_INT	(1 << 3)	/* To Host Mailbox Miscellaneous Interrupt */
#define HMB_MASK	0x0000000f	/* To Host Mailbox Mask */

/* tohostmailboxdata */
#define HMB_DATA_NAKHANDLED	1	/* we're ready to retransmit NAK'd frame to host */
#define HMB_DATA_DEVREADY	2	/* we're ready to to talk to host after enable */
#define HMB_DATA_FC		4	/* per prio flowcontrol update flag to host */
#define HMB_DATA_FWREADY	8	/* firmware is ready for protocol activity */

#define HMB_DATA_FCDATA_MASK	0xff000000	/* per prio flowcontrol data */
#define HMB_DATA_FCDATA_SHIFT	24	/* per prio flowcontrol data */

#define HMB_DATA_VERSION_MASK	0x00ff0000	/* device protocol version (with devready) */
#define HMB_DATA_VERSION_SHIFT	16	/* device protocol version (with devready) */

/*
 * Software-defined protocol header
 */

/* Current protocol version */
#define SDPCM_PROT_VERSION	4

/* SW frame header */
#define SDPCM_SEQUENCE_MASK		0x000000ff	/* Sequence Number Mask */
#define SDPCM_PACKET_SEQUENCE(p) (((u8 *)p)[0] & 0xff)	/* p starts w/SW Header */

#define SDPCM_CHANNEL_MASK		0x00000f00	/* Channel Number Mask */
#define SDPCM_CHANNEL_SHIFT		8	/* Channel Number Shift */
#define SDPCM_PACKET_CHANNEL(p) (((u8 *)p)[1] & 0x0f)	/* p starts w/SW Header */

#define SDPCM_FLAGS_MASK		0x0000f000	/* Mask of flag bits */
#define SDPCM_FLAGS_SHIFT		12	/* Flag bits shift */
#define SDPCM_PACKET_FLAGS(p) ((((u8 *)p)[1] & 0xf0) >> 4)	/* p starts w/SW Header */

/* Next Read Len: lookahead length of next frame, in 16-byte units (rounded up) */
#define SDPCM_NEXTLEN_MASK		0x00ff0000	/* Next Read Len Mask */
#define SDPCM_NEXTLEN_SHIFT		16	/* Next Read Len Shift */
#define SDPCM_NEXTLEN_VALUE(p) ((((u8 *)p)[2] & 0xff) << 4)	/* p starts w/SW Header */
#define SDPCM_NEXTLEN_OFFSET		2

/* Data Offset from SOF (HW Tag, SW Tag, Pad) */
#define SDPCM_DOFFSET_OFFSET		3	/* Data Offset */
#define SDPCM_DOFFSET_VALUE(p) 		(((u8 *)p)[SDPCM_DOFFSET_OFFSET] & 0xff)
#define SDPCM_DOFFSET_MASK		0xff000000
#define SDPCM_DOFFSET_SHIFT		24

#define SDPCM_FCMASK_OFFSET		4	/* Flow control */
#define SDPCM_FCMASK_VALUE(p)		(((u8 *)p)[SDPCM_FCMASK_OFFSET] & 0xff)
#define SDPCM_WINDOW_OFFSET		5	/* Credit based fc */
#define SDPCM_WINDOW_VALUE(p)		(((u8 *)p)[SDPCM_WINDOW_OFFSET] & 0xff)
#define SDPCM_VERSION_OFFSET		6	/* Version # */
#define SDPCM_VERSION_VALUE(p)		(((u8 *)p)[SDPCM_VERSION_OFFSET] & 0xff)
#define SDPCM_UNUSED_OFFSET		7	/* Spare */
#define SDPCM_UNUSED_VALUE(p)		(((u8 *)p)[SDPCM_UNUSED_OFFSET] & 0xff)

#define SDPCM_SWHEADER_LEN	8	/* SW header is 64 bits */

/* logical channel numbers */
#define SDPCM_CONTROL_CHANNEL	0	/* Control Request/Response Channel Id */
#define SDPCM_EVENT_CHANNEL	1	/* Asyc Event Indication Channel Id */
#define SDPCM_DATA_CHANNEL	2	/* Data Xmit/Recv Channel Id */
#define SDPCM_GLOM_CHANNEL	3	/* For coalesced packets (superframes) */
#define SDPCM_TEST_CHANNEL	15	/* Reserved for test/debug packets */
#define SDPCM_MAX_CHANNEL	15

#define SDPCM_SEQUENCE_WRAP	256	/* wrap-around val for eight-bit frame seq number */

#define SDPCM_FLAG_RESVD0	0x01
#define SDPCM_FLAG_RESVD1	0x02
#define SDPCM_FLAG_GSPI_TXENAB	0x04
#define SDPCM_FLAG_GLOMDESC	0x08	/* Superframe descriptor mask */

/* For GLOM_CHANNEL frames, use a flag to indicate descriptor frame */
#define SDPCM_GLOMDESC_FLAG	(SDPCM_FLAG_GLOMDESC << SDPCM_FLAGS_SHIFT)

#define SDPCM_GLOMDESC(p)	(((u8 *)p)[1] & 0x80)

/* For TEST_CHANNEL packets, define another 4-byte header */
#define SDPCM_TEST_HDRLEN	4	/* Generally: Cmd(1), Ext(1), Len(2);
					 * Semantics of Ext byte depend on command.
					 * Len is current or requested frame length, not
					 * including test header; sent little-endian.
					 */
#define SDPCM_TEST_DISCARD	0x01	/* Receiver discards. Ext is a pattern id. */
#define SDPCM_TEST_ECHOREQ	0x02	/* Echo request. Ext is a pattern id. */
#define SDPCM_TEST_ECHORSP	0x03	/* Echo response. Ext is a pattern id. */
#define SDPCM_TEST_BURST	0x04	/* Receiver to send a burst. Ext is a frame count */
#define SDPCM_TEST_SEND		0x05	/* Receiver sets send mode. Ext is boolean on/off */

/* Handy macro for filling in datagen packets with a pattern */
#define SDPCM_TEST_FILL(byteno, id)	((u8)(id + byteno))

/*
 * Software counters (first part matches hardware counters)
 */

typedef volatile struct {
	uint32 cmd52rd;		/* Cmd52RdCount, SDIO: cmd52 reads */
	uint32 cmd52wr;		/* Cmd52WrCount, SDIO: cmd52 writes */
	uint32 cmd53rd;		/* Cmd53RdCount, SDIO: cmd53 reads */
	uint32 cmd53wr;		/* Cmd53WrCount, SDIO: cmd53 writes */
	uint32 abort;		/* AbortCount, SDIO: aborts */
	uint32 datacrcerror;	/* DataCrcErrorCount, SDIO: frames w/CRC error */
	uint32 rdoutofsync;	/* RdOutOfSyncCount, SDIO/PCMCIA: Rd Frm out of sync */
	uint32 wroutofsync;	/* RdOutOfSyncCount, SDIO/PCMCIA: Wr Frm out of sync */
	uint32 writebusy;	/* WriteBusyCount, SDIO: device asserted "busy" */
	uint32 readwait;	/* ReadWaitCount, SDIO: no data ready for a read cmd */
	uint32 readterm;	/* ReadTermCount, SDIO: read frame termination cmds */
	uint32 writeterm;	/* WriteTermCount, SDIO: write frames termination cmds */
	uint32 rxdescuflo;	/* receive descriptor underflows */
	uint32 rxfifooflo;	/* receive fifo overflows */
	uint32 txfifouflo;	/* transmit fifo underflows */
	uint32 runt;		/* runt (too short) frames recv'd from bus */
	uint32 badlen;		/* frame's rxh len does not match its hw tag len */
	uint32 badcksum;	/* frame's hw tag chksum doesn't agree with len value */
	uint32 seqbreak;	/* break in sequence # space from one rx frame to the next */
	uint32 rxfcrc;		/* frame rx header indicates crc error */
	uint32 rxfwoos;		/* frame rx header indicates write out of sync */
	uint32 rxfwft;		/* frame rx header indicates write frame termination */
	uint32 rxfabort;	/* frame rx header indicates frame aborted */
	uint32 woosint;		/* write out of sync interrupt */
	uint32 roosint;		/* read out of sync interrupt */
	uint32 rftermint;	/* read frame terminate interrupt */
	uint32 wftermint;	/* write frame terminate interrupt */
} sdpcmd_cnt_t;

/*
 * Register Access Macros
 */

#define SDIODREV_IS(var, val)	((var) == (val))
#define SDIODREV_GE(var, val)	((var) >= (val))
#define SDIODREV_GT(var, val)	((var) > (val))
#define SDIODREV_LT(var, val)	((var) < (val))
#define SDIODREV_LE(var, val)	((var) <= (val))

#define SDIODDMAREG32(h, dir, chnl) \
	((dir) == DMA_TX ? \
	 (void *)(uintptr)&((h)->regs->dma.sdiod32.dma32regs[chnl].xmt) : \
	 (void *)(uintptr)&((h)->regs->dma.sdiod32.dma32regs[chnl].rcv))

#define SDIODDMAREG64(h, dir, chnl) \
	((dir) == DMA_TX ? \
	 (void *)(uintptr)&((h)->regs->dma.sdiod64.dma64regs[chnl].xmt) : \
	 (void *)(uintptr)&((h)->regs->dma.sdiod64.dma64regs[chnl].rcv))

#define SDIODDMAREG(h, dir, chnl) \
	(SDIODREV_LT((h)->corerev, 1) ? \
	 SDIODDMAREG32((h), (dir), (chnl)) : \
	 SDIODDMAREG64((h), (dir), (chnl)))

#define PCMDDMAREG(h, dir, chnl) \
	((dir) == DMA_TX ? \
	 (void *)(uintptr)&((h)->regs->dma.pcm32.dmaregs.xmt) : \
	 (void *)(uintptr)&((h)->regs->dma.pcm32.dmaregs.rcv))

#define SDPCMDMAREG(h, dir, chnl, coreid) \
	((coreid) == SDIOD_CORE_ID ? \
	 SDIODDMAREG(h, dir, chnl) : \
	 PCMDDMAREG(h, dir, chnl))

#define SDIODFIFOREG(h, corerev) \
	(SDIODREV_LT((corerev), 1) ? \
	 ((dma32diag_t *)(uintptr)&((h)->regs->dma.sdiod32.dmafifo)) : \
	 ((dma32diag_t *)(uintptr)&((h)->regs->dma.sdiod64.dmafifo)))

#define PCMDFIFOREG(h) \
	((dma32diag_t *)(uintptr)&((h)->regs->dma.pcm32.dmafifo))

#define SDPCMFIFOREG(h, coreid, corerev) \
	((coreid) == SDIOD_CORE_ID ? \
	 SDIODFIFOREG(h, corerev) : \
	 PCMDFIFOREG(h))

/*
 * Shared structure between dongle and the host.
 * The structure contains pointers to trap or assert information.
 */
#define SDPCM_SHARED_VERSION       0x0001
#define SDPCM_SHARED_VERSION_MASK  0x00FF
#define SDPCM_SHARED_ASSERT_BUILT  0x0100
#define SDPCM_SHARED_ASSERT        0x0200
#define SDPCM_SHARED_TRAP          0x0400

typedef struct {
	uint32 flags;
	uint32 trap_addr;
	uint32 assert_exp_addr;
	uint32 assert_file_addr;
	uint32 assert_line;
	uint32 console_addr;	/* Address of hndrte_cons_t */
	uint32 msgtrace_addr;
} sdpcm_shared_t;

extern sdpcm_shared_t sdpcm_shared;

#endif				/* _bcmsdpcm_h_ */

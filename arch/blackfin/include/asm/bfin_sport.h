/*
 * bfin_sport.h - interface to Blackfin SPORTs
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_SPORT_H__
#define __BFIN_SPORT_H__

/* Sport mode: it can be set to TDM, i2s or others */
#define NORM_MODE	0x0
#define TDM_MODE	0x1
#define I2S_MODE	0x2

/* Data format, normal, a-law or u-law */
#define NORM_FORMAT	0x0
#define ALAW_FORMAT	0x2
#define ULAW_FORMAT	0x3

/* Function driver which use sport must initialize the structure */
struct sport_config {
	/* TDM (multichannels), I2S or other mode */
	unsigned int mode:3;

	/* if TDM mode is selected, channels must be set */
	int channels;	/* Must be in 8 units */
	unsigned int frame_delay:4;	/* Delay between frame sync pulse and first bit */

	/* I2S mode */
	unsigned int right_first:1;	/* Right stereo channel first */

	/* In mormal mode, the following item need to be set */
	unsigned int lsb_first:1;	/* order of transmit or receive data */
	unsigned int fsync:1;	/* Frame sync required */
	unsigned int data_indep:1;	/* data independent frame sync generated */
	unsigned int act_low:1;	/* Active low TFS */
	unsigned int late_fsync:1;	/* Late frame sync */
	unsigned int tckfe:1;
	unsigned int sec_en:1;	/* Secondary side enabled */

	/* Choose clock source */
	unsigned int int_clk:1;	/* Internal or external clock */

	/* If external clock is used, the following fields are ignored */
	int serial_clk;
	int fsync_clk;

	unsigned int data_format:2;	/* Normal, u-law or a-law */

	int word_len;		/* How length of the word in bits, 3-32 bits */
	int dma_enabled;
};

/* Userspace interface */
#define SPORT_IOC_MAGIC		'P'
#define SPORT_IOC_CONFIG	_IOWR('P', 0x01, struct sport_config)

#ifdef __KERNEL__

#include <linux/types.h>

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m
struct sport_register {
	__BFP(tcr1);
	__BFP(tcr2);
	__BFP(tclkdiv);
	__BFP(tfsdiv);
	union {
		u32 tx32;
		u16 tx16;
	};
	u32 __pad_tx;
	union {
		u32 rx32;	/* use the anomaly wrapper below */
		u16 rx16;
	};
	u32 __pad_rx;
	__BFP(rcr1);
	__BFP(rcr2);
	__BFP(rclkdiv);
	__BFP(rfsdiv);
	__BFP(stat);
	__BFP(chnl);
	__BFP(mcmc1);
	__BFP(mcmc2);
	u32 mtcs0;
	u32 mtcs1;
	u32 mtcs2;
	u32 mtcs3;
	u32 mrcs0;
	u32 mrcs1;
	u32 mrcs2;
	u32 mrcs3;
};
#undef __BFP

#define bfin_read_sport_rx32(base) \
({ \
	struct sport_register *__mmrs = (void *)base; \
	u32 __ret; \
	unsigned long flags; \
	if (ANOMALY_05000473) \
		local_irq_save(flags); \
	__ret = __mmrs->rx32; \
	if (ANOMALY_05000473) \
		local_irq_restore(flags); \
	__ret; \
})

#endif

/* SPORT_TCR1 Masks */
#define TSPEN		0x0001	/* TX enable */
#define ITCLK		0x0002	/* Internal TX Clock Select */
#define TDTYPE		0x000C	/* TX Data Formatting Select */
#define DTYPE_NORM	0x0000	/* Data Format Normal */
#define DTYPE_ULAW	0x0008	/* Compand Using u-Law */
#define DTYPE_ALAW	0x000C	/* Compand Using A-Law */
#define TLSBIT		0x0010	/* TX Bit Order */
#define ITFS		0x0200	/* Internal TX Frame Sync Select */
#define TFSR		0x0400	/* TX Frame Sync Required Select */
#define DITFS		0x0800	/* Data Independent TX Frame Sync Select */
#define LTFS		0x1000	/* Low TX Frame Sync Select */
#define LATFS		0x2000	/* Late TX Frame Sync Select */
#define TCKFE		0x4000	/* TX Clock Falling Edge Select */

/* SPORT_TCR2 Masks */
#define SLEN		0x001F	/* SPORT TX Word Length (2 - 31) */
#define DP_SLEN(x)	BFIN_DEPOSIT(SLEN, x)
#define EX_SLEN(x)	BFIN_EXTRACT(SLEN, x)
#define TXSE		0x0100	/* TX Secondary Enable */
#define TSFSE		0x0200	/* TX Stereo Frame Sync Enable */
#define TRFST		0x0400	/* TX Right-First Data Order */

/* SPORT_RCR1 Masks */
#define RSPEN		0x0001	/* RX enable */
#define IRCLK		0x0002	/* Internal RX Clock Select */
#define RDTYPE		0x000C	/* RX Data Formatting Select */
/* DTYPE_* defined above */
#define RLSBIT		0x0010	/* RX Bit Order */
#define IRFS		0x0200	/* Internal RX Frame Sync Select */
#define RFSR		0x0400	/* RX Frame Sync Required Select */
#define LRFS		0x1000	/* Low RX Frame Sync Select */
#define LARFS		0x2000	/* Late RX Frame Sync Select */
#define RCKFE		0x4000	/* RX Clock Falling Edge Select */

/* SPORT_RCR2 Masks */
/* SLEN defined above */
#define RXSE		0x0100	/* RX Secondary Enable */
#define RSFSE		0x0200	/* RX Stereo Frame Sync Enable */
#define RRFST		0x0400	/* Right-First Data Order */

/* SPORT_STAT Masks */
#define RXNE		0x0001	/* RX FIFO Not Empty Status */
#define RUVF		0x0002	/* RX Underflow Status */
#define ROVF		0x0004	/* RX Overflow Status */
#define TXF		0x0008	/* TX FIFO Full Status */
#define TUVF		0x0010	/* TX Underflow Status */
#define TOVF		0x0020	/* TX Overflow Status */
#define TXHRE		0x0040	/* TX Hold Register Empty */

/* SPORT_MCMC1 Masks */
#define SP_WOFF		0x03FF	/* Multichannel Window Offset Field */
#define DP_SP_WOFF(x)	BFIN_DEPOSIT(SP_WOFF, x)
#define EX_SP_WOFF(x)	BFIN_EXTRACT(SP_WOFF, x)
#define SP_WSIZE	0xF000	/* Multichannel Window Size Field */
#define DP_SP_WSIZE(x)	BFIN_DEPOSIT(SP_WSIZE, x)
#define EX_SP_WSIZE(x)	BFIN_EXTRACT(SP_WSIZE, x)

/* SPORT_MCMC2 Masks */
#define MCCRM		0x0003	/* Multichannel Clock Recovery Mode */
#define REC_BYPASS	0x0000	/* Bypass Mode (No Clock Recovery) */
#define REC_2FROM4	0x0002	/* Recover 2 MHz Clock from 4 MHz Clock */
#define REC_8FROM16	0x0003	/* Recover 8 MHz Clock from 16 MHz Clock */
#define MCDTXPE		0x0004	/* Multichannel DMA Transmit Packing */
#define MCDRXPE		0x0008	/* Multichannel DMA Receive Packing */
#define MCMEN		0x0010	/* Multichannel Frame Mode Enable */
#define FSDR		0x0080	/* Multichannel Frame Sync to Data Relationship */
#define MFD		0xF000	/* Multichannel Frame Delay */
#define DP_MFD(x)	BFIN_DEPOSIT(MFD, x)
#define EX_MFD(x)	BFIN_EXTRACT(MFD, x)

#endif

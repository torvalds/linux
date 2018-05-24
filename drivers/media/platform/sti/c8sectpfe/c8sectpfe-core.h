/* SPDX-License-Identifier: GPL-2.0 */
/*
 * c8sectpfe-core.h - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *   Author:Peter Bennett <peter.bennett@st.com>
 *	    Peter Griffin <peter.griffin@linaro.org>
 *
 */
#ifndef _C8SECTPFE_CORE_H_
#define _C8SECTPFE_CORE_H_

#define C8SECTPFEI_MAXCHANNEL 16
#define C8SECTPFEI_MAXADAPTER 3

#define C8SECTPFE_MAX_TSIN_CHAN 8

struct channel_info {

	int tsin_id;
	bool invert_ts_clk;
	bool serial_not_parallel;
	bool async_not_sync;
	int i2c;
	int dvb_card;

	int rst_gpio;

	struct i2c_adapter  *i2c_adapter;
	struct i2c_adapter  *tuner_i2c;
	struct i2c_adapter  *lnb_i2c;
	struct i2c_client   *i2c_client;
	struct dvb_frontend *frontend;

	struct pinctrl_state *pstate;

	int demux_mapping;
	int active;

	void *back_buffer_start;
	void *back_buffer_aligned;
	dma_addr_t back_buffer_busaddr;

	void *pid_buffer_start;
	void *pid_buffer_aligned;
	dma_addr_t pid_buffer_busaddr;

	unsigned long  fifo;

	struct completion idle_completion;
	struct tasklet_struct tsklet;

	struct c8sectpfei *fei;
	void __iomem *irec;

};

struct c8sectpfe_hw {
	int num_ib;
	int num_mib;
	int num_swts;
	int num_tsout;
	int num_ccsc;
	int num_ram;
	int num_tp;
};

struct c8sectpfei {

	struct device *dev;
	struct pinctrl *pinctrl;

	struct dentry *root;
	struct debugfs_regset32	*regset;
	struct completion fw_ack;
	atomic_t fw_loaded;

	int tsin_count;

	struct c8sectpfe_hw hw_stats;

	struct c8sectpfe *c8sectpfe[C8SECTPFEI_MAXADAPTER];

	int mapping[C8SECTPFEI_MAXCHANNEL];

	struct mutex lock;

	struct timer_list timer;	/* timer interrupts for outputs */

	void __iomem *io;
	void __iomem *sram;

	unsigned long sram_size;

	struct channel_info *channel_data[C8SECTPFE_MAX_TSIN_CHAN];

	struct clk *c8sectpfeclk;
	int nima_rst_gpio;
	int nimb_rst_gpio;

	int idle_irq;
	int error_irq;

	int global_feed_count;
};

/* C8SECTPFE SYS Regs list */

#define SYS_INPUT_ERR_STATUS	0x0
#define SYS_OTHER_ERR_STATUS	0x8
#define SYS_INPUT_ERR_MASK	0x10
#define SYS_OTHER_ERR_MASK	0x18
#define SYS_DMA_ROUTE		0x20
#define SYS_INPUT_CLKEN		0x30
#define IBENABLE_MASK			0x7F

#define SYS_OTHER_CLKEN		0x38
#define TSDMAENABLE			BIT(1)
#define MEMDMAENABLE			BIT(0)

#define SYS_CFG_NUM_IB		0x200
#define SYS_CFG_NUM_MIB		0x204
#define SYS_CFG_NUM_SWTS	0x208
#define SYS_CFG_NUM_TSOUT	0x20C
#define SYS_CFG_NUM_CCSC	0x210
#define SYS_CFG_NUM_RAM		0x214
#define SYS_CFG_NUM_TP		0x218

/* Input Block Regs */

#define C8SECTPFE_INPUTBLK_OFFSET	0x1000
#define C8SECTPFE_CHANNEL_OFFSET(x)	((x*0x40) + C8SECTPFE_INPUTBLK_OFFSET)

#define C8SECTPFE_IB_IP_FMT_CFG(x)      (C8SECTPFE_CHANNEL_OFFSET(x) + 0x00)
#define C8SECTPFE_IGNORE_ERR_AT_SOP     BIT(7)
#define C8SECTPFE_IGNORE_ERR_IN_PKT     BIT(6)
#define C8SECTPFE_IGNORE_ERR_IN_BYTE    BIT(5)
#define C8SECTPFE_INVERT_TSCLK          BIT(4)
#define C8SECTPFE_ALIGN_BYTE_SOP        BIT(3)
#define C8SECTPFE_ASYNC_NOT_SYNC        BIT(2)
#define C8SECTPFE_BYTE_ENDIANNESS_MSB    BIT(1)
#define C8SECTPFE_SERIAL_NOT_PARALLEL   BIT(0)

#define C8SECTPFE_IB_SYNCLCKDRP_CFG(x)   (C8SECTPFE_CHANNEL_OFFSET(x) + 0x04)
#define C8SECTPFE_SYNC(x)                (x & 0xf)
#define C8SECTPFE_DROP(x)                ((x<<4) & 0xf)
#define C8SECTPFE_TOKEN(x)               ((x<<8) & 0xff00)
#define C8SECTPFE_SLDENDIANNESS          BIT(16)

#define C8SECTPFE_IB_TAGBYTES_CFG(x)     (C8SECTPFE_CHANNEL_OFFSET(x) + 0x08)
#define C8SECTPFE_TAG_HEADER(x)          (x << 16)
#define C8SECTPFE_TAG_COUNTER(x)         ((x<<1) & 0x7fff)
#define C8SECTPFE_TAG_ENABLE             BIT(0)

#define C8SECTPFE_IB_PID_SET(x)          (C8SECTPFE_CHANNEL_OFFSET(x) + 0x0C)
#define C8SECTPFE_PID_OFFSET(x)          (x & 0x3f)
#define C8SECTPFE_PID_NUMBITS(x)         ((x << 6) & 0xfff)
#define C8SECTPFE_PID_ENABLE             BIT(31)

#define C8SECTPFE_IB_PKT_LEN(x)          (C8SECTPFE_CHANNEL_OFFSET(x) + 0x10)

#define C8SECTPFE_IB_BUFF_STRT(x)        (C8SECTPFE_CHANNEL_OFFSET(x) + 0x14)
#define C8SECTPFE_IB_BUFF_END(x)         (C8SECTPFE_CHANNEL_OFFSET(x) + 0x18)
#define C8SECTPFE_IB_READ_PNT(x)         (C8SECTPFE_CHANNEL_OFFSET(x) + 0x1C)
#define C8SECTPFE_IB_WRT_PNT(x)          (C8SECTPFE_CHANNEL_OFFSET(x) + 0x20)

#define C8SECTPFE_IB_PRI_THRLD(x)        (C8SECTPFE_CHANNEL_OFFSET(x) + 0x24)
#define C8SECTPFE_PRI_VALUE(x)           (x & 0x7fffff)
#define C8SECTPFE_PRI_LOWPRI(x)          ((x & 0xf) << 24)
#define C8SECTPFE_PRI_HIGHPRI(x)         ((x & 0xf) << 28)

#define C8SECTPFE_IB_STAT(x)             (C8SECTPFE_CHANNEL_OFFSET(x) + 0x28)
#define C8SECTPFE_STAT_FIFO_OVERFLOW(x)  (x & 0x1)
#define C8SECTPFE_STAT_BUFFER_OVERFLOW(x) (x & 0x2)
#define C8SECTPFE_STAT_OUTOFORDERRP(x)   (x & 0x4)
#define C8SECTPFE_STAT_PID_OVERFLOW(x)   (x & 0x8)
#define C8SECTPFE_STAT_PKT_OVERFLOW(x)   (x & 0x10)
#define C8SECTPFE_STAT_ERROR_PACKETS(x)  ((x >> 8) & 0xf)
#define C8SECTPFE_STAT_SHORT_PACKETS(x)  ((x >> 12) & 0xf)

#define C8SECTPFE_IB_MASK(x)             (C8SECTPFE_CHANNEL_OFFSET(x) + 0x2C)
#define C8SECTPFE_MASK_FIFO_OVERFLOW     BIT(0)
#define C8SECTPFE_MASK_BUFFER_OVERFLOW   BIT(1)
#define C8SECTPFE_MASK_OUTOFORDERRP(x)   BIT(2)
#define C8SECTPFE_MASK_PID_OVERFLOW(x)   BIT(3)
#define C8SECTPFE_MASK_PKT_OVERFLOW(x)   BIT(4)
#define C8SECTPFE_MASK_ERROR_PACKETS(x)  ((x & 0xf) << 8)
#define C8SECTPFE_MASK_SHORT_PACKETS(x)  ((x & 0xf) >> 12)

#define C8SECTPFE_IB_SYS(x)              (C8SECTPFE_CHANNEL_OFFSET(x) + 0x30)
#define C8SECTPFE_SYS_RESET              BIT(1)
#define C8SECTPFE_SYS_ENABLE             BIT(0)

/*
 * Ponter record data structure required for each input block
 * see Table 82 on page 167 of functional specification.
 */

#define DMA_PRDS_MEMBASE	0x0 /* Internal sram base address */
#define DMA_PRDS_MEMTOP		0x4 /* Internal sram top address */

/*
 * TS packet size, including tag bytes added by input block,
 * rounded up to the next multiple of 8 bytes. The packet size,
 * including any tagging bytes and rounded up to the nearest
 * multiple of 8 bytes must be less than 255 bytes.
 */
#define DMA_PRDS_PKTSIZE	0x8
#define DMA_PRDS_TPENABLE	0xc

#define TP0_OFFSET		0x10
#define DMA_PRDS_BUSBASE_TP(x)	((0x10*x) + TP0_OFFSET)
#define DMA_PRDS_BUSTOP_TP(x)	((0x10*x) + TP0_OFFSET + 0x4)
#define DMA_PRDS_BUSWP_TP(x)	((0x10*x) + TP0_OFFSET + 0x8)
#define DMA_PRDS_BUSRP_TP(x)	((0x10*x) + TP0_OFFSET + 0xc)

#define DMA_PRDS_SIZE		(0x20)

#define DMA_MEMDMA_OFFSET	0x4000
#define DMA_IMEM_OFFSET		0x0
#define DMA_DMEM_OFFSET		0x4000
#define DMA_CPU			0x8000
#define DMA_PER_OFFSET		0xb000

#define DMA_MEMDMA_DMEM (DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET)
#define DMA_MEMDMA_IMEM (DMA_MEMDMA_OFFSET + DMA_IMEM_OFFSET)

/* XP70 Slim core regs */
#define DMA_CPU_ID	(DMA_MEMDMA_OFFSET + DMA_CPU + 0x0)
#define DMA_CPU_VCR	(DMA_MEMDMA_OFFSET + DMA_CPU + 0x4)
#define DMA_CPU_RUN	(DMA_MEMDMA_OFFSET + DMA_CPU + 0x8)
#define DMA_CPU_CLOCKGATE	(DMA_MEMDMA_OFFSET + DMA_CPU + 0xc)
#define DMA_CPU_PC	(DMA_MEMDMA_OFFSET + DMA_CPU + 0x20)

/* Enable Interrupt for a IB */
#define DMA_PER_TPn_DREQ_MASK	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xd00)
/* Ack interrupt by setting corresponding bit */
#define DMA_PER_TPn_DACK_SET	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xd80)
#define DMA_PER_TPn_DREQ	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xe00)
#define DMA_PER_TPn_DACK	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xe80)
#define DMA_PER_DREQ_MODE	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xf80)
#define DMA_PER_STBUS_SYNC	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xf88)
#define DMA_PER_STBUS_ACCESS	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xf8c)
#define DMA_PER_STBUS_ADDRESS	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xf90)
#define DMA_PER_IDLE_INT	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfa8)
#define DMA_PER_PRIORITY	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfac)
#define DMA_PER_MAX_OPCODE	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfb0)
#define DMA_PER_MAX_CHUNK	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfb4)
#define DMA_PER_PAGE_SIZE	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfbc)
#define DMA_PER_MBOX_STATUS	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfc0)
#define DMA_PER_MBOX_SET	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfc8)
#define DMA_PER_MBOX_CLEAR	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfd0)
#define DMA_PER_MBOX_MASK	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfd8)
#define DMA_PER_INJECT_PKT_SRC	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfe0)
#define DMA_PER_INJECT_PKT_DEST	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfe4)
#define DMA_PER_INJECT_PKT_ADDR	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfe8)
#define DMA_PER_INJECT_PKT	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xfec)
#define DMA_PER_PAT_PTR_INIT	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xff0)
#define DMA_PER_PAT_PTR		(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xff4)
#define DMA_PER_SLEEP_MASK	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xff8)
#define DMA_PER_SLEEP_COUNTER	(DMA_MEMDMA_OFFSET + DMA_PER_OFFSET + 0xffc)
/* #define DMA_RF_CPUREGn	DMA_RFBASEADDR n=0 to 15) slim regsa */

/* The following are from DMA_DMEM_BaseAddress */
#define DMA_FIRMWARE_VERSION	(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0x0)
#define DMA_PTRREC_BASE		(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0x4)
#define DMA_PTRREC_INPUT_OFFSET	(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0x8)
#define DMA_ERRREC_BASE		(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0xc)
#define DMA_ERROR_RECORD(n)	((n*4) + DMA_ERRREC_BASE + 0x4)
#define DMA_IDLE_REQ		(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0x10)
#define IDLEREQ			BIT(31)

#define DMA_FIRMWARE_CONFIG	(DMA_MEMDMA_OFFSET + DMA_DMEM_OFFSET + 0x14)

/* Regs for PID Filter */

#define PIDF_OFFSET		0x2800
#define PIDF_BASE(n)		((n*4) + PIDF_OFFSET)
#define PIDF_LEAK_ENABLE	(PIDF_OFFSET + 0x100)
#define PIDF_LEAK_STATUS	(PIDF_OFFSET + 0x108)
#define PIDF_LEAK_COUNT_RESET	(PIDF_OFFSET + 0x110)
#define PIDF_LEAK_COUNTER	(PIDF_OFFSET + 0x114)

#endif /* _C8SECTPFE_CORE_H_ */

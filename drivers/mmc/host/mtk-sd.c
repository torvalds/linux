// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015 MediaTek Inc.
 * Author: Chaotian.Jing <chaotian.jing@mediatek.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/reset.h>

#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#include "cqhci.h"

#define MAX_BD_NUM          1024
#define MSDC_NR_CLOCKS      3

/*--------------------------------------------------------------------------*/
/* Common Definition                                                        */
/*--------------------------------------------------------------------------*/
#define MSDC_BUS_1BITS          0x0
#define MSDC_BUS_4BITS          0x1
#define MSDC_BUS_8BITS          0x2

#define MSDC_BURST_64B          0x6

/*--------------------------------------------------------------------------*/
/* Register Offset                                                          */
/*--------------------------------------------------------------------------*/
#define MSDC_CFG         0x0
#define MSDC_IOCON       0x04
#define MSDC_PS          0x08
#define MSDC_INT         0x0c
#define MSDC_INTEN       0x10
#define MSDC_FIFOCS      0x14
#define SDC_CFG          0x30
#define SDC_CMD          0x34
#define SDC_ARG          0x38
#define SDC_STS          0x3c
#define SDC_RESP0        0x40
#define SDC_RESP1        0x44
#define SDC_RESP2        0x48
#define SDC_RESP3        0x4c
#define SDC_BLK_NUM      0x50
#define SDC_ADV_CFG0     0x64
#define EMMC_IOCON       0x7c
#define SDC_ACMD_RESP    0x80
#define DMA_SA_H4BIT     0x8c
#define MSDC_DMA_SA      0x90
#define MSDC_DMA_CTRL    0x98
#define MSDC_DMA_CFG     0x9c
#define MSDC_PATCH_BIT   0xb0
#define MSDC_PATCH_BIT1  0xb4
#define MSDC_PATCH_BIT2  0xb8
#define MSDC_PAD_TUNE    0xec
#define MSDC_PAD_TUNE0   0xf0
#define PAD_DS_TUNE      0x188
#define PAD_CMD_TUNE     0x18c
#define EMMC51_CFG0	 0x204
#define EMMC50_CFG0      0x208
#define EMMC50_CFG1      0x20c
#define EMMC50_CFG3      0x220
#define SDC_FIFO_CFG     0x228
#define CQHCI_SETTING	 0x7fc

/*--------------------------------------------------------------------------*/
/* Top Pad Register Offset                                                  */
/*--------------------------------------------------------------------------*/
#define EMMC_TOP_CONTROL	0x00
#define EMMC_TOP_CMD		0x04
#define EMMC50_PAD_DS_TUNE	0x0c

/*--------------------------------------------------------------------------*/
/* Register Mask                                                            */
/*--------------------------------------------------------------------------*/

/* MSDC_CFG mask */
#define MSDC_CFG_MODE           (0x1 << 0)	/* RW */
#define MSDC_CFG_CKPDN          (0x1 << 1)	/* RW */
#define MSDC_CFG_RST            (0x1 << 2)	/* RW */
#define MSDC_CFG_PIO            (0x1 << 3)	/* RW */
#define MSDC_CFG_CKDRVEN        (0x1 << 4)	/* RW */
#define MSDC_CFG_BV18SDT        (0x1 << 5)	/* RW */
#define MSDC_CFG_BV18PSS        (0x1 << 6)	/* R  */
#define MSDC_CFG_CKSTB          (0x1 << 7)	/* R  */
#define MSDC_CFG_CKDIV          (0xff << 8)	/* RW */
#define MSDC_CFG_CKMOD          (0x3 << 16)	/* RW */
#define MSDC_CFG_HS400_CK_MODE  (0x1 << 18)	/* RW */
#define MSDC_CFG_HS400_CK_MODE_EXTRA  (0x1 << 22)	/* RW */
#define MSDC_CFG_CKDIV_EXTRA    (0xfff << 8)	/* RW */
#define MSDC_CFG_CKMOD_EXTRA    (0x3 << 20)	/* RW */

/* MSDC_IOCON mask */
#define MSDC_IOCON_SDR104CKS    (0x1 << 0)	/* RW */
#define MSDC_IOCON_RSPL         (0x1 << 1)	/* RW */
#define MSDC_IOCON_DSPL         (0x1 << 2)	/* RW */
#define MSDC_IOCON_DDLSEL       (0x1 << 3)	/* RW */
#define MSDC_IOCON_DDR50CKD     (0x1 << 4)	/* RW */
#define MSDC_IOCON_DSPLSEL      (0x1 << 5)	/* RW */
#define MSDC_IOCON_W_DSPL       (0x1 << 8)	/* RW */
#define MSDC_IOCON_D0SPL        (0x1 << 16)	/* RW */
#define MSDC_IOCON_D1SPL        (0x1 << 17)	/* RW */
#define MSDC_IOCON_D2SPL        (0x1 << 18)	/* RW */
#define MSDC_IOCON_D3SPL        (0x1 << 19)	/* RW */
#define MSDC_IOCON_D4SPL        (0x1 << 20)	/* RW */
#define MSDC_IOCON_D5SPL        (0x1 << 21)	/* RW */
#define MSDC_IOCON_D6SPL        (0x1 << 22)	/* RW */
#define MSDC_IOCON_D7SPL        (0x1 << 23)	/* RW */
#define MSDC_IOCON_RISCSZ       (0x3 << 24)	/* RW */

/* MSDC_PS mask */
#define MSDC_PS_CDEN            (0x1 << 0)	/* RW */
#define MSDC_PS_CDSTS           (0x1 << 1)	/* R  */
#define MSDC_PS_CDDEBOUNCE      (0xf << 12)	/* RW */
#define MSDC_PS_DAT             (0xff << 16)	/* R  */
#define MSDC_PS_DATA1           (0x1 << 17)	/* R  */
#define MSDC_PS_CMD             (0x1 << 24)	/* R  */
#define MSDC_PS_WP              (0x1 << 31)	/* R  */

/* MSDC_INT mask */
#define MSDC_INT_MMCIRQ         (0x1 << 0)	/* W1C */
#define MSDC_INT_CDSC           (0x1 << 1)	/* W1C */
#define MSDC_INT_ACMDRDY        (0x1 << 3)	/* W1C */
#define MSDC_INT_ACMDTMO        (0x1 << 4)	/* W1C */
#define MSDC_INT_ACMDCRCERR     (0x1 << 5)	/* W1C */
#define MSDC_INT_DMAQ_EMPTY     (0x1 << 6)	/* W1C */
#define MSDC_INT_SDIOIRQ        (0x1 << 7)	/* W1C */
#define MSDC_INT_CMDRDY         (0x1 << 8)	/* W1C */
#define MSDC_INT_CMDTMO         (0x1 << 9)	/* W1C */
#define MSDC_INT_RSPCRCERR      (0x1 << 10)	/* W1C */
#define MSDC_INT_CSTA           (0x1 << 11)	/* R */
#define MSDC_INT_XFER_COMPL     (0x1 << 12)	/* W1C */
#define MSDC_INT_DXFER_DONE     (0x1 << 13)	/* W1C */
#define MSDC_INT_DATTMO         (0x1 << 14)	/* W1C */
#define MSDC_INT_DATCRCERR      (0x1 << 15)	/* W1C */
#define MSDC_INT_ACMD19_DONE    (0x1 << 16)	/* W1C */
#define MSDC_INT_DMA_BDCSERR    (0x1 << 17)	/* W1C */
#define MSDC_INT_DMA_GPDCSERR   (0x1 << 18)	/* W1C */
#define MSDC_INT_DMA_PROTECT    (0x1 << 19)	/* W1C */
#define MSDC_INT_CMDQ           (0x1 << 28)	/* W1C */

/* MSDC_INTEN mask */
#define MSDC_INTEN_MMCIRQ       (0x1 << 0)	/* RW */
#define MSDC_INTEN_CDSC         (0x1 << 1)	/* RW */
#define MSDC_INTEN_ACMDRDY      (0x1 << 3)	/* RW */
#define MSDC_INTEN_ACMDTMO      (0x1 << 4)	/* RW */
#define MSDC_INTEN_ACMDCRCERR   (0x1 << 5)	/* RW */
#define MSDC_INTEN_DMAQ_EMPTY   (0x1 << 6)	/* RW */
#define MSDC_INTEN_SDIOIRQ      (0x1 << 7)	/* RW */
#define MSDC_INTEN_CMDRDY       (0x1 << 8)	/* RW */
#define MSDC_INTEN_CMDTMO       (0x1 << 9)	/* RW */
#define MSDC_INTEN_RSPCRCERR    (0x1 << 10)	/* RW */
#define MSDC_INTEN_CSTA         (0x1 << 11)	/* RW */
#define MSDC_INTEN_XFER_COMPL   (0x1 << 12)	/* RW */
#define MSDC_INTEN_DXFER_DONE   (0x1 << 13)	/* RW */
#define MSDC_INTEN_DATTMO       (0x1 << 14)	/* RW */
#define MSDC_INTEN_DATCRCERR    (0x1 << 15)	/* RW */
#define MSDC_INTEN_ACMD19_DONE  (0x1 << 16)	/* RW */
#define MSDC_INTEN_DMA_BDCSERR  (0x1 << 17)	/* RW */
#define MSDC_INTEN_DMA_GPDCSERR (0x1 << 18)	/* RW */
#define MSDC_INTEN_DMA_PROTECT  (0x1 << 19)	/* RW */

/* MSDC_FIFOCS mask */
#define MSDC_FIFOCS_RXCNT       (0xff << 0)	/* R */
#define MSDC_FIFOCS_TXCNT       (0xff << 16)	/* R */
#define MSDC_FIFOCS_CLR         (0x1 << 31)	/* RW */

/* SDC_CFG mask */
#define SDC_CFG_SDIOINTWKUP     (0x1 << 0)	/* RW */
#define SDC_CFG_INSWKUP         (0x1 << 1)	/* RW */
#define SDC_CFG_WRDTOC          (0x1fff  << 2)  /* RW */
#define SDC_CFG_BUSWIDTH        (0x3 << 16)	/* RW */
#define SDC_CFG_SDIO            (0x1 << 19)	/* RW */
#define SDC_CFG_SDIOIDE         (0x1 << 20)	/* RW */
#define SDC_CFG_INTATGAP        (0x1 << 21)	/* RW */
#define SDC_CFG_DTOC            (0xff << 24)	/* RW */

/* SDC_STS mask */
#define SDC_STS_SDCBUSY         (0x1 << 0)	/* RW */
#define SDC_STS_CMDBUSY         (0x1 << 1)	/* RW */
#define SDC_STS_SWR_COMPL       (0x1 << 31)	/* RW */

#define SDC_DAT1_IRQ_TRIGGER	(0x1 << 19)	/* RW */
/* SDC_ADV_CFG0 mask */
#define SDC_RX_ENHANCE_EN	(0x1 << 20)	/* RW */

/* DMA_SA_H4BIT mask */
#define DMA_ADDR_HIGH_4BIT      (0xf << 0)      /* RW */

/* MSDC_DMA_CTRL mask */
#define MSDC_DMA_CTRL_START     (0x1 << 0)	/* W */
#define MSDC_DMA_CTRL_STOP      (0x1 << 1)	/* W */
#define MSDC_DMA_CTRL_RESUME    (0x1 << 2)	/* W */
#define MSDC_DMA_CTRL_MODE      (0x1 << 8)	/* RW */
#define MSDC_DMA_CTRL_LASTBUF   (0x1 << 10)	/* RW */
#define MSDC_DMA_CTRL_BRUSTSZ   (0x7 << 12)	/* RW */

/* MSDC_DMA_CFG mask */
#define MSDC_DMA_CFG_STS        (0x1 << 0)	/* R */
#define MSDC_DMA_CFG_DECSEN     (0x1 << 1)	/* RW */
#define MSDC_DMA_CFG_AHBHPROT2  (0x2 << 8)	/* RW */
#define MSDC_DMA_CFG_ACTIVEEN   (0x2 << 12)	/* RW */
#define MSDC_DMA_CFG_CS12B16B   (0x1 << 16)	/* RW */

/* MSDC_PATCH_BIT mask */
#define MSDC_PATCH_BIT_ODDSUPP    (0x1 <<  1)	/* RW */
#define MSDC_INT_DAT_LATCH_CK_SEL (0x7 <<  7)
#define MSDC_CKGEN_MSDC_DLY_SEL   (0x1f << 10)
#define MSDC_PATCH_BIT_IODSSEL    (0x1 << 16)	/* RW */
#define MSDC_PATCH_BIT_IOINTSEL   (0x1 << 17)	/* RW */
#define MSDC_PATCH_BIT_BUSYDLY    (0xf << 18)	/* RW */
#define MSDC_PATCH_BIT_WDOD       (0xf << 22)	/* RW */
#define MSDC_PATCH_BIT_IDRTSEL    (0x1 << 26)	/* RW */
#define MSDC_PATCH_BIT_CMDFSEL    (0x1 << 27)	/* RW */
#define MSDC_PATCH_BIT_INTDLSEL   (0x1 << 28)	/* RW */
#define MSDC_PATCH_BIT_SPCPUSH    (0x1 << 29)	/* RW */
#define MSDC_PATCH_BIT_DECRCTMO   (0x1 << 30)	/* RW */

#define MSDC_PATCH_BIT1_CMDTA     (0x7 << 3)    /* RW */
#define MSDC_PB1_BUSY_CHECK_SEL   (0x1 << 7)    /* RW */
#define MSDC_PATCH_BIT1_STOP_DLY  (0xf << 8)    /* RW */

#define MSDC_PATCH_BIT2_CFGRESP   (0x1 << 15)   /* RW */
#define MSDC_PATCH_BIT2_CFGCRCSTS (0x1 << 28)   /* RW */
#define MSDC_PB2_SUPPORT_64G      (0x1 << 1)    /* RW */
#define MSDC_PB2_RESPWAIT         (0x3 << 2)    /* RW */
#define MSDC_PB2_RESPSTSENSEL     (0x7 << 16)   /* RW */
#define MSDC_PB2_CRCSTSENSEL      (0x7 << 29)   /* RW */

#define MSDC_PAD_TUNE_DATWRDLY	  (0x1f <<  0)	/* RW */
#define MSDC_PAD_TUNE_DATRRDLY	  (0x1f <<  8)	/* RW */
#define MSDC_PAD_TUNE_CMDRDLY	  (0x1f << 16)  /* RW */
#define MSDC_PAD_TUNE_CMDRRDLY	  (0x1f << 22)	/* RW */
#define MSDC_PAD_TUNE_CLKTDLY	  (0x1f << 27)  /* RW */
#define MSDC_PAD_TUNE_RXDLYSEL	  (0x1 << 15)   /* RW */
#define MSDC_PAD_TUNE_RD_SEL	  (0x1 << 13)   /* RW */
#define MSDC_PAD_TUNE_CMD_SEL	  (0x1 << 21)   /* RW */

#define PAD_DS_TUNE_DLY1	  (0x1f << 2)   /* RW */
#define PAD_DS_TUNE_DLY2	  (0x1f << 7)   /* RW */
#define PAD_DS_TUNE_DLY3	  (0x1f << 12)  /* RW */

#define PAD_CMD_TUNE_RX_DLY3	  (0x1f << 1)  /* RW */

/* EMMC51_CFG0 mask */
#define CMDQ_RDAT_CNT		  (0x3ff << 12)	/* RW */

#define EMMC50_CFG_PADCMD_LATCHCK (0x1 << 0)   /* RW */
#define EMMC50_CFG_CRCSTS_EDGE    (0x1 << 3)   /* RW */
#define EMMC50_CFG_CFCSTS_SEL     (0x1 << 4)   /* RW */
#define EMMC50_CFG_CMD_RESP_SEL   (0x1 << 9)   /* RW */

/* EMMC50_CFG1 mask */
#define EMMC50_CFG1_DS_CFG        (0x1 << 28)  /* RW */

#define EMMC50_CFG3_OUTS_WR       (0x1f << 0)  /* RW */

#define SDC_FIFO_CFG_WRVALIDSEL   (0x1 << 24)  /* RW */
#define SDC_FIFO_CFG_RDVALIDSEL   (0x1 << 25)  /* RW */

/* CQHCI_SETTING */
#define CQHCI_RD_CMD_WND_SEL	  (0x1 << 14) /* RW */
#define CQHCI_WR_CMD_WND_SEL	  (0x1 << 15) /* RW */

/* EMMC_TOP_CONTROL mask */
#define PAD_RXDLY_SEL           (0x1 << 0)      /* RW */
#define DELAY_EN                (0x1 << 1)      /* RW */
#define PAD_DAT_RD_RXDLY2       (0x1f << 2)     /* RW */
#define PAD_DAT_RD_RXDLY        (0x1f << 7)     /* RW */
#define PAD_DAT_RD_RXDLY2_SEL   (0x1 << 12)     /* RW */
#define PAD_DAT_RD_RXDLY_SEL    (0x1 << 13)     /* RW */
#define DATA_K_VALUE_SEL        (0x1 << 14)     /* RW */
#define SDC_RX_ENH_EN           (0x1 << 15)     /* TW */

/* EMMC_TOP_CMD mask */
#define PAD_CMD_RXDLY2          (0x1f << 0)     /* RW */
#define PAD_CMD_RXDLY           (0x1f << 5)     /* RW */
#define PAD_CMD_RD_RXDLY2_SEL   (0x1 << 10)     /* RW */
#define PAD_CMD_RD_RXDLY_SEL    (0x1 << 11)     /* RW */
#define PAD_CMD_TX_DLY          (0x1f << 12)    /* RW */

#define REQ_CMD_EIO  (0x1 << 0)
#define REQ_CMD_TMO  (0x1 << 1)
#define REQ_DAT_ERR  (0x1 << 2)
#define REQ_STOP_EIO (0x1 << 3)
#define REQ_STOP_TMO (0x1 << 4)
#define REQ_CMD_BUSY (0x1 << 5)

#define MSDC_PREPARE_FLAG (0x1 << 0)
#define MSDC_ASYNC_FLAG (0x1 << 1)
#define MSDC_MMAP_FLAG (0x1 << 2)

#define MTK_MMC_AUTOSUSPEND_DELAY	50
#define CMD_TIMEOUT         (HZ/10 * 5)	/* 100ms x5 */
#define DAT_TIMEOUT         (HZ    * 5)	/* 1000ms x5 */

#define DEFAULT_DEBOUNCE	(8)	/* 8 cycles CD debounce */

#define PAD_DELAY_MAX	32 /* PAD delay cells */
/*--------------------------------------------------------------------------*/
/* Descriptor Structure                                                     */
/*--------------------------------------------------------------------------*/
struct mt_gpdma_desc {
	u32 gpd_info;
#define GPDMA_DESC_HWO		(0x1 << 0)
#define GPDMA_DESC_BDP		(0x1 << 1)
#define GPDMA_DESC_CHECKSUM	(0xff << 8) /* bit8 ~ bit15 */
#define GPDMA_DESC_INT		(0x1 << 16)
#define GPDMA_DESC_NEXT_H4	(0xf << 24)
#define GPDMA_DESC_PTR_H4	(0xf << 28)
	u32 next;
	u32 ptr;
	u32 gpd_data_len;
#define GPDMA_DESC_BUFLEN	(0xffff) /* bit0 ~ bit15 */
#define GPDMA_DESC_EXTLEN	(0xff << 16) /* bit16 ~ bit23 */
	u32 arg;
	u32 blknum;
	u32 cmd;
};

struct mt_bdma_desc {
	u32 bd_info;
#define BDMA_DESC_EOL		(0x1 << 0)
#define BDMA_DESC_CHECKSUM	(0xff << 8) /* bit8 ~ bit15 */
#define BDMA_DESC_BLKPAD	(0x1 << 17)
#define BDMA_DESC_DWPAD		(0x1 << 18)
#define BDMA_DESC_NEXT_H4	(0xf << 24)
#define BDMA_DESC_PTR_H4	(0xf << 28)
	u32 next;
	u32 ptr;
	u32 bd_data_len;
#define BDMA_DESC_BUFLEN	(0xffff) /* bit0 ~ bit15 */
#define BDMA_DESC_BUFLEN_EXT	(0xffffff) /* bit0 ~ bit23 */
};

struct msdc_dma {
	struct scatterlist *sg;	/* I/O scatter list */
	struct mt_gpdma_desc *gpd;		/* pointer to gpd array */
	struct mt_bdma_desc *bd;		/* pointer to bd array */
	dma_addr_t gpd_addr;	/* the physical address of gpd array */
	dma_addr_t bd_addr;	/* the physical address of bd array */
};

struct msdc_save_para {
	u32 msdc_cfg;
	u32 iocon;
	u32 sdc_cfg;
	u32 pad_tune;
	u32 patch_bit0;
	u32 patch_bit1;
	u32 patch_bit2;
	u32 pad_ds_tune;
	u32 pad_cmd_tune;
	u32 emmc50_cfg0;
	u32 emmc50_cfg3;
	u32 sdc_fifo_cfg;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
	u32 emmc50_pad_ds_tune;
};

struct mtk_mmc_compatible {
	u8 clk_div_bits;
	bool recheck_sdio_irq;
	bool hs400_tune; /* only used for MT8173 */
	u32 pad_tune_reg;
	bool async_fifo;
	bool data_tune;
	bool busy_check;
	bool stop_clk_fix;
	bool enhance_rx;
	bool support_64g;
	bool use_internal_cd;
};

struct msdc_tune_para {
	u32 iocon;
	u32 pad_tune;
	u32 pad_cmd_tune;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
};

struct msdc_delay_phase {
	u8 maxlen;
	u8 start;
	u8 final_phase;
};

struct msdc_host {
	struct device *dev;
	const struct mtk_mmc_compatible *dev_comp;
	int cmd_rsp;

	spinlock_t lock;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	int error;

	void __iomem *base;		/* host base address */
	void __iomem *top_base;		/* host top register base address */

	struct msdc_dma dma;	/* dma channel */
	u64 dma_mask;

	u32 timeout_ns;		/* data timeout ns */
	u32 timeout_clks;	/* data timeout clks */

	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_uhs;
	struct delayed_work req_timeout;
	int irq;		/* host interrupt */
	struct reset_control *reset;

	struct clk *src_clk;	/* msdc source clock */
	struct clk *h_clk;      /* msdc h_clk */
	struct clk *bus_clk;	/* bus clock which used to access register */
	struct clk *src_clk_cg; /* msdc source clock control gate */
	struct clk *sys_clk_cg;	/* msdc subsys clock control gate */
	struct clk_bulk_data bulk_clks[MSDC_NR_CLOCKS];
	u32 mclk;		/* mmc subsystem clock frequency */
	u32 src_clk_freq;	/* source clock frequency */
	unsigned char timing;
	bool vqmmc_enabled;
	u32 latch_ck;
	u32 hs400_ds_delay;
	u32 hs200_cmd_int_delay; /* cmd internal delay for HS200/SDR104 */
	u32 hs400_cmd_int_delay; /* cmd internal delay for HS400 */
	bool hs400_cmd_resp_sel_rising;
				 /* cmd response sample selection for HS400 */
	bool hs400_mode;	/* current eMMC will run at hs400 mode */
	bool internal_cd;	/* Use internal card-detect logic */
	bool cqhci;		/* support eMMC hw cmdq */
	struct msdc_save_para save_para; /* used when gate HCLK */
	struct msdc_tune_para def_tune_para; /* default tune setting */
	struct msdc_tune_para saved_tune_para; /* tune result of CMD21/CMD19 */
	struct cqhci_host *cq_host;
};

static const struct mtk_mmc_compatible mt8135_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8173_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = true,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8183_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt2701_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt2712_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt7622_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.enhance_rx = true,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8516_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
};

static const struct mtk_mmc_compatible mt7620_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.use_internal_cd = true,
};

static const struct mtk_mmc_compatible mt6779_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct of_device_id msdc_of_ids[] = {
	{ .compatible = "mediatek,mt8135-mmc", .data = &mt8135_compat},
	{ .compatible = "mediatek,mt8173-mmc", .data = &mt8173_compat},
	{ .compatible = "mediatek,mt8183-mmc", .data = &mt8183_compat},
	{ .compatible = "mediatek,mt2701-mmc", .data = &mt2701_compat},
	{ .compatible = "mediatek,mt2712-mmc", .data = &mt2712_compat},
	{ .compatible = "mediatek,mt7622-mmc", .data = &mt7622_compat},
	{ .compatible = "mediatek,mt8516-mmc", .data = &mt8516_compat},
	{ .compatible = "mediatek,mt7620-mmc", .data = &mt7620_compat},
	{ .compatible = "mediatek,mt6779-mmc", .data = &mt6779_compat},
	{}
};
MODULE_DEVICE_TABLE(of, msdc_of_ids);

static void sdr_set_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val |= bs;
	writel(val, reg);
}

static void sdr_clr_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val &= ~bs;
	writel(val, reg);
}

static void sdr_set_field(void __iomem *reg, u32 field, u32 val)
{
	unsigned int tv = readl(reg);

	tv &= ~field;
	tv |= ((val) << (ffs((unsigned int)field) - 1));
	writel(tv, reg);
}

static void sdr_get_field(void __iomem *reg, u32 field, u32 *val)
{
	unsigned int tv = readl(reg);

	*val = ((tv & field) >> (ffs((unsigned int)field) - 1));
}

static void msdc_reset_hw(struct msdc_host *host)
{
	u32 val;

	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_RST);
	while (readl(host->base + MSDC_CFG) & MSDC_CFG_RST)
		cpu_relax();

	sdr_set_bits(host->base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	while (readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_CLR)
		cpu_relax();

	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd);
static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb);

static const u32 cmd_ints_mask = MSDC_INTEN_CMDRDY | MSDC_INTEN_RSPCRCERR |
			MSDC_INTEN_CMDTMO | MSDC_INTEN_ACMDRDY |
			MSDC_INTEN_ACMDCRCERR | MSDC_INTEN_ACMDTMO;
static const u32 data_ints_mask = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO |
			MSDC_INTEN_DATCRCERR | MSDC_INTEN_DMA_BDCSERR |
			MSDC_INTEN_DMA_GPDCSERR | MSDC_INTEN_DMA_PROTECT;

static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
	u32 i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];
	return 0xff - (u8) sum;
}

static inline void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
		struct mmc_data *data)
{
	unsigned int j, dma_len;
	dma_addr_t dma_address;
	u32 dma_ctrl;
	struct scatterlist *sg;
	struct mt_gpdma_desc *gpd;
	struct mt_bdma_desc *bd;

	sg = data->sg;

	gpd = dma->gpd;
	bd = dma->bd;

	/* modify gpd */
	gpd->gpd_info |= GPDMA_DESC_HWO;
	gpd->gpd_info |= GPDMA_DESC_BDP;
	/* need to clear first. use these bits to calc checksum */
	gpd->gpd_info &= ~GPDMA_DESC_CHECKSUM;
	gpd->gpd_info |= msdc_dma_calcs((u8 *) gpd, 16) << 8;

	/* modify bd */
	for_each_sg(data->sg, sg, data->sg_count, j) {
		dma_address = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);

		/* init bd */
		bd[j].bd_info &= ~BDMA_DESC_BLKPAD;
		bd[j].bd_info &= ~BDMA_DESC_DWPAD;
		bd[j].ptr = lower_32_bits(dma_address);
		if (host->dev_comp->support_64g) {
			bd[j].bd_info &= ~BDMA_DESC_PTR_H4;
			bd[j].bd_info |= (upper_32_bits(dma_address) & 0xf)
					 << 28;
		}

		if (host->dev_comp->support_64g) {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN_EXT;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN_EXT);
		} else {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN);
		}

		if (j == data->sg_count - 1) /* the last bd */
			bd[j].bd_info |= BDMA_DESC_EOL;
		else
			bd[j].bd_info &= ~BDMA_DESC_EOL;

		/* checksume need to clear first */
		bd[j].bd_info &= ~BDMA_DESC_CHECKSUM;
		bd[j].bd_info |= msdc_dma_calcs((u8 *)(&bd[j]), 16) << 8;
	}

	sdr_set_field(host->base + MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, 1);
	dma_ctrl = readl_relaxed(host->base + MSDC_DMA_CTRL);
	dma_ctrl &= ~(MSDC_DMA_CTRL_BRUSTSZ | MSDC_DMA_CTRL_MODE);
	dma_ctrl |= (MSDC_BURST_64B << 12 | 1 << 8);
	writel_relaxed(dma_ctrl, host->base + MSDC_DMA_CTRL);
	if (host->dev_comp->support_64g)
		sdr_set_field(host->base + DMA_SA_H4BIT, DMA_ADDR_HIGH_4BIT,
			      upper_32_bits(dma->gpd_addr) & 0xf);
	writel(lower_32_bits(dma->gpd_addr), host->base + MSDC_DMA_SA);
}

static void msdc_prepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (!(data->host_cookie & MSDC_PREPARE_FLAG)) {
		data->host_cookie |= MSDC_PREPARE_FLAG;
		data->sg_count = dma_map_sg(host->dev, data->sg, data->sg_len,
					    mmc_get_dma_dir(data));
	}
}

static void msdc_unprepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (data->host_cookie & MSDC_ASYNC_FLAG)
		return;

	if (data->host_cookie & MSDC_PREPARE_FLAG) {
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
		data->host_cookie &= ~MSDC_PREPARE_FLAG;
	}
}

static u64 msdc_timeout_cal(struct msdc_host *host, u64 ns, u64 clks)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u64 timeout, clk_ns;
	u32 mode = 0;

	if (mmc->actual_clock == 0) {
		timeout = 0;
	} else {
		clk_ns  = 1000000000ULL;
		do_div(clk_ns, mmc->actual_clock);
		timeout = ns + clk_ns - 1;
		do_div(timeout, clk_ns);
		timeout += clks;
		/* in 1048576 sclk cycle unit */
		timeout = DIV_ROUND_UP(timeout, (0x1 << 20));
		if (host->dev_comp->clk_div_bits == 8)
			sdr_get_field(host->base + MSDC_CFG,
				      MSDC_CFG_CKMOD, &mode);
		else
			sdr_get_field(host->base + MSDC_CFG,
				      MSDC_CFG_CKMOD_EXTRA, &mode);
		/*DDR mode will double the clk cycles for data timeout */
		timeout = mode >= 2 ? timeout * 2 : timeout;
		timeout = timeout > 1 ? timeout - 1 : 0;
	}
	return timeout;
}

/* clock control primitives */
static void msdc_set_timeout(struct msdc_host *host, u64 ns, u64 clks)
{
	u64 timeout;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	timeout = msdc_timeout_cal(host, ns, clks);
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC,
		      (u32)(timeout > 255 ? 255 : timeout));
}

static void msdc_set_busy_timeout(struct msdc_host *host, u64 ns, u64 clks)
{
	u64 timeout;

	timeout = msdc_timeout_cal(host, ns, clks);
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_WRDTOC,
		      (u32)(timeout > 8191 ? 8191 : timeout));
}

static void msdc_gate_clock(struct msdc_host *host)
{
	clk_bulk_disable_unprepare(MSDC_NR_CLOCKS, host->bulk_clks);
	clk_disable_unprepare(host->src_clk_cg);
	clk_disable_unprepare(host->src_clk);
	clk_disable_unprepare(host->bus_clk);
	clk_disable_unprepare(host->h_clk);
}

static void msdc_ungate_clock(struct msdc_host *host)
{
	int ret;

	clk_prepare_enable(host->h_clk);
	clk_prepare_enable(host->bus_clk);
	clk_prepare_enable(host->src_clk);
	clk_prepare_enable(host->src_clk_cg);
	ret = clk_bulk_prepare_enable(MSDC_NR_CLOCKS, host->bulk_clks);
	if (ret) {
		dev_err(host->dev, "Cannot enable pclk/axi/ahb clock gates\n");
		return;
	}

	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		cpu_relax();
}

static void msdc_set_mclk(struct msdc_host *host, unsigned char timing, u32 hz)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 mode;
	u32 flags;
	u32 div;
	u32 sclk;
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (!hz) {
		dev_dbg(host->dev, "set mclk to 0\n");
		host->mclk = 0;
		mmc->actual_clock = 0;
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
		return;
	}

	flags = readl(host->base + MSDC_INTEN);
	sdr_clr_bits(host->base + MSDC_INTEN, flags);
	if (host->dev_comp->clk_div_bits == 8)
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_HS400_CK_MODE);
	else
		sdr_clr_bits(host->base + MSDC_CFG,
			     MSDC_CFG_HS400_CK_MODE_EXTRA);
	if (timing == MMC_TIMING_UHS_DDR50 ||
	    timing == MMC_TIMING_MMC_DDR52 ||
	    timing == MMC_TIMING_MMC_HS400) {
		if (timing == MMC_TIMING_MMC_HS400)
			mode = 0x3;
		else
			mode = 0x2; /* ddr mode and use divisor */

		if (hz >= (host->src_clk_freq >> 2)) {
			div = 0; /* mean div = 1/4 */
			sclk = host->src_clk_freq >> 2; /* sclk = clk / 4 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
			div = (div >> 1);
		}

		if (timing == MMC_TIMING_MMC_HS400 &&
		    hz >= (host->src_clk_freq >> 1)) {
			if (host->dev_comp->clk_div_bits == 8)
				sdr_set_bits(host->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE);
			else
				sdr_set_bits(host->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE_EXTRA);
			sclk = host->src_clk_freq >> 1;
			div = 0; /* div is ignore when bit18 is set */
		}
	} else if (hz >= host->src_clk_freq) {
		mode = 0x1; /* no divisor */
		div = 0;
		sclk = host->src_clk_freq;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (host->src_clk_freq >> 1)) {
			div = 0; /* mean div = 1/2 */
			sclk = host->src_clk_freq >> 1; /* sclk = clk / 2 */
		} else {
			div = (host->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (host->src_clk_freq >> 2) / div;
		}
	}
	sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
	/*
	 * As src_clk/HCLK use the same bit to gate/ungate,
	 * So if want to only gate src_clk, need gate its parent(mux).
	 */
	if (host->src_clk_cg)
		clk_disable_unprepare(host->src_clk_cg);
	else
		clk_disable_unprepare(clk_get_parent(host->src_clk));
	if (host->dev_comp->clk_div_bits == 8)
		sdr_set_field(host->base + MSDC_CFG,
			      MSDC_CFG_CKMOD | MSDC_CFG_CKDIV,
			      (mode << 8) | div);
	else
		sdr_set_field(host->base + MSDC_CFG,
			      MSDC_CFG_CKMOD_EXTRA | MSDC_CFG_CKDIV_EXTRA,
			      (mode << 12) | div);
	if (host->src_clk_cg)
		clk_prepare_enable(host->src_clk_cg);
	else
		clk_prepare_enable(clk_get_parent(host->src_clk));

	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		cpu_relax();
	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_CKPDN);
	mmc->actual_clock = sclk;
	host->mclk = hz;
	host->timing = timing;
	/* need because clk changed. */
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);
	sdr_set_bits(host->base + MSDC_INTEN, flags);

	/*
	 * mmc_select_hs400() will drop to 50Mhz and High speed mode,
	 * tune result of hs200/200Mhz is not suitable for 50Mhz
	 */
	if (mmc->actual_clock <= 52000000) {
		writel(host->def_tune_para.iocon, host->base + MSDC_IOCON);
		if (host->top_base) {
			writel(host->def_tune_para.emmc_top_control,
			       host->top_base + EMMC_TOP_CONTROL);
			writel(host->def_tune_para.emmc_top_cmd,
			       host->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->def_tune_para.pad_tune,
			       host->base + tune_reg);
		}
	} else {
		writel(host->saved_tune_para.iocon, host->base + MSDC_IOCON);
		writel(host->saved_tune_para.pad_cmd_tune,
		       host->base + PAD_CMD_TUNE);
		if (host->top_base) {
			writel(host->saved_tune_para.emmc_top_control,
			       host->top_base + EMMC_TOP_CONTROL);
			writel(host->saved_tune_para.emmc_top_cmd,
			       host->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->saved_tune_para.pad_tune,
			       host->base + tune_reg);
		}
	}

	if (timing == MMC_TIMING_MMC_HS400 &&
	    host->dev_comp->hs400_tune)
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs400_cmd_int_delay);
	dev_dbg(host->dev, "sclk: %d, timing: %d\n", mmc->actual_clock,
		timing);
}

static inline u32 msdc_cmd_find_resp(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	u32 resp;

	switch (mmc_resp_type(cmd)) {
		/* Actually, R1, R5, R6, R7 are the same */
	case MMC_RSP_R1:
		resp = 0x1;
		break;
	case MMC_RSP_R1B:
		resp = 0x7;
		break;
	case MMC_RSP_R2:
		resp = 0x2;
		break;
	case MMC_RSP_R3:
		resp = 0x3;
		break;
	case MMC_RSP_NONE:
	default:
		resp = 0x0;
		break;
	}

	return resp;
}

static inline u32 msdc_cmd_prepare_raw_cmd(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	/* rawcmd :
	 * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
	 * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
	 */
	u32 opcode = cmd->opcode;
	u32 resp = msdc_cmd_find_resp(host, mrq, cmd);
	u32 rawcmd = (opcode & 0x3f) | ((resp & 0x7) << 7);

	host->cmd_rsp = resp;

	if ((opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int) -1) ||
	    opcode == MMC_STOP_TRANSMISSION)
		rawcmd |= (0x1 << 14);
	else if (opcode == SD_SWITCH_VOLTAGE)
		rawcmd |= (0x1 << 30);
	else if (opcode == SD_APP_SEND_SCR ||
		 opcode == SD_APP_SEND_NUM_WR_BLKS ||
		 (opcode == SD_SWITCH && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == SD_APP_SD_STATUS && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == MMC_SEND_EXT_CSD && mmc_cmd_type(cmd) == MMC_CMD_ADTC))
		rawcmd |= (0x1 << 11);

	if (cmd->data) {
		struct mmc_data *data = cmd->data;

		if (mmc_op_multi(opcode)) {
			if (mmc_card_mmc(mmc->card) && mrq->sbc &&
			    !(mrq->sbc->arg & 0xFFFF0000))
				rawcmd |= 0x2 << 28; /* AutoCMD23 */
		}

		rawcmd |= ((data->blksz & 0xFFF) << 16);
		if (data->flags & MMC_DATA_WRITE)
			rawcmd |= (0x1 << 13);
		if (data->blocks > 1)
			rawcmd |= (0x2 << 11);
		else
			rawcmd |= (0x1 << 11);
		/* Always use dma mode */
		sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_PIO);

		if (host->timeout_ns != data->timeout_ns ||
		    host->timeout_clks != data->timeout_clks)
			msdc_set_timeout(host, data->timeout_ns,
					data->timeout_clks);

		writel(data->blocks, host->base + SDC_BLK_NUM);
	}
	return rawcmd;
}

static void msdc_start_data(struct msdc_host *host, struct mmc_request *mrq,
			    struct mmc_command *cmd, struct mmc_data *data)
{
	bool read;

	WARN_ON(host->data);
	host->data = data;
	read = data->flags & MMC_DATA_READ;

	mod_delayed_work(system_wq, &host->req_timeout, DAT_TIMEOUT);
	msdc_dma_setup(host, &host->dma, data);
	sdr_set_bits(host->base + MSDC_INTEN, data_ints_mask);
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);
	dev_dbg(host->dev, "DMA start\n");
	dev_dbg(host->dev, "%s: cmd=%d DMA data: %d blocks; read=%d\n",
			__func__, cmd->opcode, data->blocks, read);
}

static int msdc_auto_cmd_done(struct msdc_host *host, int events,
		struct mmc_command *cmd)
{
	u32 *rsp = cmd->resp;

	rsp[0] = readl(host->base + SDC_ACMD_RESP);

	if (events & MSDC_INT_ACMDRDY) {
		cmd->error = 0;
	} else {
		msdc_reset_hw(host);
		if (events & MSDC_INT_ACMDCRCERR) {
			cmd->error = -EILSEQ;
			host->error |= REQ_STOP_EIO;
		} else if (events & MSDC_INT_ACMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_STOP_TMO;
		}
		dev_err(host->dev,
			"%s: AUTO_CMD%d arg=%08X; rsp %08X; cmd_error=%d\n",
			__func__, cmd->opcode, cmd->arg, rsp[0], cmd->error);
	}
	return cmd->error;
}

/*
 * msdc_recheck_sdio_irq - recheck whether the SDIO irq is lost
 *
 * Host controller may lost interrupt in some special case.
 * Add SDIO irq recheck mechanism to make sure all interrupts
 * can be processed immediately
 */
static void msdc_recheck_sdio_irq(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 reg_int, reg_inten, reg_ps;

	if (mmc->caps & MMC_CAP_SDIO_IRQ) {
		reg_inten = readl(host->base + MSDC_INTEN);
		if (reg_inten & MSDC_INTEN_SDIOIRQ) {
			reg_int = readl(host->base + MSDC_INT);
			reg_ps = readl(host->base + MSDC_PS);
			if (!(reg_int & MSDC_INT_SDIOIRQ ||
			      reg_ps & MSDC_PS_DATA1)) {
				__msdc_enable_sdio_irq(host, 0);
				sdio_signal_irq(mmc);
			}
		}
	}
}

static void msdc_track_cmd_data(struct msdc_host *host,
				struct mmc_command *cmd, struct mmc_data *data)
{
	if (host->error)
		dev_dbg(host->dev, "%s: cmd=%d arg=%08X; host->error=0x%08X\n",
			__func__, cmd->opcode, cmd->arg, host->error);
}

static void msdc_request_done(struct msdc_host *host, struct mmc_request *mrq)
{
	unsigned long flags;

	/*
	 * No need check the return value of cancel_delayed_work, as only ONE
	 * path will go here!
	 */
	cancel_delayed_work(&host->req_timeout);

	spin_lock_irqsave(&host->lock, flags);
	host->mrq = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	msdc_track_cmd_data(host, mrq->cmd, mrq->data);
	if (mrq->data)
		msdc_unprepare_data(host, mrq->data);
	if (host->error)
		msdc_reset_hw(host);
	mmc_request_done(mmc_from_priv(host), mrq);
	if (host->dev_comp->recheck_sdio_irq)
		msdc_recheck_sdio_irq(host);
}

/* returns true if command is fully handled; returns false otherwise */
static bool msdc_cmd_done(struct msdc_host *host, int events,
			  struct mmc_request *mrq, struct mmc_command *cmd)
{
	bool done = false;
	bool sbc_error;
	unsigned long flags;
	u32 *rsp;

	if (mrq->sbc && cmd == mrq->cmd &&
	    (events & (MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR
				   | MSDC_INT_ACMDTMO)))
		msdc_auto_cmd_done(host, events, mrq->sbc);

	sbc_error = mrq->sbc && mrq->sbc->error;

	if (!sbc_error && !(events & (MSDC_INT_CMDRDY
					| MSDC_INT_RSPCRCERR
					| MSDC_INT_CMDTMO)))
		return done;

	spin_lock_irqsave(&host->lock, flags);
	done = !host->cmd;
	host->cmd = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return true;
	rsp = cmd->resp;

	sdr_clr_bits(host->base + MSDC_INTEN, cmd_ints_mask);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			rsp[0] = readl(host->base + SDC_RESP3);
			rsp[1] = readl(host->base + SDC_RESP2);
			rsp[2] = readl(host->base + SDC_RESP1);
			rsp[3] = readl(host->base + SDC_RESP0);
		} else {
			rsp[0] = readl(host->base + SDC_RESP0);
		}
	}

	if (!sbc_error && !(events & MSDC_INT_CMDRDY)) {
		if (events & MSDC_INT_CMDTMO ||
		    (cmd->opcode != MMC_SEND_TUNING_BLOCK &&
		     cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200))
			/*
			 * should not clear fifo/interrupt as the tune data
			 * may have alreay come when cmd19/cmd21 gets response
			 * CRC error.
			 */
			msdc_reset_hw(host);
		if (events & MSDC_INT_RSPCRCERR) {
			cmd->error = -EILSEQ;
			host->error |= REQ_CMD_EIO;
		} else if (events & MSDC_INT_CMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_CMD_TMO;
		}
	}
	if (cmd->error)
		dev_dbg(host->dev,
				"%s: cmd=%d arg=%08X; rsp %08X; cmd_error=%d\n",
				__func__, cmd->opcode, cmd->arg, rsp[0],
				cmd->error);

	msdc_cmd_next(host, mrq, cmd);
	return true;
}

/* It is the core layer's responsibility to ensure card status
 * is correct before issue a request. but host design do below
 * checks recommended.
 */
static inline bool msdc_cmd_is_ready(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	/* The max busy time we can endure is 20ms */
	unsigned long tmo = jiffies + msecs_to_jiffies(20);

	while ((readl(host->base + SDC_STS) & SDC_STS_CMDBUSY) &&
			time_before(jiffies, tmo))
		cpu_relax();
	if (readl(host->base + SDC_STS) & SDC_STS_CMDBUSY) {
		dev_err(host->dev, "CMD bus busy detected\n");
		host->error |= REQ_CMD_BUSY;
		msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
		return false;
	}

	if (mmc_resp_type(cmd) == MMC_RSP_R1B || cmd->data) {
		tmo = jiffies + msecs_to_jiffies(20);
		/* R1B or with data, should check SDCBUSY */
		while ((readl(host->base + SDC_STS) & SDC_STS_SDCBUSY) &&
				time_before(jiffies, tmo))
			cpu_relax();
		if (readl(host->base + SDC_STS) & SDC_STS_SDCBUSY) {
			dev_err(host->dev, "Controller busy detected\n");
			host->error |= REQ_CMD_BUSY;
			msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
			return false;
		}
	}
	return true;
}

static void msdc_start_command(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	u32 rawcmd;
	unsigned long flags;

	WARN_ON(host->cmd);
	host->cmd = cmd;

	mod_delayed_work(system_wq, &host->req_timeout, DAT_TIMEOUT);
	if (!msdc_cmd_is_ready(host, mrq, cmd))
		return;

	if ((readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16 ||
	    readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) {
		dev_err(host->dev, "TX/RX FIFO non-empty before start of IO. Reset\n");
		msdc_reset_hw(host);
	}

	cmd->error = 0;
	rawcmd = msdc_cmd_prepare_raw_cmd(host, mrq, cmd);

	spin_lock_irqsave(&host->lock, flags);
	sdr_set_bits(host->base + MSDC_INTEN, cmd_ints_mask);
	spin_unlock_irqrestore(&host->lock, flags);

	writel(cmd->arg, host->base + SDC_ARG);
	writel(rawcmd, host->base + SDC_CMD);
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	if ((cmd->error &&
	    !(cmd->error == -EILSEQ &&
	      (cmd->opcode == MMC_SEND_TUNING_BLOCK ||
	       cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200))) ||
	    (mrq->sbc && mrq->sbc->error))
		msdc_request_done(host, mrq);
	else if (cmd == mrq->sbc)
		msdc_start_command(host, mrq, mrq->cmd);
	else if (!cmd->data)
		msdc_request_done(host, mrq);
	else
		msdc_start_data(host, mrq, cmd, cmd->data);
}

static void msdc_ops_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	host->error = 0;
	WARN_ON(host->mrq);
	host->mrq = mrq;

	if (mrq->data)
		msdc_prepare_data(host, mrq->data);

	/* if SBC is required, we have HW option and SW option.
	 * if HW option is enabled, and SBC does not have "special" flags,
	 * use HW option,  otherwise use SW option
	 */
	if (mrq->sbc && (!mmc_card_mmc(mmc->card) ||
	    (mrq->sbc->arg & 0xFFFF0000)))
		msdc_start_command(host, mrq, mrq->sbc);
	else
		msdc_start_command(host, mrq, mrq->cmd);
}

static void msdc_pre_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	msdc_prepare_data(host, data);
	data->host_cookie |= MSDC_ASYNC_FLAG;
}

static void msdc_post_req(struct mmc_host *mmc, struct mmc_request *mrq,
		int err)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	if (data->host_cookie) {
		data->host_cookie &= ~MSDC_ASYNC_FLAG;
		msdc_unprepare_data(host, data);
	}
}

static void msdc_data_xfer_next(struct msdc_host *host, struct mmc_request *mrq)
{
	if (mmc_op_multi(mrq->cmd->opcode) && mrq->stop && !mrq->stop->error &&
	    !mrq->sbc)
		msdc_start_command(host, mrq, mrq->stop);
	else
		msdc_request_done(host, mrq);
}

static bool msdc_data_xfer_done(struct msdc_host *host, u32 events,
				struct mmc_request *mrq, struct mmc_data *data)
{
	struct mmc_command *stop;
	unsigned long flags;
	bool done;
	unsigned int check_data = events &
	    (MSDC_INT_XFER_COMPL | MSDC_INT_DATCRCERR | MSDC_INT_DATTMO
	     | MSDC_INT_DMA_BDCSERR | MSDC_INT_DMA_GPDCSERR
	     | MSDC_INT_DMA_PROTECT);

	spin_lock_irqsave(&host->lock, flags);
	done = !host->data;
	if (check_data)
		host->data = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return true;
	stop = data->stop;

	if (check_data || (stop && stop->error)) {
		dev_dbg(host->dev, "DMA status: 0x%8X\n",
				readl(host->base + MSDC_DMA_CFG));
		sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP,
				1);
		while (readl(host->base + MSDC_DMA_CFG) & MSDC_DMA_CFG_STS)
			cpu_relax();
		sdr_clr_bits(host->base + MSDC_INTEN, data_ints_mask);
		dev_dbg(host->dev, "DMA stop\n");

		if ((events & MSDC_INT_XFER_COMPL) && (!stop || !stop->error)) {
			data->bytes_xfered = data->blocks * data->blksz;
		} else {
			dev_dbg(host->dev, "interrupt events: %x\n", events);
			msdc_reset_hw(host);
			host->error |= REQ_DAT_ERR;
			data->bytes_xfered = 0;

			if (events & MSDC_INT_DATTMO)
				data->error = -ETIMEDOUT;
			else if (events & MSDC_INT_DATCRCERR)
				data->error = -EILSEQ;

			dev_dbg(host->dev, "%s: cmd=%d; blocks=%d",
				__func__, mrq->cmd->opcode, data->blocks);
			dev_dbg(host->dev, "data_error=%d xfer_size=%d\n",
				(int)data->error, data->bytes_xfered);
		}

		msdc_data_xfer_next(host, mrq);
		done = true;
	}
	return done;
}

static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
	u32 val = readl(host->base + SDC_CFG);

	val &= ~SDC_CFG_BUSWIDTH;

	switch (width) {
	default:
	case MMC_BUS_WIDTH_1:
		val |= (MSDC_BUS_1BITS << 16);
		break;
	case MMC_BUS_WIDTH_4:
		val |= (MSDC_BUS_4BITS << 16);
		break;
	case MMC_BUS_WIDTH_8:
		val |= (MSDC_BUS_8BITS << 16);
		break;
	}

	writel(val, host->base + SDC_CFG);
	dev_dbg(host->dev, "Bus Width = %d", width);
}

static int msdc_ops_switch_volt(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		if (ios->signal_voltage != MMC_SIGNAL_VOLTAGE_330 &&
		    ios->signal_voltage != MMC_SIGNAL_VOLTAGE_180) {
			dev_err(host->dev, "Unsupported signal voltage!\n");
			return -EINVAL;
		}

		ret = mmc_regulator_set_vqmmc(mmc, ios);
		if (ret < 0) {
			dev_dbg(host->dev, "Regulator set error %d (%d)\n",
				ret, ios->signal_voltage);
			return ret;
		}

		/* Apply different pinctrl settings for different signal voltage */
		if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			pinctrl_select_state(host->pinctrl, host->pins_uhs);
		else
			pinctrl_select_state(host->pinctrl, host->pins_default);
	}
	return 0;
}

static int msdc_card_busy(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 status = readl(host->base + MSDC_PS);

	/* only check if data0 is low */
	return !(status & BIT(16));
}

static void msdc_request_timeout(struct work_struct *work)
{
	struct msdc_host *host = container_of(work, struct msdc_host,
			req_timeout.work);

	/* simulate HW timeout status */
	dev_err(host->dev, "%s: aborting cmd/data/mrq\n", __func__);
	if (host->mrq) {
		dev_err(host->dev, "%s: aborting mrq=%p cmd=%d\n", __func__,
				host->mrq, host->mrq->cmd->opcode);
		if (host->cmd) {
			dev_err(host->dev, "%s: aborting cmd=%d\n",
					__func__, host->cmd->opcode);
			msdc_cmd_done(host, MSDC_INT_CMDTMO, host->mrq,
					host->cmd);
		} else if (host->data) {
			dev_err(host->dev, "%s: abort data: cmd%d; %d blocks\n",
					__func__, host->mrq->cmd->opcode,
					host->data->blocks);
			msdc_data_xfer_done(host, MSDC_INT_DATTMO, host->mrq,
					host->data);
		}
	}
}

static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb)
{
	if (enb) {
		sdr_set_bits(host->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
		if (host->dev_comp->recheck_sdio_irq)
			msdc_recheck_sdio_irq(host);
	} else {
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
	}
}

static void msdc_enable_sdio_irq(struct mmc_host *mmc, int enb)
{
	unsigned long flags;
	struct msdc_host *host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, enb);
	spin_unlock_irqrestore(&host->lock, flags);

	if (enb)
		pm_runtime_get_noresume(host->dev);
	else
		pm_runtime_put_noidle(host->dev);
}

static irqreturn_t msdc_cmdq_irq(struct msdc_host *host, u32 intsts)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	int cmd_err = 0, dat_err = 0;

	if (intsts & MSDC_INT_RSPCRCERR) {
		cmd_err = -EILSEQ;
		dev_err(host->dev, "%s: CMD CRC ERR", __func__);
	} else if (intsts & MSDC_INT_CMDTMO) {
		cmd_err = -ETIMEDOUT;
		dev_err(host->dev, "%s: CMD TIMEOUT ERR", __func__);
	}

	if (intsts & MSDC_INT_DATCRCERR) {
		dat_err = -EILSEQ;
		dev_err(host->dev, "%s: DATA CRC ERR", __func__);
	} else if (intsts & MSDC_INT_DATTMO) {
		dat_err = -ETIMEDOUT;
		dev_err(host->dev, "%s: DATA TIMEOUT ERR", __func__);
	}

	if (cmd_err || dat_err) {
		dev_err(host->dev, "cmd_err = %d, dat_err =%d, intsts = 0x%x",
			cmd_err, dat_err, intsts);
	}

	return cqhci_irq(mmc, 0, cmd_err, dat_err);
}

static irqreturn_t msdc_irq(int irq, void *dev_id)
{
	struct msdc_host *host = (struct msdc_host *) dev_id;
	struct mmc_host *mmc = mmc_from_priv(host);

	while (true) {
		struct mmc_request *mrq;
		struct mmc_command *cmd;
		struct mmc_data *data;
		u32 events, event_mask;

		spin_lock(&host->lock);
		events = readl(host->base + MSDC_INT);
		event_mask = readl(host->base + MSDC_INTEN);
		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			__msdc_enable_sdio_irq(host, 0);
		/* clear interrupts */
		writel(events & event_mask, host->base + MSDC_INT);

		mrq = host->mrq;
		cmd = host->cmd;
		data = host->data;
		spin_unlock(&host->lock);

		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			sdio_signal_irq(mmc);

		if ((events & event_mask) & MSDC_INT_CDSC) {
			if (host->internal_cd)
				mmc_detect_change(mmc, msecs_to_jiffies(20));
			events &= ~MSDC_INT_CDSC;
		}

		if (!(events & (event_mask & ~MSDC_INT_SDIOIRQ)))
			break;

		if ((mmc->caps2 & MMC_CAP2_CQE) &&
		    (events & MSDC_INT_CMDQ)) {
			msdc_cmdq_irq(host, events);
			/* clear interrupts */
			writel(events, host->base + MSDC_INT);
			return IRQ_HANDLED;
		}

		if (!mrq) {
			dev_err(host->dev,
				"%s: MRQ=NULL; events=%08X; event_mask=%08X\n",
				__func__, events, event_mask);
			WARN_ON(1);
			break;
		}

		dev_dbg(host->dev, "%s: events=%08X\n", __func__, events);

		if (cmd)
			msdc_cmd_done(host, events, mrq, cmd);
		else if (data)
			msdc_data_xfer_done(host, events, mrq, data);
	}

	return IRQ_HANDLED;
}

static void msdc_init_hw(struct msdc_host *host)
{
	u32 val;
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->reset) {
		reset_control_assert(host->reset);
		usleep_range(10, 50);
		reset_control_deassert(host->reset);
	}

	/* Configure to MMC/SD mode, clock free running */
	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_MODE | MSDC_CFG_CKPDN);

	/* Reset */
	msdc_reset_hw(host);

	/* Disable and clear all interrupts */
	writel(0, host->base + MSDC_INTEN);
	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);

	/* Configure card detection */
	if (host->internal_cd) {
		sdr_set_field(host->base + MSDC_PS, MSDC_PS_CDDEBOUNCE,
			      DEFAULT_DEBOUNCE);
		sdr_set_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_set_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
	} else {
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);
	}

	if (host->top_base) {
		writel(0, host->top_base + EMMC_TOP_CONTROL);
		writel(0, host->top_base + EMMC_TOP_CMD);
	} else {
		writel(0, host->base + tune_reg);
	}
	writel(0, host->base + MSDC_IOCON);
	sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DDLSEL, 0);
	writel(0x403c0046, host->base + MSDC_PATCH_BIT);
	sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_CKGEN_MSDC_DLY_SEL, 1);
	writel(0xffff4089, host->base + MSDC_PATCH_BIT1);
	sdr_set_bits(host->base + EMMC50_CFG0, EMMC50_CFG_CFCSTS_SEL);

	if (host->dev_comp->stop_clk_fix) {
		sdr_set_field(host->base + MSDC_PATCH_BIT1,
			      MSDC_PATCH_BIT1_STOP_DLY, 3);
		sdr_clr_bits(host->base + SDC_FIFO_CFG,
			     SDC_FIFO_CFG_WRVALIDSEL);
		sdr_clr_bits(host->base + SDC_FIFO_CFG,
			     SDC_FIFO_CFG_RDVALIDSEL);
	}

	if (host->dev_comp->busy_check)
		sdr_clr_bits(host->base + MSDC_PATCH_BIT1, (1 << 7));

	if (host->dev_comp->async_fifo) {
		sdr_set_field(host->base + MSDC_PATCH_BIT2,
			      MSDC_PB2_RESPWAIT, 3);
		if (host->dev_comp->enhance_rx) {
			if (host->top_base)
				sdr_set_bits(host->top_base + EMMC_TOP_CONTROL,
					     SDC_RX_ENH_EN);
			else
				sdr_set_bits(host->base + SDC_ADV_CFG0,
					     SDC_RX_ENHANCE_EN);
		} else {
			sdr_set_field(host->base + MSDC_PATCH_BIT2,
				      MSDC_PB2_RESPSTSENSEL, 2);
			sdr_set_field(host->base + MSDC_PATCH_BIT2,
				      MSDC_PB2_CRCSTSENSEL, 2);
		}
		/* use async fifo, then no need tune internal delay */
		sdr_clr_bits(host->base + MSDC_PATCH_BIT2,
			     MSDC_PATCH_BIT2_CFGRESP);
		sdr_set_bits(host->base + MSDC_PATCH_BIT2,
			     MSDC_PATCH_BIT2_CFGCRCSTS);
	}

	if (host->dev_comp->support_64g)
		sdr_set_bits(host->base + MSDC_PATCH_BIT2,
			     MSDC_PB2_SUPPORT_64G);
	if (host->dev_comp->data_tune) {
		if (host->top_base) {
			sdr_set_bits(host->top_base + EMMC_TOP_CONTROL,
				     PAD_DAT_RD_RXDLY_SEL);
			sdr_clr_bits(host->top_base + EMMC_TOP_CONTROL,
				     DATA_K_VALUE_SEL);
			sdr_set_bits(host->top_base + EMMC_TOP_CMD,
				     PAD_CMD_RD_RXDLY_SEL);
		} else {
			sdr_set_bits(host->base + tune_reg,
				     MSDC_PAD_TUNE_RD_SEL |
				     MSDC_PAD_TUNE_CMD_SEL);
		}
	} else {
		/* choose clock tune */
		if (host->top_base)
			sdr_set_bits(host->top_base + EMMC_TOP_CONTROL,
				     PAD_RXDLY_SEL);
		else
			sdr_set_bits(host->base + tune_reg,
				     MSDC_PAD_TUNE_RXDLYSEL);
	}

	/* Configure to enable SDIO mode.
	 * it's must otherwise sdio cmd5 failed
	 */
	sdr_set_bits(host->base + SDC_CFG, SDC_CFG_SDIO);

	/* Config SDIO device detect interrupt function */
	sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);
	sdr_set_bits(host->base + SDC_ADV_CFG0, SDC_DAT1_IRQ_TRIGGER);

	/* Configure to default data timeout */
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC, 3);

	host->def_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->saved_tune_para.iocon = readl(host->base + MSDC_IOCON);
	if (host->top_base) {
		host->def_tune_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->def_tune_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
		host->saved_tune_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
	} else {
		host->def_tune_para.pad_tune = readl(host->base + tune_reg);
		host->saved_tune_para.pad_tune = readl(host->base + tune_reg);
	}
	dev_dbg(host->dev, "init hardware done!");
}

static void msdc_deinit_hw(struct msdc_host *host)
{
	u32 val;

	if (host->internal_cd) {
		/* Disabled card-detect */
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
	}

	/* Disable and clear all interrupts */
	writel(0, host->base + MSDC_INTEN);

	val = readl(host->base + MSDC_INT);
	writel(val, host->base + MSDC_INT);
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
	struct mt_gpdma_desc *gpd = dma->gpd;
	struct mt_bdma_desc *bd = dma->bd;
	dma_addr_t dma_addr;
	int i;

	memset(gpd, 0, sizeof(struct mt_gpdma_desc) * 2);

	dma_addr = dma->gpd_addr + sizeof(struct mt_gpdma_desc);
	gpd->gpd_info = GPDMA_DESC_BDP; /* hwo, cs, bd pointer */
	/* gpd->next is must set for desc DMA
	 * That's why must alloc 2 gpd structure.
	 */
	gpd->next = lower_32_bits(dma_addr);
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;

	dma_addr = dma->bd_addr;
	gpd->ptr = lower_32_bits(dma->bd_addr); /* physical address */
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 28;

	memset(bd, 0, sizeof(struct mt_bdma_desc) * MAX_BD_NUM);
	for (i = 0; i < (MAX_BD_NUM - 1); i++) {
		dma_addr = dma->bd_addr + sizeof(*bd) * (i + 1);
		bd[i].next = lower_32_bits(dma_addr);
		if (host->dev_comp->support_64g)
			bd[i].bd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;
	}
}

static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	msdc_set_buswidth(host, ios->bus_width);

	/* Suspend/Resume will do power off/on */
	switch (ios->power_mode) {
	case MMC_POWER_UP:
		if (!IS_ERR(mmc->supply.vmmc)) {
			msdc_init_hw(host);
			ret = mmc_regulator_set_ocr(mmc, mmc->supply.vmmc,
					ios->vdd);
			if (ret) {
				dev_err(host->dev, "Failed to set vmmc power!\n");
				return;
			}
		}
		break;
	case MMC_POWER_ON:
		if (!IS_ERR(mmc->supply.vqmmc) && !host->vqmmc_enabled) {
			ret = regulator_enable(mmc->supply.vqmmc);
			if (ret)
				dev_err(host->dev, "Failed to set vqmmc power!\n");
			else
				host->vqmmc_enabled = true;
		}
		break;
	case MMC_POWER_OFF:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);

		if (!IS_ERR(mmc->supply.vqmmc) && host->vqmmc_enabled) {
			regulator_disable(mmc->supply.vqmmc);
			host->vqmmc_enabled = false;
		}
		break;
	default:
		break;
	}

	if (host->mclk != ios->clock || host->timing != ios->timing)
		msdc_set_mclk(host, ios->timing, ios->clock);
}

static u32 test_delay_bit(u32 delay, u32 bit)
{
	bit %= PAD_DELAY_MAX;
	return delay & (1 << bit);
}

static int get_delay_len(u32 delay, u32 start_bit)
{
	int i;

	for (i = 0; i < (PAD_DELAY_MAX - start_bit); i++) {
		if (test_delay_bit(delay, start_bit + i) == 0)
			return i;
	}
	return PAD_DELAY_MAX - start_bit;
}

static struct msdc_delay_phase get_best_delay(struct msdc_host *host, u32 delay)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xff;
	struct msdc_delay_phase delay_phase = { 0, };

	if (delay == 0) {
		dev_err(host->dev, "phase error: [map:%x]\n", delay);
		delay_phase.final_phase = final_phase;
		return delay_phase;
	}

	while (start < PAD_DELAY_MAX) {
		len = get_delay_len(delay, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
		if (len >= 12 && start_final < 4)
			break;
	}

	/* The rule is that to find the smallest delay cell */
	if (start_final == 0)
		final_phase = (start_final + len_final / 3) % PAD_DELAY_MAX;
	else
		final_phase = (start_final + len_final / 2) % PAD_DELAY_MAX;
	dev_info(host->dev, "phase: [map:%x] [maxlen:%d] [final:%d]\n",
		 delay, len_final, final_phase);

	delay_phase.maxlen = len_final;
	delay_phase.start = start_final;
	delay_phase.final_phase = final_phase;
	return delay_phase;
}

static inline void msdc_set_cmd_delay(struct msdc_host *host, u32 value)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->top_base)
		sdr_set_field(host->top_base + EMMC_TOP_CMD, PAD_CMD_RXDLY,
			      value);
	else
		sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_CMDRDLY,
			      value);
}

static inline void msdc_set_data_delay(struct msdc_host *host, u32 value)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->top_base)
		sdr_set_field(host->top_base + EMMC_TOP_CONTROL,
			      PAD_DAT_RD_RXDLY, value);
	else
		sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_DATRRDLY,
			      value);
}

static int msdc_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	struct msdc_delay_phase internal_delay_phase;
	u8 final_delay, final_maxlen;
	u32 internal_delay = 0;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	int cmd_err;
	int i, j;

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0 ; i < PAD_DELAY_MAX; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				rise_delay |= (1 << i);
			} else {
				rise_delay &= ~(1 << i);
				break;
			}
		}
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < PAD_DELAY_MAX; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				fall_delay |= (1 << i);
			} else {
				fall_delay &= ~(1 << i);
				break;
			}
		}
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_fall_delay.maxlen >= 12 && final_fall_delay.start < 4)
		final_maxlen = final_fall_delay.maxlen;
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_cmd_delay(host, final_delay);

	if (host->dev_comp->async_fifo || host->hs200_cmd_int_delay)
		goto skip_internal;

	for (i = 0; i < PAD_DELAY_MAX; i++) {
		sdr_set_field(host->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY, i);
		mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!cmd_err)
			internal_delay |= (1 << i);
	}
	dev_dbg(host->dev, "Final internal delay: 0x%x\n", internal_delay);
	internal_delay_phase = get_best_delay(host, internal_delay);
	sdr_set_field(host->base + tune_reg, MSDC_PAD_TUNE_CMDRRDLY,
		      internal_delay_phase.final_phase);
skip_internal:
	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int hs400_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 cmd_delay = 0;
	struct msdc_delay_phase final_cmd_delay = { 0,};
	u8 final_delay;
	int cmd_err;
	int i, j;

	/* select EMMC50 PAD CMD tune */
	sdr_set_bits(host->base + PAD_CMD_TUNE, BIT(0));
	sdr_set_field(host->base + MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMDTA, 2);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(host->base + MSDC_PAD_TUNE,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	if (host->hs400_cmd_resp_sel_rising)
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	else
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0 ; i < PAD_DELAY_MAX; i++) {
		sdr_set_field(host->base + PAD_CMD_TUNE,
			      PAD_CMD_TUNE_RX_DLY3, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				cmd_delay |= (1 << i);
			} else {
				cmd_delay &= ~(1 << i);
				break;
			}
		}
	}
	final_cmd_delay = get_best_delay(host, cmd_delay);
	sdr_set_field(host->base + PAD_CMD_TUNE, PAD_CMD_TUNE_RX_DLY3,
		      final_cmd_delay.final_phase);
	final_delay = final_cmd_delay.final_phase;

	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_tune_data(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);
	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
	for (i = 0 ; i < PAD_DELAY_MAX; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= (1 << i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
	for (i = 0; i < PAD_DELAY_MAX; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= (1 << i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_DSPL);
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_W_DSPL);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final data pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

/*
 * MSDC IP which supports data tune + async fifo can do CMD/DAT tune
 * together, which can save the tuning time.
 */
static int msdc_tune_together(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(host->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);

	sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	sdr_clr_bits(host->base + MSDC_IOCON,
		     MSDC_IOCON_DSPL | MSDC_IOCON_W_DSPL);
	for (i = 0 ; i < PAD_DELAY_MAX; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= (1 << i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	sdr_set_bits(host->base + MSDC_IOCON,
		     MSDC_IOCON_DSPL | MSDC_IOCON_W_DSPL);
	for (i = 0; i < PAD_DELAY_MAX; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= (1 << i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		sdr_clr_bits(host->base + MSDC_IOCON,
			     MSDC_IOCON_DSPL | MSDC_IOCON_W_DSPL);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(host->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		sdr_set_bits(host->base + MSDC_IOCON,
			     MSDC_IOCON_DSPL | MSDC_IOCON_W_DSPL);
		final_delay = final_fall_delay.final_phase;
	}

	msdc_set_cmd_delay(host, final_delay);
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (host->dev_comp->data_tune && host->dev_comp->async_fifo) {
		ret = msdc_tune_together(mmc, opcode);
		if (host->hs400_mode) {
			sdr_clr_bits(host->base + MSDC_IOCON,
				     MSDC_IOCON_DSPL | MSDC_IOCON_W_DSPL);
			msdc_set_data_delay(host, 0);
		}
		goto tune_done;
	}
	if (host->hs400_mode &&
	    host->dev_comp->hs400_tune)
		ret = hs400_tune_response(mmc, opcode);
	else
		ret = msdc_tune_response(mmc, opcode);
	if (ret == -EIO) {
		dev_err(host->dev, "Tune response fail!\n");
		return ret;
	}
	if (host->hs400_mode == false) {
		ret = msdc_tune_data(mmc, opcode);
		if (ret == -EIO)
			dev_err(host->dev, "Tune data fail!\n");
	}

tune_done:
	host->saved_tune_para.iocon = readl(host->base + MSDC_IOCON);
	host->saved_tune_para.pad_tune = readl(host->base + tune_reg);
	host->saved_tune_para.pad_cmd_tune = readl(host->base + PAD_CMD_TUNE);
	if (host->top_base) {
		host->saved_tune_para.emmc_top_control = readl(host->top_base +
				EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd = readl(host->top_base +
				EMMC_TOP_CMD);
	}
	return ret;
}

static int msdc_prepare_hs400_tuning(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	host->hs400_mode = true;

	if (host->top_base)
		writel(host->hs400_ds_delay,
		       host->top_base + EMMC50_PAD_DS_TUNE);
	else
		writel(host->hs400_ds_delay, host->base + PAD_DS_TUNE);
	/* hs400 mode must set it to 0 */
	sdr_clr_bits(host->base + MSDC_PATCH_BIT2, MSDC_PATCH_BIT2_CFGCRCSTS);
	/* to improve read performance, set outstanding to 2 */
	sdr_set_field(host->base + EMMC50_CFG3, EMMC50_CFG3_OUTS_WR, 2);

	return 0;
}

static void msdc_hw_reset(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);

	sdr_set_bits(host->base + EMMC_IOCON, 1);
	udelay(10); /* 10us is enough */
	sdr_clr_bits(host->base + EMMC_IOCON, 1);
}

static void msdc_ack_sdio_irq(struct mmc_host *mmc)
{
	unsigned long flags;
	struct msdc_host *host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, 1);
	spin_unlock_irqrestore(&host->lock, flags);
}

static int msdc_get_cd(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	int val;

	if (mmc->caps & MMC_CAP_NONREMOVABLE)
		return 1;

	if (!host->internal_cd)
		return mmc_gpio_get_cd(mmc);

	val = readl(host->base + MSDC_PS) & MSDC_PS_CDSTS;
	if (mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH)
		return !!val;
	else
		return !val;
}

static void msdc_hs400_enhanced_strobe(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);

	if (ios->enhanced_strobe) {
		msdc_prepare_hs400_tuning(mmc, ios);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 1);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 1);
		sdr_set_field(host->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 1);

		sdr_clr_bits(host->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_clr_bits(host->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_clr_bits(host->base + EMMC51_CFG0, CMDQ_RDAT_CNT);
	} else {
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 0);
		sdr_set_field(host->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 0);
		sdr_set_field(host->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 0);

		sdr_set_bits(host->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_set_bits(host->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_set_field(host->base + EMMC51_CFG0, CMDQ_RDAT_CNT, 0xb4);
	}
}

static void msdc_cqe_enable(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);

	/* enable cmdq irq */
	writel(MSDC_INT_CMDQ, host->base + MSDC_INTEN);
	/* enable busy check */
	sdr_set_bits(host->base + MSDC_PATCH_BIT1, MSDC_PB1_BUSY_CHECK_SEL);
	/* default write data / busy timeout 20s */
	msdc_set_busy_timeout(host, 20 * 1000000000ULL, 0);
	/* default read data timeout 1s */
	msdc_set_timeout(host, 1000000000ULL, 0);
}

static void msdc_cqe_disable(struct mmc_host *mmc, bool recovery)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned int val = 0;

	/* disable cmdq irq */
	sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INT_CMDQ);
	/* disable busy check */
	sdr_clr_bits(host->base + MSDC_PATCH_BIT1, MSDC_PB1_BUSY_CHECK_SEL);

	if (recovery) {
		sdr_set_field(host->base + MSDC_DMA_CTRL,
			      MSDC_DMA_CTRL_STOP, 1);
		if (WARN_ON(readl_poll_timeout(host->base + MSDC_DMA_CFG, val,
			!(val & MSDC_DMA_CFG_STS), 1, 3000)))
			return;
		msdc_reset_hw(host);
	}
}

static void msdc_cqe_pre_enable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;

	reg = cqhci_readl(cq_host, CQHCI_CFG);
	reg |= CQHCI_ENABLE;
	cqhci_writel(cq_host, reg, CQHCI_CFG);
}

static void msdc_cqe_post_disable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;

	reg = cqhci_readl(cq_host, CQHCI_CFG);
	reg &= ~CQHCI_ENABLE;
	cqhci_writel(cq_host, reg, CQHCI_CFG);
}

static const struct mmc_host_ops mt_msdc_ops = {
	.post_req = msdc_post_req,
	.pre_req = msdc_pre_req,
	.request = msdc_ops_request,
	.set_ios = msdc_ops_set_ios,
	.get_ro = mmc_gpio_get_ro,
	.get_cd = msdc_get_cd,
	.hs400_enhanced_strobe = msdc_hs400_enhanced_strobe,
	.enable_sdio_irq = msdc_enable_sdio_irq,
	.ack_sdio_irq = msdc_ack_sdio_irq,
	.start_signal_voltage_switch = msdc_ops_switch_volt,
	.card_busy = msdc_card_busy,
	.execute_tuning = msdc_execute_tuning,
	.prepare_hs400_tuning = msdc_prepare_hs400_tuning,
	.hw_reset = msdc_hw_reset,
};

static const struct cqhci_host_ops msdc_cmdq_ops = {
	.enable         = msdc_cqe_enable,
	.disable        = msdc_cqe_disable,
	.pre_enable = msdc_cqe_pre_enable,
	.post_disable = msdc_cqe_post_disable,
};

static void msdc_of_property_parse(struct platform_device *pdev,
				   struct msdc_host *host)
{
	of_property_read_u32(pdev->dev.of_node, "mediatek,latch-ck",
			     &host->latch_ck);

	of_property_read_u32(pdev->dev.of_node, "hs400-ds-delay",
			     &host->hs400_ds_delay);

	of_property_read_u32(pdev->dev.of_node, "mediatek,hs200-cmd-int-delay",
			     &host->hs200_cmd_int_delay);

	of_property_read_u32(pdev->dev.of_node, "mediatek,hs400-cmd-int-delay",
			     &host->hs400_cmd_int_delay);

	if (of_property_read_bool(pdev->dev.of_node,
				  "mediatek,hs400-cmd-resp-sel-rising"))
		host->hs400_cmd_resp_sel_rising = true;
	else
		host->hs400_cmd_resp_sel_rising = false;

	if (of_property_read_bool(pdev->dev.of_node,
				  "supports-cqe"))
		host->cqhci = true;
	else
		host->cqhci = false;
}

static int msdc_of_clock_parse(struct platform_device *pdev,
			       struct msdc_host *host)
{
	int ret;

	host->src_clk = devm_clk_get(&pdev->dev, "source");
	if (IS_ERR(host->src_clk))
		return PTR_ERR(host->src_clk);

	host->h_clk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(host->h_clk))
		return PTR_ERR(host->h_clk);

	host->bus_clk = devm_clk_get_optional(&pdev->dev, "bus_clk");
	if (IS_ERR(host->bus_clk))
		host->bus_clk = NULL;

	/*source clock control gate is optional clock*/
	host->src_clk_cg = devm_clk_get_optional(&pdev->dev, "source_cg");
	if (IS_ERR(host->src_clk_cg))
		host->src_clk_cg = NULL;

	host->sys_clk_cg = devm_clk_get_optional(&pdev->dev, "sys_cg");
	if (IS_ERR(host->sys_clk_cg))
		host->sys_clk_cg = NULL;

	/* If present, always enable for this clock gate */
	clk_prepare_enable(host->sys_clk_cg);

	host->bulk_clks[0].id = "pclk_cg";
	host->bulk_clks[1].id = "axi_cg";
	host->bulk_clks[2].id = "ahb_cg";
	ret = devm_clk_bulk_get_optional(&pdev->dev, MSDC_NR_CLOCKS,
					 host->bulk_clks);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get pclk/axi/ahb clock gates\n");
		return ret;
	}

	return 0;
}

static int msdc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;
	struct resource *res;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	/* Allocate MMC host for this device */
	mmc = mmc_alloc_host(sizeof(struct msdc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	ret = mmc_of_parse(mmc);
	if (ret)
		goto host_free;

	host->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto host_free;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		host->top_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(host->top_base))
			host->top_base = NULL;
	}

	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		goto host_free;

	ret = msdc_of_clock_parse(pdev, host);
	if (ret)
		goto host_free;

	host->reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								"hrst");
	if (IS_ERR(host->reset)) {
		ret = PTR_ERR(host->reset);
		goto host_free;
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = -EINVAL;
		goto host_free;
	}

	host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(host->pinctrl)) {
		ret = PTR_ERR(host->pinctrl);
		dev_err(&pdev->dev, "Cannot find pinctrl!\n");
		goto host_free;
	}

	host->pins_default = pinctrl_lookup_state(host->pinctrl, "default");
	if (IS_ERR(host->pins_default)) {
		ret = PTR_ERR(host->pins_default);
		dev_err(&pdev->dev, "Cannot find pinctrl default!\n");
		goto host_free;
	}

	host->pins_uhs = pinctrl_lookup_state(host->pinctrl, "state_uhs");
	if (IS_ERR(host->pins_uhs)) {
		ret = PTR_ERR(host->pins_uhs);
		dev_err(&pdev->dev, "Cannot find pinctrl uhs!\n");
		goto host_free;
	}

	msdc_of_property_parse(pdev, host);

	host->dev = &pdev->dev;
	host->dev_comp = of_device_get_match_data(&pdev->dev);
	host->src_clk_freq = clk_get_rate(host->src_clk);
	/* Set host parameters to mmc */
	mmc->ops = &mt_msdc_ops;
	if (host->dev_comp->clk_div_bits == 8)
		mmc->f_min = DIV_ROUND_UP(host->src_clk_freq, 4 * 255);
	else
		mmc->f_min = DIV_ROUND_UP(host->src_clk_freq, 4 * 4095);

	if (!(mmc->caps & MMC_CAP_NONREMOVABLE) &&
	    !mmc_can_gpio_cd(mmc) &&
	    host->dev_comp->use_internal_cd) {
		/*
		 * Is removable but no GPIO declared, so
		 * use internal functionality.
		 */
		host->internal_cd = true;
	}

	if (mmc->caps & MMC_CAP_SDIO_IRQ)
		mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	mmc->caps |= MMC_CAP_CMD23;
	if (host->cqhci)
		mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;
	/* MMC core transfer sizes tunable parameters */
	mmc->max_segs = MAX_BD_NUM;
	if (host->dev_comp->support_64g)
		mmc->max_seg_size = BDMA_DESC_BUFLEN_EXT;
	else
		mmc->max_seg_size = BDMA_DESC_BUFLEN;
	mmc->max_blk_size = 2048;
	mmc->max_req_size = 512 * 1024;
	mmc->max_blk_count = mmc->max_req_size / 512;
	if (host->dev_comp->support_64g)
		host->dma_mask = DMA_BIT_MASK(36);
	else
		host->dma_mask = DMA_BIT_MASK(32);
	mmc_dev(mmc)->dma_mask = &host->dma_mask;

	host->timeout_clks = 3 * 1048576;
	host->dma.gpd = dma_alloc_coherent(&pdev->dev,
				2 * sizeof(struct mt_gpdma_desc),
				&host->dma.gpd_addr, GFP_KERNEL);
	host->dma.bd = dma_alloc_coherent(&pdev->dev,
				MAX_BD_NUM * sizeof(struct mt_bdma_desc),
				&host->dma.bd_addr, GFP_KERNEL);
	if (!host->dma.gpd || !host->dma.bd) {
		ret = -ENOMEM;
		goto release_mem;
	}
	msdc_init_gpd_bd(host, &host->dma);
	INIT_DELAYED_WORK(&host->req_timeout, msdc_request_timeout);
	spin_lock_init(&host->lock);

	platform_set_drvdata(pdev, mmc);
	msdc_ungate_clock(host);
	msdc_init_hw(host);

	if (mmc->caps2 & MMC_CAP2_CQE) {
		host->cq_host = devm_kzalloc(mmc->parent,
					     sizeof(*host->cq_host),
					     GFP_KERNEL);
		if (!host->cq_host) {
			ret = -ENOMEM;
			goto host_free;
		}
		host->cq_host->caps |= CQHCI_TASK_DESC_SZ_128;
		host->cq_host->mmio = host->base + 0x800;
		host->cq_host->ops = &msdc_cmdq_ops;
		ret = cqhci_init(host->cq_host, mmc, true);
		if (ret)
			goto host_free;
		mmc->max_segs = 128;
		/* cqhci 16bit length */
		/* 0 size, means 65536 so we don't have to -1 here */
		mmc->max_seg_size = 64 * 1024;
	}

	ret = devm_request_irq(&pdev->dev, host->irq, msdc_irq,
			       IRQF_TRIGGER_NONE, pdev->name, host);
	if (ret)
		goto release;

	pm_runtime_set_active(host->dev);
	pm_runtime_set_autosuspend_delay(host->dev, MTK_MMC_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(host->dev);
	pm_runtime_enable(host->dev);
	ret = mmc_add_host(mmc);

	if (ret)
		goto end;

	return 0;
end:
	pm_runtime_disable(host->dev);
release:
	platform_set_drvdata(pdev, NULL);
	msdc_deinit_hw(host);
	msdc_gate_clock(host);
release_mem:
	if (host->dma.gpd)
		dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	if (host->dma.bd)
		dma_free_coherent(&pdev->dev,
			MAX_BD_NUM * sizeof(struct mt_bdma_desc),
			host->dma.bd, host->dma.bd_addr);
host_free:
	mmc_free_host(mmc);

	return ret;
}

static int msdc_drv_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;

	mmc = platform_get_drvdata(pdev);
	host = mmc_priv(mmc);

	pm_runtime_get_sync(host->dev);

	platform_set_drvdata(pdev, NULL);
	mmc_remove_host(mmc);
	msdc_deinit_hw(host);
	msdc_gate_clock(host);

	pm_runtime_disable(host->dev);
	pm_runtime_put_noidle(host->dev);
	dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	dma_free_coherent(&pdev->dev, MAX_BD_NUM * sizeof(struct mt_bdma_desc),
			host->dma.bd, host->dma.bd_addr);

	mmc_free_host(mmc);

	return 0;
}

static void msdc_save_reg(struct msdc_host *host)
{
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	host->save_para.msdc_cfg = readl(host->base + MSDC_CFG);
	host->save_para.iocon = readl(host->base + MSDC_IOCON);
	host->save_para.sdc_cfg = readl(host->base + SDC_CFG);
	host->save_para.patch_bit0 = readl(host->base + MSDC_PATCH_BIT);
	host->save_para.patch_bit1 = readl(host->base + MSDC_PATCH_BIT1);
	host->save_para.patch_bit2 = readl(host->base + MSDC_PATCH_BIT2);
	host->save_para.pad_ds_tune = readl(host->base + PAD_DS_TUNE);
	host->save_para.pad_cmd_tune = readl(host->base + PAD_CMD_TUNE);
	host->save_para.emmc50_cfg0 = readl(host->base + EMMC50_CFG0);
	host->save_para.emmc50_cfg3 = readl(host->base + EMMC50_CFG3);
	host->save_para.sdc_fifo_cfg = readl(host->base + SDC_FIFO_CFG);
	if (host->top_base) {
		host->save_para.emmc_top_control =
			readl(host->top_base + EMMC_TOP_CONTROL);
		host->save_para.emmc_top_cmd =
			readl(host->top_base + EMMC_TOP_CMD);
		host->save_para.emmc50_pad_ds_tune =
			readl(host->top_base + EMMC50_PAD_DS_TUNE);
	} else {
		host->save_para.pad_tune = readl(host->base + tune_reg);
	}
}

static void msdc_restore_reg(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	writel(host->save_para.msdc_cfg, host->base + MSDC_CFG);
	writel(host->save_para.iocon, host->base + MSDC_IOCON);
	writel(host->save_para.sdc_cfg, host->base + SDC_CFG);
	writel(host->save_para.patch_bit0, host->base + MSDC_PATCH_BIT);
	writel(host->save_para.patch_bit1, host->base + MSDC_PATCH_BIT1);
	writel(host->save_para.patch_bit2, host->base + MSDC_PATCH_BIT2);
	writel(host->save_para.pad_ds_tune, host->base + PAD_DS_TUNE);
	writel(host->save_para.pad_cmd_tune, host->base + PAD_CMD_TUNE);
	writel(host->save_para.emmc50_cfg0, host->base + EMMC50_CFG0);
	writel(host->save_para.emmc50_cfg3, host->base + EMMC50_CFG3);
	writel(host->save_para.sdc_fifo_cfg, host->base + SDC_FIFO_CFG);
	if (host->top_base) {
		writel(host->save_para.emmc_top_control,
		       host->top_base + EMMC_TOP_CONTROL);
		writel(host->save_para.emmc_top_cmd,
		       host->top_base + EMMC_TOP_CMD);
		writel(host->save_para.emmc50_pad_ds_tune,
		       host->top_base + EMMC50_PAD_DS_TUNE);
	} else {
		writel(host->save_para.pad_tune, host->base + tune_reg);
	}

	if (sdio_irq_claimed(mmc))
		__msdc_enable_sdio_irq(host, 1);
}

static int __maybe_unused msdc_runtime_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);

	msdc_save_reg(host);
	msdc_gate_clock(host);
	return 0;
}

static int __maybe_unused msdc_runtime_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct msdc_host *host = mmc_priv(mmc);

	msdc_ungate_clock(host);
	msdc_restore_reg(host);
	return 0;
}

static int __maybe_unused msdc_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	int ret;

	if (mmc->caps2 & MMC_CAP2_CQE) {
		ret = cqhci_suspend(mmc);
		if (ret)
			return ret;
	}

	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused msdc_resume(struct device *dev)
{
	return pm_runtime_force_resume(dev);
}

static const struct dev_pm_ops msdc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msdc_suspend, msdc_resume)
	SET_RUNTIME_PM_OPS(msdc_runtime_suspend, msdc_runtime_resume, NULL)
};

static struct platform_driver mt_msdc_driver = {
	.probe = msdc_drv_probe,
	.remove = msdc_drv_remove,
	.driver = {
		.name = "mtk-msdc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = msdc_of_ids,
		.pm = &msdc_dev_pm_ops,
	},
};

module_platform_driver(mt_msdc_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek SD/MMC Card Driver");

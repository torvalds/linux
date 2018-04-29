/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#ifndef MT6575_SD_H
#define MT6575_SD_H

#include <linux/bitops.h>
#include <linux/mmc/host.h>

// #include <mach/mt6575_reg_base.h> /* --- by chhung */

/*--------------------------------------------------------------------------*/
/* Common Macro                                                             */
/*--------------------------------------------------------------------------*/
#define REG_ADDR(x)                 (base + OFFSET_##x)

/*--------------------------------------------------------------------------*/
/* Common Definition                                                        */
/*--------------------------------------------------------------------------*/
#define MSDC_FIFO_SZ            (128)
#define MSDC_FIFO_THD           (64)  // (128)
#define MSDC_NUM                (4)

#define MSDC_MS                 (0)
#define MSDC_SDMMC              (1)

#define MSDC_MODE_UNKNOWN       (0)
#define MSDC_MODE_PIO           (1)
#define MSDC_MODE_DMA_BASIC     (2)
#define MSDC_MODE_DMA_DESC      (3)
#define MSDC_MODE_DMA_ENHANCED  (4)
#define MSDC_MODE_MMC_STREAM    (5)

#define MSDC_BUS_1BITS          (0)
#define MSDC_BUS_4BITS          (1)
#define MSDC_BUS_8BITS          (2)

#define MSDC_BRUST_8B           (3)
#define MSDC_BRUST_16B          (4)
#define MSDC_BRUST_32B          (5)
#define MSDC_BRUST_64B          (6)

#define MSDC_PIN_PULL_NONE      (0)
#define MSDC_PIN_PULL_DOWN      (1)
#define MSDC_PIN_PULL_UP        (2)
#define MSDC_PIN_KEEP           (3)

#define MSDC_MAX_SCLK           (48000000) /* +/- by chhung */
#define MSDC_MIN_SCLK           (260000)

#define MSDC_AUTOCMD12          (0x0001)
#define MSDC_AUTOCMD23          (0x0002)
#define MSDC_AUTOCMD19          (0x0003)

#define MSDC_EMMC_BOOTMODE0     (0)     /* Pull low CMD mode */
#define MSDC_EMMC_BOOTMODE1     (1)     /* Reset CMD mode */

enum {
	RESP_NONE = 0,
	RESP_R1,
	RESP_R2,
	RESP_R3,
	RESP_R4,
	RESP_R5,
	RESP_R6,
	RESP_R7,
	RESP_R1B
};

/*--------------------------------------------------------------------------*/
/* Register Offset                                                          */
/*--------------------------------------------------------------------------*/
#define OFFSET_MSDC_CFG         (0x0)
#define OFFSET_MSDC_IOCON       (0x04)
#define OFFSET_MSDC_PS          (0x08)
#define OFFSET_MSDC_INT         (0x0c)
#define OFFSET_MSDC_INTEN       (0x10)
#define OFFSET_MSDC_FIFOCS      (0x14)
#define OFFSET_MSDC_TXDATA      (0x18)
#define OFFSET_MSDC_RXDATA      (0x1c)
#define OFFSET_SDC_CFG          (0x30)
#define OFFSET_SDC_CMD          (0x34)
#define OFFSET_SDC_ARG          (0x38)
#define OFFSET_SDC_STS          (0x3c)
#define OFFSET_SDC_RESP0        (0x40)
#define OFFSET_SDC_RESP1        (0x44)
#define OFFSET_SDC_RESP2        (0x48)
#define OFFSET_SDC_RESP3        (0x4c)
#define OFFSET_SDC_BLK_NUM      (0x50)
#define OFFSET_SDC_CSTS         (0x58)
#define OFFSET_SDC_CSTS_EN      (0x5c)
#define OFFSET_SDC_DCRC_STS     (0x60)
#define OFFSET_EMMC_CFG0        (0x70)
#define OFFSET_EMMC_CFG1        (0x74)
#define OFFSET_EMMC_STS         (0x78)
#define OFFSET_EMMC_IOCON       (0x7c)
#define OFFSET_SDC_ACMD_RESP    (0x80)
#define OFFSET_SDC_ACMD19_TRG   (0x84)
#define OFFSET_SDC_ACMD19_STS   (0x88)
#define OFFSET_MSDC_DMA_SA      (0x90)
#define OFFSET_MSDC_DMA_CA      (0x94)
#define OFFSET_MSDC_DMA_CTRL    (0x98)
#define OFFSET_MSDC_DMA_CFG     (0x9c)
#define OFFSET_MSDC_DBG_SEL     (0xa0)
#define OFFSET_MSDC_DBG_OUT     (0xa4)
#define OFFSET_MSDC_PATCH_BIT   (0xb0)
#define OFFSET_MSDC_PATCH_BIT1  (0xb4)
#define OFFSET_MSDC_PAD_CTL0    (0xe0)
#define OFFSET_MSDC_PAD_CTL1    (0xe4)
#define OFFSET_MSDC_PAD_CTL2    (0xe8)
#define OFFSET_MSDC_PAD_TUNE    (0xec)
#define OFFSET_MSDC_DAT_RDDLY0  (0xf0)
#define OFFSET_MSDC_DAT_RDDLY1  (0xf4)
#define OFFSET_MSDC_HW_DBG      (0xf8)
#define OFFSET_MSDC_VERSION     (0x100)
#define OFFSET_MSDC_ECO_VER     (0x104)

/*--------------------------------------------------------------------------*/
/* Register Address                                                         */
/*--------------------------------------------------------------------------*/

/* common register */
#define MSDC_CFG                REG_ADDR(MSDC_CFG)
#define MSDC_IOCON              REG_ADDR(MSDC_IOCON)
#define MSDC_PS                 REG_ADDR(MSDC_PS)
#define MSDC_INT                REG_ADDR(MSDC_INT)
#define MSDC_INTEN              REG_ADDR(MSDC_INTEN)
#define MSDC_FIFOCS             REG_ADDR(MSDC_FIFOCS)
#define MSDC_TXDATA             REG_ADDR(MSDC_TXDATA)
#define MSDC_RXDATA             REG_ADDR(MSDC_RXDATA)
#define MSDC_PATCH_BIT0         REG_ADDR(MSDC_PATCH_BIT)

/* sdmmc register */
#define SDC_CFG                 REG_ADDR(SDC_CFG)
#define SDC_CMD                 REG_ADDR(SDC_CMD)
#define SDC_ARG                 REG_ADDR(SDC_ARG)
#define SDC_STS                 REG_ADDR(SDC_STS)
#define SDC_RESP0               REG_ADDR(SDC_RESP0)
#define SDC_RESP1               REG_ADDR(SDC_RESP1)
#define SDC_RESP2               REG_ADDR(SDC_RESP2)
#define SDC_RESP3               REG_ADDR(SDC_RESP3)
#define SDC_BLK_NUM             REG_ADDR(SDC_BLK_NUM)
#define SDC_CSTS                REG_ADDR(SDC_CSTS)
#define SDC_CSTS_EN             REG_ADDR(SDC_CSTS_EN)
#define SDC_DCRC_STS            REG_ADDR(SDC_DCRC_STS)

/* emmc register*/
#define EMMC_CFG0               REG_ADDR(EMMC_CFG0)
#define EMMC_CFG1               REG_ADDR(EMMC_CFG1)
#define EMMC_STS                REG_ADDR(EMMC_STS)
#define EMMC_IOCON              REG_ADDR(EMMC_IOCON)

/* auto command register */
#define SDC_ACMD_RESP           REG_ADDR(SDC_ACMD_RESP)
#define SDC_ACMD19_TRG          REG_ADDR(SDC_ACMD19_TRG)
#define SDC_ACMD19_STS          REG_ADDR(SDC_ACMD19_STS)

/* dma register */
#define MSDC_DMA_SA             REG_ADDR(MSDC_DMA_SA)
#define MSDC_DMA_CA             REG_ADDR(MSDC_DMA_CA)
#define MSDC_DMA_CTRL           REG_ADDR(MSDC_DMA_CTRL)
#define MSDC_DMA_CFG            REG_ADDR(MSDC_DMA_CFG)

/* pad ctrl register */
#define MSDC_PAD_CTL0           REG_ADDR(MSDC_PAD_CTL0)
#define MSDC_PAD_CTL1           REG_ADDR(MSDC_PAD_CTL1)
#define MSDC_PAD_CTL2           REG_ADDR(MSDC_PAD_CTL2)

/* data read delay */
#define MSDC_DAT_RDDLY0         REG_ADDR(MSDC_DAT_RDDLY0)
#define MSDC_DAT_RDDLY1         REG_ADDR(MSDC_DAT_RDDLY1)

/* debug register */
#define MSDC_DBG_SEL            REG_ADDR(MSDC_DBG_SEL)
#define MSDC_DBG_OUT            REG_ADDR(MSDC_DBG_OUT)

/* misc register */
#define MSDC_PATCH_BIT          REG_ADDR(MSDC_PATCH_BIT)
#define MSDC_PATCH_BIT1         REG_ADDR(MSDC_PATCH_BIT1)
#define MSDC_PAD_TUNE           REG_ADDR(MSDC_PAD_TUNE)
#define MSDC_HW_DBG             REG_ADDR(MSDC_HW_DBG)
#define MSDC_VERSION            REG_ADDR(MSDC_VERSION)
#define MSDC_ECO_VER            REG_ADDR(MSDC_ECO_VER) /* ECO Version */

/*--------------------------------------------------------------------------*/
/* Register Mask                                                            */
/*--------------------------------------------------------------------------*/

/* MSDC_CFG mask */
#define MSDC_CFG_MODE           (0x1  << 0)     /* RW */
#define MSDC_CFG_CKPDN          (0x1  << 1)     /* RW */
#define MSDC_CFG_RST            (0x1  << 2)     /* RW */
#define MSDC_CFG_PIO            (0x1  << 3)     /* RW */
#define MSDC_CFG_CKDRVEN        (0x1  << 4)     /* RW */
#define MSDC_CFG_BV18SDT        (0x1  << 5)     /* RW */
#define MSDC_CFG_BV18PSS        (0x1  << 6)     /* R  */
#define MSDC_CFG_CKSTB          (0x1  << 7)     /* R  */
#define MSDC_CFG_CKDIV          (0xff << 8)     /* RW */
#define MSDC_CFG_CKMOD          (0x3  << 16)    /* RW */

/* MSDC_IOCON mask */
#define MSDC_IOCON_SDR104CKS    (0x1  << 0)     /* RW */
#define MSDC_IOCON_RSPL         (0x1  << 1)     /* RW */
#define MSDC_IOCON_DSPL         (0x1  << 2)     /* RW */
#define MSDC_IOCON_DDLSEL       (0x1  << 3)     /* RW */
#define MSDC_IOCON_DDR50CKD     (0x1  << 4)     /* RW */
#define MSDC_IOCON_DSPLSEL      (0x1  << 5)     /* RW */
#define MSDC_IOCON_D0SPL        (0x1  << 16)    /* RW */
#define MSDC_IOCON_D1SPL        (0x1  << 17)    /* RW */
#define MSDC_IOCON_D2SPL        (0x1  << 18)    /* RW */
#define MSDC_IOCON_D3SPL        (0x1  << 19)    /* RW */
#define MSDC_IOCON_D4SPL        (0x1  << 20)    /* RW */
#define MSDC_IOCON_D5SPL        (0x1  << 21)    /* RW */
#define MSDC_IOCON_D6SPL        (0x1  << 22)    /* RW */
#define MSDC_IOCON_D7SPL        (0x1  << 23)    /* RW */
#define MSDC_IOCON_RISCSZ       (0x3  << 24)    /* RW */

/* MSDC_PS mask */
#define MSDC_PS_CDEN            (0x1  << 0)     /* RW */
#define MSDC_PS_CDSTS           (0x1  << 1)     /* R  */
#define MSDC_PS_CDDEBOUNCE      (0xf  << 12)    /* RW */
#define MSDC_PS_DAT             (0xff << 16)    /* R  */
#define MSDC_PS_CMD             (0x1  << 24)    /* R  */
#define MSDC_PS_WP              (0x1UL << 31)    /* R  */

/* MSDC_INT mask */
#define MSDC_INT_MMCIRQ         (0x1  << 0)     /* W1C */
#define MSDC_INT_CDSC           (0x1  << 1)     /* W1C */
#define MSDC_INT_ACMDRDY        (0x1  << 3)     /* W1C */
#define MSDC_INT_ACMDTMO        (0x1  << 4)     /* W1C */
#define MSDC_INT_ACMDCRCERR     (0x1  << 5)     /* W1C */
#define MSDC_INT_DMAQ_EMPTY     (0x1  << 6)     /* W1C */
#define MSDC_INT_SDIOIRQ        (0x1  << 7)     /* W1C */
#define MSDC_INT_CMDRDY         (0x1  << 8)     /* W1C */
#define MSDC_INT_CMDTMO         (0x1  << 9)     /* W1C */
#define MSDC_INT_RSPCRCERR      (0x1  << 10)    /* W1C */
#define MSDC_INT_CSTA           (0x1  << 11)    /* R */
#define MSDC_INT_XFER_COMPL     (0x1  << 12)    /* W1C */
#define MSDC_INT_DXFER_DONE     (0x1  << 13)    /* W1C */
#define MSDC_INT_DATTMO         (0x1  << 14)    /* W1C */
#define MSDC_INT_DATCRCERR      (0x1  << 15)    /* W1C */
#define MSDC_INT_ACMD19_DONE    (0x1  << 16)    /* W1C */

/* MSDC_INTEN mask */
#define MSDC_INTEN_MMCIRQ       (0x1  << 0)     /* RW */
#define MSDC_INTEN_CDSC         (0x1  << 1)     /* RW */
#define MSDC_INTEN_ACMDRDY      (0x1  << 3)     /* RW */
#define MSDC_INTEN_ACMDTMO      (0x1  << 4)     /* RW */
#define MSDC_INTEN_ACMDCRCERR   (0x1  << 5)     /* RW */
#define MSDC_INTEN_DMAQ_EMPTY   (0x1  << 6)     /* RW */
#define MSDC_INTEN_SDIOIRQ      (0x1  << 7)     /* RW */
#define MSDC_INTEN_CMDRDY       (0x1  << 8)     /* RW */
#define MSDC_INTEN_CMDTMO       (0x1  << 9)     /* RW */
#define MSDC_INTEN_RSPCRCERR    (0x1  << 10)    /* RW */
#define MSDC_INTEN_CSTA         (0x1  << 11)    /* RW */
#define MSDC_INTEN_XFER_COMPL   (0x1  << 12)    /* RW */
#define MSDC_INTEN_DXFER_DONE   (0x1  << 13)    /* RW */
#define MSDC_INTEN_DATTMO       (0x1  << 14)    /* RW */
#define MSDC_INTEN_DATCRCERR    (0x1  << 15)    /* RW */
#define MSDC_INTEN_ACMD19_DONE  (0x1  << 16)    /* RW */

/* MSDC_FIFOCS mask */
#define MSDC_FIFOCS_RXCNT       (0xff << 0)     /* R */
#define MSDC_FIFOCS_TXCNT       (0xff << 16)    /* R */
#define MSDC_FIFOCS_CLR         (0x1UL << 31)    /* RW */

/* SDC_CFG mask */
#define SDC_CFG_SDIOINTWKUP     (0x1  << 0)     /* RW */
#define SDC_CFG_INSWKUP         (0x1  << 1)     /* RW */
#define SDC_CFG_BUSWIDTH        (0x3  << 16)    /* RW */
#define SDC_CFG_SDIO            (0x1  << 19)    /* RW */
#define SDC_CFG_SDIOIDE         (0x1  << 20)    /* RW */
#define SDC_CFG_INTATGAP        (0x1  << 21)    /* RW */
#define SDC_CFG_DTOC            (0xffUL << 24)  /* RW */

/* SDC_CMD mask */
#define SDC_CMD_OPC             (0x3f << 0)     /* RW */
#define SDC_CMD_BRK             (0x1  << 6)     /* RW */
#define SDC_CMD_RSPTYP          (0x7  << 7)     /* RW */
#define SDC_CMD_DTYP            (0x3  << 11)    /* RW */
#define SDC_CMD_DTYP            (0x3  << 11)    /* RW */
#define SDC_CMD_RW              (0x1  << 13)    /* RW */
#define SDC_CMD_STOP            (0x1  << 14)    /* RW */
#define SDC_CMD_GOIRQ           (0x1  << 15)    /* RW */
#define SDC_CMD_BLKLEN          (0xfff << 16)    /* RW */
#define SDC_CMD_AUTOCMD         (0x3  << 28)    /* RW */
#define SDC_CMD_VOLSWTH         (0x1  << 30)    /* RW */

/* SDC_STS mask */
#define SDC_STS_SDCBUSY         (0x1  << 0)     /* RW */
#define SDC_STS_CMDBUSY         (0x1  << 1)     /* RW */
#define SDC_STS_SWR_COMPL       (0x1  << 31)    /* RW */

/* SDC_DCRC_STS mask */
#define SDC_DCRC_STS_NEG        (0xf  << 8)     /* RO */
#define SDC_DCRC_STS_POS        (0xff << 0)     /* RO */

/* EMMC_CFG0 mask */
#define EMMC_CFG0_BOOTSTART     (0x1  << 0)     /* W */
#define EMMC_CFG0_BOOTSTOP      (0x1  << 1)     /* W */
#define EMMC_CFG0_BOOTMODE      (0x1  << 2)     /* RW */
#define EMMC_CFG0_BOOTACKDIS    (0x1  << 3)     /* RW */
#define EMMC_CFG0_BOOTWDLY      (0x7  << 12)    /* RW */
#define EMMC_CFG0_BOOTSUPP      (0x1  << 15)    /* RW */

/* EMMC_CFG1 mask */
#define EMMC_CFG1_BOOTDATTMC    (0xfffff << 0)  /* RW */
#define EMMC_CFG1_BOOTACKTMC    (0xfffUL << 20) /* RW */

/* EMMC_STS mask */
#define EMMC_STS_BOOTCRCERR     (0x1  << 0)     /* W1C */
#define EMMC_STS_BOOTACKERR     (0x1  << 1)     /* W1C */
#define EMMC_STS_BOOTDATTMO     (0x1  << 2)     /* W1C */
#define EMMC_STS_BOOTACKTMO     (0x1  << 3)     /* W1C */
#define EMMC_STS_BOOTUPSTATE    (0x1  << 4)     /* R */
#define EMMC_STS_BOOTACKRCV     (0x1  << 5)     /* W1C */
#define EMMC_STS_BOOTDATRCV     (0x1  << 6)     /* R */

/* EMMC_IOCON mask */
#define EMMC_IOCON_BOOTRST      (0x1  << 0)     /* RW */

/* SDC_ACMD19_TRG mask */
#define SDC_ACMD19_TRG_TUNESEL  (0xf  << 0)     /* RW */

/* MSDC_DMA_CTRL mask */
#define MSDC_DMA_CTRL_START     (0x1  << 0)     /* W */
#define MSDC_DMA_CTRL_STOP      (0x1  << 1)     /* W */
#define MSDC_DMA_CTRL_RESUME    (0x1  << 2)     /* W */
#define MSDC_DMA_CTRL_MODE      (0x1  << 8)     /* RW */
#define MSDC_DMA_CTRL_LASTBUF   (0x1  << 10)    /* RW */
#define MSDC_DMA_CTRL_BRUSTSZ   (0x7  << 12)    /* RW */
#define MSDC_DMA_CTRL_XFERSZ    (0xffffUL << 16)/* RW */

/* MSDC_DMA_CFG mask */
#define MSDC_DMA_CFG_STS        (0x1  << 0)     /* R */
#define MSDC_DMA_CFG_DECSEN     (0x1  << 1)     /* RW */
#define MSDC_DMA_CFG_BDCSERR    (0x1  << 4)     /* R */
#define MSDC_DMA_CFG_GPDCSERR   (0x1  << 5)     /* R */

/* MSDC_PATCH_BIT mask */
#define MSDC_PATCH_BIT_WFLSMODE (0x1  << 0)     /* RW */
#define MSDC_PATCH_BIT_ODDSUPP  (0x1  << 1)     /* RW */
#define MSDC_PATCH_BIT_CKGEN_CK (0x1  << 6)     /* E2: Fixed to 1 */
#define MSDC_PATCH_BIT_IODSSEL  (0x1  << 16)    /* RW */
#define MSDC_PATCH_BIT_IOINTSEL (0x1  << 17)    /* RW */
#define MSDC_PATCH_BIT_BUSYDLY  (0xf  << 18)    /* RW */
#define MSDC_PATCH_BIT_WDOD     (0xf  << 22)    /* RW */
#define MSDC_PATCH_BIT_IDRTSEL  (0x1  << 26)    /* RW */
#define MSDC_PATCH_BIT_CMDFSEL  (0x1  << 27)    /* RW */
#define MSDC_PATCH_BIT_INTDLSEL (0x1  << 28)    /* RW */
#define MSDC_PATCH_BIT_SPCPUSH  (0x1  << 29)    /* RW */
#define MSDC_PATCH_BIT_DECRCTMO (0x1  << 30)    /* RW */

/* MSDC_PATCH_BIT1 mask */
#define MSDC_PATCH_BIT1_WRDAT_CRCS  (0x7 << 3)
#define MSDC_PATCH_BIT1_CMD_RSP     (0x7 << 0)

/* MSDC_PAD_CTL0 mask */
#define MSDC_PAD_CTL0_CLKDRVN   (0x7  << 0)     /* RW */
#define MSDC_PAD_CTL0_CLKDRVP   (0x7  << 4)     /* RW */
#define MSDC_PAD_CTL0_CLKSR     (0x1  << 8)     /* RW */
#define MSDC_PAD_CTL0_CLKPD     (0x1  << 16)    /* RW */
#define MSDC_PAD_CTL0_CLKPU     (0x1  << 17)    /* RW */
#define MSDC_PAD_CTL0_CLKSMT    (0x1  << 18)    /* RW */
#define MSDC_PAD_CTL0_CLKIES    (0x1  << 19)    /* RW */
#define MSDC_PAD_CTL0_CLKTDSEL  (0xf  << 20)    /* RW */
#define MSDC_PAD_CTL0_CLKRDSEL  (0xffUL << 24)   /* RW */

/* MSDC_PAD_CTL1 mask */
#define MSDC_PAD_CTL1_CMDDRVN   (0x7  << 0)     /* RW */
#define MSDC_PAD_CTL1_CMDDRVP   (0x7  << 4)     /* RW */
#define MSDC_PAD_CTL1_CMDSR     (0x1  << 8)     /* RW */
#define MSDC_PAD_CTL1_CMDPD     (0x1  << 16)    /* RW */
#define MSDC_PAD_CTL1_CMDPU     (0x1  << 17)    /* RW */
#define MSDC_PAD_CTL1_CMDSMT    (0x1  << 18)    /* RW */
#define MSDC_PAD_CTL1_CMDIES    (0x1  << 19)    /* RW */
#define MSDC_PAD_CTL1_CMDTDSEL  (0xf  << 20)    /* RW */
#define MSDC_PAD_CTL1_CMDRDSEL  (0xffUL << 24)   /* RW */

/* MSDC_PAD_CTL2 mask */
#define MSDC_PAD_CTL2_DATDRVN   (0x7  << 0)     /* RW */
#define MSDC_PAD_CTL2_DATDRVP   (0x7  << 4)     /* RW */
#define MSDC_PAD_CTL2_DATSR     (0x1  << 8)     /* RW */
#define MSDC_PAD_CTL2_DATPD     (0x1  << 16)    /* RW */
#define MSDC_PAD_CTL2_DATPU     (0x1  << 17)    /* RW */
#define MSDC_PAD_CTL2_DATIES    (0x1  << 19)    /* RW */
#define MSDC_PAD_CTL2_DATSMT    (0x1  << 18)    /* RW */
#define MSDC_PAD_CTL2_DATTDSEL  (0xf  << 20)    /* RW */
#define MSDC_PAD_CTL2_DATRDSEL  (0xffUL << 24)   /* RW */

/* MSDC_PAD_TUNE mask */
#define MSDC_PAD_TUNE_DATWRDLY  (0x1F << 0)     /* RW */
#define MSDC_PAD_TUNE_DATRRDLY  (0x1F << 8)     /* RW */
#define MSDC_PAD_TUNE_CMDRDLY   (0x1F << 16)    /* RW */
#define MSDC_PAD_TUNE_CMDRRDLY  (0x1FUL << 22)  /* RW */
#define MSDC_PAD_TUNE_CLKTXDLY  (0x1FUL << 27)  /* RW */

/* MSDC_DAT_RDDLY0/1 mask */
#define MSDC_DAT_RDDLY0_D0      (0x1F << 0)     /* RW */
#define MSDC_DAT_RDDLY0_D1      (0x1F << 8)     /* RW */
#define MSDC_DAT_RDDLY0_D2      (0x1F << 16)    /* RW */
#define MSDC_DAT_RDDLY0_D3      (0x1F << 24)    /* RW */

#define MSDC_DAT_RDDLY1_D4      (0x1F << 0)     /* RW */
#define MSDC_DAT_RDDLY1_D5      (0x1F << 8)     /* RW */
#define MSDC_DAT_RDDLY1_D6      (0x1F << 16)    /* RW */
#define MSDC_DAT_RDDLY1_D7      (0x1F << 24)    /* RW */

#define MSDC_CKGEN_MSDC_DLY_SEL   (0x1F << 10)
#define MSDC_INT_DAT_LATCH_CK_SEL  (0x7 << 7)
#define MSDC_CKGEN_MSDC_CK_SEL     (0x1 << 6)
#define CARD_READY_FOR_DATA             (1 << 8)
#define CARD_CURRENT_STATE(x)           ((x & 0x00001E00) >> 9)

/*--------------------------------------------------------------------------*/
/* Descriptor Structure                                                     */
/*--------------------------------------------------------------------------*/
struct gpd {
	u32  hwo:1; /* could be changed by hw */
	u32  bdp:1;
	u32  rsv0:6;
	u32  chksum:8;
	u32  intr:1;
	u32  rsv1:15;
	void *next;
	void *ptr;
	u32  buflen:16;
	u32  extlen:8;
	u32  rsv2:8;
	u32  arg;
	u32  blknum;
	u32  cmd;
};

struct bd {
	u32  eol:1;
	u32  rsv0:7;
	u32  chksum:8;
	u32  rsv1:1;
	u32  blkpad:1;
	u32  dwpad:1;
	u32  rsv2:13;
	void *next;
	void *ptr;
	u32  buflen:16;
	u32  rsv3:16;
};

/*--------------------------------------------------------------------------*/
/* Register Debugging Structure                                             */
/*--------------------------------------------------------------------------*/

struct msdc_cfg_reg {
	u32 msdc:1;
	u32 ckpwn:1;
	u32 rst:1;
	u32 pio:1;
	u32 ckdrven:1;
	u32 start18v:1;
	u32 pass18v:1;
	u32 ckstb:1;
	u32 ckdiv:8;
	u32 ckmod:2;
	u32 pad:14;
};

struct msdc_iocon_reg {
	u32 sdr104cksel:1;
	u32 rsmpl:1;
	u32 dsmpl:1;
	u32 ddlysel:1;
	u32 ddr50ckd:1;
	u32 dsplsel:1;
	u32 pad1:10;
	u32 d0spl:1;
	u32 d1spl:1;
	u32 d2spl:1;
	u32 d3spl:1;
	u32 d4spl:1;
	u32 d5spl:1;
	u32 d6spl:1;
	u32 d7spl:1;
	u32 riscsz:1;
	u32 pad2:7;
};

struct msdc_ps_reg {
	u32 cden:1;
	u32 cdsts:1;
	u32 pad1:10;
	u32 cddebounce:4;
	u32 dat:8;
	u32 cmd:1;
	u32 pad2:6;
	u32 wp:1;
};

struct msdc_int_reg {
	u32 mmcirq:1;
	u32 cdsc:1;
	u32 pad1:1;
	u32 atocmdrdy:1;
	u32 atocmdtmo:1;
	u32 atocmdcrc:1;
	u32 dmaqempty:1;
	u32 sdioirq:1;
	u32 cmdrdy:1;
	u32 cmdtmo:1;
	u32 rspcrc:1;
	u32 csta:1;
	u32 xfercomp:1;
	u32 dxferdone:1;
	u32 dattmo:1;
	u32 datcrc:1;
	u32 atocmd19done:1;
	u32 pad2:15;
};

struct msdc_inten_reg {
	u32 mmcirq:1;
	u32 cdsc:1;
	u32 pad1:1;
	u32 atocmdrdy:1;
	u32 atocmdtmo:1;
	u32 atocmdcrc:1;
	u32 dmaqempty:1;
	u32 sdioirq:1;
	u32 cmdrdy:1;
	u32 cmdtmo:1;
	u32 rspcrc:1;
	u32 csta:1;
	u32 xfercomp:1;
	u32 dxferdone:1;
	u32 dattmo:1;
	u32 datcrc:1;
	u32 atocmd19done:1;
	u32 pad2:15;
};

struct msdc_fifocs_reg {
	u32 rxcnt:8;
	u32 pad1:8;
	u32 txcnt:8;
	u32 pad2:7;
	u32 clr:1;
};

struct msdc_txdat_reg {
	u32 val;
};

struct msdc_rxdat_reg {
	u32 val;
};

struct sdc_cfg_reg {
	u32 sdiowkup:1;
	u32 inswkup:1;
	u32 pad1:14;
	u32 buswidth:2;
	u32 pad2:1;
	u32 sdio:1;
	u32 sdioide:1;
	u32 intblkgap:1;
	u32 pad4:2;
	u32 dtoc:8;
};

struct sdc_cmd_reg {
	u32 cmd:6;
	u32 brk:1;
	u32 rsptyp:3;
	u32 pad1:1;
	u32 dtype:2;
	u32 rw:1;
	u32 stop:1;
	u32 goirq:1;
	u32 blklen:12;
	u32 atocmd:2;
	u32 volswth:1;
	u32 pad2:1;
};

struct sdc_arg_reg {
	u32 arg;
};

struct sdc_sts_reg {
	u32 sdcbusy:1;
	u32 cmdbusy:1;
	u32 pad:29;
	u32 swrcmpl:1;
};

struct sdc_resp0_reg {
	u32 val;
};

struct sdc_resp1_reg {
	u32 val;
};

struct sdc_resp2_reg {
	u32 val;
};

struct sdc_resp3_reg {
	u32 val;
};

struct sdc_blknum_reg {
	u32 num;
};

struct sdc_csts_reg {
	u32 sts;
};

struct sdc_cstsen_reg {
	u32 sts;
};

struct sdc_datcrcsts_reg {
	u32 datcrcsts:8;
	u32 ddrcrcsts:4;
	u32 pad:20;
};

struct emmc_cfg0_reg {
	u32 bootstart:1;
	u32 bootstop:1;
	u32 bootmode:1;
	u32 pad1:9;
	u32 bootwaidly:3;
	u32 bootsupp:1;
	u32 pad2:16;
};

struct emmc_cfg1_reg {
	u32 bootcrctmc:16;
	u32 pad:4;
	u32 bootacktmc:12;
};

struct emmc_sts_reg {
	u32 bootcrcerr:1;
	u32 bootackerr:1;
	u32 bootdattmo:1;
	u32 bootacktmo:1;
	u32 bootupstate:1;
	u32 bootackrcv:1;
	u32 bootdatrcv:1;
	u32 pad:25;
};

struct emmc_iocon_reg {
	u32 bootrst:1;
	u32 pad:31;
};

struct msdc_acmd_resp_reg {
	u32 val;
};

struct msdc_acmd19_trg_reg {
	u32 tunesel:4;
	u32 pad:28;
};

struct msdc_acmd19_sts_reg {
	u32 val;
};

struct msdc_dma_sa_reg {
	u32 addr;
};

struct msdc_dma_ca_reg {
	u32 addr;
};

struct msdc_dma_ctrl_reg {
	u32 start:1;
	u32 stop:1;
	u32 resume:1;
	u32 pad1:5;
	u32 mode:1;
	u32 pad2:1;
	u32 lastbuf:1;
	u32 pad3:1;
	u32 brustsz:3;
	u32 pad4:1;
	u32 xfersz:16;
};

struct msdc_dma_cfg_reg {
	u32 status:1;
	u32 decsen:1;
	u32 pad1:2;
	u32 bdcsen:1;
	u32 gpdcsen:1;
	u32 pad2:26;
};

struct msdc_dbg_sel_reg {
	u32 sel:16;
	u32 pad2:16;
};

struct msdc_dbg_out_reg {
	u32 val;
};

struct msdc_pad_ctl0_reg {
	u32 clkdrvn:3;
	u32 rsv0:1;
	u32 clkdrvp:3;
	u32 rsv1:1;
	u32 clksr:1;
	u32 rsv2:7;
	u32 clkpd:1;
	u32 clkpu:1;
	u32 clksmt:1;
	u32 clkies:1;
	u32 clktdsel:4;
	u32 clkrdsel:8;
};

struct msdc_pad_ctl1_reg {
	u32 cmddrvn:3;
	u32 rsv0:1;
	u32 cmddrvp:3;
	u32 rsv1:1;
	u32 cmdsr:1;
	u32 rsv2:7;
	u32 cmdpd:1;
	u32 cmdpu:1;
	u32 cmdsmt:1;
	u32 cmdies:1;
	u32 cmdtdsel:4;
	u32 cmdrdsel:8;
};

struct msdc_pad_ctl2_reg {
	u32 datdrvn:3;
	u32 rsv0:1;
	u32 datdrvp:3;
	u32 rsv1:1;
	u32 datsr:1;
	u32 rsv2:7;
	u32 datpd:1;
	u32 datpu:1;
	u32 datsmt:1;
	u32 daties:1;
	u32 dattdsel:4;
	u32 datrdsel:8;
};

struct msdc_pad_tune_reg {
	u32 wrrxdly:3;
	u32 pad1:5;
	u32 rdrxdly:8;
	u32 pad2:16;
};

struct msdc_dat_rddly0 {
	u32 dat0:5;
	u32 rsv0:3;
	u32 dat1:5;
	u32 rsv1:3;
	u32 dat2:5;
	u32 rsv2:3;
	u32 dat3:5;
	u32 rsv3:3;
};

struct msdc_dat_rddly1 {
	u32 dat4:5;
	u32 rsv4:3;
	u32 dat5:5;
	u32 rsv5:3;
	u32 dat6:5;
	u32 rsv6:3;
	u32 dat7:5;
	u32 rsv7:3;
};

struct msdc_hw_dbg_reg {
	u32 dbg0sel:8;
	u32 dbg1sel:6;
	u32 pad1:2;
	u32 dbg2sel:6;
	u32 pad2:2;
	u32 dbg3sel:6;
	u32 pad3:2;
};

struct msdc_version_reg {
	u32 val;
};

struct msdc_eco_ver_reg {
	u32 val;
};

struct msdc_regs {
	struct msdc_cfg_reg        msdc_cfg;      /* base+0x00h */
	struct msdc_iocon_reg      msdc_iocon;    /* base+0x04h */
	struct msdc_ps_reg         msdc_ps;       /* base+0x08h */
	struct msdc_int_reg        msdc_int;      /* base+0x0ch */
	struct msdc_inten_reg      msdc_inten;    /* base+0x10h */
	struct msdc_fifocs_reg     msdc_fifocs;   /* base+0x14h */
	struct msdc_txdat_reg      msdc_txdat;    /* base+0x18h */
	struct msdc_rxdat_reg      msdc_rxdat;    /* base+0x1ch */
	u32                 rsv1[4];
	struct sdc_cfg_reg         sdc_cfg;       /* base+0x30h */
	struct sdc_cmd_reg         sdc_cmd;       /* base+0x34h */
	struct sdc_arg_reg         sdc_arg;       /* base+0x38h */
	struct sdc_sts_reg         sdc_sts;       /* base+0x3ch */
	struct sdc_resp0_reg       sdc_resp0;     /* base+0x40h */
	struct sdc_resp1_reg       sdc_resp1;     /* base+0x44h */
	struct sdc_resp2_reg       sdc_resp2;     /* base+0x48h */
	struct sdc_resp3_reg       sdc_resp3;     /* base+0x4ch */
	struct sdc_blknum_reg      sdc_blknum;    /* base+0x50h */
	u32                 rsv2[1];
	struct sdc_csts_reg        sdc_csts;      /* base+0x58h */
	struct sdc_cstsen_reg      sdc_cstsen;    /* base+0x5ch */
	struct sdc_datcrcsts_reg   sdc_dcrcsta;   /* base+0x60h */
	u32                 rsv3[3];
	struct emmc_cfg0_reg       emmc_cfg0;     /* base+0x70h */
	struct emmc_cfg1_reg       emmc_cfg1;     /* base+0x74h */
	struct emmc_sts_reg        emmc_sts;      /* base+0x78h */
	struct emmc_iocon_reg      emmc_iocon;    /* base+0x7ch */
	struct msdc_acmd_resp_reg  acmd_resp;     /* base+0x80h */
	struct msdc_acmd19_trg_reg acmd19_trg;    /* base+0x84h */
	struct msdc_acmd19_sts_reg acmd19_sts;    /* base+0x88h */
	u32                 rsv4[1];
	struct msdc_dma_sa_reg     dma_sa;        /* base+0x90h */
	struct msdc_dma_ca_reg     dma_ca;        /* base+0x94h */
	struct msdc_dma_ctrl_reg   dma_ctrl;      /* base+0x98h */
	struct msdc_dma_cfg_reg    dma_cfg;       /* base+0x9ch */
	struct msdc_dbg_sel_reg    dbg_sel;       /* base+0xa0h */
	struct msdc_dbg_out_reg    dbg_out;       /* base+0xa4h */
	u32                 rsv5[2];
	u32                 patch0;        /* base+0xb0h */
	u32                 patch1;        /* base+0xb4h */
	u32                 rsv6[10];
	struct msdc_pad_ctl0_reg   pad_ctl0;      /* base+0xe0h */
	struct msdc_pad_ctl1_reg   pad_ctl1;      /* base+0xe4h */
	struct msdc_pad_ctl2_reg   pad_ctl2;      /* base+0xe8h */
	struct msdc_pad_tune_reg   pad_tune;      /* base+0xech */
	struct msdc_dat_rddly0     dat_rddly0;    /* base+0xf0h */
	struct msdc_dat_rddly1     dat_rddly1;    /* base+0xf4h */
	struct msdc_hw_dbg_reg     hw_dbg;        /* base+0xf8h */
	u32                 rsv7[1];
	struct msdc_version_reg    version;       /* base+0x100h */
	struct msdc_eco_ver_reg    eco_ver;       /* base+0x104h */
};

struct msdc_dma {
	u32 sglen;                   /* size of scatter list */
	struct scatterlist *sg;      /* I/O scatter list */
	u8  mode;                    /* dma mode        */

	struct gpd *gpd;                  /* pointer to gpd array */
	struct bd  *bd;                   /* pointer to bd array */
	dma_addr_t gpd_addr;         /* the physical address of gpd array */
	dma_addr_t bd_addr;          /* the physical address of bd array */
};

struct msdc_host {
	struct msdc_hw              *hw;

	struct mmc_host             *mmc;           /* mmc structure */
	struct mmc_command          *cmd;
	struct mmc_data             *data;
	struct mmc_request          *mrq;
	int                         cmd_rsp;

	int                         error;
	spinlock_t                  lock;           /* mutex */
	struct semaphore            sem;

	u32                         blksz;          /* host block size */
	void __iomem                *base;           /* host base address */
	int                         id;             /* host id */
	int                         pwr_ref;        /* core power reference count */

	u32                         xfer_size;      /* total transferred size */

	struct msdc_dma             dma;            /* dma channel */
	u32                         dma_xfer_size;  /* dma transfer size in bytes */
	int                         dma_xfer;       /* dma transfer mode */

	u32                         timeout_ns;     /* data timeout ns */
	u32                         timeout_clks;   /* data timeout clks */

	atomic_t                    abort;          /* abort transfer */

	int                         irq;            /* host interrupt */

	struct delayed_work		card_delaywork;

	struct completion           cmd_done;
	struct completion           xfer_done;
	struct pm_message           pm_state;

	u32                         mclk;           /* mmc subsystem clock */
	u32                         hclk;           /* host clock speed */
	u32                         sclk;           /* SD/MS clock speed */
	u8                          core_clkon;     /* Host core clock on ? */
	u8                          card_clkon;     /* Card clock on ? */
	u8                          core_power;     /* core power */
	u8                          power_mode;     /* host power mode */
	u8                          card_inserted;  /* card inserted ? */
	u8                          suspend;        /* host suspended ? */
	u8                          app_cmd;        /* for app command */
	u32                         app_cmd_arg;
};

#define sdr_read8(reg)            readb(reg)
#define sdr_read32(reg)           readl(reg)
#define sdr_write8(reg, val)      writeb(val, reg)
#define sdr_write32(reg, val)     writel(val, reg)

static inline void sdr_set_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val |= bs;
	writel(val, reg);
}

static inline void sdr_clr_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val &= ~bs;
	writel(val, reg);
}

static inline void sdr_set_field(void __iomem *reg, u32 field, u32 val)
{
	unsigned int tv = readl(reg);

	tv &= ~field;
	tv |= ((val) << (ffs((unsigned int)field) - 1));
	writel(tv, reg);
}

static inline void sdr_get_field(void __iomem *reg, u32 field, u32 *val)
{
	unsigned int tv = readl(reg);
	*val = ((tv & field) >> (ffs((unsigned int)field) - 1));
}

#endif

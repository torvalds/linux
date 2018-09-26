/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>

#include <asm/mach-ralink/ralink_regs.h>

#include "board.h"
#include "dbg.h"
#include "mt6575_sd.h"

#ifdef CONFIG_SOC_MT7621
#define RALINK_SYSCTL_BASE		0xbe000000
#else
#define RALINK_SYSCTL_BASE		0xb0000000
#endif

#define DRV_NAME            "mtk-sd"

#if defined(CONFIG_SOC_MT7620)
#define HOST_MAX_MCLK       (48000000) /* +/- by chhung */
#elif defined(CONFIG_SOC_MT7621)
#define HOST_MAX_MCLK       (50000000) /* +/- by chhung */
#endif
#define HOST_MIN_MCLK       (260000)

#define HOST_MAX_BLKSZ      (2048)

#define MSDC_OCR_AVAIL      (MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33)

#define GPIO_PULL_DOWN      (0)
#define GPIO_PULL_UP        (1)

#if 0 /* --- by chhung */
#define MSDC_CLKSRC_REG     (0xf100000C)
#define PDN_REG           (0xF1000010)
#endif /* end of --- */

#define DEFAULT_DEBOUNCE    (8)       /* 8 cycles */
#define DEFAULT_DTOC        (40)      /* data timeout counter. 65536x40 sclk. */

#define CMD_TIMEOUT         (HZ / 10)     /* 100ms */
#define DAT_TIMEOUT         (HZ / 2 * 5)  /* 500ms x5 */

#define MAX_DMA_CNT         (64 * 1024 - 512)   /* a single transaction for WIFI may be 50K*/

#define MAX_GPD_NUM         (1 + 1)  /* one null gpd */
#define MAX_BD_NUM          (1024)

#define MAX_HW_SGMTS        (MAX_BD_NUM)
#define MAX_SGMT_SZ         (MAX_DMA_CNT)
#define MAX_REQ_SZ          (MAX_SGMT_SZ * 8)

static int cd_active_low = 1;

//=================================
#define PERI_MSDC0_PDN      (15)
//#define PERI_MSDC1_PDN    (16)
//#define PERI_MSDC2_PDN    (17)
//#define PERI_MSDC3_PDN    (18)

#if 0 /* --- by chhung */
/* gate means clock power down */
static int g_clk_gate = 0;
#define msdc_gate_clock(id) \
	do {					       \
		g_clk_gate &= ~(1 << ((id) + PERI_MSDC0_PDN));	\
	} while (0)
/* not like power down register. 1 means clock on. */
#define msdc_ungate_clock(id) \
	do {					    \
		g_clk_gate |= 1 << ((id) + PERI_MSDC0_PDN);	\
	} while (0)

// do we need sync object or not
void msdc_clk_status(int *status)
{
	*status = g_clk_gate;
}
#endif /* end of --- */

/* +++ by chhung */
struct msdc_hw msdc0_hw = {
	.clk_src        = 0,
	.flags          = MSDC_CD_PIN_EN | MSDC_REMOVABLE,
//	.flags          = MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE,
};

/* end of +++ */

static int msdc_rsp[] = {
	0,  /* RESP_NONE */
	1,  /* RESP_R1 */
	2,  /* RESP_R2 */
	3,  /* RESP_R3 */
	4,  /* RESP_R4 */
	1,  /* RESP_R5 */
	1,  /* RESP_R6 */
	1,  /* RESP_R7 */
	7,  /* RESP_R1b */
};

#define msdc_dma_on()        sdr_clr_bits(host->base + MSDC_CFG, MSDC_CFG_PIO)

static void msdc_reset_hw(struct msdc_host *host)
{
	sdr_set_bits(host->base + MSDC_CFG, MSDC_CFG_RST);
	while (readl(host->base + MSDC_CFG) & MSDC_CFG_RST)
		cpu_relax();
}

#define msdc_clr_int() \
	do {							\
		volatile u32 val = readl(host->base + MSDC_INT);	\
		writel(val, host->base + MSDC_INT);			\
	} while (0)

static void msdc_clr_fifo(struct msdc_host *host)
{
	sdr_set_bits(host->base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	while (readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_CLR)
		cpu_relax();
}

#define msdc_irq_save(val) \
	do {					\
		val = readl(host->base + MSDC_INTEN);	\
		sdr_clr_bits(host->base + MSDC_INTEN, val);	\
	} while (0)

#define msdc_irq_restore(val) \
	do {					\
		sdr_set_bits(host->base + MSDC_INTEN, val);	\
	} while (0)

/* clock source for host: global */
#if defined(CONFIG_SOC_MT7620)
static u32 hclks[] = {48000000}; /* +/- by chhung */
#elif defined(CONFIG_SOC_MT7621)
static u32 hclks[] = {50000000}; /* +/- by chhung */
#endif

//============================================
// the power for msdc host controller: global
//    always keep the VMC on.
//============================================
#define msdc_vcore_on(host) \
	do {								\
		INIT_MSG("[+]VMC ref. count<%d>", ++host->pwr_ref);	\
		(void)hwPowerOn(MT65XX_POWER_LDO_VMC, VOL_3300, "SD");	\
	} while (0)
#define msdc_vcore_off(host) \
	do {								\
		INIT_MSG("[-]VMC ref. count<%d>", --host->pwr_ref);	\
		(void)hwPowerDown(MT65XX_POWER_LDO_VMC, "SD");		\
	} while (0)

//====================================
// the vdd output for card: global
//   always keep the VMCH on.
//====================================
#define msdc_vdd_on(host) \
	do {								\
		(void)hwPowerOn(MT65XX_POWER_LDO_VMCH, VOL_3300, "SD"); \
	} while (0)
#define msdc_vdd_off(host) \
	do {							\
		(void)hwPowerDown(MT65XX_POWER_LDO_VMCH, "SD"); \
	} while (0)

#define sdc_is_busy()          (readl(host->base + SDC_STS) & SDC_STS_SDCBUSY)
#define sdc_is_cmd_busy()      (readl(host->base + SDC_STS) & SDC_STS_CMDBUSY)

#define sdc_send_cmd(cmd, arg) \
	do {					\
		writel((arg), host->base + SDC_ARG);	\
		writel((cmd), host->base + SDC_CMD);	\
	} while (0)

/* +++ by chhung */
#ifndef __ASSEMBLY__
#define PHYSADDR(a)             (((unsigned long)(a)) & 0x1fffffff)
#else
#define PHYSADDR(a)             ((a) & 0x1fffffff)
#endif
/* end of +++ */
static unsigned int msdc_do_command(struct msdc_host   *host,
				    struct mmc_command *cmd,
				    int                 tune,
				    unsigned long       timeout);

static int msdc_tune_cmdrsp(struct msdc_host *host, struct mmc_command *cmd);

#ifdef MT6575_SD_DEBUG
static void msdc_dump_card_status(struct msdc_host *host, u32 status)
{
/* N_MSG is currently a no-op */
#if 0
	static char *state[] = {
		"Idle",			/* 0 */
		"Ready",		/* 1 */
		"Ident",		/* 2 */
		"Stby",			/* 3 */
		"Tran",			/* 4 */
		"Data",			/* 5 */
		"Rcv",			/* 6 */
		"Prg",			/* 7 */
		"Dis",			/* 8 */
		"Reserved",		/* 9 */
		"Reserved",		/* 10 */
		"Reserved",		/* 11 */
		"Reserved",		/* 12 */
		"Reserved",		/* 13 */
		"Reserved",		/* 14 */
		"I/O mode",		/* 15 */
	};
#endif
	if (status & R1_OUT_OF_RANGE)
		N_MSG(RSP, "[CARD_STATUS] Out of Range");
	if (status & R1_ADDRESS_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Address Error");
	if (status & R1_BLOCK_LEN_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Block Len Error");
	if (status & R1_ERASE_SEQ_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Erase Seq Error");
	if (status & R1_ERASE_PARAM)
		N_MSG(RSP, "[CARD_STATUS] Erase Param");
	if (status & R1_WP_VIOLATION)
		N_MSG(RSP, "[CARD_STATUS] WP Violation");
	if (status & R1_CARD_IS_LOCKED)
		N_MSG(RSP, "[CARD_STATUS] Card is Locked");
	if (status & R1_LOCK_UNLOCK_FAILED)
		N_MSG(RSP, "[CARD_STATUS] Lock/Unlock Failed");
	if (status & R1_COM_CRC_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Command CRC Error");
	if (status & R1_ILLEGAL_COMMAND)
		N_MSG(RSP, "[CARD_STATUS] Illegal Command");
	if (status & R1_CARD_ECC_FAILED)
		N_MSG(RSP, "[CARD_STATUS] Card ECC Failed");
	if (status & R1_CC_ERROR)
		N_MSG(RSP, "[CARD_STATUS] CC Error");
	if (status & R1_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Error");
	if (status & R1_UNDERRUN)
		N_MSG(RSP, "[CARD_STATUS] Underrun");
	if (status & R1_OVERRUN)
		N_MSG(RSP, "[CARD_STATUS] Overrun");
	if (status & R1_CID_CSD_OVERWRITE)
		N_MSG(RSP, "[CARD_STATUS] CID/CSD Overwrite");
	if (status & R1_WP_ERASE_SKIP)
		N_MSG(RSP, "[CARD_STATUS] WP Eraser Skip");
	if (status & R1_CARD_ECC_DISABLED)
		N_MSG(RSP, "[CARD_STATUS] Card ECC Disabled");
	if (status & R1_ERASE_RESET)
		N_MSG(RSP, "[CARD_STATUS] Erase Reset");
	if (status & R1_READY_FOR_DATA)
		N_MSG(RSP, "[CARD_STATUS] Ready for Data");
	if (status & R1_SWITCH_ERROR)
		N_MSG(RSP, "[CARD_STATUS] Switch error");
	if (status & R1_APP_CMD)
		N_MSG(RSP, "[CARD_STATUS] App Command");

	N_MSG(RSP, "[CARD_STATUS] '%s' State", state[R1_CURRENT_STATE(status)]);
}

static void msdc_dump_ocr_reg(struct msdc_host *host, u32 resp)
{
	if (resp & (1 << 7))
		N_MSG(RSP, "[OCR] Low Voltage Range");
	if (resp & (1 << 15))
		N_MSG(RSP, "[OCR] 2.7-2.8 volt");
	if (resp & (1 << 16))
		N_MSG(RSP, "[OCR] 2.8-2.9 volt");
	if (resp & (1 << 17))
		N_MSG(RSP, "[OCR] 2.9-3.0 volt");
	if (resp & (1 << 18))
		N_MSG(RSP, "[OCR] 3.0-3.1 volt");
	if (resp & (1 << 19))
		N_MSG(RSP, "[OCR] 3.1-3.2 volt");
	if (resp & (1 << 20))
		N_MSG(RSP, "[OCR] 3.2-3.3 volt");
	if (resp & (1 << 21))
		N_MSG(RSP, "[OCR] 3.3-3.4 volt");
	if (resp & (1 << 22))
		N_MSG(RSP, "[OCR] 3.4-3.5 volt");
	if (resp & (1 << 23))
		N_MSG(RSP, "[OCR] 3.5-3.6 volt");
	if (resp & (1 << 24))
		N_MSG(RSP, "[OCR] Switching to 1.8V Accepted (S18A)");
	if (resp & (1 << 30))
		N_MSG(RSP, "[OCR] Card Capacity Status (CCS)");
	if (resp & (1 << 31))
		N_MSG(RSP, "[OCR] Card Power Up Status (Idle)");
	else
		N_MSG(RSP, "[OCR] Card Power Up Status (Busy)");
}

static void msdc_dump_rca_resp(struct msdc_host *host, u32 resp)
{
	u32 status = (((resp >> 15) & 0x1) << 23) |
		     (((resp >> 14) & 0x1) << 22) |
		     (((resp >> 13) & 0x1) << 19) |
		     (resp & 0x1fff);

	N_MSG(RSP, "[RCA] 0x%.4x", resp >> 16);
	msdc_dump_card_status(host, status);
}

static void msdc_dump_io_resp(struct msdc_host *host, u32 resp)
{
	u32 flags = (resp >> 8) & 0xFF;
#if 0
	char *state[] = {"DIS", "CMD", "TRN", "RFU"};
#endif
	if (flags & (1 << 7))
		N_MSG(RSP, "[IO] COM_CRC_ERR");
	if (flags & (1 << 6))
		N_MSG(RSP, "[IO] Illegal command");
	if (flags & (1 << 3))
		N_MSG(RSP, "[IO] Error");
	if (flags & (1 << 2))
		N_MSG(RSP, "[IO] RFU");
	if (flags & (1 << 1))
		N_MSG(RSP, "[IO] Function number error");
	if (flags & (1 << 0))
		N_MSG(RSP, "[IO] Out of range");

	N_MSG(RSP, "[IO] State: %s, Data:0x%x", state[(resp >> 12) & 0x3], resp & 0xFF);
}
#endif

static void msdc_set_timeout(struct msdc_host *host, u32 ns, u32 clks)
{
	u32 timeout, clk_ns;

	host->timeout_ns   = ns;
	host->timeout_clks = clks;

	clk_ns  = 1000000000UL / host->sclk;
	timeout = ns / clk_ns + clks;
	timeout = timeout >> 16; /* in 65536 sclk cycle unit */
	timeout = timeout > 1 ? timeout - 1 : 0;
	timeout = timeout > 255 ? 255 : timeout;

	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC, timeout);

	N_MSG(OPS, "Set read data timeout: %dns %dclks -> %d x 65536 cycles",
	      ns, clks, timeout + 1);
}

static void msdc_tasklet_card(struct work_struct *work)
{
	struct msdc_host *host = (struct msdc_host *)container_of(work,
				struct msdc_host, card_delaywork.work);
	u32 inserted;
	u32 status = 0;
    //u32 change = 0;

	spin_lock(&host->lock);

	status = readl(host->base + MSDC_PS);
	if (cd_active_low)
		inserted = (status & MSDC_PS_CDSTS) ? 0 : 1;
	else
		inserted = (status & MSDC_PS_CDSTS) ? 1 : 0;

#if 0
	change = host->card_inserted ^ inserted;
	host->card_inserted = inserted;

	if (change && !host->suspend) {
		if (inserted)
			host->mmc->f_max = HOST_MAX_MCLK;  // work around
		mmc_detect_change(host->mmc, msecs_to_jiffies(20));
	}
#else  /* Make sure: handle the last interrupt */
	host->card_inserted = inserted;

	if (!host->suspend) {
		host->mmc->f_max = HOST_MAX_MCLK;
		mmc_detect_change(host->mmc, msecs_to_jiffies(20));
	}

	IRQ_MSG("card found<%s>", inserted ? "inserted" : "removed");
#endif

	spin_unlock(&host->lock);
}

#if 0 /* --- by chhung */
/* For E2 only */
static u8 clk_src_bit[4] = {
	0, 3, 5, 7
};

static void msdc_select_clksrc(struct msdc_host *host, unsigned char clksrc)
{
	u32 val;

	BUG_ON(clksrc > 3);
	INIT_MSG("set clock source to <%d>", clksrc);

	val = readl(host->base + MSDC_CLKSRC_REG);
	if (readl(host->base + MSDC_ECO_VER) >= 4) {
		val &= ~(0x3  << clk_src_bit[host->id]);
		val |= clksrc << clk_src_bit[host->id];
	} else {
		val &= ~0x3; val |= clksrc;
	}
	writel(val, host->base + MSDC_CLKSRC_REG);

	host->hclk = hclks[clksrc];
	host->hw->clk_src = clksrc;
}
#endif /* end of --- */

static void msdc_set_mclk(struct msdc_host *host, int ddr, unsigned int hz)
{
	//struct msdc_hw *hw = host->hw;
	u32 mode;
	u32 flags;
	u32 div;
	u32 sclk;
	u32 hclk = host->hclk;
	//u8  clksrc = hw->clk_src;

	if (!hz) { // set mmc system clock to 0 ?
		//ERR_MSG("set mclk to 0!!!");
		msdc_reset_hw(host);
		return;
	}

	msdc_irq_save(flags);

	if (ddr) {
		mode = 0x2; /* ddr mode and use divisor */
		if (hz >= (hclk >> 2)) {
			div  = 1;         /* mean div = 1/4 */
			sclk = hclk >> 2; /* sclk = clk / 4 */
		} else {
			div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
			sclk = (hclk >> 2) / div;
		}
	} else if (hz >= hclk) { /* bug fix */
		mode = 0x1; /* no divisor and divisor is ignored */
		div  = 0;
		sclk = hclk;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (hclk >> 1)) {
			div  = 0;         /* mean div = 1/2 */
			sclk = hclk >> 1; /* sclk = clk / 2 */
		} else {
			div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
			sclk = (hclk >> 2) / div;
		}
	}

	/* set clock mode and divisor */
	sdr_set_field(host->base + MSDC_CFG, MSDC_CFG_CKMOD, mode);
	sdr_set_field(host->base + MSDC_CFG, MSDC_CFG_CKDIV, div);

	/* wait clock stable */
	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		cpu_relax();

	host->sclk = sclk;
	host->mclk = hz;
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks); // need?

	INIT_MSG("================");
	INIT_MSG("!!! Set<%dKHz> Source<%dKHz> -> sclk<%dKHz>", hz / 1000, hclk / 1000, sclk / 1000);
	INIT_MSG("================");

	msdc_irq_restore(flags);
}

/* Fix me. when need to abort */
static void msdc_abort_data(struct msdc_host *host)
{
	struct mmc_command *stop = host->mrq->stop;

	ERR_MSG("Need to Abort.");

	msdc_reset_hw(host);
	msdc_clr_fifo(host);
	msdc_clr_int();

	// need to check FIFO count 0 ?

	if (stop) {  /* try to stop, but may not success */
		ERR_MSG("stop when abort CMD<%d>", stop->opcode);
		(void)msdc_do_command(host, stop, 0, CMD_TIMEOUT);
	}

	//if (host->mclk >= 25000000) {
	//      msdc_set_mclk(host, 0, host->mclk >> 1);
	//}
}

#if 0 /* --- by chhung */
static void msdc_pin_config(struct msdc_host *host, int mode)
{
	struct msdc_hw *hw = host->hw;
	int pull = (mode == MSDC_PIN_PULL_UP) ? GPIO_PULL_UP : GPIO_PULL_DOWN;

	/* Config WP pin */
	if (hw->flags & MSDC_WP_PIN_EN) {
		if (hw->config_gpio_pin) /* NULL */
			hw->config_gpio_pin(MSDC_WP_PIN, pull);
	}

	switch (mode) {
	case MSDC_PIN_PULL_UP:
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 1); /* Check & FIXME */
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 0); /* Check & FIXME */
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 1);
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 1);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 0);
		break;
	case MSDC_PIN_PULL_DOWN:
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 0); /* Check & FIXME */
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 1); /* Check & FIXME */
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 1);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 1);
		break;
	case MSDC_PIN_PULL_NONE:
	default:
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPU, 0); /* Check & FIXME */
		//sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKPD, 0); /* Check & FIXME */
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPU, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDPD, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPU, 0);
		sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATPD, 0);
		break;
	}

	N_MSG(CFG, "Pins mode(%d), down(%d), up(%d)",
	      mode, MSDC_PIN_PULL_DOWN, MSDC_PIN_PULL_UP);
}

void msdc_pin_reset(struct msdc_host *host, int mode)
{
	struct msdc_hw *hw = (struct msdc_hw *)host->hw;
	int pull = (mode == MSDC_PIN_PULL_UP) ? GPIO_PULL_UP : GPIO_PULL_DOWN;

	/* Config reset pin */
	if (hw->flags & MSDC_RST_PIN_EN) {
		if (hw->config_gpio_pin) /* NULL */
			hw->config_gpio_pin(MSDC_RST_PIN, pull);

		if (mode == MSDC_PIN_PULL_UP)
			sdr_clr_bits(host->base + EMMC_IOCON, EMMC_IOCON_BOOTRST);
		else
			sdr_set_bits(host->base + EMMC_IOCON, EMMC_IOCON_BOOTRST);
	}
}

static void msdc_core_power(struct msdc_host *host, int on)
{
	N_MSG(CFG, "Turn %s %s power (copower: %d -> %d)",
		on ? "on" : "off", "core", host->core_power, on);

	if (on && host->core_power == 0) {
		msdc_vcore_on(host);
		host->core_power = 1;
		msleep(1);
	} else if (!on && host->core_power == 1) {
		msdc_vcore_off(host);
		host->core_power = 0;
		msleep(1);
	}
}

static void msdc_host_power(struct msdc_host *host, int on)
{
	N_MSG(CFG, "Turn %s %s power ", on ? "on" : "off", "host");

	if (on) {
		//msdc_core_power(host, 1); // need do card detection.
		msdc_pin_reset(host, MSDC_PIN_PULL_UP);
	} else {
		msdc_pin_reset(host, MSDC_PIN_PULL_DOWN);
		//msdc_core_power(host, 0);
	}
}

static void msdc_card_power(struct msdc_host *host, int on)
{
	N_MSG(CFG, "Turn %s %s power ", on ? "on" : "off", "card");

	if (on) {
		msdc_pin_config(host, MSDC_PIN_PULL_UP);
		//msdc_vdd_on(host);  // need todo card detection.
		msleep(1);
	} else {
		//msdc_vdd_off(host);
		msdc_pin_config(host, MSDC_PIN_PULL_DOWN);
		msleep(1);
	}
}

static void msdc_set_power_mode(struct msdc_host *host, u8 mode)
{
	N_MSG(CFG, "Set power mode(%d)", mode);

	if (host->power_mode == MMC_POWER_OFF && mode != MMC_POWER_OFF) {
		msdc_host_power(host, 1);
		msdc_card_power(host, 1);
	} else if (host->power_mode != MMC_POWER_OFF && mode == MMC_POWER_OFF) {
		msdc_card_power(host, 0);
		msdc_host_power(host, 0);
	}
	host->power_mode = mode;
}
#endif /* end of --- */

#ifdef CONFIG_PM
/*
   register as callback function of WIFI(combo_sdio_register_pm) .
   can called by msdc_drv_suspend/resume too.
*/
static void msdc_pm(pm_message_t state, void *data)
{
	struct msdc_host *host = (struct msdc_host *)data;
	int evt = state.event;

	if (evt == PM_EVENT_USER_RESUME || evt == PM_EVENT_USER_SUSPEND) {
		INIT_MSG("USR_%s: suspend<%d> power<%d>",
			evt == PM_EVENT_USER_RESUME ? "EVENT_USER_RESUME" : "EVENT_USER_SUSPEND",
			host->suspend, host->power_mode);
	}

	if (evt == PM_EVENT_SUSPEND || evt == PM_EVENT_USER_SUSPEND) {
		if (host->suspend) /* already suspend */  /* default 0*/
			return;

		/* for memory card. already power off by mmc */
		if (evt == PM_EVENT_SUSPEND && host->power_mode == MMC_POWER_OFF)
			return;

		host->suspend = 1;
		host->pm_state = state;  /* default PMSG_RESUME */

	} else if (evt == PM_EVENT_RESUME || evt == PM_EVENT_USER_RESUME) {
		if (!host->suspend) {
			//ERR_MSG("warning: already resume");
			return;
		}

		/* No PM resume when USR suspend */
		if (evt == PM_EVENT_RESUME && host->pm_state.event == PM_EVENT_USER_SUSPEND) {
			ERR_MSG("PM Resume when in USR Suspend");		/* won't happen. */
			return;
		}

		host->suspend = 0;
		host->pm_state = state;

	}
}
#endif

static inline u32 msdc_cmd_find_resp(struct mmc_command *cmd)
{
	u32 opcode = cmd->opcode;
	u32 resp;

	if (opcode == MMC_SET_RELATIVE_ADDR) {
		resp = (mmc_cmd_type(cmd) == MMC_CMD_BCR) ? RESP_R6 : RESP_R1;
	} else if (opcode == MMC_FAST_IO) {
		resp = RESP_R4;
	} else if (opcode == MMC_GO_IRQ_STATE) {
		resp = RESP_R5;
	} else if (opcode == MMC_SELECT_CARD) {
		resp = (cmd->arg != 0) ? RESP_R1B : RESP_NONE;
	} else if (opcode == SD_IO_RW_DIRECT || opcode == SD_IO_RW_EXTENDED) {
		resp = RESP_R1; /* SDIO workaround. */
	} else if (opcode == SD_SEND_IF_COND && (mmc_cmd_type(cmd) == MMC_CMD_BCR)) {
		resp = RESP_R1;
	} else {
		switch (mmc_resp_type(cmd)) {
		case MMC_RSP_R1:
			resp = RESP_R1;
			break;
		case MMC_RSP_R1B:
			resp = RESP_R1B;
			break;
		case MMC_RSP_R2:
			resp = RESP_R2;
			break;
		case MMC_RSP_R3:
			resp = RESP_R3;
			break;
		case MMC_RSP_NONE:
		default:
			resp = RESP_NONE;
			break;
		}
	}

	return resp;
}

/*--------------------------------------------------------------------------*/
/* mmc_host_ops members                                                      */
/*--------------------------------------------------------------------------*/
static unsigned int msdc_command_start(struct msdc_host   *host,
				       struct mmc_command *cmd,
				       unsigned long       timeout)
{
	u32 opcode = cmd->opcode;
	u32 rawcmd;
	u32 wints = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |
		    MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO |
		    MSDC_INT_ACMD19_DONE;

	u32 resp;
	unsigned long tmo;

	/* Protocol layer does not provide response type, but our hardware needs
	 * to know exact type, not just size!
	 */
	resp = msdc_cmd_find_resp(cmd);

	cmd->error = 0;
	/* rawcmd :
	 * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
	 * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
	 */
	rawcmd = opcode | msdc_rsp[resp] << 7 | host->blksz << 16;

	if (opcode == MMC_READ_MULTIPLE_BLOCK) {
		rawcmd |= (2 << 11);
	} else if (opcode == MMC_READ_SINGLE_BLOCK) {
		rawcmd |= (1 << 11);
	} else if (opcode == MMC_WRITE_MULTIPLE_BLOCK) {
		rawcmd |= ((2 << 11) | (1 << 13));
	} else if (opcode == MMC_WRITE_BLOCK) {
		rawcmd |= ((1 << 11) | (1 << 13));
	} else if (opcode == SD_IO_RW_EXTENDED) {
		if (cmd->data->flags & MMC_DATA_WRITE)
			rawcmd |= (1 << 13);
		if (cmd->data->blocks > 1)
			rawcmd |= (2 << 11);
		else
			rawcmd |= (1 << 11);
	} else if (opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int)-1) {
		rawcmd |= (1 << 14);
	} else if ((opcode == SD_APP_SEND_SCR) ||
		(opcode == SD_APP_SEND_NUM_WR_BLKS) ||
		(opcode == SD_SWITCH && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
		(opcode == SD_APP_SD_STATUS && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
		(opcode == MMC_SEND_EXT_CSD && (mmc_cmd_type(cmd) == MMC_CMD_ADTC))) {
		rawcmd |= (1 << 11);
	} else if (opcode == MMC_STOP_TRANSMISSION) {
		rawcmd |= (1 << 14);
		rawcmd &= ~(0x0FFF << 16);
	}

	N_MSG(CMD, "CMD<%d><0x%.8x> Arg<0x%.8x>", opcode, rawcmd, cmd->arg);

	tmo = jiffies + timeout;

	if (opcode == MMC_SEND_STATUS) {
		for (;;) {
			if (!sdc_is_cmd_busy())
				break;

			if (time_after(jiffies, tmo)) {
				ERR_MSG("XXX cmd_busy timeout: before CMD<%d>", opcode);
				cmd->error = -ETIMEDOUT;
				msdc_reset_hw(host);
				goto end;
			}
		}
	} else {
		for (;;) {
			if (!sdc_is_busy())
				break;
			if (time_after(jiffies, tmo)) {
				ERR_MSG("XXX sdc_busy timeout: before CMD<%d>", opcode);
				cmd->error = -ETIMEDOUT;
				msdc_reset_hw(host);
				goto end;
			}
		}
	}

	//BUG_ON(in_interrupt());
	host->cmd     = cmd;
	host->cmd_rsp = resp;

	init_completion(&host->cmd_done);

	sdr_set_bits(host->base + MSDC_INTEN, wints);
	sdc_send_cmd(rawcmd, cmd->arg);

end:
	return cmd->error;
}

static unsigned int msdc_command_resp(struct msdc_host   *host,
				      struct mmc_command *cmd,
				      int                 tune,
				      unsigned long       timeout)
	__must_hold(&host->lock)
{
	u32 opcode = cmd->opcode;
	//u32 rawcmd;
	u32 wints = MSDC_INT_CMDRDY  | MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  |
		    MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO |
		    MSDC_INT_ACMD19_DONE;

	BUG_ON(in_interrupt());
	//init_completion(&host->cmd_done);
	//sdr_set_bits(host->base + MSDC_INTEN, wints);

	spin_unlock(&host->lock);
	if (!wait_for_completion_timeout(&host->cmd_done, 10 * timeout)) {
		ERR_MSG("XXX CMD<%d> wait_for_completion timeout ARG<0x%.8x>", opcode, cmd->arg);
		cmd->error = -ETIMEDOUT;
		msdc_reset_hw(host);
	}
	spin_lock(&host->lock);

	sdr_clr_bits(host->base + MSDC_INTEN, wints);
	host->cmd = NULL;

//end:
#ifdef MT6575_SD_DEBUG
	switch (resp) {
	case RESP_NONE:
		N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)", opcode, cmd->error, resp);
		break;
	case RESP_R2:
		N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)= %.8x %.8x %.8x %.8x",
			opcode, cmd->error, resp, cmd->resp[0], cmd->resp[1],
			cmd->resp[2], cmd->resp[3]);
		break;
	default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
		N_MSG(RSP, "CMD_RSP(%d): %d RSP(%d)= 0x%.8x",
			opcode, cmd->error, resp, cmd->resp[0]);
		if (cmd->error == 0) {
			switch (resp) {
			case RESP_R1:
			case RESP_R1B:
				msdc_dump_card_status(host, cmd->resp[0]);
				break;
			case RESP_R3:
				msdc_dump_ocr_reg(host, cmd->resp[0]);
				break;
			case RESP_R5:
				msdc_dump_io_resp(host, cmd->resp[0]);
				break;
			case RESP_R6:
				msdc_dump_rca_resp(host, cmd->resp[0]);
				break;
			}
		}
		break;
	}
#endif

	/* do we need to save card's RCA when SD_SEND_RELATIVE_ADDR */

	if (!tune)
		return cmd->error;

	/* memory card CRC */
	if (host->hw->flags & MSDC_REMOVABLE && cmd->error == -EIO) {
		/* check if has data phase */
		if (readl(host->base + SDC_CMD) & 0x1800) {
			msdc_abort_data(host);
		} else {
			/* do basic: reset*/
			msdc_reset_hw(host);
			msdc_clr_fifo(host);
			msdc_clr_int();
		}
		cmd->error = msdc_tune_cmdrsp(host, cmd);
	}

	//  check DAT0
	/* if (resp == RESP_R1B) {
	   while ((readl(host->base + MSDC_PS) & 0x10000) != 0x10000);
	   } */
	/* CMD12 Error Handle */

	return cmd->error;
}

static unsigned int msdc_do_command(struct msdc_host   *host,
				    struct mmc_command *cmd,
				    int                 tune,
				    unsigned long       timeout)
{
	if (msdc_command_start(host, cmd, timeout))
		goto end;

	if (msdc_command_resp(host, cmd, tune, timeout))
		goto end;

end:

	N_MSG(CMD, "        return<%d> resp<0x%.8x>", cmd->error, cmd->resp[0]);
	return cmd->error;
}

#if 0 /* --- by chhung */
// DMA resume / start / stop
static void msdc_dma_resume(struct msdc_host *host)
{
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_RESUME, 1);

	N_MSG(DMA, "DMA resume");
}
#endif /* end of --- */

static void msdc_dma_start(struct msdc_host *host)
{
	u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;

	sdr_set_bits(host->base + MSDC_INTEN, wints);
	//dsb(); /* --- by chhung */
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);

	N_MSG(DMA, "DMA start");
}

static void msdc_dma_stop(struct msdc_host *host)
{
	//u32 retries=500;
	u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;

	N_MSG(DMA, "DMA status: 0x%.8x", readl(host->base + MSDC_DMA_CFG));
	//while (readl(host->base + MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);

	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP, 1);
	while (readl(host->base + MSDC_DMA_CFG) & MSDC_DMA_CFG_STS)
		;

	//dsb(); /* --- by chhung */
	sdr_clr_bits(host->base + MSDC_INTEN, wints); /* Not just xfer_comp */

	N_MSG(DMA, "DMA stop");
}

/* calc checksum */
static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
	u32 i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];
	return 0xFF - (u8)sum;
}

static void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
			   struct scatterlist *sg_cmd, unsigned int sglen)
{
	struct scatterlist *sg;
	struct gpd *gpd;
	struct bd *bd;
	u32 j;

	BUG_ON(sglen > MAX_BD_NUM); /* not support currently */

	N_MSG(DMA, "DMA sglen<%d> xfersz<%d>", sglen, host->xfer_size);

	gpd = dma->gpd;
	bd  = dma->bd;

	/* modify gpd*/
	//gpd->intr = 0;
	gpd->hwo = 1;  /* hw will clear it */
	gpd->bdp = 1;
	gpd->chksum = 0;  /* need to clear first. */
	gpd->chksum = msdc_dma_calcs((u8 *)gpd, 16);

	/* modify bd*/
	for_each_sg(sg_cmd, sg, sglen, j) {
		bd[j].blkpad = 0;
		bd[j].dwpad = 0;
		bd[j].ptr = (void *)sg_dma_address(sg);
		bd[j].buflen = sg_dma_len(sg);

		if (j == sglen - 1)
			bd[j].eol = 1;	/* the last bd */
		else
			bd[j].eol = 0;

		bd[j].chksum = 0; /* checksume need to clear first */
		bd[j].chksum = msdc_dma_calcs((u8 *)(&bd[j]), 16);
	}

	sdr_set_field(host->base + MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, 1);
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ,
		      MSDC_BRUST_64B);
	sdr_set_field(host->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 1);

	writel(PHYSADDR((u32)dma->gpd_addr), host->base + MSDC_DMA_SA);

	N_MSG(DMA, "DMA_CTRL = 0x%x", readl(host->base + MSDC_DMA_CTRL));
	N_MSG(DMA, "DMA_CFG  = 0x%x", readl(host->base + MSDC_DMA_CFG));
	N_MSG(DMA, "DMA_SA   = 0x%x", readl(host->base + MSDC_DMA_SA));
}

static int msdc_do_request(struct mmc_host *mmc, struct mmc_request *mrq)
	__must_hold(&host->lock)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	struct mmc_data *data;
	//u32 intsts = 0;
	int read = 1, send_type = 0;

#define SND_DAT 0
#define SND_CMD 1

	BUG_ON(mmc == NULL);
	BUG_ON(mrq == NULL);

	host->error = 0;

	cmd  = mrq->cmd;
	data = mrq->cmd->data;

#if 0 /* --- by chhung */
	//if(host->id ==1){
	N_MSG(OPS, "enable clock!");
	msdc_ungate_clock(host->id);
	//}
#endif /* end of --- */

	if (!data) {
		send_type = SND_CMD;
		if (msdc_do_command(host, cmd, 1, CMD_TIMEOUT) != 0)
			goto done;
	} else {
		BUG_ON(data->blksz > HOST_MAX_BLKSZ);
		send_type = SND_DAT;

		data->error = 0;
		read = data->flags & MMC_DATA_READ ? 1 : 0;
		host->data = data;
		host->xfer_size = data->blocks * data->blksz;
		host->blksz = data->blksz;

		if (read) {
			if ((host->timeout_ns != data->timeout_ns) ||
				(host->timeout_clks != data->timeout_clks)) {
				msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);
			}
		}

		writel(data->blocks, host->base + SDC_BLK_NUM);
		//msdc_clr_fifo(host);  /* no need */

		msdc_dma_on();  /* enable DMA mode first!! */
		init_completion(&host->xfer_done);

		/* start the command first*/
		if (msdc_command_start(host, cmd, CMD_TIMEOUT) != 0)
			goto done;

		data->sg_count = dma_map_sg(mmc_dev(mmc), data->sg,
					    data->sg_len,
					    mmc_get_dma_dir(data));
		msdc_dma_setup(host, &host->dma, data->sg,
			       data->sg_count);

		/* then wait command done */
		if (msdc_command_resp(host, cmd, 1, CMD_TIMEOUT) != 0)
			goto done;

		/* for read, the data coming too fast, then CRC error
		   start DMA no business with CRC. */
		//init_completion(&host->xfer_done);
		msdc_dma_start(host);

		spin_unlock(&host->lock);
		if (!wait_for_completion_timeout(&host->xfer_done, DAT_TIMEOUT)) {
			ERR_MSG("XXX CMD<%d> wait xfer_done<%d> timeout!!", cmd->opcode, data->blocks * data->blksz);
			ERR_MSG("    DMA_SA   = 0x%x",
				readl(host->base + MSDC_DMA_SA));
			ERR_MSG("    DMA_CA   = 0x%x",
				readl(host->base + MSDC_DMA_CA));
			ERR_MSG("    DMA_CTRL = 0x%x",
				readl(host->base + MSDC_DMA_CTRL));
			ERR_MSG("    DMA_CFG  = 0x%x",
				readl(host->base + MSDC_DMA_CFG));
			data->error = -ETIMEDOUT;

			msdc_reset_hw(host);
			msdc_clr_fifo(host);
			msdc_clr_int();
		}
		spin_lock(&host->lock);
		msdc_dma_stop(host);

		/* Last: stop transfer */
		if (data->stop) {
			if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0)
				goto done;
		}
	}

done:
	if (data != NULL) {
		host->data = NULL;
		dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
		host->blksz = 0;

#if 0 // don't stop twice!
		if (host->hw->flags & MSDC_REMOVABLE && data->error) {
			msdc_abort_data(host);
			/* reset in IRQ, stop command has issued. -> No need */
		}
#endif

		N_MSG(OPS, "CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>", cmd->opcode, (dma ? "dma" : "pio"),
			(read ? "read " : "write"), data->blksz, data->blocks, data->error);
	}

#if 0 /* --- by chhung */
#if 1
	//if(host->id==1) {
	if (send_type == SND_CMD) {
		if (cmd->opcode == MMC_SEND_STATUS) {
			if ((cmd->resp[0] & CARD_READY_FOR_DATA) || (CARD_CURRENT_STATE(cmd->resp[0]) != 7)) {
				N_MSG(OPS, "disable clock, CMD13 IDLE");
				msdc_gate_clock(host->id);
			}
		} else {
			N_MSG(OPS, "disable clock, CMD<%d>", cmd->opcode);
			msdc_gate_clock(host->id);
		}
	} else {
		if (read) {
			N_MSG(OPS, "disable clock!!! Read CMD<%d>", cmd->opcode);
			msdc_gate_clock(host->id);
		}
	}
	//}
#else
	msdc_gate_clock(host->id);
#endif
#endif /* end of --- */

	if (mrq->cmd->error)
		host->error = 0x001;
	if (mrq->data && mrq->data->error)
		host->error |= 0x010;
	if (mrq->stop && mrq->stop->error)
		host->error |= 0x100;

	//if (host->error) ERR_MSG("host->error<%d>", host->error);

	return host->error;
}

static int msdc_app_cmd(struct mmc_host *mmc, struct msdc_host *host)
{
	struct mmc_command cmd;
	struct mmc_request mrq;
	u32 err;

	memset(&cmd, 0, sizeof(struct mmc_command));
	cmd.opcode = MMC_APP_CMD;
#if 0   /* bug: we meet mmc->card is null when ACMD6 */
	cmd.arg = mmc->card->rca << 16;
#else
	cmd.arg = host->app_cmd_arg;
#endif
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	memset(&mrq, 0, sizeof(struct mmc_request));
	mrq.cmd = &cmd; cmd.mrq = &mrq;
	cmd.data = NULL;

	err = msdc_do_command(host, &cmd, 0, CMD_TIMEOUT);
	return err;
}

static int msdc_tune_cmdrsp(struct msdc_host *host, struct mmc_command *cmd)
{
	int result = -1;
	u32 rsmpl, cur_rsmpl, orig_rsmpl;
	u32 rrdly, cur_rrdly = 0xffffffff, orig_rrdly;
	u32 skip = 1;

	/* ==== don't support 3.0 now ====
	   1: R_SMPL[1]
	   2: PAD_CMD_RESP_RXDLY[26:22]
	   ==========================*/

	// save the previous tune result
	sdr_get_field(host->base + MSDC_IOCON, MSDC_IOCON_RSPL, &orig_rsmpl);
	sdr_get_field(host->base + MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY,
		      &orig_rrdly);

	rrdly = 0;
	do {
		for (rsmpl = 0; rsmpl < 2; rsmpl++) {
			/* Lv1: R_SMPL[1] */
			cur_rsmpl = (orig_rsmpl + rsmpl) % 2;
			if (skip == 1) {
				skip = 0;
				continue;
			}
			sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_RSPL,
				      cur_rsmpl);

			if (host->app_cmd) {
				result = msdc_app_cmd(host->mmc, host);
				if (result) {
					ERR_MSG("TUNE_CMD app_cmd<%d> failed: RESP_RXDLY<%d>,R_SMPL<%d>",
						host->mrq->cmd->opcode, cur_rrdly, cur_rsmpl);
					continue;
				}
			}
			result = msdc_do_command(host, cmd, 0, CMD_TIMEOUT); // not tune.
			ERR_MSG("TUNE_CMD<%d> %s PAD_CMD_RESP_RXDLY[26:22]<%d> R_SMPL[1]<%d>", cmd->opcode,
				(result == 0) ? "PASS" : "FAIL", cur_rrdly, cur_rsmpl);

			if (result == 0)
				return 0;
			if (result != -EIO) {
				ERR_MSG("TUNE_CMD<%d> Error<%d> not -EIO", cmd->opcode, result);
				return result;
			}

			/* should be EIO */
			/* check if has data phase */
			if (readl(host->base + SDC_CMD) & 0x1800)
				msdc_abort_data(host);
		}

		/* Lv2: PAD_CMD_RESP_RXDLY[26:22] */
		cur_rrdly = (orig_rrdly + rrdly + 1) % 32;
		sdr_set_field(host->base + MSDC_PAD_TUNE,
			      MSDC_PAD_TUNE_CMDRRDLY, cur_rrdly);
	} while (++rrdly < 32);

	return result;
}

/* Support SD2.0 Only */
static int msdc_tune_bread(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 ddr = 0;
	u32 dcrc = 0;
	u32 rxdly, cur_rxdly0, cur_rxdly1;
	u32 dsmpl, cur_dsmpl,  orig_dsmpl;
	u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
	u32 cur_dat4,  cur_dat5,  cur_dat6,  cur_dat7;
	u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
	u32 orig_dat4, orig_dat5, orig_dat6, orig_dat7;
	int result = -1;
	u32 skip = 1;

	sdr_get_field(host->base + MSDC_IOCON, MSDC_IOCON_DSPL, &orig_dsmpl);

	/* Tune Method 2. */
	sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);

	rxdly = 0;
	do {
		for (dsmpl = 0; dsmpl < 2; dsmpl++) {
			cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
			if (skip == 1) {
				skip = 0;
				continue;
			}
			sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DSPL,
				      cur_dsmpl);

			if (host->app_cmd) {
				result = msdc_app_cmd(host->mmc, host);
				if (result) {
					ERR_MSG("TUNE_BREAD app_cmd<%d> failed", host->mrq->cmd->opcode);
					continue;
				}
			}
			result = msdc_do_request(mmc, mrq);

			sdr_get_field(host->base + SDC_DCRC_STS,
				      SDC_DCRC_STS_POS | SDC_DCRC_STS_NEG,
				      &dcrc); /* RO */
			if (!ddr)
				dcrc &= ~SDC_DCRC_STS_NEG;
			ERR_MSG("TUNE_BREAD<%s> dcrc<0x%x> DATRDDLY0/1<0x%x><0x%x> dsmpl<0x%x>",
				(result == 0 && dcrc == 0) ? "PASS" : "FAIL", dcrc,
				readl(host->base + MSDC_DAT_RDDLY0),
				readl(host->base + MSDC_DAT_RDDLY1), cur_dsmpl);

			/* Fix me: result is 0, but dcrc is still exist */
			if (result == 0 && dcrc == 0) {
				goto done;
			} else {
				/* there is a case: command timeout, and data phase not processed */
				if (mrq->data->error != 0 &&
				    mrq->data->error != -EIO) {
					ERR_MSG("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>",
						result, mrq->cmd->error, mrq->data->error);
					goto done;
				}
			}
		}

		cur_rxdly0 = readl(host->base + MSDC_DAT_RDDLY0);
		cur_rxdly1 = readl(host->base + MSDC_DAT_RDDLY1);

		/* E1 ECO. YD: Reverse */
		if (readl(host->base + MSDC_ECO_VER) >= 4) {
			orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
			orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
			orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
			orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
			orig_dat4 = (cur_rxdly1 >> 24) & 0x1F;
			orig_dat5 = (cur_rxdly1 >> 16) & 0x1F;
			orig_dat6 = (cur_rxdly1 >>  8) & 0x1F;
			orig_dat7 = (cur_rxdly1 >>  0) & 0x1F;
		} else {
			orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
			orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
			orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
			orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
			orig_dat4 = (cur_rxdly1 >>  0) & 0x1F;
			orig_dat5 = (cur_rxdly1 >>  8) & 0x1F;
			orig_dat6 = (cur_rxdly1 >> 16) & 0x1F;
			orig_dat7 = (cur_rxdly1 >> 24) & 0x1F;
		}

		if (ddr) {
			cur_dat0 = (dcrc & (1 << 0) || dcrc & (1 << 8))  ? ((orig_dat0 + 1) % 32) : orig_dat0;
			cur_dat1 = (dcrc & (1 << 1) || dcrc & (1 << 9))  ? ((orig_dat1 + 1) % 32) : orig_dat1;
			cur_dat2 = (dcrc & (1 << 2) || dcrc & (1 << 10)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
			cur_dat3 = (dcrc & (1 << 3) || dcrc & (1 << 11)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
		} else {
			cur_dat0 = (dcrc & (1 << 0)) ? ((orig_dat0 + 1) % 32) : orig_dat0;
			cur_dat1 = (dcrc & (1 << 1)) ? ((orig_dat1 + 1) % 32) : orig_dat1;
			cur_dat2 = (dcrc & (1 << 2)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
			cur_dat3 = (dcrc & (1 << 3)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
		}
		cur_dat4 = (dcrc & (1 << 4)) ? ((orig_dat4 + 1) % 32) : orig_dat4;
		cur_dat5 = (dcrc & (1 << 5)) ? ((orig_dat5 + 1) % 32) : orig_dat5;
		cur_dat6 = (dcrc & (1 << 6)) ? ((orig_dat6 + 1) % 32) : orig_dat6;
		cur_dat7 = (dcrc & (1 << 7)) ? ((orig_dat7 + 1) % 32) : orig_dat7;

		cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
		cur_rxdly1 = (cur_dat4 << 24) | (cur_dat5 << 16) | (cur_dat6 << 8) | (cur_dat7 << 0);

		writel(cur_rxdly0, host->base + MSDC_DAT_RDDLY0);
		writel(cur_rxdly1, host->base + MSDC_DAT_RDDLY1);

	} while (++rxdly < 32);

done:
	return result;
}

static int msdc_tune_bwrite(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	u32 wrrdly, cur_wrrdly = 0xffffffff, orig_wrrdly;
	u32 dsmpl,  cur_dsmpl,  orig_dsmpl;
	u32 rxdly,  cur_rxdly0;
	u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
	u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
	int result = -1;
	u32 skip = 1;

	// MSDC_IOCON_DDR50CKD need to check. [Fix me]

	sdr_get_field(host->base + MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY,
		      &orig_wrrdly);
	sdr_get_field(host->base + MSDC_IOCON, MSDC_IOCON_DSPL, &orig_dsmpl);

	/* Tune Method 2. just DAT0 */
	sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
	cur_rxdly0 = readl(host->base + MSDC_DAT_RDDLY0);

	/* E1 ECO. YD: Reverse */
	if (readl(host->base + MSDC_ECO_VER) >= 4) {
		orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
		orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
		orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
		orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
	} else {
		orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
		orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
		orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
		orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
	}

	rxdly = 0;
	do {
		wrrdly = 0;
		do {
			for (dsmpl = 0; dsmpl < 2; dsmpl++) {
				cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
				if (skip == 1) {
					skip = 0;
					continue;
				}
				sdr_set_field(host->base + MSDC_IOCON,
					      MSDC_IOCON_DSPL, cur_dsmpl);

				if (host->app_cmd) {
					result = msdc_app_cmd(host->mmc, host);
					if (result) {
						ERR_MSG("TUNE_BWRITE app_cmd<%d> failed", host->mrq->cmd->opcode);
						continue;
					}
				}
				result = msdc_do_request(mmc, mrq);

				ERR_MSG("TUNE_BWRITE<%s> DSPL<%d> DATWRDLY<%d> MSDC_DAT_RDDLY0<0x%x>",
					result == 0 ? "PASS" : "FAIL",
					cur_dsmpl, cur_wrrdly, cur_rxdly0);

				if (result == 0) {
					goto done;
				} else {
					/* there is a case: command timeout, and data phase not processed */
					if (mrq->data->error != -EIO) {
						ERR_MSG("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>",
							result, mrq->cmd->error, mrq->data->error);
						goto done;
					}
				}
			}
			cur_wrrdly = (orig_wrrdly + wrrdly + 1) % 32;
			sdr_set_field(host->base + MSDC_PAD_TUNE,
				      MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly);
		} while (++wrrdly < 32);

		cur_dat0 = (orig_dat0 + rxdly) % 32; /* only adjust bit-1 for crc */
		cur_dat1 = orig_dat1;
		cur_dat2 = orig_dat2;
		cur_dat3 = orig_dat3;

		cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
		writel(cur_rxdly0, host->base + MSDC_DAT_RDDLY0);
	} while (++rxdly < 32);

done:
	return result;
}

static int msdc_get_card_status(struct mmc_host *mmc, struct msdc_host *host, u32 *status)
{
	struct mmc_command cmd;
	struct mmc_request mrq;
	u32 err;

	memset(&cmd, 0, sizeof(struct mmc_command));
	cmd.opcode = MMC_SEND_STATUS;
	if (mmc->card) {
		cmd.arg = mmc->card->rca << 16;
	} else {
		ERR_MSG("cmd13 mmc card is null");
		cmd.arg = host->app_cmd_arg;
	}
	cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

	memset(&mrq, 0, sizeof(struct mmc_request));
	mrq.cmd = &cmd; cmd.mrq = &mrq;
	cmd.data = NULL;

	err = msdc_do_command(host, &cmd, 1, CMD_TIMEOUT);

	if (status)
		*status = cmd.resp[0];

	return err;
}

static int msdc_check_busy(struct mmc_host *mmc, struct msdc_host *host)
{
	u32 err = 0;
	u32 status = 0;

	do {
		err = msdc_get_card_status(mmc, host, &status);
		if (err)
			return err;
		/* need cmd12? */
		ERR_MSG("cmd<13> resp<0x%x>", status);
	} while (R1_CURRENT_STATE(status) == 7);

	return err;
}

/* failed when msdc_do_request */
static int msdc_tune_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_data *data;
	//u32 base = host->base;
	int ret = 0, read;

	data = mrq->cmd->data;

	read = data->flags & MMC_DATA_READ ? 1 : 0;

	if (read) {
		if (data->error == -EIO)
			ret = msdc_tune_bread(mmc, mrq);
	} else {
		ret = msdc_check_busy(mmc, host);
		if (ret) {
			ERR_MSG("XXX cmd13 wait program done failed");
			return ret;
		}
		/* CRC and TO */
		/* Fix me: don't care card status? */
		ret = msdc_tune_bwrite(mmc, mrq);
	}

	return ret;
}

/* ops.request */
static void msdc_ops_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	//=== for sdio profile ===
#if 0 /* --- by chhung */
	u32 old_H32, old_L32, new_H32, new_L32;
	u32 ticks = 0, opcode = 0, sizes = 0, bRx = 0;
#endif /* end of --- */

	WARN_ON(host->mrq);

	/* start to process */
	spin_lock(&host->lock);
#if 0 /* --- by chhung */
	if (sdio_pro_enable) {  //=== for sdio profile ===
		if (mrq->cmd->opcode == 52 || mrq->cmd->opcode == 53)
			GPT_GetCounter64(&old_L32, &old_H32);
	}
#endif /* end of --- */

	host->mrq = mrq;

	if (msdc_do_request(mmc, mrq)) {
		if (host->hw->flags & MSDC_REMOVABLE && ralink_soc == MT762X_SOC_MT7621AT && mrq->data && mrq->data->error)
			msdc_tune_request(mmc, mrq);
	}

	/* ==== when request done, check if app_cmd ==== */
	if (mrq->cmd->opcode == MMC_APP_CMD) {
		host->app_cmd = 1;
		host->app_cmd_arg = mrq->cmd->arg;  /* save the RCA */
	} else {
		host->app_cmd = 0;
		//host->app_cmd_arg = 0;
	}

	host->mrq = NULL;

#if 0 /* --- by chhung */
	//=== for sdio profile ===
	if (sdio_pro_enable) {
		if (mrq->cmd->opcode == 52 || mrq->cmd->opcode == 53) {
			GPT_GetCounter64(&new_L32, &new_H32);
			ticks = msdc_time_calc(old_L32, old_H32, new_L32, new_H32);

			opcode = mrq->cmd->opcode;
			if (mrq->cmd->data) {
				sizes = mrq->cmd->data->blocks * mrq->cmd->data->blksz;
				bRx = mrq->cmd->data->flags & MMC_DATA_READ ? 1 : 0;
			} else {
				bRx = mrq->cmd->arg	& 0x80000000 ? 1 : 0;
			}

			if (!mrq->cmd->error)
				msdc_performance(opcode, sizes, bRx, ticks);
		}
	}
#endif /* end of --- */
	spin_unlock(&host->lock);

	mmc_request_done(mmc, mrq);

	return;
}

/* called by ops.set_ios */
static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
	u32 val = readl(host->base + SDC_CFG);

	val &= ~SDC_CFG_BUSWIDTH;

	switch (width) {
	default:
	case MMC_BUS_WIDTH_1:
		width = 1;
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

	N_MSG(CFG, "Bus Width = %d", width);
}

/* ops.set_ios */
static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	u32 ddr = 0;

#ifdef MT6575_SD_DEBUG
	static char *vdd[] = {
		"1.50v", "1.55v", "1.60v", "1.65v", "1.70v", "1.80v", "1.90v",
		"2.00v", "2.10v", "2.20v", "2.30v", "2.40v", "2.50v", "2.60v",
		"2.70v", "2.80v", "2.90v", "3.00v", "3.10v", "3.20v", "3.30v",
		"3.40v", "3.50v", "3.60v"
	};
	static char *power_mode[] = {
		"OFF", "UP", "ON"
	};
	static char *bus_mode[] = {
		"UNKNOWN", "OPENDRAIN", "PUSHPULL"
	};
	static char *timing[] = {
		"LEGACY", "MMC_HS", "SD_HS"
	};

	printk("SET_IOS: CLK(%dkHz), BUS(%s), BW(%u), PWR(%s), VDD(%s), TIMING(%s)",
		ios->clock / 1000, bus_mode[ios->bus_mode],
		(ios->bus_width == MMC_BUS_WIDTH_4) ? 4 : 1,
		power_mode[ios->power_mode], vdd[ios->vdd], timing[ios->timing]);
#endif

	msdc_set_buswidth(host, ios->bus_width);

	/* Power control ??? */
	switch (ios->power_mode) {
	case MMC_POWER_OFF:
	case MMC_POWER_UP:
		// msdc_set_power_mode(host, ios->power_mode); /* --- by chhung */
		break;
	case MMC_POWER_ON:
		host->power_mode = MMC_POWER_ON;
		break;
	default:
		break;
	}

	/* Clock control */
	if (host->mclk != ios->clock) {
		if (ios->clock > 25000000) {
			//if (!(host->hw->flags & MSDC_REMOVABLE)) {
			INIT_MSG("SD data latch edge<%d>", MSDC_SMPL_FALLING);
			sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_RSPL,
				      MSDC_SMPL_FALLING);
			sdr_set_field(host->base + MSDC_IOCON, MSDC_IOCON_DSPL,
				      MSDC_SMPL_FALLING);
			//} /* for tuning debug */
		} else { /* default value */
			writel(0x00000000, host->base + MSDC_IOCON);
			// writel(0x00000000, host->base + MSDC_DAT_RDDLY0);

			// for MT7620 E2 and afterward
			writel(0x10101010, host->base + MSDC_DAT_RDDLY0);

			writel(0x00000000, host->base + MSDC_DAT_RDDLY1);
			// writel(0x00000000, host->base + MSDC_PAD_TUNE);

			// for MT7620 E2 and afterward
			writel(0x84101010, host->base + MSDC_PAD_TUNE);
		}
		msdc_set_mclk(host, ddr, ios->clock);
	}
}

/* ops.get_ro */
static int msdc_ops_get_ro(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned long flags;
	int ro = 0;

	if (host->hw->flags & MSDC_WP_PIN_EN) { /* set for card */
		spin_lock_irqsave(&host->lock, flags);
		ro = (readl(host->base + MSDC_PS) >> 31);
		spin_unlock_irqrestore(&host->lock, flags);
	}
	return ro;
}

/* ops.get_cd */
static int msdc_ops_get_cd(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned long flags;
	int present = 1;

	/* for sdio, MSDC_REMOVABLE not set, always return 1 */
	if (!(host->hw->flags & MSDC_REMOVABLE)) {
		/* For sdio, read H/W always get<1>, but may timeout some times */
#if 1
		host->card_inserted = 1;
		return 1;
#else
		host->card_inserted = (host->pm_state.event == PM_EVENT_USER_RESUME) ? 1 : 0;
		INIT_MSG("sdio ops_get_cd<%d>", host->card_inserted);
		return host->card_inserted;
#endif
	}

	/* MSDC_CD_PIN_EN set for card */
	if (host->hw->flags & MSDC_CD_PIN_EN) {
		spin_lock_irqsave(&host->lock, flags);
#if 0
		present = host->card_inserted;  /* why not read from H/W: Fix me*/
#else
		// CD
		present = readl(host->base + MSDC_PS) & MSDC_PS_CDSTS;
		if (cd_active_low)
			present = present ? 0 : 1;
		else
			present = present ? 1 : 0;
		host->card_inserted = present;
#endif
		spin_unlock_irqrestore(&host->lock, flags);
	} else {
		present = 0; /* TODO? Check DAT3 pins for card detection */
	}

	INIT_MSG("ops_get_cd return<%d>", present);
	return present;
}

static struct mmc_host_ops mt_msdc_ops = {
	.request         = msdc_ops_request,
	.set_ios         = msdc_ops_set_ios,
	.get_ro          = msdc_ops_get_ro,
	.get_cd          = msdc_ops_get_cd,
};

/*--------------------------------------------------------------------------*/
/* interrupt handler                                                    */
/*--------------------------------------------------------------------------*/
static irqreturn_t msdc_irq(int irq, void *dev_id)
{
	struct msdc_host  *host = (struct msdc_host *)dev_id;
	struct mmc_data   *data = host->data;
	struct mmc_command *cmd = host->cmd;

	u32 cmdsts = MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  | MSDC_INT_CMDRDY  |
		MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY |
		MSDC_INT_ACMD19_DONE;
	u32 datsts = MSDC_INT_DATCRCERR | MSDC_INT_DATTMO;

	u32 intsts = readl(host->base + MSDC_INT);
	u32 inten  = readl(host->base + MSDC_INTEN); inten &= intsts;

	writel(intsts, host->base + MSDC_INT);  /* clear interrupts */
	/* MSG will cause fatal error */

	/* card change interrupt */
	if (intsts & MSDC_INT_CDSC) {
		if (host->mmc->caps & MMC_CAP_NEEDS_POLL)
			return IRQ_HANDLED;
		IRQ_MSG("MSDC_INT_CDSC irq<0x%.8x>", intsts);
		schedule_delayed_work(&host->card_delaywork, HZ);
		/* tuning when plug card ? */
	}

	/* sdio interrupt */
	if (intsts & MSDC_INT_SDIOIRQ) {
		IRQ_MSG("XXX MSDC_INT_SDIOIRQ");  /* seems not sdio irq */
		//mmc_signal_sdio_irq(host->mmc);
	}

	/* transfer complete interrupt */
	if (data != NULL) {
		if (inten & MSDC_INT_XFER_COMPL) {
			data->bytes_xfered = host->xfer_size;
			complete(&host->xfer_done);
		}

		if (intsts & datsts) {
			/* do basic reset, or stop command will sdc_busy */
			msdc_reset_hw(host);
			msdc_clr_fifo(host);
			msdc_clr_int();

			if (intsts & MSDC_INT_DATTMO) {
				IRQ_MSG("XXX CMD<%d> MSDC_INT_DATTMO", host->mrq->cmd->opcode);
				data->error = -ETIMEDOUT;
			} else if (intsts & MSDC_INT_DATCRCERR) {
				IRQ_MSG("XXX CMD<%d> MSDC_INT_DATCRCERR, SDC_DCRC_STS<0x%x>", host->mrq->cmd->opcode, readl(host->base + SDC_DCRC_STS));
				data->error = -EIO;
			}

			//if(readl(MSDC_INTEN) & MSDC_INT_XFER_COMPL) {
			complete(&host->xfer_done); /* Read CRC come fast, XFER_COMPL not enabled */
		}
	}

	/* command interrupts */
	if ((cmd != NULL) && (intsts & cmdsts)) {
		if ((intsts & MSDC_INT_CMDRDY) || (intsts & MSDC_INT_ACMDRDY) ||
			(intsts & MSDC_INT_ACMD19_DONE)) {
			u32 *rsp = &cmd->resp[0];

			switch (host->cmd_rsp) {
			case RESP_NONE:
				break;
			case RESP_R2:
				*rsp++ = readl(host->base + SDC_RESP3);
				*rsp++ = readl(host->base + SDC_RESP2);
				*rsp++ = readl(host->base + SDC_RESP1);
				*rsp++ = readl(host->base + SDC_RESP0);
				break;
			default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
				if ((intsts & MSDC_INT_ACMDRDY) || (intsts & MSDC_INT_ACMD19_DONE))
					*rsp = readl(host->base + SDC_ACMD_RESP);
				else
					*rsp = readl(host->base + SDC_RESP0);
				break;
			}
		} else if ((intsts & MSDC_INT_RSPCRCERR) || (intsts & MSDC_INT_ACMDCRCERR)) {
			if (intsts & MSDC_INT_ACMDCRCERR)
				IRQ_MSG("XXX CMD<%d> MSDC_INT_ACMDCRCERR", cmd->opcode);
			else
				IRQ_MSG("XXX CMD<%d> MSDC_INT_RSPCRCERR", cmd->opcode);
			cmd->error = -EIO;
		} else if ((intsts & MSDC_INT_CMDTMO) || (intsts & MSDC_INT_ACMDTMO)) {
			if (intsts & MSDC_INT_ACMDTMO)
				IRQ_MSG("XXX CMD<%d> MSDC_INT_ACMDTMO", cmd->opcode);
			else
				IRQ_MSG("XXX CMD<%d> MSDC_INT_CMDTMO", cmd->opcode);
			cmd->error = -ETIMEDOUT;
			msdc_reset_hw(host);
			msdc_clr_fifo(host);
			msdc_clr_int();
		}
		complete(&host->cmd_done);
	}

	/* mmc irq interrupts */
	if (intsts & MSDC_INT_MMCIRQ)
		printk(KERN_INFO "msdc[%d] MMCIRQ: SDC_CSTS=0x%.8x\r\n",
		       host->id, readl(host->base + SDC_CSTS));

#ifdef MT6575_SD_DEBUG
	{
/*        msdc_int_reg *int_reg = (msdc_int_reg*)&intsts;*/
		N_MSG(INT, "IRQ_EVT(0x%x): MMCIRQ(%d) CDSC(%d), ACRDY(%d), ACTMO(%d), ACCRE(%d) AC19DN(%d)",
			intsts,
			int_reg->mmcirq,
			int_reg->cdsc,
			int_reg->atocmdrdy,
			int_reg->atocmdtmo,
			int_reg->atocmdcrc,
			int_reg->atocmd19done);
		N_MSG(INT, "IRQ_EVT(0x%x): SDIO(%d) CMDRDY(%d), CMDTMO(%d), RSPCRC(%d), CSTA(%d)",
			intsts,
			int_reg->sdioirq,
			int_reg->cmdrdy,
			int_reg->cmdtmo,
			int_reg->rspcrc,
			int_reg->csta);
		N_MSG(INT, "IRQ_EVT(0x%x): XFCMP(%d) DXDONE(%d), DATTMO(%d), DATCRC(%d), DMAEMP(%d)",
			intsts,
			int_reg->xfercomp,
			int_reg->dxferdone,
			int_reg->dattmo,
			int_reg->datcrc,
			int_reg->dmaqempty);
	}
#endif

	return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/
/* platform_driver members                                                      */
/*--------------------------------------------------------------------------*/
/* called by msdc_drv_probe/remove */
static void msdc_enable_cd_irq(struct msdc_host *host, int enable)
{
	struct msdc_hw *hw = host->hw;

	/* for sdio, not set */
	if ((hw->flags & MSDC_CD_PIN_EN) == 0) {
		/* Pull down card detection pin since it is not avaiable */
		/*
		  if (hw->config_gpio_pin)
		  hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);
		*/
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
		return;
	}

	N_MSG(CFG, "CD IRQ Enable(%d)", enable);

	if (enable) {
		/* card detection circuit relies on the core power so that the core power
		 * shouldn't be turned off. Here adds a reference count to keep
		 * the core power alive.
		 */
		//msdc_vcore_on(host); //did in msdc_init_hw()

		if (hw->config_gpio_pin) /* NULL */
			hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_UP);

		sdr_set_field(host->base + MSDC_PS, MSDC_PS_CDDEBOUNCE,
			      DEFAULT_DEBOUNCE);
		sdr_set_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_set_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);

		/* not in document! Fix me */
		sdr_set_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
	} else {
		if (hw->config_gpio_pin) /* NULL */
			hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);

		sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_INSWKUP);
		sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(host->base + MSDC_INTEN, MSDC_INTEN_CDSC);

		/* Here decreases a reference count to core power since card
		 * detection circuit is shutdown.
		 */
		//msdc_vcore_off(host);
	}
}

/* called by msdc_drv_probe */
static void msdc_init_hw(struct msdc_host *host)
{

	/* Power on */
#if 0 /* --- by chhung */
	msdc_vcore_on(host);
	msdc_pin_reset(host, MSDC_PIN_PULL_UP);
	msdc_select_clksrc(host, hw->clk_src);
	enable_clock(PERI_MSDC0_PDN + host->id, "SD");
	msdc_vdd_on(host);
#endif /* end of --- */
	/* Configure to MMC/SD mode */
	sdr_set_field(host->base + MSDC_CFG, MSDC_CFG_MODE, MSDC_SDMMC);

	/* Reset */
	msdc_reset_hw(host);
	msdc_clr_fifo(host);

	/* Disable card detection */
	sdr_clr_bits(host->base + MSDC_PS, MSDC_PS_CDEN);

	/* Disable and clear all interrupts */
	sdr_clr_bits(host->base + MSDC_INTEN, readl(host->base + MSDC_INTEN));
	writel(readl(host->base + MSDC_INT), host->base + MSDC_INT);

#if 1
	/* reset tuning parameter */
	writel(0x00090000, host->base + MSDC_PAD_CTL0);
	writel(0x000A0000, host->base + MSDC_PAD_CTL1);
	writel(0x000A0000, host->base + MSDC_PAD_CTL2);
	// writel(  0x00000000, host->base + MSDC_PAD_TUNE);

	// for MT7620 E2 and afterward
	writel(0x84101010, host->base + MSDC_PAD_TUNE);

	// writel(0x00000000, host->base + MSDC_DAT_RDDLY0);

	// for MT7620 E2 and afterward
	writel(0x10101010, host->base + MSDC_DAT_RDDLY0);

	writel(0x00000000, host->base + MSDC_DAT_RDDLY1);
	writel(0x00000000, host->base + MSDC_IOCON);
#if 0 // use MT7620 default value: 0x403c004f
	/* bit0 modified: Rx Data Clock Source: 1 -> 2.0*/
	writel(0x003C000F, host->base + MSDC_PATCH_BIT0);
#endif

	if (readl(host->base + MSDC_ECO_VER) >= 4) {
		if (host->id == 1) {
			sdr_set_field(host->base + MSDC_PATCH_BIT1,
				      MSDC_PATCH_BIT1_WRDAT_CRCS, 1);
			sdr_set_field(host->base + MSDC_PATCH_BIT1,
				      MSDC_PATCH_BIT1_CMD_RSP,    1);

			/* internal clock: latch read data */
			sdr_set_bits(host->base + MSDC_PATCH_BIT0,
				     MSDC_PATCH_BIT_CKGEN_CK);
		}
	}
#endif

	/* for safety, should clear SDC_CFG.SDIO_INT_DET_EN & set SDC_CFG.SDIO in
	   pre-loader,uboot,kernel drivers. and SDC_CFG.SDIO_INT_DET_EN will be only
	   set when kernel driver wants to use SDIO bus interrupt */
	/* Configure to enable SDIO mode. it's must otherwise sdio cmd5 failed */
	sdr_set_bits(host->base + SDC_CFG, SDC_CFG_SDIO);

	/* disable detect SDIO device interupt function */
	sdr_clr_bits(host->base + SDC_CFG, SDC_CFG_SDIOIDE);

	/* eneable SMT for glitch filter */
	sdr_set_bits(host->base + MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKSMT);
	sdr_set_bits(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDSMT);
	sdr_set_bits(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATSMT);

#if 1
	/* set clk, cmd, dat pad driving */
	sdr_set_field(host->base + MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, 4);
	sdr_set_field(host->base + MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, 4);
	sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, 4);
	sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, 4);
	sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, 4);
	sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, 4);
#else
	sdr_set_field(host->base + MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, 0);
	sdr_set_field(host->base + MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, 0);
	sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, 0);
	sdr_set_field(host->base + MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, 0);
	sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, 0);
	sdr_set_field(host->base + MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, 0);
#endif

	/* set sampling edge */

	/* write crc timeout detection */
	sdr_set_field(host->base + MSDC_PATCH_BIT0, 1 << 30, 1);

	/* Configure to default data timeout */
	sdr_set_field(host->base + SDC_CFG, SDC_CFG_DTOC, DEFAULT_DTOC);

	msdc_set_buswidth(host, MMC_BUS_WIDTH_1);

	N_MSG(FUC, "init hardware done!");
}

/* called by msdc_drv_remove */
static void msdc_deinit_hw(struct msdc_host *host)
{
	/* Disable and clear all interrupts */
	sdr_clr_bits(host->base + MSDC_INTEN, readl(host->base + MSDC_INTEN));
	writel(readl(host->base + MSDC_INT), host->base + MSDC_INT);

	/* Disable card detection */
	msdc_enable_cd_irq(host, 0);
	// msdc_set_power_mode(host, MMC_POWER_OFF);   /* make sure power down */ /* --- by chhung */
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
	struct gpd *gpd = dma->gpd;
	struct bd  *bd  = dma->bd;
	int i;

	/* we just support one gpd, but gpd->next must be set for desc
	 * DMA. That's why we alloc 2 gpd structurs.
	 */

	memset(gpd, 0, sizeof(struct gpd) * 2);

	gpd->bdp  = 1;   /* hwo, cs, bd pointer */
	gpd->ptr = (void *)dma->bd_addr; /* physical address */
	gpd->next = (void *)((u32)dma->gpd_addr + sizeof(struct gpd));

	memset(bd, 0, sizeof(struct bd) * MAX_BD_NUM);
	for (i = 0; i < (MAX_BD_NUM - 1); i++)
		bd[i].next = (void *)(dma->bd_addr + sizeof(*bd) * (i + 1));
}

static int msdc_drv_probe(struct platform_device *pdev)
{
	struct resource *res;
	__iomem void *base;
	struct mmc_host *mmc;
	struct msdc_host *host;
	struct msdc_hw *hw;
	int ret;

	hw = &msdc0_hw;

	if (of_property_read_bool(pdev->dev.of_node, "mtk,wp-en"))
		msdc0_hw.flags |= MSDC_WP_PIN_EN;

	/* Allocate MMC host for this device */
	mmc = mmc_alloc_host(sizeof(struct msdc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto host_free;
	}

	/* Set host parameters to mmc */
	mmc->ops        = &mt_msdc_ops;
	mmc->f_min      = HOST_MIN_MCLK;
	mmc->f_max      = HOST_MAX_MCLK;
	mmc->ocr_avail  = MSDC_OCR_AVAIL;

	mmc->caps   = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;

	//TODO: read this as bus-width from dt (via mmc_of_parse)
	mmc->caps  |= MMC_CAP_4_BIT_DATA;

	cd_active_low = !of_property_read_bool(pdev->dev.of_node, "mediatek,cd-high");

	if (of_property_read_bool(pdev->dev.of_node, "mediatek,cd-poll"))
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	/* MMC core transfer sizes tunable parameters */
	mmc->max_segs      = MAX_HW_SGMTS;

	mmc->max_seg_size  = MAX_SGMT_SZ;
	mmc->max_blk_size  = HOST_MAX_BLKSZ;
	mmc->max_req_size  = MAX_REQ_SZ;
	mmc->max_blk_count = mmc->max_req_size;

	host = mmc_priv(mmc);
	host->hw        = hw;
	host->mmc       = mmc;
	host->id        = pdev->id;
	if (host->id < 0 || host->id >= 4)
		host->id = 0;
	host->error     = 0;

	host->irq       = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = -EINVAL;
		goto host_free;
	}

	host->base      = base;
	host->mclk      = 0;                   /* mclk: the request clock of mmc sub-system */
	host->hclk      = hclks[hw->clk_src];  /* hclk: clock of clock source to msdc controller */
	host->sclk      = 0;                   /* sclk: the really clock after divition */
	host->pm_state  = PMSG_RESUME;
	host->suspend   = 0;
	host->core_clkon = 0;
	host->card_clkon = 0;
	host->core_power = 0;
	host->power_mode = MMC_POWER_OFF;
//    host->card_inserted = hw->flags & MSDC_REMOVABLE ? 0 : 1;
	host->timeout_ns = 0;
	host->timeout_clks = DEFAULT_DTOC * 65536;

	host->mrq = NULL;
	//init_MUTEX(&host->sem); /* we don't need to support multiple threads access */

	mmc_dev(mmc)->dma_mask = NULL;

	/* using dma_alloc_coherent*/  /* todo: using 1, for all 4 slots */
	host->dma.gpd = dma_alloc_coherent(&pdev->dev,
					   MAX_GPD_NUM * sizeof(struct gpd),
					   &host->dma.gpd_addr, GFP_KERNEL);
	host->dma.bd =  dma_alloc_coherent(&pdev->dev,
					   MAX_BD_NUM  * sizeof(struct bd),
					   &host->dma.bd_addr,  GFP_KERNEL);
	if (!host->dma.gpd || !host->dma.bd) {
		ret = -ENOMEM;
		goto release_mem;
	}
	msdc_init_gpd_bd(host, &host->dma);

	INIT_DELAYED_WORK(&host->card_delaywork, msdc_tasklet_card);
	spin_lock_init(&host->lock);
	msdc_init_hw(host);

	/* TODO check weather flags 0 is correct, the mtk-sd driver uses
	 * IRQF_TRIGGER_LOW | IRQF_ONESHOT for flags
	 *
	 * for flags 0 the trigger polarity is determined by the
	 * device tree, but not the oneshot flag, but maybe it is also
	 * not needed because the soc could be oneshot safe.
	 */
	ret = devm_request_irq(&pdev->dev, host->irq, msdc_irq, 0, pdev->name,
			       host);
	if (ret)
		goto release;

	platform_set_drvdata(pdev, mmc);

	ret = mmc_add_host(mmc);
	if (ret)
		goto release;

	/* Config card detection pin and enable interrupts */
	if (hw->flags & MSDC_CD_PIN_EN) {  /* set for card */
		msdc_enable_cd_irq(host, 1);
	} else {
		msdc_enable_cd_irq(host, 0);
	}

	return 0;

release:
	platform_set_drvdata(pdev, NULL);
	msdc_deinit_hw(host);
	cancel_delayed_work_sync(&host->card_delaywork);

release_mem:
	if (host->dma.gpd)
		dma_free_coherent(&pdev->dev, MAX_GPD_NUM * sizeof(struct gpd),
				  host->dma.gpd, host->dma.gpd_addr);
	if (host->dma.bd)
		dma_free_coherent(&pdev->dev, MAX_BD_NUM * sizeof(struct bd),
				  host->dma.bd, host->dma.bd_addr);
host_free:
	mmc_free_host(mmc);

	return ret;
}

/* 4 device share one driver, using "drvdata" to show difference */
static int msdc_drv_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;

	mmc  = platform_get_drvdata(pdev);
	BUG_ON(!mmc);

	host = mmc_priv(mmc);
	BUG_ON(!host);

	ERR_MSG("removed !!!");

	platform_set_drvdata(pdev, NULL);
	mmc_remove_host(host->mmc);
	msdc_deinit_hw(host);

	cancel_delayed_work_sync(&host->card_delaywork);

	dma_free_coherent(&pdev->dev, MAX_GPD_NUM * sizeof(struct gpd),
			  host->dma.gpd, host->dma.gpd_addr);
	dma_free_coherent(&pdev->dev, MAX_BD_NUM  * sizeof(struct bd),
			  host->dma.bd,  host->dma.bd_addr);

	mmc_free_host(host->mmc);

	return 0;
}

/* Fix me: Power Flow */
#ifdef CONFIG_PM

static void msdc_drv_pm(struct platform_device *pdev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	if (mmc) {
		struct msdc_host *host = mmc_priv(mmc);
		msdc_pm(state, (void *)host);
	}
}

static int msdc_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	if (state.event == PM_EVENT_SUSPEND)
		msdc_drv_pm(pdev, state);
	return 0;
}

static int msdc_drv_resume(struct platform_device *pdev)
{
	struct pm_message state;

	state.event = PM_EVENT_RESUME;
	msdc_drv_pm(pdev, state);
	return 0;
}
#endif

static const struct of_device_id mt7620_sdhci_match[] = {
	{ .compatible = "ralink,mt7620-sdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7620_sdhci_match);

static struct platform_driver mt_msdc_driver = {
	.probe   = msdc_drv_probe,
	.remove  = msdc_drv_remove,
#ifdef CONFIG_PM
	.suspend = msdc_drv_suspend,
	.resume  = msdc_drv_resume,
#endif
	.driver  = {
		.name  = DRV_NAME,
		.of_match_table = mt7620_sdhci_match,
	},
};

/*--------------------------------------------------------------------------*/
/* module init/exit                                                      */
/*--------------------------------------------------------------------------*/
static int __init mt_msdc_init(void)
{
	int ret;
	u32 reg;

	// Set the pins for sdxc to sdxc mode
	//FIXME: this should be done by pinctl and not by the sd driver
	reg = readl((void __iomem *)(RALINK_SYSCTL_BASE + 0x60)) & ~(0x3 << 18);
	writel(reg, (void __iomem *)(RALINK_SYSCTL_BASE + 0x60));

	ret = platform_driver_register(&mt_msdc_driver);
	if (ret) {
		printk(KERN_ERR DRV_NAME ": Can't register driver");
		return ret;
	}

#if defined(MT6575_SD_DEBUG)
	msdc_debug_proc_init();
#endif
	return 0;
}

static void __exit mt_msdc_exit(void)
{
	platform_driver_unregister(&mt_msdc_driver);
}

module_init(mt_msdc_init);
module_exit(mt_msdc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MT6575 SD/MMC Card Driver");
MODULE_AUTHOR("Infinity Chen <infinity.chen@mediatek.com>");

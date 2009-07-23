/* Definitons for use with the tmio_mmc.c
 *
 * (c) 2004 Ian Molton <spyro@f2s.com>
 * (c) 2007 Ian Molton <spyro@f2s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/highmem.h>

#define CNF_CMD     0x04
#define CNF_CTL_BASE   0x10
#define CNF_INT_PIN  0x3d
#define CNF_STOP_CLK_CTL 0x40
#define CNF_GCLK_CTL 0x41
#define CNF_SD_CLK_MODE 0x42
#define CNF_PIN_STATUS 0x44
#define CNF_PWR_CTL_1 0x48
#define CNF_PWR_CTL_2 0x49
#define CNF_PWR_CTL_3 0x4a
#define CNF_CARD_DETECT_MODE 0x4c
#define CNF_SD_SLOT 0x50
#define CNF_EXT_GCLK_CTL_1 0xf0
#define CNF_EXT_GCLK_CTL_2 0xf1
#define CNF_EXT_GCLK_CTL_3 0xf9
#define CNF_SD_LED_EN_1 0xfa
#define CNF_SD_LED_EN_2 0xfe

#define   SDCREN 0x2   /* Enable access to MMC CTL regs. (flag in COMMAND_REG)*/

#define CTL_SD_CMD 0x00
#define CTL_ARG_REG 0x04
#define CTL_STOP_INTERNAL_ACTION 0x08
#define CTL_XFER_BLK_COUNT 0xa
#define CTL_RESPONSE 0x0c
#define CTL_STATUS 0x1c
#define CTL_IRQ_MASK 0x20
#define CTL_SD_CARD_CLK_CTL 0x24
#define CTL_SD_XFER_LEN 0x26
#define CTL_SD_MEM_CARD_OPT 0x28
#define CTL_SD_ERROR_DETAIL_STATUS 0x2c
#define CTL_SD_DATA_PORT 0x30
#define CTL_TRANSACTION_CTL 0x34
#define CTL_RESET_SD 0xe0
#define CTL_SDIO_REGS 0x100
#define CTL_CLK_AND_WAIT_CTL 0x138
#define CTL_RESET_SDIO 0x1e0

/* Definitions for values the CTRL_STATUS register can take. */
#define TMIO_STAT_CMDRESPEND    0x00000001
#define TMIO_STAT_DATAEND       0x00000004
#define TMIO_STAT_CARD_REMOVE   0x00000008
#define TMIO_STAT_CARD_INSERT   0x00000010
#define TMIO_STAT_SIGSTATE      0x00000020
#define TMIO_STAT_WRPROTECT     0x00000080
#define TMIO_STAT_CARD_REMOVE_A 0x00000100
#define TMIO_STAT_CARD_INSERT_A 0x00000200
#define TMIO_STAT_SIGSTATE_A    0x00000400
#define TMIO_STAT_CMD_IDX_ERR   0x00010000
#define TMIO_STAT_CRCFAIL       0x00020000
#define TMIO_STAT_STOPBIT_ERR   0x00040000
#define TMIO_STAT_DATATIMEOUT   0x00080000
#define TMIO_STAT_RXOVERFLOW    0x00100000
#define TMIO_STAT_TXUNDERRUN    0x00200000
#define TMIO_STAT_CMDTIMEOUT    0x00400000
#define TMIO_STAT_RXRDY         0x01000000
#define TMIO_STAT_TXRQ          0x02000000
#define TMIO_STAT_ILL_FUNC      0x20000000
#define TMIO_STAT_CMD_BUSY      0x40000000
#define TMIO_STAT_ILL_ACCESS    0x80000000

/* Define some IRQ masks */
/* This is the mask used at reset by the chip */
#define TMIO_MASK_ALL           0x837f031d
#define TMIO_MASK_READOP  (TMIO_STAT_RXRDY | TMIO_STAT_DATAEND | \
		TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_WRITEOP (TMIO_STAT_TXRQ | TMIO_STAT_DATAEND | \
		TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_CMD     (TMIO_STAT_CMDRESPEND | TMIO_STAT_CMDTIMEOUT | \
		TMIO_STAT_CARD_REMOVE | TMIO_STAT_CARD_INSERT)
#define TMIO_MASK_IRQ     (TMIO_MASK_READOP | TMIO_MASK_WRITEOP | TMIO_MASK_CMD)


#define enable_mmc_irqs(host, i) \
	do { \
		u32 mask;\
		mask  = sd_ctrl_read32((host), CTL_IRQ_MASK); \
		mask &= ~((i) & TMIO_MASK_IRQ); \
		sd_ctrl_write32((host), CTL_IRQ_MASK, mask); \
	} while (0)

#define disable_mmc_irqs(host, i) \
	do { \
		u32 mask;\
		mask  = sd_ctrl_read32((host), CTL_IRQ_MASK); \
		mask |= ((i) & TMIO_MASK_IRQ); \
		sd_ctrl_write32((host), CTL_IRQ_MASK, mask); \
	} while (0)

#define ack_mmc_irqs(host, i) \
	do { \
		u32 mask;\
		mask  = sd_ctrl_read32((host), CTL_STATUS); \
		mask &= ~((i) & TMIO_MASK_IRQ); \
		sd_ctrl_write32((host), CTL_STATUS, mask); \
	} while (0)


struct tmio_mmc_host {
	void __iomem *cnf;
	void __iomem *ctl;
	unsigned long bus_shift;
	struct mmc_command      *cmd;
	struct mmc_request      *mrq;
	struct mmc_data         *data;
	struct mmc_host         *mmc;
	int                     irq;

	/* pio related stuff */
	struct scatterlist      *sg_ptr;
	unsigned int            sg_len;
	unsigned int            sg_off;
};

#include <linux/io.h>

static inline u16 sd_ctrl_read16(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift));
}

static inline void sd_ctrl_read16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	readsw(host->ctl + (addr << host->bus_shift), buf, count);
}

static inline u32 sd_ctrl_read32(struct tmio_mmc_host *host, int addr)
{
	return readw(host->ctl + (addr << host->bus_shift)) |
	       readw(host->ctl + ((addr + 2) << host->bus_shift)) << 16;
}

static inline void sd_ctrl_write16(struct tmio_mmc_host *host, int addr,
		u16 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
}

static inline void sd_ctrl_write16_rep(struct tmio_mmc_host *host, int addr,
		u16 *buf, int count)
{
	writesw(host->ctl + (addr << host->bus_shift), buf, count);
}

static inline void sd_ctrl_write32(struct tmio_mmc_host *host, int addr,
		u32 val)
{
	writew(val, host->ctl + (addr << host->bus_shift));
	writew(val >> 16, host->ctl + ((addr + 2) << host->bus_shift));
}

static inline void sd_config_write8(struct tmio_mmc_host *host, int addr,
		u8 val)
{
	writeb(val, host->cnf + (addr << host->bus_shift));
}

static inline void sd_config_write16(struct tmio_mmc_host *host, int addr,
		u16 val)
{
	writew(val, host->cnf + (addr << host->bus_shift));
}

static inline void sd_config_write32(struct tmio_mmc_host *host, int addr,
		u32 val)
{
	writew(val, host->cnf + (addr << host->bus_shift));
	writew(val >> 16, host->cnf + ((addr + 2) << host->bus_shift));
}

#include <linux/scatterlist.h>
#include <linux/blkdev.h>

static inline void tmio_mmc_init_sg(struct tmio_mmc_host *host,
	struct mmc_data *data)
{
	host->sg_len = data->sg_len;
	host->sg_ptr = data->sg;
	host->sg_off = 0;
}

static inline int tmio_mmc_next_sg(struct tmio_mmc_host *host)
{
	host->sg_ptr = sg_next(host->sg_ptr);
	host->sg_off = 0;
	return --host->sg_len;
}

static inline char *tmio_mmc_kmap_atomic(struct tmio_mmc_host *host,
	unsigned long *flags)
{
	struct scatterlist *sg = host->sg_ptr;

	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
}

static inline void tmio_mmc_kunmap_atomic(struct tmio_mmc_host *host,
	unsigned long *flags)
{
	kunmap_atomic(sg_page(host->sg_ptr), KM_BIO_SRC_IRQ);
	local_irq_restore(*flags);
}

#ifdef CONFIG_MMC_DEBUG

#define STATUS_TO_TEXT(a) \
	do { \
		if (status & TMIO_STAT_##a) \
			printk(#a); \
	} while (0)

void pr_debug_status(u32 status)
{
	printk(KERN_DEBUG "status: %08x = ", status);
	STATUS_TO_TEXT(CARD_REMOVE);
	STATUS_TO_TEXT(CARD_INSERT);
	STATUS_TO_TEXT(SIGSTATE);
	STATUS_TO_TEXT(WRPROTECT);
	STATUS_TO_TEXT(CARD_REMOVE_A);
	STATUS_TO_TEXT(CARD_INSERT_A);
	STATUS_TO_TEXT(SIGSTATE_A);
	STATUS_TO_TEXT(CMD_IDX_ERR);
	STATUS_TO_TEXT(STOPBIT_ERR);
	STATUS_TO_TEXT(ILL_FUNC);
	STATUS_TO_TEXT(CMD_BUSY);
	STATUS_TO_TEXT(CMDRESPEND);
	STATUS_TO_TEXT(DATAEND);
	STATUS_TO_TEXT(CRCFAIL);
	STATUS_TO_TEXT(DATATIMEOUT);
	STATUS_TO_TEXT(CMDTIMEOUT);
	STATUS_TO_TEXT(RXOVERFLOW);
	STATUS_TO_TEXT(TXUNDERRUN);
	STATUS_TO_TEXT(RXRDY);
	STATUS_TO_TEXT(TXRQ);
	STATUS_TO_TEXT(ILL_ACCESS);
	printk("\n");
}

#else
#define pr_debug_status(s)  do { } while (0)
#endif

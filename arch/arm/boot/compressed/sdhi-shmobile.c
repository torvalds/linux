/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2010 Kuninori Morimoto
 * Copyright (C) 2010 Simon Horman
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Parts inspired by u-boot
 */

#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/tmio.h>
#include <mach/sdhi.h>

#define OCR_FASTBOOT		(1<<29)
#define OCR_HCS			(1<<30)
#define OCR_BUSY		(1<<31)

#define RESP_CMD12		0x00000030

static inline u16 sd_ctrl_read16(void __iomem *base, int addr)
{
        return __raw_readw(base + addr);
}

static inline u32 sd_ctrl_read32(void __iomem *base, int addr)
{
	return __raw_readw(base + addr) |
	       __raw_readw(base + addr + 2) << 16;
}

static inline void sd_ctrl_write16(void __iomem *base, int addr, u16 val)
{
	__raw_writew(val, base + addr);
}

static inline void sd_ctrl_write32(void __iomem *base, int addr, u32 val)
{
	__raw_writew(val, base + addr);
	__raw_writew(val >> 16, base + addr + 2);
}

#define ALL_ERROR (TMIO_STAT_CMD_IDX_ERR | TMIO_STAT_CRCFAIL |		\
		   TMIO_STAT_STOPBIT_ERR | TMIO_STAT_DATATIMEOUT |	\
		   TMIO_STAT_RXOVERFLOW | TMIO_STAT_TXUNDERRUN |	\
		   TMIO_STAT_CMDTIMEOUT | TMIO_STAT_ILL_ACCESS |	\
		   TMIO_STAT_ILL_FUNC)

static int sdhi_intr(void __iomem *base)
{
	unsigned long state = sd_ctrl_read32(base, CTL_STATUS);

	if (state & ALL_ERROR) {
		sd_ctrl_write32(base, CTL_STATUS, ~ALL_ERROR);
		sd_ctrl_write32(base, CTL_IRQ_MASK,
				ALL_ERROR |
				sd_ctrl_read32(base, CTL_IRQ_MASK));
		return -EINVAL;
	}
	if (state & TMIO_STAT_CMDRESPEND) {
		sd_ctrl_write32(base, CTL_STATUS, ~TMIO_STAT_CMDRESPEND);
		sd_ctrl_write32(base, CTL_IRQ_MASK,
				TMIO_STAT_CMDRESPEND |
				sd_ctrl_read32(base, CTL_IRQ_MASK));
		return 0;
	}
	if (state & TMIO_STAT_RXRDY) {
		sd_ctrl_write32(base, CTL_STATUS, ~TMIO_STAT_RXRDY);
		sd_ctrl_write32(base, CTL_IRQ_MASK,
				TMIO_STAT_RXRDY | TMIO_STAT_TXUNDERRUN |
				sd_ctrl_read32(base, CTL_IRQ_MASK));
		return 0;
	}
	if (state & TMIO_STAT_DATAEND) {
		sd_ctrl_write32(base, CTL_STATUS, ~TMIO_STAT_DATAEND);
		sd_ctrl_write32(base, CTL_IRQ_MASK,
				TMIO_STAT_DATAEND |
				sd_ctrl_read32(base, CTL_IRQ_MASK));
		return 0;
	}

	return -EAGAIN;
}

static int sdhi_boot_wait_resp_end(void __iomem *base)
{
	int err = -EAGAIN, timeout = 10000000;

	while (timeout--) {
		err = sdhi_intr(base);
		if (err != -EAGAIN)
			break;
		udelay(1);
	}

	return err;
}

/* SDHI_CLK_CTRL */
#define CLK_MMC_ENABLE                 (1 << 8)
#define CLK_MMC_INIT                   (1 << 6)        /* clk / 256 */

static void sdhi_boot_mmc_clk_stop(void __iomem *base)
{
	sd_ctrl_write16(base, CTL_CLK_AND_WAIT_CTL, 0x0000);
	msleep(10);
	sd_ctrl_write16(base, CTL_SD_CARD_CLK_CTL, ~CLK_MMC_ENABLE &
		sd_ctrl_read16(base, CTL_SD_CARD_CLK_CTL));
	msleep(10);
}

static void sdhi_boot_mmc_clk_start(void __iomem *base)
{
	sd_ctrl_write16(base, CTL_SD_CARD_CLK_CTL, CLK_MMC_ENABLE |
		sd_ctrl_read16(base, CTL_SD_CARD_CLK_CTL));
	msleep(10);
	sd_ctrl_write16(base, CTL_CLK_AND_WAIT_CTL, CLK_MMC_ENABLE);
	msleep(10);
}

static void sdhi_boot_reset(void __iomem *base)
{
	sd_ctrl_write16(base, CTL_RESET_SD, 0x0000);
	msleep(10);
	sd_ctrl_write16(base, CTL_RESET_SD, 0x0001);
	msleep(10);
}

/* Set MMC clock / power.
 * Note: This controller uses a simple divider scheme therefore it cannot
 * run a MMC card at full speed (20MHz). The max clock is 24MHz on SD, but as
 * MMC wont run that fast, it has to be clocked at 12MHz which is the next
 * slowest setting.
 */
static int sdhi_boot_mmc_set_ios(void __iomem *base, struct mmc_ios *ios)
{
	if (sd_ctrl_read32(base, CTL_STATUS) & TMIO_STAT_CMD_BUSY)
		return -EBUSY;

	if (ios->clock)
		sd_ctrl_write16(base, CTL_SD_CARD_CLK_CTL,
				ios->clock | CLK_MMC_ENABLE);

	/* Power sequence - OFF -> ON -> UP */
	switch (ios->power_mode) {
	case MMC_POWER_OFF: /* power down SD bus */
		sdhi_boot_mmc_clk_stop(base);
		break;
	case MMC_POWER_ON: /* power up SD bus */
		break;
	case MMC_POWER_UP: /* start bus clock */
		sdhi_boot_mmc_clk_start(base);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		sd_ctrl_write16(base, CTL_SD_MEM_CARD_OPT, 0x80e0);
	break;
	case MMC_BUS_WIDTH_4:
		sd_ctrl_write16(base, CTL_SD_MEM_CARD_OPT, 0x00e0);
	break;
	}

	/* Let things settle. delay taken from winCE driver */
	udelay(140);

	return 0;
}

/* These are the bitmasks the tmio chip requires to implement the MMC response
 * types. Note that R1 and R6 are the same in this scheme. */
#define RESP_NONE      0x0300
#define RESP_R1        0x0400
#define RESP_R1B       0x0500
#define RESP_R2        0x0600
#define RESP_R3        0x0700
#define DATA_PRESENT   0x0800
#define TRANSFER_READ  0x1000

static int sdhi_boot_request(void __iomem *base, struct mmc_command *cmd)
{
	int err, c = cmd->opcode;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE: c |= RESP_NONE; break;
	case MMC_RSP_R1:   c |= RESP_R1;   break;
	case MMC_RSP_R1B:  c |= RESP_R1B;  break;
	case MMC_RSP_R2:   c |= RESP_R2;   break;
	case MMC_RSP_R3:   c |= RESP_R3;   break;
	default:
		return -EINVAL;
	}

	/* No interrupts so this may not be cleared */
	sd_ctrl_write32(base, CTL_STATUS, ~TMIO_STAT_CMDRESPEND);

	sd_ctrl_write32(base, CTL_IRQ_MASK, TMIO_STAT_CMDRESPEND |
			sd_ctrl_read32(base, CTL_IRQ_MASK));
	sd_ctrl_write32(base, CTL_ARG_REG, cmd->arg);
	sd_ctrl_write16(base, CTL_SD_CMD, c);


	sd_ctrl_write32(base, CTL_IRQ_MASK,
			~(TMIO_STAT_CMDRESPEND | ALL_ERROR) &
			sd_ctrl_read32(base, CTL_IRQ_MASK));

	err = sdhi_boot_wait_resp_end(base);
	if (err)
		return err;

	cmd->resp[0] = sd_ctrl_read32(base, CTL_RESPONSE);

	return 0;
}

static int sdhi_boot_do_read_single(void __iomem *base, int high_capacity,
				    unsigned long block, unsigned short *buf)
{
	int err, i;

	/* CMD17 - Read */
	{
		struct mmc_command cmd;

		cmd.opcode = MMC_READ_SINGLE_BLOCK | \
			     TRANSFER_READ | DATA_PRESENT;
		if (high_capacity)
			cmd.arg = block;
		else
			cmd.arg = block * TMIO_BBS;
		cmd.flags = MMC_RSP_R1;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
	}

	sd_ctrl_write32(base, CTL_IRQ_MASK,
			~(TMIO_STAT_DATAEND | TMIO_STAT_RXRDY |
			  TMIO_STAT_TXUNDERRUN) &
			sd_ctrl_read32(base, CTL_IRQ_MASK));
	err = sdhi_boot_wait_resp_end(base);
	if (err)
		return err;

	sd_ctrl_write16(base, CTL_SD_XFER_LEN, TMIO_BBS);
	for (i = 0; i < TMIO_BBS / sizeof(*buf); i++)
		*buf++ = sd_ctrl_read16(base, RESP_CMD12);

	err = sdhi_boot_wait_resp_end(base);
	if (err)
		return err;

	return 0;
}

int sdhi_boot_do_read(void __iomem *base, int high_capacity,
		      unsigned long offset, unsigned short count,
		      unsigned short *buf)
{
	unsigned long i;
	int err = 0;

	for (i = 0; i < count; i++) {
		err = sdhi_boot_do_read_single(base, high_capacity, offset + i,
					       buf + (i * TMIO_BBS /
						      sizeof(*buf)));
		if (err)
			return err;
	}

	return 0;
}

#define VOLTAGES (MMC_VDD_32_33 | MMC_VDD_33_34)

int sdhi_boot_init(void __iomem *base)
{
	bool sd_v2 = false, sd_v1_0 = false;
	unsigned short cid;
	int err, high_capacity = 0;

	sdhi_boot_mmc_clk_stop(base);
	sdhi_boot_reset(base);

	/* mmc0: clock 400000Hz busmode 1 powermode 2 cs 0 Vdd 21 width 0 timing 0 */
	{
		struct mmc_ios ios;
		ios.power_mode = MMC_POWER_ON;
		ios.bus_width = MMC_BUS_WIDTH_1;
		ios.clock = CLK_MMC_INIT;
		err = sdhi_boot_mmc_set_ios(base, &ios);
		if (err)
			return err;
	}

	/* CMD0 */
	{
		struct mmc_command cmd;
		msleep(1);
		cmd.opcode = MMC_GO_IDLE_STATE;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
		msleep(2);
	}

	/* CMD8 - Test for SD version 2 */
	{
		struct mmc_command cmd;
		cmd.opcode = SD_SEND_IF_COND;
		cmd.arg = (VOLTAGES != 0) << 8 | 0xaa;
		cmd.flags = MMC_RSP_R1;
		err = sdhi_boot_request(base, &cmd); /* Ignore error */
		if ((cmd.resp[0] & 0xff) == 0xaa)
			sd_v2 = true;
	}

	/* CMD55 - Get OCR (SD) */
	{
		int timeout = 1000;
		struct mmc_command cmd;

		cmd.arg = 0;

		do {
			cmd.opcode = MMC_APP_CMD;
			cmd.flags = MMC_RSP_R1;
			cmd.arg = 0;
			err = sdhi_boot_request(base, &cmd);
			if (err)
				break;

			cmd.opcode = SD_APP_OP_COND;
			cmd.flags = MMC_RSP_R3;
			cmd.arg = (VOLTAGES & 0xff8000);
			if (sd_v2)
				cmd.arg |= OCR_HCS;
			cmd.arg |= OCR_FASTBOOT;
			err = sdhi_boot_request(base, &cmd);
			if (err)
				break;

			msleep(1);
		} while((!(cmd.resp[0] & OCR_BUSY)) && --timeout);

		if (!err && timeout) {
			if (!sd_v2)
				sd_v1_0 = true;
			high_capacity = (cmd.resp[0] & OCR_HCS) == OCR_HCS;
		}
	}

	/* CMD1 - Get OCR (MMC) */
	if (!sd_v2 && !sd_v1_0) {
		int timeout = 1000;
		struct mmc_command cmd;

		do {
			cmd.opcode = MMC_SEND_OP_COND;
			cmd.arg = VOLTAGES | OCR_HCS;
			cmd.flags = MMC_RSP_R3;
			err = sdhi_boot_request(base, &cmd);
			if (err)
				return err;

			msleep(1);
		} while((!(cmd.resp[0] & OCR_BUSY)) && --timeout);

		if (!timeout)
			return -EAGAIN;

		high_capacity = (cmd.resp[0] & OCR_HCS) == OCR_HCS;
	}

	/* CMD2 - Get CID */
	{
		struct mmc_command cmd;
		cmd.opcode = MMC_ALL_SEND_CID;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R2;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
	}

	/* CMD3
	 * MMC: Set the relative address
	 * SD:  Get the relative address
	 * Also puts the card into the standby state
	 */
	{
		struct mmc_command cmd;
		cmd.opcode = MMC_SET_RELATIVE_ADDR;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
		cid = cmd.resp[0] >> 16;
	}

	/* CMD9 - Get CSD */
	{
		struct mmc_command cmd;
		cmd.opcode = MMC_SEND_CSD;
		cmd.arg = cid << 16;
		cmd.flags = MMC_RSP_R2;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
	}

	/* CMD7 - Select the card */
	{
		struct mmc_command cmd;
		cmd.opcode = MMC_SELECT_CARD;
		//cmd.arg = rca << 16;
		cmd.arg = cid << 16;
		//cmd.flags = MMC_RSP_R1B;
		cmd.flags = MMC_RSP_R1;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
	}

	/* CMD16 - Set the block size */
	{
		struct mmc_command cmd;
		cmd.opcode = MMC_SET_BLOCKLEN;
		cmd.arg = TMIO_BBS;
		cmd.flags = MMC_RSP_R1;
		err = sdhi_boot_request(base, &cmd);
		if (err)
			return err;
	}

	return high_capacity;
}

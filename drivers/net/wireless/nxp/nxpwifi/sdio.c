// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: SDIO specific handling
 *
 * Copyright 2011-2024 NXP
 */

#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/iopoll.h>
#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "sdio.h"

#define SDIO_VERSION	"1.0"

/* Process deferred SDIO work items. */
static void nxpwifi_sdio_work(struct work_struct *work);

static struct nxpwifi_if_ops sdio_ops;

static const struct nxpwifi_sdio_card_reg nxpwifi_reg_iw61x = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xF8,
	.base_1_reg = 0xF9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			   CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0C,
	.host_int_mask_reg = 0x08,
	.host_strap_reg = 0xF4,
	.host_strap_mask = 0x01,
	.host_strap_value = 0x00,
	.status_reg_0 = 0xE8,
	.status_reg_1 = 0xE9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.fw_dump_host_ready = 0xcc,
	.fw_dump_ctrl = 0xf9,
	.fw_dump_start = 0xf1,
	.fw_dump_end = 0xf8,
	.func1_dump_reg_start = 0x10,
	.func1_dump_reg_end = 0x17,
	.func1_scratch_reg = 0xE8,
	.func1_spec_reg_num = 13,
	.func1_spec_reg_table = {0x08, 0x58, 0x5C, 0x5D, 0x60,
				 0x61, 0x62, 0x64, 0x65, 0x66,
				 0x68, 0x69, 0x6a},
};

static const struct nxpwifi_sdio_device nxpwifi_sdio_iw61x = {
	.firmware = IW61X_SDIO_FW_NAME,
	.reg = &nxpwifi_reg_iw61x,
	.max_ports = 32,
	.mp_agg_pkt_limit = 16,
	.tx_buf_size = NXPWIFI_TX_DATA_BUF_SIZE_4K,
	.mp_tx_agg_buf_size = NXPWIFI_MP_AGGR_BSIZE_MAX,
	.mp_rx_agg_buf_size = NXPWIFI_MP_AGGR_BSIZE_MAX,
	.can_dump_fw = true,
	.fw_dump_enh = true,
	.can_ext_scan = true,
};

static struct memory_type_mapping generic_mem_type_map[] = {
	{"DUMP", NULL, 0, 0xDD},
};

static struct memory_type_mapping mem_type_mapping_tbl[] = {
	{"ITCM", NULL, 0, 0xF0},
	{"DTCM", NULL, 0, 0xF1},
	{"SQRAM", NULL, 0, 0xF2},
	{"APU", NULL, 0, 0xF3},
	{"CIU", NULL, 0, 0xF4},
	{"ICU", NULL, 0, 0xF5},
	{"MAC", NULL, 0, 0xF6},
	{"EXT7", NULL, 0, 0xF7},
	{"EXT8", NULL, 0, 0xF8},
	{"EXT9", NULL, 0, 0xF9},
	{"EXT10", NULL, 0, 0xFA},
	{"EXT11", NULL, 0, 0xFB},
	{"EXT12", NULL, 0, 0xFC},
	{"EXT13", NULL, 0, 0xFD},
	{"EXTLAST", NULL, 0, 0xFE},
};

/* Bind the SDIO function and start device registration. */
static int
nxpwifi_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret;
	struct sdio_mmc_card *card = NULL;

	card = devm_kzalloc(&func->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	init_completion(&card->fw_done);

	card->func = func;

	if (id->driver_data) {
		struct nxpwifi_sdio_device *data = (void *)id->driver_data;

		card->firmware = data->firmware;
		card->firmware_sdiouart = data->firmware_sdiouart;
		card->reg = data->reg;
		card->max_ports = data->max_ports;
		card->mp_agg_pkt_limit = data->mp_agg_pkt_limit;
		card->tx_buf_size = data->tx_buf_size;
		card->mp_tx_agg_buf_size = data->mp_tx_agg_buf_size;
		card->mp_rx_agg_buf_size = data->mp_rx_agg_buf_size;
		card->can_dump_fw = data->can_dump_fw;
		card->fw_dump_enh = data->fw_dump_enh;
		card->can_ext_scan = data->can_ext_scan;
		INIT_WORK(&card->work, nxpwifi_sdio_work);
	}

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	sdio_release_host(func);

	if (ret) {
		dev_err(&func->dev, "failed to enable function\n");
		return ret;
	}

	ret = nxpwifi_add_card(card, &card->fw_done, &sdio_ops,
			       NXPWIFI_SDIO, &func->dev);
	if (ret) {
		dev_err(&func->dev, "add card failed\n");
		goto err_disable;
	}

	return 0;

err_disable:
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	return ret;
}

/* Resume the SDIO function and cancel host sleep. */
static int nxpwifi_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_mmc_card *card;
	struct nxpwifi_adapter *adapter;

	card = sdio_get_drvdata(func);

	if (unlikely(!card || !card->adapter)) {
		dev_dbg(dev, "resume: %s not ready\n", !card ? "card" : "adapter");
		return -ENODEV;
	}

	adapter = card->adapter;

	if (!test_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags))
		return 0;

	clear_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags);

	/* Disable Host Sleep */
	nxpwifi_cancel_hs(nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_STA),
			  NXPWIFI_SYNC_CMD);

	return 0;
}

static int
nxpwifi_write_reg_locked(struct sdio_func *func, u32 reg, u8 data)
{
	int ret;

	sdio_writeb(func, data, reg, &ret);
	return ret;
}

static int
nxpwifi_write_reg(struct nxpwifi_adapter *adapter, u32 reg, u8 data)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;

	sdio_claim_host(card->func);
	ret = nxpwifi_write_reg_locked(card->func, reg, data);
	sdio_release_host(card->func);

	return ret;
}

static int
nxpwifi_read_reg(struct nxpwifi_adapter *adapter, u32 reg, u8 *data)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u8 val;

	sdio_claim_host(card->func);
	val = sdio_readb(card->func, reg, &ret);
	sdio_release_host(card->func);

	*data = val;

	return ret;
}

static int
nxpwifi_write_data_sync(struct nxpwifi_adapter *adapter,
			u8 *buffer, u32 pkt_len, u32 port)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u8 blk_mode =
		(port & NXPWIFI_SDIO_BYTE_MODE_MASK) ? BYTE_MODE : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? NXPWIFI_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt =
		(blk_mode ==
		 BLOCK_MODE) ? (pkt_len /
				NXPWIFI_SDIO_BLOCK_SIZE) : pkt_len;
	u32 ioport = (port & NXPWIFI_SDIO_IO_PORT_MASK);

	if (test_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: not allowed while suspended\n", __func__);
		return -EPERM;
	}

	sdio_claim_host(card->func);

	ret = sdio_writesb(card->func, ioport, buffer, blk_cnt * blk_size);

	sdio_release_host(card->func);

	return ret;
}

static int nxpwifi_read_data_sync(struct nxpwifi_adapter *adapter, u8 *buffer,
				  u32 len, u32 port, u8 claim)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u8 blk_mode = (port & NXPWIFI_SDIO_BYTE_MODE_MASK) ? BYTE_MODE
		       : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? NXPWIFI_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt = (blk_mode == BLOCK_MODE) ? (len / NXPWIFI_SDIO_BLOCK_SIZE)
			: len;
	u32 ioport = (port & NXPWIFI_SDIO_IO_PORT_MASK);

	if (claim)
		sdio_claim_host(card->func);

	ret = sdio_readsb(card->func, buffer, ioport, blk_cnt * blk_size);

	if (claim)
		sdio_release_host(card->func);

	return ret;
}

static int
nxpwifi_sdio_read_fw_status(struct nxpwifi_adapter *adapter, u16 *dat)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	u8 fws0, fws1;
	int ret;

	ret = nxpwifi_read_reg(adapter, reg->status_reg_0, &fws0);
	if (ret)
		return ret;

	ret = nxpwifi_read_reg(adapter, reg->status_reg_1, &fws1);
	if (ret)
		return ret;

	*dat = (u16)((fws1 << 8) | fws0);
	return ret;
}

static int nxpwifi_check_fw_status(struct nxpwifi_adapter *adapter,
				   u32 poll_num)
{
	int ret = 0;
	u16 firmware_stat = 0;

	unsigned int timeout_us = poll_num * 100000; /* 100 ms * poll_num */
	/*
	 * Poll every 100 ms until firmware reports FIRMWARE_READY_SDIO.
	 * On timeout, read_poll_timeout() returns -ETIMEDOUT.
	 */
	ret = read_poll_timeout(nxpwifi_sdio_read_fw_status, ret,
				(!ret && firmware_stat == FIRMWARE_READY_SDIO),
				100000, timeout_us, true, /* sleep */
				adapter, &firmware_stat);

	/* FW may appear ready; wait a bit to avoid early races. */
	if (firmware_stat == FIRMWARE_READY_SDIO)
		msleep(100);

	return ret;
}

static int nxpwifi_check_winner_status(struct nxpwifi_adapter *adapter)
{
	int ret;
	u8 winner = 0;
	struct sdio_mmc_card *card = adapter->card;

	ret = nxpwifi_read_reg(adapter, card->reg->status_reg_0, &winner);
	if (ret)
		return ret;

	if (winner)
		adapter->winner = 0;
	else
		adapter->winner = 1;

	return ret;
}

/* Remove the SDIO function and tear down the adapter. */
static void
nxpwifi_sdio_remove(struct sdio_func *func)
{
	struct sdio_mmc_card *card;
	struct nxpwifi_adapter *adapter;
	struct nxpwifi_private *priv;
	int ret = 0;
	u16 firmware_stat;

	card = sdio_get_drvdata(func);
	if (!card)
		return;

	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	ret = nxpwifi_sdio_read_fw_status(adapter, &firmware_stat);
	if (!ret && firmware_stat == FIRMWARE_READY_SDIO) {
		nxpwifi_deauthenticate_all(adapter);

		priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
		nxpwifi_disable_auto_ds(priv);
		nxpwifi_init_shutdown_fw(priv, NXPWIFI_FUNC_SHUTDOWN);
	}

	nxpwifi_remove_card(adapter);
}

/* Suspend the SDIO function while keeping SDIO power. */
static int nxpwifi_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_mmc_card *card;
	struct nxpwifi_adapter *adapter;
	mmc_pm_flag_t caps;
	unsigned long flags = 0;
	int ret = 0;

	caps = sdio_get_host_pm_caps(func);

	if (!(caps & MMC_PM_KEEP_POWER)) {
		/* host lacks keep-power capability */
		dev_warn(dev, "suspend: host does not support MMC_PM_KEEP_POWER\n");
		return -EOPNOTSUPP;
	}

	card = sdio_get_drvdata(func);

	if (!card) {
		dev_warn(dev, "suspend: card not ready\n");
		return -ENODEV;
	}

	/* Might still be loading firmware */
	wait_for_completion(&card->fw_done);

	adapter = card->adapter;
	if (!adapter) {
		dev_warn(dev, "suspend: adapter not ready\n");
		return -ENODEV;
	}

	/* Enable the Host Sleep */
	if (!nxpwifi_enable_hs(adapter)) {
		nxpwifi_dbg(adapter, ERROR, "suspend: enable host sleep failed\n");
		clear_bit(NXPWIFI_IS_HS_ENABLING, &adapter->work_flags);
		return -ETIMEDOUT;
	}

	flags |= MMC_PM_KEEP_POWER;

	if (adapter->wowlan_enabled && (caps & MMC_PM_WAKE_SDIO_IRQ))
		flags |= MMC_PM_WAKE_SDIO_IRQ;

	ret = sdio_set_host_pm_flags(func, flags);

	/* Indicate device suspended */
	set_bit(NXPWIFI_IS_SUSPENDED, &adapter->work_flags);
	clear_bit(NXPWIFI_IS_HS_ENABLING, &adapter->work_flags);

	return ret;
}

static void nxpwifi_sdio_coredump(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_mmc_card *card;

	card = sdio_get_drvdata(func);
	if (!test_and_set_bit(NXPWIFI_IFACE_WORK_DEVICE_DUMP,
			      &card->work_flags))
		nxpwifi_queue_work(card->adapter, &card->work);
}

/* WLAN IDs */
static const struct sdio_device_id nxpwifi_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_NXP, SDIO_DEVICE_ID_NXP_IW61X),
		.driver_data = (unsigned long)&nxpwifi_sdio_iw61x},
	{},
};

MODULE_DEVICE_TABLE(sdio, nxpwifi_ids);

static const struct dev_pm_ops nxpwifi_sdio_pm_ops = {
	.suspend = nxpwifi_sdio_suspend,
	.resume = nxpwifi_sdio_resume,
};

static struct sdio_driver nxpwifi_sdio = {
	.name = "nxpwifi_sdio",
	.id_table = nxpwifi_ids,
	.probe = nxpwifi_sdio_probe,
	.remove = nxpwifi_sdio_remove,
	.drv = {
		.coredump = nxpwifi_sdio_coredump,
		.pm = &nxpwifi_sdio_pm_ops,
	}
};

static int nxpwifi_pm_wakeup_card(struct nxpwifi_adapter *adapter)
{
	nxpwifi_dbg(adapter, EVENT, "event: wakeup device...\n");

	return nxpwifi_write_reg(adapter, CONFIGURATION_REG, HOST_POWER_UP);
}

static int nxpwifi_pm_wakeup_card_complete(struct nxpwifi_adapter *adapter)
{
	nxpwifi_dbg(adapter, EVENT, "cmd: wakeup device completed\n");

	return nxpwifi_write_reg(adapter, CONFIGURATION_REG, 0);
}

/* SDIO wrapper for firmware download (claims host). */
static int nxpwifi_sdio_dnld_fw(struct nxpwifi_adapter *adapter,
				struct nxpwifi_fw_image *fw)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;

	sdio_claim_host(card->func);
	ret = nxpwifi_dnld_fw(adapter, fw);
	sdio_release_host(card->func);

	return ret;
}

static int nxpwifi_init_sdio_new_mode(struct nxpwifi_adapter *adapter)
{
	u8 reg;
	struct sdio_mmc_card *card = adapter->card;
	int ret;

	adapter->ioport = MEM_PORT;

	/* enable sdio new mode */
	ret = nxpwifi_read_reg(adapter, card->reg->card_cfg_2_1_reg, &reg);
	if (ret)
		return ret;
	ret = nxpwifi_write_reg(adapter, card->reg->card_cfg_2_1_reg,
				reg | CMD53_NEW_MODE);
	if (ret)
		return ret;

	/* Configure cmd port and enable reading rx length from the register */
	ret = nxpwifi_read_reg(adapter, card->reg->cmd_cfg_0, &reg);
	if (ret)
		return ret;
	ret = nxpwifi_write_reg(adapter, card->reg->cmd_cfg_0,
				reg | CMD_PORT_RD_LEN_EN);
	if (ret)
		return ret;

	/*
	 * Enable Dnld/Upld ready auto reset for cmd port after cmd53 is
	 * completed
	 */
	ret = nxpwifi_read_reg(adapter, card->reg->cmd_cfg_1, &reg);
	if (ret)
		return ret;
	ret = nxpwifi_write_reg(adapter, card->reg->cmd_cfg_1,
				reg | CMD_PORT_AUTO_EN);

	return ret;
}

/* Initialize SDIO IO ports and host-int behavior. */
static int nxpwifi_init_sdio_ioport(struct nxpwifi_adapter *adapter)
{
	u8 reg;
	struct sdio_mmc_card *card = adapter->card;
	int ret;

	ret = nxpwifi_init_sdio_new_mode(adapter);
	if (ret)
		return ret;

	/* Set Host interrupt reset to read to clear */
	ret = nxpwifi_read_reg(adapter, card->reg->host_int_rsr_reg, &reg);
	if (ret)
		return ret;
	ret = nxpwifi_write_reg(adapter, card->reg->host_int_rsr_reg,
				reg | card->reg->sdio_int_mask);
	if (ret)
		return ret;

	/* Dnld/Upld ready set to auto reset */
	ret = nxpwifi_read_reg(adapter, card->reg->card_misc_cfg_reg, &reg);
	if (ret)
		return ret;
	ret = nxpwifi_write_reg(adapter, card->reg->card_misc_cfg_reg,
				reg | AUTO_RE_ENABLE_INT);

	return ret;
}

static int nxpwifi_write_data_to_card(struct nxpwifi_adapter *adapter,
				      u8 *payload, u32 pkt_len, u32 port)
{
	u32 i = 0;
	int ret;

	do {
		ret = nxpwifi_write_data_sync(adapter, payload, pkt_len, port);
		if (ret) {
			i++;
			nxpwifi_dbg(adapter, ERROR, "host_to_card, write iomem\t"
				    "(%d) failed: %d\n", i, ret);
			if (nxpwifi_write_reg(adapter, CONFIGURATION_REG, 0x04))
				nxpwifi_dbg(adapter, ERROR, "write CFG reg failed\n");

			if (i > MAX_WRITE_IOMEM_RETRY)
				return ret;
		}
	} while (ret);

	return ret;
}

static int nxpwifi_get_rd_port(struct nxpwifi_adapter *adapter, u8 *port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	u32 rd_bitmap = card->mp_rd_bitmap;

	if (!(rd_bitmap & reg->data_port_mask))
		return -EINVAL;

	if (!(card->mp_rd_bitmap & (1 << card->curr_rd_port)))
		return -EINVAL;

	/* We are now handling the SDIO data ports */
	card->mp_rd_bitmap &= (u32)(~(1 << card->curr_rd_port));
	*port = card->curr_rd_port;

	if (++card->curr_rd_port == card->max_ports)
		card->curr_rd_port = reg->start_rd_port;

	return 0;
}

static int nxpwifi_get_wr_port_data(struct nxpwifi_adapter *adapter, u32 *port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	u32 wr_bitmap = card->mp_wr_bitmap;

	if (!(wr_bitmap & card->mp_data_port_mask)) {
		adapter->data_sent = true;
		return -EBUSY;
	}

	if (card->mp_wr_bitmap & (1 << card->curr_wr_port)) {
		card->mp_wr_bitmap &= (u32)(~(1 << card->curr_wr_port));
		*port = card->curr_wr_port;
		if (++card->curr_wr_port == card->mp_end_port)
			card->curr_wr_port = reg->start_wr_port;
	} else {
		adapter->data_sent = true;
		return -EBUSY;
	}

	return 0;
}

static int
nxpwifi_sdio_poll_card_status(struct nxpwifi_adapter *adapter, u8 bits)
{
	struct sdio_mmc_card *card = adapter->card;
	u32 tries;
	u8 cs;
	int ret;

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		ret = nxpwifi_read_reg(adapter, card->reg->poll_reg, &cs);
		if (ret)
			break;
		else if ((cs & bits) == bits)
			return 0;

		usleep_range(10, 20);
	}

	nxpwifi_dbg(adapter, ERROR, "poll card status failed, tries = %d\n", tries);

	return ret;
}

/* Disable SDIO host interrupt and release IRQ. */
static void nxpwifi_sdio_disable_host_int(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;

	sdio_claim_host(func);
	nxpwifi_write_reg_locked(func, card->reg->host_int_mask_reg, 0);
	sdio_release_irq(func);
	sdio_release_host(func);
}

static void nxpwifi_interrupt_status(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	u8 sdio_ireg;
	unsigned long flags;

	if (nxpwifi_read_data_sync(adapter, card->mp_regs,
				   card->reg->max_mp_regs,
				   REG_PORT | NXPWIFI_SDIO_BYTE_MODE_MASK, 0)) {
		nxpwifi_dbg(adapter, ERROR, "read mp_regs failed\n");
		return;
	}

	sdio_ireg = card->mp_regs[card->reg->host_int_status_reg];
	if (sdio_ireg) {
		nxpwifi_dbg(adapter, INTR, "intr: sdio_ireg = %#x\n", sdio_ireg);
		spin_lock_irqsave(&adapter->int_lock, flags);
		adapter->int_status |= sdio_ireg;
		spin_unlock_irqrestore(&adapter->int_lock, flags);
	}
}

/* SDIO IRQ handler: snapshot status and schedule main work. */
static void
nxpwifi_sdio_interrupt(struct sdio_func *func)
{
	struct nxpwifi_adapter *adapter;
	struct sdio_mmc_card *card;

	card = sdio_get_drvdata(func);

	if (!card || !card->adapter) {
		/* device-scoped error logging (rate-limited to avoid flood) */
		dev_err_ratelimited(&func->dev, "interrupt: missing card/adapter\n");
		return;
	}

	adapter = card->adapter;

	if (!adapter->pps_uapsd_mode && adapter->ps_state == PS_STATE_SLEEP)
		adapter->ps_state = PS_STATE_AWAKE;

	nxpwifi_interrupt_status(adapter);
	nxpwifi_queue_work(adapter, &adapter->main_work);
}

/* Enable SDIO host interrupt and claim IRQ. */
static int nxpwifi_sdio_enable_host_int(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;
	int ret;

	sdio_claim_host(func);

	/* Request the SDIO IRQ */
	ret = sdio_claim_irq(func, nxpwifi_sdio_interrupt);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "claim irq failed: ret=%d\n", ret);
		goto done;
	}

	/* Simply write the mask to the register */
	ret = nxpwifi_write_reg_locked(func, card->reg->host_int_mask_reg,
				       card->reg->host_int_enable);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "enable host interrupt failed\n");
		sdio_release_irq(func);
	}

done:
	sdio_release_host(func);
	return ret;
}

static int nxpwifi_sdio_card_to_host(struct nxpwifi_adapter *adapter,
				     u32 *type, u8 *buffer,
				     u32 npayload, u32 ioport)
{
	int ret;
	u32 nb;

	if (!buffer)
		return -EINVAL;

	ret = nxpwifi_read_data_sync(adapter, buffer, npayload, ioport, 1);

	if (ret) {
		nxpwifi_dbg(adapter, ERROR,
			    "read iomem failed (ioport=%#x, len=%u): %d",
			    ioport, npayload, ret);

		return ret;
	}

	nb = get_unaligned_le16((buffer));
	if (nb > npayload) {
		nxpwifi_dbg(adapter, ERROR,
			    "invalid packet len: nb=%u > npayload=%u (ioport=%#x)",
			    nb, npayload, ioport);
		return -EINVAL;
	}

	*type = get_unaligned_le16((buffer + 2));

	return ret;
}

/* Download firmware using the helper protocol. */
static int nxpwifi_prog_fw_w_helper(struct nxpwifi_adapter *adapter,
				    struct nxpwifi_fw_image *fw)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	int ret;
	u8 *firmware = fw->fw_buf;
	u32 firmware_len = fw->fw_len;
	u32 offset = 0;
	u8 base0, base1;
	u8 *fwbuf;
	u16 len = 0;
	u32 txlen, tx_blocks = 0, tries;
	u32 i = 0;

	if (!firmware_len) {
		nxpwifi_dbg(adapter, ERROR,
			    "firmware image not found! Terminating download\n");
		return -EINVAL;
	}

	/* Assume that the allocated buffer is 8-byte aligned */
	fwbuf = kzalloc(NXPWIFI_UPLD_SIZE, GFP_KERNEL);
	if (!fwbuf)
		return -ENOMEM;

	sdio_claim_host(card->func);

	/* Perform firmware data transfer */
	do {
		/*
		 * The host polls for the DN_LD_CARD_RDY and CARD_IO_READY
		 * bits
		 */
		ret = nxpwifi_sdio_poll_card_status(adapter, CARD_IO_READY |
						    DN_LD_CARD_RDY);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "FW download with helper:\t"
				    "poll status timeout @ %d\n", offset);
			goto done;
		}

		/* More data? */
		if (offset >= firmware_len)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = nxpwifi_read_reg(adapter, reg->base_0_reg,
					       &base0);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR,
					    "dev BASE0 register read failed:\t"
					    "base0=%#04X(%d). Terminating dnld\n",
					    base0, base0);
				goto done;
			}
			ret = nxpwifi_read_reg(adapter, reg->base_1_reg,
					       &base1);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR,
					    "dev BASE1 register read failed:\t"
					    "base1=%#04X(%d). Terminating dnld\n",
					    base1, base1);
				goto done;
			}
			len = (u16)(((base1 & 0xff) << 8) | (base0 & 0xff));

			if (len)
				break;

			usleep_range(10, 20);
		}

		if (!len) {
			break;
		} else if (len > NXPWIFI_UPLD_SIZE) {
			nxpwifi_dbg(adapter, ERROR,
				    "FW dnld failed @ %d, invalid length %d\n",
				    offset, len);
			ret = -EINVAL;
			goto done;
		}

		txlen = len;

		if (len & BIT(0)) {
			i++;
			if (i > MAX_WRITE_IOMEM_RETRY) {
				nxpwifi_dbg(adapter, ERROR,
					    "FW dnld failed @ %d, over max retry\n",
					    offset);
				ret = -EIO;
				goto done;
			}
			nxpwifi_dbg(adapter, ERROR,
				    "CRC indicated by the helper:\t"
				    "len = 0x%04X, txlen = %d\n", len, txlen);
			len &= ~BIT(0);
			/* Setting this to 0 to resend from same offset */
			txlen = 0;
		} else {
			i = 0;

			/*
			 * Set blocksize to transfer - checking for last
			 * block
			 */
			if (firmware_len - offset < txlen)
				txlen = firmware_len - offset;

			tx_blocks = (txlen + NXPWIFI_SDIO_BLOCK_SIZE - 1)
				    / NXPWIFI_SDIO_BLOCK_SIZE;

			/* Copy payload to buffer */
			memcpy(fwbuf, &firmware[offset], txlen);
		}

		ret = nxpwifi_write_data_sync(adapter, fwbuf, tx_blocks *
					      NXPWIFI_SDIO_BLOCK_SIZE,
					      adapter->ioport);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "FW download, write iomem (%d) failed @ %d\n",
				    i, offset);
			if (nxpwifi_write_reg(adapter, CONFIGURATION_REG, 0x04))
				nxpwifi_dbg(adapter, ERROR, "write CFG reg failed\n");

			goto done;
		}

		offset += txlen;
	} while (true);

	nxpwifi_dbg(adapter, MSG, "FW download complete (%u bytes)\n", offset);

	ret = 0;
done:
	sdio_release_host(card->func);
	kfree(fwbuf);
	return ret;
}

/* Deaggregate an SDIO RX aggregation packet. */
static void nxpwifi_deaggr_sdio_pkt(struct nxpwifi_adapter *adapter,
				    struct sk_buff *skb)
{
	u32 total_pkt_len, pkt_len;
	struct sk_buff *skb_deaggr;
	u16 blk_size;
	u8 blk_num;
	u8 *data;

	data = skb->data;
	total_pkt_len = skb->len;

	while (total_pkt_len >= (SDIO_HEADER_OFFSET + adapter->intf_hdr_len)) {
		if (total_pkt_len < adapter->sdio_rx_block_size)
			break;
		blk_num = *(data + BLOCK_NUMBER_OFFSET);
		blk_size = adapter->sdio_rx_block_size * blk_num;
		if (blk_size > total_pkt_len) {
			nxpwifi_dbg(adapter, ERROR,
				    "%s: error in blk_size,\t"
				    "blk_num=%d, blk_size=%d, total_pkt_len=%d\n",
				    __func__, blk_num, blk_size, total_pkt_len);
			break;
		}
		pkt_len = get_unaligned_le16((data +
					     SDIO_HEADER_OFFSET));
		if ((pkt_len + SDIO_HEADER_OFFSET) > blk_size) {
			nxpwifi_dbg(adapter, ERROR,
				    "%s: error in pkt_len,\t"
				    "pkt_len=%d, blk_size=%d\n",
				    __func__, pkt_len, blk_size);
			break;
		}

		skb_deaggr = nxpwifi_alloc_dma_align_buf(pkt_len, GFP_KERNEL);
		if (!skb_deaggr)
			break;
		skb_put(skb_deaggr, pkt_len);
		memcpy(skb_deaggr->data, data + SDIO_HEADER_OFFSET, pkt_len);
		skb_pull(skb_deaggr, adapter->intf_hdr_len);

		nxpwifi_handle_rx_packet(adapter, skb_deaggr);
		data += blk_size;
		total_pkt_len -= blk_size;
	}
}

static void nxpwifi_decode_rx_packet(struct nxpwifi_adapter *adapter,
				     struct sk_buff *skb, u32 upld_typ)
{
	u8 *cmd_buf;
	u16 pkt_len;
	struct nxpwifi_rxinfo *rx_info;

	pkt_len = get_unaligned_le16(skb->data);

	if (upld_typ != NXPWIFI_TYPE_AGGR_DATA) {
		skb_trim(skb, pkt_len);
		skb_pull(skb, adapter->intf_hdr_len);
	}

	switch (upld_typ) {
	case NXPWIFI_TYPE_AGGR_DATA:
		nxpwifi_dbg(adapter, DATA,
			    "Rx Aggr Data packet\n");
		rx_info = NXPWIFI_SKB_RXCB(skb);
		rx_info->buf_type = NXPWIFI_TYPE_AGGR_DATA;
		if (adapter->rx_work_enabled) {
			skb_queue_tail(&adapter->rx_data_q, skb);
			atomic_inc(&adapter->rx_pending);
			adapter->data_received = true;
		} else {
			/* Deaggregate an SDIO RX aggregation packet. */
			nxpwifi_deaggr_sdio_pkt(adapter, skb);
			dev_kfree_skb_any(skb);
		}
		break;

	case NXPWIFI_TYPE_DATA:
		nxpwifi_dbg(adapter, DATA, "Rx Data packet\n");
		if (adapter->rx_work_enabled) {
			skb_queue_tail(&adapter->rx_data_q, skb);
			adapter->data_received = true;
			atomic_inc(&adapter->rx_pending);
		} else {
			nxpwifi_handle_rx_packet(adapter, skb);
		}
		break;

	case NXPWIFI_TYPE_CMD:
		nxpwifi_dbg(adapter, CMD, "Rx Cmd Response\n");
		/* take care of curr_cmd = NULL case */
		if (!adapter->curr_cmd) {
			cmd_buf = adapter->upld_buf;

			if (adapter->ps_state == PS_STATE_SLEEP_CFM)
				nxpwifi_process_sleep_confirm_resp(adapter,
								   skb->data,
								   skb->len);

			memcpy(cmd_buf, skb->data,
			       min_t(u32, NXPWIFI_SIZE_OF_CMD_BUFFER,
				     skb->len));

			dev_kfree_skb_any(skb);
		} else {
			adapter->cmd_resp_received = true;
			adapter->curr_cmd->resp_skb = skb;
		}
		break;

	case NXPWIFI_TYPE_EVENT:
		nxpwifi_dbg(adapter, EVENT, "Rx Event\n");
		adapter->event_cause = get_unaligned_le32(skb->data);

		if (skb->len > NXPWIFI_EVENT_HEADER_LEN) {
			u32 body_len = min_t(u32, skb->len - NXPWIFI_EVENT_HEADER_LEN,
					     MAX_EVENT_SIZE);
			memcpy(adapter->event_body, skb->data + NXPWIFI_EVENT_HEADER_LEN,
			       body_len);
		}

		/* event cause has been saved to adapter->event_cause */
		adapter->event_received = true;
		adapter->event_skb = skb;

		break;

	default:
		nxpwifi_dbg(adapter, ERROR, "unknown upload type %#x\n", upld_typ);
		dev_kfree_skb_any(skb);
		break;
	}
}

/* Receive path with SDIO multi-port aggregation. */
static int nxpwifi_sdio_card_to_host_mp_aggr(struct nxpwifi_adapter *adapter,
					     u16 rx_len, u8 port)
{
	struct sdio_mmc_card *card = adapter->card;
	s32 f_do_rx_aggr = 0;
	s32 f_do_rx_cur = 0;
	s32 f_aggr_cur = 0;
	s32 f_post_aggr_cur = 0;
	struct sk_buff *skb_deaggr;
	struct sk_buff *skb = NULL;
	u32 pkt_len, pkt_type, mport, pind;
	u8 *curr_ptr;
	int ret = 0;

	if (!card->mpa_rx.enabled) {
		nxpwifi_dbg(adapter, WARN, "rx aggregation disabled\n");
		f_do_rx_cur = 1;
		goto rx_curr_single;
	}

	if (card->mp_rd_bitmap & card->reg->data_port_mask) {
		/* Some more data RX pending */

		if (MP_RX_AGGR_IN_PROGRESS(card)) {
			if (MP_RX_AGGR_BUF_HAS_ROOM(card, rx_len)) {
				f_aggr_cur = 1;
			} else {
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_aggr = 1;
				f_post_aggr_cur = 1;
			}
		} else {
			/* Rx aggr not in progress */
			f_aggr_cur = 1;
		}

	} else {
		/* No more data RX pending */

		if (MP_RX_AGGR_IN_PROGRESS(card)) {
			f_do_rx_aggr = 1;
			if (MP_RX_AGGR_BUF_HAS_ROOM(card, rx_len))
				f_aggr_cur = 1;
			else
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_cur = 1;
		} else {
			f_do_rx_cur = 1;
		}
	}

	if (f_aggr_cur) {
		/* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);

		if (MP_RX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_rx_aggr_port_limit_reached(card)) {
			/* No more pkts allowed in Aggr buf, rx it */
			f_do_rx_aggr = 1;
		}
	}

	if (f_do_rx_aggr) {
		u32 port_count;
		int i;

		/* do aggr RX now */
		for (i = 0, port_count = 0; i < card->max_ports; i++)
			if (card->mpa_rx.ports & BIT(i))
				port_count++;

		/*
		 * Reading data from "start_port + 0" to "start_port +
		 * port_count -1", so decrease the count by 1
		 */
		port_count--;
		mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
			 (port_count << 8)) + card->mpa_rx.start_port;

		if (card->mpa_rx.pkt_cnt == 1)
			mport = adapter->ioport + card->mpa_rx.start_port;

		ret = nxpwifi_read_data_sync(adapter, card->mpa_rx.buf,
					     card->mpa_rx.buf_len, mport, 1);
		if (ret)
			goto error;

		curr_ptr = card->mpa_rx.buf;

		for (pind = 0; pind < card->mpa_rx.pkt_cnt; pind++) {
			u32 *len_arr = card->mpa_rx.len_arr;

			/* get curr PKT len & type */
			pkt_len = get_unaligned_le16(&curr_ptr[0]);
			pkt_type = get_unaligned_le16(&curr_ptr[2]);

			/* copy pkt to deaggr buf */
			skb_deaggr = nxpwifi_alloc_dma_align_buf(len_arr[pind],
								 GFP_KERNEL);
			if (!skb_deaggr) {
				nxpwifi_dbg(adapter, ERROR, "skb allocation failure\t"
					    "drop pkt len=%d type=%d\n",
					    pkt_len, pkt_type);
				curr_ptr += len_arr[pind];
				continue;
			}

			skb_put(skb_deaggr, len_arr[pind]);

			if ((pkt_type == NXPWIFI_TYPE_DATA ||
			     (pkt_type == NXPWIFI_TYPE_AGGR_DATA &&
			      adapter->sdio_rx_aggr_enable)) &&
			    pkt_len <= len_arr[pind]) {
				memcpy(skb_deaggr->data, curr_ptr, pkt_len);

				skb_trim(skb_deaggr, pkt_len);

				nxpwifi_decode_rx_packet(adapter, skb_deaggr,
							 pkt_type);
			} else {
				nxpwifi_dbg(adapter, ERROR,
					    "drop wrong aggr pkt:\t"
					    "sdio_single_port_rx_aggr=%d\t"
					    "type=%d len=%d max_len=%d\n",
					    adapter->sdio_rx_aggr_enable,
					    pkt_type, pkt_len, len_arr[pind]);
				dev_kfree_skb_any(skb_deaggr);
			}
			curr_ptr += len_arr[pind];
		}
		MP_RX_AGGR_BUF_RESET(card);
	}

rx_curr_single:
	if (f_do_rx_cur) {
		skb = nxpwifi_alloc_dma_align_buf(rx_len, GFP_KERNEL);
		if (!skb) {
			nxpwifi_dbg(adapter, ERROR,
				    "single skb allocated fail,\t"
				    "drop pkt port=%d len=%d\n", port, rx_len);
			ret = nxpwifi_sdio_card_to_host(adapter, &pkt_type,
							card->mpa_rx.buf,
							rx_len,
							adapter->ioport + port);
			if (ret)
				goto error;
			return 0;
		}

		skb_put(skb, rx_len);

		ret = nxpwifi_sdio_card_to_host(adapter, &pkt_type,
						skb->data, skb->len,
						adapter->ioport + port);
		if (ret)
			goto error;
		if (!adapter->sdio_rx_aggr_enable &&
		    pkt_type == NXPWIFI_TYPE_AGGR_DATA) {
			nxpwifi_dbg(adapter, ERROR, "drop wrong pkt type %d\t"
				    "current SDIO RX Aggr not enabled\n",
				    pkt_type);
			dev_kfree_skb_any(skb);
			return 0;
		}

		nxpwifi_decode_rx_packet(adapter, skb, pkt_type);
	}
	if (f_post_aggr_cur) /* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);

	return 0;
error:
	if (MP_RX_AGGR_IN_PROGRESS(card))
		MP_RX_AGGR_BUF_RESET(card);

	if (f_do_rx_cur && skb)	/* Single transfer pending. Free curr buff also */
		dev_kfree_skb_any(skb);

	return ret;
}

static int nxpwifi_process_int_status(struct nxpwifi_adapter *adapter, u8 sdio_ireg)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	int ret = 0;
	struct sk_buff *skb;
	u8 port;
	u32 len_reg_l, len_reg_u;
	u32 rx_blocks;
	u16 rx_len;
	u32 bitmap;
	u8 cr;

	if (!sdio_ireg)
		return ret;

	if (sdio_ireg & DN_LD_CMD_PORT_HOST_INT_STATUS && adapter->cmd_sent)
		adapter->cmd_sent = false;

	if (sdio_ireg & UP_LD_CMD_PORT_HOST_INT_STATUS) {
		u32 pkt_type;

		/* read the len of control packet */
		rx_len = card->mp_regs[reg->cmd_rd_len_1] << 8;
		rx_len |= (u16)card->mp_regs[reg->cmd_rd_len_0];
		rx_blocks = DIV_ROUND_UP(rx_len, NXPWIFI_SDIO_BLOCK_SIZE);
		if (rx_len <= adapter->intf_hdr_len ||
		    (rx_blocks * NXPWIFI_SDIO_BLOCK_SIZE) >
		     NXPWIFI_RX_DATA_BUF_SIZE)
			return -EINVAL;
		rx_len = (u16)(rx_blocks * NXPWIFI_SDIO_BLOCK_SIZE);

		skb = nxpwifi_alloc_dma_align_buf(rx_len, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		skb_put(skb, rx_len);

		ret = nxpwifi_sdio_card_to_host(adapter, &pkt_type, skb->data,
						skb->len, adapter->ioport |
						CMD_PORT_SLCT);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR, "failed to card_to_host");
			dev_kfree_skb_any(skb);
			goto term_cmd;
		}

		if (pkt_type != NXPWIFI_TYPE_CMD &&
		    pkt_type != NXPWIFI_TYPE_EVENT)
			nxpwifi_dbg(adapter, ERROR, "Received wrong packet on cmd port");

		nxpwifi_decode_rx_packet(adapter, skb, pkt_type);
	}

	if (sdio_ireg & DN_LD_HOST_INT_STATUS) {
		bitmap = (u32)card->mp_regs[reg->wr_bitmap_l];
		bitmap |= ((u32)card->mp_regs[reg->wr_bitmap_u]) << 8;
		bitmap |= ((u32)card->mp_regs[reg->wr_bitmap_1l]) << 16;
		bitmap |= ((u32)card->mp_regs[reg->wr_bitmap_1u]) << 24;
		card->mp_wr_bitmap = bitmap;

		nxpwifi_dbg(adapter, INTR, "intr: wr_bitmap=0x%x\n", card->mp_wr_bitmap);
		if (adapter->data_sent &&
		    (card->mp_wr_bitmap & card->mp_data_port_mask)) {
			nxpwifi_dbg(adapter, INTR, "Tx DONE\n");
			adapter->data_sent = false;
		}
	}

	nxpwifi_dbg(adapter, INTR, "cmd_sent=%d data_sent=%d\n",
		    adapter->cmd_sent, adapter->data_sent);
	if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
		bitmap = (u32)card->mp_regs[reg->rd_bitmap_l];
		bitmap |= ((u32)card->mp_regs[reg->rd_bitmap_u]) << 8;
		bitmap |= ((u32)card->mp_regs[reg->rd_bitmap_1l]) << 16;
		bitmap |= ((u32)card->mp_regs[reg->rd_bitmap_1u]) << 24;
		card->mp_rd_bitmap = bitmap;
		nxpwifi_dbg(adapter, INTR, "intr: rd_bitmap=0x%x\n", card->mp_rd_bitmap);

		while (true) {
			ret = nxpwifi_get_rd_port(adapter, &port);

			if (ret)
				break;

			len_reg_l = reg->rd_len_p0_l + (port << 1);
			len_reg_u = reg->rd_len_p0_u + (port << 1);
			rx_len = ((u16)card->mp_regs[len_reg_u]) << 8;
			rx_len |= (u16)card->mp_regs[len_reg_l];
			rx_blocks =
				(rx_len + NXPWIFI_SDIO_BLOCK_SIZE -
				 1) / NXPWIFI_SDIO_BLOCK_SIZE;
			if (rx_len <= adapter->intf_hdr_len ||
			    (card->mpa_rx.enabled &&
			     ((rx_blocks * NXPWIFI_SDIO_BLOCK_SIZE) >
			      card->mpa_rx.buf_size))) {
				nxpwifi_dbg(adapter, ERROR, "invalid rx_len=%d\n", rx_len);
				ret = -EINVAL;
				goto term_cmd;
			}

			rx_len = (u16)(rx_blocks * NXPWIFI_SDIO_BLOCK_SIZE);

			ret = nxpwifi_sdio_card_to_host_mp_aggr(adapter, rx_len,
								port);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR,
					    "card_to_host_mpa failed: int status=%#x\n",
					    sdio_ireg);
				goto term_cmd;
			}
		}
	}

	return 0;

term_cmd:
	/* terminate cmd */
	if (nxpwifi_read_reg(adapter, CONFIGURATION_REG, &cr))
		nxpwifi_dbg(adapter, ERROR, "read CFG reg failed\n");
	else
		nxpwifi_dbg(adapter, INFO, "info: CFG reg val = %d\n", cr);

	if (nxpwifi_write_reg(adapter, CONFIGURATION_REG, (cr | 0x04)))
		nxpwifi_dbg(adapter, ERROR, "write CFG reg failed\n");
	else
		nxpwifi_dbg(adapter, INFO, "info: write success\n");

	if (nxpwifi_read_reg(adapter, CONFIGURATION_REG, &cr))
		nxpwifi_dbg(adapter, ERROR, "read CFG reg failed\n");
	else
		nxpwifi_dbg(adapter, INFO, "info: CFG reg val =%x\n", cr);

	return ret;
}

/* Transmit using SDIO multi-port aggregation. */
static int nxpwifi_host_to_card_mp_aggr(struct nxpwifi_adapter *adapter,
					u8 *payload, u32 pkt_len, u32 port,
					u32 next_pkt_len)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret = 0;
	s32 f_send_aggr_buf = 0;
	s32 f_send_cur_buf = 0;
	s32 f_precopy_cur_buf = 0;
	s32 f_postcopy_cur_buf = 0;
	u32 mport;
	int index;

	if (!card->mpa_tx.enabled || port == CMD_PORT_SLCT) {
		nxpwifi_dbg(adapter, WARN, "tx aggregation disabled\n");
		f_send_cur_buf = 1;
		goto tx_curr_single;
	}

	if (next_pkt_len) {
		/* More pkt in TX queue */

		if (MP_TX_AGGR_IN_PROGRESS(card)) {
			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len)) {
				f_precopy_cur_buf = 1;

				if (!(card->mp_wr_bitmap &
				      (1 << card->curr_wr_port)) ||
				    !MP_TX_AGGR_BUF_HAS_ROOM
				     (card, pkt_len + next_pkt_len))
					f_send_aggr_buf = 1;
			} else {
				/* No room in Aggr buf, send it */
				f_send_aggr_buf = 1;

				if (!(card->mp_wr_bitmap &
				      (1 << card->curr_wr_port)))
					f_send_cur_buf = 1;
				else
					f_postcopy_cur_buf = 1;
			}
		} else {
			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len) &&
			    (card->mp_wr_bitmap & (1 << card->curr_wr_port)))
				f_precopy_cur_buf = 1;
			else
				f_send_cur_buf = 1;
		}
	} else {
		/* Last pkt in TX queue */

		if (MP_TX_AGGR_IN_PROGRESS(card)) {
			/* some packs in Aggr buf already */
			f_send_aggr_buf = 1;

			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len))
				f_precopy_cur_buf = 1;
			else
				/* No room in Aggr buf, send it */
				f_send_cur_buf = 1;
		} else {
			f_send_cur_buf = 1;
		}
	}

	if (f_precopy_cur_buf) {
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);

		if (MP_TX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_tx_aggr_port_limit_reached(card))
			/* No more pkts allowed in Aggr buf, send it */
			f_send_aggr_buf = 1;
	}

	if (f_send_aggr_buf) {
		u32 port_count;
		int i;

		for (i = 0, port_count = 0; i < card->max_ports; i++)
			if (card->mpa_tx.ports & BIT(i))
				port_count++;

		/*
		 * Writing data from "start_port + 0" to "start_port +
		 * port_count -1", so decrease the count by 1
		 */
		port_count--;
		mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
			 (port_count << 8)) + card->mpa_tx.start_port;

		if (card->mpa_tx.pkt_cnt == 1)
			mport = adapter->ioport + card->mpa_tx.start_port;

		ret = nxpwifi_write_data_to_card(adapter, card->mpa_tx.buf,
						 card->mpa_tx.buf_len, mport);

		/* Save the last multi port tx aggregation info to debug log */
		index = adapter->dbg.last_sdio_mp_index;
		index = (index + 1) % NXPWIFI_DBG_SDIO_MP_NUM;
		adapter->dbg.last_sdio_mp_index = index;
		adapter->dbg.last_mp_wr_ports[index] = mport;
		adapter->dbg.last_mp_wr_bitmap[index] = card->mp_wr_bitmap;
		adapter->dbg.last_mp_wr_len[index] = card->mpa_tx.buf_len;
		adapter->dbg.last_mp_curr_wr_port[index] = card->curr_wr_port;

		MP_TX_AGGR_BUF_RESET(card);
	}

tx_curr_single:
	if (f_send_cur_buf)
		ret = nxpwifi_write_data_to_card(adapter, payload, pkt_len,
						 adapter->ioport + port);

	if (f_postcopy_cur_buf)
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);

	return ret;
}

static int nxpwifi_sdio_host_to_card(struct nxpwifi_adapter *adapter,
				     u8 type, struct sk_buff *skb,
				     struct nxpwifi_tx_param *tx_param)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u32 buf_block_len;
	u32 blk_size;
	u32 port;
	u8 *payload = (u8 *)skb->data;
	u32 pkt_len = skb->len;

	/* Allocate buffer and copy payload */
	blk_size = NXPWIFI_SDIO_BLOCK_SIZE;
	buf_block_len = (pkt_len + blk_size - 1) / blk_size;
	put_unaligned_le16((u16)pkt_len, payload + 0);
	put_unaligned_le16((u16)type, payload + 2);

	/*
	 * This is SDIO specific header
	 *  u16 length,
	 *  u16 type (NXPWIFI_TYPE_DATA = 0, NXPWIFI_TYPE_CMD = 1,
	 *  NXPWIFI_TYPE_EVENT = 3)
	 */
	if (type == NXPWIFI_TYPE_DATA) {
		ret = nxpwifi_get_wr_port_data(adapter, &port);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR, "no wr_port available\n");
			return ret;
		}
	} else {
		adapter->cmd_sent = true;

		if (pkt_len <= adapter->intf_hdr_len ||
		    pkt_len > NXPWIFI_UPLD_SIZE) {
			nxpwifi_dbg(adapter, ERROR,
				    "invalid upld pkt_len=%u (hdr_len=%u, max=%u)\n",
				    pkt_len, adapter->intf_hdr_len, NXPWIFI_UPLD_SIZE);
			return -EINVAL;
		}

		port = CMD_PORT_SLCT;
	}

	/* Transfer data to card */
	pkt_len = buf_block_len * blk_size;

	if (tx_param)
		ret = nxpwifi_host_to_card_mp_aggr(adapter, payload, pkt_len,
						   port, tx_param->next_pkt_len
						   );
	else
		ret = nxpwifi_host_to_card_mp_aggr(adapter, payload, pkt_len,
						   port, 0);

	if (ret) {
		if (type == NXPWIFI_TYPE_CMD ||
		    type == NXPWIFI_TYPE_VDLL)
			adapter->cmd_sent = false;
		if (type == NXPWIFI_TYPE_DATA) {
			adapter->data_sent = false;
			/* restore curr_wr_port in error cases */
			card->curr_wr_port = port;
			card->mp_wr_bitmap |= (u32)(1 << card->curr_wr_port);
		}
	} else {
		if (type == NXPWIFI_TYPE_DATA) {
			if (!(card->mp_wr_bitmap & (1 << card->curr_wr_port)))
				adapter->data_sent = true;
			else
				adapter->data_sent = false;
		}
	}

	return ret;
}

static int nxpwifi_alloc_sdio_mpa_buffers(struct nxpwifi_adapter *adapter,
					  u32 mpa_tx_buf_size,
					  u32 mpa_rx_buf_size)
{
	struct sdio_mmc_card *card = adapter->card;
	u32 rx_buf_size;
	int ret = 0;

	card->mpa_tx.buf = kzalloc(mpa_tx_buf_size, GFP_KERNEL);
	if (!card->mpa_tx.buf) {
		ret = -ENOMEM;
		goto error;
	}

	card->mpa_tx.buf_size = mpa_tx_buf_size;

	rx_buf_size = max_t(u32, mpa_rx_buf_size,
			    (u32)SDIO_MAX_AGGR_BUF_SIZE);
	card->mpa_rx.buf = kzalloc(rx_buf_size, GFP_KERNEL);
	if (!card->mpa_rx.buf) {
		ret = -ENOMEM;
		goto error;
	}

	card->mpa_rx.buf_size = rx_buf_size;

error:
	if (ret) {
		kfree(card->mpa_tx.buf);
		kfree(card->mpa_rx.buf);
		card->mpa_tx.buf_size = 0;
		card->mpa_rx.buf_size = 0;
		card->mpa_tx.buf = NULL;
		card->mpa_rx.buf = NULL;
	}

	return ret;
}

static void
nxpwifi_unregister_dev(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	if (adapter->card) {
		card->adapter = NULL;
		sdio_claim_host(card->func);
		sdio_disable_func(card->func);
		sdio_release_host(card->func);
	}
}

static int nxpwifi_register_dev(struct nxpwifi_adapter *adapter)
{
	int ret;
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;
	const char *firmware = card->firmware;

	/* save adapter pointer in card */
	card->adapter = adapter;
	adapter->tx_buf_size = card->tx_buf_size;

	sdio_claim_host(func);

	/* Set block size */
	ret = sdio_set_block_size(card->func, NXPWIFI_SDIO_BLOCK_SIZE);
	sdio_release_host(func);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "cannot set SDIO block size\n");
		return ret;
	}

	/*
	 * Select correct firmware (sdsd or sdiouart) firmware based on the strapping
	 * option
	 */
	if (card->firmware_sdiouart) {
		u8 val;

		nxpwifi_read_reg(adapter, card->reg->host_strap_reg, &val);
		if ((val & card->reg->host_strap_mask) == card->reg->host_strap_value)
			firmware = card->firmware_sdiouart;
	}
	strscpy(adapter->fw_name, firmware, sizeof(adapter->fw_name));

	if (card->fw_dump_enh) {
		adapter->mem_type_mapping_tbl = generic_mem_type_map;
		adapter->num_mem_types = 1;
	} else {
		adapter->mem_type_mapping_tbl = mem_type_mapping_tbl;
		adapter->num_mem_types = ARRAY_SIZE(mem_type_mapping_tbl);
	}

	return 0;
}

static int nxpwifi_init_sdio(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	int ret;
	u8 sdio_ireg;

	sdio_set_drvdata(card->func, card);

	/*
	 * Read the host_int_status_reg for ACK the first interrupt got
	 * from the bootloader. If we don't do this we get a interrupt
	 * as soon as we register the irq.
	 */
	nxpwifi_read_reg(adapter, card->reg->host_int_status_reg, &sdio_ireg);

	/* Get SDIO ioport */
	if (nxpwifi_init_sdio_ioport(adapter))
		return -EIO;

	/* Initialize SDIO variables in card */
	card->mp_rd_bitmap = 0;
	card->mp_wr_bitmap = 0;
	card->curr_rd_port = reg->start_rd_port;
	card->curr_wr_port = reg->start_wr_port;

	card->mp_data_port_mask = reg->data_port_mask;

	card->mpa_tx.buf_len = 0;
	card->mpa_tx.pkt_cnt = 0;
	card->mpa_tx.start_port = 0;

	card->mpa_tx.enabled = 1;
	card->mpa_tx.pkt_aggr_limit = card->mp_agg_pkt_limit;

	card->mpa_rx.buf_len = 0;
	card->mpa_rx.pkt_cnt = 0;
	card->mpa_rx.start_port = 0;

	card->mpa_rx.enabled = 1;
	card->mpa_rx.pkt_aggr_limit = card->mp_agg_pkt_limit;

	/* Allocate buffers for SDIO MP-A */
	card->mp_regs = devm_kzalloc(&card->func->dev, reg->max_mp_regs, GFP_KERNEL);

	if (!card->mp_regs)
		return -ENOMEM;

	card->mpa_rx.len_arr =
		devm_kcalloc(&card->func->dev, card->mp_agg_pkt_limit,
			     sizeof(*card->mpa_rx.len_arr), GFP_KERNEL);

	if (!card->mpa_rx.len_arr)
		return -ENOMEM;

	ret = nxpwifi_alloc_sdio_mpa_buffers(adapter,
					     card->mp_tx_agg_buf_size,
					     card->mp_rx_agg_buf_size);

	/* Allocate 32k MPA Tx/Rx buffers if 64k memory allocation fails */
	if (ret && (card->mp_tx_agg_buf_size == NXPWIFI_MP_AGGR_BSIZE_MAX ||
		    card->mp_rx_agg_buf_size == NXPWIFI_MP_AGGR_BSIZE_MAX)) {
		/* Disable rx single port aggregation */
		adapter->host_disable_sdio_rx_aggr = true;

		ret = nxpwifi_alloc_sdio_mpa_buffers(adapter,
						     NXPWIFI_MP_AGGR_BSIZE_32K,
						     NXPWIFI_MP_AGGR_BSIZE_32K);
		if (ret) {
			/* Disable multi port aggregation */
			card->mpa_tx.enabled = 0;
			card->mpa_rx.enabled = 0;
		}
	}

	adapter->ext_scan = card->can_ext_scan;
	return ret;
}

static void nxpwifi_cleanup_mpa_buf(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	MP_TX_AGGR_BUF_RESET(card);
	MP_RX_AGGR_BUF_RESET(card);
}

static void nxpwifi_cleanup_sdio(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	cancel_work_sync(&card->work);

	kfree(card->mpa_tx.buf);
	kfree(card->mpa_rx.buf);
}

static void
nxpwifi_update_mp_end_port(struct nxpwifi_adapter *adapter, u16 port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct nxpwifi_sdio_card_reg *reg = card->reg;
	int i;

	card->mp_end_port = port;

	card->mp_data_port_mask = reg->data_port_mask;

	if (reg->start_wr_port) {
		for (i = 1; i <= card->max_ports - card->mp_end_port; i++)
			card->mp_data_port_mask &=
					~(1 << (card->max_ports - i));
	}

	card->curr_wr_port = reg->start_wr_port;
}

/* Perform an SDIO card reset in workqueue context. */
static void nxpwifi_sdio_card_reset_work(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;
	int ret;

	/* Prepare the adapter for the reset. */
	nxpwifi_shutdown_sw(adapter);
	clear_bit(NXPWIFI_IFACE_WORK_DEVICE_DUMP, &card->work_flags);
	clear_bit(NXPWIFI_IFACE_WORK_CARD_RESET, &card->work_flags);

	/* Run a HW reset of the SDIO interface. */
	sdio_claim_host(func);
	ret = mmc_hw_reset(func->card);
	sdio_release_host(func);

	switch (ret) {
	case 1:
		nxpwifi_dbg(adapter, MSG, "SDIO HW reset asynchronous\n");
		complete_all(adapter->fw_done);
		break;
	case 0:
		ret = nxpwifi_reinit_sw(adapter);
		if (ret)
			dev_err(&func->dev, "reinit failed: %d\n", ret);
		break;
	default:
		dev_err(&func->dev, "SDIO HW reset failed: %d\n", ret);
		break;
	}
}

static enum
rdwr_status nxpwifi_sdio_rdwr_firmware(struct nxpwifi_adapter *adapter,
				       u8 doneflag)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret, tries;
	u8 ctrl_data = 0;

	sdio_writeb(card->func, card->reg->fw_dump_host_ready,
		    card->reg->fw_dump_ctrl, &ret);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "SDIO Write ERR\n");
		return RDWR_STATUS_FAILURE;
	}
	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		ctrl_data = sdio_readb(card->func, card->reg->fw_dump_ctrl,
				       &ret);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR, "SDIO read err\n");
			return RDWR_STATUS_FAILURE;
		}
		if (ctrl_data == FW_DUMP_DONE)
			break;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != card->reg->fw_dump_host_ready) {
			nxpwifi_dbg(adapter, WARN,
				    "The ctrl reg was changed, re-try again\n");
			sdio_writeb(card->func, card->reg->fw_dump_host_ready,
				    card->reg->fw_dump_ctrl, &ret);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR, "SDIO write err\n");
				return RDWR_STATUS_FAILURE;
			}
		}
		usleep_range(100, 200);
	}
	if (ctrl_data == card->reg->fw_dump_host_ready) {
		nxpwifi_dbg(adapter, ERROR, "Fail to pull ctrl_data\n");
		return RDWR_STATUS_FAILURE;
	}

	return RDWR_STATUS_SUCCESS;
}

/* Dump firmware memories for post-mortem analysis. */
static void nxpwifi_sdio_fw_dump(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret = 0;
	unsigned int reg, reg_start, reg_end;
	u8 *dbg_ptr, *end_ptr, dump_num, idx, i, read_reg, doneflag = 0;
	enum rdwr_status stat;
	u32 memory_size;

	if (!card->can_dump_fw)
		return;

	for (idx = 0; idx < ARRAY_SIZE(mem_type_mapping_tbl); idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}

	nxpwifi_pm_wakeup_card(adapter);
	sdio_claim_host(card->func);

	nxpwifi_dbg(adapter, MSG, "== nxpwifi firmware dump start ==\n");

	stat = nxpwifi_sdio_rdwr_firmware(adapter, doneflag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg = card->reg->fw_dump_start;
	/* Read the number of the memories which will dump */
	dump_num = sdio_readb(card->func, reg, &ret);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "SDIO read memory length err\n");
		goto done;
	}

	/* Read the length of every memory which will dump */
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		stat = nxpwifi_sdio_rdwr_firmware(adapter, doneflag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;

		memory_size = 0;
		reg = card->reg->fw_dump_start;
		for (i = 0; i < 4; i++) {
			read_reg = sdio_readb(card->func, reg, &ret);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR, "SDIO read err\n");
				goto done;
			}
			memory_size |= (read_reg << i * 8);
			reg++;
		}

		if (memory_size == 0) {
			nxpwifi_dbg(adapter, DUMP, "Firmware dump Finished!\n");
			ret = nxpwifi_write_reg(adapter,
						card->reg->fw_dump_ctrl,
						FW_DUMP_READ_DONE);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR, "SDIO write err\n");
				return;
			}
			break;
		}

		nxpwifi_dbg(adapter, DUMP,
			    "%s_SIZE=0x%x\n", entry->mem_name, memory_size);
		entry->mem_ptr = vmalloc(memory_size + 1);
		entry->mem_size = memory_size;
		if (!entry->mem_ptr)
			goto done;
		dbg_ptr = entry->mem_ptr;
		end_ptr = dbg_ptr + memory_size;

		doneflag = entry->done_flag;
		nxpwifi_dbg(adapter, DUMP, "Start %s output, please wait...\n",
			    entry->mem_name);

		do {
			stat = nxpwifi_sdio_rdwr_firmware(adapter, doneflag);
			if (stat == RDWR_STATUS_FAILURE)
				goto done;

			reg_start = card->reg->fw_dump_start;
			reg_end = card->reg->fw_dump_end;
			for (reg = reg_start; reg <= reg_end; reg++) {
				*dbg_ptr = sdio_readb(card->func, reg, &ret);
				if (ret) {
					nxpwifi_dbg(adapter, ERROR, "SDIO read err\n");
					goto done;
				}
				if (dbg_ptr < end_ptr)
					dbg_ptr++;
				else
					nxpwifi_dbg(adapter, ERROR, "Allocated buf not enough\n");
			}

			if (stat != RDWR_STATUS_DONE)
				continue;

			nxpwifi_dbg(adapter, DUMP, "%s done: size=0x%tx\n",
				    entry->mem_name, dbg_ptr - entry->mem_ptr);
			break;
		} while (1);
	}
	nxpwifi_dbg(adapter, MSG, "== nxpwifi firmware dump end ==\n");

done:
	sdio_release_host(card->func);
}

/* Generic firmware dump flow for enhanced devices. */
static void nxpwifi_sdio_generic_fw_dump(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct memory_type_mapping *entry = &generic_mem_type_map[0];
	unsigned int reg, reg_start, reg_end;
	u8 start_flag = 0, done_flag = 0;
	u8 *dbg_ptr, *end_ptr;
	enum rdwr_status stat;
	int ret = -EPERM, tries;

	if (!card->fw_dump_enh)
		return;

	if (entry->mem_ptr) {
		vfree(entry->mem_ptr);
		entry->mem_ptr = NULL;
	}
	entry->mem_size = 0;

	nxpwifi_pm_wakeup_card(adapter);
	sdio_claim_host(card->func);

	nxpwifi_dbg(adapter, MSG, "== nxpwifi firmware dump start ==\n");

	stat = nxpwifi_sdio_rdwr_firmware(adapter, done_flag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg_start = card->reg->fw_dump_start;
	reg_end = card->reg->fw_dump_end;
	for (reg = reg_start; reg <= reg_end; reg++) {
		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			start_flag = sdio_readb(card->func, reg, &ret);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR, "SDIO read err\n");
				goto done;
			}
			if (start_flag == 0)
				break;
			if (tries == MAX_POLL_TRIES) {
				nxpwifi_dbg(adapter, ERROR, "FW not ready to dump\n");
				ret = -EPERM;
				goto done;
			}
		}
		usleep_range(100, 200);
	}

	entry->mem_ptr = vmalloc(0xf0000 + 1);
	if (!entry->mem_ptr) {
		ret = -ENOMEM;
		goto done;
	}
	dbg_ptr = entry->mem_ptr;
	entry->mem_size = 0xf0000;
	end_ptr = dbg_ptr + entry->mem_size;

	done_flag = entry->done_flag;
	nxpwifi_dbg(adapter, DUMP,
		    "Start %s output, please wait...\n", entry->mem_name);

	while (true) {
		stat = nxpwifi_sdio_rdwr_firmware(adapter, done_flag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;
		for (reg = reg_start; reg <= reg_end; reg++) {
			*dbg_ptr = sdio_readb(card->func, reg, &ret);
			if (ret) {
				nxpwifi_dbg(adapter, ERROR, "SDIO read err\n");
				goto done;
			}
			dbg_ptr++;
			if (dbg_ptr >= end_ptr) {
				u8 *tmp_ptr;

				tmp_ptr = vmalloc(entry->mem_size + 0x4000 + 1);
				if (!tmp_ptr)
					goto done;

				memcpy(tmp_ptr, entry->mem_ptr,
				       entry->mem_size);
				vfree(entry->mem_ptr);
				entry->mem_ptr = tmp_ptr;
				tmp_ptr = NULL;
				dbg_ptr = entry->mem_ptr + entry->mem_size;
				entry->mem_size += 0x4000;
				end_ptr = entry->mem_ptr + entry->mem_size;
			}
		}
		if (stat == RDWR_STATUS_DONE) {
			entry->mem_size = dbg_ptr - entry->mem_ptr;
			nxpwifi_dbg(adapter, DUMP, "dump %s done size=0x%x\n",
				    entry->mem_name, entry->mem_size);
			ret = 0;
			break;
		}
	}
	nxpwifi_dbg(adapter, MSG, "== nxpwifi firmware dump end ==\n");

done:
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "firmware dump failed\n");
		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}
	sdio_release_host(card->func);
}

/* Build and upload consolidated device dump. */
static void nxpwifi_sdio_device_dump_work(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	adapter->devdump_data = vzalloc(NXPWIFI_FW_DUMP_SIZE);
	if (!adapter->devdump_data)
		return;

	nxpwifi_drv_info_dump(adapter);

	/* Generic firmware dump flow for enhanced devices. */
	if (card->fw_dump_enh)
		nxpwifi_sdio_generic_fw_dump(adapter);
	/* Dump firmware memories for post-mortem analysis. */
	else
		nxpwifi_sdio_fw_dump(adapter);

	nxpwifi_prepare_fw_dump_info(adapter);
	nxpwifi_upload_device_dump(adapter);
}

/* Process deferred SDIO work items. */
static void nxpwifi_sdio_work(struct work_struct *work)
{
	struct sdio_mmc_card *card =
		container_of(work, struct sdio_mmc_card, work);

	/* Build and upload consolidated device dump. */
	if (test_and_clear_bit(NXPWIFI_IFACE_WORK_DEVICE_DUMP,
			       &card->work_flags))
		nxpwifi_sdio_device_dump_work(card->adapter);

	/* Perform an SDIO card reset in workqueue context. */
	if (test_and_clear_bit(NXPWIFI_IFACE_WORK_CARD_RESET,
			       &card->work_flags))
		nxpwifi_sdio_card_reset_work(card->adapter);
}

/* Schedule SDIO card reset. */
static void nxpwifi_sdio_card_reset(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	if (!test_and_set_bit(NXPWIFI_IFACE_WORK_CARD_RESET, &card->work_flags))
		nxpwifi_queue_work(adapter, &card->work);
}

static void nxpwifi_sdio_device_dump(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	if (!test_and_set_bit(NXPWIFI_IFACE_WORK_DEVICE_DUMP,
			      &card->work_flags))
		nxpwifi_queue_work(adapter, &card->work);
}

/* Dump SDIO function and scratch registers into drv_buf. */
static int
nxpwifi_sdio_reg_dump(struct nxpwifi_adapter *adapter, char *drv_buf)
{
	char *p = drv_buf;
	struct sdio_mmc_card *cardp = adapter->card;
	int ret = 0;
	u8 count, func, data, index = 0, size = 0;
	u8 reg, reg_start, reg_end;
	char buf[256], *ptr;

	if (!p)
		return 0;

	nxpwifi_dbg(adapter, MSG, "SDIO register dump start\n");

	nxpwifi_pm_wakeup_card(adapter);

	sdio_claim_host(cardp->func);

	for (count = 0; count < 5; count++) {
		memset(buf, 0, sizeof(buf));
		ptr = buf;

		switch (count) {
		case 0:
			/* Read the registers of SDIO function0 */
			func = count;
			reg_start = 0;
			reg_end = 9;
			break;
		case 1:
			/* Read the registers of SDIO function1 */
			func = count;
			reg_start = cardp->reg->func1_dump_reg_start;
			reg_end = cardp->reg->func1_dump_reg_end;
			break;
		case 2:
			index = 0;
			func = 1;
			reg_start = cardp->reg->func1_spec_reg_table[index++];
			size = cardp->reg->func1_spec_reg_num;
			reg_end = cardp->reg->func1_spec_reg_table[size - 1];
			break;
		default:
			/* Read the scratch registers of SDIO function1 */
			if (count == 4)
				msleep(100);
			func = 1;
			reg_start = cardp->reg->func1_scratch_reg;
			reg_end = reg_start + NXPWIFI_SDIO_SCRATCH_SIZE;
		}

		if (count != 2)
			ptr += scnprintf(ptr, sizeof(buf) - (ptr - buf),
							 "SDIO Func%d (%#x-%#x): ", func, reg_start,
							 reg_end);
		else
			ptr += scnprintf(ptr, sizeof(buf) - (ptr - buf),
							 "SDIO Func%d: ", func);

		for (reg = reg_start; reg <= reg_end;) {
			if (func == 0)
				data = sdio_f0_readb(cardp->func, reg, &ret);
			else
				data = sdio_readb(cardp->func, reg, &ret);

			if (count == 2)
				ptr += scnprintf(ptr, sizeof(buf) - (ptr - buf), "(%#x) ", reg);
			if (!ret) {
				ptr += scnprintf(ptr, sizeof(buf) - (ptr - buf), "%02x ", data);
			} else {
				ptr += scnprintf(ptr, sizeof(buf) - (ptr - buf), "ERR");
				break;
			}

			if (count == 2 && reg < reg_end)
				reg = cardp->reg->func1_spec_reg_table[index++];
			else
				reg++;
		}

		nxpwifi_dbg(adapter, MSG, "%s\n", buf);
		p += sprintf(p, "%s\n", buf);
	}

	sdio_release_host(cardp->func);

	nxpwifi_dbg(adapter, MSG, "SDIO register dump end\n");

	return p - drv_buf;
}

static void nxpwifi_sdio_up_dev(struct nxpwifi_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	u8 sdio_ireg;
	int ret = 0;

	sdio_claim_host(card->func);
	ret = sdio_enable_func(card->func);

	if (ret)
		nxpwifi_dbg(adapter, ERROR, "sdio_enable_func failed: %d\n", ret);

	ret = sdio_set_block_size(card->func, NXPWIFI_SDIO_BLOCK_SIZE);

	if (ret)
		nxpwifi_dbg(adapter, ERROR, "sdio_set_block_size failed: %d\n",	ret);

	sdio_release_host(card->func);

	/*
	 * tx_buf_size might be changed to 3584 by firmware during
	 * data transfer, we will reset to default size.
	 */
	adapter->tx_buf_size = card->tx_buf_size;

	/*
	 * Read the host_int_status_reg for ACK the first interrupt got
	 * from the bootloader. If we don't do this we get a interrupt
	 * as soon as we register the irq.
	 */
	nxpwifi_read_reg(adapter, card->reg->host_int_status_reg, &sdio_ireg);

	if (nxpwifi_init_sdio_ioport(adapter))
		nxpwifi_dbg(adapter, ERROR, "error enabling SDIO port\n");
}

static struct nxpwifi_if_ops sdio_ops = {
	.init_if = nxpwifi_init_sdio,
	.cleanup_if = nxpwifi_cleanup_sdio,
	.check_fw_status = nxpwifi_check_fw_status,
	.check_winner_status = nxpwifi_check_winner_status,
	.prog_fw = nxpwifi_prog_fw_w_helper,
	.register_dev = nxpwifi_register_dev,
	.unregister_dev = nxpwifi_unregister_dev,
	.enable_int = nxpwifi_sdio_enable_host_int,
	.disable_int = nxpwifi_sdio_disable_host_int,
	.process_int_status = nxpwifi_process_int_status,
	.host_to_card = nxpwifi_sdio_host_to_card,
	.wakeup = nxpwifi_pm_wakeup_card,
	.wakeup_complete = nxpwifi_pm_wakeup_card_complete,

	/* SDIO specific */
	.update_mp_end_port = nxpwifi_update_mp_end_port,
	.cleanup_mpa_buf = nxpwifi_cleanup_mpa_buf,
	.cmdrsp_complete = nxpwifi_sdio_cmdrsp_complete,
	.event_complete = nxpwifi_sdio_event_complete,
	.dnld_fw = nxpwifi_sdio_dnld_fw,
	.card_reset = nxpwifi_sdio_card_reset,
	.reg_dump = nxpwifi_sdio_reg_dump,
	.device_dump = nxpwifi_sdio_device_dump,
	.deaggr_pkt = nxpwifi_deaggr_sdio_pkt,
	.up_dev = nxpwifi_sdio_up_dev,
};

module_sdio_driver(nxpwifi_sdio);

MODULE_AUTHOR("NXP International Ltd.");
MODULE_DESCRIPTION("NXP WiFi SDIO Driver version " SDIO_VERSION);
MODULE_VERSION(SDIO_VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(IW61X_SDIO_FW_NAME);

/*
 * Marvell Wireless LAN device driver: SDIO specific handling
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/firmware.h>

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "sdio.h"


#define SDIO_VERSION	"1.0"

/* The mwifiex_sdio_remove() callback function is called when
 * user removes this module from kernel space or ejects
 * the card from the slot. The driver handles these 2 cases
 * differently.
 * If the user is removing the module, the few commands (FUNC_SHUTDOWN,
 * HS_CANCEL etc.) are sent to the firmware.
 * If the card is removed, there is no need to send these command.
 *
 * The variable 'user_rmmod' is used to distinguish these two
 * scenarios. This flag is initialized as FALSE in case the card
 * is removed, and will be set to TRUE for module removal when
 * module_exit function is called.
 */
static u8 user_rmmod;

static struct mwifiex_if_ops sdio_ops;
static unsigned long iface_work_flags;

static struct semaphore add_remove_card_sem;

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

static const struct of_device_id mwifiex_sdio_of_match_table[] = {
	{ .compatible = "marvell,sd8897" },
	{ .compatible = "marvell,sd8997" },
	{ }
};

static irqreturn_t mwifiex_wake_irq_wifi(int irq, void *priv)
{
	struct mwifiex_plt_wake_cfg *cfg = priv;

	if (cfg->irq_wifi >= 0) {
		pr_info("%s: wake by wifi", __func__);
		cfg->wake_by_wifi = true;
		disable_irq_nosync(irq);
	}

	return IRQ_HANDLED;
}

/* This function parse device tree node using mmc subnode devicetree API.
 * The device node is saved in card->plt_of_node.
 * if the device tree node exist and include interrupts attributes, this
 * function will also request platform specific wakeup interrupt.
 */
static int mwifiex_sdio_probe_of(struct device *dev, struct sdio_mmc_card *card)
{
	struct mwifiex_plt_wake_cfg *cfg;
	int ret;

	if (!dev->of_node ||
	    !of_match_node(mwifiex_sdio_of_match_table, dev->of_node)) {
		pr_err("sdio platform data not available");
		return -1;
	}

	card->plt_of_node = dev->of_node;
	card->plt_wake_cfg = devm_kzalloc(dev, sizeof(*card->plt_wake_cfg),
					  GFP_KERNEL);
	cfg = card->plt_wake_cfg;
	if (cfg && card->plt_of_node) {
		cfg->irq_wifi = irq_of_parse_and_map(card->plt_of_node, 0);
		if (!cfg->irq_wifi) {
			dev_err(dev, "fail to parse irq_wifi from device tree");
		} else {
			ret = devm_request_irq(dev, cfg->irq_wifi,
					       mwifiex_wake_irq_wifi,
					       IRQF_TRIGGER_LOW,
					       "wifi_wake", cfg);
			if (ret) {
				dev_err(dev,
					"Failed to request irq_wifi %d (%d)\n",
					cfg->irq_wifi, ret);
			}
			disable_irq(cfg->irq_wifi);
		}
	}

	return 0;
}

/*
 * SDIO probe.
 *
 * This function probes an mwifiex device and registers it. It allocates
 * the card structure, enables SDIO function number and initiates the
 * device registration and initialization procedure by adding a logical
 * interface.
 */
static int
mwifiex_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret;
	struct sdio_mmc_card *card = NULL;

	pr_debug("info: vendor=0x%4.04X device=0x%4.04X class=%d function=%d\n",
		 func->vendor, func->device, func->class, func->num);

	card = kzalloc(sizeof(struct sdio_mmc_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;
	card->device_id = id;

	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

	if (id->driver_data) {
		struct mwifiex_sdio_device *data = (void *)id->driver_data;

		card->firmware = data->firmware;
		card->reg = data->reg;
		card->max_ports = data->max_ports;
		card->mp_agg_pkt_limit = data->mp_agg_pkt_limit;
		card->supports_sdio_new_mode = data->supports_sdio_new_mode;
		card->has_control_mask = data->has_control_mask;
		card->tx_buf_size = data->tx_buf_size;
		card->mp_tx_agg_buf_size = data->mp_tx_agg_buf_size;
		card->mp_rx_agg_buf_size = data->mp_rx_agg_buf_size;
		card->can_dump_fw = data->can_dump_fw;
		card->fw_dump_enh = data->fw_dump_enh;
		card->can_auto_tdls = data->can_auto_tdls;
		card->can_ext_scan = data->can_ext_scan;
	}

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	sdio_release_host(func);

	if (ret) {
		pr_err("%s: failed to enable function\n", __func__);
		kfree(card);
		return -EIO;
	}

	/* device tree node parsing and platform specific configuration*/
	mwifiex_sdio_probe_of(&func->dev, card);

	if (mwifiex_add_card(card, &add_remove_card_sem, &sdio_ops,
			     MWIFIEX_SDIO)) {
		pr_err("%s: add card failed\n", __func__);
		kfree(card);
		sdio_claim_host(func);
		ret = sdio_disable_func(func);
		sdio_release_host(func);
		ret = -1;
	}

	return ret;
}

/*
 * SDIO resume.
 *
 * Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not resumed, this function turns on the traffic and
 * sends a host sleep cancel request to the firmware.
 */
static int mwifiex_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_mmc_card *card;
	struct mwifiex_adapter *adapter;
	mmc_pm_flag_t pm_flag = 0;

	if (func) {
		pm_flag = sdio_get_host_pm_caps(func);
		card = sdio_get_drvdata(func);
		if (!card || !card->adapter) {
			pr_err("resume: invalid card or adapter\n");
			return 0;
		}
	} else {
		pr_err("resume: sdio_func is not specified\n");
		return 0;
	}

	adapter = card->adapter;

	if (!adapter->is_suspended) {
		mwifiex_dbg(adapter, WARN,
			    "device already resumed\n");
		return 0;
	}

	adapter->is_suspended = false;

	/* Disable Host Sleep */
	mwifiex_cancel_hs(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA),
			  MWIFIEX_SYNC_CMD);

	/* Disable platform specific wakeup interrupt */
	if (card->plt_wake_cfg && card->plt_wake_cfg->irq_wifi >= 0) {
		disable_irq_wake(card->plt_wake_cfg->irq_wifi);
		if (!card->plt_wake_cfg->wake_by_wifi)
			disable_irq(card->plt_wake_cfg->irq_wifi);
	}

	return 0;
}

/*
 * SDIO remove.
 *
 * This function removes the interface and frees up the card structure.
 */
static void
mwifiex_sdio_remove(struct sdio_func *func)
{
	struct sdio_mmc_card *card;
	struct mwifiex_adapter *adapter;
	struct mwifiex_private *priv;

	card = sdio_get_drvdata(func);
	if (!card)
		return;

	adapter = card->adapter;
	if (!adapter || !adapter->priv_num)
		return;

	mwifiex_dbg(adapter, INFO, "info: SDIO func num=%d\n", func->num);

	if (user_rmmod) {
		if (adapter->is_suspended)
			mwifiex_sdio_resume(adapter->dev);

		mwifiex_deauthenticate_all(adapter);

		priv = mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_ANY);
		mwifiex_disable_auto_ds(priv);
		mwifiex_init_shutdown_fw(priv, MWIFIEX_FUNC_SHUTDOWN);
	}

	mwifiex_remove_card(card->adapter, &add_remove_card_sem);
}

/*
 * SDIO suspend.
 *
 * Kernel needs to suspend all functions separately. Therefore all
 * registered functions must have drivers with suspend and resume
 * methods. Failing that the kernel simply removes the whole card.
 *
 * If already not suspended, this function allocates and sends a host
 * sleep activate request to the firmware and turns off the traffic.
 */
static int mwifiex_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_mmc_card *card;
	struct mwifiex_adapter *adapter;
	mmc_pm_flag_t pm_flag = 0;
	int ret = 0;

	if (func) {
		pm_flag = sdio_get_host_pm_caps(func);
		pr_debug("cmd: %s: suspend: PM flag = 0x%x\n",
			 sdio_func_id(func), pm_flag);
		if (!(pm_flag & MMC_PM_KEEP_POWER)) {
			pr_err("%s: cannot remain alive while host is"
				" suspended\n", sdio_func_id(func));
			return -ENOSYS;
		}

		card = sdio_get_drvdata(func);
		if (!card || !card->adapter) {
			pr_err("suspend: invalid card or adapter\n");
			return 0;
		}
	} else {
		pr_err("suspend: sdio_func is not specified\n");
		return 0;
	}

	adapter = card->adapter;

	/* Enable platform specific wakeup interrupt */
	if (card->plt_wake_cfg && card->plt_wake_cfg->irq_wifi >= 0) {
		card->plt_wake_cfg->wake_by_wifi = false;
		enable_irq(card->plt_wake_cfg->irq_wifi);
		enable_irq_wake(card->plt_wake_cfg->irq_wifi);
	}

	/* Enable the Host Sleep */
	if (!mwifiex_enable_hs(adapter)) {
		mwifiex_dbg(adapter, ERROR,
			    "cmd: failed to suspend\n");
		adapter->hs_enabling = false;
		return -EFAULT;
	}

	mwifiex_dbg(adapter, INFO,
		    "cmd: suspend with MMC_PM_KEEP_POWER\n");
	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

	/* Indicate device suspended */
	adapter->is_suspended = true;
	adapter->hs_enabling = false;

	return ret;
}

/* Device ID for SD8786 */
#define SDIO_DEVICE_ID_MARVELL_8786   (0x9116)
/* Device ID for SD8787 */
#define SDIO_DEVICE_ID_MARVELL_8787   (0x9119)
/* Device ID for SD8797 */
#define SDIO_DEVICE_ID_MARVELL_8797   (0x9129)
/* Device ID for SD8897 */
#define SDIO_DEVICE_ID_MARVELL_8897   (0x912d)
/* Device ID for SD8887 */
#define SDIO_DEVICE_ID_MARVELL_8887   (0x9135)
/* Device ID for SD8801 */
#define SDIO_DEVICE_ID_MARVELL_8801   (0x9139)
/* Device ID for SD8997 */
#define SDIO_DEVICE_ID_MARVELL_8997   (0x9141)


/* WLAN IDs */
static const struct sdio_device_id mwifiex_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8786),
		.driver_data = (unsigned long) &mwifiex_sdio_sd8786},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8787),
		.driver_data = (unsigned long) &mwifiex_sdio_sd8787},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8797),
		.driver_data = (unsigned long) &mwifiex_sdio_sd8797},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8897),
		.driver_data = (unsigned long) &mwifiex_sdio_sd8897},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8887),
		.driver_data = (unsigned long)&mwifiex_sdio_sd8887},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8801),
		.driver_data = (unsigned long)&mwifiex_sdio_sd8801},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8997),
		.driver_data = (unsigned long)&mwifiex_sdio_sd8997},
	{},
};

MODULE_DEVICE_TABLE(sdio, mwifiex_ids);

static const struct dev_pm_ops mwifiex_sdio_pm_ops = {
	.suspend = mwifiex_sdio_suspend,
	.resume = mwifiex_sdio_resume,
};

static struct sdio_driver mwifiex_sdio = {
	.name = "mwifiex_sdio",
	.id_table = mwifiex_ids,
	.probe = mwifiex_sdio_probe,
	.remove = mwifiex_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.pm = &mwifiex_sdio_pm_ops,
	}
};

/* Write data into SDIO card register. Caller claims SDIO device. */
static int
mwifiex_write_reg_locked(struct sdio_func *func, u32 reg, u8 data)
{
	int ret = -1;
	sdio_writeb(func, data, reg, &ret);
	return ret;
}

/*
 * This function writes data into SDIO card register.
 */
static int
mwifiex_write_reg(struct mwifiex_adapter *adapter, u32 reg, u8 data)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;

	sdio_claim_host(card->func);
	ret = mwifiex_write_reg_locked(card->func, reg, data);
	sdio_release_host(card->func);

	return ret;
}

/*
 * This function reads data from SDIO card register.
 */
static int
mwifiex_read_reg(struct mwifiex_adapter *adapter, u32 reg, u8 *data)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret = -1;
	u8 val;

	sdio_claim_host(card->func);
	val = sdio_readb(card->func, reg, &ret);
	sdio_release_host(card->func);

	*data = val;

	return ret;
}

/*
 * This function writes multiple data into SDIO card memory.
 *
 * This does not work in suspended mode.
 */
static int
mwifiex_write_data_sync(struct mwifiex_adapter *adapter,
			u8 *buffer, u32 pkt_len, u32 port)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u8 blk_mode =
		(port & MWIFIEX_SDIO_BYTE_MODE_MASK) ? BYTE_MODE : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? MWIFIEX_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt =
		(blk_mode ==
		 BLOCK_MODE) ? (pkt_len /
				MWIFIEX_SDIO_BLOCK_SIZE) : pkt_len;
	u32 ioport = (port & MWIFIEX_SDIO_IO_PORT_MASK);

	if (adapter->is_suspended) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: not allowed while suspended\n", __func__);
		return -1;
	}

	sdio_claim_host(card->func);

	ret = sdio_writesb(card->func, ioport, buffer, blk_cnt * blk_size);

	sdio_release_host(card->func);

	return ret;
}

/*
 * This function reads multiple data from SDIO card memory.
 */
static int mwifiex_read_data_sync(struct mwifiex_adapter *adapter, u8 *buffer,
				  u32 len, u32 port, u8 claim)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u8 blk_mode = (port & MWIFIEX_SDIO_BYTE_MODE_MASK) ? BYTE_MODE
		       : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? MWIFIEX_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt = (blk_mode == BLOCK_MODE) ? (len / MWIFIEX_SDIO_BLOCK_SIZE)
			: len;
	u32 ioport = (port & MWIFIEX_SDIO_IO_PORT_MASK);

	if (claim)
		sdio_claim_host(card->func);

	ret = sdio_readsb(card->func, buffer, ioport, blk_cnt * blk_size);

	if (claim)
		sdio_release_host(card->func);

	return ret;
}

/*
 * This function wakes up the card.
 *
 * A host power up command is written to the card configuration
 * register to wake up the card.
 */
static int mwifiex_pm_wakeup_card(struct mwifiex_adapter *adapter)
{
	mwifiex_dbg(adapter, EVENT,
		    "event: wakeup device...\n");

	return mwifiex_write_reg(adapter, CONFIGURATION_REG, HOST_POWER_UP);
}

/*
 * This function is called after the card has woken up.
 *
 * The card configuration register is reset.
 */
static int mwifiex_pm_wakeup_card_complete(struct mwifiex_adapter *adapter)
{
	mwifiex_dbg(adapter, EVENT,
		    "cmd: wakeup device completed\n");

	return mwifiex_write_reg(adapter, CONFIGURATION_REG, 0);
}

/*
 * This function is used to initialize IO ports for the
 * chipsets supporting SDIO new mode eg SD8897.
 */
static int mwifiex_init_sdio_new_mode(struct mwifiex_adapter *adapter)
{
	u8 reg;
	struct sdio_mmc_card *card = adapter->card;

	adapter->ioport = MEM_PORT;

	/* enable sdio new mode */
	if (mwifiex_read_reg(adapter, card->reg->card_cfg_2_1_reg, &reg))
		return -1;
	if (mwifiex_write_reg(adapter, card->reg->card_cfg_2_1_reg,
			      reg | CMD53_NEW_MODE))
		return -1;

	/* Configure cmd port and enable reading rx length from the register */
	if (mwifiex_read_reg(adapter, card->reg->cmd_cfg_0, &reg))
		return -1;
	if (mwifiex_write_reg(adapter, card->reg->cmd_cfg_0,
			      reg | CMD_PORT_RD_LEN_EN))
		return -1;

	/* Enable Dnld/Upld ready auto reset for cmd port after cmd53 is
	 * completed
	 */
	if (mwifiex_read_reg(adapter, card->reg->cmd_cfg_1, &reg))
		return -1;
	if (mwifiex_write_reg(adapter, card->reg->cmd_cfg_1,
			      reg | CMD_PORT_AUTO_EN))
		return -1;

	return 0;
}

/* This function initializes the IO ports.
 *
 * The following operations are performed -
 *      - Read the IO ports (0, 1 and 2)
 *      - Set host interrupt Reset-To-Read to clear
 *      - Set auto re-enable interrupt
 */
static int mwifiex_init_sdio_ioport(struct mwifiex_adapter *adapter)
{
	u8 reg;
	struct sdio_mmc_card *card = adapter->card;

	adapter->ioport = 0;

	if (card->supports_sdio_new_mode) {
		if (mwifiex_init_sdio_new_mode(adapter))
			return -1;
		goto cont;
	}

	/* Read the IO port */
	if (!mwifiex_read_reg(adapter, card->reg->io_port_0_reg, &reg))
		adapter->ioport |= (reg & 0xff);
	else
		return -1;

	if (!mwifiex_read_reg(adapter, card->reg->io_port_1_reg, &reg))
		adapter->ioport |= ((reg & 0xff) << 8);
	else
		return -1;

	if (!mwifiex_read_reg(adapter, card->reg->io_port_2_reg, &reg))
		adapter->ioport |= ((reg & 0xff) << 16);
	else
		return -1;
cont:
	mwifiex_dbg(adapter, INFO,
		    "info: SDIO FUNC1 IO port: %#x\n", adapter->ioport);

	/* Set Host interrupt reset to read to clear */
	if (!mwifiex_read_reg(adapter, card->reg->host_int_rsr_reg, &reg))
		mwifiex_write_reg(adapter, card->reg->host_int_rsr_reg,
				  reg | card->reg->sdio_int_mask);
	else
		return -1;

	/* Dnld/Upld ready set to auto reset */
	if (!mwifiex_read_reg(adapter, card->reg->card_misc_cfg_reg, &reg))
		mwifiex_write_reg(adapter, card->reg->card_misc_cfg_reg,
				  reg | AUTO_RE_ENABLE_INT);
	else
		return -1;

	return 0;
}

/*
 * This function sends data to the card.
 */
static int mwifiex_write_data_to_card(struct mwifiex_adapter *adapter,
				      u8 *payload, u32 pkt_len, u32 port)
{
	u32 i = 0;
	int ret;

	do {
		ret = mwifiex_write_data_sync(adapter, payload, pkt_len, port);
		if (ret) {
			i++;
			mwifiex_dbg(adapter, ERROR,
				    "host_to_card, write iomem\t"
				    "(%d) failed: %d\n", i, ret);
			if (mwifiex_write_reg(adapter, CONFIGURATION_REG, 0x04))
				mwifiex_dbg(adapter, ERROR,
					    "write CFG reg failed\n");

			ret = -1;
			if (i > MAX_WRITE_IOMEM_RETRY)
				return ret;
		}
	} while (ret == -1);

	return ret;
}

/*
 * This function gets the read port.
 *
 * If control port bit is set in MP read bitmap, the control port
 * is returned, otherwise the current read port is returned and
 * the value is increased (provided it does not reach the maximum
 * limit, in which case it is reset to 1)
 */
static int mwifiex_get_rd_port(struct mwifiex_adapter *adapter, u8 *port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	u32 rd_bitmap = card->mp_rd_bitmap;

	mwifiex_dbg(adapter, DATA,
		    "data: mp_rd_bitmap=0x%08x\n", rd_bitmap);

	if (card->supports_sdio_new_mode) {
		if (!(rd_bitmap & reg->data_port_mask))
			return -1;
	} else {
		if (!(rd_bitmap & (CTRL_PORT_MASK | reg->data_port_mask)))
			return -1;
	}

	if ((card->has_control_mask) &&
	    (card->mp_rd_bitmap & CTRL_PORT_MASK)) {
		card->mp_rd_bitmap &= (u32) (~CTRL_PORT_MASK);
		*port = CTRL_PORT;
		mwifiex_dbg(adapter, DATA,
			    "data: port=%d mp_rd_bitmap=0x%08x\n",
			    *port, card->mp_rd_bitmap);
		return 0;
	}

	if (!(card->mp_rd_bitmap & (1 << card->curr_rd_port)))
		return -1;

	/* We are now handling the SDIO data ports */
	card->mp_rd_bitmap &= (u32)(~(1 << card->curr_rd_port));
	*port = card->curr_rd_port;

	if (++card->curr_rd_port == card->max_ports)
		card->curr_rd_port = reg->start_rd_port;

	mwifiex_dbg(adapter, DATA,
		    "data: port=%d mp_rd_bitmap=0x%08x -> 0x%08x\n",
		    *port, rd_bitmap, card->mp_rd_bitmap);

	return 0;
}

/*
 * This function gets the write port for data.
 *
 * The current write port is returned if available and the value is
 * increased (provided it does not reach the maximum limit, in which
 * case it is reset to 1)
 */
static int mwifiex_get_wr_port_data(struct mwifiex_adapter *adapter, u32 *port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	u32 wr_bitmap = card->mp_wr_bitmap;

	mwifiex_dbg(adapter, DATA,
		    "data: mp_wr_bitmap=0x%08x\n", wr_bitmap);

	if (!(wr_bitmap & card->mp_data_port_mask)) {
		adapter->data_sent = true;
		return -EBUSY;
	}

	if (card->mp_wr_bitmap & (1 << card->curr_wr_port)) {
		card->mp_wr_bitmap &= (u32) (~(1 << card->curr_wr_port));
		*port = card->curr_wr_port;
		if (++card->curr_wr_port == card->mp_end_port)
			card->curr_wr_port = reg->start_wr_port;
	} else {
		adapter->data_sent = true;
		return -EBUSY;
	}

	if ((card->has_control_mask) && (*port == CTRL_PORT)) {
		mwifiex_dbg(adapter, ERROR,
			    "invalid data port=%d cur port=%d mp_wr_bitmap=0x%08x -> 0x%08x\n",
			    *port, card->curr_wr_port, wr_bitmap,
			    card->mp_wr_bitmap);
		return -1;
	}

	mwifiex_dbg(adapter, DATA,
		    "data: port=%d mp_wr_bitmap=0x%08x -> 0x%08x\n",
		    *port, wr_bitmap, card->mp_wr_bitmap);

	return 0;
}

/*
 * This function polls the card status.
 */
static int
mwifiex_sdio_poll_card_status(struct mwifiex_adapter *adapter, u8 bits)
{
	struct sdio_mmc_card *card = adapter->card;
	u32 tries;
	u8 cs;

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		if (mwifiex_read_reg(adapter, card->reg->poll_reg, &cs))
			break;
		else if ((cs & bits) == bits)
			return 0;

		usleep_range(10, 20);
	}

	mwifiex_dbg(adapter, ERROR,
		    "poll card status failed, tries = %d\n", tries);

	return -1;
}

/*
 * This function reads the firmware status.
 */
static int
mwifiex_sdio_read_fw_status(struct mwifiex_adapter *adapter, u16 *dat)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	u8 fws0, fws1;

	if (mwifiex_read_reg(adapter, reg->status_reg_0, &fws0))
		return -1;

	if (mwifiex_read_reg(adapter, reg->status_reg_1, &fws1))
		return -1;

	*dat = (u16) ((fws1 << 8) | fws0);

	return 0;
}

/*
 * This function disables the host interrupt.
 *
 * The host interrupt mask is read, the disable bit is reset and
 * written back to the card host interrupt mask register.
 */
static void mwifiex_sdio_disable_host_int(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;

	sdio_claim_host(func);
	mwifiex_write_reg_locked(func, card->reg->host_int_mask_reg, 0);
	sdio_release_irq(func);
	sdio_release_host(func);
}

/*
 * This function reads the interrupt status from card.
 */
static void mwifiex_interrupt_status(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	u8 sdio_ireg;
	unsigned long flags;

	if (mwifiex_read_data_sync(adapter, card->mp_regs,
				   card->reg->max_mp_regs,
				   REG_PORT | MWIFIEX_SDIO_BYTE_MODE_MASK, 0)) {
		mwifiex_dbg(adapter, ERROR, "read mp_regs failed\n");
		return;
	}

	sdio_ireg = card->mp_regs[card->reg->host_int_status_reg];
	if (sdio_ireg) {
		/*
		 * DN_LD_HOST_INT_STATUS and/or UP_LD_HOST_INT_STATUS
		 * For SDIO new mode CMD port interrupts
		 *	DN_LD_CMD_PORT_HOST_INT_STATUS and/or
		 *	UP_LD_CMD_PORT_HOST_INT_STATUS
		 * Clear the interrupt status register
		 */
		mwifiex_dbg(adapter, INTR,
			    "int: sdio_ireg = %#x\n", sdio_ireg);
		spin_lock_irqsave(&adapter->int_lock, flags);
		adapter->int_status |= sdio_ireg;
		spin_unlock_irqrestore(&adapter->int_lock, flags);
	}
}

/*
 * SDIO interrupt handler.
 *
 * This function reads the interrupt status from firmware and handles
 * the interrupt in current thread (ksdioirqd) right away.
 */
static void
mwifiex_sdio_interrupt(struct sdio_func *func)
{
	struct mwifiex_adapter *adapter;
	struct sdio_mmc_card *card;

	card = sdio_get_drvdata(func);
	if (!card || !card->adapter) {
		pr_err("int: func=%p card=%p adapter=%p\n",
		       func, card, card ? card->adapter : NULL);
		return;
	}
	adapter = card->adapter;

	if (!adapter->pps_uapsd_mode && adapter->ps_state == PS_STATE_SLEEP)
		adapter->ps_state = PS_STATE_AWAKE;

	mwifiex_interrupt_status(adapter);
	mwifiex_main_process(adapter);
}

/*
 * This function enables the host interrupt.
 *
 * The host interrupt enable mask is written to the card
 * host interrupt mask register.
 */
static int mwifiex_sdio_enable_host_int(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;
	int ret;

	sdio_claim_host(func);

	/* Request the SDIO IRQ */
	ret = sdio_claim_irq(func, mwifiex_sdio_interrupt);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "claim irq failed: ret=%d\n", ret);
		goto out;
	}

	/* Simply write the mask to the register */
	ret = mwifiex_write_reg_locked(func, card->reg->host_int_mask_reg,
				       card->reg->host_int_enable);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "enable host interrupt failed\n");
		sdio_release_irq(func);
	}

out:
	sdio_release_host(func);
	return ret;
}

/*
 * This function sends a data buffer to the card.
 */
static int mwifiex_sdio_card_to_host(struct mwifiex_adapter *adapter,
				     u32 *type, u8 *buffer,
				     u32 npayload, u32 ioport)
{
	int ret;
	u32 nb;

	if (!buffer) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: buffer is NULL\n", __func__);
		return -1;
	}

	ret = mwifiex_read_data_sync(adapter, buffer, npayload, ioport, 1);

	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: read iomem failed: %d\n", __func__,
			ret);
		return -1;
	}

	nb = le16_to_cpu(*(__le16 *) (buffer));
	if (nb > npayload) {
		mwifiex_dbg(adapter, ERROR,
			    "%s: invalid packet, nb=%d npayload=%d\n",
			    __func__, nb, npayload);
		return -1;
	}

	*type = le16_to_cpu(*(__le16 *) (buffer + 2));

	return ret;
}

/*
 * This function downloads the firmware to the card.
 *
 * Firmware is downloaded to the card in blocks. Every block download
 * is tested for CRC errors, and retried a number of times before
 * returning failure.
 */
static int mwifiex_prog_fw_w_helper(struct mwifiex_adapter *adapter,
				    struct mwifiex_fw_image *fw)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
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
		mwifiex_dbg(adapter, ERROR,
			    "firmware image not found! Terminating download\n");
		return -1;
	}

	mwifiex_dbg(adapter, INFO,
		    "info: downloading FW image (%d bytes)\n",
		    firmware_len);

	/* Assume that the allocated buffer is 8-byte aligned */
	fwbuf = kzalloc(MWIFIEX_UPLD_SIZE, GFP_KERNEL);
	if (!fwbuf)
		return -ENOMEM;

	sdio_claim_host(card->func);

	/* Perform firmware data transfer */
	do {
		/* The host polls for the DN_LD_CARD_RDY and CARD_IO_READY
		   bits */
		ret = mwifiex_sdio_poll_card_status(adapter, CARD_IO_READY |
						    DN_LD_CARD_RDY);
		if (ret) {
			mwifiex_dbg(adapter, ERROR,
				    "FW download with helper:\t"
				    "poll status timeout @ %d\n", offset);
			goto done;
		}

		/* More data? */
		if (offset >= firmware_len)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = mwifiex_read_reg(adapter, reg->base_0_reg,
					       &base0);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "dev BASE0 register read failed:\t"
					    "base0=%#04X(%d). Terminating dnld\n",
					    base0, base0);
				goto done;
			}
			ret = mwifiex_read_reg(adapter, reg->base_1_reg,
					       &base1);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "dev BASE1 register read failed:\t"
					    "base1=%#04X(%d). Terminating dnld\n",
					    base1, base1);
				goto done;
			}
			len = (u16) (((base1 & 0xff) << 8) | (base0 & 0xff));

			if (len)
				break;

			usleep_range(10, 20);
		}

		if (!len) {
			break;
		} else if (len > MWIFIEX_UPLD_SIZE) {
			mwifiex_dbg(adapter, ERROR,
				    "FW dnld failed @ %d, invalid length %d\n",
				    offset, len);
			ret = -1;
			goto done;
		}

		txlen = len;

		if (len & BIT(0)) {
			i++;
			if (i > MAX_WRITE_IOMEM_RETRY) {
				mwifiex_dbg(adapter, ERROR,
					    "FW dnld failed @ %d, over max retry\n",
					    offset);
				ret = -1;
				goto done;
			}
			mwifiex_dbg(adapter, ERROR,
				    "CRC indicated by the helper:\t"
				    "len = 0x%04X, txlen = %d\n", len, txlen);
			len &= ~BIT(0);
			/* Setting this to 0 to resend from same offset */
			txlen = 0;
		} else {
			i = 0;

			/* Set blocksize to transfer - checking for last
			   block */
			if (firmware_len - offset < txlen)
				txlen = firmware_len - offset;

			tx_blocks = (txlen + MWIFIEX_SDIO_BLOCK_SIZE - 1)
				    / MWIFIEX_SDIO_BLOCK_SIZE;

			/* Copy payload to buffer */
			memmove(fwbuf, &firmware[offset], txlen);
		}

		ret = mwifiex_write_data_sync(adapter, fwbuf, tx_blocks *
					      MWIFIEX_SDIO_BLOCK_SIZE,
					      adapter->ioport);
		if (ret) {
			mwifiex_dbg(adapter, ERROR,
				    "FW download, write iomem (%d) failed @ %d\n",
				    i, offset);
			if (mwifiex_write_reg(adapter, CONFIGURATION_REG, 0x04))
				mwifiex_dbg(adapter, ERROR,
					    "write CFG reg failed\n");

			ret = -1;
			goto done;
		}

		offset += txlen;
	} while (true);

	mwifiex_dbg(adapter, MSG,
		    "info: FW download over, size %d bytes\n", offset);

	ret = 0;
done:
	sdio_release_host(card->func);
	kfree(fwbuf);
	return ret;
}

/*
 * This function checks the firmware status in card.
 */
static int mwifiex_check_fw_status(struct mwifiex_adapter *adapter,
				   u32 poll_num)
{
	int ret = 0;
	u16 firmware_stat;
	u32 tries;

	for (tries = 0; tries < poll_num; tries++) {
		ret = mwifiex_sdio_read_fw_status(adapter, &firmware_stat);
		if (ret)
			continue;
		if (firmware_stat == FIRMWARE_READY_SDIO) {
			ret = 0;
			break;
		} else {
			msleep(100);
			ret = -1;
		}
	}

	return ret;
}

/* This function checks if WLAN is the winner.
 */
static int mwifiex_check_winner_status(struct mwifiex_adapter *adapter)
{
	int ret = 0;
	u8 winner = 0;
	struct sdio_mmc_card *card = adapter->card;

	if (mwifiex_read_reg(adapter, card->reg->status_reg_0, &winner))
		return -1;

	if (winner)
		adapter->winner = 0;
	else
		adapter->winner = 1;

	return ret;
}

/*
 * This function decode sdio aggreation pkt.
 *
 * Based on the the data block size and pkt_len,
 * skb data will be decoded to few packets.
 */
static void mwifiex_deaggr_sdio_pkt(struct mwifiex_adapter *adapter,
				    struct sk_buff *skb)
{
	u32 total_pkt_len, pkt_len;
	struct sk_buff *skb_deaggr;
	u32 pkt_type;
	u16 blk_size;
	u8 blk_num;
	u8 *data;

	data = skb->data;
	total_pkt_len = skb->len;

	while (total_pkt_len >= (SDIO_HEADER_OFFSET + INTF_HEADER_LEN)) {
		if (total_pkt_len < adapter->sdio_rx_block_size)
			break;
		blk_num = *(data + BLOCK_NUMBER_OFFSET);
		blk_size = adapter->sdio_rx_block_size * blk_num;
		if (blk_size > total_pkt_len) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: error in blk_size,\t"
				    "blk_num=%d, blk_size=%d, total_pkt_len=%d\n",
				    __func__, blk_num, blk_size, total_pkt_len);
			break;
		}
		pkt_len = le16_to_cpu(*(__le16 *)(data + SDIO_HEADER_OFFSET));
		pkt_type = le16_to_cpu(*(__le16 *)(data + SDIO_HEADER_OFFSET +
					 2));
		if ((pkt_len + SDIO_HEADER_OFFSET) > blk_size) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: error in pkt_len,\t"
				    "pkt_len=%d, blk_size=%d\n",
				    __func__, pkt_len, blk_size);
			break;
		}

		skb_deaggr = mwifiex_alloc_dma_align_buf(pkt_len, GFP_KERNEL);
		if (!skb_deaggr)
			break;
		skb_put(skb_deaggr, pkt_len);
		memcpy(skb_deaggr->data, data + SDIO_HEADER_OFFSET, pkt_len);
		skb_pull(skb_deaggr, INTF_HEADER_LEN);

		mwifiex_handle_rx_packet(adapter, skb_deaggr);
		data += blk_size;
		total_pkt_len -= blk_size;
	}
}

/*
 * This function decodes a received packet.
 *
 * Based on the type, the packet is treated as either a data, or
 * a command response, or an event, and the correct handler
 * function is invoked.
 */
static int mwifiex_decode_rx_packet(struct mwifiex_adapter *adapter,
				    struct sk_buff *skb, u32 upld_typ)
{
	u8 *cmd_buf;
	__le16 *curr_ptr = (__le16 *)skb->data;
	u16 pkt_len = le16_to_cpu(*curr_ptr);
	struct mwifiex_rxinfo *rx_info;

	if (upld_typ != MWIFIEX_TYPE_AGGR_DATA) {
		skb_trim(skb, pkt_len);
		skb_pull(skb, INTF_HEADER_LEN);
	}

	switch (upld_typ) {
	case MWIFIEX_TYPE_AGGR_DATA:
		mwifiex_dbg(adapter, INFO,
			    "info: --- Rx: Aggr Data packet ---\n");
		rx_info = MWIFIEX_SKB_RXCB(skb);
		rx_info->buf_type = MWIFIEX_TYPE_AGGR_DATA;
		if (adapter->rx_work_enabled) {
			skb_queue_tail(&adapter->rx_data_q, skb);
			atomic_inc(&adapter->rx_pending);
			adapter->data_received = true;
		} else {
			mwifiex_deaggr_sdio_pkt(adapter, skb);
			dev_kfree_skb_any(skb);
		}
		break;

	case MWIFIEX_TYPE_DATA:
		mwifiex_dbg(adapter, DATA,
			    "info: --- Rx: Data packet ---\n");
		if (adapter->rx_work_enabled) {
			skb_queue_tail(&adapter->rx_data_q, skb);
			adapter->data_received = true;
			atomic_inc(&adapter->rx_pending);
		} else {
			mwifiex_handle_rx_packet(adapter, skb);
		}
		break;

	case MWIFIEX_TYPE_CMD:
		mwifiex_dbg(adapter, CMD,
			    "info: --- Rx: Cmd Response ---\n");
		/* take care of curr_cmd = NULL case */
		if (!adapter->curr_cmd) {
			cmd_buf = adapter->upld_buf;

			if (adapter->ps_state == PS_STATE_SLEEP_CFM)
				mwifiex_process_sleep_confirm_resp(adapter,
								   skb->data,
								   skb->len);

			memcpy(cmd_buf, skb->data,
			       min_t(u32, MWIFIEX_SIZE_OF_CMD_BUFFER,
				     skb->len));

			dev_kfree_skb_any(skb);
		} else {
			adapter->cmd_resp_received = true;
			adapter->curr_cmd->resp_skb = skb;
		}
		break;

	case MWIFIEX_TYPE_EVENT:
		mwifiex_dbg(adapter, EVENT,
			    "info: --- Rx: Event ---\n");
		adapter->event_cause = le32_to_cpu(*(__le32 *) skb->data);

		if ((skb->len > 0) && (skb->len  < MAX_EVENT_SIZE))
			memcpy(adapter->event_body,
			       skb->data + MWIFIEX_EVENT_HEADER_LEN,
			       skb->len);

		/* event cause has been saved to adapter->event_cause */
		adapter->event_received = true;
		adapter->event_skb = skb;

		break;

	default:
		mwifiex_dbg(adapter, ERROR,
			    "unknown upload type %#x\n", upld_typ);
		dev_kfree_skb_any(skb);
		break;
	}

	return 0;
}

/*
 * This function transfers received packets from card to driver, performing
 * aggregation if required.
 *
 * For data received on control port, or if aggregation is disabled, the
 * received buffers are uploaded as separate packets. However, if aggregation
 * is enabled and required, the buffers are copied onto an aggregation buffer,
 * provided there is space left, processed and finally uploaded.
 */
static int mwifiex_sdio_card_to_host_mp_aggr(struct mwifiex_adapter *adapter,
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

	if ((card->has_control_mask) && (port == CTRL_PORT)) {
		/* Read the command Resp without aggr */
		mwifiex_dbg(adapter, CMD,
			    "info: %s: no aggregation for cmd\t"
			    "response\n", __func__);

		f_do_rx_cur = 1;
		goto rx_curr_single;
	}

	if (!card->mpa_rx.enabled) {
		mwifiex_dbg(adapter, WARN,
			    "info: %s: rx aggregation disabled\n",
			    __func__);

		f_do_rx_cur = 1;
		goto rx_curr_single;
	}

	if ((!card->has_control_mask && (card->mp_rd_bitmap &
					 card->reg->data_port_mask)) ||
	    (card->has_control_mask && (card->mp_rd_bitmap &
					(~((u32) CTRL_PORT_MASK))))) {
		/* Some more data RX pending */
		mwifiex_dbg(adapter, INFO,
			    "info: %s: not last packet\n", __func__);

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
		mwifiex_dbg(adapter, INFO,
			    "info: %s: last packet\n", __func__);

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
		mwifiex_dbg(adapter, INFO,
			    "info: current packet aggregation\n");
		/* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);

		if (MP_RX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_rx_aggr_port_limit_reached(card)) {
			mwifiex_dbg(adapter, INFO,
				    "info: %s: aggregated packet\t"
				    "limit reached\n", __func__);
			/* No more pkts allowed in Aggr buf, rx it */
			f_do_rx_aggr = 1;
		}
	}

	if (f_do_rx_aggr) {
		/* do aggr RX now */
		mwifiex_dbg(adapter, DATA,
			    "info: do_rx_aggr: num of packets: %d\n",
			    card->mpa_rx.pkt_cnt);

		if (card->supports_sdio_new_mode) {
			int i;
			u32 port_count;

			for (i = 0, port_count = 0; i < card->max_ports; i++)
				if (card->mpa_rx.ports & BIT(i))
					port_count++;

			/* Reading data from "start_port + 0" to "start_port +
			 * port_count -1", so decrease the count by 1
			 */
			port_count--;
			mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
				 (port_count << 8)) + card->mpa_rx.start_port;
		} else {
			mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
				 (card->mpa_rx.ports << 4)) +
				 card->mpa_rx.start_port;
		}

		if (card->mpa_rx.pkt_cnt == 1)
			mport = adapter->ioport + port;

		if (mwifiex_read_data_sync(adapter, card->mpa_rx.buf,
					   card->mpa_rx.buf_len, mport, 1))
			goto error;

		curr_ptr = card->mpa_rx.buf;

		for (pind = 0; pind < card->mpa_rx.pkt_cnt; pind++) {
			u32 *len_arr = card->mpa_rx.len_arr;

			/* get curr PKT len & type */
			pkt_len = le16_to_cpu(*(__le16 *) &curr_ptr[0]);
			pkt_type = le16_to_cpu(*(__le16 *) &curr_ptr[2]);

			/* copy pkt to deaggr buf */
			skb_deaggr = mwifiex_alloc_dma_align_buf(len_arr[pind],
								 GFP_KERNEL);
			if (!skb_deaggr) {
				mwifiex_dbg(adapter, ERROR, "skb allocation failure\t"
					    "drop pkt len=%d type=%d\n",
					    pkt_len, pkt_type);
				curr_ptr += len_arr[pind];
				continue;
			}

			skb_put(skb_deaggr, len_arr[pind]);

			if ((pkt_type == MWIFIEX_TYPE_DATA ||
			     (pkt_type == MWIFIEX_TYPE_AGGR_DATA &&
			      adapter->sdio_rx_aggr_enable)) &&
			    (pkt_len <= len_arr[pind])) {

				memcpy(skb_deaggr->data, curr_ptr, pkt_len);

				skb_trim(skb_deaggr, pkt_len);

				/* Process de-aggr packet */
				mwifiex_decode_rx_packet(adapter, skb_deaggr,
							 pkt_type);
			} else {
				mwifiex_dbg(adapter, ERROR,
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
		mwifiex_dbg(adapter, INFO, "info: RX: port: %d, rx_len: %d\n",
			    port, rx_len);

		skb = mwifiex_alloc_dma_align_buf(rx_len, GFP_KERNEL | GFP_DMA);
		if (!skb) {
			mwifiex_dbg(adapter, ERROR,
				    "single skb allocated fail,\t"
				    "drop pkt port=%d len=%d\n", port, rx_len);
			if (mwifiex_sdio_card_to_host(adapter, &pkt_type,
						      card->mpa_rx.buf, rx_len,
						      adapter->ioport + port))
				goto error;
			return 0;
		}

		skb_put(skb, rx_len);

		if (mwifiex_sdio_card_to_host(adapter, &pkt_type,
					      skb->data, skb->len,
					      adapter->ioport + port))
			goto error;
		if (!adapter->sdio_rx_aggr_enable &&
		    pkt_type == MWIFIEX_TYPE_AGGR_DATA) {
			mwifiex_dbg(adapter, ERROR, "drop wrong pkt type %d\t"
				    "current SDIO RX Aggr not enabled\n",
				    pkt_type);
			dev_kfree_skb_any(skb);
			return 0;
		}

		mwifiex_decode_rx_packet(adapter, skb, pkt_type);
	}
	if (f_post_aggr_cur) {
		mwifiex_dbg(adapter, INFO,
			    "info: current packet aggregation\n");
		/* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);
	}

	return 0;
error:
	if (MP_RX_AGGR_IN_PROGRESS(card))
		MP_RX_AGGR_BUF_RESET(card);

	if (f_do_rx_cur && skb)
		/* Single transfer pending. Free curr buff also */
		dev_kfree_skb_any(skb);

	return -1;
}

/*
 * This function checks the current interrupt status.
 *
 * The following interrupts are checked and handled by this function -
 *      - Data sent
 *      - Command sent
 *      - Packets received
 *
 * Since the firmware does not generate download ready interrupt if the
 * port updated is command port only, command sent interrupt checking
 * should be done manually, and for every SDIO interrupt.
 *
 * In case of Rx packets received, the packets are uploaded from card to
 * host and processed accordingly.
 */
static int mwifiex_process_int_status(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	int ret = 0;
	u8 sdio_ireg;
	struct sk_buff *skb;
	u8 port = CTRL_PORT;
	u32 len_reg_l, len_reg_u;
	u32 rx_blocks;
	u16 rx_len;
	unsigned long flags;
	u32 bitmap;
	u8 cr;

	spin_lock_irqsave(&adapter->int_lock, flags);
	sdio_ireg = adapter->int_status;
	adapter->int_status = 0;
	spin_unlock_irqrestore(&adapter->int_lock, flags);

	if (!sdio_ireg)
		return ret;

	/* Following interrupt is only for SDIO new mode */
	if (sdio_ireg & DN_LD_CMD_PORT_HOST_INT_STATUS && adapter->cmd_sent)
		adapter->cmd_sent = false;

	/* Following interrupt is only for SDIO new mode */
	if (sdio_ireg & UP_LD_CMD_PORT_HOST_INT_STATUS) {
		u32 pkt_type;

		/* read the len of control packet */
		rx_len = card->mp_regs[reg->cmd_rd_len_1] << 8;
		rx_len |= (u16)card->mp_regs[reg->cmd_rd_len_0];
		rx_blocks = DIV_ROUND_UP(rx_len, MWIFIEX_SDIO_BLOCK_SIZE);
		if (rx_len <= INTF_HEADER_LEN ||
		    (rx_blocks * MWIFIEX_SDIO_BLOCK_SIZE) >
		     MWIFIEX_RX_DATA_BUF_SIZE)
			return -1;
		rx_len = (u16) (rx_blocks * MWIFIEX_SDIO_BLOCK_SIZE);
		mwifiex_dbg(adapter, INFO, "info: rx_len = %d\n", rx_len);

		skb = mwifiex_alloc_dma_align_buf(rx_len, GFP_KERNEL | GFP_DMA);
		if (!skb)
			return -1;

		skb_put(skb, rx_len);

		if (mwifiex_sdio_card_to_host(adapter, &pkt_type, skb->data,
					      skb->len, adapter->ioport |
							CMD_PORT_SLCT)) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: failed to card_to_host", __func__);
			dev_kfree_skb_any(skb);
			goto term_cmd;
		}

		if ((pkt_type != MWIFIEX_TYPE_CMD) &&
		    (pkt_type != MWIFIEX_TYPE_EVENT))
			mwifiex_dbg(adapter, ERROR,
				    "%s:Received wrong packet on cmd port",
				    __func__);

		mwifiex_decode_rx_packet(adapter, skb, pkt_type);
	}

	if (sdio_ireg & DN_LD_HOST_INT_STATUS) {
		bitmap = (u32) card->mp_regs[reg->wr_bitmap_l];
		bitmap |= ((u32) card->mp_regs[reg->wr_bitmap_u]) << 8;
		if (card->supports_sdio_new_mode) {
			bitmap |=
				((u32) card->mp_regs[reg->wr_bitmap_1l]) << 16;
			bitmap |=
				((u32) card->mp_regs[reg->wr_bitmap_1u]) << 24;
		}
		card->mp_wr_bitmap = bitmap;

		mwifiex_dbg(adapter, INTR,
			    "int: DNLD: wr_bitmap=0x%x\n",
			    card->mp_wr_bitmap);
		if (adapter->data_sent &&
		    (card->mp_wr_bitmap & card->mp_data_port_mask)) {
			mwifiex_dbg(adapter, INTR,
				    "info:  <--- Tx DONE Interrupt --->\n");
			adapter->data_sent = false;
		}
	}

	/* As firmware will not generate download ready interrupt if the port
	   updated is command port only, cmd_sent should be done for any SDIO
	   interrupt. */
	if (card->has_control_mask && adapter->cmd_sent) {
		/* Check if firmware has attach buffer at command port and
		   update just that in wr_bit_map. */
		card->mp_wr_bitmap |=
			(u32) card->mp_regs[reg->wr_bitmap_l] & CTRL_PORT_MASK;
		if (card->mp_wr_bitmap & CTRL_PORT_MASK)
			adapter->cmd_sent = false;
	}

	mwifiex_dbg(adapter, INTR, "info: cmd_sent=%d data_sent=%d\n",
		    adapter->cmd_sent, adapter->data_sent);
	if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
		bitmap = (u32) card->mp_regs[reg->rd_bitmap_l];
		bitmap |= ((u32) card->mp_regs[reg->rd_bitmap_u]) << 8;
		if (card->supports_sdio_new_mode) {
			bitmap |=
				((u32) card->mp_regs[reg->rd_bitmap_1l]) << 16;
			bitmap |=
				((u32) card->mp_regs[reg->rd_bitmap_1u]) << 24;
		}
		card->mp_rd_bitmap = bitmap;
		mwifiex_dbg(adapter, INTR,
			    "int: UPLD: rd_bitmap=0x%x\n",
			    card->mp_rd_bitmap);

		while (true) {
			ret = mwifiex_get_rd_port(adapter, &port);
			if (ret) {
				mwifiex_dbg(adapter, INFO,
					    "info: no more rd_port available\n");
				break;
			}
			len_reg_l = reg->rd_len_p0_l + (port << 1);
			len_reg_u = reg->rd_len_p0_u + (port << 1);
			rx_len = ((u16) card->mp_regs[len_reg_u]) << 8;
			rx_len |= (u16) card->mp_regs[len_reg_l];
			mwifiex_dbg(adapter, INFO,
				    "info: RX: port=%d rx_len=%u\n",
				    port, rx_len);
			rx_blocks =
				(rx_len + MWIFIEX_SDIO_BLOCK_SIZE -
				 1) / MWIFIEX_SDIO_BLOCK_SIZE;
			if (rx_len <= INTF_HEADER_LEN ||
			    (card->mpa_rx.enabled &&
			     ((rx_blocks * MWIFIEX_SDIO_BLOCK_SIZE) >
			      card->mpa_rx.buf_size))) {
				mwifiex_dbg(adapter, ERROR,
					    "invalid rx_len=%d\n",
					    rx_len);
				return -1;
			}

			rx_len = (u16) (rx_blocks * MWIFIEX_SDIO_BLOCK_SIZE);
			mwifiex_dbg(adapter, INFO, "info: rx_len = %d\n",
				    rx_len);

			if (mwifiex_sdio_card_to_host_mp_aggr(adapter, rx_len,
							      port)) {
				mwifiex_dbg(adapter, ERROR,
					    "card_to_host_mpa failed: int status=%#x\n",
					    sdio_ireg);
				goto term_cmd;
			}
		}
	}

	return 0;

term_cmd:
	/* terminate cmd */
	if (mwifiex_read_reg(adapter, CONFIGURATION_REG, &cr))
		mwifiex_dbg(adapter, ERROR, "read CFG reg failed\n");
	else
		mwifiex_dbg(adapter, INFO,
			    "info: CFG reg val = %d\n", cr);

	if (mwifiex_write_reg(adapter, CONFIGURATION_REG, (cr | 0x04)))
		mwifiex_dbg(adapter, ERROR,
			    "write CFG reg failed\n");
	else
		mwifiex_dbg(adapter, INFO, "info: write success\n");

	if (mwifiex_read_reg(adapter, CONFIGURATION_REG, &cr))
		mwifiex_dbg(adapter, ERROR,
			    "read CFG reg failed\n");
	else
		mwifiex_dbg(adapter, INFO,
			    "info: CFG reg val =%x\n", cr);

	return -1;
}

/*
 * This function aggregates transmission buffers in driver and downloads
 * the aggregated packet to card.
 *
 * The individual packets are aggregated by copying into an aggregation
 * buffer and then downloaded to the card. Previous unsent packets in the
 * aggregation buffer are pre-copied first before new packets are added.
 * Aggregation is done till there is space left in the aggregation buffer,
 * or till new packets are available.
 *
 * The function will only download the packet to the card when aggregation
 * stops, otherwise it will just aggregate the packet in aggregation buffer
 * and return.
 */
static int mwifiex_host_to_card_mp_aggr(struct mwifiex_adapter *adapter,
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

	if (!card->mpa_tx.enabled ||
	    (card->has_control_mask && (port == CTRL_PORT)) ||
	    (card->supports_sdio_new_mode && (port == CMD_PORT_SLCT))) {
		mwifiex_dbg(adapter, WARN,
			    "info: %s: tx aggregation disabled\n",
			    __func__);

		f_send_cur_buf = 1;
		goto tx_curr_single;
	}

	if (next_pkt_len) {
		/* More pkt in TX queue */
		mwifiex_dbg(adapter, INFO,
			    "info: %s: more packets in queue.\n",
			    __func__);

		if (MP_TX_AGGR_IN_PROGRESS(card)) {
			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len)) {
				f_precopy_cur_buf = 1;

				if (!(card->mp_wr_bitmap &
				      (1 << card->curr_wr_port)) ||
				    !MP_TX_AGGR_BUF_HAS_ROOM(
					    card, pkt_len + next_pkt_len))
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
		mwifiex_dbg(adapter, INFO,
			    "info: %s: Last packet in Tx Queue.\n",
			    __func__);

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
		mwifiex_dbg(adapter, DATA,
			    "data: %s: precopy current buffer\n",
			    __func__);
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);

		if (MP_TX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_tx_aggr_port_limit_reached(card))
			/* No more pkts allowed in Aggr buf, send it */
			f_send_aggr_buf = 1;
	}

	if (f_send_aggr_buf) {
		mwifiex_dbg(adapter, DATA,
			    "data: %s: send aggr buffer: %d %d\n",
			    __func__, card->mpa_tx.start_port,
			    card->mpa_tx.ports);
		if (card->supports_sdio_new_mode) {
			u32 port_count;
			int i;

			for (i = 0, port_count = 0; i < card->max_ports; i++)
				if (card->mpa_tx.ports & BIT(i))
					port_count++;

			/* Writing data from "start_port + 0" to "start_port +
			 * port_count -1", so decrease the count by 1
			 */
			port_count--;
			mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
				 (port_count << 8)) + card->mpa_tx.start_port;
		} else {
			mport = (adapter->ioport | SDIO_MPA_ADDR_BASE |
				 (card->mpa_tx.ports << 4)) +
				 card->mpa_tx.start_port;
		}

		if (card->mpa_tx.pkt_cnt == 1)
			mport = adapter->ioport + port;

		ret = mwifiex_write_data_to_card(adapter, card->mpa_tx.buf,
						 card->mpa_tx.buf_len, mport);

		/* Save the last multi port tx aggreagation info to debug log */
		index = adapter->dbg.last_sdio_mp_index;
		index = (index + 1) % MWIFIEX_DBG_SDIO_MP_NUM;
		adapter->dbg.last_sdio_mp_index = index;
		adapter->dbg.last_mp_wr_ports[index] = mport;
		adapter->dbg.last_mp_wr_bitmap[index] = card->mp_wr_bitmap;
		adapter->dbg.last_mp_wr_len[index] = card->mpa_tx.buf_len;
		adapter->dbg.last_mp_curr_wr_port[index] = card->curr_wr_port;

		MP_TX_AGGR_BUF_RESET(card);
	}

tx_curr_single:
	if (f_send_cur_buf) {
		mwifiex_dbg(adapter, DATA,
			    "data: %s: send current buffer %d\n",
			    __func__, port);
		ret = mwifiex_write_data_to_card(adapter, payload, pkt_len,
						 adapter->ioport + port);
	}

	if (f_postcopy_cur_buf) {
		mwifiex_dbg(adapter, DATA,
			    "data: %s: postcopy current buffer\n",
			    __func__);
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);
	}

	return ret;
}

/*
 * This function downloads data from driver to card.
 *
 * Both commands and data packets are transferred to the card by this
 * function.
 *
 * This function adds the SDIO specific header to the front of the buffer
 * before transferring. The header contains the length of the packet and
 * the type. The firmware handles the packets based upon this set type.
 */
static int mwifiex_sdio_host_to_card(struct mwifiex_adapter *adapter,
				     u8 type, struct sk_buff *skb,
				     struct mwifiex_tx_param *tx_param)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret;
	u32 buf_block_len;
	u32 blk_size;
	u32 port = CTRL_PORT;
	u8 *payload = (u8 *)skb->data;
	u32 pkt_len = skb->len;

	/* Allocate buffer and copy payload */
	blk_size = MWIFIEX_SDIO_BLOCK_SIZE;
	buf_block_len = (pkt_len + blk_size - 1) / blk_size;
	*(__le16 *)&payload[0] = cpu_to_le16((u16)pkt_len);
	*(__le16 *)&payload[2] = cpu_to_le16(type);

	/*
	 * This is SDIO specific header
	 *  u16 length,
	 *  u16 type (MWIFIEX_TYPE_DATA = 0, MWIFIEX_TYPE_CMD = 1,
	 *  MWIFIEX_TYPE_EVENT = 3)
	 */
	if (type == MWIFIEX_TYPE_DATA) {
		ret = mwifiex_get_wr_port_data(adapter, &port);
		if (ret) {
			mwifiex_dbg(adapter, ERROR,
				    "%s: no wr_port available\n",
				    __func__);
			return ret;
		}
	} else {
		adapter->cmd_sent = true;
		/* Type must be MWIFIEX_TYPE_CMD */

		if (pkt_len <= INTF_HEADER_LEN ||
		    pkt_len > MWIFIEX_UPLD_SIZE)
			mwifiex_dbg(adapter, ERROR,
				    "%s: payload=%p, nb=%d\n",
				    __func__, payload, pkt_len);

		if (card->supports_sdio_new_mode)
			port = CMD_PORT_SLCT;
	}

	/* Transfer data to card */
	pkt_len = buf_block_len * blk_size;

	if (tx_param)
		ret = mwifiex_host_to_card_mp_aggr(adapter, payload, pkt_len,
						   port, tx_param->next_pkt_len
						   );
	else
		ret = mwifiex_host_to_card_mp_aggr(adapter, payload, pkt_len,
						   port, 0);

	if (ret) {
		if (type == MWIFIEX_TYPE_CMD)
			adapter->cmd_sent = false;
		if (type == MWIFIEX_TYPE_DATA) {
			adapter->data_sent = false;
			/* restore curr_wr_port in error cases */
			card->curr_wr_port = port;
			card->mp_wr_bitmap |= (u32)(1 << card->curr_wr_port);
		}
	} else {
		if (type == MWIFIEX_TYPE_DATA) {
			if (!(card->mp_wr_bitmap & (1 << card->curr_wr_port)))
				adapter->data_sent = true;
			else
				adapter->data_sent = false;
		}
	}

	return ret;
}

/*
 * This function allocates the MPA Tx and Rx buffers.
 */
static int mwifiex_alloc_sdio_mpa_buffers(struct mwifiex_adapter *adapter,
				   u32 mpa_tx_buf_size, u32 mpa_rx_buf_size)
{
	struct sdio_mmc_card *card = adapter->card;
	u32 rx_buf_size;
	int ret = 0;

	card->mpa_tx.buf = kzalloc(mpa_tx_buf_size, GFP_KERNEL);
	if (!card->mpa_tx.buf) {
		ret = -1;
		goto error;
	}

	card->mpa_tx.buf_size = mpa_tx_buf_size;

	rx_buf_size = max_t(u32, mpa_rx_buf_size,
			    (u32)SDIO_MAX_AGGR_BUF_SIZE);
	card->mpa_rx.buf = kzalloc(rx_buf_size, GFP_KERNEL);
	if (!card->mpa_rx.buf) {
		ret = -1;
		goto error;
	}

	card->mpa_rx.buf_size = rx_buf_size;

error:
	if (ret) {
		kfree(card->mpa_tx.buf);
		kfree(card->mpa_rx.buf);
		card->mpa_tx.buf_size = 0;
		card->mpa_rx.buf_size = 0;
	}

	return ret;
}

/*
 * This function unregisters the SDIO device.
 *
 * The SDIO IRQ is released, the function is disabled and driver
 * data is set to null.
 */
static void
mwifiex_unregister_dev(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	if (adapter->card) {
		sdio_claim_host(card->func);
		sdio_disable_func(card->func);
		sdio_release_host(card->func);
	}
}

/*
 * This function registers the SDIO device.
 *
 * SDIO IRQ is claimed, block size is set and driver data is initialized.
 */
static int mwifiex_register_dev(struct mwifiex_adapter *adapter)
{
	int ret;
	struct sdio_mmc_card *card = adapter->card;
	struct sdio_func *func = card->func;

	/* save adapter pointer in card */
	card->adapter = adapter;
	adapter->tx_buf_size = card->tx_buf_size;

	sdio_claim_host(func);

	/* Set block size */
	ret = sdio_set_block_size(card->func, MWIFIEX_SDIO_BLOCK_SIZE);
	sdio_release_host(func);
	if (ret) {
		mwifiex_dbg(adapter, ERROR,
			    "cannot set SDIO block size\n");
		return ret;
	}


	adapter->dev = &func->dev;

	strcpy(adapter->fw_name, card->firmware);
	if (card->fw_dump_enh) {
		adapter->mem_type_mapping_tbl = generic_mem_type_map;
		adapter->num_mem_types = 1;
	} else {
		adapter->mem_type_mapping_tbl = mem_type_mapping_tbl;
		adapter->num_mem_types = ARRAY_SIZE(mem_type_mapping_tbl);
	}

	return 0;
}

/*
 * This function initializes the SDIO driver.
 *
 * The following initializations steps are followed -
 *      - Read the Host interrupt status register to acknowledge
 *        the first interrupt got from bootloader
 *      - Disable host interrupt mask register
 *      - Get SDIO port
 *      - Initialize SDIO variables in card
 *      - Allocate MP registers
 *      - Allocate MPA Tx and Rx buffers
 */
static int mwifiex_init_sdio(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	int ret;
	u8 sdio_ireg;

	sdio_set_drvdata(card->func, card);

	/*
	 * Read the host_int_status_reg for ACK the first interrupt got
	 * from the bootloader. If we don't do this we get a interrupt
	 * as soon as we register the irq.
	 */
	mwifiex_read_reg(adapter, card->reg->host_int_status_reg, &sdio_ireg);

	/* Get SDIO ioport */
	mwifiex_init_sdio_ioport(adapter);

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
	card->mp_regs = kzalloc(reg->max_mp_regs, GFP_KERNEL);
	if (!card->mp_regs)
		return -ENOMEM;

	/* Allocate skb pointer buffers */
	card->mpa_rx.skb_arr = kzalloc((sizeof(void *)) *
				       card->mp_agg_pkt_limit, GFP_KERNEL);
	if (!card->mpa_rx.skb_arr) {
		kfree(card->mp_regs);
		return -ENOMEM;
	}

	card->mpa_rx.len_arr = kzalloc(sizeof(*card->mpa_rx.len_arr) *
				       card->mp_agg_pkt_limit, GFP_KERNEL);
	if (!card->mpa_rx.len_arr) {
		kfree(card->mp_regs);
		kfree(card->mpa_rx.skb_arr);
		return -ENOMEM;
	}

	ret = mwifiex_alloc_sdio_mpa_buffers(adapter,
					     card->mp_tx_agg_buf_size,
					     card->mp_rx_agg_buf_size);

	/* Allocate 32k MPA Tx/Rx buffers if 64k memory allocation fails */
	if (ret && (card->mp_tx_agg_buf_size == MWIFIEX_MP_AGGR_BUF_SIZE_MAX ||
		    card->mp_rx_agg_buf_size == MWIFIEX_MP_AGGR_BUF_SIZE_MAX)) {
		/* Disable rx single port aggregation */
		adapter->host_disable_sdio_rx_aggr = true;

		ret = mwifiex_alloc_sdio_mpa_buffers
			(adapter, MWIFIEX_MP_AGGR_BUF_SIZE_32K,
			 MWIFIEX_MP_AGGR_BUF_SIZE_32K);
		if (ret) {
			/* Disable multi port aggregation */
			card->mpa_tx.enabled = 0;
			card->mpa_rx.enabled = 0;
		}
	}

	adapter->auto_tdls = card->can_auto_tdls;
	adapter->ext_scan = card->can_ext_scan;
	return 0;
}

/*
 * This function resets the MPA Tx and Rx buffers.
 */
static void mwifiex_cleanup_mpa_buf(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	MP_TX_AGGR_BUF_RESET(card);
	MP_RX_AGGR_BUF_RESET(card);
}

/*
 * This function cleans up the allocated card buffers.
 *
 * The following are freed by this function -
 *      - MP registers
 *      - MPA Tx buffer
 *      - MPA Rx buffer
 */
static void mwifiex_cleanup_sdio(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	kfree(card->mp_regs);
	kfree(card->mpa_rx.skb_arr);
	kfree(card->mpa_rx.len_arr);
	kfree(card->mpa_tx.buf);
	kfree(card->mpa_rx.buf);
	sdio_set_drvdata(card->func, NULL);
	kfree(card);
}

/*
 * This function updates the MP end port in card.
 */
static void
mwifiex_update_mp_end_port(struct mwifiex_adapter *adapter, u16 port)
{
	struct sdio_mmc_card *card = adapter->card;
	const struct mwifiex_sdio_card_reg *reg = card->reg;
	int i;

	card->mp_end_port = port;

	card->mp_data_port_mask = reg->data_port_mask;

	if (reg->start_wr_port) {
		for (i = 1; i <= card->max_ports - card->mp_end_port; i++)
			card->mp_data_port_mask &=
					~(1 << (card->max_ports - i));
	}

	card->curr_wr_port = reg->start_wr_port;

	mwifiex_dbg(adapter, CMD,
		    "cmd: mp_end_port %d, data port mask 0x%x\n",
		    port, card->mp_data_port_mask);
}

static void mwifiex_recreate_adapter(struct sdio_mmc_card *card)
{
	struct sdio_func *func = card->func;
	const struct sdio_device_id *device_id = card->device_id;

	/* TODO mmc_hw_reset does not require destroying and re-probing the
	 * whole adapter. Hence there was no need to for this rube-goldberg
	 * design to reload the fw from an external workqueue. If we don't
	 * destroy the adapter we could reload the fw from
	 * mwifiex_main_work_queue directly.
	 * The real difficulty with fw reset is to restore all the user
	 * settings applied through ioctl. By destroying and recreating the
	 * adapter, we take the easy way out, since we rely on user space to
	 * restore them. We assume that user space will treat the new
	 * incarnation of the adapter(interfaces) as if they had been just
	 * discovered and initializes them from scratch.
	 */

	mwifiex_sdio_remove(func);

	/* power cycle the adapter */
	sdio_claim_host(func);
	mmc_hw_reset(func->card->host);
	sdio_release_host(func);

	mwifiex_sdio_probe(func, device_id);
}

static struct mwifiex_adapter *save_adapter;
static void mwifiex_sdio_card_reset_work(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	/* TODO card pointer is unprotected. If the adapter is removed
	 * physically, sdio core might trigger mwifiex_sdio_remove, before this
	 * workqueue is run, which will destroy the adapter struct. When this
	 * workqueue eventually exceutes it will dereference an invalid adapter
	 * pointer
	 */
	mwifiex_recreate_adapter(card);
}

/* This function read/write firmware */
static enum
rdwr_status mwifiex_sdio_rdwr_firmware(struct mwifiex_adapter *adapter,
				       u8 doneflag)
{
	struct sdio_mmc_card *card = adapter->card;
	int ret, tries;
	u8 ctrl_data = 0;

	sdio_writeb(card->func, card->reg->fw_dump_host_ready,
		    card->reg->fw_dump_ctrl, &ret);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "SDIO Write ERR\n");
		return RDWR_STATUS_FAILURE;
	}
	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		ctrl_data = sdio_readb(card->func, card->reg->fw_dump_ctrl,
				       &ret);
		if (ret) {
			mwifiex_dbg(adapter, ERROR, "SDIO read err\n");
			return RDWR_STATUS_FAILURE;
		}
		if (ctrl_data == FW_DUMP_DONE)
			break;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != card->reg->fw_dump_host_ready) {
			mwifiex_dbg(adapter, WARN,
				    "The ctrl reg was changed, re-try again\n");
			sdio_writeb(card->func, card->reg->fw_dump_host_ready,
				    card->reg->fw_dump_ctrl, &ret);
			if (ret) {
				mwifiex_dbg(adapter, ERROR, "SDIO write err\n");
				return RDWR_STATUS_FAILURE;
			}
		}
		usleep_range(100, 200);
	}
	if (ctrl_data == card->reg->fw_dump_host_ready) {
		mwifiex_dbg(adapter, ERROR,
			    "Fail to pull ctrl_data\n");
		return RDWR_STATUS_FAILURE;
	}

	return RDWR_STATUS_SUCCESS;
}

/* This function dump firmware memory to file */
static void mwifiex_sdio_fw_dump(struct mwifiex_adapter *adapter)
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

	mwifiex_pm_wakeup_card(adapter);
	sdio_claim_host(card->func);

	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump start ==\n");

	stat = mwifiex_sdio_rdwr_firmware(adapter, doneflag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg = card->reg->fw_dump_start;
	/* Read the number of the memories which will dump */
	dump_num = sdio_readb(card->func, reg, &ret);
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "SDIO read memory length err\n");
		goto done;
	}

	/* Read the length of every memory which will dump */
	for (idx = 0; idx < dump_num; idx++) {
		struct memory_type_mapping *entry = &mem_type_mapping_tbl[idx];

		stat = mwifiex_sdio_rdwr_firmware(adapter, doneflag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;

		memory_size = 0;
		reg = card->reg->fw_dump_start;
		for (i = 0; i < 4; i++) {
			read_reg = sdio_readb(card->func, reg, &ret);
			if (ret) {
				mwifiex_dbg(adapter, ERROR, "SDIO read err\n");
				goto done;
			}
			memory_size |= (read_reg << i*8);
			reg++;
		}

		if (memory_size == 0) {
			mwifiex_dbg(adapter, DUMP, "Firmware dump Finished!\n");
			ret = mwifiex_write_reg(adapter,
						card->reg->fw_dump_ctrl,
						FW_DUMP_READ_DONE);
			if (ret) {
				mwifiex_dbg(adapter, ERROR, "SDIO write err\n");
				return;
			}
			break;
		}

		mwifiex_dbg(adapter, DUMP,
			    "%s_SIZE=0x%x\n", entry->mem_name, memory_size);
		entry->mem_ptr = vmalloc(memory_size + 1);
		entry->mem_size = memory_size;
		if (!entry->mem_ptr) {
			mwifiex_dbg(adapter, ERROR, "Vmalloc %s failed\n",
				    entry->mem_name);
			goto done;
		}
		dbg_ptr = entry->mem_ptr;
		end_ptr = dbg_ptr + memory_size;

		doneflag = entry->done_flag;
		mwifiex_dbg(adapter, DUMP,
			    "Start %s output, please wait...\n",
			    entry->mem_name);

		do {
			stat = mwifiex_sdio_rdwr_firmware(adapter, doneflag);
			if (stat == RDWR_STATUS_FAILURE)
				goto done;

			reg_start = card->reg->fw_dump_start;
			reg_end = card->reg->fw_dump_end;
			for (reg = reg_start; reg <= reg_end; reg++) {
				*dbg_ptr = sdio_readb(card->func, reg, &ret);
				if (ret) {
					mwifiex_dbg(adapter, ERROR,
						    "SDIO read err\n");
					goto done;
				}
				if (dbg_ptr < end_ptr)
					dbg_ptr++;
				else
					mwifiex_dbg(adapter, ERROR,
						    "Allocated buf not enough\n");
			}

			if (stat != RDWR_STATUS_DONE)
				continue;

			mwifiex_dbg(adapter, DUMP, "%s done: size=0x%tx\n",
				    entry->mem_name, dbg_ptr - entry->mem_ptr);
			break;
		} while (1);
	}
	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump end ==\n");

done:
	sdio_release_host(card->func);
}

static void mwifiex_sdio_generic_fw_dump(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;
	struct memory_type_mapping *entry = &generic_mem_type_map[0];
	unsigned int reg, reg_start, reg_end;
	u8 start_flag = 0, done_flag = 0;
	u8 *dbg_ptr, *end_ptr;
	enum rdwr_status stat;
	int ret = -1, tries;

	if (!card->fw_dump_enh)
		return;

	if (entry->mem_ptr) {
		vfree(entry->mem_ptr);
		entry->mem_ptr = NULL;
	}
	entry->mem_size = 0;

	mwifiex_pm_wakeup_card(adapter);
	sdio_claim_host(card->func);

	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump start ==\n");

	stat = mwifiex_sdio_rdwr_firmware(adapter, done_flag);
	if (stat == RDWR_STATUS_FAILURE)
		goto done;

	reg_start = card->reg->fw_dump_start;
	reg_end = card->reg->fw_dump_end;
	for (reg = reg_start; reg <= reg_end; reg++) {
		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			start_flag = sdio_readb(card->func, reg, &ret);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "SDIO read err\n");
				goto done;
			}
			if (start_flag == 0)
				break;
			if (tries == MAX_POLL_TRIES) {
				mwifiex_dbg(adapter, ERROR,
					    "FW not ready to dump\n");
				ret = -1;
				goto done;
			}
		}
		usleep_range(100, 200);
	}

	entry->mem_ptr = vmalloc(0xf0000 + 1);
	if (!entry->mem_ptr) {
		ret = -1;
		goto done;
	}
	dbg_ptr = entry->mem_ptr;
	entry->mem_size = 0xf0000;
	end_ptr = dbg_ptr + entry->mem_size;

	done_flag = entry->done_flag;
	mwifiex_dbg(adapter, DUMP,
		    "Start %s output, please wait...\n", entry->mem_name);

	while (true) {
		stat = mwifiex_sdio_rdwr_firmware(adapter, done_flag);
		if (stat == RDWR_STATUS_FAILURE)
			goto done;
		for (reg = reg_start; reg <= reg_end; reg++) {
			*dbg_ptr = sdio_readb(card->func, reg, &ret);
			if (ret) {
				mwifiex_dbg(adapter, ERROR,
					    "SDIO read err\n");
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
			mwifiex_dbg(adapter, DUMP, "dump %s done size=0x%x\n",
				    entry->mem_name, entry->mem_size);
			ret = 0;
			break;
		}
	}
	mwifiex_dbg(adapter, MSG, "== mwifiex firmware dump end ==\n");

done:
	if (ret) {
		mwifiex_dbg(adapter, ERROR, "firmware dump failed\n");
		if (entry->mem_ptr) {
			vfree(entry->mem_ptr);
			entry->mem_ptr = NULL;
		}
		entry->mem_size = 0;
	}
	sdio_release_host(card->func);
}

static void mwifiex_sdio_device_dump_work(struct mwifiex_adapter *adapter)
{
	struct sdio_mmc_card *card = adapter->card;

	mwifiex_drv_info_dump(adapter);
	if (card->fw_dump_enh)
		mwifiex_sdio_generic_fw_dump(adapter);
	else
		mwifiex_sdio_fw_dump(adapter);
	mwifiex_upload_device_dump(adapter);
}

static void mwifiex_sdio_work(struct work_struct *work)
{
	if (test_and_clear_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP,
			       &iface_work_flags))
		mwifiex_sdio_device_dump_work(save_adapter);
	if (test_and_clear_bit(MWIFIEX_IFACE_WORK_CARD_RESET,
			       &iface_work_flags))
		mwifiex_sdio_card_reset_work(save_adapter);
}

static DECLARE_WORK(sdio_work, mwifiex_sdio_work);
/* This function resets the card */
static void mwifiex_sdio_card_reset(struct mwifiex_adapter *adapter)
{
	save_adapter = adapter;
	if (test_bit(MWIFIEX_IFACE_WORK_CARD_RESET, &iface_work_flags))
		return;

	set_bit(MWIFIEX_IFACE_WORK_CARD_RESET, &iface_work_flags);

	schedule_work(&sdio_work);
}

/* This function dumps FW information */
static void mwifiex_sdio_device_dump(struct mwifiex_adapter *adapter)
{
	save_adapter = adapter;
	if (test_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP, &iface_work_flags))
		return;

	set_bit(MWIFIEX_IFACE_WORK_DEVICE_DUMP, &iface_work_flags);
	schedule_work(&sdio_work);
}

/* Function to dump SDIO function registers and SDIO scratch registers in case
 * of FW crash
 */
static int
mwifiex_sdio_reg_dump(struct mwifiex_adapter *adapter, char *drv_buf)
{
	char *p = drv_buf;
	struct sdio_mmc_card *cardp = adapter->card;
	int ret = 0;
	u8 count, func, data, index = 0, size = 0;
	u8 reg, reg_start, reg_end;
	char buf[256], *ptr;

	if (!p)
		return 0;

	mwifiex_dbg(adapter, MSG, "SDIO register dump start\n");

	mwifiex_pm_wakeup_card(adapter);

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
			reg_end = cardp->reg->func1_spec_reg_table[size-1];
			break;
		default:
			/* Read the scratch registers of SDIO function1 */
			if (count == 4)
				mdelay(100);
			func = 1;
			reg_start = cardp->reg->func1_scratch_reg;
			reg_end = reg_start + MWIFIEX_SDIO_SCRATCH_SIZE;
		}

		if (count != 2)
			ptr += sprintf(ptr, "SDIO Func%d (%#x-%#x): ",
				       func, reg_start, reg_end);
		else
			ptr += sprintf(ptr, "SDIO Func%d: ", func);

		for (reg = reg_start; reg <= reg_end;) {
			if (func == 0)
				data = sdio_f0_readb(cardp->func, reg, &ret);
			else
				data = sdio_readb(cardp->func, reg, &ret);

			if (count == 2)
				ptr += sprintf(ptr, "(%#x) ", reg);
			if (!ret) {
				ptr += sprintf(ptr, "%02x ", data);
			} else {
				ptr += sprintf(ptr, "ERR");
				break;
			}

			if (count == 2 && reg < reg_end)
				reg = cardp->reg->func1_spec_reg_table[index++];
			else
				reg++;
		}

		mwifiex_dbg(adapter, MSG, "%s\n", buf);
		p += sprintf(p, "%s\n", buf);
	}

	sdio_release_host(cardp->func);

	mwifiex_dbg(adapter, MSG, "SDIO register dump end\n");

	return p - drv_buf;
}

static struct mwifiex_if_ops sdio_ops = {
	.init_if = mwifiex_init_sdio,
	.cleanup_if = mwifiex_cleanup_sdio,
	.check_fw_status = mwifiex_check_fw_status,
	.check_winner_status = mwifiex_check_winner_status,
	.prog_fw = mwifiex_prog_fw_w_helper,
	.register_dev = mwifiex_register_dev,
	.unregister_dev = mwifiex_unregister_dev,
	.enable_int = mwifiex_sdio_enable_host_int,
	.disable_int = mwifiex_sdio_disable_host_int,
	.process_int_status = mwifiex_process_int_status,
	.host_to_card = mwifiex_sdio_host_to_card,
	.wakeup = mwifiex_pm_wakeup_card,
	.wakeup_complete = mwifiex_pm_wakeup_card_complete,

	/* SDIO specific */
	.update_mp_end_port = mwifiex_update_mp_end_port,
	.cleanup_mpa_buf = mwifiex_cleanup_mpa_buf,
	.cmdrsp_complete = mwifiex_sdio_cmdrsp_complete,
	.event_complete = mwifiex_sdio_event_complete,
	.card_reset = mwifiex_sdio_card_reset,
	.reg_dump = mwifiex_sdio_reg_dump,
	.device_dump = mwifiex_sdio_device_dump,
	.deaggr_pkt = mwifiex_deaggr_sdio_pkt,
};

/*
 * This function initializes the SDIO driver.
 *
 * This initiates the semaphore and registers the device with
 * SDIO bus.
 */
static int
mwifiex_sdio_init_module(void)
{
	sema_init(&add_remove_card_sem, 1);

	/* Clear the flag in case user removes the card. */
	user_rmmod = 0;

	return sdio_register_driver(&mwifiex_sdio);
}

/*
 * This function cleans up the SDIO driver.
 *
 * The following major steps are followed for cleanup -
 *      - Resume the device if its suspended
 *      - Disconnect the device if connected
 *      - Shutdown the firmware
 *      - Unregister the device from SDIO bus.
 */
static void
mwifiex_sdio_cleanup_module(void)
{
	if (!down_interruptible(&add_remove_card_sem))
		up(&add_remove_card_sem);

	/* Set the flag as user is removing this module. */
	user_rmmod = 1;
	cancel_work_sync(&sdio_work);

	sdio_unregister_driver(&mwifiex_sdio);
}

module_init(mwifiex_sdio_init_module);
module_exit(mwifiex_sdio_cleanup_module);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION("Marvell WiFi-Ex SDIO Driver version " SDIO_VERSION);
MODULE_VERSION(SDIO_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(SD8786_DEFAULT_FW_NAME);
MODULE_FIRMWARE(SD8787_DEFAULT_FW_NAME);
MODULE_FIRMWARE(SD8797_DEFAULT_FW_NAME);
MODULE_FIRMWARE(SD8897_DEFAULT_FW_NAME);
MODULE_FIRMWARE(SD8887_DEFAULT_FW_NAME);
MODULE_FIRMWARE(SD8997_DEFAULT_FW_NAME);

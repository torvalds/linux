/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/lockdep.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <net/mac80211.h>

#include "common.h"

int
_il_poll_bit(struct il_priv *il, u32 addr, u32 bits, u32 mask, int timeout)
{
	const int interval = 10; /* microseconds */
	int t = 0;

	do {
		if ((_il_rd(il, addr) & mask) == (bits & mask))
			return t;
		udelay(interval);
		t += interval;
	} while (t < timeout);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL(_il_poll_bit);

void
il_set_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_set_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
EXPORT_SYMBOL(il_set_bit);

void
il_clear_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_clear_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
EXPORT_SYMBOL(il_clear_bit);

bool
_il_grab_nic_access(struct il_priv *il)
{
	int ret;
	u32 val;

	/* this bit wakes up the NIC */
	_il_set_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/*
	 * These bits say the device is running, and should keep running for
	 * at least a short while (at least as long as MAC_ACCESS_REQ stays 1),
	 * but they do not indicate that embedded SRAM is restored yet;
	 * 3945 and 4965 have volatile SRAM, and must save/restore contents
	 * to/from host DRAM when sleeping/waking for power-saving.
	 * Each direction takes approximately 1/4 millisecond; with this
	 * overhead, it's a good idea to grab and hold MAC_ACCESS_REQUEST if a
	 * series of register accesses are expected (e.g. reading Event Log),
	 * to keep device from sleeping.
	 *
	 * CSR_UCODE_DRV_GP1 register bit MAC_SLEEP == 0 indicates that
	 * SRAM is okay/restored.  We don't check that here because this call
	 * is just for hardware register access; but GP1 MAC_SLEEP check is a
	 * good idea before accessing 3945/4965 SRAM (e.g. reading Event Log).
	 *
	 */
	ret =
	    _il_poll_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			 (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			  CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (unlikely(ret < 0)) {
		val = _il_rd(il, CSR_GP_CNTRL);
		WARN_ONCE(1, "Timeout waiting for ucode processor access "
			     "(CSR_GP_CNTRL 0x%08x)\n", val);
		_il_wr(il, CSR_RESET, CSR_RESET_REG_FLAG_FORCE_NMI);
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(_il_grab_nic_access);

int
il_poll_bit(struct il_priv *il, u32 addr, u32 mask, int timeout)
{
	const int interval = 10; /* microseconds */
	int t = 0;

	do {
		if ((il_rd(il, addr) & mask) == mask)
			return t;
		udelay(interval);
		t += interval;
	} while (t < timeout);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL(il_poll_bit);

u32
il_rd_prph(struct il_priv *il, u32 reg)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	_il_grab_nic_access(il);
	val = _il_rd_prph(il, reg);
	_il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return val;
}
EXPORT_SYMBOL(il_rd_prph);

void
il_wr_prph(struct il_priv *il, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		_il_wr_prph(il, addr, val);
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}
EXPORT_SYMBOL(il_wr_prph);

u32
il_read_targ_mem(struct il_priv *il, u32 addr)
{
	unsigned long reg_flags;
	u32 value;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	_il_grab_nic_access(il);

	_il_wr(il, HBUS_TARG_MEM_RADDR, addr);
	value = _il_rd(il, HBUS_TARG_MEM_RDAT);

	_il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return value;
}
EXPORT_SYMBOL(il_read_targ_mem);

void
il_write_targ_mem(struct il_priv *il, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (likely(_il_grab_nic_access(il))) {
		_il_wr(il, HBUS_TARG_MEM_WADDR, addr);
		_il_wr(il, HBUS_TARG_MEM_WDAT, val);
		_il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}
EXPORT_SYMBOL(il_write_targ_mem);

const char *
il_get_cmd_string(u8 cmd)
{
	switch (cmd) {
		IL_CMD(N_ALIVE);
		IL_CMD(N_ERROR);
		IL_CMD(C_RXON);
		IL_CMD(C_RXON_ASSOC);
		IL_CMD(C_QOS_PARAM);
		IL_CMD(C_RXON_TIMING);
		IL_CMD(C_ADD_STA);
		IL_CMD(C_REM_STA);
		IL_CMD(C_WEPKEY);
		IL_CMD(N_3945_RX);
		IL_CMD(C_TX);
		IL_CMD(C_RATE_SCALE);
		IL_CMD(C_LEDS);
		IL_CMD(C_TX_LINK_QUALITY_CMD);
		IL_CMD(C_CHANNEL_SWITCH);
		IL_CMD(N_CHANNEL_SWITCH);
		IL_CMD(C_SPECTRUM_MEASUREMENT);
		IL_CMD(N_SPECTRUM_MEASUREMENT);
		IL_CMD(C_POWER_TBL);
		IL_CMD(N_PM_SLEEP);
		IL_CMD(N_PM_DEBUG_STATS);
		IL_CMD(C_SCAN);
		IL_CMD(C_SCAN_ABORT);
		IL_CMD(N_SCAN_START);
		IL_CMD(N_SCAN_RESULTS);
		IL_CMD(N_SCAN_COMPLETE);
		IL_CMD(N_BEACON);
		IL_CMD(C_TX_BEACON);
		IL_CMD(C_TX_PWR_TBL);
		IL_CMD(C_BT_CONFIG);
		IL_CMD(C_STATS);
		IL_CMD(N_STATS);
		IL_CMD(N_CARD_STATE);
		IL_CMD(N_MISSED_BEACONS);
		IL_CMD(C_CT_KILL_CONFIG);
		IL_CMD(C_SENSITIVITY);
		IL_CMD(C_PHY_CALIBRATION);
		IL_CMD(N_RX_PHY);
		IL_CMD(N_RX_MPDU);
		IL_CMD(N_RX);
		IL_CMD(N_COMPRESSED_BA);
	default:
		return "UNKNOWN";

	}
}
EXPORT_SYMBOL(il_get_cmd_string);

#define HOST_COMPLETE_TIMEOUT (HZ / 2)

static void
il_generic_cmd_callback(struct il_priv *il, struct il_device_cmd *cmd,
			struct il_rx_pkt *pkt)
{
	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR("Bad return from %s (0x%08X)\n",
		       il_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		return;
	}
#ifdef CONFIG_IWLEGACY_DEBUG
	switch (cmd->hdr.cmd) {
	case C_TX_LINK_QUALITY_CMD:
	case C_SENSITIVITY:
		D_HC_DUMP("back from %s (0x%08X)\n",
			  il_get_cmd_string(cmd->hdr.cmd), pkt->hdr.flags);
		break;
	default:
		D_HC("back from %s (0x%08X)\n", il_get_cmd_string(cmd->hdr.cmd),
		     pkt->hdr.flags);
	}
#endif
}

static int
il_send_cmd_async(struct il_priv *il, struct il_host_cmd *cmd)
{
	int ret;

	BUG_ON(!(cmd->flags & CMD_ASYNC));

	/* An asynchronous command can not expect an SKB to be set. */
	BUG_ON(cmd->flags & CMD_WANT_SKB);

	/* Assign a generic callback if one is not provided */
	if (!cmd->callback)
		cmd->callback = il_generic_cmd_callback;

	if (test_bit(S_EXIT_PENDING, &il->status))
		return -EBUSY;

	ret = il_enqueue_hcmd(il, cmd);
	if (ret < 0) {
		IL_ERR("Error sending %s: enqueue_hcmd failed: %d\n",
		       il_get_cmd_string(cmd->id), ret);
		return ret;
	}
	return 0;
}

int
il_send_cmd_sync(struct il_priv *il, struct il_host_cmd *cmd)
{
	int cmd_idx;
	int ret;

	lockdep_assert_held(&il->mutex);

	BUG_ON(cmd->flags & CMD_ASYNC);

	/* A synchronous command can not have a callback set. */
	BUG_ON(cmd->callback);

	D_INFO("Attempting to send sync command %s\n",
	       il_get_cmd_string(cmd->id));

	set_bit(S_HCMD_ACTIVE, &il->status);
	D_INFO("Setting HCMD_ACTIVE for command %s\n",
	       il_get_cmd_string(cmd->id));

	cmd_idx = il_enqueue_hcmd(il, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		IL_ERR("Error sending %s: enqueue_hcmd failed: %d\n",
		       il_get_cmd_string(cmd->id), ret);
		goto out;
	}

	ret = wait_event_timeout(il->wait_command_queue,
				 !test_bit(S_HCMD_ACTIVE, &il->status),
				 HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		if (test_bit(S_HCMD_ACTIVE, &il->status)) {
			IL_ERR("Error sending %s: time out after %dms.\n",
			       il_get_cmd_string(cmd->id),
			       jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

			clear_bit(S_HCMD_ACTIVE, &il->status);
			D_INFO("Clearing HCMD_ACTIVE for command %s\n",
			       il_get_cmd_string(cmd->id));
			ret = -ETIMEDOUT;
			goto cancel;
		}
	}

	if (test_bit(S_RFKILL, &il->status)) {
		IL_ERR("Command %s aborted: RF KILL Switch\n",
		       il_get_cmd_string(cmd->id));
		ret = -ECANCELED;
		goto fail;
	}
	if (test_bit(S_FW_ERROR, &il->status)) {
		IL_ERR("Command %s failed: FW Error\n",
		       il_get_cmd_string(cmd->id));
		ret = -EIO;
		goto fail;
	}
	if ((cmd->flags & CMD_WANT_SKB) && !cmd->reply_page) {
		IL_ERR("Error: Response NULL in '%s'\n",
		       il_get_cmd_string(cmd->id));
		ret = -EIO;
		goto cancel;
	}

	ret = 0;
	goto out;

cancel:
	if (cmd->flags & CMD_WANT_SKB) {
		/*
		 * Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source).
		 */
		il->txq[il->cmd_queue].meta[cmd_idx].flags &= ~CMD_WANT_SKB;
	}
fail:
	if (cmd->reply_page) {
		il_free_pages(il, cmd->reply_page);
		cmd->reply_page = 0;
	}
out:
	return ret;
}
EXPORT_SYMBOL(il_send_cmd_sync);

int
il_send_cmd(struct il_priv *il, struct il_host_cmd *cmd)
{
	if (cmd->flags & CMD_ASYNC)
		return il_send_cmd_async(il, cmd);

	return il_send_cmd_sync(il, cmd);
}
EXPORT_SYMBOL(il_send_cmd);

int
il_send_cmd_pdu(struct il_priv *il, u8 id, u16 len, const void *data)
{
	struct il_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	return il_send_cmd_sync(il, &cmd);
}
EXPORT_SYMBOL(il_send_cmd_pdu);

int
il_send_cmd_pdu_async(struct il_priv *il, u8 id, u16 len, const void *data,
		      void (*callback) (struct il_priv *il,
					struct il_device_cmd *cmd,
					struct il_rx_pkt *pkt))
{
	struct il_host_cmd cmd = {
		.id = id,
		.len = len,
		.data = data,
	};

	cmd.flags |= CMD_ASYNC;
	cmd.callback = callback;

	return il_send_cmd_async(il, &cmd);
}
EXPORT_SYMBOL(il_send_cmd_pdu_async);

/* default: IL_LED_BLINK(0) using blinking idx table */
static int led_mode;
module_param(led_mode, int, S_IRUGO);
MODULE_PARM_DESC(led_mode,
		 "0=system default, " "1=On(RF On)/Off(RF Off), 2=blinking");

/* Throughput		OFF time(ms)	ON time (ms)
 *	>300			25		25
 *	>200 to 300		40		40
 *	>100 to 200		55		55
 *	>70 to 100		65		65
 *	>50 to 70		75		75
 *	>20 to 50		85		85
 *	>10 to 20		95		95
 *	>5 to 10		110		110
 *	>1 to 5			130		130
 *	>0 to 1			167		167
 *	<=0					SOLID ON
 */
static const struct ieee80211_tpt_blink il_blink[] = {
	{.throughput = 0,		.blink_time = 334},
	{.throughput = 1 * 1024 - 1,	.blink_time = 260},
	{.throughput = 5 * 1024 - 1,	.blink_time = 220},
	{.throughput = 10 * 1024 - 1,	.blink_time = 190},
	{.throughput = 20 * 1024 - 1,	.blink_time = 170},
	{.throughput = 50 * 1024 - 1,	.blink_time = 150},
	{.throughput = 70 * 1024 - 1,	.blink_time = 130},
	{.throughput = 100 * 1024 - 1,	.blink_time = 110},
	{.throughput = 200 * 1024 - 1,	.blink_time = 80},
	{.throughput = 300 * 1024 - 1,	.blink_time = 50},
};

/*
 * Adjust led blink rate to compensate on a MAC Clock difference on every HW
 * Led blink rate analysis showed an average deviation of 0% on 3945,
 * 5% on 4965 HW.
 * Need to compensate on the led on/off time per HW according to the deviation
 * to achieve the desired led frequency
 * The calculation is: (100-averageDeviation)/100 * blinkTime
 * For code efficiency the calculation will be:
 *     compensation = (100 - averageDeviation) * 64 / 100
 *     NewBlinkTime = (compensation * BlinkTime) / 64
 */
static inline u8
il_blink_compensation(struct il_priv *il, u8 time, u16 compensation)
{
	if (!compensation) {
		IL_ERR("undefined blink compensation: "
		       "use pre-defined blinking time\n");
		return time;
	}

	return (u8) ((time * compensation) >> 6);
}

/* Set led pattern command */
static int
il_led_cmd(struct il_priv *il, unsigned long on, unsigned long off)
{
	struct il_led_cmd led_cmd = {
		.id = IL_LED_LINK,
		.interval = IL_DEF_LED_INTRVL
	};
	int ret;

	if (!test_bit(S_READY, &il->status))
		return -EBUSY;

	if (il->blink_on == on && il->blink_off == off)
		return 0;

	if (off == 0) {
		/* led is SOLID_ON */
		on = IL_LED_SOLID;
	}

	D_LED("Led blink time compensation=%u\n",
	      il->cfg->led_compensation);
	led_cmd.on =
	    il_blink_compensation(il, on,
				  il->cfg->led_compensation);
	led_cmd.off =
	    il_blink_compensation(il, off,
				  il->cfg->led_compensation);

	ret = il->ops->send_led_cmd(il, &led_cmd);
	if (!ret) {
		il->blink_on = on;
		il->blink_off = off;
	}
	return ret;
}

static void
il_led_brightness_set(struct led_classdev *led_cdev,
		      enum led_brightness brightness)
{
	struct il_priv *il = container_of(led_cdev, struct il_priv, led);
	unsigned long on = 0;

	if (brightness > 0)
		on = IL_LED_SOLID;

	il_led_cmd(il, on, 0);
}

static int
il_led_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on,
		 unsigned long *delay_off)
{
	struct il_priv *il = container_of(led_cdev, struct il_priv, led);

	return il_led_cmd(il, *delay_on, *delay_off);
}

void
il_leds_init(struct il_priv *il)
{
	int mode = led_mode;
	int ret;

	if (mode == IL_LED_DEFAULT)
		mode = il->cfg->led_mode;

	il->led.name =
	    kasprintf(GFP_KERNEL, "%s-led", wiphy_name(il->hw->wiphy));
	il->led.brightness_set = il_led_brightness_set;
	il->led.blink_set = il_led_blink_set;
	il->led.max_brightness = 1;

	switch (mode) {
	case IL_LED_DEFAULT:
		WARN_ON(1);
		break;
	case IL_LED_BLINK:
		il->led.default_trigger =
		    ieee80211_create_tpt_led_trigger(il->hw,
						     IEEE80211_TPT_LEDTRIG_FL_CONNECTED,
						     il_blink,
						     ARRAY_SIZE(il_blink));
		break;
	case IL_LED_RF_STATE:
		il->led.default_trigger = ieee80211_get_radio_led_name(il->hw);
		break;
	}

	ret = led_classdev_register(&il->pci_dev->dev, &il->led);
	if (ret) {
		kfree(il->led.name);
		return;
	}

	il->led_registered = true;
}
EXPORT_SYMBOL(il_leds_init);

void
il_leds_exit(struct il_priv *il)
{
	if (!il->led_registered)
		return;

	led_classdev_unregister(&il->led);
	kfree(il->led.name);
}
EXPORT_SYMBOL(il_leds_exit);

/************************** EEPROM BANDS ****************************
 *
 * The il_eeprom_band definitions below provide the mapping from the
 * EEPROM contents to the specific channel number supported for each
 * band.
 *
 * For example, il_priv->eeprom.band_3_channels[4] from the band_3
 * definition below maps to physical channel 42 in the 5.2GHz spectrum.
 * The specific geography and calibration information for that channel
 * is contained in the eeprom map itself.
 *
 * During init, we copy the eeprom information and channel map
 * information into il->channel_info_24/52 and il->channel_map_24/52
 *
 * channel_map_24/52 provides the idx in the channel_info array for a
 * given channel.  We have to have two separate maps as there is channel
 * overlap with the 2.4GHz and 5.2GHz spectrum as seen in band_1 and
 * band_2
 *
 * A value of 0xff stored in the channel_map indicates that the channel
 * is not supported by the hardware at all.
 *
 * A value of 0xfe in the channel_map indicates that the channel is not
 * valid for Tx with the current hardware.  This means that
 * while the system can tune and receive on a given channel, it may not
 * be able to associate or transmit any frames on that
 * channel.  There is no corresponding channel information for that
 * entry.
 *
 *********************************************************************/

/* 2.4 GHz */
const u8 il_eeprom_band_1[14] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

/* 5.2 GHz bands */
static const u8 il_eeprom_band_2[] = {	/* 4915-5080MHz */
	183, 184, 185, 187, 188, 189, 192, 196, 7, 8, 11, 12, 16
};

static const u8 il_eeprom_band_3[] = {	/* 5170-5320MHz */
	34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
};

static const u8 il_eeprom_band_4[] = {	/* 5500-5700MHz */
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
};

static const u8 il_eeprom_band_5[] = {	/* 5725-5825MHz */
	145, 149, 153, 157, 161, 165
};

static const u8 il_eeprom_band_6[] = {	/* 2.4 ht40 channel */
	1, 2, 3, 4, 5, 6, 7
};

static const u8 il_eeprom_band_7[] = {	/* 5.2 ht40 channel */
	36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157
};

/******************************************************************************
 *
 * EEPROM related functions
 *
******************************************************************************/

static int
il_eeprom_verify_signature(struct il_priv *il)
{
	u32 gp = _il_rd(il, CSR_EEPROM_GP) & CSR_EEPROM_GP_VALID_MSK;
	int ret = 0;

	D_EEPROM("EEPROM signature=0x%08x\n", gp);
	switch (gp) {
	case CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K:
	case CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K:
		break;
	default:
		IL_ERR("bad EEPROM signature," "EEPROM_GP=0x%08x\n", gp);
		ret = -ENOENT;
		break;
	}
	return ret;
}

const u8 *
il_eeprom_query_addr(const struct il_priv *il, size_t offset)
{
	BUG_ON(offset >= il->cfg->eeprom_size);
	return &il->eeprom[offset];
}
EXPORT_SYMBOL(il_eeprom_query_addr);

u16
il_eeprom_query16(const struct il_priv *il, size_t offset)
{
	if (!il->eeprom)
		return 0;
	return (u16) il->eeprom[offset] | ((u16) il->eeprom[offset + 1] << 8);
}
EXPORT_SYMBOL(il_eeprom_query16);

/**
 * il_eeprom_init - read EEPROM contents
 *
 * Load the EEPROM contents from adapter into il->eeprom
 *
 * NOTE:  This routine uses the non-debug IO access functions.
 */
int
il_eeprom_init(struct il_priv *il)
{
	__le16 *e;
	u32 gp = _il_rd(il, CSR_EEPROM_GP);
	int sz;
	int ret;
	u16 addr;

	/* allocate eeprom */
	sz = il->cfg->eeprom_size;
	D_EEPROM("NVM size = %d\n", sz);
	il->eeprom = kzalloc(sz, GFP_KERNEL);
	if (!il->eeprom) {
		ret = -ENOMEM;
		goto alloc_err;
	}
	e = (__le16 *) il->eeprom;

	il->ops->apm_init(il);

	ret = il_eeprom_verify_signature(il);
	if (ret < 0) {
		IL_ERR("EEPROM not found, EEPROM_GP=0x%08x\n", gp);
		ret = -ENOENT;
		goto err;
	}

	/* Make sure driver (instead of uCode) is allowed to read EEPROM */
	ret = il->ops->eeprom_acquire_semaphore(il);
	if (ret < 0) {
		IL_ERR("Failed to acquire EEPROM semaphore.\n");
		ret = -ENOENT;
		goto err;
	}

	/* eeprom is an array of 16bit values */
	for (addr = 0; addr < sz; addr += sizeof(u16)) {
		u32 r;

		_il_wr(il, CSR_EEPROM_REG,
		       CSR_EEPROM_REG_MSK_ADDR & (addr << 1));

		ret =
		    _il_poll_bit(il, CSR_EEPROM_REG,
				 CSR_EEPROM_REG_READ_VALID_MSK,
				 CSR_EEPROM_REG_READ_VALID_MSK,
				 IL_EEPROM_ACCESS_TIMEOUT);
		if (ret < 0) {
			IL_ERR("Time out reading EEPROM[%d]\n", addr);
			goto done;
		}
		r = _il_rd(il, CSR_EEPROM_REG);
		e[addr / 2] = cpu_to_le16(r >> 16);
	}

	D_EEPROM("NVM Type: %s, version: 0x%x\n", "EEPROM",
		 il_eeprom_query16(il, EEPROM_VERSION));

	ret = 0;
done:
	il->ops->eeprom_release_semaphore(il);

err:
	if (ret)
		il_eeprom_free(il);
	/* Reset chip to save power until we load uCode during "up". */
	il_apm_stop(il);
alloc_err:
	return ret;
}
EXPORT_SYMBOL(il_eeprom_init);

void
il_eeprom_free(struct il_priv *il)
{
	kfree(il->eeprom);
	il->eeprom = NULL;
}
EXPORT_SYMBOL(il_eeprom_free);

static void
il_init_band_reference(const struct il_priv *il, int eep_band,
		       int *eeprom_ch_count,
		       const struct il_eeprom_channel **eeprom_ch_info,
		       const u8 **eeprom_ch_idx)
{
	u32 offset = il->cfg->regulatory_bands[eep_band - 1];

	switch (eep_band) {
	case 1:		/* 2.4GHz band */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_1);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_1;
		break;
	case 2:		/* 4.9GHz band */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_2);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_2;
		break;
	case 3:		/* 5.2GHz band */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_3);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_3;
		break;
	case 4:		/* 5.5GHz band */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_4);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_4;
		break;
	case 5:		/* 5.7GHz band */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_5);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_5;
		break;
	case 6:		/* 2.4GHz ht40 channels */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_6);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_6;
		break;
	case 7:		/* 5 GHz ht40 channels */
		*eeprom_ch_count = ARRAY_SIZE(il_eeprom_band_7);
		*eeprom_ch_info =
		    (struct il_eeprom_channel *)il_eeprom_query_addr(il,
								     offset);
		*eeprom_ch_idx = il_eeprom_band_7;
		break;
	default:
		BUG();
	}
}

#define CHECK_AND_PRINT(x) ((eeprom_ch->flags & EEPROM_CHANNEL_##x) \
			    ? # x " " : "")
/**
 * il_mod_ht40_chan_info - Copy ht40 channel info into driver's il.
 *
 * Does not set up a command, or touch hardware.
 */
static int
il_mod_ht40_chan_info(struct il_priv *il, enum ieee80211_band band, u16 channel,
		      const struct il_eeprom_channel *eeprom_ch,
		      u8 clear_ht40_extension_channel)
{
	struct il_channel_info *ch_info;

	ch_info =
	    (struct il_channel_info *)il_get_channel_info(il, band, channel);

	if (!il_is_channel_valid(ch_info))
		return -1;

	D_EEPROM("HT40 Ch. %d [%sGHz] %s%s%s%s%s(0x%02x %ddBm):"
		 " Ad-Hoc %ssupported\n", ch_info->channel,
		 il_is_channel_a_band(ch_info) ? "5.2" : "2.4",
		 CHECK_AND_PRINT(IBSS), CHECK_AND_PRINT(ACTIVE),
		 CHECK_AND_PRINT(RADAR), CHECK_AND_PRINT(WIDE),
		 CHECK_AND_PRINT(DFS), eeprom_ch->flags,
		 eeprom_ch->max_power_avg,
		 ((eeprom_ch->flags & EEPROM_CHANNEL_IBSS) &&
		  !(eeprom_ch->flags & EEPROM_CHANNEL_RADAR)) ? "" : "not ");

	ch_info->ht40_eeprom = *eeprom_ch;
	ch_info->ht40_max_power_avg = eeprom_ch->max_power_avg;
	ch_info->ht40_flags = eeprom_ch->flags;
	if (eeprom_ch->flags & EEPROM_CHANNEL_VALID)
		ch_info->ht40_extension_channel &=
		    ~clear_ht40_extension_channel;

	return 0;
}

#define CHECK_AND_PRINT_I(x) ((eeprom_ch_info[ch].flags & EEPROM_CHANNEL_##x) \
			    ? # x " " : "")

/**
 * il_init_channel_map - Set up driver's info for all possible channels
 */
int
il_init_channel_map(struct il_priv *il)
{
	int eeprom_ch_count = 0;
	const u8 *eeprom_ch_idx = NULL;
	const struct il_eeprom_channel *eeprom_ch_info = NULL;
	int band, ch;
	struct il_channel_info *ch_info;

	if (il->channel_count) {
		D_EEPROM("Channel map already initialized.\n");
		return 0;
	}

	D_EEPROM("Initializing regulatory info from EEPROM\n");

	il->channel_count =
	    ARRAY_SIZE(il_eeprom_band_1) + ARRAY_SIZE(il_eeprom_band_2) +
	    ARRAY_SIZE(il_eeprom_band_3) + ARRAY_SIZE(il_eeprom_band_4) +
	    ARRAY_SIZE(il_eeprom_band_5);

	D_EEPROM("Parsing data for %d channels.\n", il->channel_count);

	il->channel_info =
	    kzalloc(sizeof(struct il_channel_info) * il->channel_count,
		    GFP_KERNEL);
	if (!il->channel_info) {
		IL_ERR("Could not allocate channel_info\n");
		il->channel_count = 0;
		return -ENOMEM;
	}

	ch_info = il->channel_info;

	/* Loop through the 5 EEPROM bands adding them in order to the
	 * channel map we maintain (that contains additional information than
	 * what just in the EEPROM) */
	for (band = 1; band <= 5; band++) {

		il_init_band_reference(il, band, &eeprom_ch_count,
				       &eeprom_ch_info, &eeprom_ch_idx);

		/* Loop through each band adding each of the channels */
		for (ch = 0; ch < eeprom_ch_count; ch++) {
			ch_info->channel = eeprom_ch_idx[ch];
			ch_info->band =
			    (band ==
			     1) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;

			/* permanently store EEPROM's channel regulatory flags
			 *   and max power in channel info database. */
			ch_info->eeprom = eeprom_ch_info[ch];

			/* Copy the run-time flags so they are there even on
			 * invalid channels */
			ch_info->flags = eeprom_ch_info[ch].flags;
			/* First write that ht40 is not enabled, and then enable
			 * one by one */
			ch_info->ht40_extension_channel =
			    IEEE80211_CHAN_NO_HT40;

			if (!(il_is_channel_valid(ch_info))) {
				D_EEPROM("Ch. %d Flags %x [%sGHz] - "
					 "No traffic\n", ch_info->channel,
					 ch_info->flags,
					 il_is_channel_a_band(ch_info) ? "5.2" :
					 "2.4");
				ch_info++;
				continue;
			}

			/* Initialize regulatory-based run-time data */
			ch_info->max_power_avg = ch_info->curr_txpow =
			    eeprom_ch_info[ch].max_power_avg;
			ch_info->scan_power = eeprom_ch_info[ch].max_power_avg;
			ch_info->min_power = 0;

			D_EEPROM("Ch. %d [%sGHz] " "%s%s%s%s%s%s(0x%02x %ddBm):"
				 " Ad-Hoc %ssupported\n", ch_info->channel,
				 il_is_channel_a_band(ch_info) ? "5.2" : "2.4",
				 CHECK_AND_PRINT_I(VALID),
				 CHECK_AND_PRINT_I(IBSS),
				 CHECK_AND_PRINT_I(ACTIVE),
				 CHECK_AND_PRINT_I(RADAR),
				 CHECK_AND_PRINT_I(WIDE),
				 CHECK_AND_PRINT_I(DFS),
				 eeprom_ch_info[ch].flags,
				 eeprom_ch_info[ch].max_power_avg,
				 ((eeprom_ch_info[ch].
				   flags & EEPROM_CHANNEL_IBSS) &&
				  !(eeprom_ch_info[ch].
				    flags & EEPROM_CHANNEL_RADAR)) ? "" :
				 "not ");

			ch_info++;
		}
	}

	/* Check if we do have HT40 channels */
	if (il->cfg->regulatory_bands[5] == EEPROM_REGULATORY_BAND_NO_HT40 &&
	    il->cfg->regulatory_bands[6] == EEPROM_REGULATORY_BAND_NO_HT40)
		return 0;

	/* Two additional EEPROM bands for 2.4 and 5 GHz HT40 channels */
	for (band = 6; band <= 7; band++) {
		enum ieee80211_band ieeeband;

		il_init_band_reference(il, band, &eeprom_ch_count,
				       &eeprom_ch_info, &eeprom_ch_idx);

		/* EEPROM band 6 is 2.4, band 7 is 5 GHz */
		ieeeband =
		    (band == 6) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;

		/* Loop through each band adding each of the channels */
		for (ch = 0; ch < eeprom_ch_count; ch++) {
			/* Set up driver's info for lower half */
			il_mod_ht40_chan_info(il, ieeeband, eeprom_ch_idx[ch],
					      &eeprom_ch_info[ch],
					      IEEE80211_CHAN_NO_HT40PLUS);

			/* Set up driver's info for upper half */
			il_mod_ht40_chan_info(il, ieeeband,
					      eeprom_ch_idx[ch] + 4,
					      &eeprom_ch_info[ch],
					      IEEE80211_CHAN_NO_HT40MINUS);
		}
	}

	return 0;
}
EXPORT_SYMBOL(il_init_channel_map);

/*
 * il_free_channel_map - undo allocations in il_init_channel_map
 */
void
il_free_channel_map(struct il_priv *il)
{
	kfree(il->channel_info);
	il->channel_count = 0;
}
EXPORT_SYMBOL(il_free_channel_map);

/**
 * il_get_channel_info - Find driver's ilate channel info
 *
 * Based on band and channel number.
 */
const struct il_channel_info *
il_get_channel_info(const struct il_priv *il, enum ieee80211_band band,
		    u16 channel)
{
	int i;

	switch (band) {
	case IEEE80211_BAND_5GHZ:
		for (i = 14; i < il->channel_count; i++) {
			if (il->channel_info[i].channel == channel)
				return &il->channel_info[i];
		}
		break;
	case IEEE80211_BAND_2GHZ:
		if (channel >= 1 && channel <= 14)
			return &il->channel_info[channel - 1];
		break;
	default:
		BUG();
	}

	return NULL;
}
EXPORT_SYMBOL(il_get_channel_info);

/*
 * Setting power level allows the card to go to sleep when not busy.
 *
 * We calculate a sleep command based on the required latency, which
 * we get from mac80211. In order to handle thermal throttling, we can
 * also use pre-defined power levels.
 */

/*
 * This defines the old power levels. They are still used by default
 * (level 1) and for thermal throttle (levels 3 through 5)
 */

struct il_power_vec_entry {
	struct il_powertable_cmd cmd;
	u8 no_dtim;		/* number of skip dtim */
};

static void
il_power_sleep_cam_cmd(struct il_priv *il, struct il_powertable_cmd *cmd)
{
	memset(cmd, 0, sizeof(*cmd));

	if (il->power_data.pci_pm)
		cmd->flags |= IL_POWER_PCI_PM_MSK;

	D_POWER("Sleep command for CAM\n");
}

static int
il_set_power(struct il_priv *il, struct il_powertable_cmd *cmd)
{
	D_POWER("Sending power/sleep command\n");
	D_POWER("Flags value = 0x%08X\n", cmd->flags);
	D_POWER("Tx timeout = %u\n", le32_to_cpu(cmd->tx_data_timeout));
	D_POWER("Rx timeout = %u\n", le32_to_cpu(cmd->rx_data_timeout));
	D_POWER("Sleep interval vector = { %d , %d , %d , %d , %d }\n",
		le32_to_cpu(cmd->sleep_interval[0]),
		le32_to_cpu(cmd->sleep_interval[1]),
		le32_to_cpu(cmd->sleep_interval[2]),
		le32_to_cpu(cmd->sleep_interval[3]),
		le32_to_cpu(cmd->sleep_interval[4]));

	return il_send_cmd_pdu(il, C_POWER_TBL,
			       sizeof(struct il_powertable_cmd), cmd);
}

int
il_power_set_mode(struct il_priv *il, struct il_powertable_cmd *cmd, bool force)
{
	int ret;
	bool update_chains;

	lockdep_assert_held(&il->mutex);

	/* Don't update the RX chain when chain noise calibration is running */
	update_chains = il->chain_noise_data.state == IL_CHAIN_NOISE_DONE ||
	    il->chain_noise_data.state == IL_CHAIN_NOISE_ALIVE;

	if (!memcmp(&il->power_data.sleep_cmd, cmd, sizeof(*cmd)) && !force)
		return 0;

	if (!il_is_ready_rf(il))
		return -EIO;

	/* scan complete use sleep_power_next, need to be updated */
	memcpy(&il->power_data.sleep_cmd_next, cmd, sizeof(*cmd));
	if (test_bit(S_SCANNING, &il->status) && !force) {
		D_INFO("Defer power set mode while scanning\n");
		return 0;
	}

	if (cmd->flags & IL_POWER_DRIVER_ALLOW_SLEEP_MSK)
		set_bit(S_POWER_PMI, &il->status);

	ret = il_set_power(il, cmd);
	if (!ret) {
		if (!(cmd->flags & IL_POWER_DRIVER_ALLOW_SLEEP_MSK))
			clear_bit(S_POWER_PMI, &il->status);

		if (il->ops->update_chain_flags && update_chains)
			il->ops->update_chain_flags(il);
		else if (il->ops->update_chain_flags)
			D_POWER("Cannot update the power, chain noise "
				"calibration running: %d\n",
				il->chain_noise_data.state);

		memcpy(&il->power_data.sleep_cmd, cmd, sizeof(*cmd));
	} else
		IL_ERR("set power fail, ret = %d", ret);

	return ret;
}

int
il_power_update_mode(struct il_priv *il, bool force)
{
	struct il_powertable_cmd cmd;

	il_power_sleep_cam_cmd(il, &cmd);
	return il_power_set_mode(il, &cmd, force);
}
EXPORT_SYMBOL(il_power_update_mode);

/* initialize to default */
void
il_power_initialize(struct il_priv *il)
{
	u16 lctl = il_pcie_link_ctl(il);

	il->power_data.pci_pm = !(lctl & PCI_CFG_LINK_CTRL_VAL_L0S_EN);

	il->power_data.debug_sleep_level_override = -1;

	memset(&il->power_data.sleep_cmd, 0, sizeof(il->power_data.sleep_cmd));
}
EXPORT_SYMBOL(il_power_initialize);

/* For active scan, listen ACTIVE_DWELL_TIME (msec) on each channel after
 * sending probe req.  This should be set long enough to hear probe responses
 * from more than one AP.  */
#define IL_ACTIVE_DWELL_TIME_24    (30)	/* all times in msec */
#define IL_ACTIVE_DWELL_TIME_52    (20)

#define IL_ACTIVE_DWELL_FACTOR_24GHZ (3)
#define IL_ACTIVE_DWELL_FACTOR_52GHZ (2)

/* For passive scan, listen PASSIVE_DWELL_TIME (msec) on each channel.
 * Must be set longer than active dwell time.
 * For the most reliable scan, set > AP beacon interval (typically 100msec). */
#define IL_PASSIVE_DWELL_TIME_24   (20)	/* all times in msec */
#define IL_PASSIVE_DWELL_TIME_52   (10)
#define IL_PASSIVE_DWELL_BASE      (100)
#define IL_CHANNEL_TUNE_TIME       5

static int
il_send_scan_abort(struct il_priv *il)
{
	int ret;
	struct il_rx_pkt *pkt;
	struct il_host_cmd cmd = {
		.id = C_SCAN_ABORT,
		.flags = CMD_WANT_SKB,
	};

	/* Exit instantly with error when device is not ready
	 * to receive scan abort command or it does not perform
	 * hardware scan currently */
	if (!test_bit(S_READY, &il->status) ||
	    !test_bit(S_GEO_CONFIGURED, &il->status) ||
	    !test_bit(S_SCAN_HW, &il->status) ||
	    test_bit(S_FW_ERROR, &il->status) ||
	    test_bit(S_EXIT_PENDING, &il->status))
		return -EIO;

	ret = il_send_cmd_sync(il, &cmd);
	if (ret)
		return ret;

	pkt = (struct il_rx_pkt *)cmd.reply_page;
	if (pkt->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		D_SCAN("SCAN_ABORT ret %d.\n", pkt->u.status);
		ret = -EIO;
	}

	il_free_pages(il, cmd.reply_page);
	return ret;
}

static void
il_complete_scan(struct il_priv *il, bool aborted)
{
	/* check if scan was requested from mac80211 */
	if (il->scan_request) {
		D_SCAN("Complete scan in mac80211\n");
		ieee80211_scan_completed(il->hw, aborted);
	}

	il->scan_vif = NULL;
	il->scan_request = NULL;
}

void
il_force_scan_end(struct il_priv *il)
{
	lockdep_assert_held(&il->mutex);

	if (!test_bit(S_SCANNING, &il->status)) {
		D_SCAN("Forcing scan end while not scanning\n");
		return;
	}

	D_SCAN("Forcing scan end\n");
	clear_bit(S_SCANNING, &il->status);
	clear_bit(S_SCAN_HW, &il->status);
	clear_bit(S_SCAN_ABORTING, &il->status);
	il_complete_scan(il, true);
}

static void
il_do_scan_abort(struct il_priv *il)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	if (!test_bit(S_SCANNING, &il->status)) {
		D_SCAN("Not performing scan to abort\n");
		return;
	}

	if (test_and_set_bit(S_SCAN_ABORTING, &il->status)) {
		D_SCAN("Scan abort in progress\n");
		return;
	}

	ret = il_send_scan_abort(il);
	if (ret) {
		D_SCAN("Send scan abort failed %d\n", ret);
		il_force_scan_end(il);
	} else
		D_SCAN("Successfully send scan abort\n");
}

/**
 * il_scan_cancel - Cancel any currently executing HW scan
 */
int
il_scan_cancel(struct il_priv *il)
{
	D_SCAN("Queuing abort scan\n");
	queue_work(il->workqueue, &il->abort_scan);
	return 0;
}
EXPORT_SYMBOL(il_scan_cancel);

/**
 * il_scan_cancel_timeout - Cancel any currently executing HW scan
 * @ms: amount of time to wait (in milliseconds) for scan to abort
 *
 */
int
il_scan_cancel_timeout(struct il_priv *il, unsigned long ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms);

	lockdep_assert_held(&il->mutex);

	D_SCAN("Scan cancel timeout\n");

	il_do_scan_abort(il);

	while (time_before_eq(jiffies, timeout)) {
		if (!test_bit(S_SCAN_HW, &il->status))
			break;
		msleep(20);
	}

	return test_bit(S_SCAN_HW, &il->status);
}
EXPORT_SYMBOL(il_scan_cancel_timeout);

/* Service response to C_SCAN (0x80) */
static void
il_hdl_scan(struct il_priv *il, struct il_rx_buf *rxb)
{
#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanreq_notification *notif =
	    (struct il_scanreq_notification *)pkt->u.raw;

	D_SCAN("Scan request status = 0x%x\n", notif->status);
#endif
}

/* Service N_SCAN_START (0x82) */
static void
il_hdl_scan_start(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanstart_notification *notif =
	    (struct il_scanstart_notification *)pkt->u.raw;
	il->scan_start_tsf = le32_to_cpu(notif->tsf_low);
	D_SCAN("Scan start: " "%d [802.11%s] "
	       "(TSF: 0x%08X:%08X) - %d (beacon timer %u)\n", notif->channel,
	       notif->band ? "bg" : "a", le32_to_cpu(notif->tsf_high),
	       le32_to_cpu(notif->tsf_low), notif->status, notif->beacon_timer);
}

/* Service N_SCAN_RESULTS (0x83) */
static void
il_hdl_scan_results(struct il_priv *il, struct il_rx_buf *rxb)
{
#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanresults_notification *notif =
	    (struct il_scanresults_notification *)pkt->u.raw;

	D_SCAN("Scan ch.res: " "%d [802.11%s] " "(TSF: 0x%08X:%08X) - %d "
	       "elapsed=%lu usec\n", notif->channel, notif->band ? "bg" : "a",
	       le32_to_cpu(notif->tsf_high), le32_to_cpu(notif->tsf_low),
	       le32_to_cpu(notif->stats[0]),
	       le32_to_cpu(notif->tsf_low) - il->scan_start_tsf);
#endif
}

/* Service N_SCAN_COMPLETE (0x84) */
static void
il_hdl_scan_complete(struct il_priv *il, struct il_rx_buf *rxb)
{

#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scancomplete_notification *scan_notif = (void *)pkt->u.raw;
#endif

	D_SCAN("Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
	       scan_notif->scanned_channels, scan_notif->tsf_low,
	       scan_notif->tsf_high, scan_notif->status);

	/* The HW is no longer scanning */
	clear_bit(S_SCAN_HW, &il->status);

	D_SCAN("Scan on %sGHz took %dms\n",
	       (il->scan_band == IEEE80211_BAND_2GHZ) ? "2.4" : "5.2",
	       jiffies_to_msecs(jiffies - il->scan_start));

	queue_work(il->workqueue, &il->scan_completed);
}

void
il_setup_rx_scan_handlers(struct il_priv *il)
{
	/* scan handlers */
	il->handlers[C_SCAN] = il_hdl_scan;
	il->handlers[N_SCAN_START] = il_hdl_scan_start;
	il->handlers[N_SCAN_RESULTS] = il_hdl_scan_results;
	il->handlers[N_SCAN_COMPLETE] = il_hdl_scan_complete;
}
EXPORT_SYMBOL(il_setup_rx_scan_handlers);

inline u16
il_get_active_dwell_time(struct il_priv *il, enum ieee80211_band band,
			 u8 n_probes)
{
	if (band == IEEE80211_BAND_5GHZ)
		return IL_ACTIVE_DWELL_TIME_52 +
		    IL_ACTIVE_DWELL_FACTOR_52GHZ * (n_probes + 1);
	else
		return IL_ACTIVE_DWELL_TIME_24 +
		    IL_ACTIVE_DWELL_FACTOR_24GHZ * (n_probes + 1);
}
EXPORT_SYMBOL(il_get_active_dwell_time);

u16
il_get_passive_dwell_time(struct il_priv *il, enum ieee80211_band band,
			  struct ieee80211_vif *vif)
{
	u16 value;

	u16 passive =
	    (band ==
	     IEEE80211_BAND_2GHZ) ? IL_PASSIVE_DWELL_BASE +
	    IL_PASSIVE_DWELL_TIME_24 : IL_PASSIVE_DWELL_BASE +
	    IL_PASSIVE_DWELL_TIME_52;

	if (il_is_any_associated(il)) {
		/*
		 * If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the smallest beacon interval
		 * (minus 2 * channel tune time)
		 */
		value = il->vif ? il->vif->bss_conf.beacon_int : 0;
		if (value > IL_PASSIVE_DWELL_BASE || !value)
			value = IL_PASSIVE_DWELL_BASE;
		value = (value * 98) / 100 - IL_CHANNEL_TUNE_TIME * 2;
		passive = min(value, passive);
	}

	return passive;
}
EXPORT_SYMBOL(il_get_passive_dwell_time);

void
il_init_scan_params(struct il_priv *il)
{
	u8 ant_idx = fls(il->hw_params.valid_tx_ant) - 1;
	if (!il->scan_tx_ant[IEEE80211_BAND_5GHZ])
		il->scan_tx_ant[IEEE80211_BAND_5GHZ] = ant_idx;
	if (!il->scan_tx_ant[IEEE80211_BAND_2GHZ])
		il->scan_tx_ant[IEEE80211_BAND_2GHZ] = ant_idx;
}
EXPORT_SYMBOL(il_init_scan_params);

static int
il_scan_initiate(struct il_priv *il, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	cancel_delayed_work(&il->scan_check);

	if (!il_is_ready_rf(il)) {
		IL_WARN("Request scan called when driver not ready.\n");
		return -EIO;
	}

	if (test_bit(S_SCAN_HW, &il->status)) {
		D_SCAN("Multiple concurrent scan requests in parallel.\n");
		return -EBUSY;
	}

	if (test_bit(S_SCAN_ABORTING, &il->status)) {
		D_SCAN("Scan request while abort pending.\n");
		return -EBUSY;
	}

	D_SCAN("Starting scan...\n");

	set_bit(S_SCANNING, &il->status);
	il->scan_start = jiffies;

	ret = il->ops->request_scan(il, vif);
	if (ret) {
		clear_bit(S_SCANNING, &il->status);
		return ret;
	}

	queue_delayed_work(il->workqueue, &il->scan_check,
			   IL_SCAN_CHECK_WATCHDOG);

	return 0;
}

int
il_mac_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct cfg80211_scan_request *req)
{
	struct il_priv *il = hw->priv;
	int ret;

	if (req->n_channels == 0) {
		IL_ERR("Can not scan on no channels.\n");
		return -EINVAL;
	}

	mutex_lock(&il->mutex);
	D_MAC80211("enter\n");

	if (test_bit(S_SCANNING, &il->status)) {
		D_SCAN("Scan already in progress.\n");
		ret = -EAGAIN;
		goto out_unlock;
	}

	/* mac80211 will only ask for one band at a time */
	il->scan_request = req;
	il->scan_vif = vif;
	il->scan_band = req->channels[0]->band;

	ret = il_scan_initiate(il, vif);

out_unlock:
	D_MAC80211("leave ret %d\n", ret);
	mutex_unlock(&il->mutex);

	return ret;
}
EXPORT_SYMBOL(il_mac_hw_scan);

static void
il_bg_scan_check(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, scan_check.work);

	D_SCAN("Scan check work\n");

	/* Since we are here firmware does not finish scan and
	 * most likely is in bad shape, so we don't bother to
	 * send abort command, just force scan complete to mac80211 */
	mutex_lock(&il->mutex);
	il_force_scan_end(il);
	mutex_unlock(&il->mutex);
}

/**
 * il_fill_probe_req - fill in all required fields and IE for probe request
 */

u16
il_fill_probe_req(struct il_priv *il, struct ieee80211_mgmt *frame,
		  const u8 *ta, const u8 *ies, int ie_len, int left)
{
	int len = 0;
	u8 *pos = NULL;

	/* Make sure there is enough space for the probe request,
	 * two mandatory IEs and the data */
	left -= 24;
	if (left < 0)
		return 0;

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	memcpy(frame->da, il_bcast_addr, ETH_ALEN);
	memcpy(frame->sa, ta, ETH_ALEN);
	memcpy(frame->bssid, il_bcast_addr, ETH_ALEN);
	frame->seq_ctrl = 0;

	len += 24;

	/* ...next IE... */
	pos = &frame->u.probe_req.variable[0];

	/* fill in our indirect SSID IE */
	left -= 2;
	if (left < 0)
		return 0;
	*pos++ = WLAN_EID_SSID;
	*pos++ = 0;

	len += 2;

	if (WARN_ON(left < ie_len))
		return len;

	if (ies && ie_len) {
		memcpy(pos, ies, ie_len);
		len += ie_len;
	}

	return (u16) len;
}
EXPORT_SYMBOL(il_fill_probe_req);

static void
il_bg_abort_scan(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv, abort_scan);

	D_SCAN("Abort scan work\n");

	/* We keep scan_check work queued in case when firmware will not
	 * report back scan completed notification */
	mutex_lock(&il->mutex);
	il_scan_cancel_timeout(il, 200);
	mutex_unlock(&il->mutex);
}

static void
il_bg_scan_completed(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv, scan_completed);
	bool aborted;

	D_SCAN("Completed scan.\n");

	cancel_delayed_work(&il->scan_check);

	mutex_lock(&il->mutex);

	aborted = test_and_clear_bit(S_SCAN_ABORTING, &il->status);
	if (aborted)
		D_SCAN("Aborted scan completed.\n");

	if (!test_and_clear_bit(S_SCANNING, &il->status)) {
		D_SCAN("Scan already completed.\n");
		goto out_settings;
	}

	il_complete_scan(il, aborted);

out_settings:
	/* Can we still talk to firmware ? */
	if (!il_is_ready_rf(il))
		goto out;

	/*
	 * We do not commit power settings while scan is pending,
	 * do it now if the settings changed.
	 */
	il_power_set_mode(il, &il->power_data.sleep_cmd_next, false);
	il_set_tx_power(il, il->tx_power_next, false);

	il->ops->post_scan(il);

out:
	mutex_unlock(&il->mutex);
}

void
il_setup_scan_deferred_work(struct il_priv *il)
{
	INIT_WORK(&il->scan_completed, il_bg_scan_completed);
	INIT_WORK(&il->abort_scan, il_bg_abort_scan);
	INIT_DELAYED_WORK(&il->scan_check, il_bg_scan_check);
}
EXPORT_SYMBOL(il_setup_scan_deferred_work);

void
il_cancel_scan_deferred_work(struct il_priv *il)
{
	cancel_work_sync(&il->abort_scan);
	cancel_work_sync(&il->scan_completed);

	if (cancel_delayed_work_sync(&il->scan_check)) {
		mutex_lock(&il->mutex);
		il_force_scan_end(il);
		mutex_unlock(&il->mutex);
	}
}
EXPORT_SYMBOL(il_cancel_scan_deferred_work);

/* il->sta_lock must be held */
static void
il_sta_ucode_activate(struct il_priv *il, u8 sta_id)
{

	if (!(il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE))
		IL_ERR("ACTIVATE a non DRIVER active station id %u addr %pM\n",
		       sta_id, il->stations[sta_id].sta.sta.addr);

	if (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE) {
		D_ASSOC("STA id %u addr %pM already present"
			" in uCode (according to driver)\n", sta_id,
			il->stations[sta_id].sta.sta.addr);
	} else {
		il->stations[sta_id].used |= IL_STA_UCODE_ACTIVE;
		D_ASSOC("Added STA id %u addr %pM to uCode\n", sta_id,
			il->stations[sta_id].sta.sta.addr);
	}
}

static int
il_process_add_sta_resp(struct il_priv *il, struct il_addsta_cmd *addsta,
			struct il_rx_pkt *pkt, bool sync)
{
	u8 sta_id = addsta->sta.sta_id;
	unsigned long flags;
	int ret = -EIO;

	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR("Bad return from C_ADD_STA (0x%08X)\n", pkt->hdr.flags);
		return ret;
	}

	D_INFO("Processing response for adding station %u\n", sta_id);

	spin_lock_irqsave(&il->sta_lock, flags);

	switch (pkt->u.add_sta.status) {
	case ADD_STA_SUCCESS_MSK:
		D_INFO("C_ADD_STA PASSED\n");
		il_sta_ucode_activate(il, sta_id);
		ret = 0;
		break;
	case ADD_STA_NO_ROOM_IN_TBL:
		IL_ERR("Adding station %d failed, no room in table.\n", sta_id);
		break;
	case ADD_STA_NO_BLOCK_ACK_RESOURCE:
		IL_ERR("Adding station %d failed, no block ack resource.\n",
		       sta_id);
		break;
	case ADD_STA_MODIFY_NON_EXIST_STA:
		IL_ERR("Attempting to modify non-existing station %d\n",
		       sta_id);
		break;
	default:
		D_ASSOC("Received C_ADD_STA:(0x%08X)\n", pkt->u.add_sta.status);
		break;
	}

	D_INFO("%s station id %u addr %pM\n",
	       il->stations[sta_id].sta.mode ==
	       STA_CONTROL_MODIFY_MSK ? "Modified" : "Added", sta_id,
	       il->stations[sta_id].sta.sta.addr);

	/*
	 * XXX: The MAC address in the command buffer is often changed from
	 * the original sent to the device. That is, the MAC address
	 * written to the command buffer often is not the same MAC address
	 * read from the command buffer when the command returns. This
	 * issue has not yet been resolved and this debugging is left to
	 * observe the problem.
	 */
	D_INFO("%s station according to cmd buffer %pM\n",
	       il->stations[sta_id].sta.mode ==
	       STA_CONTROL_MODIFY_MSK ? "Modified" : "Added", addsta->sta.addr);
	spin_unlock_irqrestore(&il->sta_lock, flags);

	return ret;
}

static void
il_add_sta_callback(struct il_priv *il, struct il_device_cmd *cmd,
		    struct il_rx_pkt *pkt)
{
	struct il_addsta_cmd *addsta = (struct il_addsta_cmd *)cmd->cmd.payload;

	il_process_add_sta_resp(il, addsta, pkt, false);

}

int
il_send_add_sta(struct il_priv *il, struct il_addsta_cmd *sta, u8 flags)
{
	struct il_rx_pkt *pkt = NULL;
	int ret = 0;
	u8 data[sizeof(*sta)];
	struct il_host_cmd cmd = {
		.id = C_ADD_STA,
		.flags = flags,
		.data = data,
	};
	u8 sta_id __maybe_unused = sta->sta.sta_id;

	D_INFO("Adding sta %u (%pM) %ssynchronously\n", sta_id, sta->sta.addr,
	       flags & CMD_ASYNC ? "a" : "");

	if (flags & CMD_ASYNC)
		cmd.callback = il_add_sta_callback;
	else {
		cmd.flags |= CMD_WANT_SKB;
		might_sleep();
	}

	cmd.len = il->ops->build_addsta_hcmd(sta, data);
	ret = il_send_cmd(il, &cmd);

	if (ret || (flags & CMD_ASYNC))
		return ret;

	if (ret == 0) {
		pkt = (struct il_rx_pkt *)cmd.reply_page;
		ret = il_process_add_sta_resp(il, sta, pkt, true);
	}
	il_free_pages(il, cmd.reply_page);

	return ret;
}
EXPORT_SYMBOL(il_send_add_sta);

static void
il_set_ht_add_station(struct il_priv *il, u8 idx, struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *sta_ht_inf = &sta->ht_cap;
	__le32 sta_flags;
	u8 mimo_ps_mode;

	if (!sta || !sta_ht_inf->ht_supported)
		goto done;

	mimo_ps_mode = (sta_ht_inf->cap & IEEE80211_HT_CAP_SM_PS) >> 2;
	D_ASSOC("spatial multiplexing power save mode: %s\n",
		(mimo_ps_mode == WLAN_HT_CAP_SM_PS_STATIC) ? "static" :
		(mimo_ps_mode == WLAN_HT_CAP_SM_PS_DYNAMIC) ? "dynamic" :
		"disabled");

	sta_flags = il->stations[idx].sta.station_flags;

	sta_flags &= ~(STA_FLG_RTS_MIMO_PROT_MSK | STA_FLG_MIMO_DIS_MSK);

	switch (mimo_ps_mode) {
	case WLAN_HT_CAP_SM_PS_STATIC:
		sta_flags |= STA_FLG_MIMO_DIS_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DYNAMIC:
		sta_flags |= STA_FLG_RTS_MIMO_PROT_MSK;
		break;
	case WLAN_HT_CAP_SM_PS_DISABLED:
		break;
	default:
		IL_WARN("Invalid MIMO PS mode %d\n", mimo_ps_mode);
		break;
	}

	sta_flags |=
	    cpu_to_le32((u32) sta_ht_inf->
			ampdu_factor << STA_FLG_MAX_AGG_SIZE_POS);

	sta_flags |=
	    cpu_to_le32((u32) sta_ht_inf->
			ampdu_density << STA_FLG_AGG_MPDU_DENSITY_POS);

	if (il_is_ht40_tx_allowed(il, &sta->ht_cap))
		sta_flags |= STA_FLG_HT40_EN_MSK;
	else
		sta_flags &= ~STA_FLG_HT40_EN_MSK;

	il->stations[idx].sta.station_flags = sta_flags;
done:
	return;
}

/**
 * il_prep_station - Prepare station information for addition
 *
 * should be called with sta_lock held
 */
u8
il_prep_station(struct il_priv *il, const u8 *addr, bool is_ap,
		struct ieee80211_sta *sta)
{
	struct il_station_entry *station;
	int i;
	u8 sta_id = IL_INVALID_STATION;
	u16 rate;

	if (is_ap)
		sta_id = IL_AP_ID;
	else if (is_broadcast_ether_addr(addr))
		sta_id = il->hw_params.bcast_id;
	else
		for (i = IL_STA_ID; i < il->hw_params.max_stations; i++) {
			if (ether_addr_equal(il->stations[i].sta.sta.addr,
					     addr)) {
				sta_id = i;
				break;
			}

			if (!il->stations[i].used &&
			    sta_id == IL_INVALID_STATION)
				sta_id = i;
		}

	/*
	 * These two conditions have the same outcome, but keep them
	 * separate
	 */
	if (unlikely(sta_id == IL_INVALID_STATION))
		return sta_id;

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (il->stations[sta_id].used & IL_STA_UCODE_INPROGRESS) {
		D_INFO("STA %d already in process of being added.\n", sta_id);
		return sta_id;
	}

	if ((il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE) &&
	    (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE) &&
	    ether_addr_equal(il->stations[sta_id].sta.sta.addr, addr)) {
		D_ASSOC("STA %d (%pM) already added, not adding again.\n",
			sta_id, addr);
		return sta_id;
	}

	station = &il->stations[sta_id];
	station->used = IL_STA_DRIVER_ACTIVE;
	D_ASSOC("Add STA to driver ID %d: %pM\n", sta_id, addr);
	il->num_stations++;

	/* Set up the C_ADD_STA command to send to device */
	memset(&station->sta, 0, sizeof(struct il_addsta_cmd));
	memcpy(station->sta.sta.addr, addr, ETH_ALEN);
	station->sta.mode = 0;
	station->sta.sta.sta_id = sta_id;
	station->sta.station_flags = 0;

	/*
	 * OK to call unconditionally, since local stations (IBSS BSSID
	 * STA and broadcast STA) pass in a NULL sta, and mac80211
	 * doesn't allow HT IBSS.
	 */
	il_set_ht_add_station(il, sta_id, sta);

	/* 3945 only */
	rate = (il->band == IEEE80211_BAND_5GHZ) ? RATE_6M_PLCP : RATE_1M_PLCP;
	/* Turn on both antennas for the station... */
	station->sta.rate_n_flags = cpu_to_le16(rate | RATE_MCS_ANT_AB_MSK);

	return sta_id;

}
EXPORT_SYMBOL_GPL(il_prep_station);

#define STA_WAIT_TIMEOUT (HZ/2)

/**
 * il_add_station_common -
 */
int
il_add_station_common(struct il_priv *il, const u8 *addr, bool is_ap,
		      struct ieee80211_sta *sta, u8 *sta_id_r)
{
	unsigned long flags_spin;
	int ret = 0;
	u8 sta_id;
	struct il_addsta_cmd sta_cmd;

	*sta_id_r = 0;
	spin_lock_irqsave(&il->sta_lock, flags_spin);
	sta_id = il_prep_station(il, addr, is_ap, sta);
	if (sta_id == IL_INVALID_STATION) {
		IL_ERR("Unable to prepare station %pM for addition\n", addr);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EINVAL;
	}

	/*
	 * uCode is not able to deal with multiple requests to add a
	 * station. Keep track if one is in progress so that we do not send
	 * another.
	 */
	if (il->stations[sta_id].used & IL_STA_UCODE_INPROGRESS) {
		D_INFO("STA %d already in process of being added.\n", sta_id);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EEXIST;
	}

	if ((il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE) &&
	    (il->stations[sta_id].used & IL_STA_UCODE_ACTIVE)) {
		D_ASSOC("STA %d (%pM) already added, not adding again.\n",
			sta_id, addr);
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EEXIST;
	}

	il->stations[sta_id].used |= IL_STA_UCODE_INPROGRESS;
	memcpy(&sta_cmd, &il->stations[sta_id].sta,
	       sizeof(struct il_addsta_cmd));
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	/* Add station to device's station table */
	ret = il_send_add_sta(il, &sta_cmd, CMD_SYNC);
	if (ret) {
		spin_lock_irqsave(&il->sta_lock, flags_spin);
		IL_ERR("Adding station %pM failed.\n",
		       il->stations[sta_id].sta.sta.addr);
		il->stations[sta_id].used &= ~IL_STA_DRIVER_ACTIVE;
		il->stations[sta_id].used &= ~IL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	}
	*sta_id_r = sta_id;
	return ret;
}
EXPORT_SYMBOL(il_add_station_common);

/**
 * il_sta_ucode_deactivate - deactivate ucode status for a station
 *
 * il->sta_lock must be held
 */
static void
il_sta_ucode_deactivate(struct il_priv *il, u8 sta_id)
{
	/* Ucode must be active and driver must be non active */
	if ((il->stations[sta_id].
	     used & (IL_STA_UCODE_ACTIVE | IL_STA_DRIVER_ACTIVE)) !=
	    IL_STA_UCODE_ACTIVE)
		IL_ERR("removed non active STA %u\n", sta_id);

	il->stations[sta_id].used &= ~IL_STA_UCODE_ACTIVE;

	memset(&il->stations[sta_id], 0, sizeof(struct il_station_entry));
	D_ASSOC("Removed STA %u\n", sta_id);
}

static int
il_send_remove_station(struct il_priv *il, const u8 * addr, int sta_id,
		       bool temporary)
{
	struct il_rx_pkt *pkt;
	int ret;

	unsigned long flags_spin;
	struct il_rem_sta_cmd rm_sta_cmd;

	struct il_host_cmd cmd = {
		.id = C_REM_STA,
		.len = sizeof(struct il_rem_sta_cmd),
		.flags = CMD_SYNC,
		.data = &rm_sta_cmd,
	};

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	rm_sta_cmd.num_sta = 1;
	memcpy(&rm_sta_cmd.addr, addr, ETH_ALEN);

	cmd.flags |= CMD_WANT_SKB;

	ret = il_send_cmd(il, &cmd);

	if (ret)
		return ret;

	pkt = (struct il_rx_pkt *)cmd.reply_page;
	if (pkt->hdr.flags & IL_CMD_FAILED_MSK) {
		IL_ERR("Bad return from C_REM_STA (0x%08X)\n", pkt->hdr.flags);
		ret = -EIO;
	}

	if (!ret) {
		switch (pkt->u.rem_sta.status) {
		case REM_STA_SUCCESS_MSK:
			if (!temporary) {
				spin_lock_irqsave(&il->sta_lock, flags_spin);
				il_sta_ucode_deactivate(il, sta_id);
				spin_unlock_irqrestore(&il->sta_lock,
						       flags_spin);
			}
			D_ASSOC("C_REM_STA PASSED\n");
			break;
		default:
			ret = -EIO;
			IL_ERR("C_REM_STA failed\n");
			break;
		}
	}
	il_free_pages(il, cmd.reply_page);

	return ret;
}

/**
 * il_remove_station - Remove driver's knowledge of station.
 */
int
il_remove_station(struct il_priv *il, const u8 sta_id, const u8 * addr)
{
	unsigned long flags;

	if (!il_is_ready(il)) {
		D_INFO("Unable to remove station %pM, device not ready.\n",
		       addr);
		/*
		 * It is typical for stations to be removed when we are
		 * going down. Return success since device will be down
		 * soon anyway
		 */
		return 0;
	}

	D_ASSOC("Removing STA from driver:%d  %pM\n", sta_id, addr);

	if (WARN_ON(sta_id == IL_INVALID_STATION))
		return -EINVAL;

	spin_lock_irqsave(&il->sta_lock, flags);

	if (!(il->stations[sta_id].used & IL_STA_DRIVER_ACTIVE)) {
		D_INFO("Removing %pM but non DRIVER active\n", addr);
		goto out_err;
	}

	if (!(il->stations[sta_id].used & IL_STA_UCODE_ACTIVE)) {
		D_INFO("Removing %pM but non UCODE active\n", addr);
		goto out_err;
	}

	if (il->stations[sta_id].used & IL_STA_LOCAL) {
		kfree(il->stations[sta_id].lq);
		il->stations[sta_id].lq = NULL;
	}

	il->stations[sta_id].used &= ~IL_STA_DRIVER_ACTIVE;

	il->num_stations--;

	BUG_ON(il->num_stations < 0);

	spin_unlock_irqrestore(&il->sta_lock, flags);

	return il_send_remove_station(il, addr, sta_id, false);
out_err:
	spin_unlock_irqrestore(&il->sta_lock, flags);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(il_remove_station);

/**
 * il_clear_ucode_stations - clear ucode station table bits
 *
 * This function clears all the bits in the driver indicating
 * which stations are active in the ucode. Call when something
 * other than explicit station management would cause this in
 * the ucode, e.g. unassociated RXON.
 */
void
il_clear_ucode_stations(struct il_priv *il)
{
	int i;
	unsigned long flags_spin;
	bool cleared = false;

	D_INFO("Clearing ucode stations in driver\n");

	spin_lock_irqsave(&il->sta_lock, flags_spin);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if (il->stations[i].used & IL_STA_UCODE_ACTIVE) {
			D_INFO("Clearing ucode active for station %d\n", i);
			il->stations[i].used &= ~IL_STA_UCODE_ACTIVE;
			cleared = true;
		}
	}
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	if (!cleared)
		D_INFO("No active stations found to be cleared\n");
}
EXPORT_SYMBOL(il_clear_ucode_stations);

/**
 * il_restore_stations() - Restore driver known stations to device
 *
 * All stations considered active by driver, but not present in ucode, is
 * restored.
 *
 * Function sleeps.
 */
void
il_restore_stations(struct il_priv *il)
{
	struct il_addsta_cmd sta_cmd;
	struct il_link_quality_cmd lq;
	unsigned long flags_spin;
	int i;
	bool found = false;
	int ret;
	bool send_lq;

	if (!il_is_ready(il)) {
		D_INFO("Not ready yet, not restoring any stations.\n");
		return;
	}

	D_ASSOC("Restoring all known stations ... start.\n");
	spin_lock_irqsave(&il->sta_lock, flags_spin);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if ((il->stations[i].used & IL_STA_DRIVER_ACTIVE) &&
		    !(il->stations[i].used & IL_STA_UCODE_ACTIVE)) {
			D_ASSOC("Restoring sta %pM\n",
				il->stations[i].sta.sta.addr);
			il->stations[i].sta.mode = 0;
			il->stations[i].used |= IL_STA_UCODE_INPROGRESS;
			found = true;
		}
	}

	for (i = 0; i < il->hw_params.max_stations; i++) {
		if ((il->stations[i].used & IL_STA_UCODE_INPROGRESS)) {
			memcpy(&sta_cmd, &il->stations[i].sta,
			       sizeof(struct il_addsta_cmd));
			send_lq = false;
			if (il->stations[i].lq) {
				memcpy(&lq, il->stations[i].lq,
				       sizeof(struct il_link_quality_cmd));
				send_lq = true;
			}
			spin_unlock_irqrestore(&il->sta_lock, flags_spin);
			ret = il_send_add_sta(il, &sta_cmd, CMD_SYNC);
			if (ret) {
				spin_lock_irqsave(&il->sta_lock, flags_spin);
				IL_ERR("Adding station %pM failed.\n",
				       il->stations[i].sta.sta.addr);
				il->stations[i].used &= ~IL_STA_DRIVER_ACTIVE;
				il->stations[i].used &=
				    ~IL_STA_UCODE_INPROGRESS;
				spin_unlock_irqrestore(&il->sta_lock,
						       flags_spin);
			}
			/*
			 * Rate scaling has already been initialized, send
			 * current LQ command
			 */
			if (send_lq)
				il_send_lq_cmd(il, &lq, CMD_SYNC, true);
			spin_lock_irqsave(&il->sta_lock, flags_spin);
			il->stations[i].used &= ~IL_STA_UCODE_INPROGRESS;
		}
	}

	spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	if (!found)
		D_INFO("Restoring all known stations"
		       " .... no stations to be restored.\n");
	else
		D_INFO("Restoring all known stations" " .... complete.\n");
}
EXPORT_SYMBOL(il_restore_stations);

int
il_get_free_ucode_key_idx(struct il_priv *il)
{
	int i;

	for (i = 0; i < il->sta_key_max_num; i++)
		if (!test_and_set_bit(i, &il->ucode_key_table))
			return i;

	return WEP_INVALID_OFFSET;
}
EXPORT_SYMBOL(il_get_free_ucode_key_idx);

void
il_dealloc_bcast_stations(struct il_priv *il)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&il->sta_lock, flags);
	for (i = 0; i < il->hw_params.max_stations; i++) {
		if (!(il->stations[i].used & IL_STA_BCAST))
			continue;

		il->stations[i].used &= ~IL_STA_UCODE_ACTIVE;
		il->num_stations--;
		BUG_ON(il->num_stations < 0);
		kfree(il->stations[i].lq);
		il->stations[i].lq = NULL;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags);
}
EXPORT_SYMBOL_GPL(il_dealloc_bcast_stations);

#ifdef CONFIG_IWLEGACY_DEBUG
static void
il_dump_lq_cmd(struct il_priv *il, struct il_link_quality_cmd *lq)
{
	int i;
	D_RATE("lq station id 0x%x\n", lq->sta_id);
	D_RATE("lq ant 0x%X 0x%X\n", lq->general_params.single_stream_ant_msk,
	       lq->general_params.dual_stream_ant_msk);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		D_RATE("lq idx %d 0x%X\n", i, lq->rs_table[i].rate_n_flags);
}
#else
static inline void
il_dump_lq_cmd(struct il_priv *il, struct il_link_quality_cmd *lq)
{
}
#endif

/**
 * il_is_lq_table_valid() - Test one aspect of LQ cmd for validity
 *
 * It sometimes happens when a HT rate has been in use and we
 * loose connectivity with AP then mac80211 will first tell us that the
 * current channel is not HT anymore before removing the station. In such a
 * scenario the RXON flags will be updated to indicate we are not
 * communicating HT anymore, but the LQ command may still contain HT rates.
 * Test for this to prevent driver from sending LQ command between the time
 * RXON flags are updated and when LQ command is updated.
 */
static bool
il_is_lq_table_valid(struct il_priv *il, struct il_link_quality_cmd *lq)
{
	int i;

	if (il->ht.enabled)
		return true;

	D_INFO("Channel %u is not an HT channel\n", il->active.channel);
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		if (le32_to_cpu(lq->rs_table[i].rate_n_flags) & RATE_MCS_HT_MSK) {
			D_INFO("idx %d of LQ expects HT channel\n", i);
			return false;
		}
	}
	return true;
}

/**
 * il_send_lq_cmd() - Send link quality command
 * @init: This command is sent as part of station initialization right
 *        after station has been added.
 *
 * The link quality command is sent as the last step of station creation.
 * This is the special case in which init is set and we call a callback in
 * this case to clear the state indicating that station creation is in
 * progress.
 */
int
il_send_lq_cmd(struct il_priv *il, struct il_link_quality_cmd *lq,
	       u8 flags, bool init)
{
	int ret = 0;
	unsigned long flags_spin;

	struct il_host_cmd cmd = {
		.id = C_TX_LINK_QUALITY_CMD,
		.len = sizeof(struct il_link_quality_cmd),
		.flags = flags,
		.data = lq,
	};

	if (WARN_ON(lq->sta_id == IL_INVALID_STATION))
		return -EINVAL;

	spin_lock_irqsave(&il->sta_lock, flags_spin);
	if (!(il->stations[lq->sta_id].used & IL_STA_DRIVER_ACTIVE)) {
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&il->sta_lock, flags_spin);

	il_dump_lq_cmd(il, lq);
	BUG_ON(init && (cmd.flags & CMD_ASYNC));

	if (il_is_lq_table_valid(il, lq))
		ret = il_send_cmd(il, &cmd);
	else
		ret = -EINVAL;

	if (cmd.flags & CMD_ASYNC)
		return ret;

	if (init) {
		D_INFO("init LQ command complete,"
		       " clearing sta addition status for sta %d\n",
		       lq->sta_id);
		spin_lock_irqsave(&il->sta_lock, flags_spin);
		il->stations[lq->sta_id].used &= ~IL_STA_UCODE_INPROGRESS;
		spin_unlock_irqrestore(&il->sta_lock, flags_spin);
	}
	return ret;
}
EXPORT_SYMBOL(il_send_lq_cmd);

int
il_mac_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  struct ieee80211_sta *sta)
{
	struct il_priv *il = hw->priv;
	struct il_station_priv_common *sta_common = (void *)sta->drv_priv;
	int ret;

	mutex_lock(&il->mutex);
	D_MAC80211("enter station %pM\n", sta->addr);

	ret = il_remove_station(il, sta_common->sta_id, sta->addr);
	if (ret)
		IL_ERR("Error removing station %pM\n", sta->addr);

	D_MAC80211("leave ret %d\n", ret);
	mutex_unlock(&il->mutex);

	return ret;
}
EXPORT_SYMBOL(il_mac_sta_remove);

/************************** RX-FUNCTIONS ****************************/
/*
 * Rx theory of operation
 *
 * Driver allocates a circular buffer of Receive Buffer Descriptors (RBDs),
 * each of which point to Receive Buffers to be filled by the NIC.  These get
 * used not only for Rx frames, but for any command response or notification
 * from the NIC.  The driver and NIC manage the Rx buffers by means
 * of idxes into the circular buffer.
 *
 * Rx Queue Indexes
 * The host/firmware share two idx registers for managing the Rx buffers.
 *
 * The READ idx maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ idx is managed by the firmware once the card is enabled.
 *
 * The WRITE idx maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization, the host sets up the READ queue position to the first
 * IDX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer, it will advance the READ idx
 * and fire the RX interrupt.  The driver can then query the READ idx and
 * process as many packets as possible, moving the WRITE idx forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated SKBs is stored in iwl->rxq->rx_free.  When
 *   iwl->rxq->free_count drops to or below RX_LOW_WATERMARK, work is scheduled
 *   to replenish the iwl->rxq->rx_free.
 * + In il_rx_replenish (scheduled) if 'processed' != 'read' then the
 *   iwl->rxq is replenished and the READ IDX is updated (updating the
 *   'processed' and 'read' driver idxes as well)
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the iwl->rxq.  The driver 'processed' idx is updated.
 * + The Host/Firmware iwl->rxq is replenished at tasklet time from the rx_free
 *   list. If there are no allocated buffers in iwl->rxq->rx_free, the READ
 *   IDX is not incremented and iwl->status(RX_STALLED) is set.  If there
 *   were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * il_rx_queue_alloc()   Allocates rx_free
 * il_rx_replenish()     Replenishes rx_free list from rx_used, and calls
 *                            il_rx_queue_restock
 * il_rx_queue_restock() Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE idx.  If insufficient rx_free buffers
 *                            are available, schedules il_rx_replenish
 *
 * -- enable interrupts --
 * ISR - il_rx()         Detach il_rx_bufs from pool up to the
 *                            READ IDX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Calls il_rx_queue_restock to refill any empty
 *                            slots.
 * ...
 *
 */

/**
 * il_rx_queue_space - Return number of free slots available in queue.
 */
int
il_rx_queue_space(const struct il_rx_queue *q)
{
	int s = q->read - q->write;
	if (s <= 0)
		s += RX_QUEUE_SIZE;
	/* keep some buffer to not confuse full and empty queue */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}
EXPORT_SYMBOL(il_rx_queue_space);

/**
 * il_rx_queue_update_write_ptr - Update the write pointer for the RX queue
 */
void
il_rx_queue_update_write_ptr(struct il_priv *il, struct il_rx_queue *q)
{
	unsigned long flags;
	u32 rx_wrt_ptr_reg = il->hw_params.rx_wrt_ptr_reg;
	u32 reg;

	spin_lock_irqsave(&q->lock, flags);

	if (q->need_update == 0)
		goto exit_unlock;

	/* If power-saving is in use, make sure device is awake */
	if (test_bit(S_POWER_PMI, &il->status)) {
		reg = _il_rd(il, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			D_INFO("Rx queue requesting wakeup," " GP1 = 0x%x\n",
			       reg);
			il_set_bit(il, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			goto exit_unlock;
		}

		q->write_actual = (q->write & ~0x7);
		il_wr(il, rx_wrt_ptr_reg, q->write_actual);

		/* Else device is assumed to be awake */
	} else {
		/* Device expects a multiple of 8 */
		q->write_actual = (q->write & ~0x7);
		il_wr(il, rx_wrt_ptr_reg, q->write_actual);
	}

	q->need_update = 0;

exit_unlock:
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(il_rx_queue_update_write_ptr);

int
il_rx_queue_alloc(struct il_priv *il)
{
	struct il_rx_queue *rxq = &il->rxq;
	struct device *dev = &il->pci_dev->dev;
	int i;

	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Alloc the circular buffer of Read Buffer Descriptors (RBDs) */
	rxq->bd =
	    dma_alloc_coherent(dev, 4 * RX_QUEUE_SIZE, &rxq->bd_dma,
			       GFP_KERNEL);
	if (!rxq->bd)
		goto err_bd;

	rxq->rb_stts =
	    dma_alloc_coherent(dev, sizeof(struct il_rb_status),
			       &rxq->rb_stts_dma, GFP_KERNEL);
	if (!rxq->rb_stts)
		goto err_rb;

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++)
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	rxq->need_update = 0;
	return 0;

err_rb:
	dma_free_coherent(&il->pci_dev->dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			  rxq->bd_dma);
err_bd:
	return -ENOMEM;
}
EXPORT_SYMBOL(il_rx_queue_alloc);

void
il_hdl_spectrum_measurement(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_spectrum_notification *report = &(pkt->u.spectrum_notif);

	if (!report->state) {
		D_11H("Spectrum Measure Notification: Start\n");
		return;
	}

	memcpy(&il->measure_report, report, sizeof(*report));
	il->measurement_status |= MEASUREMENT_READY;
}
EXPORT_SYMBOL(il_hdl_spectrum_measurement);

/*
 * returns non-zero if packet should be dropped
 */
int
il_set_decrypted_flag(struct il_priv *il, struct ieee80211_hdr *hdr,
		      u32 decrypt_res, struct ieee80211_rx_status *stats)
{
	u16 fc = le16_to_cpu(hdr->frame_control);

	/*
	 * All contexts have the same setting here due to it being
	 * a module parameter, so OK to check any context.
	 */
	if (il->active.filter_flags & RXON_FILTER_DIS_DECRYPT_MSK)
		return 0;

	if (!(fc & IEEE80211_FCTL_PROTECTED))
		return 0;

	D_RX("decrypt_res:0x%x\n", decrypt_res);
	switch (decrypt_res & RX_RES_STATUS_SEC_TYPE_MSK) {
	case RX_RES_STATUS_SEC_TYPE_TKIP:
		/* The uCode has got a bad phase 1 Key, pushes the packet.
		 * Decryption will be done in SW. */
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_KEY_TTAK)
			break;

	case RX_RES_STATUS_SEC_TYPE_WEP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_ICV_MIC) {
			/* bad ICV, the packet is destroyed since the
			 * decryption is inplace, drop it */
			D_RX("Packet destroyed\n");
			return -1;
		}
	case RX_RES_STATUS_SEC_TYPE_CCMP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_DECRYPT_OK) {
			D_RX("hw decrypt successfully!!!\n");
			stats->flag |= RX_FLAG_DECRYPTED;
		}
		break;

	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL(il_set_decrypted_flag);

/**
 * il_txq_update_write_ptr - Send new write idx to hardware
 */
void
il_txq_update_write_ptr(struct il_priv *il, struct il_tx_queue *txq)
{
	u32 reg = 0;
	int txq_id = txq->q.id;

	if (txq->need_update == 0)
		return;

	/* if we're trying to save power */
	if (test_bit(S_POWER_PMI, &il->status)) {
		/* wake up nic if it's powered down ...
		 * uCode will wake up, and interrupt us again, so next
		 * time we'll skip this part. */
		reg = _il_rd(il, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			D_INFO("Tx queue %d requesting wakeup," " GP1 = 0x%x\n",
			       txq_id, reg);
			il_set_bit(il, CSR_GP_CNTRL,
				   CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			return;
		}

		il_wr(il, HBUS_TARG_WRPTR, txq->q.write_ptr | (txq_id << 8));

		/*
		 * else not in power-save mode,
		 * uCode will never sleep when we're
		 * trying to tx (during RFKILL, we're not trying to tx).
		 */
	} else
		_il_wr(il, HBUS_TARG_WRPTR, txq->q.write_ptr | (txq_id << 8));
	txq->need_update = 0;
}
EXPORT_SYMBOL(il_txq_update_write_ptr);

/**
 * il_tx_queue_unmap -  Unmap any remaining DMA mappings and free skb's
 */
void
il_tx_queue_unmap(struct il_priv *il, int txq_id)
{
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct il_queue *q = &txq->q;

	if (q->n_bd == 0)
		return;

	while (q->write_ptr != q->read_ptr) {
		il->ops->txq_free_tfd(il, txq);
		q->read_ptr = il_queue_inc_wrap(q->read_ptr, q->n_bd);
	}
}
EXPORT_SYMBOL(il_tx_queue_unmap);

/**
 * il_tx_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
void
il_tx_queue_free(struct il_priv *il, int txq_id)
{
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct device *dev = &il->pci_dev->dev;
	int i;

	il_tx_queue_unmap(il, txq_id);

	/* De-alloc array of command/tx buffers */
	for (i = 0; i < TFD_TX_CMD_SLOTS; i++)
		kfree(txq->cmd[i]);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd)
		dma_free_coherent(dev, il->hw_params.tfd_size * txq->q.n_bd,
				  txq->tfds, txq->q.dma_addr);

	/* De-alloc array of per-TFD driver data */
	kfree(txq->skbs);
	txq->skbs = NULL;

	/* deallocate arrays */
	kfree(txq->cmd);
	kfree(txq->meta);
	txq->cmd = NULL;
	txq->meta = NULL;

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}
EXPORT_SYMBOL(il_tx_queue_free);

/**
 * il_cmd_queue_unmap - Unmap any remaining DMA mappings from command queue
 */
void
il_cmd_queue_unmap(struct il_priv *il)
{
	struct il_tx_queue *txq = &il->txq[il->cmd_queue];
	struct il_queue *q = &txq->q;
	int i;

	if (q->n_bd == 0)
		return;

	while (q->read_ptr != q->write_ptr) {
		i = il_get_cmd_idx(q, q->read_ptr, 0);

		if (txq->meta[i].flags & CMD_MAPPED) {
			pci_unmap_single(il->pci_dev,
					 dma_unmap_addr(&txq->meta[i], mapping),
					 dma_unmap_len(&txq->meta[i], len),
					 PCI_DMA_BIDIRECTIONAL);
			txq->meta[i].flags = 0;
		}

		q->read_ptr = il_queue_inc_wrap(q->read_ptr, q->n_bd);
	}

	i = q->n_win;
	if (txq->meta[i].flags & CMD_MAPPED) {
		pci_unmap_single(il->pci_dev,
				 dma_unmap_addr(&txq->meta[i], mapping),
				 dma_unmap_len(&txq->meta[i], len),
				 PCI_DMA_BIDIRECTIONAL);
		txq->meta[i].flags = 0;
	}
}
EXPORT_SYMBOL(il_cmd_queue_unmap);

/**
 * il_cmd_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
void
il_cmd_queue_free(struct il_priv *il)
{
	struct il_tx_queue *txq = &il->txq[il->cmd_queue];
	struct device *dev = &il->pci_dev->dev;
	int i;

	il_cmd_queue_unmap(il);

	/* De-alloc array of command/tx buffers */
	for (i = 0; i <= TFD_CMD_SLOTS; i++)
		kfree(txq->cmd[i]);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd)
		dma_free_coherent(dev, il->hw_params.tfd_size * txq->q.n_bd,
				  txq->tfds, txq->q.dma_addr);

	/* deallocate arrays */
	kfree(txq->cmd);
	kfree(txq->meta);
	txq->cmd = NULL;
	txq->meta = NULL;

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}
EXPORT_SYMBOL(il_cmd_queue_free);

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 * See more detailed info in 4965.h.
 ***************************************************/

int
il_queue_space(const struct il_queue *q)
{
	int s = q->read_ptr - q->write_ptr;

	if (q->read_ptr > q->write_ptr)
		s -= q->n_bd;

	if (s <= 0)
		s += q->n_win;
	/* keep some reserve to not confuse empty and full situations */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}
EXPORT_SYMBOL(il_queue_space);


/**
 * il_queue_init - Initialize queue's high/low-water and read/write idxes
 */
static int
il_queue_init(struct il_priv *il, struct il_queue *q, int slots, u32 id)
{
	/*
	 * TFD_QUEUE_SIZE_MAX must be power-of-two size, otherwise
	 * il_queue_inc_wrap and il_queue_dec_wrap are broken.
	 */
	BUILD_BUG_ON(TFD_QUEUE_SIZE_MAX & (TFD_QUEUE_SIZE_MAX - 1));
	/* FIXME: remove q->n_bd */
	q->n_bd = TFD_QUEUE_SIZE_MAX;

	q->n_win = slots;
	q->id = id;

	/* slots_must be power-of-two size, otherwise
	 * il_get_cmd_idx is broken. */
	BUG_ON(!is_power_of_2(slots));

	q->low_mark = q->n_win / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_win / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = q->read_ptr = 0;

	return 0;
}

/**
 * il_tx_queue_alloc - Alloc driver data and TFD CB for one Tx/cmd queue
 */
static int
il_tx_queue_alloc(struct il_priv *il, struct il_tx_queue *txq, u32 id)
{
	struct device *dev = &il->pci_dev->dev;
	size_t tfd_sz = il->hw_params.tfd_size * TFD_QUEUE_SIZE_MAX;

	/* Driver ilate data, only for Tx (not command) queues,
	 * not shared with device. */
	if (id != il->cmd_queue) {
		txq->skbs = kcalloc(TFD_QUEUE_SIZE_MAX, sizeof(struct skb *),
				    GFP_KERNEL);
		if (!txq->skbs) {
			IL_ERR("Fail to alloc skbs\n");
			goto error;
		}
	} else
		txq->skbs = NULL;

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->tfds =
	    dma_alloc_coherent(dev, tfd_sz, &txq->q.dma_addr, GFP_KERNEL);
	if (!txq->tfds) {
		IL_ERR("Fail to alloc TFDs\n");
		goto error;
	}
	txq->q.id = id;

	return 0;

error:
	kfree(txq->skbs);
	txq->skbs = NULL;

	return -ENOMEM;
}

/**
 * il_tx_queue_init - Allocate and initialize one tx/cmd queue
 */
int
il_tx_queue_init(struct il_priv *il, u32 txq_id)
{
	int i, len, ret;
	int slots, actual_slots;
	struct il_tx_queue *txq = &il->txq[txq_id];

	/*
	 * Alloc buffer array for commands (Tx or other types of commands).
	 * For the command queue (#4/#9), allocate command space + one big
	 * command for scan, since scan command is very huge; the system will
	 * not have two scans at the same time, so only one is needed.
	 * For normal Tx queues (all other queues), no super-size command
	 * space is needed.
	 */
	if (txq_id == il->cmd_queue) {
		slots = TFD_CMD_SLOTS;
		actual_slots = slots + 1;
	} else {
		slots = TFD_TX_CMD_SLOTS;
		actual_slots = slots;
	}

	txq->meta =
	    kzalloc(sizeof(struct il_cmd_meta) * actual_slots, GFP_KERNEL);
	txq->cmd =
	    kzalloc(sizeof(struct il_device_cmd *) * actual_slots, GFP_KERNEL);

	if (!txq->meta || !txq->cmd)
		goto out_free_arrays;

	len = sizeof(struct il_device_cmd);
	for (i = 0; i < actual_slots; i++) {
		/* only happens for cmd queue */
		if (i == slots)
			len = IL_MAX_CMD_SIZE;

		txq->cmd[i] = kmalloc(len, GFP_KERNEL);
		if (!txq->cmd[i])
			goto err;
	}

	/* Alloc driver data array and TFD circular buffer */
	ret = il_tx_queue_alloc(il, txq, txq_id);
	if (ret)
		goto err;

	txq->need_update = 0;

	/*
	 * For the default queues 0-3, set up the swq_id
	 * already -- all others need to get one later
	 * (if they need one at all).
	 */
	if (txq_id < 4)
		il_set_swq_id(txq, txq_id, txq_id);

	/* Initialize queue's high/low-water marks, and head/tail idxes */
	il_queue_init(il, &txq->q, slots, txq_id);

	/* Tell device where to find queue */
	il->ops->txq_init(il, txq);

	return 0;
err:
	for (i = 0; i < actual_slots; i++)
		kfree(txq->cmd[i]);
out_free_arrays:
	kfree(txq->meta);
	kfree(txq->cmd);

	return -ENOMEM;
}
EXPORT_SYMBOL(il_tx_queue_init);

void
il_tx_queue_reset(struct il_priv *il, u32 txq_id)
{
	int slots, actual_slots;
	struct il_tx_queue *txq = &il->txq[txq_id];

	if (txq_id == il->cmd_queue) {
		slots = TFD_CMD_SLOTS;
		actual_slots = TFD_CMD_SLOTS + 1;
	} else {
		slots = TFD_TX_CMD_SLOTS;
		actual_slots = TFD_TX_CMD_SLOTS;
	}

	memset(txq->meta, 0, sizeof(struct il_cmd_meta) * actual_slots);
	txq->need_update = 0;

	/* Initialize queue's high/low-water marks, and head/tail idxes */
	il_queue_init(il, &txq->q, slots, txq_id);

	/* Tell device where to find queue */
	il->ops->txq_init(il, txq);
}
EXPORT_SYMBOL(il_tx_queue_reset);

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/**
 * il_enqueue_hcmd - enqueue a uCode command
 * @il: device ilate data point
 * @cmd: a point to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation is
 * failed. On success, it turns the idx (> 0) of command in the
 * command queue.
 */
int
il_enqueue_hcmd(struct il_priv *il, struct il_host_cmd *cmd)
{
	struct il_tx_queue *txq = &il->txq[il->cmd_queue];
	struct il_queue *q = &txq->q;
	struct il_device_cmd *out_cmd;
	struct il_cmd_meta *out_meta;
	dma_addr_t phys_addr;
	unsigned long flags;
	int len;
	u32 idx;
	u16 fix_size;

	cmd->len = il->ops->get_hcmd_size(cmd->id, cmd->len);
	fix_size = (u16) (cmd->len + sizeof(out_cmd->hdr));

	/* If any of the command structures end up being larger than
	 * the TFD_MAX_PAYLOAD_SIZE, and it sent as a 'small' command then
	 * we will need to increase the size of the TFD entries
	 * Also, check to see if command buffer should not exceed the size
	 * of device_cmd and max_cmd_size. */
	BUG_ON((fix_size > TFD_MAX_PAYLOAD_SIZE) &&
	       !(cmd->flags & CMD_SIZE_HUGE));
	BUG_ON(fix_size > IL_MAX_CMD_SIZE);

	if (il_is_rfkill(il) || il_is_ctkill(il)) {
		IL_WARN("Not sending command - %s KILL\n",
			il_is_rfkill(il) ? "RF" : "CT");
		return -EIO;
	}

	spin_lock_irqsave(&il->hcmd_lock, flags);

	if (il_queue_space(q) < ((cmd->flags & CMD_ASYNC) ? 2 : 1)) {
		spin_unlock_irqrestore(&il->hcmd_lock, flags);

		IL_ERR("Restarting adapter due to command queue full\n");
		queue_work(il->workqueue, &il->restart);
		return -ENOSPC;
	}

	idx = il_get_cmd_idx(q, q->write_ptr, cmd->flags & CMD_SIZE_HUGE);
	out_cmd = txq->cmd[idx];
	out_meta = &txq->meta[idx];

	if (WARN_ON(out_meta->flags & CMD_MAPPED)) {
		spin_unlock_irqrestore(&il->hcmd_lock, flags);
		return -ENOSPC;
	}

	memset(out_meta, 0, sizeof(*out_meta));	/* re-initialize to NULL */
	out_meta->flags = cmd->flags | CMD_MAPPED;
	if (cmd->flags & CMD_WANT_SKB)
		out_meta->source = cmd;
	if (cmd->flags & CMD_ASYNC)
		out_meta->callback = cmd->callback;

	out_cmd->hdr.cmd = cmd->id;
	memcpy(&out_cmd->cmd.payload, cmd->data, cmd->len);

	/* At this point, the out_cmd now has all of the incoming cmd
	 * information */

	out_cmd->hdr.flags = 0;
	out_cmd->hdr.sequence =
	    cpu_to_le16(QUEUE_TO_SEQ(il->cmd_queue) | IDX_TO_SEQ(q->write_ptr));
	if (cmd->flags & CMD_SIZE_HUGE)
		out_cmd->hdr.sequence |= SEQ_HUGE_FRAME;
	len = sizeof(struct il_device_cmd);
	if (idx == TFD_CMD_SLOTS)
		len = IL_MAX_CMD_SIZE;

#ifdef CONFIG_IWLEGACY_DEBUG
	switch (out_cmd->hdr.cmd) {
	case C_TX_LINK_QUALITY_CMD:
	case C_SENSITIVITY:
		D_HC_DUMP("Sending command %s (#%x), seq: 0x%04X, "
			  "%d bytes at %d[%d]:%d\n",
			  il_get_cmd_string(out_cmd->hdr.cmd), out_cmd->hdr.cmd,
			  le16_to_cpu(out_cmd->hdr.sequence), fix_size,
			  q->write_ptr, idx, il->cmd_queue);
		break;
	default:
		D_HC("Sending command %s (#%x), seq: 0x%04X, "
		     "%d bytes at %d[%d]:%d\n",
		     il_get_cmd_string(out_cmd->hdr.cmd), out_cmd->hdr.cmd,
		     le16_to_cpu(out_cmd->hdr.sequence), fix_size, q->write_ptr,
		     idx, il->cmd_queue);
	}
#endif
	txq->need_update = 1;

	if (il->ops->txq_update_byte_cnt_tbl)
		/* Set up entry in queue's byte count circular buffer */
		il->ops->txq_update_byte_cnt_tbl(il, txq, 0);

	phys_addr =
	    pci_map_single(il->pci_dev, &out_cmd->hdr, fix_size,
			   PCI_DMA_BIDIRECTIONAL);
	dma_unmap_addr_set(out_meta, mapping, phys_addr);
	dma_unmap_len_set(out_meta, len, fix_size);

	il->ops->txq_attach_buf_to_tfd(il, txq, phys_addr, fix_size, 1,
					    U32_PAD(cmd->len));

	/* Increment and update queue's write idx */
	q->write_ptr = il_queue_inc_wrap(q->write_ptr, q->n_bd);
	il_txq_update_write_ptr(il, txq);

	spin_unlock_irqrestore(&il->hcmd_lock, flags);
	return idx;
}

/**
 * il_hcmd_queue_reclaim - Reclaim TX command queue entries already Tx'd
 *
 * When FW advances 'R' idx, all entries between old and new 'R' idx
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
static void
il_hcmd_queue_reclaim(struct il_priv *il, int txq_id, int idx, int cmd_idx)
{
	struct il_tx_queue *txq = &il->txq[txq_id];
	struct il_queue *q = &txq->q;
	int nfreed = 0;

	if (idx >= q->n_bd || il_queue_used(q, idx) == 0) {
		IL_ERR("Read idx for DMA queue txq id (%d), idx %d, "
		       "is out of range [0-%d] %d %d.\n", txq_id, idx, q->n_bd,
		       q->write_ptr, q->read_ptr);
		return;
	}

	for (idx = il_queue_inc_wrap(idx, q->n_bd); q->read_ptr != idx;
	     q->read_ptr = il_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		if (nfreed++ > 0) {
			IL_ERR("HCMD skipped: idx (%d) %d %d\n", idx,
			       q->write_ptr, q->read_ptr);
			queue_work(il->workqueue, &il->restart);
		}

	}
}

/**
 * il_tx_cmd_complete - Pull unused buffers off the queue and reclaim them
 * @rxb: Rx buffer to reclaim
 *
 * If an Rx buffer has an async callback associated with it the callback
 * will be executed.  The attached skb (if present) will only be freed
 * if the callback returns 1
 */
void
il_tx_cmd_complete(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int idx = SEQ_TO_IDX(sequence);
	int cmd_idx;
	bool huge = !!(pkt->hdr.sequence & SEQ_HUGE_FRAME);
	struct il_device_cmd *cmd;
	struct il_cmd_meta *meta;
	struct il_tx_queue *txq = &il->txq[il->cmd_queue];
	unsigned long flags;

	/* If a Tx command is being handled and it isn't in the actual
	 * command queue then there a command routing bug has been introduced
	 * in the queue management code. */
	if (WARN
	    (txq_id != il->cmd_queue,
	     "wrong command queue %d (should be %d), sequence 0x%X readp=%d writep=%d\n",
	     txq_id, il->cmd_queue, sequence, il->txq[il->cmd_queue].q.read_ptr,
	     il->txq[il->cmd_queue].q.write_ptr)) {
		il_print_hex_error(il, pkt, 32);
		return;
	}

	cmd_idx = il_get_cmd_idx(&txq->q, idx, huge);
	cmd = txq->cmd[cmd_idx];
	meta = &txq->meta[cmd_idx];

	txq->time_stamp = jiffies;

	pci_unmap_single(il->pci_dev, dma_unmap_addr(meta, mapping),
			 dma_unmap_len(meta, len), PCI_DMA_BIDIRECTIONAL);

	/* Input error checking is done when commands are added to queue. */
	if (meta->flags & CMD_WANT_SKB) {
		meta->source->reply_page = (unsigned long)rxb_addr(rxb);
		rxb->page = NULL;
	} else if (meta->callback)
		meta->callback(il, cmd, pkt);

	spin_lock_irqsave(&il->hcmd_lock, flags);

	il_hcmd_queue_reclaim(il, txq_id, idx, cmd_idx);

	if (!(meta->flags & CMD_ASYNC)) {
		clear_bit(S_HCMD_ACTIVE, &il->status);
		D_INFO("Clearing HCMD_ACTIVE for command %s\n",
		       il_get_cmd_string(cmd->hdr.cmd));
		wake_up(&il->wait_command_queue);
	}

	/* Mark as unmapped */
	meta->flags = 0;

	spin_unlock_irqrestore(&il->hcmd_lock, flags);
}
EXPORT_SYMBOL(il_tx_cmd_complete);

MODULE_DESCRIPTION("iwl-legacy: common functions for 3945 and 4965");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

/*
 * set bt_coex_active to true, uCode will do kill/defer
 * every time the priority line is asserted (BT is sending signals on the
 * priority line in the PCIx).
 * set bt_coex_active to false, uCode will ignore the BT activity and
 * perform the normal operation
 *
 * User might experience transmit issue on some platform due to WiFi/BT
 * co-exist problem. The possible behaviors are:
 *   Able to scan and finding all the available AP
 *   Not able to associate with any AP
 * On those platforms, WiFi communication can be restored by set
 * "bt_coex_active" module parameter to "false"
 *
 * default: bt_coex_active = true (BT_COEX_ENABLE)
 */
static bool bt_coex_active = true;
module_param(bt_coex_active, bool, S_IRUGO);
MODULE_PARM_DESC(bt_coex_active, "enable wifi/bluetooth co-exist");

u32 il_debug_level;
EXPORT_SYMBOL(il_debug_level);

const u8 il_bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
EXPORT_SYMBOL(il_bcast_addr);

#define MAX_BIT_RATE_40_MHZ 150	/* Mbps */
#define MAX_BIT_RATE_20_MHZ 72	/* Mbps */
static void
il_init_ht_hw_capab(const struct il_priv *il,
		    struct ieee80211_sta_ht_cap *ht_info,
		    enum ieee80211_band band)
{
	u16 max_bit_rate = 0;
	u8 rx_chains_num = il->hw_params.rx_chains_num;
	u8 tx_chains_num = il->hw_params.tx_chains_num;

	ht_info->cap = 0;
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	ht_info->ht_supported = true;

	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (il->hw_params.ht40_channel & BIT(band)) {
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
		ht_info->mcs.rx_mask[4] = 0x01;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	if (il->cfg->mod_params->amsdu_size_8K)
		ht_info->cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	ht_info->ampdu_factor = CFG_HT_RX_AMPDU_FACTOR_DEF;
	ht_info->ampdu_density = CFG_HT_MPDU_DENSITY_DEF;

	ht_info->mcs.rx_mask[0] = 0xFF;
	if (rx_chains_num >= 2)
		ht_info->mcs.rx_mask[1] = 0xFF;
	if (rx_chains_num >= 3)
		ht_info->mcs.rx_mask[2] = 0xFF;

	/* Highest supported Rx data rate */
	max_bit_rate *= rx_chains_num;
	WARN_ON(max_bit_rate & ~IEEE80211_HT_MCS_RX_HIGHEST_MASK);
	ht_info->mcs.rx_highest = cpu_to_le16(max_bit_rate);

	/* Tx MCS capabilities */
	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (tx_chains_num != rx_chains_num) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |=
		    ((tx_chains_num -
		      1) << IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}
}

/**
 * il_init_geos - Initialize mac80211's geo/channel info based from eeprom
 */
int
il_init_geos(struct il_priv *il)
{
	struct il_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;
	s8 max_tx_power = 0;

	if (il->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    il->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		D_INFO("Geography modes already initialized.\n");
		set_bit(S_GEO_CONFIGURED, &il->status);
		return 0;
	}

	channels =
	    kzalloc(sizeof(struct ieee80211_channel) * il->channel_count,
		    GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates =
	    kzalloc((sizeof(struct ieee80211_rate) * RATE_COUNT_LEGACY),
		    GFP_KERNEL);
	if (!rates) {
		kfree(channels);
		return -ENOMEM;
	}

	/* 5.2GHz channels start after the 2.4GHz channels */
	sband = &il->bands[IEEE80211_BAND_5GHZ];
	sband->channels = &channels[ARRAY_SIZE(il_eeprom_band_1)];
	/* just OFDM */
	sband->bitrates = &rates[IL_FIRST_OFDM_RATE];
	sband->n_bitrates = RATE_COUNT_LEGACY - IL_FIRST_OFDM_RATE;

	if (il->cfg->sku & IL_SKU_N)
		il_init_ht_hw_capab(il, &sband->ht_cap, IEEE80211_BAND_5GHZ);

	sband = &il->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = RATE_COUNT_LEGACY;

	if (il->cfg->sku & IL_SKU_N)
		il_init_ht_hw_capab(il, &sband->ht_cap, IEEE80211_BAND_2GHZ);

	il->ieee_channels = channels;
	il->ieee_rates = rates;

	for (i = 0; i < il->channel_count; i++) {
		ch = &il->channel_info[i];

		if (!il_is_channel_valid(ch))
			continue;

		sband = &il->bands[ch->band];

		geo_ch = &sband->channels[sband->n_channels++];

		geo_ch->center_freq =
		    ieee80211_channel_to_frequency(ch->channel, ch->band);
		geo_ch->max_power = ch->max_power_avg;
		geo_ch->max_antenna_gain = 0xff;
		geo_ch->hw_value = ch->channel;

		if (il_is_channel_valid(ch)) {
			if (!(ch->flags & EEPROM_CHANNEL_IBSS))
				geo_ch->flags |= IEEE80211_CHAN_NO_IBSS;

			if (!(ch->flags & EEPROM_CHANNEL_ACTIVE))
				geo_ch->flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			if (ch->flags & EEPROM_CHANNEL_RADAR)
				geo_ch->flags |= IEEE80211_CHAN_RADAR;

			geo_ch->flags |= ch->ht40_extension_channel;

			if (ch->max_power_avg > max_tx_power)
				max_tx_power = ch->max_power_avg;
		} else {
			geo_ch->flags |= IEEE80211_CHAN_DISABLED;
		}

		D_INFO("Channel %d Freq=%d[%sGHz] %s flag=0x%X\n", ch->channel,
		       geo_ch->center_freq,
		       il_is_channel_a_band(ch) ? "5.2" : "2.4",
		       geo_ch->
		       flags & IEEE80211_CHAN_DISABLED ? "restricted" : "valid",
		       geo_ch->flags);
	}

	il->tx_power_device_lmt = max_tx_power;
	il->tx_power_user_lmt = max_tx_power;
	il->tx_power_next = max_tx_power;

	if (il->bands[IEEE80211_BAND_5GHZ].n_channels == 0 &&
	    (il->cfg->sku & IL_SKU_A)) {
		IL_INFO("Incorrectly detected BG card as ABG. "
			"Please send your PCI ID 0x%04X:0x%04X to maintainer.\n",
			il->pci_dev->device, il->pci_dev->subsystem_device);
		il->cfg->sku &= ~IL_SKU_A;
	}

	IL_INFO("Tunable channels: %d 802.11bg, %d 802.11a channels\n",
		il->bands[IEEE80211_BAND_2GHZ].n_channels,
		il->bands[IEEE80211_BAND_5GHZ].n_channels);

	set_bit(S_GEO_CONFIGURED, &il->status);

	return 0;
}
EXPORT_SYMBOL(il_init_geos);

/*
 * il_free_geos - undo allocations in il_init_geos
 */
void
il_free_geos(struct il_priv *il)
{
	kfree(il->ieee_channels);
	kfree(il->ieee_rates);
	clear_bit(S_GEO_CONFIGURED, &il->status);
}
EXPORT_SYMBOL(il_free_geos);

static bool
il_is_channel_extension(struct il_priv *il, enum ieee80211_band band,
			u16 channel, u8 extension_chan_offset)
{
	const struct il_channel_info *ch_info;

	ch_info = il_get_channel_info(il, band, channel);
	if (!il_is_channel_valid(ch_info))
		return false;

	if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
		return !(ch_info->
			 ht40_extension_channel & IEEE80211_CHAN_NO_HT40PLUS);
	else if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW)
		return !(ch_info->
			 ht40_extension_channel & IEEE80211_CHAN_NO_HT40MINUS);

	return false;
}

bool
il_is_ht40_tx_allowed(struct il_priv *il, struct ieee80211_sta_ht_cap *ht_cap)
{
	if (!il->ht.enabled || !il->ht.is_40mhz)
		return false;

	/*
	 * We do not check for IEEE80211_HT_CAP_SUP_WIDTH_20_40
	 * the bit will not set if it is pure 40MHz case
	 */
	if (ht_cap && !ht_cap->ht_supported)
		return false;

#ifdef CONFIG_IWLEGACY_DEBUGFS
	if (il->disable_ht40)
		return false;
#endif

	return il_is_channel_extension(il, il->band,
				       le16_to_cpu(il->staging.channel),
				       il->ht.extension_chan_offset);
}
EXPORT_SYMBOL(il_is_ht40_tx_allowed);

static u16
il_adjust_beacon_interval(u16 beacon_val, u16 max_beacon_val)
{
	u16 new_val;
	u16 beacon_factor;

	/*
	 * If mac80211 hasn't given us a beacon interval, program
	 * the default into the device.
	 */
	if (!beacon_val)
		return DEFAULT_BEACON_INTERVAL;

	/*
	 * If the beacon interval we obtained from the peer
	 * is too large, we'll have to wake up more often
	 * (and in IBSS case, we'll beacon too much)
	 *
	 * For example, if max_beacon_val is 4096, and the
	 * requested beacon interval is 7000, we'll have to
	 * use 3500 to be able to wake up on the beacons.
	 *
	 * This could badly influence beacon detection stats.
	 */

	beacon_factor = (beacon_val + max_beacon_val) / max_beacon_val;
	new_val = beacon_val / beacon_factor;

	if (!new_val)
		new_val = max_beacon_val;

	return new_val;
}

int
il_send_rxon_timing(struct il_priv *il)
{
	u64 tsf;
	s32 interval_tm, rem;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int;
	struct ieee80211_vif *vif = il->vif;

	conf = &il->hw->conf;

	lockdep_assert_held(&il->mutex);

	memset(&il->timing, 0, sizeof(struct il_rxon_time_cmd));

	il->timing.timestamp = cpu_to_le64(il->timestamp);
	il->timing.listen_interval = cpu_to_le16(conf->listen_interval);

	beacon_int = vif ? vif->bss_conf.beacon_int : 0;

	/*
	 * TODO: For IBSS we need to get atim_win from mac80211,
	 *       for now just always use 0
	 */
	il->timing.atim_win = 0;

	beacon_int =
	    il_adjust_beacon_interval(beacon_int,
				      il->hw_params.max_beacon_itrvl *
				      TIME_UNIT);
	il->timing.beacon_interval = cpu_to_le16(beacon_int);

	tsf = il->timestamp;	/* tsf is modifed by do_div: copy it */
	interval_tm = beacon_int * TIME_UNIT;
	rem = do_div(tsf, interval_tm);
	il->timing.beacon_init_val = cpu_to_le32(interval_tm - rem);

	il->timing.dtim_period = vif ? (vif->bss_conf.dtim_period ? : 1) : 1;

	D_ASSOC("beacon interval %d beacon timer %d beacon tim %d\n",
		le16_to_cpu(il->timing.beacon_interval),
		le32_to_cpu(il->timing.beacon_init_val),
		le16_to_cpu(il->timing.atim_win));

	return il_send_cmd_pdu(il, C_RXON_TIMING, sizeof(il->timing),
			       &il->timing);
}
EXPORT_SYMBOL(il_send_rxon_timing);

void
il_set_rxon_hwcrypto(struct il_priv *il, int hw_decrypt)
{
	struct il_rxon_cmd *rxon = &il->staging;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}
EXPORT_SYMBOL(il_set_rxon_hwcrypto);

/* validate RXON structure is valid */
int
il_check_rxon_cmd(struct il_priv *il)
{
	struct il_rxon_cmd *rxon = &il->staging;
	bool error = false;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		if (rxon->flags & RXON_FLG_TGJ_NARROW_BAND_MSK) {
			IL_WARN("check 2.4G: wrong narrow\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_RADAR_DETECT_MSK) {
			IL_WARN("check 2.4G: wrong radar\n");
			error = true;
		}
	} else {
		if (!(rxon->flags & RXON_FLG_SHORT_SLOT_MSK)) {
			IL_WARN("check 5.2G: not short slot!\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_CCK_MSK) {
			IL_WARN("check 5.2G: CCK!\n");
			error = true;
		}
	}
	if ((rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1) {
		IL_WARN("mac/bssid mcast!\n");
		error = true;
	}

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	if ((rxon->ofdm_basic_rates & RATE_6M_MASK) == 0 &&
	    (rxon->cck_basic_rates & RATE_1M_MASK) == 0) {
		IL_WARN("neither 1 nor 6 are basic\n");
		error = true;
	}

	if (le16_to_cpu(rxon->assoc_id) > 2007) {
		IL_WARN("aid > 2007\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK)) ==
	    (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK)) {
		IL_WARN("CCK and short slot\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK)) ==
	    (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK)) {
		IL_WARN("CCK and auto detect");
		error = true;
	}

	if ((rxon->
	     flags & (RXON_FLG_AUTO_DETECT_MSK | RXON_FLG_TGG_PROTECT_MSK)) ==
	    RXON_FLG_TGG_PROTECT_MSK) {
		IL_WARN("TGg but no auto-detect\n");
		error = true;
	}

	if (error)
		IL_WARN("Tuning to channel %d\n", le16_to_cpu(rxon->channel));

	if (error) {
		IL_ERR("Invalid RXON\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(il_check_rxon_cmd);

/**
 * il_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @il: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
int
il_full_rxon_required(struct il_priv *il)
{
	const struct il_rxon_cmd *staging = &il->staging;
	const struct il_rxon_cmd *active = &il->active;

#define CHK(cond)							\
	if ((cond)) {							\
		D_INFO("need full RXON - " #cond "\n");	\
		return 1;						\
	}

#define CHK_NEQ(c1, c2)						\
	if ((c1) != (c2)) {					\
		D_INFO("need full RXON - "	\
			       #c1 " != " #c2 " - %d != %d\n",	\
			       (c1), (c2));			\
		return 1;					\
	}

	/* These items are only settable from the full RXON command */
	CHK(!il_is_associated(il));
	CHK(!ether_addr_equal(staging->bssid_addr, active->bssid_addr));
	CHK(!ether_addr_equal(staging->node_addr, active->node_addr));
	CHK(!ether_addr_equal(staging->wlap_bssid_addr,
			      active->wlap_bssid_addr));
	CHK_NEQ(staging->dev_type, active->dev_type);
	CHK_NEQ(staging->channel, active->channel);
	CHK_NEQ(staging->air_propagation, active->air_propagation);
	CHK_NEQ(staging->ofdm_ht_single_stream_basic_rates,
		active->ofdm_ht_single_stream_basic_rates);
	CHK_NEQ(staging->ofdm_ht_dual_stream_basic_rates,
		active->ofdm_ht_dual_stream_basic_rates);
	CHK_NEQ(staging->assoc_id, active->assoc_id);

	/* flags, filter_flags, ofdm_basic_rates, and cck_basic_rates can
	 * be updated with the RXON_ASSOC command -- however only some
	 * flag transitions are allowed using RXON_ASSOC */

	/* Check if we are not switching bands */
	CHK_NEQ(staging->flags & RXON_FLG_BAND_24G_MSK,
		active->flags & RXON_FLG_BAND_24G_MSK);

	/* Check if we are switching association toggle */
	CHK_NEQ(staging->filter_flags & RXON_FILTER_ASSOC_MSK,
		active->filter_flags & RXON_FILTER_ASSOC_MSK);

#undef CHK
#undef CHK_NEQ

	return 0;
}
EXPORT_SYMBOL(il_full_rxon_required);

u8
il_get_lowest_plcp(struct il_priv *il)
{
	/*
	 * Assign the lowest rate -- should really get this from
	 * the beacon skb from mac80211.
	 */
	if (il->staging.flags & RXON_FLG_BAND_24G_MSK)
		return RATE_1M_PLCP;
	else
		return RATE_6M_PLCP;
}
EXPORT_SYMBOL(il_get_lowest_plcp);

static void
_il_set_rxon_ht(struct il_priv *il, struct il_ht_config *ht_conf)
{
	struct il_rxon_cmd *rxon = &il->staging;

	if (!il->ht.enabled) {
		rxon->flags &=
		    ~(RXON_FLG_CHANNEL_MODE_MSK |
		      RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK | RXON_FLG_HT40_PROT_MSK
		      | RXON_FLG_HT_PROT_MSK);
		return;
	}

	rxon->flags |=
	    cpu_to_le32(il->ht.protection << RXON_FLG_HT_OPERATING_MODE_POS);

	/* Set up channel bandwidth:
	 * 20 MHz only, 20/40 mixed or pure 40 if ht40 ok */
	/* clear the HT channel mode before set the mode */
	rxon->flags &=
	    ~(RXON_FLG_CHANNEL_MODE_MSK | RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
	if (il_is_ht40_tx_allowed(il, NULL)) {
		/* pure ht40 */
		if (il->ht.protection == IEEE80211_HT_OP_MODE_PROTECTION_20MHZ) {
			rxon->flags |= RXON_FLG_CHANNEL_MODE_PURE_40;
			/* Note: control channel is opposite of extension channel */
			switch (il->ht.extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &=
				    ~RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			}
		} else {
			/* Note: control channel is opposite of extension channel */
			switch (il->ht.extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &=
				    ~(RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |= RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_NONE:
			default:
				/* channel location only valid if in Mixed mode */
				IL_ERR("invalid extension channel offset\n");
				break;
			}
		}
	} else {
		rxon->flags |= RXON_FLG_CHANNEL_MODE_LEGACY;
	}

	if (il->ops->set_rxon_chain)
		il->ops->set_rxon_chain(il);

	D_ASSOC("rxon flags 0x%X operation mode :0x%X "
		"extension channel offset 0x%x\n", le32_to_cpu(rxon->flags),
		il->ht.protection, il->ht.extension_chan_offset);
}

void
il_set_rxon_ht(struct il_priv *il, struct il_ht_config *ht_conf)
{
	_il_set_rxon_ht(il, ht_conf);
}
EXPORT_SYMBOL(il_set_rxon_ht);

/* Return valid, unused, channel for a passive scan to reset the RF */
u8
il_get_single_channel_number(struct il_priv *il, enum ieee80211_band band)
{
	const struct il_channel_info *ch_info;
	int i;
	u8 channel = 0;
	u8 min, max;

	if (band == IEEE80211_BAND_5GHZ) {
		min = 14;
		max = il->channel_count;
	} else {
		min = 0;
		max = 14;
	}

	for (i = min; i < max; i++) {
		channel = il->channel_info[i].channel;
		if (channel == le16_to_cpu(il->staging.channel))
			continue;

		ch_info = il_get_channel_info(il, band, channel);
		if (il_is_channel_valid(ch_info))
			break;
	}

	return channel;
}
EXPORT_SYMBOL(il_get_single_channel_number);

/**
 * il_set_rxon_channel - Set the band and channel values in staging RXON
 * @ch: requested channel as a pointer to struct ieee80211_channel

 * NOTE:  Does not commit to the hardware; it sets appropriate bit fields
 * in the staging RXON flag structure based on the ch->band
 */
int
il_set_rxon_channel(struct il_priv *il, struct ieee80211_channel *ch)
{
	enum ieee80211_band band = ch->band;
	u16 channel = ch->hw_value;

	if (le16_to_cpu(il->staging.channel) == channel && il->band == band)
		return 0;

	il->staging.channel = cpu_to_le16(channel);
	if (band == IEEE80211_BAND_5GHZ)
		il->staging.flags &= ~RXON_FLG_BAND_24G_MSK;
	else
		il->staging.flags |= RXON_FLG_BAND_24G_MSK;

	il->band = band;

	D_INFO("Staging channel set to %d [%d]\n", channel, band);

	return 0;
}
EXPORT_SYMBOL(il_set_rxon_channel);

void
il_set_flags_for_band(struct il_priv *il, enum ieee80211_band band,
		      struct ieee80211_vif *vif)
{
	if (band == IEEE80211_BAND_5GHZ) {
		il->staging.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK |
		      RXON_FLG_CCK_MSK);
		il->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from il_post_associate() */
		if (vif && vif->bss_conf.use_short_slot)
			il->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			il->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		il->staging.flags |= RXON_FLG_BAND_24G_MSK;
		il->staging.flags |= RXON_FLG_AUTO_DETECT_MSK;
		il->staging.flags &= ~RXON_FLG_CCK_MSK;
	}
}
EXPORT_SYMBOL(il_set_flags_for_band);

/*
 * initialize rxon structure with default values from eeprom
 */
void
il_connection_init_rx_config(struct il_priv *il)
{
	const struct il_channel_info *ch_info;

	memset(&il->staging, 0, sizeof(il->staging));

	if (!il->vif) {
		il->staging.dev_type = RXON_DEV_TYPE_ESS;
	} else if (il->vif->type == NL80211_IFTYPE_STATION) {
		il->staging.dev_type = RXON_DEV_TYPE_ESS;
		il->staging.filter_flags = RXON_FILTER_ACCEPT_GRP_MSK;
	} else if (il->vif->type == NL80211_IFTYPE_ADHOC) {
		il->staging.dev_type = RXON_DEV_TYPE_IBSS;
		il->staging.flags = RXON_FLG_SHORT_PREAMBLE_MSK;
		il->staging.filter_flags =
		    RXON_FILTER_BCON_AWARE_MSK | RXON_FILTER_ACCEPT_GRP_MSK;
	} else {
		IL_ERR("Unsupported interface type %d\n", il->vif->type);
		return;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(il->hw)->short_preamble)
		il->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		il->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info =
	    il_get_channel_info(il, il->band, le16_to_cpu(il->active.channel));

	if (!ch_info)
		ch_info = &il->channel_info[0];

	il->staging.channel = cpu_to_le16(ch_info->channel);
	il->band = ch_info->band;

	il_set_flags_for_band(il, il->band, il->vif);

	il->staging.ofdm_basic_rates =
	    (IL_OFDM_RATES_MASK >> IL_FIRST_OFDM_RATE) & 0xFF;
	il->staging.cck_basic_rates =
	    (IL_CCK_RATES_MASK >> IL_FIRST_CCK_RATE) & 0xF;

	/* clear both MIX and PURE40 mode flag */
	il->staging.flags &=
	    ~(RXON_FLG_CHANNEL_MODE_MIXED | RXON_FLG_CHANNEL_MODE_PURE_40);
	if (il->vif)
		memcpy(il->staging.node_addr, il->vif->addr, ETH_ALEN);

	il->staging.ofdm_ht_single_stream_basic_rates = 0xff;
	il->staging.ofdm_ht_dual_stream_basic_rates = 0xff;
}
EXPORT_SYMBOL(il_connection_init_rx_config);

void
il_set_rate(struct il_priv *il)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	int i;

	hw = il_get_hw_mode(il, il->band);
	if (!hw) {
		IL_ERR("Failed to set rate: unable to get hw mode\n");
		return;
	}

	il->active_rate = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < RATE_COUNT_LEGACY)
			il->active_rate |= (1 << rate->hw_value);
	}

	D_RATE("Set active_rate = %0x\n", il->active_rate);

	il->staging.cck_basic_rates =
	    (IL_CCK_BASIC_RATES_MASK >> IL_FIRST_CCK_RATE) & 0xF;

	il->staging.ofdm_basic_rates =
	    (IL_OFDM_BASIC_RATES_MASK >> IL_FIRST_OFDM_RATE) & 0xFF;
}
EXPORT_SYMBOL(il_set_rate);

void
il_chswitch_done(struct il_priv *il, bool is_success)
{
	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	if (test_and_clear_bit(S_CHANNEL_SWITCH_PENDING, &il->status))
		ieee80211_chswitch_done(il->vif, is_success);
}
EXPORT_SYMBOL(il_chswitch_done);

void
il_hdl_csa(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_csa_notification *csa = &(pkt->u.csa_notif);
	struct il_rxon_cmd *rxon = (void *)&il->active;

	if (!test_bit(S_CHANNEL_SWITCH_PENDING, &il->status))
		return;

	if (!le32_to_cpu(csa->status) && csa->channel == il->switch_channel) {
		rxon->channel = csa->channel;
		il->staging.channel = csa->channel;
		D_11H("CSA notif: channel %d\n", le16_to_cpu(csa->channel));
		il_chswitch_done(il, true);
	} else {
		IL_ERR("CSA notif (fail) : channel %d\n",
		       le16_to_cpu(csa->channel));
		il_chswitch_done(il, false);
	}
}
EXPORT_SYMBOL(il_hdl_csa);

#ifdef CONFIG_IWLEGACY_DEBUG
void
il_print_rx_config_cmd(struct il_priv *il)
{
	struct il_rxon_cmd *rxon = &il->staging;

	D_RADIO("RX CONFIG:\n");
	il_print_hex_dump(il, IL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	D_RADIO("u16 channel: 0x%x\n", le16_to_cpu(rxon->channel));
	D_RADIO("u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	D_RADIO("u32 filter_flags: 0x%08x\n", le32_to_cpu(rxon->filter_flags));
	D_RADIO("u8 dev_type: 0x%x\n", rxon->dev_type);
	D_RADIO("u8 ofdm_basic_rates: 0x%02x\n", rxon->ofdm_basic_rates);
	D_RADIO("u8 cck_basic_rates: 0x%02x\n", rxon->cck_basic_rates);
	D_RADIO("u8[6] node_addr: %pM\n", rxon->node_addr);
	D_RADIO("u8[6] bssid_addr: %pM\n", rxon->bssid_addr);
	D_RADIO("u16 assoc_id: 0x%x\n", le16_to_cpu(rxon->assoc_id));
}
EXPORT_SYMBOL(il_print_rx_config_cmd);
#endif
/**
 * il_irq_handle_error - called for HW or SW error interrupt from card
 */
void
il_irq_handle_error(struct il_priv *il)
{
	/* Set the FW error flag -- cleared on il_down */
	set_bit(S_FW_ERROR, &il->status);

	/* Cancel currently queued command. */
	clear_bit(S_HCMD_ACTIVE, &il->status);

	IL_ERR("Loaded firmware version: %s\n", il->hw->wiphy->fw_version);

	il->ops->dump_nic_error_log(il);
	if (il->ops->dump_fh)
		il->ops->dump_fh(il, NULL, false);
#ifdef CONFIG_IWLEGACY_DEBUG
	if (il_get_debug_level(il) & IL_DL_FW_ERRORS)
		il_print_rx_config_cmd(il);
#endif

	wake_up(&il->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(S_READY, &il->status);

	if (!test_bit(S_EXIT_PENDING, &il->status)) {
		IL_DBG(IL_DL_FW_ERRORS,
		       "Restarting adapter due to uCode error.\n");

		if (il->cfg->mod_params->restart_fw)
			queue_work(il->workqueue, &il->restart);
	}
}
EXPORT_SYMBOL(il_irq_handle_error);

static int
_il_apm_stop_master(struct il_priv *il)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	_il_set_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret =
	    _il_poll_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_MASTER_DISABLED,
			 CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret < 0)
		IL_WARN("Master Disable Timed Out, 100 usec\n");

	D_INFO("stop master\n");

	return ret;
}

void
_il_apm_stop(struct il_priv *il)
{
	lockdep_assert_held(&il->reg_lock);

	D_INFO("Stop card, put in low power state\n");

	/* Stop device's DMA activity */
	_il_apm_stop_master(il);

	/* Reset the entire device */
	_il_set_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	_il_clear_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}
EXPORT_SYMBOL(_il_apm_stop);

void
il_apm_stop(struct il_priv *il)
{
	unsigned long flags;

	spin_lock_irqsave(&il->reg_lock, flags);
	_il_apm_stop(il);
	spin_unlock_irqrestore(&il->reg_lock, flags);
}
EXPORT_SYMBOL(il_apm_stop);

/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via il_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int
il_apm_init(struct il_priv *il)
{
	int ret = 0;
	u16 lctl;

	D_INFO("Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	il_set_bit(il, CSR_GIO_CHICKEN_BITS,
		   CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	il_set_bit(il, CSR_GIO_CHICKEN_BITS,
		   CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	il_set_bit(il, CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 * NOTE:  This is no-op for 3945 (non-existent bit)
	 */
	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
		   CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	/*
	 * HW bug W/A for instability in PCIe bus L0->L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	if (il->cfg->set_l0s) {
		lctl = il_pcie_link_ctl(il);
		if ((lctl & PCI_CFG_LINK_CTRL_VAL_L1_EN) ==
		    PCI_CFG_LINK_CTRL_VAL_L1_EN) {
			/* L1-ASPM enabled; disable(!) L0S  */
			il_set_bit(il, CSR_GIO_REG,
				   CSR_GIO_REG_VAL_L0S_ENABLED);
			D_POWER("L1 Enabled; Disabling L0S\n");
		} else {
			/* L1-ASPM disabled; enable(!) L0S */
			il_clear_bit(il, CSR_GIO_REG,
				     CSR_GIO_REG_VAL_L0S_ENABLED);
			D_POWER("L1 Disabled; Enabling L0S\n");
		}
	}

	/* Configure analog phase-lock-loop before activating to D0A */
	if (il->cfg->pll_cfg_val)
		il_set_bit(il, CSR_ANA_PLL_CFG,
			   il->cfg->pll_cfg_val);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	il_set_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. il_wr_prph()
	 * and accesses to uCode SRAM.
	 */
	ret =
	    _il_poll_bit(il, CSR_GP_CNTRL,
			 CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			 CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		D_INFO("Failed to init the card\n");
		goto out;
	}

	/*
	 * Enable DMA and BSM (if used) clocks, wait for them to stabilize.
	 * BSM (Boostrap State Machine) is only in 3945 and 4965.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	if (il->cfg->use_bsm)
		il_wr_prph(il, APMG_CLK_EN_REG,
			   APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);
	else
		il_wr_prph(il, APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(20);

	/* Disable L1-Active */
	il_set_bits_prph(il, APMG_PCIDEV_STT_REG,
			 APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

out:
	return ret;
}
EXPORT_SYMBOL(il_apm_init);

int
il_set_tx_power(struct il_priv *il, s8 tx_power, bool force)
{
	int ret;
	s8 prev_tx_power;
	bool defer;

	lockdep_assert_held(&il->mutex);

	if (il->tx_power_user_lmt == tx_power && !force)
		return 0;

	if (!il->ops->send_tx_power)
		return -EOPNOTSUPP;

	/* 0 dBm mean 1 milliwatt */
	if (tx_power < 0) {
		IL_WARN("Requested user TXPOWER %d below 1 mW.\n", tx_power);
		return -EINVAL;
	}

	if (tx_power > il->tx_power_device_lmt) {
		IL_WARN("Requested user TXPOWER %d above upper limit %d.\n",
			tx_power, il->tx_power_device_lmt);
		return -EINVAL;
	}

	if (!il_is_ready_rf(il))
		return -EIO;

	/* scan complete and commit_rxon use tx_power_next value,
	 * it always need to be updated for newest request */
	il->tx_power_next = tx_power;

	/* do not set tx power when scanning or channel changing */
	defer = test_bit(S_SCANNING, &il->status) ||
	    memcmp(&il->active, &il->staging, sizeof(il->staging));
	if (defer && !force) {
		D_INFO("Deferring tx power set\n");
		return 0;
	}

	prev_tx_power = il->tx_power_user_lmt;
	il->tx_power_user_lmt = tx_power;

	ret = il->ops->send_tx_power(il);

	/* if fail to set tx_power, restore the orig. tx power */
	if (ret) {
		il->tx_power_user_lmt = prev_tx_power;
		il->tx_power_next = prev_tx_power;
	}
	return ret;
}
EXPORT_SYMBOL(il_set_tx_power);

void
il_send_bt_config(struct il_priv *il)
{
	struct il_bt_cmd bt_cmd = {
		.lead_time = BT_LEAD_TIME_DEF,
		.max_kill = BT_MAX_KILL_DEF,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	if (!bt_coex_active)
		bt_cmd.flags = BT_COEX_DISABLE;
	else
		bt_cmd.flags = BT_COEX_ENABLE;

	D_INFO("BT coex %s\n",
	       (bt_cmd.flags == BT_COEX_DISABLE) ? "disable" : "active");

	if (il_send_cmd_pdu(il, C_BT_CONFIG, sizeof(struct il_bt_cmd), &bt_cmd))
		IL_ERR("failed to send BT Coex Config\n");
}
EXPORT_SYMBOL(il_send_bt_config);

int
il_send_stats_request(struct il_priv *il, u8 flags, bool clear)
{
	struct il_stats_cmd stats_cmd = {
		.configuration_flags = clear ? IL_STATS_CONF_CLEAR_STATS : 0,
	};

	if (flags & CMD_ASYNC)
		return il_send_cmd_pdu_async(il, C_STATS, sizeof(struct il_stats_cmd),
					     &stats_cmd, NULL);
	else
		return il_send_cmd_pdu(il, C_STATS, sizeof(struct il_stats_cmd),
				       &stats_cmd);
}
EXPORT_SYMBOL(il_send_stats_request);

void
il_hdl_pm_sleep(struct il_priv *il, struct il_rx_buf *rxb)
{
#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_sleep_notification *sleep = &(pkt->u.sleep_notif);
	D_RX("sleep mode: %d, src: %d\n",
	     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}
EXPORT_SYMBOL(il_hdl_pm_sleep);

void
il_hdl_pm_debug_stats(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	u32 len = le32_to_cpu(pkt->len_n_flags) & IL_RX_FRAME_SIZE_MSK;
	D_RADIO("Dumping %d bytes of unhandled notification for %s:\n", len,
		il_get_cmd_string(pkt->hdr.cmd));
	il_print_hex_dump(il, IL_DL_RADIO, pkt->u.raw, len);
}
EXPORT_SYMBOL(il_hdl_pm_debug_stats);

void
il_hdl_error(struct il_priv *il, struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);

	IL_ERR("Error Reply type 0x%08X cmd %s (0x%02X) "
	       "seq 0x%04X ser 0x%08X\n",
	       le32_to_cpu(pkt->u.err_resp.error_type),
	       il_get_cmd_string(pkt->u.err_resp.cmd_id),
	       pkt->u.err_resp.cmd_id,
	       le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
	       le32_to_cpu(pkt->u.err_resp.error_info));
}
EXPORT_SYMBOL(il_hdl_error);

void
il_clear_isr_stats(struct il_priv *il)
{
	memset(&il->isr_stats, 0, sizeof(il->isr_stats));
}

int
il_mac_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct il_priv *il = hw->priv;
	unsigned long flags;
	int q;

	D_MAC80211("enter\n");

	if (!il_is_ready_rf(il)) {
		D_MAC80211("leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		D_MAC80211("leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&il->lock, flags);

	il->qos_data.def_qos_parm.ac[q].cw_min =
	    cpu_to_le16(params->cw_min);
	il->qos_data.def_qos_parm.ac[q].cw_max =
	    cpu_to_le16(params->cw_max);
	il->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
	il->qos_data.def_qos_parm.ac[q].edca_txop =
	    cpu_to_le16((params->txop * 32));

	il->qos_data.def_qos_parm.ac[q].reserved1 = 0;

	spin_unlock_irqrestore(&il->lock, flags);

	D_MAC80211("leave\n");
	return 0;
}
EXPORT_SYMBOL(il_mac_conf_tx);

int
il_mac_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct il_priv *il = hw->priv;
	int ret;

	D_MAC80211("enter\n");

	ret = (il->ibss_manager == IL_IBSS_MANAGER);

	D_MAC80211("leave ret %d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(il_mac_tx_last_beacon);

static int
il_set_mode(struct il_priv *il)
{
	il_connection_init_rx_config(il);

	if (il->ops->set_rxon_chain)
		il->ops->set_rxon_chain(il);

	return il_commit_rxon(il);
}

int
il_mac_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	int err;
	bool reset;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: type %d, addr %pM\n", vif->type, vif->addr);

	if (!il_is_ready_rf(il)) {
		IL_WARN("Try to add interface when device not ready\n");
		err = -EINVAL;
		goto out;
	}

	/*
	 * We do not support multiple virtual interfaces, but on hardware reset
	 * we have to add the same interface again.
	 */
	reset = (il->vif == vif);
	if (il->vif && !reset) {
		err = -EOPNOTSUPP;
		goto out;
	}

	il->vif = vif;
	il->iw_mode = vif->type;

	err = il_set_mode(il);
	if (err) {
		IL_WARN("Fail to set mode %d\n", vif->type);
		if (!reset) {
			il->vif = NULL;
			il->iw_mode = NL80211_IFTYPE_STATION;
		}
	}

out:
	D_MAC80211("leave err %d\n", err);
	mutex_unlock(&il->mutex);

	return err;
}
EXPORT_SYMBOL(il_mac_add_interface);

static void
il_teardown_interface(struct il_priv *il, struct ieee80211_vif *vif,
		      bool mode_change)
{
	lockdep_assert_held(&il->mutex);

	if (il->scan_vif == vif) {
		il_scan_cancel_timeout(il, 200);
		il_force_scan_end(il);
	}

	if (!mode_change)
		il_set_mode(il);

}

void
il_mac_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: type %d, addr %pM\n", vif->type, vif->addr);

	WARN_ON(il->vif != vif);
	il->vif = NULL;

	il_teardown_interface(il, vif, false);
	memset(il->bssid, 0, ETH_ALEN);

	D_MAC80211("leave\n");
	mutex_unlock(&il->mutex);
}
EXPORT_SYMBOL(il_mac_remove_interface);

int
il_alloc_txq_mem(struct il_priv *il)
{
	if (!il->txq)
		il->txq =
		    kzalloc(sizeof(struct il_tx_queue) *
			    il->cfg->num_of_queues, GFP_KERNEL);
	if (!il->txq) {
		IL_ERR("Not enough memory for txq\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(il_alloc_txq_mem);

void
il_free_txq_mem(struct il_priv *il)
{
	kfree(il->txq);
	il->txq = NULL;
}
EXPORT_SYMBOL(il_free_txq_mem);

int
il_force_reset(struct il_priv *il, bool external)
{
	struct il_force_reset *force_reset;

	if (test_bit(S_EXIT_PENDING, &il->status))
		return -EINVAL;

	force_reset = &il->force_reset;
	force_reset->reset_request_count++;
	if (!external) {
		if (force_reset->last_force_reset_jiffies &&
		    time_after(force_reset->last_force_reset_jiffies +
			       force_reset->reset_duration, jiffies)) {
			D_INFO("force reset rejected\n");
			force_reset->reset_reject_count++;
			return -EAGAIN;
		}
	}
	force_reset->reset_success_count++;
	force_reset->last_force_reset_jiffies = jiffies;

	/*
	 * if the request is from external(ex: debugfs),
	 * then always perform the request in regardless the module
	 * parameter setting
	 * if the request is from internal (uCode error or driver
	 * detect failure), then fw_restart module parameter
	 * need to be check before performing firmware reload
	 */

	if (!external && !il->cfg->mod_params->restart_fw) {
		D_INFO("Cancel firmware reload based on "
		       "module parameter setting\n");
		return 0;
	}

	IL_ERR("On demand firmware reload\n");

	/* Set the FW error flag -- cleared on il_down */
	set_bit(S_FW_ERROR, &il->status);
	wake_up(&il->wait_command_queue);
	/*
	 * Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit
	 */
	clear_bit(S_READY, &il->status);
	queue_work(il->workqueue, &il->restart);

	return 0;
}

int
il_mac_change_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			enum nl80211_iftype newtype, bool newp2p)
{
	struct il_priv *il = hw->priv;
	int err;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: type %d, addr %pM newtype %d newp2p %d\n",
		    vif->type, vif->addr, newtype, newp2p);

	if (newp2p) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!il->vif || !il_is_ready_rf(il)) {
		/*
		 * Huh? But wait ... this can maybe happen when
		 * we're in the middle of a firmware restart!
		 */
		err = -EBUSY;
		goto out;
	}

	/* success */
	il_teardown_interface(il, vif, true);
	vif->type = newtype;
	vif->p2p = false;
	err = il_set_mode(il);
	WARN_ON(err);
	/*
	 * We've switched internally, but submitting to the
	 * device may have failed for some reason. Mask this
	 * error, because otherwise mac80211 will not switch
	 * (and set the interface type back) and we'll be
	 * out of sync with it.
	 */
	err = 0;

out:
	D_MAC80211("leave err %d\n", err);
	mutex_unlock(&il->mutex);

	return err;
}
EXPORT_SYMBOL(il_mac_change_interface);

/*
 * On every watchdog tick we check (latest) time stamp. If it does not
 * change during timeout period and queue is not empty we reset firmware.
 */
static int
il_check_stuck_queue(struct il_priv *il, int cnt)
{
	struct il_tx_queue *txq = &il->txq[cnt];
	struct il_queue *q = &txq->q;
	unsigned long timeout;
	int ret;

	if (q->read_ptr == q->write_ptr) {
		txq->time_stamp = jiffies;
		return 0;
	}

	timeout =
	    txq->time_stamp +
	    msecs_to_jiffies(il->cfg->wd_timeout);

	if (time_after(jiffies, timeout)) {
		IL_ERR("Queue %d stuck for %u ms.\n", q->id,
		       il->cfg->wd_timeout);
		ret = il_force_reset(il, false);
		return (ret == -EAGAIN) ? 0 : 1;
	}

	return 0;
}

/*
 * Making watchdog tick be a quarter of timeout assure we will
 * discover the queue hung between timeout and 1.25*timeout
 */
#define IL_WD_TICK(timeout) ((timeout) / 4)

/*
 * Watchdog timer callback, we check each tx queue for stuck, if if hung
 * we reset the firmware. If everything is fine just rearm the timer.
 */
void
il_bg_watchdog(unsigned long data)
{
	struct il_priv *il = (struct il_priv *)data;
	int cnt;
	unsigned long timeout;

	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	timeout = il->cfg->wd_timeout;
	if (timeout == 0)
		return;

	/* monitor and check for stuck cmd queue */
	if (il_check_stuck_queue(il, il->cmd_queue))
		return;

	/* monitor and check for other stuck queues */
	if (il_is_any_associated(il)) {
		for (cnt = 0; cnt < il->hw_params.max_txq_num; cnt++) {
			/* skip as we already checked the command queue */
			if (cnt == il->cmd_queue)
				continue;
			if (il_check_stuck_queue(il, cnt))
				return;
		}
	}

	mod_timer(&il->watchdog,
		  jiffies + msecs_to_jiffies(IL_WD_TICK(timeout)));
}
EXPORT_SYMBOL(il_bg_watchdog);

void
il_setup_watchdog(struct il_priv *il)
{
	unsigned int timeout = il->cfg->wd_timeout;

	if (timeout)
		mod_timer(&il->watchdog,
			  jiffies + msecs_to_jiffies(IL_WD_TICK(timeout)));
	else
		del_timer(&il->watchdog);
}
EXPORT_SYMBOL(il_setup_watchdog);

/*
 * extended beacon time format
 * time in usec will be changed into a 32-bit value in extended:internal format
 * the extended part is the beacon counts
 * the internal part is the time in usec within one beacon interval
 */
u32
il_usecs_to_beacons(struct il_priv *il, u32 usec, u32 beacon_interval)
{
	u32 quot;
	u32 rem;
	u32 interval = beacon_interval * TIME_UNIT;

	if (!interval || !usec)
		return 0;

	quot =
	    (usec /
	     interval) & (il_beacon_time_mask_high(il,
						   il->hw_params.
						   beacon_time_tsf_bits) >> il->
			  hw_params.beacon_time_tsf_bits);
	rem =
	    (usec % interval) & il_beacon_time_mask_low(il,
							il->hw_params.
							beacon_time_tsf_bits);

	return (quot << il->hw_params.beacon_time_tsf_bits) + rem;
}
EXPORT_SYMBOL(il_usecs_to_beacons);

/* base is usually what we get from ucode with each received frame,
 * the same as HW timer counter counting down
 */
__le32
il_add_beacon_time(struct il_priv *il, u32 base, u32 addon,
		   u32 beacon_interval)
{
	u32 base_low = base & il_beacon_time_mask_low(il,
						      il->hw_params.
						      beacon_time_tsf_bits);
	u32 addon_low = addon & il_beacon_time_mask_low(il,
							il->hw_params.
							beacon_time_tsf_bits);
	u32 interval = beacon_interval * TIME_UNIT;
	u32 res = (base & il_beacon_time_mask_high(il,
						   il->hw_params.
						   beacon_time_tsf_bits)) +
	    (addon & il_beacon_time_mask_high(il,
					      il->hw_params.
					      beacon_time_tsf_bits));

	if (base_low > addon_low)
		res += base_low - addon_low;
	else if (base_low < addon_low) {
		res += interval + base_low - addon_low;
		res += (1 << il->hw_params.beacon_time_tsf_bits);
	} else
		res += (1 << il->hw_params.beacon_time_tsf_bits);

	return cpu_to_le32(res);
}
EXPORT_SYMBOL(il_add_beacon_time);

#ifdef CONFIG_PM

int
il_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct il_priv *il = pci_get_drvdata(pdev);

	/*
	 * This function is called when system goes into suspend state
	 * mac80211 will call il_mac_stop() from the mac80211 suspend function
	 * first but since il_mac_stop() has no knowledge of who the caller is,
	 * it will not call apm_ops.stop() to stop the DMA operation.
	 * Calling apm_ops.stop here to make sure we stop the DMA.
	 */
	il_apm_stop(il);

	return 0;
}
EXPORT_SYMBOL(il_pci_suspend);

int
il_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct il_priv *il = pci_get_drvdata(pdev);
	bool hw_rfkill = false;

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	il_enable_interrupts(il);

	if (!(_il_rd(il, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
		hw_rfkill = true;

	if (hw_rfkill)
		set_bit(S_RFKILL, &il->status);
	else
		clear_bit(S_RFKILL, &il->status);

	wiphy_rfkill_set_hw_state(il->hw->wiphy, hw_rfkill);

	return 0;
}
EXPORT_SYMBOL(il_pci_resume);

const struct dev_pm_ops il_pm_ops = {
	.suspend = il_pci_suspend,
	.resume = il_pci_resume,
	.freeze = il_pci_suspend,
	.thaw = il_pci_resume,
	.poweroff = il_pci_suspend,
	.restore = il_pci_resume,
};
EXPORT_SYMBOL(il_pm_ops);

#endif /* CONFIG_PM */

static void
il_update_qos(struct il_priv *il)
{
	if (test_bit(S_EXIT_PENDING, &il->status))
		return;

	il->qos_data.def_qos_parm.qos_flags = 0;

	if (il->qos_data.qos_active)
		il->qos_data.def_qos_parm.qos_flags |=
		    QOS_PARAM_FLG_UPDATE_EDCA_MSK;

	if (il->ht.enabled)
		il->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;

	D_QOS("send QoS cmd with Qos active=%d FLAGS=0x%X\n",
	      il->qos_data.qos_active, il->qos_data.def_qos_parm.qos_flags);

	il_send_cmd_pdu_async(il, C_QOS_PARAM, sizeof(struct il_qosparam_cmd),
			      &il->qos_data.def_qos_parm, NULL);
}

/**
 * il_mac_config - mac80211 config callback
 */
int
il_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct il_priv *il = hw->priv;
	const struct il_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = conf->channel;
	struct il_ht_config *ht_conf = &il->current_ht_config;
	unsigned long flags = 0;
	int ret = 0;
	u16 ch;
	int scan_active = 0;
	bool ht_changed = false;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: channel %d changed 0x%X\n", channel->hw_value,
		   changed);

	if (unlikely(test_bit(S_SCANNING, &il->status))) {
		scan_active = 1;
		D_MAC80211("scan active\n");
	}

	if (changed &
	    (IEEE80211_CONF_CHANGE_SMPS | IEEE80211_CONF_CHANGE_CHANNEL)) {
		/* mac80211 uses static for non-HT which is what we want */
		il->current_ht_config.smps = conf->smps_mode;

		/*
		 * Recalculate chain counts.
		 *
		 * If monitor mode is enabled then mac80211 will
		 * set up the SM PS mode to OFF if an HT channel is
		 * configured.
		 */
		if (il->ops->set_rxon_chain)
			il->ops->set_rxon_chain(il);
	}

	/* during scanning mac80211 will delay channel setting until
	 * scan finish with changed = 0
	 */
	if (!changed || (changed & IEEE80211_CONF_CHANGE_CHANNEL)) {

		if (scan_active)
			goto set_ch_out;

		ch = channel->hw_value;
		ch_info = il_get_channel_info(il, channel->band, ch);
		if (!il_is_channel_valid(ch_info)) {
			D_MAC80211("leave - invalid channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		if (il->iw_mode == NL80211_IFTYPE_ADHOC &&
		    !il_is_channel_ibss(ch_info)) {
			D_MAC80211("leave - not IBSS channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		spin_lock_irqsave(&il->lock, flags);

		/* Configure HT40 channels */
		if (il->ht.enabled != conf_is_ht(conf)) {
			il->ht.enabled = conf_is_ht(conf);
			ht_changed = true;
		}
		if (il->ht.enabled) {
			if (conf_is_ht40_minus(conf)) {
				il->ht.extension_chan_offset =
				    IEEE80211_HT_PARAM_CHA_SEC_BELOW;
				il->ht.is_40mhz = true;
			} else if (conf_is_ht40_plus(conf)) {
				il->ht.extension_chan_offset =
				    IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
				il->ht.is_40mhz = true;
			} else {
				il->ht.extension_chan_offset =
				    IEEE80211_HT_PARAM_CHA_SEC_NONE;
				il->ht.is_40mhz = false;
			}
		} else
			il->ht.is_40mhz = false;

		/*
		 * Default to no protection. Protection mode will
		 * later be set from BSS config in il_ht_conf
		 */
		il->ht.protection = IEEE80211_HT_OP_MODE_PROTECTION_NONE;

		/* if we are switching from ht to 2.4 clear flags
		 * from any ht related info since 2.4 does not
		 * support ht */
		if ((le16_to_cpu(il->staging.channel) != ch))
			il->staging.flags = 0;

		il_set_rxon_channel(il, channel);
		il_set_rxon_ht(il, ht_conf);

		il_set_flags_for_band(il, channel->band, il->vif);

		spin_unlock_irqrestore(&il->lock, flags);

		if (il->ops->update_bcast_stations)
			ret = il->ops->update_bcast_stations(il);

set_ch_out:
		/* The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists */
		il_set_rate(il);
	}

	if (changed & (IEEE80211_CONF_CHANGE_PS | IEEE80211_CONF_CHANGE_IDLE)) {
		ret = il_power_update_mode(il, false);
		if (ret)
			D_MAC80211("Error setting sleep level\n");
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		D_MAC80211("TX Power old=%d new=%d\n", il->tx_power_user_lmt,
			   conf->power_level);

		il_set_tx_power(il, conf->power_level, false);
	}

	if (!il_is_ready(il)) {
		D_MAC80211("leave - not ready\n");
		goto out;
	}

	if (scan_active)
		goto out;

	if (memcmp(&il->active, &il->staging, sizeof(il->staging)))
		il_commit_rxon(il);
	else
		D_INFO("Not re-sending same RXON configuration.\n");
	if (ht_changed)
		il_update_qos(il);

out:
	D_MAC80211("leave ret %d\n", ret);
	mutex_unlock(&il->mutex);

	return ret;
}
EXPORT_SYMBOL(il_mac_config);

void
il_mac_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	unsigned long flags;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: type %d, addr %pM\n", vif->type, vif->addr);

	spin_lock_irqsave(&il->lock, flags);

	memset(&il->current_ht_config, 0, sizeof(struct il_ht_config));

	/* new association get rid of ibss beacon skb */
	if (il->beacon_skb)
		dev_kfree_skb(il->beacon_skb);
	il->beacon_skb = NULL;
	il->timestamp = 0;

	spin_unlock_irqrestore(&il->lock, flags);

	il_scan_cancel_timeout(il, 100);
	if (!il_is_ready_rf(il)) {
		D_MAC80211("leave - not ready\n");
		mutex_unlock(&il->mutex);
		return;
	}

	/* we are restarting association process */
	il->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	il_commit_rxon(il);

	il_set_rate(il);

	D_MAC80211("leave\n");
	mutex_unlock(&il->mutex);
}
EXPORT_SYMBOL(il_mac_reset_tsf);

static void
il_ht_conf(struct il_priv *il, struct ieee80211_vif *vif)
{
	struct il_ht_config *ht_conf = &il->current_ht_config;
	struct ieee80211_sta *sta;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	D_ASSOC("enter:\n");

	if (!il->ht.enabled)
		return;

	il->ht.protection =
	    bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	il->ht.non_gf_sta_present =
	    !!(bss_conf->
	       ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);

	ht_conf->single_chain_sufficient = false;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		rcu_read_lock();
		sta = ieee80211_find_sta(vif, bss_conf->bssid);
		if (sta) {
			struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
			int maxstreams;

			maxstreams =
			    (ht_cap->mcs.
			     tx_params & IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK)
			    >> IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
			maxstreams += 1;

			if (ht_cap->mcs.rx_mask[1] == 0 &&
			    ht_cap->mcs.rx_mask[2] == 0)
				ht_conf->single_chain_sufficient = true;
			if (maxstreams <= 1)
				ht_conf->single_chain_sufficient = true;
		} else {
			/*
			 * If at all, this can only happen through a race
			 * when the AP disconnects us while we're still
			 * setting up the connection, in that case mac80211
			 * will soon tell us about that.
			 */
			ht_conf->single_chain_sufficient = true;
		}
		rcu_read_unlock();
		break;
	case NL80211_IFTYPE_ADHOC:
		ht_conf->single_chain_sufficient = true;
		break;
	default:
		break;
	}

	D_ASSOC("leave\n");
}

static inline void
il_set_no_assoc(struct il_priv *il, struct ieee80211_vif *vif)
{
	/*
	 * inform the ucode that there is no longer an
	 * association and that no more packets should be
	 * sent
	 */
	il->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	il->staging.assoc_id = 0;
	il_commit_rxon(il);
}

static void
il_beacon_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	unsigned long flags;
	__le64 timestamp;
	struct sk_buff *skb = ieee80211_beacon_get(hw, vif);

	if (!skb)
		return;

	D_MAC80211("enter\n");

	lockdep_assert_held(&il->mutex);

	if (!il->beacon_enabled) {
		IL_ERR("update beacon with no beaconing enabled\n");
		dev_kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&il->lock, flags);

	if (il->beacon_skb)
		dev_kfree_skb(il->beacon_skb);

	il->beacon_skb = skb;

	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	il->timestamp = le64_to_cpu(timestamp);

	D_MAC80211("leave\n");
	spin_unlock_irqrestore(&il->lock, flags);

	if (!il_is_ready_rf(il)) {
		D_MAC80211("leave - RF not ready\n");
		return;
	}

	il->ops->post_associate(il);
}

void
il_mac_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *bss_conf, u32 changes)
{
	struct il_priv *il = hw->priv;
	int ret;

	mutex_lock(&il->mutex);
	D_MAC80211("enter: changes 0x%x\n", changes);

	if (!il_is_alive(il)) {
		D_MAC80211("leave - not alive\n");
		mutex_unlock(&il->mutex);
		return;
	}

	if (changes & BSS_CHANGED_QOS) {
		unsigned long flags;

		spin_lock_irqsave(&il->lock, flags);
		il->qos_data.qos_active = bss_conf->qos;
		il_update_qos(il);
		spin_unlock_irqrestore(&il->lock, flags);
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		/* FIXME: can we remove beacon_enabled ? */
		if (vif->bss_conf.enable_beacon)
			il->beacon_enabled = true;
		else
			il->beacon_enabled = false;
	}

	if (changes & BSS_CHANGED_BSSID) {
		D_MAC80211("BSSID %pM\n", bss_conf->bssid);

		/*
		 * If there is currently a HW scan going on in the background,
		 * then we need to cancel it, otherwise sometimes we are not
		 * able to authenticate (FIXME: why ?)
		 */
		if (il_scan_cancel_timeout(il, 100)) {
			D_MAC80211("leave - scan abort failed\n");
			mutex_unlock(&il->mutex);
			return;
		}

		/* mac80211 only sets assoc when in STATION mode */
		memcpy(il->staging.bssid_addr, bss_conf->bssid, ETH_ALEN);

		/* FIXME: currently needed in a few places */
		memcpy(il->bssid, bss_conf->bssid, ETH_ALEN);
	}

	/*
	 * This needs to be after setting the BSSID in case
	 * mac80211 decides to do both changes at once because
	 * it will invoke post_associate.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC && (changes & BSS_CHANGED_BEACON))
		il_beacon_update(hw, vif);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		D_MAC80211("ERP_PREAMBLE %d\n", bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			il->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			il->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		D_MAC80211("ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && il->band != IEEE80211_BAND_5GHZ)
			il->staging.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			il->staging.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
		if (bss_conf->use_cts_prot)
			il->staging.flags |= RXON_FLG_SELF_CTS_EN;
		else
			il->staging.flags &= ~RXON_FLG_SELF_CTS_EN;
	}

	if (changes & BSS_CHANGED_BASIC_RATES) {
		/* XXX use this information
		 *
		 * To do that, remove code from il_set_rate() and put something
		 * like this here:
		 *
		 if (A-band)
		 il->staging.ofdm_basic_rates =
		 bss_conf->basic_rates;
		 else
		 il->staging.ofdm_basic_rates =
		 bss_conf->basic_rates >> 4;
		 il->staging.cck_basic_rates =
		 bss_conf->basic_rates & 0xF;
		 */
	}

	if (changes & BSS_CHANGED_HT) {
		il_ht_conf(il, vif);

		if (il->ops->set_rxon_chain)
			il->ops->set_rxon_chain(il);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		D_MAC80211("ASSOC %d\n", bss_conf->assoc);
		if (bss_conf->assoc) {
			il->timestamp = bss_conf->sync_tsf;

			if (!il_is_rfkill(il))
				il->ops->post_associate(il);
		} else
			il_set_no_assoc(il, vif);
	}

	if (changes && il_is_associated(il) && bss_conf->aid) {
		D_MAC80211("Changes (%#x) while associated\n", changes);
		ret = il_send_rxon_assoc(il);
		if (!ret) {
			/* Sync active_rxon with latest change. */
			memcpy((void *)&il->active, &il->staging,
			       sizeof(struct il_rxon_cmd));
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		if (vif->bss_conf.enable_beacon) {
			memcpy(il->staging.bssid_addr, bss_conf->bssid,
			       ETH_ALEN);
			memcpy(il->bssid, bss_conf->bssid, ETH_ALEN);
			il->ops->config_ap(il);
		} else
			il_set_no_assoc(il, vif);
	}

	if (changes & BSS_CHANGED_IBSS) {
		ret = il->ops->manage_ibss_station(il, vif,
						   bss_conf->ibss_joined);
		if (ret)
			IL_ERR("failed to %s IBSS station %pM\n",
			       bss_conf->ibss_joined ? "add" : "remove",
			       bss_conf->bssid);
	}

	D_MAC80211("leave\n");
	mutex_unlock(&il->mutex);
}
EXPORT_SYMBOL(il_mac_bss_info_changed);

irqreturn_t
il_isr(int irq, void *data)
{
	struct il_priv *il = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	unsigned long flags;
	if (!il)
		return IRQ_NONE;

	spin_lock_irqsave(&il->lock, flags);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = _il_rd(il, CSR_INT_MASK);	/* just for debug */
	_il_wr(il, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = _il_rd(il, CSR_INT);
	inta_fh = _il_rd(il, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		D_ISR("Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if (inta == 0xFFFFFFFF || (inta & 0xFFFFFFF0) == 0xa5a5a5a0) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IL_WARN("HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

	D_ISR("ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n", inta, inta_mask,
	      inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* il_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&il->irq_tasklet);

unplugged:
	spin_unlock_irqrestore(&il->lock, flags);
	return IRQ_HANDLED;

none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if disabled by irq */
	if (test_bit(S_INT_ENABLED, &il->status))
		il_enable_interrupts(il);
	spin_unlock_irqrestore(&il->lock, flags);
	return IRQ_NONE;
}
EXPORT_SYMBOL(il_isr);

/*
 *  il_tx_cmd_protection: Set rts/cts. 3945 and 4965 only share this
 *  function.
 */
void
il_tx_cmd_protection(struct il_priv *il, struct ieee80211_tx_info *info,
		     __le16 fc, __le32 *tx_flags)
{
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		*tx_flags |= TX_CMD_FLG_RTS_MSK;
		*tx_flags &= ~TX_CMD_FLG_CTS_MSK;
		*tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

		if (!ieee80211_is_mgmt(fc))
			return;

		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			*tx_flags &= ~TX_CMD_FLG_RTS_MSK;
			*tx_flags |= TX_CMD_FLG_CTS_MSK;
			break;
		}
	} else if (info->control.rates[0].
		   flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		*tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		*tx_flags |= TX_CMD_FLG_CTS_MSK;
		*tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;
	}
}
EXPORT_SYMBOL(il_tx_cmd_protection);

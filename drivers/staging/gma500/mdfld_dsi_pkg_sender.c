/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Jackie Li<yaodong.li@intel.com>
 */

#include <linux/freezer.h>

#include "mdfld_dsi_output.h"
#include "mdfld_dsi_pkg_sender.h"
#include "mdfld_dsi_dbi.h"

#define MDFLD_DSI_DBI_FIFO_TIMEOUT	100

static const char * const dsi_errors[] = {
	"RX SOT Error",
	"RX SOT Sync Error",
	"RX EOT Sync Error",
	"RX Escape Mode Entry Error",
	"RX LP TX Sync Error",
	"RX HS Receive Timeout Error",
	"RX False Control Error",
	"RX ECC Single Bit Error",
	"RX ECC Multibit Error",
	"RX Checksum Error",
	"RX DSI Data Type Not Recognised",
	"RX DSI VC ID Invalid",
	"TX False Control Error",
	"TX ECC Single Bit Error",
	"TX ECC Multibit Error",
	"TX Checksum Error",
	"TX DSI Data Type Not Recognised",
	"TX DSI VC ID invalid",
	"High Contention",
	"Low contention",
	"DPI FIFO Under run",
	"HS TX Timeout",
	"LP RX Timeout",
	"Turn Around ACK Timeout",
	"ACK With No Error",
	"RX Invalid TX Length",
	"RX Prot Violation",
	"HS Generic Write FIFO Full",
	"LP Generic Write FIFO Full",
	"Generic Read Data Avail",
	"Special Packet Sent",
	"Tearing Effect",
};

static int wait_for_gen_fifo_empty(struct mdfld_dsi_pkg_sender *sender,
								u32 mask)
{
	struct drm_device *dev = sender->dev;
	u32 gen_fifo_stat_reg = sender->mipi_gen_fifo_stat_reg;
	int retry = 0xffff;

	while (retry--) {
		if ((mask & REG_READ(gen_fifo_stat_reg)) == mask)
			return 0;
		udelay(100);
	}
	dev_err(dev->dev, "fifo is NOT empty 0x%08x\n",
					REG_READ(gen_fifo_stat_reg));
	return -EIO;
}

static int wait_for_all_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (1 << 2) | (1 << 10) | (1 << 18)
		| (1 << 26) | (1 << 27) | (1 << 28));
}

static int wait_for_lp_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (1 << 10) | (1 << 26));
}

static int wait_for_hs_fifos_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (1 << 2) | (1 << 18));
}

static int wait_for_dbi_fifo_empty(struct mdfld_dsi_pkg_sender *sender)
{
	return wait_for_gen_fifo_empty(sender, (1 << 27));
}

static int handle_dsi_error(struct mdfld_dsi_pkg_sender *sender, u32 mask)
{
	u32 intr_stat_reg = sender->mipi_intr_stat_reg;
	struct drm_device *dev = sender->dev;

	switch (mask) {
	case (1 << 0):
	case (1 << 1):
	case (1 << 2):
	case (1 << 3):
	case (1 << 4):
	case (1 << 5):
	case (1 << 6):
	case (1 << 7):
	case (1 << 8):
	case (1 << 9):
	case (1 << 10):
	case (1 << 11):
	case (1 << 12):
	case (1 << 13):
		break;
	case (1 << 14):
		/*wait for all fifo empty*/
		/*wait_for_all_fifos_empty(sender)*/;
		break;
	case (1 << 15):
		break;
	case (1 << 16):
		break;
	case (1 << 17):
		break;
	case (1 << 18):
	case (1 << 19):
		/*wait for contention recovery time*/
		/*mdelay(10);*/
		/*wait for all fifo empty*/
		if (0)
			wait_for_all_fifos_empty(sender);
		break;
	case (1 << 20):
		break;
	case (1 << 21):
		/*wait for all fifo empty*/
		/*wait_for_all_fifos_empty(sender);*/
		break;
	case (1 << 22):
		break;
	case (1 << 23):
	case (1 << 24):
	case (1 << 25):
	case (1 << 26):
	case (1 << 27):
		/* HS Gen fifo full */
		REG_WRITE(intr_stat_reg, mask);
		wait_for_hs_fifos_empty(sender);
		break;
	case (1 << 28):
		/* LP Gen fifo full\n */
		REG_WRITE(intr_stat_reg, mask);
		wait_for_lp_fifos_empty(sender);
		break;
	case (1 << 29):
	case (1 << 30):
	case (1 << 31):
		break;
	}

	if (mask & REG_READ(intr_stat_reg))
		dev_warn(dev->dev, "Cannot clean interrupt 0x%08x\n", mask);

	return 0;
}

static int dsi_error_handler(struct mdfld_dsi_pkg_sender *sender)
{
	struct drm_device *dev = sender->dev;
	u32 intr_stat_reg = sender->mipi_intr_stat_reg;
	u32 mask;
	u32 intr_stat;
	int i;
	int err = 0;

	intr_stat = REG_READ(intr_stat_reg);

	for (i = 0; i < 32; i++) {
		mask = (0x00000001UL) << i;
		if (intr_stat & mask) {
			dev_dbg(dev->dev, "[DSI]: %s\n", dsi_errors[i]);
			err = handle_dsi_error(sender, mask);
			if (err)
				dev_err(dev->dev, "Cannot handle error\n");
		}
	}
	return err;
}

static inline int dbi_cmd_sent(struct mdfld_dsi_pkg_sender *sender)
{
	struct drm_device *dev = sender->dev;
	u32 retry = 0xffff;
	u32 dbi_cmd_addr_reg = sender->mipi_cmd_addr_reg;

	/* Query the command execution status */
	while (retry--) {
		if (!(REG_READ(dbi_cmd_addr_reg) & (1 << 0)))
			break;
	}

	if (!retry) {
		dev_err(dev->dev, "Timeout waiting for DBI Command status\n");
		return -EAGAIN;
	}
	return 0;
}

/*
 * NOTE: this interface is abandoned expect for write_mem_start DCS
 * other DCS are sent via generic pkg interfaces
 */
static int send_dcs_pkg(struct mdfld_dsi_pkg_sender *sender,
			struct mdfld_dsi_pkg *pkg)
{
	struct drm_device *dev = sender->dev;
	struct mdfld_dsi_dcs_pkg *dcs_pkg = &pkg->pkg.dcs_pkg;
	u32 dbi_cmd_len_reg = sender->mipi_cmd_len_reg;
	u32 dbi_cmd_addr_reg = sender->mipi_cmd_addr_reg;
	u32 cb_phy = sender->dbi_cb_phy;
	u32 index = 0;
	u8 *cb = (u8 *)sender->dbi_cb_addr;
	int i;
	int ret;

	if (!sender->dbi_pkg_support) {
		dev_err(dev->dev, "Trying to send DCS on a non DBI output, abort!\n");
		return -ENOTSUPP;
	}

	/*wait for DBI fifo empty*/
	wait_for_dbi_fifo_empty(sender);

	*(cb + (index++)) = dcs_pkg->cmd;
	if (dcs_pkg->param_num) {
		for (i = 0; i < dcs_pkg->param_num; i++)
			*(cb + (index++)) = *(dcs_pkg->param + i);
	}

	REG_WRITE(dbi_cmd_len_reg, (1 + dcs_pkg->param_num));
	REG_WRITE(dbi_cmd_addr_reg,
		(cb_phy << CMD_MEM_ADDR_OFFSET)
		| (1 << 0)
		| ((dcs_pkg->data_src == CMD_DATA_SRC_PIPE) ? (1 << 1) : 0));

	ret = dbi_cmd_sent(sender);
	if (ret) {
		dev_err(dev->dev, "command 0x%x not complete\n", dcs_pkg->cmd);
		return -EAGAIN;
	}
	return 0;
}

static int __send_short_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	struct drm_device *dev = sender->dev;
	u32 hs_gen_ctrl_reg = sender->mipi_hs_gen_ctrl_reg;
	u32 lp_gen_ctrl_reg = sender->mipi_lp_gen_ctrl_reg;
	u32 gen_ctrl_val = 0;
	struct mdfld_dsi_gen_short_pkg *short_pkg = &pkg->pkg.short_pkg;

	gen_ctrl_val |= short_pkg->cmd << MCS_COMMANDS_POS;
	gen_ctrl_val |= 0 << DCS_CHANNEL_NUMBER_POS;
	gen_ctrl_val |= pkg->pkg_type;
	gen_ctrl_val |= short_pkg->param << MCS_PARAMETER_POS;

	if (pkg->transmission_type == MDFLD_DSI_HS_TRANSMISSION) {
		/* wait for hs fifo empty */
		/* wait_for_hs_fifos_empty(sender); */
		/* Send pkg */
		REG_WRITE(hs_gen_ctrl_reg, gen_ctrl_val);
	} else if (pkg->transmission_type == MDFLD_DSI_LP_TRANSMISSION) {
		/* wait_for_lp_fifos_empty(sender); */
		/* Send pkg*/
		REG_WRITE(lp_gen_ctrl_reg, gen_ctrl_val);
	} else {
		dev_err(dev->dev, "Unknown transmission type %d\n",
							pkg->transmission_type);
		return -EINVAL;
	}

	return 0;
}

static int __send_long_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	struct drm_device *dev = sender->dev;
	u32 hs_gen_ctrl_reg = sender->mipi_hs_gen_ctrl_reg;
	u32 hs_gen_data_reg = sender->mipi_hs_gen_data_reg;
	u32 lp_gen_ctrl_reg = sender->mipi_lp_gen_ctrl_reg;
	u32 lp_gen_data_reg = sender->mipi_lp_gen_data_reg;
	u32 gen_ctrl_val = 0;
	u32 *dp;
	int i;
	struct mdfld_dsi_gen_long_pkg *long_pkg = &pkg->pkg.long_pkg;

	dp = long_pkg->data;

	/*
	 * Set up word count for long pkg
	 * FIXME: double check word count field.
	 * currently, using the byte counts of the payload as the word count.
	 * ------------------------------------------------------------
	 * | DI |   WC   | ECC|         PAYLOAD              |CHECKSUM|
	 * ------------------------------------------------------------
	 */
	gen_ctrl_val |= (long_pkg->len << 2) << WORD_COUNTS_POS;
	gen_ctrl_val |= 0 << DCS_CHANNEL_NUMBER_POS;
	gen_ctrl_val |= pkg->pkg_type;

	if (pkg->transmission_type == MDFLD_DSI_HS_TRANSMISSION) {
		/* Wait for hs ctrl and data fifos to be empty */
		/* wait_for_hs_fifos_empty(sender); */
		for (i = 0; i < long_pkg->len; i++)
			REG_WRITE(hs_gen_data_reg, *(dp + i));
		REG_WRITE(hs_gen_ctrl_reg, gen_ctrl_val);
	} else if (pkg->transmission_type == MDFLD_DSI_LP_TRANSMISSION) {
		/* wait_for_lp_fifos_empty(sender); */
		for (i = 0; i < long_pkg->len; i++)
			REG_WRITE(lp_gen_data_reg, *(dp + i));
		REG_WRITE(lp_gen_ctrl_reg, gen_ctrl_val);
	} else {
		dev_err(dev->dev, "Unknown transmission type %d\n",
						pkg->transmission_type);
		return -EINVAL;
	}

	return 0;

}

static int send_mcs_short_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	return __send_short_pkg(sender, pkg);
}

static int send_mcs_long_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	return __send_long_pkg(sender, pkg);
}

static int send_gen_short_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	return __send_short_pkg(sender, pkg);
}

static int send_gen_long_pkg(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	return __send_long_pkg(sender, pkg);
}

static int send_pkg_prepare(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	u8 cmd;
	u8 *data;

	switch (pkg->pkg_type) {
	case MDFLD_DSI_PKG_DCS:
		cmd = pkg->pkg.dcs_pkg.cmd;
		break;
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_0:
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_1:
		cmd = pkg->pkg.short_pkg.cmd;
		break;
	case MDFLD_DSI_PKG_MCS_LONG_WRITE:
		data = (u8 *)pkg->pkg.long_pkg.data;
		cmd = *data;
		break;
	default:
		return 0;
	}

	/* This prevents other package sending while doing msleep */
	sender->status = MDFLD_DSI_PKG_SENDER_BUSY;

	/* Check panel mode v.s. sending command */
	if ((sender->panel_mode & MDFLD_DSI_PANEL_MODE_SLEEP) &&
		cmd != exit_sleep_mode) {
		dev_err(sender->dev->dev,
				"sending 0x%x when panel sleep in\n", cmd);
		sender->status = MDFLD_DSI_PKG_SENDER_FREE;
		return -EINVAL;
	}

	/* Wait for 120 milliseconds in case exit_sleep_mode just be sent */
	if (cmd == enter_sleep_mode)
		mdelay(120);
	return 0;
}

static int send_pkg_done(struct mdfld_dsi_pkg_sender *sender,
				struct mdfld_dsi_pkg *pkg)
{
	u8 cmd;
	u8 *data;

	switch (pkg->pkg_type) {
	case MDFLD_DSI_PKG_DCS:
		cmd = pkg->pkg.dcs_pkg.cmd;
		break;
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_0:
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_1:
		cmd = pkg->pkg.short_pkg.cmd;
		break;
	case MDFLD_DSI_PKG_MCS_LONG_WRITE:
		data = (u8 *)pkg->pkg.long_pkg.data;
		cmd = *data;
		break;
	default:
		return 0;
	}

	/* Update panel status */
	if (cmd == enter_sleep_mode) {
		sender->panel_mode |= MDFLD_DSI_PANEL_MODE_SLEEP;
		/*TODO: replace it with msleep later*/
		mdelay(120);
	} else if (cmd == exit_sleep_mode) {
		sender->panel_mode &= ~MDFLD_DSI_PANEL_MODE_SLEEP;
		/*TODO: replace it with msleep later*/
		mdelay(120);
	}

	sender->status = MDFLD_DSI_PKG_SENDER_FREE;
	return 0;

}

static int do_send_pkg(struct mdfld_dsi_pkg_sender *sender,
			struct mdfld_dsi_pkg *pkg)
{
	int ret;

	if (sender->status == MDFLD_DSI_PKG_SENDER_BUSY) {
		dev_err(sender->dev->dev, "sender is busy\n");
		return -EAGAIN;
	}

	ret = send_pkg_prepare(sender, pkg);
	if (ret) {
		dev_err(sender->dev->dev, "send_pkg_prepare error\n");
		return ret;
	}

	switch (pkg->pkg_type) {
	case MDFLD_DSI_PKG_DCS:
		ret = send_dcs_pkg(sender, pkg);
		break;
	case MDFLD_DSI_PKG_GEN_SHORT_WRITE_0:
	case MDFLD_DSI_PKG_GEN_SHORT_WRITE_1:
	case MDFLD_DSI_PKG_GEN_SHORT_WRITE_2:
		ret = send_gen_short_pkg(sender, pkg);
		break;
	case MDFLD_DSI_PKG_GEN_LONG_WRITE:
		ret = send_gen_long_pkg(sender, pkg);
		break;
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_0:
	case MDFLD_DSI_PKG_MCS_SHORT_WRITE_1:
		ret = send_mcs_short_pkg(sender, pkg);
		break;
	case MDFLD_DSI_PKG_MCS_LONG_WRITE:
		ret = send_mcs_long_pkg(sender, pkg);
		break;
	default:
		dev_err(sender->dev->dev, "Invalid pkg type 0x%x\n",
							pkg->pkg_type);
		ret = -EINVAL;
	}
	send_pkg_done(sender, pkg);
	return ret;
}

static int send_pkg(struct mdfld_dsi_pkg_sender *sender,
			struct mdfld_dsi_pkg *pkg)
{
	int err ;

	/* Handle DSI error */
	err = dsi_error_handler(sender);
	if (err) {
		dev_err(sender->dev->dev, "Error handling failed\n");
		err = -EAGAIN;
		goto send_pkg_err;
	}

	/* Send pkg */
	err = do_send_pkg(sender, pkg);
	if (err) {
		dev_err(sender->dev->dev, "sent pkg failed\n");
		err = -EAGAIN;
		goto send_pkg_err;
	}

	/* FIXME: should I query complete and fifo empty here? */
send_pkg_err:
	return err;
}

static struct mdfld_dsi_pkg *pkg_sender_get_pkg_locked(
					struct mdfld_dsi_pkg_sender *sender)
{
	struct mdfld_dsi_pkg *pkg;

	if (list_empty(&sender->free_list)) {
		dev_err(sender->dev->dev, "No free pkg left\n");
		return NULL;
	}
	pkg = list_first_entry(&sender->free_list, struct mdfld_dsi_pkg, entry);
	/* Detach from free list */
	list_del_init(&pkg->entry);
	return pkg;
}

static void pkg_sender_put_pkg_locked(struct mdfld_dsi_pkg_sender *sender,
					struct mdfld_dsi_pkg *pkg)
{
	memset(pkg, 0, sizeof(struct mdfld_dsi_pkg));
	INIT_LIST_HEAD(&pkg->entry);
	list_add_tail(&pkg->entry, &sender->free_list);
}

static int mdfld_dbi_cb_init(struct mdfld_dsi_pkg_sender *sender,
					struct psb_gtt *pg, int pipe)
{
	unsigned long phys;
	void *virt_addr = NULL;

	switch (pipe) {
	case 0:
		phys = pg->gtt_phys_start - 0x1000;
		break;
	case 2:
		phys = pg->gtt_phys_start - 0x800;
		break;
	default:
		dev_err(sender->dev->dev, "Unsupported channel %d\n", pipe);
		return -EINVAL;
	}

	virt_addr = ioremap_nocache(phys, 0x800);
	if (!virt_addr) {
		dev_err(sender->dev->dev, "Map DBI command buffer error\n");
		return -ENOMEM;
	}
	sender->dbi_cb_phy = phys;
	sender->dbi_cb_addr = virt_addr;
	return 0;
}

static void mdfld_dbi_cb_destroy(struct mdfld_dsi_pkg_sender *sender)
{
	if (sender && sender->dbi_cb_addr)
		iounmap(sender->dbi_cb_addr);
}

static void pkg_sender_queue_pkg(struct mdfld_dsi_pkg_sender *sender,
					struct mdfld_dsi_pkg *pkg,
					int delay)
{
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);

	if (!delay) {
		send_pkg(sender, pkg);
		pkg_sender_put_pkg_locked(sender, pkg);
	} else {
		/* Queue it */
		list_add_tail(&pkg->entry, &sender->pkg_list);
	}
	spin_unlock_irqrestore(&sender->lock, flags);
}

static void process_pkg_list(struct mdfld_dsi_pkg_sender *sender)
{
	struct mdfld_dsi_pkg *pkg;
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);

	while (!list_empty(&sender->pkg_list)) {
		pkg = list_first_entry(&sender->pkg_list,
					struct mdfld_dsi_pkg, entry);
		send_pkg(sender, pkg);
		list_del_init(&pkg->entry);
		pkg_sender_put_pkg_locked(sender, pkg);
	}

	spin_unlock_irqrestore(&sender->lock, flags);
}

static int mdfld_dsi_send_mcs_long(struct mdfld_dsi_pkg_sender *sender,
	u32 *data, u32 len, u8 transmission, int delay)
{
	struct mdfld_dsi_pkg *pkg;
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);
	pkg = pkg_sender_get_pkg_locked(sender);
	spin_unlock_irqrestore(&sender->lock, flags);

	if (!pkg) {
		dev_err(sender->dev->dev, "No memory\n");
		return -ENOMEM;
	}
	pkg->pkg_type = MDFLD_DSI_PKG_MCS_LONG_WRITE;
	pkg->transmission_type = transmission;
	pkg->pkg.long_pkg.data = data;
	pkg->pkg.long_pkg.len = len;
	INIT_LIST_HEAD(&pkg->entry);

	pkg_sender_queue_pkg(sender, pkg, delay);
	return 0;
}

static int mdfld_dsi_send_mcs_short(struct mdfld_dsi_pkg_sender *sender,
					u8 cmd, u8 param, u8 param_num,
					u8 transmission,
					int delay)
{
	struct mdfld_dsi_pkg *pkg;
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);
	pkg = pkg_sender_get_pkg_locked(sender);
	spin_unlock_irqrestore(&sender->lock, flags);

	if (!pkg) {
		dev_err(sender->dev->dev, "No memory\n");
		return -ENOMEM;
	}

	if (param_num) {
		pkg->pkg_type = MDFLD_DSI_PKG_MCS_SHORT_WRITE_1;
		pkg->pkg.short_pkg.param = param;
	} else {
		pkg->pkg_type = MDFLD_DSI_PKG_MCS_SHORT_WRITE_0;
		pkg->pkg.short_pkg.param = 0;
	}
	pkg->transmission_type = transmission;
	pkg->pkg.short_pkg.cmd = cmd;
	INIT_LIST_HEAD(&pkg->entry);

	pkg_sender_queue_pkg(sender, pkg, delay);
	return 0;
}

static int mdfld_dsi_send_gen_short(struct mdfld_dsi_pkg_sender *sender,
					u8 param0, u8 param1, u8 param_num,
					u8 transmission,
					int delay)
{
	struct mdfld_dsi_pkg *pkg;
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);
	pkg = pkg_sender_get_pkg_locked(sender);
	spin_unlock_irqrestore(&sender->lock, flags);

	if (!pkg) {
		dev_err(sender->dev->dev, "No pkg memory\n");
		return -ENOMEM;
	}

	switch (param_num) {
	case 0:
		pkg->pkg_type = MDFLD_DSI_PKG_GEN_SHORT_WRITE_0;
		pkg->pkg.short_pkg.cmd = 0;
		pkg->pkg.short_pkg.param = 0;
		break;
	case 1:
		pkg->pkg_type = MDFLD_DSI_PKG_GEN_SHORT_WRITE_1;
		pkg->pkg.short_pkg.cmd = param0;
		pkg->pkg.short_pkg.param = 0;
		break;
	case 2:
		pkg->pkg_type = MDFLD_DSI_PKG_GEN_SHORT_WRITE_2;
		pkg->pkg.short_pkg.cmd = param0;
		pkg->pkg.short_pkg.param = param1;
		break;
	}

	pkg->transmission_type = transmission;
	INIT_LIST_HEAD(&pkg->entry);

	pkg_sender_queue_pkg(sender, pkg, delay);
	return 0;
}

static int mdfld_dsi_send_gen_long(struct mdfld_dsi_pkg_sender *sender,
				u32 *data, u32 len, u8 transmission, int delay)
{
	struct mdfld_dsi_pkg *pkg;
	unsigned long flags;

	spin_lock_irqsave(&sender->lock, flags);
	pkg = pkg_sender_get_pkg_locked(sender);
	spin_unlock_irqrestore(&sender->lock, flags);

	if (!pkg) {
		dev_err(sender->dev->dev, "No pkg memory\n");
		return -ENOMEM;
	}

	pkg->pkg_type = MDFLD_DSI_PKG_GEN_LONG_WRITE;
	pkg->transmission_type = transmission;
	pkg->pkg.long_pkg.data = data;
	pkg->pkg.long_pkg.len = len;

	INIT_LIST_HEAD(&pkg->entry);

	pkg_sender_queue_pkg(sender, pkg, delay);

	return 0;
}

void mdfld_dsi_cmds_kick_out(struct mdfld_dsi_pkg_sender *sender)
{
	process_pkg_list(sender);
}

int mdfld_dsi_send_dcs(struct mdfld_dsi_pkg_sender *sender,
			u8 dcs, u8 *param, u32 param_num, u8 data_src,
			int delay)
{
	struct mdfld_dsi_pkg *pkg;
	u32 cb_phy = sender->dbi_cb_phy;
	struct drm_device *dev = sender->dev;
	u32 index = 0;
	u8 *cb = (u8 *)sender->dbi_cb_addr;
	unsigned long flags;
	int retry;
	u8 *dst = NULL;
	u32 len;

	if (!sender) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (!sender->dbi_pkg_support) {
		dev_err(dev->dev, "No DBI pkg sending on this sender\n");
		return -ENOTSUPP;
	}

	if (param_num > MDFLD_MAX_DCS_PARAM) {
		dev_err(dev->dev, "Sender only supports up to %d DCS params\n",
							MDFLD_MAX_DCS_PARAM);
		return -EINVAL;
	}

	/*
	 * If dcs is write_mem_start, send it directly using DSI adapter
	 * interface
	 */
	if (dcs == write_mem_start) {
		if (!spin_trylock(&sender->lock))
			return -EAGAIN;

		/*
		 * query whether DBI FIFO is empty,
		 * if not wait it becoming empty
		 */
		retry = MDFLD_DSI_DBI_FIFO_TIMEOUT;
		while (retry &&
		    !(REG_READ(sender->mipi_gen_fifo_stat_reg) & (1 << 27))) {
			udelay(500);
			retry--;
		}

		/* If DBI FIFO timeout, drop this frame */
		if (!retry) {
			spin_unlock(&sender->lock);
			return 0;
		}

		*(cb + (index++)) = write_mem_start;

		REG_WRITE(sender->mipi_cmd_len_reg, 1);
		REG_WRITE(sender->mipi_cmd_addr_reg,
					cb_phy | (1 << 0) | (1 << 1));

		retry = MDFLD_DSI_DBI_FIFO_TIMEOUT;
		while (retry &&
			(REG_READ(sender->mipi_cmd_addr_reg) & (1 << 0))) {
			udelay(1);
			retry--;
		}

		spin_unlock(&sender->lock);
		return 0;
	}

	/* Get a free pkg */
	spin_lock_irqsave(&sender->lock, flags);
	pkg = pkg_sender_get_pkg_locked(sender);
	spin_unlock_irqrestore(&sender->lock, flags);

	if (!pkg) {
		dev_err(dev->dev, "No packages memory\n");
		return -ENOMEM;
	}

	dst = pkg->pkg.dcs_pkg.param;
	memcpy(dst, param, param_num);

	pkg->pkg_type = MDFLD_DSI_PKG_DCS;
	pkg->transmission_type = MDFLD_DSI_DCS;
	pkg->pkg.dcs_pkg.cmd = dcs;
	pkg->pkg.dcs_pkg.param_num = param_num;
	pkg->pkg.dcs_pkg.data_src = data_src;

	INIT_LIST_HEAD(&pkg->entry);

	if (param_num == 0)
		return mdfld_dsi_send_mcs_short_hs(sender, dcs, 0, 0, delay);
	else if (param_num == 1)
		return mdfld_dsi_send_mcs_short_hs(sender, dcs,
							param[0], 1, delay);
	else if (param_num > 1) {
		len = (param_num + 1) / 4;
		if ((param_num + 1) % 4)
			len++;
		return mdfld_dsi_send_mcs_long_hs(sender,
				(u32 *)&pkg->pkg.dcs_pkg, len, delay);
	}
	return 0;
}

int mdfld_dsi_send_mcs_short_hs(struct mdfld_dsi_pkg_sender *sender,
				u8 cmd, u8 param, u8 param_num, int delay)
{
	if (!sender) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_mcs_short(sender, cmd, param, param_num,
					MDFLD_DSI_HS_TRANSMISSION, delay);
}

int mdfld_dsi_send_mcs_short_lp(struct mdfld_dsi_pkg_sender *sender,
				u8 cmd, u8 param, u8 param_num, int delay)
{
	if (!sender) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_mcs_short(sender, cmd, param, param_num,
					MDFLD_DSI_LP_TRANSMISSION, delay);
}

int mdfld_dsi_send_mcs_long_hs(struct mdfld_dsi_pkg_sender *sender,
				u32 *data,
				u32 len,
				int delay)
{
	if (!sender || !data || !len) {
		DRM_ERROR("Invalid parameters\n");
		return -EINVAL;
	}
	return mdfld_dsi_send_mcs_long(sender, data, len,
					MDFLD_DSI_HS_TRANSMISSION, delay);
}

int mdfld_dsi_send_mcs_long_lp(struct mdfld_dsi_pkg_sender *sender,
				u32 *data,
				u32 len,
				int delay)
{
	if (!sender || !data || !len) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_mcs_long(sender, data, len,
				MDFLD_DSI_LP_TRANSMISSION, delay);
}

int mdfld_dsi_send_gen_short_hs(struct mdfld_dsi_pkg_sender *sender,
				u8 param0, u8 param1, u8 param_num, int delay)
{
	if (!sender) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_gen_short(sender, param0, param1, param_num,
					MDFLD_DSI_HS_TRANSMISSION, delay);
}

int mdfld_dsi_send_gen_short_lp(struct mdfld_dsi_pkg_sender *sender,
				u8 param0, u8 param1, u8 param_num, int delay)
{
	if (!sender || param_num < 0 || param_num > 2) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_gen_short(sender, param0, param1, param_num,
					MDFLD_DSI_LP_TRANSMISSION, delay);
}

int mdfld_dsi_send_gen_long_hs(struct mdfld_dsi_pkg_sender *sender,
				u32 *data,
				u32 len,
				int delay)
{
	if (!sender || !data || !len) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_gen_long(sender, data, len,
					MDFLD_DSI_HS_TRANSMISSION, delay);
}

int mdfld_dsi_send_gen_long_lp(struct mdfld_dsi_pkg_sender *sender,
				u32 *data,
				u32 len,
				int delay)
{
	if (!sender || !data || !len) {
		WARN_ON(1);
		return -EINVAL;
	}
	return mdfld_dsi_send_gen_long(sender, data, len,
					MDFLD_DSI_LP_TRANSMISSION, delay);
}

int mdfld_dsi_pkg_sender_init(struct mdfld_dsi_connector *dsi_connector,
								int pipe)
{
	int ret;
	struct mdfld_dsi_pkg_sender *pkg_sender;
	struct mdfld_dsi_config *dsi_config =
					mdfld_dsi_get_config(dsi_connector);
	struct drm_device *dev = dsi_config->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;
	int i;
	struct mdfld_dsi_pkg *pkg, *tmp;

	if (!dsi_connector) {
		WARN_ON(1);
		return -EINVAL;
	}

	pkg_sender = dsi_connector->pkg_sender;

	if (!pkg_sender || IS_ERR(pkg_sender)) {
		pkg_sender = kzalloc(sizeof(struct mdfld_dsi_pkg_sender),
								GFP_KERNEL);
		if (!pkg_sender) {
			dev_err(dev->dev, "Create DSI pkg sender failed\n");
			return -ENOMEM;
		}

		dsi_connector->pkg_sender = (void *)pkg_sender;
	}

	pkg_sender->dev = dev;
	pkg_sender->dsi_connector = dsi_connector;
	pkg_sender->pipe = pipe;
	pkg_sender->pkg_num = 0;
	pkg_sender->panel_mode = 0;
	pkg_sender->status = MDFLD_DSI_PKG_SENDER_FREE;

	/* Init dbi command buffer*/

	if (dsi_config->type == MDFLD_DSI_ENCODER_DBI) {
		pkg_sender->dbi_pkg_support = 1;
		ret = mdfld_dbi_cb_init(pkg_sender, pg, pipe);
		if (ret) {
			dev_err(dev->dev, "DBI command buffer map failed\n");
			goto mapping_err;
		}
	}

	/* Init regs */
	if (pipe == 0) {
		pkg_sender->dpll_reg = MRST_DPLL_A;
		pkg_sender->dspcntr_reg = DSPACNTR;
		pkg_sender->pipeconf_reg = PIPEACONF;
		pkg_sender->dsplinoff_reg = DSPALINOFF;
		pkg_sender->dspsurf_reg = DSPASURF;
		pkg_sender->pipestat_reg = PIPEASTAT;

		pkg_sender->mipi_intr_stat_reg = MIPIA_INTR_STAT_REG;
		pkg_sender->mipi_lp_gen_data_reg = MIPIA_LP_GEN_DATA_REG;
		pkg_sender->mipi_hs_gen_data_reg = MIPIA_HS_GEN_DATA_REG;
		pkg_sender->mipi_lp_gen_ctrl_reg = MIPIA_LP_GEN_CTRL_REG;
		pkg_sender->mipi_hs_gen_ctrl_reg = MIPIA_HS_GEN_CTRL_REG;
		pkg_sender->mipi_gen_fifo_stat_reg = MIPIA_GEN_FIFO_STAT_REG;
		pkg_sender->mipi_data_addr_reg = MIPIA_DATA_ADD_REG;
		pkg_sender->mipi_data_len_reg = MIPIA_DATA_LEN_REG;
		pkg_sender->mipi_cmd_addr_reg = MIPIA_CMD_ADD_REG;
		pkg_sender->mipi_cmd_len_reg = MIPIA_CMD_LEN_REG;
	} else if (pipe == 2) {
		pkg_sender->dpll_reg = MRST_DPLL_A;
		pkg_sender->dspcntr_reg = DSPCCNTR;
		pkg_sender->pipeconf_reg = PIPECCONF;
		pkg_sender->dsplinoff_reg = DSPCLINOFF;
		pkg_sender->dspsurf_reg = DSPCSURF;
		pkg_sender->pipestat_reg = 72024;

		pkg_sender->mipi_intr_stat_reg =
				MIPIA_INTR_STAT_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_lp_gen_data_reg =
				MIPIA_LP_GEN_DATA_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_hs_gen_data_reg =
				MIPIA_HS_GEN_DATA_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_lp_gen_ctrl_reg =
				MIPIA_LP_GEN_CTRL_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_hs_gen_ctrl_reg =
				MIPIA_HS_GEN_CTRL_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_gen_fifo_stat_reg =
				MIPIA_GEN_FIFO_STAT_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_data_addr_reg =
				MIPIA_DATA_ADD_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_data_len_reg =
				MIPIA_DATA_LEN_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_cmd_addr_reg =
				MIPIA_CMD_ADD_REG + MIPIC_REG_OFFSET;
		pkg_sender->mipi_cmd_len_reg =
				MIPIA_CMD_LEN_REG + MIPIC_REG_OFFSET;
	}

	/* Init pkg list */
	INIT_LIST_HEAD(&pkg_sender->pkg_list);
	INIT_LIST_HEAD(&pkg_sender->free_list);

	spin_lock_init(&pkg_sender->lock);

	/* Allocate free pkg pool */
	for (i = 0; i < MDFLD_MAX_PKG_NUM; i++) {
		pkg = kzalloc(sizeof(struct mdfld_dsi_pkg), GFP_KERNEL);
		if (!pkg) {
			dev_err(dev->dev, "Out of memory allocating pkg pool");
			ret = -ENOMEM;
			goto pkg_alloc_err;
		}
		INIT_LIST_HEAD(&pkg->entry);
		list_add_tail(&pkg->entry, &pkg_sender->free_list);
	}
	return 0;

pkg_alloc_err:
	list_for_each_entry_safe(pkg, tmp, &pkg_sender->free_list, entry) {
		list_del(&pkg->entry);
		kfree(pkg);
	}

	/* Free mapped command buffer */
	mdfld_dbi_cb_destroy(pkg_sender);
mapping_err:
	kfree(pkg_sender);
	dsi_connector->pkg_sender = NULL;
	return ret;
}

void mdfld_dsi_pkg_sender_destroy(struct mdfld_dsi_pkg_sender *sender)
{
	struct mdfld_dsi_pkg *pkg, *tmp;

	if (!sender || IS_ERR(sender))
		return;

	/* Free pkg pool */
	list_for_each_entry_safe(pkg, tmp, &sender->free_list, entry) {
		list_del(&pkg->entry);
		kfree(pkg);
	}
	/* Free pkg list */
	list_for_each_entry_safe(pkg, tmp, &sender->pkg_list, entry) {
		list_del(&pkg->entry);
		kfree(pkg);
	}
	mdfld_dbi_cb_destroy(sender);	/* free mapped command buffer */
	kfree(sender);
}

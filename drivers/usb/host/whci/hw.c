/*
 * Wireless Host Controller (WHC) hardware access helpers.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

void whc_write_wusbcmd(struct whc *whc, u32 mask, u32 val)
{
	unsigned long flags;
	u32 cmd;

	spin_lock_irqsave(&whc->lock, flags);

	cmd = le_readl(whc->base + WUSBCMD);
	cmd = (cmd & ~mask) | val;
	le_writel(cmd, whc->base + WUSBCMD);

	spin_unlock_irqrestore(&whc->lock, flags);
}

/**
 * whc_do_gencmd - start a generic command via the WUSBGENCMDSTS register
 * @whc:    the WHCI HC
 * @cmd:    command to start.
 * @params: parameters for the command (the WUSBGENCMDPARAMS register value).
 * @addr:   pointer to any data for the command (may be NULL).
 * @len:    length of the data (if any).
 */
int whc_do_gencmd(struct whc *whc, u32 cmd, u32 params, void *addr, size_t len)
{
	unsigned long flags;
	dma_addr_t dma_addr;
	int t;
	int ret = 0;

	mutex_lock(&whc->mutex);

	/* Wait for previous command to complete. */
	t = wait_event_timeout(whc->cmd_wq,
			       (le_readl(whc->base + WUSBGENCMDSTS) & WUSBGENCMDSTS_ACTIVE) == 0,
			       WHC_GENCMD_TIMEOUT_MS);
	if (t == 0) {
		dev_err(&whc->umc->dev, "generic command timeout (%04x/%04x)\n",
			le_readl(whc->base + WUSBGENCMDSTS),
			le_readl(whc->base + WUSBGENCMDPARAMS));
		ret = -ETIMEDOUT;
		goto out;
	}

	if (addr) {
		memcpy(whc->gen_cmd_buf, addr, len);
		dma_addr = whc->gen_cmd_buf_dma;
	} else
		dma_addr = 0;

	/* Poke registers to start cmd. */
	spin_lock_irqsave(&whc->lock, flags);

	le_writel(params, whc->base + WUSBGENCMDPARAMS);
	le_writeq(dma_addr, whc->base + WUSBGENADDR);

	le_writel(WUSBGENCMDSTS_ACTIVE | WUSBGENCMDSTS_IOC | cmd,
		  whc->base + WUSBGENCMDSTS);

	spin_unlock_irqrestore(&whc->lock, flags);
out:
	mutex_unlock(&whc->mutex);

	return ret;
}

/**
 * whc_hw_error - recover from a hardware error
 * @whc:    the WHCI HC that broke.
 * @reason: a description of the failure.
 *
 * Recover from broken hardware with a full reset.
 */
void whc_hw_error(struct whc *whc, const char *reason)
{
	struct wusbhc *wusbhc = &whc->wusbhc;

	dev_err(&whc->umc->dev, "hardware error: %s\n", reason);
	wusbhc_reset_all(wusbhc);
}

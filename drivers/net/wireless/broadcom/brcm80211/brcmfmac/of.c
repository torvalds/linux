/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/mmc/card.h>
#include <linux/platform_data/brcmfmac-sdio.h>
#include <linux/mmc/sdio_func.h>

#include <defs.h>
#include "debug.h"
#include "sdio.h"

void brcmf_of_probe(struct brcmf_sdio_dev *sdiodev)
{
	struct device *dev = sdiodev->dev;
	struct device_node *np = dev->of_node;
	int irq;
	u32 irqf;
	u32 val;

	if (!np || !of_device_is_compatible(np, "brcm,bcm4329-fmac"))
		return;

	sdiodev->pdata = devm_kzalloc(dev, sizeof(*sdiodev->pdata), GFP_KERNEL);
	if (!sdiodev->pdata)
		return;

	if (of_property_read_u32(np, "brcm,drive-strength", &val) == 0)
		sdiodev->pdata->drive_strength = val;

	/* make sure there are interrupts defined in the node */
	if (!of_find_property(np, "interrupts", NULL))
		return;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		brcmf_err("interrupt could not be mapped\n");
		return;
	}
	irqf = irqd_get_trigger_type(irq_get_irq_data(irq));

	sdiodev->pdata->oob_irq_supported = true;
	sdiodev->pdata->oob_irq_nr = irq;
	sdiodev->pdata->oob_irq_flags = irqf;
}

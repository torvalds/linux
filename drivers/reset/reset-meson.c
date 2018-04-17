/*
 * Amlogic Meson Reset Controller driver
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * BSD LICENSE
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of_device.h>

#define REG_COUNT	8
#define BITS_PER_REG	32
#define LEVEL_OFFSET	0x7c

struct meson_reset {
	void __iomem *reg_base;
	struct reset_controller_dev rcdev;
	spinlock_t lock;
};

static int meson_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int bank = id / BITS_PER_REG;
	unsigned int offset = id % BITS_PER_REG;
	void __iomem *reg_addr = data->reg_base + (bank << 2);

	writel(BIT(offset), reg_addr);

	return 0;
}

static int meson_reset_level(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int bank = id / BITS_PER_REG;
	unsigned int offset = id % BITS_PER_REG;
	void __iomem *reg_addr = data->reg_base + LEVEL_OFFSET + (bank << 2);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(reg_addr);
	if (assert)
		writel(reg & ~BIT(offset), reg_addr);
	else
		writel(reg | BIT(offset), reg_addr);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int meson_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return meson_reset_level(rcdev, id, true);
}

static int meson_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return meson_reset_level(rcdev, id, false);
}

static const struct reset_control_ops meson_reset_meson8_ops = {
	.reset		= meson_reset_reset,
};

static const struct reset_control_ops meson_reset_gx_ops = {
	.reset		= meson_reset_reset,
	.assert		= meson_reset_assert,
	.deassert	= meson_reset_deassert,
};

static const struct of_device_id meson_reset_dt_ids[] = {
	 { .compatible = "amlogic,meson8b-reset",
	   .data = &meson_reset_meson8_ops, },
	 { .compatible = "amlogic,meson-gxbb-reset",
	   .data = &meson_reset_gx_ops, },
	 { /* sentinel */ },
};

static int meson_reset_probe(struct platform_device *pdev)
{
	const struct reset_control_ops *ops;
	struct meson_reset *data;
	struct resource *res;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ops = of_device_get_match_data(&pdev->dev);
	if (!ops)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->reg_base))
		return PTR_ERR(data->reg_base);

	platform_set_drvdata(pdev, data);

	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = REG_COUNT * BITS_PER_REG;
	data->rcdev.ops = ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static struct platform_driver meson_reset_driver = {
	.probe	= meson_reset_probe,
	.driver = {
		.name		= "meson_reset",
		.of_match_table	= meson_reset_dt_ids,
	},
};
builtin_platform_driver(meson_reset_driver);

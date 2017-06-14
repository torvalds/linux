/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Amlogic, Inc.
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
 * Copyright (C) 2014 Amlogic, Inc.
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
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/clk.h>

#define RNG_DATA 0x00

struct meson_rng_data {
	void __iomem *base;
	struct platform_device *pdev;
	struct hwrng rng;
	struct clk *core_clk;
};

static int meson_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct meson_rng_data *data =
			container_of(rng, struct meson_rng_data, rng);

	*(u32 *)buf = readl_relaxed(data->base + RNG_DATA);

	return sizeof(u32);
}

static void meson_rng_clk_disable(void *data)
{
	clk_disable_unprepare(data);
}

static int meson_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_rng_data *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(data->core_clk))
		data->core_clk = NULL;

	if (data->core_clk) {
		ret = clk_prepare_enable(data->core_clk);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, meson_rng_clk_disable,
					       data->core_clk);
		if (ret)
			return ret;
	}

	data->rng.name = pdev->name;
	data->rng.read = meson_rng_read;

	platform_set_drvdata(pdev, data);

	return devm_hwrng_register(dev, &data->rng);
}

static const struct of_device_id meson_rng_of_match[] = {
	{ .compatible = "amlogic,meson-rng", },
	{},
};
MODULE_DEVICE_TABLE(of, meson_rng_of_match);

static struct platform_driver meson_rng_driver = {
	.probe	= meson_rng_probe,
	.driver	= {
		.name = "meson-rng",
		.of_match_table = meson_rng_of_match,
	},
};

module_platform_driver(meson_rng_driver);

MODULE_DESCRIPTION("Meson H/W Random Number Generator driver");
MODULE_AUTHOR("Lawrence Mok <lawrence.mok@amlogic.com>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");

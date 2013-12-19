/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk-provider.h>
#include <linux/genalloc.h>
#include <linux/of_platform.h>

void socfpga_init_ocram_ecc(void)
{
	struct device_node *np;
	const __be32 *prop;
	u32 ocr_edac_addr, iram_addr, len;
	void __iomem  *mapped_ocr_edac_addr;
	size_t size;
	struct gen_pool *gp;

	np = of_find_compatible_node(NULL, NULL, "altr,ocram-edac");
	if (!np) {
		pr_err("SOCFPGA: Unable to find altr,ocram-edac in dtb\n");
		return;
	}

	prop = of_get_property(np, "reg", &size);
	ocr_edac_addr = be32_to_cpup(prop++);
	len = be32_to_cpup(prop);
	if (!prop || size < sizeof(*prop)) {
		pr_err("SOCFPGA: Unable to find OCRAM ECC mapping in dtb\n");
		return;
	}

	gp = of_get_named_gen_pool(np, "iram", 0);
	if (!gp) {
		pr_err("SOCFPGA: OCRAM cannot find gen pool\n");
		return;
	}

	np = of_find_compatible_node(NULL, NULL, "mmio-sram");
	if (!np) {
		pr_err("SOCFPGA: Unable to find mmio-sram in dtb\n");
		return;
	}
	/* Determine the OCRAM address and size */
	prop = of_get_property(np, "reg", &size);
	iram_addr = be32_to_cpup(prop++);
	len = be32_to_cpup(prop);

	if (!prop || size < sizeof(*prop)) {
		pr_err("SOCFPGA: Unable to find OCRAM mapping in dtb\n");
		return;
	}

	iram_addr = gen_pool_alloc(gp, len);
	if (iram_addr == 0) {
		pr_err("SOCFPGA: cannot alloc from gen pool\n");
		return;
	}

	memset((void *)iram_addr, 0, len);

	mapped_ocr_edac_addr = ioremap(ocr_edac_addr, 4);

	gen_pool_free(gp, iram_addr, len);

	/* Clear any pending OCRAM ECC interrupts, then enable ECC */
	writel(0x18, mapped_ocr_edac_addr);
	writel(0x19, mapped_ocr_edac_addr);

	pr_alert("SOCFPGA: Success Initializing OCRAM");

	return;
}


/*
 * Renesas SoC Identification
 *
 * Copyright (C) 2014-2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sys_soc.h>


struct renesas_family {
	const char name[16];
	u32 reg;			/* CCCR or PRR, if not in DT */
};

static const struct renesas_family fam_rcar_gen1 __initconst __maybe_unused = {
	.name	= "R-Car Gen1",
	.reg	= 0xff000044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_rcar_gen2 __initconst __maybe_unused = {
	.name	= "R-Car Gen2",
	.reg	= 0xff000044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_rcar_gen3 __initconst __maybe_unused = {
	.name	= "R-Car Gen3",
	.reg	= 0xfff00044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_rmobile __initconst __maybe_unused = {
	.name	= "R-Mobile",
	.reg	= 0xe600101c,		/* CCCR (Common Chip Code Register) */
};

static const struct renesas_family fam_rza __initconst __maybe_unused = {
	.name	= "RZ/A",
};

static const struct renesas_family fam_rzg __initconst __maybe_unused = {
	.name	= "RZ/G",
	.reg	= 0xff000044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_shmobile __initconst __maybe_unused = {
	.name	= "SH-Mobile",
	.reg	= 0xe600101c,		/* CCCR (Common Chip Code Register) */
};


struct renesas_soc {
	const struct renesas_family *family;
	u8 id;
};

static const struct renesas_soc soc_rz_a1h __initconst __maybe_unused = {
	.family	= &fam_rza,
};

static const struct renesas_soc soc_rmobile_ape6 __initconst __maybe_unused = {
	.family	= &fam_rmobile,
	.id	= 0x3f,
};

static const struct renesas_soc soc_rmobile_a1 __initconst __maybe_unused = {
	.family	= &fam_rmobile,
	.id	= 0x40,
};

static const struct renesas_soc soc_rz_g1h __initconst __maybe_unused = {
	.family	= &fam_rzg,
	.id	= 0x45,
};

static const struct renesas_soc soc_rz_g1m __initconst __maybe_unused = {
	.family	= &fam_rzg,
	.id	= 0x47,
};

static const struct renesas_soc soc_rz_g1n __initconst __maybe_unused = {
	.family	= &fam_rzg,
	.id	= 0x4b,
};

static const struct renesas_soc soc_rz_g1e __initconst __maybe_unused = {
	.family	= &fam_rzg,
	.id	= 0x4c,
};

static const struct renesas_soc soc_rcar_m1a __initconst __maybe_unused = {
	.family	= &fam_rcar_gen1,
};

static const struct renesas_soc soc_rcar_h1 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen1,
	.id	= 0x3b,
};

static const struct renesas_soc soc_rcar_h2 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen2,
	.id	= 0x45,
};

static const struct renesas_soc soc_rcar_m2_w __initconst __maybe_unused = {
	.family	= &fam_rcar_gen2,
	.id	= 0x47,
};

static const struct renesas_soc soc_rcar_v2h __initconst __maybe_unused = {
	.family	= &fam_rcar_gen2,
	.id	= 0x4a,
};

static const struct renesas_soc soc_rcar_m2_n __initconst __maybe_unused = {
	.family	= &fam_rcar_gen2,
	.id	= 0x4b,
};

static const struct renesas_soc soc_rcar_e2 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen2,
	.id	= 0x4c,
};

static const struct renesas_soc soc_rcar_h3 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x4f,
};

static const struct renesas_soc soc_rcar_m3_w __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x52,
};

static const struct renesas_soc soc_rcar_v3m __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x54,
};

static const struct renesas_soc soc_rcar_v3h __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x56,
};

static const struct renesas_soc soc_rcar_d3 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x58,
};

static const struct renesas_soc soc_shmobile_ag5 __initconst __maybe_unused = {
	.family	= &fam_shmobile,
	.id	= 0x37,
};


static const struct of_device_id renesas_socs[] __initconst = {
#ifdef CONFIG_ARCH_R7S72100
	{ .compatible = "renesas,r7s72100",	.data = &soc_rz_a1h },
#endif
#ifdef CONFIG_ARCH_R8A73A4
	{ .compatible = "renesas,r8a73a4",	.data = &soc_rmobile_ape6 },
#endif
#ifdef CONFIG_ARCH_R8A7740
	{ .compatible = "renesas,r8a7740",	.data = &soc_rmobile_a1 },
#endif
#ifdef CONFIG_ARCH_R8A7742
	{ .compatible = "renesas,r8a7742",	.data = &soc_rz_g1h },
#endif
#ifdef CONFIG_ARCH_R8A7743
	{ .compatible = "renesas,r8a7743",	.data = &soc_rz_g1m },
#endif
#ifdef CONFIG_ARCH_R8A7744
	{ .compatible = "renesas,r8a7744",	.data = &soc_rz_g1n },
#endif
#ifdef CONFIG_ARCH_R8A7745
	{ .compatible = "renesas,r8a7745",	.data = &soc_rz_g1e },
#endif
#ifdef CONFIG_ARCH_R8A7778
	{ .compatible = "renesas,r8a7778",	.data = &soc_rcar_m1a },
#endif
#ifdef CONFIG_ARCH_R8A7779
	{ .compatible = "renesas,r8a7779",	.data = &soc_rcar_h1 },
#endif
#ifdef CONFIG_ARCH_R8A7790
	{ .compatible = "renesas,r8a7790",	.data = &soc_rcar_h2 },
#endif
#ifdef CONFIG_ARCH_R8A7791
	{ .compatible = "renesas,r8a7791",	.data = &soc_rcar_m2_w },
#endif
#ifdef CONFIG_ARCH_R8A7792
	{ .compatible = "renesas,r8a7792",	.data = &soc_rcar_v2h },
#endif
#ifdef CONFIG_ARCH_R8A7793
	{ .compatible = "renesas,r8a7793",	.data = &soc_rcar_m2_n },
#endif
#ifdef CONFIG_ARCH_R8A7794
	{ .compatible = "renesas,r8a7794",	.data = &soc_rcar_e2 },
#endif
#ifdef CONFIG_ARCH_R8A7795
	{ .compatible = "renesas,r8a7795",	.data = &soc_rcar_h3 },
#endif
#ifdef CONFIG_ARCH_R8A7796
	{ .compatible = "renesas,r8a7796",	.data = &soc_rcar_m3_w },
#endif
#ifdef CONFIG_ARCH_R8A77970
	{ .compatible = "renesas,r8a77970",	.data = &soc_rcar_v3m },
#endif
#ifdef CONFIG_ARCH_R8A77980
	{ .compatible = "renesas,r8a77980",	.data = &soc_rcar_v3h },
#endif
#ifdef CONFIG_ARCH_R8A77995
	{ .compatible = "renesas,r8a77995",	.data = &soc_rcar_d3 },
#endif
#ifdef CONFIG_ARCH_SH73A0
	{ .compatible = "renesas,sh73a0",	.data = &soc_shmobile_ag5 },
#endif
	{ /* sentinel */ }
};

static int __init renesas_soc_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	const struct renesas_family *family;
	const struct of_device_id *match;
	const struct renesas_soc *soc;
	void __iomem *chipid = NULL;
	struct soc_device *soc_dev;
	struct device_node *np;
	unsigned int product;

	match = of_match_node(renesas_socs, of_root);
	if (!match)
		return -ENODEV;

	soc = match->data;
	family = soc->family;

	/* Try PRR first, then hardcoded fallback */
	np = of_find_compatible_node(NULL, NULL, "renesas,prr");
	if (np) {
		chipid = of_iomap(np, 0);
		of_node_put(np);
	} else if (soc->id) {
		chipid = ioremap(family->reg, 4);
	}
	if (chipid) {
		product = readl(chipid);
		iounmap(chipid);
		/* R-Car M3-W ES1.1 incorrectly identifies as ES2.0 */
		if ((product & 0x7fff) == 0x5210)
			product ^= 0x11;
		if (soc->id && ((product >> 8) & 0xff) != soc->id) {
			pr_warn("SoC mismatch (product = 0x%x)\n", product);
			return -ENODEV;
		}
	}

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_dev_attr->machine);
	of_node_put(np);

	soc_dev_attr->family = kstrdup_const(family->name, GFP_KERNEL);
	soc_dev_attr->soc_id = kstrdup_const(strchr(match->compatible, ',') + 1,
					     GFP_KERNEL);
	if (chipid)
		soc_dev_attr->revision = kasprintf(GFP_KERNEL, "ES%u.%u",
						   ((product >> 4) & 0x0f) + 1,
						   product & 0xf);

	pr_info("Detected Renesas %s %s %s\n", soc_dev_attr->family,
		soc_dev_attr->soc_id, soc_dev_attr->revision ?: "");

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree_const(soc_dev_attr->soc_id);
		kfree_const(soc_dev_attr->family);
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
early_initcall(renesas_soc_init);

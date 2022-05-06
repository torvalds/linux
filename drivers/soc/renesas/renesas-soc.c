// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas SoC Identification
 *
 * Copyright (C) 2014-2016 Glider bvba
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

static const struct renesas_family fam_rcar_gen4 __initconst __maybe_unused = {
	.name	= "R-Car Gen4",
};

static const struct renesas_family fam_rmobile __initconst __maybe_unused = {
	.name	= "R-Mobile",
	.reg	= 0xe600101c,		/* CCCR (Common Chip Code Register) */
};

static const struct renesas_family fam_rza1 __initconst __maybe_unused = {
	.name	= "RZ/A1",
};

static const struct renesas_family fam_rza2 __initconst __maybe_unused = {
	.name	= "RZ/A2",
};

static const struct renesas_family fam_rzg1 __initconst __maybe_unused = {
	.name	= "RZ/G1",
	.reg	= 0xff000044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_rzg2 __initconst __maybe_unused = {
	.name	= "RZ/G2",
	.reg	= 0xfff00044,		/* PRR (Product Register) */
};

static const struct renesas_family fam_rzg2l __initconst __maybe_unused = {
	.name	= "RZ/G2L",
};

static const struct renesas_family fam_rzg2ul __initconst __maybe_unused = {
	.name	= "RZ/G2UL",
};

static const struct renesas_family fam_rzv2l __initconst __maybe_unused = {
	.name	= "RZ/V2L",
};

static const struct renesas_family fam_shmobile __initconst __maybe_unused = {
	.name	= "SH-Mobile",
	.reg	= 0xe600101c,		/* CCCR (Common Chip Code Register) */
};


struct renesas_soc {
	const struct renesas_family *family;
	u32 id;
};

static const struct renesas_soc soc_rz_a1h __initconst __maybe_unused = {
	.family	= &fam_rza1,
};

static const struct renesas_soc soc_rz_a2m __initconst __maybe_unused = {
	.family	= &fam_rza2,
	.id	= 0x3b,
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
	.family	= &fam_rzg1,
	.id	= 0x45,
};

static const struct renesas_soc soc_rz_g1m __initconst __maybe_unused = {
	.family	= &fam_rzg1,
	.id	= 0x47,
};

static const struct renesas_soc soc_rz_g1n __initconst __maybe_unused = {
	.family	= &fam_rzg1,
	.id	= 0x4b,
};

static const struct renesas_soc soc_rz_g1e __initconst __maybe_unused = {
	.family	= &fam_rzg1,
	.id	= 0x4c,
};

static const struct renesas_soc soc_rz_g1c __initconst __maybe_unused = {
	.family	= &fam_rzg1,
	.id	= 0x53,
};

static const struct renesas_soc soc_rz_g2m __initconst __maybe_unused = {
	.family	= &fam_rzg2,
	.id	= 0x52,
};

static const struct renesas_soc soc_rz_g2n __initconst __maybe_unused = {
	.family = &fam_rzg2,
	.id     = 0x55,
};

static const struct renesas_soc soc_rz_g2e __initconst __maybe_unused = {
	.family	= &fam_rzg2,
	.id	= 0x57,
};

static const struct renesas_soc soc_rz_g2h __initconst __maybe_unused = {
	.family	= &fam_rzg2,
	.id	= 0x4f,
};

static const struct renesas_soc soc_rz_g2l __initconst __maybe_unused = {
	.family = &fam_rzg2l,
	.id     = 0x841c447,
};

static const struct renesas_soc soc_rz_g2ul __initconst __maybe_unused = {
	.family = &fam_rzg2ul,
	.id     = 0x8450447,
};

static const struct renesas_soc soc_rz_v2l __initconst __maybe_unused = {
	.family = &fam_rzv2l,
	.id     = 0x8447447,
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

static const struct renesas_soc soc_rcar_m3_n __initconst __maybe_unused = {
	.family = &fam_rcar_gen3,
	.id     = 0x55,
};

static const struct renesas_soc soc_rcar_v3m __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x54,
};

static const struct renesas_soc soc_rcar_v3h __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x56,
};

static const struct renesas_soc soc_rcar_e3 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x57,
};

static const struct renesas_soc soc_rcar_d3 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen3,
	.id	= 0x58,
};

static const struct renesas_soc soc_rcar_v3u __initconst __maybe_unused = {
	.family	= &fam_rcar_gen4,
	.id	= 0x59,
};

static const struct renesas_soc soc_rcar_s4 __initconst __maybe_unused = {
	.family	= &fam_rcar_gen4,
	.id	= 0x5a,
};

static const struct renesas_soc soc_rcar_v4h __initconst __maybe_unused = {
	.family	= &fam_rcar_gen4,
	.id	= 0x5c,
};

static const struct renesas_soc soc_shmobile_ag5 __initconst __maybe_unused = {
	.family	= &fam_shmobile,
	.id	= 0x37,
};


static const struct of_device_id renesas_socs[] __initconst = {
#ifdef CONFIG_ARCH_R7S72100
	{ .compatible = "renesas,r7s72100",	.data = &soc_rz_a1h },
#endif
#ifdef CONFIG_ARCH_R7S9210
	{ .compatible = "renesas,r7s9210",	.data = &soc_rz_a2m },
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
#ifdef CONFIG_ARCH_R8A77470
	{ .compatible = "renesas,r8a77470",	.data = &soc_rz_g1c },
#endif
#ifdef CONFIG_ARCH_R8A774A1
	{ .compatible = "renesas,r8a774a1",	.data = &soc_rz_g2m },
#endif
#ifdef CONFIG_ARCH_R8A774B1
	{ .compatible = "renesas,r8a774b1",	.data = &soc_rz_g2n },
#endif
#ifdef CONFIG_ARCH_R8A774C0
	{ .compatible = "renesas,r8a774c0",	.data = &soc_rz_g2e },
#endif
#ifdef CONFIG_ARCH_R8A774E1
	{ .compatible = "renesas,r8a774e1",	.data = &soc_rz_g2h },
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
#if defined(CONFIG_ARCH_R8A77950) || defined(CONFIG_ARCH_R8A77951)
	{ .compatible = "renesas,r8a7795",	.data = &soc_rcar_h3 },
#endif
#ifdef CONFIG_ARCH_R8A77951
	{ .compatible = "renesas,r8a779m0",	.data = &soc_rcar_h3 },
	{ .compatible = "renesas,r8a779m1",	.data = &soc_rcar_h3 },
	{ .compatible = "renesas,r8a779m8",	.data = &soc_rcar_h3 },
#endif
#ifdef CONFIG_ARCH_R8A77960
	{ .compatible = "renesas,r8a7796",	.data = &soc_rcar_m3_w },
#endif
#ifdef CONFIG_ARCH_R8A77961
	{ .compatible = "renesas,r8a77961",	.data = &soc_rcar_m3_w },
	{ .compatible = "renesas,r8a779m2",	.data = &soc_rcar_m3_w },
	{ .compatible = "renesas,r8a779m3",	.data = &soc_rcar_m3_w },
#endif
#ifdef CONFIG_ARCH_R8A77965
	{ .compatible = "renesas,r8a77965",	.data = &soc_rcar_m3_n },
	{ .compatible = "renesas,r8a779m4",	.data = &soc_rcar_m3_n },
	{ .compatible = "renesas,r8a779m5",	.data = &soc_rcar_m3_n },
#endif
#ifdef CONFIG_ARCH_R8A77970
	{ .compatible = "renesas,r8a77970",	.data = &soc_rcar_v3m },
#endif
#ifdef CONFIG_ARCH_R8A77980
	{ .compatible = "renesas,r8a77980",	.data = &soc_rcar_v3h },
#endif
#ifdef CONFIG_ARCH_R8A77990
	{ .compatible = "renesas,r8a77990",	.data = &soc_rcar_e3 },
	{ .compatible = "renesas,r8a779m6",	.data = &soc_rcar_e3 },
#endif
#ifdef CONFIG_ARCH_R8A77995
	{ .compatible = "renesas,r8a77995",	.data = &soc_rcar_d3 },
	{ .compatible = "renesas,r8a779m7",	.data = &soc_rcar_d3 },
#endif
#ifdef CONFIG_ARCH_R8A779A0
	{ .compatible = "renesas,r8a779a0",	.data = &soc_rcar_v3u },
#endif
#ifdef CONFIG_ARCH_R8A779F0
	{ .compatible = "renesas,r8a779f0",	.data = &soc_rcar_s4 },
#endif
#ifdef CONFIG_ARCH_R8A779G0
	{ .compatible = "renesas,r8a779g0",	.data = &soc_rcar_v4h },
#endif
#if defined(CONFIG_ARCH_R9A07G043)
	{ .compatible = "renesas,r9a07g043",	.data = &soc_rz_g2ul },
#endif
#if defined(CONFIG_ARCH_R9A07G044)
	{ .compatible = "renesas,r9a07g044",	.data = &soc_rz_g2l },
#endif
#if defined(CONFIG_ARCH_R9A07G054)
	{ .compatible = "renesas,r9a07g054",	.data = &soc_rz_v2l },
#endif
#ifdef CONFIG_ARCH_SH73A0
	{ .compatible = "renesas,sh73a0",	.data = &soc_shmobile_ag5 },
#endif
	{ /* sentinel */ }
};

struct renesas_id {
	unsigned int offset;
	u32 mask;
};

static const struct renesas_id id_bsid __initconst = {
	.offset = 0,
	.mask = 0xff0000,
	/*
	 * TODO: Upper 4 bits of BSID are for chip version, but the format is
	 * not known at this time so we don't know how to specify eshi and eslo
	 */
};

static const struct renesas_id id_rzg2l __initconst = {
	.offset = 0xa04,
	.mask = 0xfffffff,
};

static const struct renesas_id id_prr __initconst = {
	.offset = 0,
	.mask = 0xff00,
};

static const struct of_device_id renesas_ids[] __initconst = {
	{ .compatible = "renesas,bsid",			.data = &id_bsid },
	{ .compatible = "renesas,r9a07g043-sysc",	.data = &id_rzg2l },
	{ .compatible = "renesas,r9a07g044-sysc",	.data = &id_rzg2l },
	{ .compatible = "renesas,r9a07g054-sysc",	.data = &id_rzg2l },
	{ .compatible = "renesas,prr",			.data = &id_prr },
	{ /* sentinel */ }
};

static int __init renesas_soc_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	unsigned int product, eshi = 0, eslo;
	const struct renesas_family *family;
	const struct of_device_id *match;
	const struct renesas_soc *soc;
	const struct renesas_id *id;
	void __iomem *chipid = NULL;
	const char *rev_prefix = "";
	struct soc_device *soc_dev;
	struct device_node *np;
	const char *soc_id;
	int ret;

	match = of_match_node(renesas_socs, of_root);
	if (!match)
		return -ENODEV;

	soc_id = strchr(match->compatible, ',') + 1;
	soc = match->data;
	family = soc->family;

	np = of_find_matching_node_and_match(NULL, renesas_ids, &match);
	if (np) {
		id = match->data;
		chipid = of_iomap(np, 0);
		of_node_put(np);
	} else if (soc->id && family->reg) {
		/* Try hardcoded CCCR/PRR fallback */
		id = &id_prr;
		chipid = ioremap(family->reg, 4);
	}

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_dev_attr->machine);
	of_node_put(np);

	soc_dev_attr->family = kstrdup_const(family->name, GFP_KERNEL);
	soc_dev_attr->soc_id = kstrdup_const(soc_id, GFP_KERNEL);

	if (chipid) {
		product = readl(chipid + id->offset);
		iounmap(chipid);

		if (id == &id_prr) {
			/* R-Car M3-W ES1.1 incorrectly identifies as ES2.0 */
			if ((product & 0x7fff) == 0x5210)
				product ^= 0x11;
			/* R-Car M3-W ES1.3 incorrectly identifies as ES2.1 */
			if ((product & 0x7fff) == 0x5211)
				product ^= 0x12;

			eshi = ((product >> 4) & 0x0f) + 1;
			eslo = product & 0xf;
			soc_dev_attr->revision = kasprintf(GFP_KERNEL, "ES%u.%u",
							   eshi, eslo);
		}  else if (id == &id_rzg2l) {
			eshi =  ((product >> 28) & 0x0f);
			soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%u",
							   eshi);
			rev_prefix = "Rev ";
		}

		if (soc->id &&
		    ((product & id->mask) >> __ffs(id->mask)) != soc->id) {
			pr_warn("SoC mismatch (product = 0x%x)\n", product);
			ret = -ENODEV;
			goto free_soc_dev_attr;
		}
	}

	pr_info("Detected Renesas %s %s %s%s\n", soc_dev_attr->family,
		soc_dev_attr->soc_id, rev_prefix, soc_dev_attr->revision ?: "");

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto free_soc_dev_attr;
	}

	return 0;

free_soc_dev_attr:
	kfree(soc_dev_attr->revision);
	kfree_const(soc_dev_attr->soc_id);
	kfree_const(soc_dev_attr->family);
	kfree(soc_dev_attr);
	return ret;
}
early_initcall(renesas_soc_init);

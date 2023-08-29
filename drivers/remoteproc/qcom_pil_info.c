// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020 Linaro Ltd.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include "qcom_pil_info.h"
#include <linux/syscore_ops.h>

/*
 * The PIL relocation information region is used to communicate memory regions
 * occupied by co-processor firmware for post mortem crash analysis.
 *
 * It consists of an array of entries with an 8 byte textual identifier of the
 * region followed by a 64 bit base address and 32 bit size, both little
 * endian.
 */
#define PIL_RELOC_NAME_LEN	8
#define PIL_RELOC_ENTRY_SIZE	(PIL_RELOC_NAME_LEN + sizeof(__le64) + sizeof(__le32))

struct pil_reloc {
	void __iomem *base;
	size_t num_entries;
};

static struct pil_reloc _reloc __read_mostly;
static DEFINE_MUTEX(pil_reloc_lock);
static bool timeouts_disabled;

#ifdef CONFIG_HIBERNATION
static bool hibernation;
#endif

/**
 * qcom_pil_timeouts_disabled() - Check if pil timeouts are disabled in imem
 *
 * Return: true if the value 0x53444247 is set in the disable timeout pil
 * imem region, false otherwise.
 */
bool qcom_pil_timeouts_disabled(void)
{
	struct device_node *np;
	struct resource imem;
	void __iomem *base;
	int ret;
	const char *prop = "qcom,msm-imem-pil-disable-timeout";

	np = of_find_compatible_node(NULL, NULL, prop);
	if (!np) {
		pr_err("%s entry missing!\n", prop);
		goto out;
	}

	ret = of_address_to_resource(np, 0, &imem);
	of_node_put(np);
	if (ret < 0) {
		pr_err("address to resource conversion failed for %s\n", prop);
		goto out;
	}

	base = ioremap(imem.start, resource_size(&imem));
	if (!base) {
		pr_err("failed to map PIL disable timeouts region\n");
		goto out;
	}

	if (__raw_readl(base) == 0x53444247) {
		pr_info("pil-imem set to disable pil timeouts\n");
		timeouts_disabled = true;
	} else
		timeouts_disabled = false;

	iounmap(base);

out:
	return timeouts_disabled;
}
EXPORT_SYMBOL(qcom_pil_timeouts_disabled);

#ifdef CONFIG_HIBERNATION
static void pil_reloc_restore_syscore_resume(void)
{
	if (_reloc.base) {
		mutex_lock(&pil_reloc_lock);
		iounmap(_reloc.base);
		_reloc.base = NULL;
		mutex_unlock(&pil_reloc_lock);
	} else
		pr_info("The PIL relocation information region is not mapped\n");
}

static struct syscore_ops pil_reloc_restore_syscore_ops = {
	.resume = pil_reloc_restore_syscore_resume,
};
#endif

static int qcom_pil_info_init(void)
{
	struct device_node *np;
	struct resource imem;
	void __iomem *base;
	int ret;

#ifdef CONFIG_HIBERNATION
	if (!hibernation) {
		register_syscore_ops(&pil_reloc_restore_syscore_ops);
		hibernation = true;
	}
#endif

	/* Already initialized? */
	if (_reloc.base)
		return 0;

	np = of_find_compatible_node(NULL, NULL, "qcom,pil-reloc-info");
	if (!np)
		return -ENOENT;

	ret = of_address_to_resource(np, 0, &imem);
	of_node_put(np);
	if (ret < 0)
		return ret;

	base = ioremap(imem.start, resource_size(&imem));
	if (!base) {
		pr_err("failed to map PIL relocation info region\n");
		return -ENOMEM;
	}

	memset_io(base, 0, resource_size(&imem));

	_reloc.base = base;
	_reloc.num_entries = (u32)resource_size(&imem) / PIL_RELOC_ENTRY_SIZE;

	return 0;
}

/**
 * qcom_pil_info_store() - store PIL information of image in IMEM
 * @image:	name of the image
 * @base:	base address of the loaded image
 * @size:	size of the loaded image
 *
 * Return: 0 on success, negative errno on failure
 */
int qcom_pil_info_store(const char *image, phys_addr_t base, size_t size)
{
	char buf[PIL_RELOC_NAME_LEN];
	void __iomem *entry;
	size_t entry_size;
	int ret;
	int i;

	mutex_lock(&pil_reloc_lock);
	ret = qcom_pil_info_init();
	if (ret < 0) {
		mutex_unlock(&pil_reloc_lock);
		return ret;
	}

	for (i = 0; i < _reloc.num_entries; i++) {
		entry = _reloc.base + i * PIL_RELOC_ENTRY_SIZE;

		memcpy_fromio(buf, entry, PIL_RELOC_NAME_LEN);

		/*
		 * An empty record means we didn't find it, given that the
		 * records are packed.
		 */
		if (!buf[0])
			goto found_unused;

		if (!strncmp(buf, image, PIL_RELOC_NAME_LEN))
			goto found_existing;
	}

	pr_warn("insufficient PIL info slots\n");
	mutex_unlock(&pil_reloc_lock);
	return -ENOMEM;

found_unused:
	entry_size = min(strlen(image), PIL_RELOC_ENTRY_SIZE - 1);
	memcpy_toio(entry, image, entry_size);
found_existing:
	/* Use two writel() as base is only aligned to 4 bytes on odd entries */
	writel(base, entry + PIL_RELOC_NAME_LEN);
	writel((u64)base >> 32, entry + PIL_RELOC_NAME_LEN + 4);
	writel(size, entry + PIL_RELOC_NAME_LEN + sizeof(__le64));
	mutex_unlock(&pil_reloc_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_pil_info_store);

static void __exit pil_reloc_exit(void)
{
	mutex_lock(&pil_reloc_lock);
	iounmap(_reloc.base);
	_reloc.base = NULL;
	mutex_unlock(&pil_reloc_lock);
}
module_exit(pil_reloc_exit);

MODULE_DESCRIPTION("Qualcomm PIL relocation info");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K RVU Hardware Random Number Generator.
 *
 * Copyright (C) 2021 Marvell.
 *
 */

#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/delay.h>

#include <linux/arm-smccc.h>

/* CSRs */
#define RNM_CTL_STATUS		0x000
#define RNM_ENTROPY_STATUS	0x008
#define RNM_CONST		0x030
#define RNM_EBG_ENT		0x048
#define RNM_PF_EBG_HEALTH	0x050
#define RNM_PF_RANDOM		0x400
#define RNM_TRNG_RESULT		0x408

struct cn10k_rng {
	void __iomem *reg_base;
	struct hwrng ops;
	struct pci_dev *pdev;
};

#define PLAT_OCTEONTX_RESET_RNG_EBG_HEALTH_STATE     0xc2000b0f

static unsigned long reset_rng_health_state(struct cn10k_rng *rng)
{
	struct arm_smccc_res res;

	/* Send SMC service call to reset EBG health state */
	arm_smccc_smc(PLAT_OCTEONTX_RESET_RNG_EBG_HEALTH_STATE, 0, 0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static int check_rng_health(struct cn10k_rng *rng)
{
	u64 status;
	unsigned long err;

	/* Skip checking health */
	if (!rng->reg_base)
		return -ENODEV;

	status = readq(rng->reg_base + RNM_PF_EBG_HEALTH);
	if (status & BIT_ULL(20)) {
		err = reset_rng_health_state(rng);
		if (err) {
			dev_err(&rng->pdev->dev, "HWRNG: Health test failed (status=%llx)\n",
					status);
			dev_err(&rng->pdev->dev, "HWRNG: error during reset (error=%lx)\n",
					err);
			return -EIO;
		}
	}
	return 0;
}

static void cn10k_read_trng(struct cn10k_rng *rng, u64 *value)
{
	u64 upper, lower;

	*value = readq(rng->reg_base + RNM_PF_RANDOM);

	/* HW can run out of entropy if large amount random data is read in
	 * quick succession. Zeros may not be real random data from HW.
	 */
	if (!*value) {
		upper = readq(rng->reg_base + RNM_PF_RANDOM);
		lower = readq(rng->reg_base + RNM_PF_RANDOM);
		while (!(upper & 0x00000000FFFFFFFFULL))
			upper = readq(rng->reg_base + RNM_PF_RANDOM);
		while (!(lower & 0xFFFFFFFF00000000ULL))
			lower = readq(rng->reg_base + RNM_PF_RANDOM);

		*value = (upper & 0xFFFFFFFF00000000) | (lower & 0xFFFFFFFF);
	}
}

static int cn10k_rng_read(struct hwrng *hwrng, void *data,
			  size_t max, bool wait)
{
	struct cn10k_rng *rng = (struct cn10k_rng *)hwrng->priv;
	unsigned int size;
	u8 *pos = data;
	int err = 0;
	u64 value;

	err = check_rng_health(rng);
	if (err)
		return err;

	size = max;

	while (size >= 8) {
		cn10k_read_trng(rng, &value);

		*((u64 *)pos) = value;
		size -= 8;
		pos += 8;
	}

	if (size > 0) {
		cn10k_read_trng(rng, &value);

		while (size > 0) {
			*pos = (u8)value;
			value >>= 8;
			size--;
			pos++;
		}
	}

	return max - size;
}

static int cn10k_rng_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct	cn10k_rng *rng;
	int	err;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->pdev = pdev;
	pci_set_drvdata(pdev, rng);

	rng->reg_base = pcim_iomap(pdev, 0, 0);
	if (!rng->reg_base) {
		dev_err(&pdev->dev, "Error while mapping CSRs, exiting\n");
		return -ENOMEM;
	}

	rng->ops.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "cn10k-rng-%s", dev_name(&pdev->dev));
	if (!rng->ops.name)
		return -ENOMEM;

	rng->ops.read    = cn10k_rng_read;
	rng->ops.quality = 1000;
	rng->ops.priv = (unsigned long)rng;

	reset_rng_health_state(rng);

	err = devm_hwrng_register(&pdev->dev, &rng->ops);
	if (err) {
		dev_err(&pdev->dev, "Could not register hwrng device.\n");
		return err;
	}

	return 0;
}

static void cn10k_rng_remove(struct pci_dev *pdev)
{
	/* Nothing to do */
}

static const struct pci_device_id cn10k_rng_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xA098) }, /* RNG PF */
	{0,},
};

MODULE_DEVICE_TABLE(pci, cn10k_rng_id_table);

static struct pci_driver cn10k_rng_driver = {
	.name		= "cn10k_rng",
	.id_table	= cn10k_rng_id_table,
	.probe		= cn10k_rng_probe,
	.remove		= cn10k_rng_remove,
};

module_pci_driver(cn10k_rng_driver);
MODULE_AUTHOR("Sunil Goutham <sgoutham@marvell.com>");
MODULE_DESCRIPTION("Marvell CN10K HW RNG Driver");
MODULE_LICENSE("GPL v2");

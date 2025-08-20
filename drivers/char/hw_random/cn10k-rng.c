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

/* Extended TRNG Read and Status Registers */
#define RNM_PF_TRNG_DAT		0x1000
#define RNM_PF_TRNG_RES		0x1008

struct cn10k_rng {
	void __iomem *reg_base;
	struct hwrng ops;
	struct pci_dev *pdev;
	/* Octeon CN10K-A A0/A1, CNF10K-A A0/A1 and CNF10K-B A0/B0
	 * does not support extended TRNG registers
	 */
	bool extended_trng_regs;
};

#define PLAT_OCTEONTX_RESET_RNG_EBG_HEALTH_STATE     0xc2000b0f

#define PCI_SUBSYS_DEVID_CN10K_A_RNG	0xB900
#define PCI_SUBSYS_DEVID_CNF10K_A_RNG	0xBA00
#define PCI_SUBSYS_DEVID_CNF10K_B_RNG	0xBC00

static bool cn10k_is_extended_trng_regs_supported(struct pci_dev *pdev)
{
	/* CN10K-A A0/A1 */
	if ((pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_A_RNG) &&
	    (!pdev->revision || (pdev->revision & 0xff) == 0x50 ||
	     (pdev->revision & 0xff) == 0x51))
		return false;

	/* CNF10K-A A0 */
	if ((pdev->subsystem_device == PCI_SUBSYS_DEVID_CNF10K_A_RNG) &&
	    (!pdev->revision || (pdev->revision & 0xff) == 0x60 ||
	     (pdev->revision & 0xff) == 0x61))
		return false;

	/* CNF10K-B A0/B0 */
	if ((pdev->subsystem_device == PCI_SUBSYS_DEVID_CNF10K_B_RNG) &&
	    (!pdev->revision || (pdev->revision & 0xff) == 0x70 ||
	     (pdev->revision & 0xff) == 0x74))
		return false;

	return true;
}

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

/* Returns true when valid data available otherwise return false */
static bool cn10k_read_trng(struct cn10k_rng *rng, u64 *value)
{
	u16 retry_count = 0;
	u64 upper, lower;
	u64 status;

	if (rng->extended_trng_regs) {
		do {
			*value = readq(rng->reg_base + RNM_PF_TRNG_DAT);
			if (*value)
				return true;
			status = readq(rng->reg_base + RNM_PF_TRNG_RES);
			if (!status && (retry_count++ > 0x1000))
				return false;
		} while (!status);
	}

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
	return true;
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
		if (!cn10k_read_trng(rng, &value))
			goto out;

		*((u64 *)pos) = value;
		size -= 8;
		pos += 8;
	}

	if (size > 0) {
		if (!cn10k_read_trng(rng, &value))
			goto out;

		while (size > 0) {
			*pos = (u8)value;
			value >>= 8;
			size--;
			pos++;
		}
	}

out:
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
	if (!rng->reg_base)
		return -ENOMEM;

	rng->ops.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "cn10k-rng-%s", dev_name(&pdev->dev));
	if (!rng->ops.name)
		return -ENOMEM;

	rng->ops.read = cn10k_rng_read;
	rng->ops.priv = (unsigned long)rng;

	rng->extended_trng_regs = cn10k_is_extended_trng_regs_supported(pdev);

	reset_rng_health_state(rng);

	err = devm_hwrng_register(&pdev->dev, &rng->ops);
	if (err)
		return dev_err_probe(&pdev->dev, err, "Could not register hwrng device.\n");

	return 0;
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
};

module_pci_driver(cn10k_rng_driver);
MODULE_AUTHOR("Sunil Goutham <sgoutham@marvell.com>");
MODULE_DESCRIPTION("Marvell CN10K HW RNG Driver");
MODULE_LICENSE("GPL v2");

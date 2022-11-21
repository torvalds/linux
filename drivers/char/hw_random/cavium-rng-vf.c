// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware Random Number Generator support.
 * Cavium Thunder, Marvell OcteonTx/Tx2 processor families.
 *
 * Copyright (C) 2016 Cavium, Inc.
 */

#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <asm/arch_timer.h>

/* PCI device IDs */
#define	PCI_DEVID_CAVIUM_RNG_PF		0xA018
#define	PCI_DEVID_CAVIUM_RNG_VF		0xA033

#define HEALTH_STATUS_REG		0x38

/* RST device info */
#define PCI_DEVICE_ID_RST_OTX2		0xA085
#define RST_BOOT_REG			0x1600ULL
#define CLOCK_BASE_RATE			50000000ULL
#define MSEC_TO_NSEC(x)			(x * 1000000)

struct cavium_rng {
	struct hwrng ops;
	void __iomem *result;
	void __iomem *pf_regbase;
	struct pci_dev *pdev;
	u64  clock_rate;
	u64  prev_error;
	u64  prev_time;
};

static inline bool is_octeontx(struct pci_dev *pdev)
{
	if (midr_is_cpu_model_range(read_cpuid_id(), MIDR_THUNDERX_83XX,
				    MIDR_CPU_VAR_REV(0, 0),
				    MIDR_CPU_VAR_REV(3, 0)) ||
	    midr_is_cpu_model_range(read_cpuid_id(), MIDR_THUNDERX_81XX,
				    MIDR_CPU_VAR_REV(0, 0),
				    MIDR_CPU_VAR_REV(3, 0)) ||
	    midr_is_cpu_model_range(read_cpuid_id(), MIDR_THUNDERX,
				    MIDR_CPU_VAR_REV(0, 0),
				    MIDR_CPU_VAR_REV(3, 0)))
		return true;

	return false;
}

static u64 rng_get_coprocessor_clkrate(void)
{
	u64 ret = CLOCK_BASE_RATE * 16; /* Assume 800Mhz as default */
	struct pci_dev *pdev;
	void __iomem *base;

	pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
			      PCI_DEVICE_ID_RST_OTX2, NULL);
	if (!pdev)
		goto error;

	base = pci_ioremap_bar(pdev, 0);
	if (!base)
		goto error_put_pdev;

	/* RST: PNR_MUL * 50Mhz gives clockrate */
	ret = CLOCK_BASE_RATE * ((readq(base + RST_BOOT_REG) >> 33) & 0x3F);

	iounmap(base);

error_put_pdev:
	pci_dev_put(pdev);

error:
	return ret;
}

static int check_rng_health(struct cavium_rng *rng)
{
	u64 cur_err, cur_time;
	u64 status, cycles;
	u64 time_elapsed;


	/* Skip checking health for OcteonTx */
	if (!rng->pf_regbase)
		return 0;

	status = readq(rng->pf_regbase + HEALTH_STATUS_REG);
	if (status & BIT_ULL(0)) {
		dev_err(&rng->pdev->dev, "HWRNG: Startup health test failed\n");
		return -EIO;
	}

	cycles = status >> 1;
	if (!cycles)
		return 0;

	cur_time = arch_timer_read_counter();

	/* RNM_HEALTH_STATUS[CYCLES_SINCE_HEALTH_FAILURE]
	 * Number of coprocessor cycles times 2 since the last failure.
	 * This field doesn't get cleared/updated until another failure.
	 */
	cycles = cycles / 2;
	cur_err = (cycles * 1000000000) / rng->clock_rate; /* In nanosec */

	/* Ignore errors that happenned a long time ago, these
	 * are most likely false positive errors.
	 */
	if (cur_err > MSEC_TO_NSEC(10)) {
		rng->prev_error = 0;
		rng->prev_time = 0;
		return 0;
	}

	if (rng->prev_error) {
		/* Calculate time elapsed since last error
		 * '1' tick of CNTVCT is 10ns, since it runs at 100Mhz.
		 */
		time_elapsed = (cur_time - rng->prev_time) * 10;
		time_elapsed += rng->prev_error;

		/* Check if current error is a new one or the old one itself.
		 * If error is a new one then consider there is a persistent
		 * issue with entropy, declare hardware failure.
		 */
		if (cur_err < time_elapsed) {
			dev_err(&rng->pdev->dev, "HWRNG failure detected\n");
			rng->prev_error = cur_err;
			rng->prev_time = cur_time;
			return -EIO;
		}
	}

	rng->prev_error = cur_err;
	rng->prev_time = cur_time;
	return 0;
}

/* Read data from the RNG unit */
static int cavium_rng_read(struct hwrng *rng, void *dat, size_t max, bool wait)
{
	struct cavium_rng *p = container_of(rng, struct cavium_rng, ops);
	unsigned int size = max;
	int err = 0;

	err = check_rng_health(p);
	if (err)
		return err;

	while (size >= 8) {
		*((u64 *)dat) = readq(p->result);
		size -= 8;
		dat += 8;
	}
	while (size > 0) {
		*((u8 *)dat) = readb(p->result);
		size--;
		dat++;
	}
	return max;
}

static int cavium_map_pf_regs(struct cavium_rng *rng)
{
	struct pci_dev *pdev;

	/* Health status is not supported on 83xx, skip mapping PF CSRs */
	if (is_octeontx(rng->pdev)) {
		rng->pf_regbase = NULL;
		return 0;
	}

	pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
			      PCI_DEVID_CAVIUM_RNG_PF, NULL);
	if (!pdev) {
		pr_err("Cannot find RNG PF device\n");
		return -EIO;
	}

	rng->pf_regbase = ioremap(pci_resource_start(pdev, 0),
				  pci_resource_len(pdev, 0));
	if (!rng->pf_regbase) {
		dev_err(&pdev->dev, "Failed to map PF CSR region\n");
		pci_dev_put(pdev);
		return -ENOMEM;
	}

	pci_dev_put(pdev);

	/* Get co-processor clock rate */
	rng->clock_rate = rng_get_coprocessor_clkrate();

	return 0;
}

/* Map Cavium RNG to an HWRNG object */
static int cavium_rng_probe_vf(struct	pci_dev		*pdev,
			 const struct	pci_device_id	*id)
{
	struct	cavium_rng *rng;
	int	ret;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->pdev = pdev;

	/* Map the RNG result */
	rng->result = pcim_iomap(pdev, 0, 0);
	if (!rng->result) {
		dev_err(&pdev->dev, "Error iomap failed retrieving result.\n");
		return -ENOMEM;
	}

	rng->ops.name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
				       "cavium-rng-%s", dev_name(&pdev->dev));
	if (!rng->ops.name)
		return -ENOMEM;

	rng->ops.read    = cavium_rng_read;
	rng->ops.quality = 1000;

	pci_set_drvdata(pdev, rng);

	/* Health status is available only at PF, hence map PF registers. */
	ret = cavium_map_pf_regs(rng);
	if (ret)
		return ret;

	ret = devm_hwrng_register(&pdev->dev, &rng->ops);
	if (ret) {
		dev_err(&pdev->dev, "Error registering device as HWRNG.\n");
		return ret;
	}

	return 0;
}

/* Remove the VF */
static void cavium_rng_remove_vf(struct pci_dev *pdev)
{
	struct cavium_rng *rng;

	rng = pci_get_drvdata(pdev);
	iounmap(rng->pf_regbase);
}

static const struct pci_device_id cavium_rng_vf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CAVIUM_RNG_VF) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, cavium_rng_vf_id_table);

static struct pci_driver cavium_rng_vf_driver = {
	.name		= "cavium_rng_vf",
	.id_table	= cavium_rng_vf_id_table,
	.probe		= cavium_rng_probe_vf,
	.remove		= cavium_rng_remove_vf,
};
module_pci_driver(cavium_rng_vf_driver);

MODULE_AUTHOR("Omer Khaliq <okhaliq@caviumnetworks.com>");
MODULE_LICENSE("GPL v2");

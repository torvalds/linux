// SPDX-License-Identifier: GPL-2.0
/* Marvell PTP driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "ptp.h"
#include "mbox.h"
#include "rvu.h"

#define DRV_NAME				"Marvell PTP Driver"

#define PCI_DEVID_OCTEONTX2_PTP			0xA00C
#define PCI_SUBSYS_DEVID_OCTX2_98xx_PTP		0xB100
#define PCI_SUBSYS_DEVID_OCTX2_96XX_PTP		0xB200
#define PCI_SUBSYS_DEVID_OCTX2_95XX_PTP		0xB300
#define PCI_SUBSYS_DEVID_OCTX2_95XXN_PTP	0xB400
#define PCI_SUBSYS_DEVID_OCTX2_95MM_PTP		0xB500
#define PCI_SUBSYS_DEVID_OCTX2_95XXO_PTP	0xB600
#define PCI_DEVID_OCTEONTX2_RST			0xA085
#define PCI_DEVID_CN10K_PTP			0xA09E

#define PCI_PTP_BAR_NO				0
#define PCI_RST_BAR_NO				0

#define PTP_CLOCK_CFG				0xF00ULL
#define PTP_CLOCK_CFG_PTP_EN			BIT_ULL(0)
#define PTP_CLOCK_LO				0xF08ULL
#define PTP_CLOCK_HI				0xF10ULL
#define PTP_CLOCK_COMP				0xF18ULL

#define RST_BOOT				0x1600ULL
#define RST_MUL_BITS				GENMASK_ULL(38, 33)
#define CLOCK_BASE_RATE				50000000ULL

static struct ptp *first_ptp_block;
static const struct pci_device_id ptp_id_table[];

static u64 get_clock_rate(void)
{
	u64 cfg, ret = CLOCK_BASE_RATE * 16;
	struct pci_dev *pdev;
	void __iomem *base;

	/* To get the input clock frequency with which PTP co-processor
	 * block is running the base frequency(50 MHz) needs to be multiplied
	 * with multiplier bits present in RST_BOOT register of RESET block.
	 * Hence below code gets the multiplier bits from the RESET PCI
	 * device present in the system.
	 */
	pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
			      PCI_DEVID_OCTEONTX2_RST, NULL);
	if (!pdev)
		goto error;

	base = pci_ioremap_bar(pdev, PCI_RST_BAR_NO);
	if (!base)
		goto error_put_pdev;

	cfg = readq(base + RST_BOOT);
	ret = CLOCK_BASE_RATE * FIELD_GET(RST_MUL_BITS, cfg);

	iounmap(base);

error_put_pdev:
	pci_dev_put(pdev);

error:
	return ret;
}

struct ptp *ptp_get(void)
{
	struct ptp *ptp = first_ptp_block;

	/* Check PTP block is present in hardware */
	if (!pci_dev_present(ptp_id_table))
		return ERR_PTR(-ENODEV);
	/* Check driver is bound to PTP block */
	if (!ptp)
		ptp = ERR_PTR(-EPROBE_DEFER);

	return ptp;
}

void ptp_put(struct ptp *ptp)
{
	if (!ptp)
		return;

	pci_dev_put(ptp->pdev);
}

static int ptp_adjfine(struct ptp *ptp, long scaled_ppm)
{
	bool neg_adj = false;
	u64 comp;
	u64 adj;
	s64 ppb;

	if (scaled_ppm < 0) {
		neg_adj = true;
		scaled_ppm = -scaled_ppm;
	}

	/* The hardware adds the clock compensation value to the PTP clock
	 * on every coprocessor clock cycle. Typical convention is that it
	 * represent number of nanosecond betwen each cycle. In this
	 * convention compensation value is in 64 bit fixed-point
	 * representation where upper 32 bits are number of nanoseconds
	 * and lower is fractions of nanosecond.
	 * The scaled_ppm represent the ratio in "parts per million" by which
	 * the compensation value should be corrected.
	 * To calculate new compenstation value we use 64bit fixed point
	 * arithmetic on following formula
	 * comp = tbase + tbase * scaled_ppm / (1M * 2^16)
	 * where tbase is the basic compensation value calculated
	 * initialy in the probe function.
	 */
	comp = ((u64)1000000000ull << 32) / ptp->clock_rate;
	/* convert scaled_ppm to ppb */
	ppb = 1 + scaled_ppm;
	ppb *= 125;
	ppb >>= 13;
	adj = comp * ppb;
	adj = div_u64(adj, 1000000000ull);
	comp = neg_adj ? comp - adj : comp + adj;

	writeq(comp, ptp->reg_base + PTP_CLOCK_COMP);

	return 0;
}

static int ptp_get_clock(struct ptp *ptp, u64 *clk)
{
	/* Return the current PTP clock */
	*clk = readq(ptp->reg_base + PTP_CLOCK_HI);

	return 0;
}

static int ptp_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct ptp *ptp;
	u64 clock_comp;
	u64 clock_cfg;
	int err;

	ptp = devm_kzalloc(dev, sizeof(*ptp), GFP_KERNEL);
	if (!ptp) {
		err = -ENOMEM;
		goto error;
	}

	ptp->pdev = pdev;

	err = pcim_enable_device(pdev);
	if (err)
		goto error_free;

	err = pcim_iomap_regions(pdev, 1 << PCI_PTP_BAR_NO, pci_name(pdev));
	if (err)
		goto error_free;

	ptp->reg_base = pcim_iomap_table(pdev)[PCI_PTP_BAR_NO];

	ptp->clock_rate = get_clock_rate();

	/* Enable PTP clock */
	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);
	clock_cfg |= PTP_CLOCK_CFG_PTP_EN;
	writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);

	clock_comp = ((u64)1000000000ull << 32) / ptp->clock_rate;
	/* Initial compensation value to start the nanosecs counter */
	writeq(clock_comp, ptp->reg_base + PTP_CLOCK_COMP);

	pci_set_drvdata(pdev, ptp);
	if (!first_ptp_block)
		first_ptp_block = ptp;

	return 0;

error_free:
	devm_kfree(dev, ptp);

error:
	/* For `ptp_get()` we need to differentiate between the case
	 * when the core has not tried to probe this device and the case when
	 * the probe failed.  In the later case we pretend that the
	 * initialization was successful and keep the error in
	 * `dev->driver_data`.
	 */
	pci_set_drvdata(pdev, ERR_PTR(err));
	if (!first_ptp_block)
		first_ptp_block = ERR_PTR(err);

	return 0;
}

static void ptp_remove(struct pci_dev *pdev)
{
	struct ptp *ptp = pci_get_drvdata(pdev);
	u64 clock_cfg;

	if (IS_ERR_OR_NULL(ptp))
		return;

	/* Disable PTP clock */
	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);
	clock_cfg &= ~PTP_CLOCK_CFG_PTP_EN;
	writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);
}

static const struct pci_device_id ptp_id_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_98xx_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_96XX_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XX_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XXN_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95MM_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XXO_PTP) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10K_PTP) },
	{ 0, }
};

struct pci_driver ptp_driver = {
	.name = DRV_NAME,
	.id_table = ptp_id_table,
	.probe = ptp_probe,
	.remove = ptp_remove,
};

int rvu_mbox_handler_ptp_op(struct rvu *rvu, struct ptp_req *req,
			    struct ptp_rsp *rsp)
{
	int err = 0;

	/* This function is the PTP mailbox handler invoked when
	 * called by AF consumers/netdev drivers via mailbox mechanism.
	 * It is used by netdev driver to get the PTP clock and to set
	 * frequency adjustments. Since mailbox can be called without
	 * notion of whether the driver is bound to ptp device below
	 * validation is needed as first step.
	 */
	if (!rvu->ptp)
		return -ENODEV;

	switch (req->op) {
	case PTP_OP_ADJFINE:
		err = ptp_adjfine(rvu->ptp, req->scaled_ppm);
		break;
	case PTP_OP_GET_CLOCK:
		err = ptp_get_clock(rvu->ptp, &rsp->clk);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

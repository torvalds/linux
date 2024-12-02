// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2021 NXP
 *
 * The Integrated Endpoint Register Block (IERB) is configured by pre-boot
 * software and is supposed to be to ENETC what a NVRAM is to a 'real' PCIe
 * card. Upon FLR, values from the IERB are transferred to the ENETC PFs, and
 * are read-only in the PF memory space.
 *
 * This driver fixes up the power-on reset values for the ENETC shared FIFO,
 * such that the TX and RX allocations are sufficient for jumbo frames, and
 * that intelligent FIFO dropping is enabled before the internal data
 * structures are corrupted.
 *
 * Even though not all ports might be used on a given board, we are not
 * concerned with partitioning the FIFO, because the default values configure
 * no strict reservations, so the entire FIFO can be used by the RX of a single
 * port, or the TX of a single port.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include "enetc.h"
#include "enetc_ierb.h"

/* IERB registers */
#define ENETC_IERB_TXMBAR(port)			(((port) * 0x100) + 0x8080)
#define ENETC_IERB_RXMBER(port)			(((port) * 0x100) + 0x8090)
#define ENETC_IERB_RXMBLR(port)			(((port) * 0x100) + 0x8094)
#define ENETC_IERB_RXBCR(port)			(((port) * 0x100) + 0x80a0)
#define ENETC_IERB_TXBCR(port)			(((port) * 0x100) + 0x80a8)
#define ENETC_IERB_FMBDTR			0xa000

#define ENETC_RESERVED_FOR_ICM			1024

struct enetc_ierb {
	void __iomem *regs;
};

static void enetc_ierb_write(struct enetc_ierb *ierb, u32 offset, u32 val)
{
	iowrite32(val, ierb->regs + offset);
}

int enetc_ierb_register_pf(struct platform_device *pdev,
			   struct pci_dev *pf_pdev)
{
	struct enetc_ierb *ierb = platform_get_drvdata(pdev);
	int port = enetc_pf_to_port(pf_pdev);
	u16 tx_credit, rx_credit, tx_alloc;

	if (port < 0)
		return -ENODEV;

	if (!ierb)
		return -EPROBE_DEFER;

	/* By default, it is recommended to set the Host Transfer Agent
	 * per port transmit byte credit to "1000 + max_frame_size/2".
	 * The power-on reset value (1800 bytes) is rounded up to the nearest
	 * 100 assuming a maximum frame size of 1536 bytes.
	 */
	tx_credit = roundup(1000 + ENETC_MAC_MAXFRM_SIZE / 2, 100);

	/* Internal memory allocated for transmit buffering is guaranteed but
	 * not reserved; i.e. if the total transmit allocation is not used,
	 * then the unused portion is not left idle, it can be used for receive
	 * buffering but it will be reclaimed, if required, from receive by
	 * intelligently dropping already stored receive frames in the internal
	 * memory to ensure that the transmit allocation is respected.
	 *
	 * PaTXMBAR must be set to a value larger than
	 *     PaTXBCR + 2 * max_frame_size + 32
	 * if frame preemption is not enabled, or to
	 *     2 * PaTXBCR + 2 * p_max_frame_size (pMAC maximum frame size) +
	 *     2 * np_max_frame_size (eMAC maximum frame size) + 64
	 * if frame preemption is enabled.
	 */
	tx_alloc = roundup(2 * tx_credit + 4 * ENETC_MAC_MAXFRM_SIZE + 64, 16);

	/* Initial credits, in units of 8 bytes, to the Ingress Congestion
	 * Manager for the maximum amount of bytes the port is allocated for
	 * pending traffic.
	 * It is recommended to set the initial credits to 2 times the maximum
	 * frame size (2 frames of maximum size).
	 */
	rx_credit = DIV_ROUND_UP(ENETC_MAC_MAXFRM_SIZE * 2, 8);

	enetc_ierb_write(ierb, ENETC_IERB_TXBCR(port), tx_credit);
	enetc_ierb_write(ierb, ENETC_IERB_TXMBAR(port), tx_alloc);
	enetc_ierb_write(ierb, ENETC_IERB_RXBCR(port), rx_credit);

	return 0;
}
EXPORT_SYMBOL(enetc_ierb_register_pf);

static int enetc_ierb_probe(struct platform_device *pdev)
{
	struct enetc_ierb *ierb;
	void __iomem *regs;

	ierb = devm_kzalloc(&pdev->dev, sizeof(*ierb), GFP_KERNEL);
	if (!ierb)
		return -ENOMEM;

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ierb->regs = regs;

	/* Free buffer depletion threshold in bytes.
	 * This sets the minimum amount of free buffer memory that should be
	 * maintained in the datapath sub system, and when the amount of free
	 * buffer memory falls below this threshold, a depletion indication is
	 * asserted, which may trigger "intelligent drop" frame releases from
	 * the ingress queues in the ICM.
	 * It is recommended to set the free buffer depletion threshold to 1024
	 * bytes, since the ICM needs some FIFO memory for its own use.
	 */
	enetc_ierb_write(ierb, ENETC_IERB_FMBDTR, ENETC_RESERVED_FOR_ICM);

	platform_set_drvdata(pdev, ierb);

	return 0;
}

static int enetc_ierb_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id enetc_ierb_match[] = {
	{ .compatible = "fsl,ls1028a-enetc-ierb", },
	{},
};
MODULE_DEVICE_TABLE(of, enetc_ierb_match);

static struct platform_driver enetc_ierb_driver = {
	.driver = {
		.name = "fsl-enetc-ierb",
		.of_match_table = enetc_ierb_match,
	},
	.probe = enetc_ierb_probe,
	.remove = enetc_ierb_remove,
};

module_platform_driver(enetc_ierb_driver);

MODULE_DESCRIPTION("NXP ENETC IERB");
MODULE_LICENSE("Dual BSD/GPL");

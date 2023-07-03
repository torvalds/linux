// SPDX-License-Identifier: GPL-2.0+
/*
 * aspeed-vhub -- Driver for Aspeed SoC "vHub" USB gadget
 *
 * core.c - Top level support
 *
 * Copyright 2017 IBM Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/prefetch.h>
#include <linux/clk.h>
#include <linux/usb/gadget.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/dma-mapping.h>

#include "vhub.h"

void ast_vhub_done(struct ast_vhub_ep *ep, struct ast_vhub_req *req,
		   int status)
{
	bool internal = req->internal;
	struct ast_vhub *vhub = ep->vhub;

	EPVDBG(ep, "completing request @%p, status %d\n", req, status);

	list_del_init(&req->queue);

	if ((req->req.status == -EINPROGRESS) ||  (status == -EOVERFLOW))
		req->req.status = status;

	if (req->req.dma) {
		if (!WARN_ON(!ep->dev))
			usb_gadget_unmap_request_by_dev(&vhub->pdev->dev,
						 &req->req, ep->epn.is_in);
		req->req.dma = 0;
	}

	/*
	 * If this isn't an internal EP0 request, call the core
	 * to call the gadget completion.
	 */
	if (!internal) {
		spin_unlock(&ep->vhub->lock);
		usb_gadget_giveback_request(&ep->ep, &req->req);
		spin_lock(&ep->vhub->lock);
	}
}

void ast_vhub_nuke(struct ast_vhub_ep *ep, int status)
{
	struct ast_vhub_req *req;
	int count = 0;

	/* Beware, lock will be dropped & req-acquired by done() */
	while (!list_empty(&ep->queue)) {
		req = list_first_entry(&ep->queue, struct ast_vhub_req, queue);
		ast_vhub_done(ep, req, status);
		count++;
	}
	if (count)
		EPDBG(ep, "Nuked %d request(s)\n", count);
}

struct usb_request *ast_vhub_alloc_request(struct usb_ep *u_ep,
					   gfp_t gfp_flags)
{
	struct ast_vhub_req *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;
	return &req->req;
}

void ast_vhub_free_request(struct usb_ep *u_ep, struct usb_request *u_req)
{
	struct ast_vhub_req *req = to_ast_req(u_req);

	kfree(req);
}

static irqreturn_t ast_vhub_irq(int irq, void *data)
{
	struct ast_vhub *vhub = data;
	irqreturn_t iret = IRQ_NONE;
	u32 i, istat;

	/* Stale interrupt while tearing down */
	if (!vhub->ep0_bufs)
		return IRQ_NONE;

	spin_lock(&vhub->lock);

	/* Read and ACK interrupts */
	istat = readl(vhub->regs + AST_VHUB_ISR);
	if (!istat)
		goto bail;
	writel(istat, vhub->regs + AST_VHUB_ISR);
	iret = IRQ_HANDLED;

	UDCVDBG(vhub, "irq status=%08x, ep_acks=%08x ep_nacks=%08x\n",
	       istat,
	       readl(vhub->regs + AST_VHUB_EP_ACK_ISR),
	       readl(vhub->regs + AST_VHUB_EP_NACK_ISR));

	/* Handle generic EPs first */
	if (istat & VHUB_IRQ_EP_POOL_ACK_STALL) {
		u32 ep_acks = readl(vhub->regs + AST_VHUB_EP_ACK_ISR);
		writel(ep_acks, vhub->regs + AST_VHUB_EP_ACK_ISR);

		for (i = 0; ep_acks && i < vhub->max_epns; i++) {
			u32 mask = VHUB_EP_IRQ(i);
			if (ep_acks & mask) {
				ast_vhub_epn_ack_irq(&vhub->epns[i]);
				ep_acks &= ~mask;
			}
		}
	}

	/* Handle device interrupts */
	if (istat & vhub->port_irq_mask) {
		for (i = 0; i < vhub->max_ports; i++) {
			if (istat & VHUB_DEV_IRQ(i))
				ast_vhub_dev_irq(&vhub->ports[i].dev);
		}
	}

	/* Handle top-level vHub EP0 interrupts */
	if (istat & (VHUB_IRQ_HUB_EP0_OUT_ACK_STALL |
		     VHUB_IRQ_HUB_EP0_IN_ACK_STALL |
		     VHUB_IRQ_HUB_EP0_SETUP)) {
		if (istat & VHUB_IRQ_HUB_EP0_IN_ACK_STALL)
			ast_vhub_ep0_handle_ack(&vhub->ep0, true);
		if (istat & VHUB_IRQ_HUB_EP0_OUT_ACK_STALL)
			ast_vhub_ep0_handle_ack(&vhub->ep0, false);
		if (istat & VHUB_IRQ_HUB_EP0_SETUP)
			ast_vhub_ep0_handle_setup(&vhub->ep0);
	}

	/* Various top level bus events */
	if (istat & (VHUB_IRQ_BUS_RESUME |
		     VHUB_IRQ_BUS_SUSPEND |
		     VHUB_IRQ_BUS_RESET)) {
		if (istat & VHUB_IRQ_BUS_RESUME)
			ast_vhub_hub_resume(vhub);
		if (istat & VHUB_IRQ_BUS_SUSPEND)
			ast_vhub_hub_suspend(vhub);
		if (istat & VHUB_IRQ_BUS_RESET)
			ast_vhub_hub_reset(vhub);
	}

 bail:
	spin_unlock(&vhub->lock);
	return iret;
}

void ast_vhub_init_hw(struct ast_vhub *vhub)
{
	u32 ctrl, port_mask, epn_mask;

	UDCDBG(vhub,"(Re)Starting HW ...\n");

	/* Enable PHY */
	ctrl = VHUB_CTRL_PHY_CLK |
		VHUB_CTRL_PHY_RESET_DIS;

       /*
	* We do *NOT* set the VHUB_CTRL_CLK_STOP_SUSPEND bit
	* to stop the logic clock during suspend because
	* it causes the registers to become inaccessible and
	* we haven't yet figured out a good wayt to bring the
	* controller back into life to issue a wakeup.
	*/

	/*
	 * Set some ISO & split control bits according to Aspeed
	 * recommendation
	 *
	 * VHUB_CTRL_ISO_RSP_CTRL: When set tells the HW to respond
	 * with 0 bytes data packet to ISO IN endpoints when no data
	 * is available.
	 *
	 * VHUB_CTRL_SPLIT_IN: This makes a SOF complete a split IN
	 * transaction.
	 */
	ctrl |= VHUB_CTRL_ISO_RSP_CTRL | VHUB_CTRL_SPLIT_IN;
	writel(ctrl, vhub->regs + AST_VHUB_CTRL);
	udelay(1);

	/* Set descriptor ring size */
	if (AST_VHUB_DESCS_COUNT == 256) {
		ctrl |= VHUB_CTRL_LONG_DESC;
		writel(ctrl, vhub->regs + AST_VHUB_CTRL);
	} else {
		BUILD_BUG_ON(AST_VHUB_DESCS_COUNT != 32);
	}

	/* Reset all devices */
	port_mask = GENMASK(vhub->max_ports, 1);
	writel(VHUB_SW_RESET_ROOT_HUB |
	       VHUB_SW_RESET_DMA_CONTROLLER |
	       VHUB_SW_RESET_EP_POOL |
	       port_mask, vhub->regs + AST_VHUB_SW_RESET);
	udelay(1);
	writel(0, vhub->regs + AST_VHUB_SW_RESET);

	/* Disable and cleanup EP ACK/NACK interrupts */
	epn_mask = GENMASK(vhub->max_epns - 1, 0);
	writel(0, vhub->regs + AST_VHUB_EP_ACK_IER);
	writel(0, vhub->regs + AST_VHUB_EP_NACK_IER);
	writel(epn_mask, vhub->regs + AST_VHUB_EP_ACK_ISR);
	writel(epn_mask, vhub->regs + AST_VHUB_EP_NACK_ISR);

	/* Default settings for EP0, enable HW hub EP1 */
	writel(0, vhub->regs + AST_VHUB_EP0_CTRL);
	writel(VHUB_EP1_CTRL_RESET_TOGGLE |
	       VHUB_EP1_CTRL_ENABLE,
	       vhub->regs + AST_VHUB_EP1_CTRL);
	writel(0, vhub->regs + AST_VHUB_EP1_STS_CHG);

	/* Configure EP0 DMA buffer */
	writel(vhub->ep0.buf_dma, vhub->regs + AST_VHUB_EP0_DATA);

	/* Clear address */
	writel(0, vhub->regs + AST_VHUB_CONF);

	/* Pullup hub (activate on host) */
	if (vhub->force_usb1)
		ctrl |= VHUB_CTRL_FULL_SPEED_ONLY;

	ctrl |= VHUB_CTRL_UPSTREAM_CONNECT;
	writel(ctrl, vhub->regs + AST_VHUB_CTRL);

	/* Enable some interrupts */
	writel(VHUB_IRQ_HUB_EP0_IN_ACK_STALL |
	       VHUB_IRQ_HUB_EP0_OUT_ACK_STALL |
	       VHUB_IRQ_HUB_EP0_SETUP |
	       VHUB_IRQ_EP_POOL_ACK_STALL |
	       VHUB_IRQ_BUS_RESUME |
	       VHUB_IRQ_BUS_SUSPEND |
	       VHUB_IRQ_BUS_RESET,
	       vhub->regs + AST_VHUB_IER);
}

static void ast_vhub_remove(struct platform_device *pdev)
{
	struct ast_vhub *vhub = platform_get_drvdata(pdev);
	unsigned long flags;
	int i;

	if (!vhub || !vhub->regs)
		return;

	/* Remove devices */
	for (i = 0; i < vhub->max_ports; i++)
		ast_vhub_del_dev(&vhub->ports[i].dev);

	spin_lock_irqsave(&vhub->lock, flags);

	/* Mask & ack all interrupts  */
	writel(0, vhub->regs + AST_VHUB_IER);
	writel(VHUB_IRQ_ACK_ALL, vhub->regs + AST_VHUB_ISR);

	/* Pull device, leave PHY enabled */
	writel(VHUB_CTRL_PHY_CLK |
	       VHUB_CTRL_PHY_RESET_DIS,
	       vhub->regs + AST_VHUB_CTRL);

	if (vhub->clk)
		clk_disable_unprepare(vhub->clk);

	spin_unlock_irqrestore(&vhub->lock, flags);

	if (vhub->ep0_bufs)
		dma_free_coherent(&pdev->dev,
				  AST_VHUB_EP0_MAX_PACKET *
				  (vhub->max_ports + 1),
				  vhub->ep0_bufs,
				  vhub->ep0_bufs_dma);
	vhub->ep0_bufs = NULL;
}

static int ast_vhub_probe(struct platform_device *pdev)
{
	enum usb_device_speed max_speed;
	struct ast_vhub *vhub;
	struct resource *res;
	int i, rc = 0;
	const struct device_node *np = pdev->dev.of_node;

	vhub = devm_kzalloc(&pdev->dev, sizeof(*vhub), GFP_KERNEL);
	if (!vhub)
		return -ENOMEM;

	rc = of_property_read_u32(np, "aspeed,vhub-downstream-ports",
				  &vhub->max_ports);
	if (rc < 0)
		vhub->max_ports = AST_VHUB_NUM_PORTS;

	vhub->ports = devm_kcalloc(&pdev->dev, vhub->max_ports,
				   sizeof(*vhub->ports), GFP_KERNEL);
	if (!vhub->ports)
		return -ENOMEM;

	rc = of_property_read_u32(np, "aspeed,vhub-generic-endpoints",
				  &vhub->max_epns);
	if (rc < 0)
		vhub->max_epns = AST_VHUB_NUM_GEN_EPs;

	vhub->epns = devm_kcalloc(&pdev->dev, vhub->max_epns,
				  sizeof(*vhub->epns), GFP_KERNEL);
	if (!vhub->epns)
		return -ENOMEM;

	spin_lock_init(&vhub->lock);
	vhub->pdev = pdev;
	vhub->port_irq_mask = GENMASK(VHUB_IRQ_DEV1_BIT + vhub->max_ports - 1,
				      VHUB_IRQ_DEV1_BIT);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vhub->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vhub->regs)) {
		dev_err(&pdev->dev, "Failed to map resources\n");
		return PTR_ERR(vhub->regs);
	}
	UDCDBG(vhub, "vHub@%pR mapped @%p\n", res, vhub->regs);

	platform_set_drvdata(pdev, vhub);

	vhub->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(vhub->clk)) {
		rc = PTR_ERR(vhub->clk);
		goto err;
	}
	rc = clk_prepare_enable(vhub->clk);
	if (rc) {
		dev_err(&pdev->dev, "Error couldn't enable clock (%d)\n", rc);
		goto err;
	}

	/* Check if we need to limit the HW to USB1 */
	max_speed = usb_get_maximum_speed(&pdev->dev);
	if (max_speed != USB_SPEED_UNKNOWN && max_speed < USB_SPEED_HIGH)
		vhub->force_usb1 = true;

	/* Mask & ack all interrupts before installing the handler */
	writel(0, vhub->regs + AST_VHUB_IER);
	writel(VHUB_IRQ_ACK_ALL, vhub->regs + AST_VHUB_ISR);

	/* Find interrupt and install handler */
	vhub->irq = platform_get_irq(pdev, 0);
	if (vhub->irq < 0) {
		rc = vhub->irq;
		goto err;
	}
	rc = devm_request_irq(&pdev->dev, vhub->irq, ast_vhub_irq, 0,
			      KBUILD_MODNAME, vhub);
	if (rc) {
		dev_err(&pdev->dev, "Failed to request interrupt\n");
		goto err;
	}

	/*
	 * Allocate DMA buffers for all EP0s in one chunk,
	 * one per port and one for the vHub itself
	 */
	vhub->ep0_bufs = dma_alloc_coherent(&pdev->dev,
					    AST_VHUB_EP0_MAX_PACKET *
					    (vhub->max_ports + 1),
					    &vhub->ep0_bufs_dma, GFP_KERNEL);
	if (!vhub->ep0_bufs) {
		dev_err(&pdev->dev, "Failed to allocate EP0 DMA buffers\n");
		rc = -ENOMEM;
		goto err;
	}
	UDCVDBG(vhub, "EP0 DMA buffers @%p (DMA 0x%08x)\n",
		vhub->ep0_bufs, (u32)vhub->ep0_bufs_dma);

	/* Init vHub EP0 */
	ast_vhub_init_ep0(vhub, &vhub->ep0, NULL);

	/* Init devices */
	for (i = 0; i < vhub->max_ports && rc == 0; i++)
		rc = ast_vhub_init_dev(vhub, i);
	if (rc)
		goto err;

	/* Init hub emulation */
	rc = ast_vhub_init_hub(vhub);
	if (rc)
		goto err;

	/* Initialize HW */
	ast_vhub_init_hw(vhub);

	dev_info(&pdev->dev, "Initialized virtual hub in USB%d mode\n",
		 vhub->force_usb1 ? 1 : 2);

	return 0;
 err:
	ast_vhub_remove(pdev);
	return rc;
}

static const struct of_device_id ast_vhub_dt_ids[] = {
	{
		.compatible = "aspeed,ast2400-usb-vhub",
	},
	{
		.compatible = "aspeed,ast2500-usb-vhub",
	},
	{
		.compatible = "aspeed,ast2600-usb-vhub",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ast_vhub_dt_ids);

static struct platform_driver ast_vhub_driver = {
	.probe		= ast_vhub_probe,
	.remove_new	= ast_vhub_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= ast_vhub_dt_ids,
	},
};
module_platform_driver(ast_vhub_driver);

MODULE_DESCRIPTION("Aspeed vHub udc driver");
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_LICENSE("GPL");

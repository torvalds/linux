/*
 * Driver for Cadence QSPI Controller
 *
 * Copyright (C) 2012 Altera Corporation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include "spi-cadence-qspi.h"
#include "spi-cadence-qspi-apb.h"

#define CADENCE_QSPI_NAME			"cadence-qspi"

unsigned int cadence_qspi_init_timeout(const unsigned long timeout_in_ms)
{
	return (jiffies + msecs_to_jiffies(timeout_in_ms));
}

unsigned int cadence_qspi_check_timeout(const unsigned long timeout)
{
	return (time_before(jiffies, timeout));
}

static irqreturn_t cadence_qspi_irq_handler(int this_irq, void *dev)
{
	struct struct_cqspi *cadence_qspi = dev;

	/* Read interrupt status */
	cadence_qspi->irq_status = CQSPI_READ_IRQ_STATUS(cadence_qspi->iobase);

	/* Clear interrupt */
	CQSPI_CLEAR_IRQ(cadence_qspi->iobase, cadence_qspi->irq_status);

	wake_up(&cadence_qspi->waitqueue);

	return IRQ_HANDLED;
}

static void cadence_qspi_work(struct work_struct *work)
{
	struct struct_cqspi *cadence_qspi
		= container_of(work, struct struct_cqspi, work);
	unsigned long flags;

	pr_debug("%s\n", __func__);

	spin_lock_irqsave(&cadence_qspi->lock, flags);
	while ((!list_empty(&cadence_qspi->msg_queue)) &&
		cadence_qspi->running) {
		struct spi_message *spi_msg;
		struct spi_device *spi;
		struct spi_transfer *spi_xfer;
		struct spi_transfer *xfer[CQSPI_MAX_TRANS];
		int status = 0;
		int n_trans = 0;
		int next_in_queue = 0;

		spi_msg = container_of(cadence_qspi->msg_queue.next,
			struct spi_message, queue);
		list_del_init(&spi_msg->queue);
		spin_unlock_irqrestore(&cadence_qspi->lock, flags);
		spi = spi_msg->spi;
		list_for_each_entry(spi_xfer, &spi_msg->transfers,
				transfer_list) {
			if (n_trans >= CQSPI_MAX_TRANS) {
				dev_err(&spi->dev,"ERROR: Number of SPI "
					"transfer is more than %d.\n",
					CQSPI_MAX_TRANS);
				/* Skip process the queue if number of
				 * transaction is greater than max 2. */
				next_in_queue = 1;
				break;
			}
			xfer[n_trans++] = spi_xfer;
		}

		/* Continue to next queue if next_in_queue is set. */
		if (next_in_queue)
			continue;

		status = cadence_qspi_apb_process_queue(cadence_qspi, spi,
					n_trans, xfer);

		if (!status) {
			spi_msg->actual_length += xfer[0]->len;
			if (n_trans > 1)
				spi_msg->actual_length += xfer[1]->len;
		}

		spi_msg->status = status;
		spi_msg->complete(spi_msg->context);
		spin_lock_irqsave(&cadence_qspi->lock, flags);
	}
	spin_unlock_irqrestore(&cadence_qspi->lock, flags);
}

static int cadence_qspi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct struct_cqspi *cadence_qspi =
		spi_master_get_devdata(spi->master);
	struct spi_transfer *spi_xfer;
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	unsigned long flags;

	pr_debug("%s\n", __func__);

	list_for_each_entry(spi_xfer, &msg->transfers, transfer_list) {
		if (spi_xfer->speed_hz > (pdata->master_ref_clk_hz / 2)) {
			dev_err(&spi->dev, "speed_hz%d greater than "
				"maximum %dHz\n",
				spi_xfer->speed_hz,
				(pdata->master_ref_clk_hz / 2));
			msg->status = -EINVAL;
			return -EINVAL;
		}
	}

	spin_lock_irqsave(&cadence_qspi->lock, flags);

	if (!cadence_qspi->running) {
		spin_unlock_irqrestore(&cadence_qspi->lock, flags);
		return -ESHUTDOWN;
	}

	msg->status = -EINPROGRESS;
	msg->actual_length = 0;

	list_add_tail(&msg->queue, &cadence_qspi->msg_queue);
	queue_work(cadence_qspi->workqueue, &cadence_qspi->work);
	spin_unlock_irqrestore(&cadence_qspi->lock, flags);
	return 0;
}

static int cadence_qspi_setup(struct spi_device *spi)
{
	pr_debug("%s\n", __func__);

	if (spi->chip_select > spi->master->num_chipselect) {
		dev_err(&spi->dev, "%d chip select is out of range\n",
			spi->chip_select);
		return -EINVAL;
	}
	pr_debug("cadence_qspi : bits per word %d, chip select %d, "
		"speed %d KHz\n", spi->bits_per_word, spi->chip_select,
		spi->max_speed_hz);
	return 0;
}

static int cadence_qspi_start_queue(struct struct_cqspi *cadence_qspi)
{
	unsigned long flags;

	pr_debug("%s\n", __func__);
	spin_lock_irqsave(&cadence_qspi->lock, flags);

	if (cadence_qspi->running) {
		spin_unlock_irqrestore(&cadence_qspi->lock, flags);
		return -EBUSY;
	}

	if (!cadence_qspi_apb_is_controller_ready (cadence_qspi->iobase) ) {
		spin_unlock_irqrestore(&cadence_qspi->lock, flags);
		return -EBUSY;
	}

	cadence_qspi->running = true;

	spin_unlock_irqrestore(&cadence_qspi->lock, flags);

	queue_work(cadence_qspi->workqueue, &cadence_qspi->work);
	return 0;
}

static int cadence_qspi_stop_queue(struct struct_cqspi *cadence_qspi)
{
	unsigned long flags;
	unsigned limit = 500;
	int status = 0;

	spin_lock_irqsave(&cadence_qspi->lock, flags);
	cadence_qspi->running = false;
	/* We will wait until controller process all the queue and ensure the
	 * controller is not busy. */
	while ((!list_empty(&cadence_qspi->msg_queue) ||
		!cadence_qspi_apb_is_controller_ready(cadence_qspi->iobase))
		&& limit--) {
		spin_unlock_irqrestore(&cadence_qspi->lock, flags);
		msleep(10);
		spin_lock_irqsave(&cadence_qspi->lock, flags);
	}

	if (!list_empty(&cadence_qspi->msg_queue) ||
		!cadence_qspi_apb_is_controller_ready(cadence_qspi->iobase))
		status = -EBUSY;

	spin_unlock_irqrestore(&cadence_qspi->lock, flags);
	return status;
}

static int cadence_qspi_of_get_pdata(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *nc;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	struct cqspi_flash_pdata *f_pdata;
	unsigned int cs;
	unsigned int prop;

	if (of_property_read_u32(np, "bus-num", &prop)) {
		dev_err(&pdev->dev, "couldn't determine bus-num\n");
		return -ENXIO;
	}
	pdata->bus_num = prop;

	if (of_property_read_u32(np, "num-chipselect", &prop)) {
		dev_err(&pdev->dev, "couldn't determine num-chipselect\n");
		return -ENXIO;
	}
	pdata->num_chipselect = prop;

	if (of_property_read_u32(np, "master-ref-clk", &prop)) {
		dev_err(&pdev->dev, "couldn't determine master-ref-clk\n");
		return -ENXIO;
	}
	pdata->master_ref_clk_hz = prop;

	if (of_property_read_u32(np, "ext-decoder", &prop)) {
		dev_err(&pdev->dev, "couldn't determine ext-decoder\n");
		return -ENXIO;
	}
	pdata->ext_decoder = prop;

	if (of_property_read_u32(np, "fifo-depth", &prop)) {
		dev_err(&pdev->dev, "couldn't determine fifo-depth\n");
		return -ENXIO;
	}
	pdata->fifo_depth = prop;

	pdata->enable_dma = of_property_read_bool(np, "enable-dma");
	dev_info(&pdev->dev, "DMA %senabled\n",
		pdata->enable_dma ? "" : "NOT ");

	if (pdata->enable_dma) {
		if (of_property_read_u32(np, "tx-dma-peri-id", &prop)) {
			dev_err(&pdev->dev, "couldn't determine tx-dma-peri-id\n");
			return -ENXIO;
		}
		pdata->tx_dma_peri_id = prop;

		if (of_property_read_u32(np, "rx-dma-peri-id", &prop)) {
			dev_err(&pdev->dev, "couldn't determine rx-dma-peri-id\n");
			return -ENXIO;
		}
		pdata->rx_dma_peri_id = prop;
	}

	/* Get flash devices platform data */
	for_each_child_of_node(np, nc) {
		if (of_property_read_u32(nc, "reg", &cs)) {
			dev_err(&pdev->dev, "couldn't determine reg\n");
			return -ENXIO;
		}

		f_pdata = &(pdata->f_pdata[cs]);

		if (of_property_read_u32(nc, "page-size", &prop)) {
			dev_err(&pdev->dev, "couldn't determine page-size\n");
			return -ENXIO;
		}
		f_pdata->page_size = prop;

		if (of_property_read_u32(nc, "block-size", &prop)) {
			dev_err(&pdev->dev, "couldn't determine block-size\n");
			return -ENXIO;
		}
		f_pdata->block_size = prop;

		if (of_property_read_u32(nc, "read-delay", &prop)) {
			dev_err(&pdev->dev, "couldn't determine read-delay\n");
			return -ENXIO;
		}
		f_pdata->read_delay = prop;

		if (of_property_read_u32(nc, "tshsl-ns", &prop)) {
			dev_err(&pdev->dev, "couldn't determine tshsl-ns\n");
			return -ENXIO;
		}
		f_pdata->tshsl_ns = prop;

		if (of_property_read_u32(nc, "tsd2d-ns", &prop)) {
			dev_err(&pdev->dev, "couldn't determine tsd2d-ns\n");
			return -ENXIO;
		}
		f_pdata->tsd2d_ns = prop;

		if (of_property_read_u32(nc, "tchsh-ns", &prop)) {
			dev_err(&pdev->dev, "couldn't determine tchsh-ns\n");
			return -ENXIO;
		}
		f_pdata->tchsh_ns = prop;

		if (of_property_read_u32(nc, "tslch-ns", &prop)) {
			dev_err(&pdev->dev, "couldn't determine tslch-ns\n");
			return -ENXIO;
		}
		f_pdata->tslch_ns = prop;
	}
	return 0;
}

static void cadence_qspi_dma_shutdown(struct struct_cqspi *cadence_qspi)
{
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	if (cadence_qspi->txchan)
		dma_release_channel(cadence_qspi->txchan);
	if (cadence_qspi->rxchan)
		dma_release_channel(cadence_qspi->rxchan);
	pdata->enable_dma = 0;
	cadence_qspi->rxchan = cadence_qspi->txchan = NULL;
}

static bool dma_channel_filter(struct dma_chan *chan, void *param)
{
	return (chan->chan_id == (unsigned int)param);
}

static void cadence_qspi_dma_init(struct struct_cqspi *cadence_qspi)
{
	struct platform_device *pdev = cadence_qspi->pdev;
	struct cqspi_platform_data *pdata = pdev->dev.platform_data;
	dma_cap_mask_t mask;
	unsigned int channel_num;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	channel_num = pdata->tx_dma_peri_id;
	cadence_qspi->txchan = dma_request_channel(mask,
		dma_channel_filter, (void *)channel_num);
	if (cadence_qspi->txchan)
		dev_dbg(&pdev->dev, "TX channel %s %d selected\n",
			dma_chan_name(cadence_qspi->txchan),
			cadence_qspi->txchan->chan_id);
	else
		dev_err(&pdev->dev, "could not get dma channel %d\n",
			channel_num);

	channel_num = pdata->rx_dma_peri_id;
	cadence_qspi->rxchan = dma_request_channel(mask,
		dma_channel_filter, (void *)channel_num);
	if (cadence_qspi->rxchan)
		dev_dbg(&pdev->dev, "RX channel %s %d selected\n",
			dma_chan_name(cadence_qspi->rxchan),
			cadence_qspi->rxchan->chan_id);
	else
		dev_err(&pdev->dev, "could not get dma channel %d\n",
			channel_num);

	if (!cadence_qspi->rxchan  || !cadence_qspi->txchan) {
		/* Error, fall back to non-dma mode */
		cadence_qspi_dma_shutdown(cadence_qspi);
		dev_info(&pdev->dev, "falling back to non-DMA operation\n");
	}
}

static int cadence_qspi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct struct_cqspi *cadence_qspi;
	struct resource *res;
	struct resource *res_ahb;
	struct cqspi_platform_data *pdata;
	int status;

	pr_debug("%s\n", __func__);
	pr_debug("%s %s %s\n", __func__,
		pdev->name, pdev->id_entry->name);

	master = spi_alloc_master(&pdev->dev, sizeof(*cadence_qspi));
	if (master == NULL) {
		dev_err(&pdev->dev, "spi_alloc_master failed\n");
		return -ENOMEM;
	}

	master->mode_bits = SPI_CS_HIGH | SPI_CPOL | SPI_CPHA;
	master->setup = cadence_qspi_setup;
	master->transfer = cadence_qspi_transfer;
	master->dev.of_node = pdev->dev.of_node;

	cadence_qspi = spi_master_get_devdata(master);
	cadence_qspi->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "platform_get_resource failed\n");
		status = -ENXIO;
		goto err_iomem;
	}

	cadence_qspi->res = res;

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		status = -EBUSY;
		goto err_iomem;
	}

	cadence_qspi->iobase = ioremap(res->start, resource_size(res));
	if (!cadence_qspi->iobase) {
		dev_err(&pdev->dev, "ioremap failed\n");
		status = -ENOMEM;
		goto err_ioremap;
	}

	res_ahb = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res_ahb) {
		dev_err(&pdev->dev, "platform_get_resource failed\n");
		status = -ENXIO;
		goto err_ahbmem;
	}
	cadence_qspi->res_ahb = res_ahb;

	if (!request_mem_region(res_ahb->start, resource_size(res_ahb),
		pdev->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		status = -EBUSY;
		goto err_ahbmem;
	}

	cadence_qspi->qspi_ahb_virt = ioremap(res_ahb->start,
		resource_size(res_ahb));
	if (!cadence_qspi->qspi_ahb_virt) {
		dev_err(&pdev->dev, "ioremap res_ahb failed\n");
		status = -ENOMEM;
		goto err_ahbremap;
	}

	cadence_qspi->workqueue =
		create_singlethread_workqueue(dev_name(master->dev.parent));
	if (!cadence_qspi->workqueue) {
		dev_err(&pdev->dev, "create_workqueue failed\n");
		status = -ENOMEM;
		goto err_wq;
	}

	cadence_qspi->running = false;
	INIT_WORK(&cadence_qspi->work, cadence_qspi_work);
	spin_lock_init(&cadence_qspi->lock);
	INIT_LIST_HEAD(&cadence_qspi->msg_queue);
	init_waitqueue_head(&cadence_qspi->waitqueue);
	status = cadence_qspi_start_queue(cadence_qspi);
	if (status) {
		dev_err(&pdev->dev, "problem starting queue.\n");
		goto err_start_q;
	}

	cadence_qspi->irq = platform_get_irq(pdev, 0);

	if (cadence_qspi->irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		status = -ENXIO;
		goto err_irq;
	}

	status = request_irq(cadence_qspi->irq, cadence_qspi_irq_handler,
		0, pdev->name, cadence_qspi);
	if (status) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_irq;
	}

	pdata = kmalloc(sizeof(struct cqspi_platform_data), GFP_KERNEL);
	if (!pdata) {
		status = -ENOMEM;
		goto err_pdata;
	}

	pdev->dev.platform_data = pdata;
	pdata->qspi_ahb_phy = res_ahb->start;

	status = cadence_qspi_of_get_pdata(pdev);
	if (status) {
		dev_err(&pdev->dev, "Get platform data failed.\n");
		goto err_of;
	}

	master->bus_num = pdata->bus_num;
	master->num_chipselect = pdata->num_chipselect;

	platform_set_drvdata(pdev, master);
	cadence_qspi_apb_controller_init(cadence_qspi);
	cadence_qspi->current_cs = -1;
	pr_debug("%s call spi_register_master\n", __func__);
	status = spi_register_master(master);
	if (status) {
		dev_err(&pdev->dev, "spi_register_master failed\n");
		goto err_of;
	}

	if (pdata->enable_dma)
		cadence_qspi_dma_init(cadence_qspi);

	dev_info(&pdev->dev, "Cadence QSPI controller driver\n");
	return 0;

err_of:
	kfree(pdata);
err_pdata:
	free_irq(cadence_qspi->irq, cadence_qspi);
err_start_q:
err_irq:
	destroy_workqueue(cadence_qspi->workqueue);
err_wq:
	iounmap(cadence_qspi->qspi_ahb_virt);
err_ahbremap:
	release_mem_region(res_ahb->start, resource_size(res_ahb));
err_ahbmem:
	iounmap(cadence_qspi->iobase);
err_ioremap:
	release_mem_region(res->start, resource_size(res));
err_iomem:
	spi_master_put(master);
	dev_err(&pdev->dev, "Cadence QSPI controller probe failed\n");
	return status;
}

static int cadence_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct struct_cqspi *cadence_qspi = spi_master_get_devdata(master);

	cadence_qspi_dma_shutdown(cadence_qspi);

	cadence_qspi_apb_controller_disable(cadence_qspi->iobase);

	platform_set_drvdata(pdev, NULL);
	destroy_workqueue(cadence_qspi->workqueue);
	free_irq(cadence_qspi->irq, cadence_qspi);
	iounmap(cadence_qspi->iobase);
	iounmap(cadence_qspi->qspi_ahb_virt);
	release_mem_region(cadence_qspi->res->start,
		resource_size(cadence_qspi->res));
	release_mem_region(cadence_qspi->res_ahb->start,
		resource_size(cadence_qspi->res_ahb));
	kfree(pdev->dev.platform_data);
	spi_unregister_master(master);
	spi_master_put(master);

	return 0;
}

#ifdef CONFIG_PM

static int cadence_qspi_suspend(struct device *dev)
{
	struct spi_master	*master = dev_get_drvdata(dev);
	struct struct_cqspi *cadence_qspi = spi_master_get_devdata(master);
	int status=0;

	/* Stop the queue */
	status = cadence_qspi_stop_queue(cadence_qspi);
	if (status != 0)
		return status;
	/* Disable the controller to conserve the power */
	cadence_qspi_apb_controller_disable(cadence_qspi->iobase);
	return 0;
}

static int cadence_qspi_resume(struct device *dev)
{
	struct spi_master	*master = dev_get_drvdata(dev);
	struct struct_cqspi *cadence_qspi = spi_master_get_devdata(master);
	int status = 0;

	cadence_qspi_apb_controller_enable(cadence_qspi->iobase);
	/* Start the queue running */
	status = cadence_qspi_start_queue(cadence_qspi);
	if (status != 0) {
		cadence_qspi_apb_controller_disable(cadence_qspi->iobase);
		dev_err(dev, "problem starting queue (%d)\n", status);
		return status;
	}
	return 0;
}
static struct dev_pm_ops cadence_qspi__dev_pm_ops =
{
	.suspend	= cadence_qspi_suspend,
	.resume		= cadence_qspi_resume,
};
#define	CADENCE_QSPI_DEV_PM_OPS	(&cadence_qspi__dev_pm_ops)
#else
#define	CADENCE_QSPI_DEV_PM_OPS	NULL
#endif

#ifdef CONFIG_OF
static struct of_device_id cadence_qspi_of_match[] = {
	{ .compatible = "cadence,qspi",},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, cadence_qspi_of_match);
#else
#define cadence_qspi_of_match NULL
#endif /* CONFIG_OF */


static struct platform_driver cadence_qspi_platform_driver =
{
	.probe		= cadence_qspi_probe,
	.remove		= cadence_qspi_remove,
	.driver = {
		.name	= CADENCE_QSPI_NAME,
		.owner	= THIS_MODULE,
		.pm	= CADENCE_QSPI_DEV_PM_OPS,
		.of_match_table = cadence_qspi_of_match,
	},
};

static int __init cadence_qspi_init(void)
{
	return platform_driver_register(&cadence_qspi_platform_driver);
}
static void __exit cadence_qspi_exit(void)
{
	platform_driver_unregister(&cadence_qspi_platform_driver);
}

module_init(cadence_qspi_init);
module_exit(cadence_qspi_exit);

MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_DESCRIPTION("Cadence QSPI Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" CADENCE_QSPI_NAME);

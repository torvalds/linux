// SPDX-License-Identifier: GPL-2.0
//
// RPC-IF SPI/QSPI/Octa driver
//
// Copyright (C) 2018 ~ 2019 Renesas Solutions Corp.
// Copyright (C) 2019 Macronix International Co., Ltd.
// Copyright (C) 2019 - 2020 Cogent Embedded, Inc.
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#include <memory/renesas-rpc-if.h>

#include <asm/unaligned.h>

static void rpcif_spi_mem_prepare(struct spi_device *spi_dev,
				  const struct spi_mem_op *spi_op,
				  u64 *offs, size_t *len)
{
	struct rpcif *rpc = spi_controller_get_devdata(spi_dev->controller);
	struct rpcif_op rpc_op = { };

	rpc_op.cmd.opcode = spi_op->cmd.opcode;
	rpc_op.cmd.buswidth = spi_op->cmd.buswidth;

	if (spi_op->addr.nbytes) {
		rpc_op.addr.buswidth = spi_op->addr.buswidth;
		rpc_op.addr.nbytes = spi_op->addr.nbytes;
		rpc_op.addr.val = spi_op->addr.val;
	}

	if (spi_op->dummy.nbytes) {
		rpc_op.dummy.buswidth = spi_op->dummy.buswidth;
		rpc_op.dummy.ncycles  = spi_op->dummy.nbytes * 8 /
					spi_op->dummy.buswidth;
	}

	if (spi_op->data.nbytes || (offs && len)) {
		rpc_op.data.buswidth = spi_op->data.buswidth;
		rpc_op.data.nbytes = spi_op->data.nbytes;
		switch (spi_op->data.dir) {
		case SPI_MEM_DATA_IN:
			rpc_op.data.dir = RPCIF_DATA_IN;
			rpc_op.data.buf.in = spi_op->data.buf.in;
			break;
		case SPI_MEM_DATA_OUT:
			rpc_op.data.dir = RPCIF_DATA_OUT;
			rpc_op.data.buf.out = spi_op->data.buf.out;
			break;
		case SPI_MEM_NO_DATA:
			rpc_op.data.dir = RPCIF_NO_DATA;
			break;
		}
	} else	{
		rpc_op.data.dir = RPCIF_NO_DATA;
	}

	rpcif_prepare(rpc, &rpc_op, offs, len);
}

static bool rpcif_spi_mem_supports_op(struct spi_mem *mem,
				      const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->data.buswidth > 4 || op->addr.buswidth > 4 ||
	    op->dummy.buswidth > 4 || op->cmd.buswidth > 4 ||
	    op->addr.nbytes > 4)
		return false;

	return true;
}

static ssize_t rpcif_spi_mem_dirmap_read(struct spi_mem_dirmap_desc *desc,
					 u64 offs, size_t len, void *buf)
{
	struct rpcif *rpc =
		spi_controller_get_devdata(desc->mem->spi->controller);

	if (offs + desc->info.offset + len > U32_MAX)
		return -EINVAL;

	rpcif_spi_mem_prepare(desc->mem->spi, &desc->info.op_tmpl, &offs, &len);

	return rpcif_dirmap_read(rpc, offs, len, buf);
}

static int rpcif_spi_mem_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct rpcif *rpc =
		spi_controller_get_devdata(desc->mem->spi->controller);

	if (desc->info.offset + desc->info.length > U32_MAX)
		return -ENOTSUPP;

	if (!rpcif_spi_mem_supports_op(desc->mem, &desc->info.op_tmpl))
		return -ENOTSUPP;

	if (!rpc->dirmap && desc->info.op_tmpl.data.dir == SPI_MEM_DATA_IN)
		return -ENOTSUPP;

	if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_OUT)
		return -ENOTSUPP;

	return 0;
}

static int rpcif_spi_mem_exec_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct rpcif *rpc =
		spi_controller_get_devdata(mem->spi->controller);

	rpcif_spi_mem_prepare(mem->spi, op, NULL, NULL);

	return rpcif_manual_xfer(rpc);
}

static const struct spi_controller_mem_ops rpcif_spi_mem_ops = {
	.supports_op	= rpcif_spi_mem_supports_op,
	.exec_op	= rpcif_spi_mem_exec_op,
	.dirmap_create	= rpcif_spi_mem_dirmap_create,
	.dirmap_read	= rpcif_spi_mem_dirmap_read,
};

static int rpcif_spi_probe(struct platform_device *pdev)
{
	struct device *parent = pdev->dev.parent;
	struct spi_controller *ctlr;
	struct rpcif *rpc;
	int error;

	ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*rpc));
	if (!ctlr)
		return -ENOMEM;

	rpc = spi_controller_get_devdata(ctlr);
	error = rpcif_sw_init(rpc, parent);
	if (error)
		return error;

	platform_set_drvdata(pdev, ctlr);

	ctlr->dev.of_node = parent->of_node;

	rpcif_enable_rpm(rpc);

	ctlr->num_chipselect = 1;
	ctlr->mem_ops = &rpcif_spi_mem_ops;

	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_TX_QUAD | SPI_RX_QUAD;
	ctlr->flags = SPI_CONTROLLER_HALF_DUPLEX;

	rpcif_hw_init(rpc, false);

	error = spi_register_controller(ctlr);
	if (error) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		rpcif_disable_rpm(rpc);
	}

	return error;
}

static int rpcif_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct rpcif *rpc = spi_controller_get_devdata(ctlr);

	spi_unregister_controller(ctlr);
	rpcif_disable_rpm(rpc);

	return 0;
}

static int __maybe_unused rpcif_spi_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	return spi_controller_suspend(ctlr);
}

static int __maybe_unused rpcif_spi_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);

	return spi_controller_resume(ctlr);
}

static SIMPLE_DEV_PM_OPS(rpcif_spi_pm_ops, rpcif_spi_suspend, rpcif_spi_resume);

static struct platform_driver rpcif_spi_driver = {
	.probe	= rpcif_spi_probe,
	.remove	= rpcif_spi_remove,
	.driver = {
		.name	= "rpc-if-spi",
#ifdef CONFIG_PM_SLEEP
		.pm	= &rpcif_spi_pm_ops,
#endif
	},
};
module_platform_driver(rpcif_spi_driver);

MODULE_DESCRIPTION("Renesas RPC-IF SPI driver");
MODULE_LICENSE("GPL v2");

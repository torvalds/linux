/*
 * FPGA DMA transfer module
 *
 * Copyright Altera Corporation (C) 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/****************************************************************************/

static unsigned int max_burst_words = 16;
module_param(max_burst_words, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_burst_words, "Size of a burst in words "
		 "(in this case a word is 64 bits)");

static int timeout = 1000;
module_param(timeout, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(timeout, "Transfer Timeout in msec (default: 1000), "
		 "Pass -1 for infinite timeout");

#define ALT_FPGADMA_DATA_WRITE		0x00
#define ALT_FPGADMA_DATA_READ		0x08

#define ALT_FPGADMA_CSR_WR_WTRMK	0x00
#define ALT_FPGADMA_CSR_RD_WTRMK	0x04
#define ALT_FPGADMA_CSR_BURST		0x08
#define ALT_FPGADMA_CSR_FIFO_STATUS	0x0C
#define ALT_FPGADMA_CSR_DATA_WIDTH	0x10
#define ALT_FPGADMA_CSR_FIFO_DEPTH	0x14
#define ALT_FPGADMA_CSR_FIFO_CLEAR	0x18
#define ALT_FPGADMA_CSR_ZERO		0x1C

#define ALT_FPGADMA_CSR_BURST_TX_SINGLE	(1 << 0)
#define ALT_FPGADMA_CSR_BURST_TX_BURST	(1 << 1)
#define ALT_FPGADMA_CSR_BURST_RX_SINGLE	(1 << 2)
#define ALT_FPGADMA_CSR_BURST_RX_BURST	(1 << 3)

#define ALT_FPGADMA_FIFO_FULL		(1 << 25)
#define ALT_FPGADMA_FIFO_EMPTY		(1 << 24)
#define ALT_FPGADMA_FIFO_USED_MASK	((1 << 24)-1)

struct fpga_dma_pdata {

	struct platform_device *pdev;

	struct dentry *root;

	unsigned int data_reg_phy;
	void __iomem *data_reg;
	void __iomem *csr_reg;

	unsigned int fifo_size_bytes;
	unsigned int fifo_depth;
	unsigned int data_width;
	unsigned int data_width_bytes;
	unsigned char *read_buf;
	unsigned char *write_buf;

	struct dma_chan *txchan;
	struct dma_chan *rxchan;
	dma_addr_t tx_dma_addr;
	dma_addr_t rx_dma_addr;
	dma_cookie_t rx_cookie;
	dma_cookie_t tx_cookie;
};

static DECLARE_COMPLETION(dma_read_complete);
static DECLARE_COMPLETION(dma_write_complete);

#define IS_DMA_READ (true)
#define IS_DMA_WRITE (false)

static int fpga_dma_dma_start_rx(struct platform_device *pdev,
				 unsigned datalen, unsigned char *databuf,
				 u32 burst_size);
static int fpga_dma_dma_start_tx(struct platform_device *pdev,
				 unsigned datalen, unsigned char *databuf,
				 u32 burst_size);

/* --------------------------------------------------------------------- */

static void dump_csr(struct fpga_dma_pdata *pdata)
{
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_WR_WTRMK      %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_WR_WTRMK));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_RD_WTRMK      %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_RD_WTRMK));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_BURST         %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_BURST));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_FIFO_STATUS   %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_FIFO_STATUS));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_DATA_WIDTH    %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_DATA_WIDTH));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_FIFO_DEPTH    %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_FIFO_DEPTH));
	dev_info(&pdata->pdev->dev, "ALT_FPGADMA_CSR_ZERO          %08x\n",
		 readl(pdata->csr_reg + ALT_FPGADMA_CSR_ZERO));
}

/* --------------------------------------------------------------------- */

static void recalc_burst_and_words(struct fpga_dma_pdata *pdata,
				   int *burst_size, int *num_words)
{
	/* adjust size and maxburst so that total bytes transferred
	   is a multiple of burst length and width */
	if (*num_words < max_burst_words) {
		/* we have only a few words left, make it our burst size */
		*burst_size = *num_words;
	} else {
		/* here we may not transfer all words to FIFO, but next
		   call will pick them up... */
		*num_words = max_burst_words * (*num_words / max_burst_words);
		*burst_size = max_burst_words;
	}
}

static int word_to_bytes(struct fpga_dma_pdata *pdata, int num_bytes)
{
	return (num_bytes + pdata->data_width_bytes - 1)
	    / pdata->data_width_bytes;
}

static ssize_t dbgfs_write_dma(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	int ret = 0;
	int bytes_to_transfer;
	int num_words;
	u32 burst_size;
	int pad_index;

	*ppos = 0;

	/* get user data into kernel buffer */
	bytes_to_transfer = simple_write_to_buffer(pdata->write_buf,
						   pdata->fifo_size_bytes, ppos,
						   user_buf, count);
	pad_index = bytes_to_transfer;

	num_words = word_to_bytes(pdata, bytes_to_transfer);
	recalc_burst_and_words(pdata, &burst_size, &num_words);
	/* we sometimes send more than asked for, padded with zeros */
	bytes_to_transfer = num_words * pdata->data_width_bytes;
	for (; pad_index < bytes_to_transfer; pad_index++)
		pdata->write_buf[pad_index] = 0;

	ret = fpga_dma_dma_start_tx(pdata->pdev,
				    bytes_to_transfer, pdata->write_buf,
				    burst_size);
	if (ret) {
		dev_err(&pdata->pdev->dev, "Error starting TX DMA %d\n", ret);
		return ret;
	}

	if (!wait_for_completion_timeout(&dma_write_complete,
					 msecs_to_jiffies(timeout))) {
		dev_err(&pdata->pdev->dev, "Timeout waiting for TX DMA!\n");
		dev_err(&pdata->pdev->dev,
			"count %d burst_size %d num_words %d bytes_to_transfer %d\n",
			count, burst_size, num_words, bytes_to_transfer);
		dmaengine_terminate_all(pdata->txchan);
		return -ETIMEDOUT;
	}

	return bytes_to_transfer;
}

static ssize_t dbgfs_read_dma(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	int ret;
	int num_words;
	int num_bytes;
	u32 burst_size;

	num_words = readl(pdata->csr_reg + ALT_FPGADMA_CSR_FIFO_STATUS);
	num_words &= ALT_FPGADMA_FIFO_USED_MASK;

	num_bytes = num_words * pdata->data_width_bytes;
	if (num_bytes > count) {
		dev_dbg(&pdata->pdev->dev,
			"dbgfs_read_dma num_bytes %d > count %d\n",
			num_bytes, count);
		num_bytes = count;
		num_words = num_bytes / (pdata->data_width_bytes);
	}
	if (num_bytes > pdata->fifo_size_bytes) {
		dev_dbg(&pdata->pdev->dev,
			"dbgfs_read_dma num_bytes %d > pdata->fifo_size_bytes %d\n",
			num_bytes, pdata->fifo_size_bytes);
		num_bytes = pdata->fifo_size_bytes;
		num_words = num_bytes / (pdata->data_width_bytes);
	}

	recalc_burst_and_words(pdata, &burst_size, &num_words);
	num_bytes = num_words * pdata->data_width_bytes;

	if (num_bytes > 0) {
		ret = fpga_dma_dma_start_rx(pdata->pdev, num_bytes,
					    pdata->read_buf, burst_size);
		if (ret) {
			dev_err(&pdata->pdev->dev,
				"Error starting RX DMA %d\n", ret);
			return ret;
		}

		if (!wait_for_completion_timeout(&dma_read_complete,
						 msecs_to_jiffies(timeout))) {
			dev_err(&pdata->pdev->dev,
				"Timeout waiting for RX DMA!\n");
			dmaengine_terminate_all(pdata->rxchan);
			return -ETIMEDOUT;
		}
		*ppos = 0;
	}
	return simple_read_from_buffer(user_buf, count, ppos,
				       pdata->read_buf, num_bytes);
}

static const struct file_operations dbgfs_dma_fops = {
	.write = dbgfs_write_dma,
	.read = dbgfs_read_dma,
	.open = simple_open,
	.llseek = no_llseek,
};

/* --------------------------------------------------------------------- */

static ssize_t dbgfs_read_csr(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	dump_csr(pdata);
	return 0;
}

static const struct file_operations dbgfs_csr_fops = {
	.read = dbgfs_read_csr,
	.open = simple_open,
	.llseek = no_llseek,
};

/* --------------------------------------------------------------------- */

static ssize_t dbgfs_write_clear(struct file *file,
				 const char __user *user_buf, size_t count,
				 loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	writel(1, pdata->csr_reg + ALT_FPGADMA_CSR_FIFO_CLEAR);
	return count;
}

static const struct file_operations dbgfs_clear_fops = {
	.write = dbgfs_write_clear,
	.open = simple_open,
	.llseek = no_llseek,
};

/* --------------------------------------------------------------------- */

static ssize_t dbgfs_write_wrwtrmk(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	char buf[32];
	unsigned long val;
	int ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, user_buf, min(count, (sizeof(buf) - 1))))
		return -EFAULT;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	writel(val, pdata->csr_reg + ALT_FPGADMA_CSR_WR_WTRMK);
	return count;
}

static const struct file_operations dbgfs_wrwtrmk_fops = {
	.write = dbgfs_write_wrwtrmk,
	.open = simple_open,
	.llseek = no_llseek,
};

/* --------------------------------------------------------------------- */

static ssize_t dbgfs_write_rdwtrmk(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct fpga_dma_pdata *pdata = file->private_data;
	char buf[32];
	int ret;
	unsigned long val;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, user_buf, min(count, (sizeof(buf) - 1))))
		return -EFAULT;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	writel(val, pdata->csr_reg + ALT_FPGADMA_CSR_RD_WTRMK);
	return count;
}

static const struct file_operations dbgfs_rdwtrmk_fops = {
	.write = dbgfs_write_rdwtrmk,
	.open = simple_open,
	.llseek = no_llseek,
};

/* --------------------------------------------------------------------- */

static int fpga_dma_register_dbgfs(struct fpga_dma_pdata *pdata)
{
	struct dentry *d;

	d = debugfs_create_dir("fpga_dma", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);
	if (!d) {
		dev_err(&pdata->pdev->dev, "Failed to initialize debugfs\n");
		return -ENOMEM;
	}

	pdata->root = d;

	debugfs_create_file("dma", S_IWUSR | S_IRUGO, pdata->root, pdata,
			    &dbgfs_dma_fops);

	debugfs_create_file("csr", S_IRUGO, pdata->root, pdata,
			    &dbgfs_csr_fops);

	debugfs_create_file("clear", S_IWUSR, pdata->root, pdata,
			    &dbgfs_clear_fops);

	debugfs_create_file("wrwtrmk", S_IWUSR, pdata->root, pdata,
			    &dbgfs_wrwtrmk_fops);

	debugfs_create_file("rdwtrmk", S_IWUSR, pdata->root, pdata,
			    &dbgfs_rdwtrmk_fops);

	return 0;
}

/* --------------------------------------------------------------------- */

static void fpga_dma_dma_rx_done(void *arg)
{
	complete(&dma_read_complete);
}

static void fpga_dma_dma_tx_done(void *arg)
{
	complete(&dma_write_complete);
}

static void fpga_dma_dma_cleanup(struct platform_device *pdev,
				 unsigned datalen, bool do_read)
{
	struct fpga_dma_pdata *pdata = platform_get_drvdata(pdev);
	if (do_read)
		dma_unmap_single(&pdev->dev, pdata->rx_dma_addr,
				 datalen, DMA_FROM_DEVICE);
	else
		dma_unmap_single(&pdev->dev, pdata->tx_dma_addr,
				 datalen, DMA_TO_DEVICE);
}

static int fpga_dma_dma_start_rx(struct platform_device *pdev,
				 unsigned datalen, unsigned char *databuf,
				 u32 burst_size)
{
	struct fpga_dma_pdata *pdata = platform_get_drvdata(pdev);
	struct dma_chan *dmachan;
	struct dma_slave_config dmaconf;
	struct dma_async_tx_descriptor *dmadesc = NULL;

	int num_words;

	num_words = word_to_bytes(pdata, datalen);

	dmachan = pdata->rxchan;
	memset(&dmaconf, 0, sizeof(dmaconf));
	dmaconf.direction = DMA_DEV_TO_MEM;
	dmaconf.src_addr = pdata->data_reg_phy + ALT_FPGADMA_DATA_READ;
	dmaconf.src_addr_width = 8;
	dmaconf.src_maxburst = burst_size;

	pdata->rx_dma_addr = dma_map_single(&pdev->dev,
					    databuf, datalen, DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, pdata->rx_dma_addr)) {
		dev_err(&pdev->dev, "dma_map_single for RX failed\n");
		return -EINVAL;
	}

	/* set up slave config */
	dmaengine_slave_config(dmachan, &dmaconf);

	/* get dmadesc */
	dmadesc = dmaengine_prep_slave_single(dmachan,
					      pdata->rx_dma_addr,
					      datalen,
					      dmaconf.direction,
					      DMA_PREP_INTERRUPT);
	if (!dmadesc) {
		fpga_dma_dma_cleanup(pdev, datalen, IS_DMA_READ);
		return -ENOMEM;
	}
	dmadesc->callback = fpga_dma_dma_rx_done;
	dmadesc->callback_param = pdata;

	/* start DMA */
	pdata->rx_cookie = dmaengine_submit(dmadesc);
	if (dma_submit_error(pdata->rx_cookie))
		dev_err(&pdev->dev, "rx_cookie error on dmaengine_submit\n");
	dma_async_issue_pending(dmachan);

	return 0;
}

static int fpga_dma_dma_start_tx(struct platform_device *pdev,
				 unsigned datalen, unsigned char *databuf,
				 u32 burst_size)
{
	struct fpga_dma_pdata *pdata = platform_get_drvdata(pdev);
	struct dma_chan *dmachan;
	struct dma_slave_config dmaconf;
	struct dma_async_tx_descriptor *dmadesc = NULL;

	int num_words;

	num_words = word_to_bytes(pdata, datalen);

	dmachan = pdata->txchan;
	memset(&dmaconf, 0, sizeof(dmaconf));
	dmaconf.direction = DMA_MEM_TO_DEV;
	dmaconf.dst_addr = pdata->data_reg_phy + ALT_FPGADMA_DATA_WRITE;
	dmaconf.dst_addr_width = 8;
	dmaconf.dst_maxburst = burst_size;
	pdata->tx_dma_addr = dma_map_single(&pdev->dev,
					    databuf, datalen, DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, pdata->tx_dma_addr)) {
		dev_err(&pdev->dev, "dma_map_single for TX failed\n");
		return -EINVAL;
	}

	/* set up slave config */
	dmaengine_slave_config(dmachan, &dmaconf);

	/* get dmadesc */
	dmadesc = dmaengine_prep_slave_single(dmachan,
					      pdata->tx_dma_addr,
					      datalen,
					      dmaconf.direction,
					      DMA_PREP_INTERRUPT);
	if (!dmadesc) {
		fpga_dma_dma_cleanup(pdev, datalen, IS_DMA_WRITE);
		return -ENOMEM;
	}
	dmadesc->callback = fpga_dma_dma_tx_done;
	dmadesc->callback_param = pdata;

	/* start DMA */
	pdata->tx_cookie = dmaengine_submit(dmadesc);
	if (dma_submit_error(pdata->tx_cookie))
		dev_err(&pdev->dev, "tx_cookie error on dmaengine_submit\n");
	dma_async_issue_pending(dmachan);

	return 0;
}

static void fpga_dma_dma_shutdown(struct fpga_dma_pdata *pdata)
{
	if (pdata->txchan) {
		dmaengine_terminate_all(pdata->txchan);
		dma_release_channel(pdata->txchan);
	}
	if (pdata->rxchan) {
		dmaengine_terminate_all(pdata->rxchan);
		dma_release_channel(pdata->rxchan);
	}
	pdata->rxchan = pdata->txchan = NULL;
}

static int fpga_dma_dma_init(struct fpga_dma_pdata *pdata)
{
	struct platform_device *pdev = pdata->pdev;

	pdata->txchan = dma_request_slave_channel(&pdev->dev, "tx");
	if (pdata->txchan)
		dev_dbg(&pdev->dev, "TX channel %s %d selected\n",
			dma_chan_name(pdata->txchan), pdata->txchan->chan_id);
	else
		dev_err(&pdev->dev, "could not get TX dma channel\n");

	pdata->rxchan = dma_request_slave_channel(&pdev->dev, "rx");
	if (pdata->rxchan)
		dev_dbg(&pdev->dev, "RX channel %s %d selected\n",
			dma_chan_name(pdata->rxchan), pdata->rxchan->chan_id);
	else
		dev_err(&pdev->dev, "could not get RX dma channel\n");

	if (!pdata->rxchan && !pdata->txchan)
		/* both channels not there, maybe it's
		   bcs dma isn't loaded... */
		return -EPROBE_DEFER;

	if (!pdata->rxchan || !pdata->txchan)
		return -ENOMEM;

	return 0;
}

/* --------------------------------------------------------------------- */

static void __iomem *request_and_map(struct platform_device *pdev,
				     const struct resource *res)
{
	void __iomem *ptr;

	if (!devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
				     pdev->name)) {
		dev_err(&pdev->dev, "unable to request %s\n", res->name);
		return NULL;
	}

	ptr = devm_ioremap_nocache(&pdev->dev, res->start, resource_size(res));
	if (!ptr)
		dev_err(&pdev->dev, "ioremap_nocache of %s failed!", res->name);

	return ptr;
}

static int fpga_dma_remove(struct platform_device *pdev)
{
	struct fpga_dma_pdata *pdata = platform_get_drvdata(pdev);
	dev_dbg(&pdev->dev, "fpga_dma_remove\n");
	debugfs_remove_recursive(pdata->root);
	fpga_dma_dma_shutdown(pdata);
	return 0;
}

static int fpga_dma_probe(struct platform_device *pdev)
{
	struct resource *csr_reg, *data_reg;
	struct fpga_dma_pdata *pdata;
	int ret;

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct fpga_dma_pdata),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	csr_reg = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csr");
	data_reg = platform_get_resource_byname(pdev, IORESOURCE_MEM, "data");
	if (!csr_reg || !data_reg) {
		dev_err(&pdev->dev, "registers not completely defined\n");
		return -EINVAL;
	}

	pdata->csr_reg = request_and_map(pdev, csr_reg);
	if (!pdata->csr_reg)
		return -ENOMEM;

	pdata->data_reg = request_and_map(pdev, data_reg);
	if (!pdata->data_reg)
		return -ENOMEM;
	pdata->data_reg_phy = data_reg->start;

	/* read HW and calculate fifo size in bytes */
	pdata->fifo_depth = readl(pdata->csr_reg + ALT_FPGADMA_CSR_FIFO_DEPTH);
	pdata->data_width = readl(pdata->csr_reg + ALT_FPGADMA_CSR_DATA_WIDTH);
	/* 64-bit bus to FIFO */
	pdata->data_width_bytes = pdata->data_width / sizeof(u64);
	pdata->fifo_size_bytes = pdata->fifo_depth * pdata->data_width_bytes;

	pdata->read_buf = devm_kzalloc(&pdev->dev, pdata->fifo_size_bytes,
				       GFP_KERNEL);
	if (!pdata->read_buf)
		return -ENOMEM;

	pdata->write_buf = devm_kzalloc(&pdev->dev, pdata->fifo_size_bytes,
					GFP_KERNEL);
	if (!pdata->write_buf)
		return -ENOMEM;

	ret = fpga_dma_register_dbgfs(pdata);
	if (ret)
		return ret;

	pdata->pdev = pdev;
	platform_set_drvdata(pdev, pdata);

	ret = fpga_dma_dma_init(pdata);
	if (ret) {
		fpga_dma_remove(pdev);
		return ret;
	}

	/* OK almost ready, set up the watermarks */
	/* we may need to tweak this for single/burst, etc */
	writel(pdata->fifo_depth - max_burst_words,
	       pdata->csr_reg + ALT_FPGADMA_CSR_WR_WTRMK);
	/* we use read watermark of 0 so that rx_burst line
	   is always asserted, i.e. no single-only requests */
	writel(0, pdata->csr_reg + ALT_FPGADMA_CSR_RD_WTRMK);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id fpga_dma_of_match[] = {
	{.compatible = "altr,fpga-dma",},
	{},
};

MODULE_DEVICE_TABLE(of, fpga_dma_of_match);
#endif

static struct platform_driver fpga_dma_driver = {
	.probe = fpga_dma_probe,
	.remove = fpga_dma_remove,
	.driver = {
		   .name = "fpga_dma",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(fpga_dma_of_match),
		   },
};

static int __init fpga_dma_init(void)
{
	return platform_driver_probe(&fpga_dma_driver, fpga_dma_probe);
}

static void __exit fpga_dma_exit(void)
{
	platform_driver_unregister(&fpga_dma_driver);
}

late_initcall(fpga_dma_init);
module_exit(fpga_dma_exit);

MODULE_AUTHOR("Graham Moore (Altera)");
MODULE_DESCRIPTION("Altera FPGA DMA Example Driver");
MODULE_LICENSE("GPL v2");

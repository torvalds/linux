// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * AMD MP2 PCIe communication driver
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Elie Morisse <syniurge@gmail.com>
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "i2c-amd-mp2.h"

#include <linux/io-64-nonatomic-lo-hi.h>

static void amd_mp2_c2p_mutex_lock(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;

	/* there is only one data mailbox for two i2c adapters */
	mutex_lock(&privdata->c2p_lock);
	privdata->c2p_lock_busid = i2c_common->bus_id;
}

static void amd_mp2_c2p_mutex_unlock(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;

	if (unlikely(privdata->c2p_lock_busid != i2c_common->bus_id)) {
		dev_warn(ndev_dev(privdata),
			 "bus %d attempting to unlock C2P locked by bus %d\n",
			 i2c_common->bus_id, privdata->c2p_lock_busid);
		return;
	}

	mutex_unlock(&privdata->c2p_lock);
}

static int amd_mp2_cmd(struct amd_i2c_common *i2c_common,
		       union i2c_cmd_base i2c_cmd_base)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;
	void __iomem *reg;

	i2c_common->reqcmd = i2c_cmd_base.s.i2c_cmd;

	reg = privdata->mmio + ((i2c_cmd_base.s.bus_id == 1) ?
				AMD_C2P_MSG1 : AMD_C2P_MSG0);
	writel(i2c_cmd_base.ul, reg);

	return 0;
}

int amd_mp2_bus_enable_set(struct amd_i2c_common *i2c_common, bool enable)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;
	union i2c_cmd_base i2c_cmd_base;

	dev_dbg(ndev_dev(privdata), "%s id: %d\n", __func__,
		i2c_common->bus_id);

	i2c_cmd_base.ul = 0;
	i2c_cmd_base.s.i2c_cmd = enable ? i2c_enable : i2c_disable;
	i2c_cmd_base.s.bus_id = i2c_common->bus_id;
	i2c_cmd_base.s.i2c_speed = i2c_common->i2c_speed;

	amd_mp2_c2p_mutex_lock(i2c_common);

	return amd_mp2_cmd(i2c_common, i2c_cmd_base);
}
EXPORT_SYMBOL_GPL(amd_mp2_bus_enable_set);

static void amd_mp2_cmd_rw_fill(struct amd_i2c_common *i2c_common,
				union i2c_cmd_base *i2c_cmd_base,
				enum i2c_cmd reqcmd)
{
	i2c_cmd_base->s.i2c_cmd = reqcmd;
	i2c_cmd_base->s.bus_id = i2c_common->bus_id;
	i2c_cmd_base->s.i2c_speed = i2c_common->i2c_speed;
	i2c_cmd_base->s.slave_addr = i2c_common->msg->addr;
	i2c_cmd_base->s.length = i2c_common->msg->len;
}

int amd_mp2_rw(struct amd_i2c_common *i2c_common, enum i2c_cmd reqcmd)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;
	union i2c_cmd_base i2c_cmd_base;

	amd_mp2_cmd_rw_fill(i2c_common, &i2c_cmd_base, reqcmd);
	amd_mp2_c2p_mutex_lock(i2c_common);

	if (i2c_common->msg->len <= 32) {
		i2c_cmd_base.s.mem_type = use_c2pmsg;
		if (reqcmd == i2c_write)
			memcpy_toio(privdata->mmio + AMD_C2P_MSG2,
				    i2c_common->msg->buf,
				    i2c_common->msg->len);
	} else {
		i2c_cmd_base.s.mem_type = use_dram;
		writeq((u64)i2c_common->dma_addr,
		       privdata->mmio + AMD_C2P_MSG2);
	}

	return amd_mp2_cmd(i2c_common, i2c_cmd_base);
}
EXPORT_SYMBOL_GPL(amd_mp2_rw);

static void amd_mp2_pci_check_rw_event(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;
	int len = i2c_common->eventval.r.length;
	u32 slave_addr = i2c_common->eventval.r.slave_addr;
	bool err = false;

	if (unlikely(len != i2c_common->msg->len)) {
		dev_err(ndev_dev(privdata),
			"length %d in event doesn't match buffer length %d!\n",
			len, i2c_common->msg->len);
		err = true;
	}

	if (unlikely(slave_addr != i2c_common->msg->addr)) {
		dev_err(ndev_dev(privdata),
			"unexpected slave address %x (expected: %x)!\n",
			slave_addr, i2c_common->msg->addr);
		err = true;
	}

	if (!err)
		i2c_common->cmd_success = true;
}

static void __amd_mp2_process_event(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;
	enum status_type sts = i2c_common->eventval.r.status;
	enum response_type res = i2c_common->eventval.r.response;
	int len = i2c_common->eventval.r.length;

	if (res != command_success) {
		if (res != command_failed)
			dev_err(ndev_dev(privdata), "invalid response to i2c command!\n");
		return;
	}

	switch (i2c_common->reqcmd) {
	case i2c_read:
		if (sts == i2c_readcomplete_event) {
			amd_mp2_pci_check_rw_event(i2c_common);
			if (len <= 32)
				memcpy_fromio(i2c_common->msg->buf,
					      privdata->mmio + AMD_C2P_MSG2,
					      len);
		} else if (sts != i2c_readfail_event) {
			dev_err(ndev_dev(privdata),
				"invalid i2c status after read (%d)!\n", sts);
		}
		break;
	case i2c_write:
		if (sts == i2c_writecomplete_event)
			amd_mp2_pci_check_rw_event(i2c_common);
		else if (sts != i2c_writefail_event)
			dev_err(ndev_dev(privdata),
				"invalid i2c status after write (%d)!\n", sts);
		break;
	case i2c_enable:
		if (sts == i2c_busenable_complete)
			i2c_common->cmd_success = true;
		else if (sts != i2c_busenable_failed)
			dev_err(ndev_dev(privdata),
				"invalid i2c status after bus enable (%d)!\n",
				sts);
		break;
	case i2c_disable:
		if (sts == i2c_busdisable_complete)
			i2c_common->cmd_success = true;
		else if (sts != i2c_busdisable_failed)
			dev_err(ndev_dev(privdata),
				"invalid i2c status after bus disable (%d)!\n",
				sts);
		break;
	default:
		break;
	}
}

void amd_mp2_process_event(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;

	if (unlikely(i2c_common->reqcmd == i2c_none)) {
		dev_warn(ndev_dev(privdata),
			 "received msg but no cmd was sent (bus = %d)!\n",
			 i2c_common->bus_id);
		return;
	}

	__amd_mp2_process_event(i2c_common);

	i2c_common->reqcmd = i2c_none;
	amd_mp2_c2p_mutex_unlock(i2c_common);
}
EXPORT_SYMBOL_GPL(amd_mp2_process_event);

static irqreturn_t amd_mp2_irq_isr(int irq, void *dev)
{
	struct amd_mp2_dev *privdata = dev;
	struct amd_i2c_common *i2c_common;
	u32 val;
	unsigned int bus_id;
	void __iomem *reg;
	enum irqreturn ret = IRQ_NONE;

	for (bus_id = 0; bus_id < 2; bus_id++) {
		i2c_common = privdata->busses[bus_id];
		if (!i2c_common)
			continue;

		reg = privdata->mmio + ((bus_id == 0) ?
					AMD_P2C_MSG1 : AMD_P2C_MSG2);
		val = readl(reg);
		if (val != 0) {
			writel(0, reg);
			writel(0, privdata->mmio + AMD_P2C_MSG_INTEN);
			i2c_common->eventval.ul = val;
			i2c_common->cmd_completion(i2c_common);

			ret = IRQ_HANDLED;
		}
	}

	if (ret != IRQ_HANDLED) {
		val = readl(privdata->mmio + AMD_P2C_MSG_INTEN);
		if (val != 0) {
			writel(0, privdata->mmio + AMD_P2C_MSG_INTEN);
			dev_warn(ndev_dev(privdata),
				 "received irq without message\n");
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

void amd_mp2_rw_timeout(struct amd_i2c_common *i2c_common)
{
	i2c_common->reqcmd = i2c_none;
	amd_mp2_c2p_mutex_unlock(i2c_common);
}
EXPORT_SYMBOL_GPL(amd_mp2_rw_timeout);

int amd_mp2_register_cb(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;

	if (i2c_common->bus_id > 1)
		return -EINVAL;

	if (privdata->busses[i2c_common->bus_id]) {
		dev_err(ndev_dev(privdata),
			"Bus %d already taken!\n", i2c_common->bus_id);
		return -EINVAL;
	}

	privdata->busses[i2c_common->bus_id] = i2c_common;

	return 0;
}
EXPORT_SYMBOL_GPL(amd_mp2_register_cb);

int amd_mp2_unregister_cb(struct amd_i2c_common *i2c_common)
{
	struct amd_mp2_dev *privdata = i2c_common->mp2_dev;

	privdata->busses[i2c_common->bus_id] = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(amd_mp2_unregister_cb);

static void amd_mp2_clear_reg(struct amd_mp2_dev *privdata)
{
	int reg;

	for (reg = AMD_C2P_MSG0; reg <= AMD_C2P_MSG9; reg += 4)
		writel(0, privdata->mmio + reg);

	for (reg = AMD_P2C_MSG1; reg <= AMD_P2C_MSG2; reg += 4)
		writel(0, privdata->mmio + reg);
}

static int amd_mp2_pci_init(struct amd_mp2_dev *privdata,
			    struct pci_dev *pci_dev)
{
	int rc;

	pci_set_drvdata(pci_dev, privdata);

	rc = pcim_enable_device(pci_dev);
	if (rc) {
		dev_err(ndev_dev(privdata), "Failed to enable MP2 PCI device\n");
		goto err_pci_enable;
	}

	rc = pcim_iomap_regions(pci_dev, 1 << 2, pci_name(pci_dev));
	if (rc) {
		dev_err(ndev_dev(privdata), "I/O memory remapping failed\n");
		goto err_pci_enable;
	}
	privdata->mmio = pcim_iomap_table(pci_dev)[2];

	pci_set_master(pci_dev);

	rc = pci_set_dma_mask(pci_dev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_dma_mask(pci_dev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
	}

	/* Set up intx irq */
	writel(0, privdata->mmio + AMD_P2C_MSG_INTEN);
	pci_intx(pci_dev, 1);
	rc = devm_request_irq(&pci_dev->dev, pci_dev->irq, amd_mp2_irq_isr,
			      IRQF_SHARED, dev_name(&pci_dev->dev), privdata);
	if (rc)
		dev_err(&pci_dev->dev, "Failure requesting irq %i: %d\n",
			pci_dev->irq, rc);

	return rc;

err_dma_mask:
	pci_clear_master(pci_dev);
err_pci_enable:
	pci_set_drvdata(pci_dev, NULL);
	return rc;
}

static int amd_mp2_pci_probe(struct pci_dev *pci_dev,
			     const struct pci_device_id *id)
{
	struct amd_mp2_dev *privdata;
	int rc;

	privdata = devm_kzalloc(&pci_dev->dev, sizeof(*privdata), GFP_KERNEL);
	if (!privdata)
		return -ENOMEM;

	rc = amd_mp2_pci_init(privdata, pci_dev);
	if (rc)
		return rc;

	mutex_init(&privdata->c2p_lock);
	privdata->pci_dev = pci_dev;

	pm_runtime_set_autosuspend_delay(&pci_dev->dev, 1000);
	pm_runtime_use_autosuspend(&pci_dev->dev);
	pm_runtime_put_autosuspend(&pci_dev->dev);
	pm_runtime_allow(&pci_dev->dev);

	privdata->probed = true;

	dev_info(&pci_dev->dev, "MP2 device registered.\n");
	return 0;
}

static void amd_mp2_pci_remove(struct pci_dev *pci_dev)
{
	struct amd_mp2_dev *privdata = pci_get_drvdata(pci_dev);

	pm_runtime_forbid(&pci_dev->dev);
	pm_runtime_get_noresume(&pci_dev->dev);

	pci_intx(pci_dev, 0);
	pci_clear_master(pci_dev);

	amd_mp2_clear_reg(privdata);
}

#ifdef CONFIG_PM
static int amd_mp2_pci_suspend(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct amd_mp2_dev *privdata = pci_get_drvdata(pci_dev);
	struct amd_i2c_common *i2c_common;
	unsigned int bus_id;
	int ret = 0;

	for (bus_id = 0; bus_id < 2; bus_id++) {
		i2c_common = privdata->busses[bus_id];
		if (i2c_common)
			i2c_common->suspend(i2c_common);
	}

	ret = pci_save_state(pci_dev);
	if (ret) {
		dev_err(ndev_dev(privdata),
			"pci_save_state failed = %d\n", ret);
		return ret;
	}

	pci_disable_device(pci_dev);
	return ret;
}

static int amd_mp2_pci_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct amd_mp2_dev *privdata = pci_get_drvdata(pci_dev);
	struct amd_i2c_common *i2c_common;
	unsigned int bus_id;
	int ret = 0;

	pci_restore_state(pci_dev);
	ret = pci_enable_device(pci_dev);
	if (ret < 0) {
		dev_err(ndev_dev(privdata),
			"pci_enable_device failed = %d\n", ret);
		return ret;
	}

	for (bus_id = 0; bus_id < 2; bus_id++) {
		i2c_common = privdata->busses[bus_id];
		if (i2c_common) {
			ret = i2c_common->resume(i2c_common);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

static UNIVERSAL_DEV_PM_OPS(amd_mp2_pci_pm_ops, amd_mp2_pci_suspend,
			    amd_mp2_pci_resume, NULL);
#endif /* CONFIG_PM */

static const struct pci_device_id amd_mp2_pci_tbl[] = {
	{PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2)},
	{0}
};
MODULE_DEVICE_TABLE(pci, amd_mp2_pci_tbl);

static struct pci_driver amd_mp2_pci_driver = {
	.name		= "i2c_amd_mp2",
	.id_table	= amd_mp2_pci_tbl,
	.probe		= amd_mp2_pci_probe,
	.remove		= amd_mp2_pci_remove,
#ifdef CONFIG_PM
	.driver = {
		.pm	= &amd_mp2_pci_pm_ops,
	},
#endif
};
module_pci_driver(amd_mp2_pci_driver);

static int amd_mp2_device_match(struct device *dev, const void *data)
{
	return 1;
}

struct amd_mp2_dev *amd_mp2_find_device(void)
{
	struct device *dev;
	struct pci_dev *pci_dev;

	dev = driver_find_device(&amd_mp2_pci_driver.driver, NULL, NULL,
				 amd_mp2_device_match);
	if (!dev)
		return NULL;

	pci_dev = to_pci_dev(dev);
	return (struct amd_mp2_dev *)pci_get_drvdata(pci_dev);
}
EXPORT_SYMBOL_GPL(amd_mp2_find_device);

MODULE_DESCRIPTION("AMD(R) PCI-E MP2 I2C Controller Driver");
MODULE_AUTHOR("Shyam Sundar S K <Shyam-sundar.S-k@amd.com>");
MODULE_AUTHOR("Elie Morisse <syniurge@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");

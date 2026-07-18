// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>

#include "eea_net.h"
#include "eea_pci.h"

#define EEA_PCI_DB_OFFSET 4096
#define EEA_PCI_DB_MIN_SIZE 8
#define EEA_PCI_DB_MAX_SIZE 512
#define EEA_PCI_Q_MAX_NUM 1000

#define EEA_PCI_CAP_RESET_DEVICE 0xFA
#define EEA_PCI_CAP_RESET_FLAG BIT(1)

struct eea_pci_cfg {
	__le32 reserve0;
	__le32 reserve1;
	__le32 drv_f_idx;
	__le32 drv_f;

#define EEA_S_INIT         (BIT(0) | BIT(1))
#define EEA_S_OK           BIT(2)
#define EEA_S_FEATURE_DONE BIT(3)
#define EEA_S_FAILED       BIT(7)
	u8   device_status;
	u8   reserved[7];

	__le32 rx_num_max;
	__le32 tx_num_max;
	__le32 db_blk_size;

	/* admin queue cfg */
	__le16 aq_size;
	__le16 aq_msix_vector;
	__le32 aq_db_off;

	__le32 aq_sq_addr;
	__le32 aq_sq_addr_hi;
	__le32 aq_cq_addr;
	__le32 aq_cq_addr_hi;

	__le32 reserved1;
	__le64 hw_ts;
};

struct eea_pci_device {
	struct eea_device edev;
	struct pci_dev *pci_dev;

	u32 msix_vec_n;
	u32 db_len;

	void __iomem *reg;
	void __iomem *db_base;
	void __iomem *db_end;

	int ha_irq;

	struct work_struct ha_handle_work;
	char ha_irq_name[32];
	int reset_pos;
	bool ha_ready;

	bool shutdown;
};

#define cfg_pointer(reg, item) \
	((void __iomem *)((reg) + offsetof(struct eea_pci_cfg, item)))

#define cfg_write8(reg, item, val) iowrite8(val, cfg_pointer(reg, item))
#define cfg_write16(reg, item, val) iowrite16(val, cfg_pointer(reg, item))
#define cfg_write32(reg, item, val) iowrite32(val, cfg_pointer(reg, item))
#define cfg_write64(reg, item, val) iowrite64_lo_hi(val, cfg_pointer(reg, item))

#define cfg_read8(reg, item) ioread8(cfg_pointer(reg, item))
#define cfg_read32(reg, item) ioread32(cfg_pointer(reg, item))
#define cfg_read64(reg, item) ioread64(cfg_pointer(reg, item))

/* Due to circular references, we have to add function definitions here. */
static int __eea_pci_probe(struct pci_dev *pci_dev,
			   struct eea_pci_device *ep_dev, bool pci_probe);
static void __eea_pci_remove(struct pci_dev *pci_dev, bool pci_remove);

const char *eea_pci_name(struct eea_device *edev)
{
	return pci_name(edev->ep_dev->pci_dev);
}

int eea_pci_domain_nr(struct eea_device *edev)
{
	return pci_domain_nr(edev->ep_dev->pci_dev->bus);
}

u16 eea_pci_bdf(struct eea_device *edev)
{
	return pci_dev_id(edev->ep_dev->pci_dev);
}

static void eea_pci_io_set_status(struct eea_device *edev, u8 status)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;

	cfg_write8(ep_dev->reg, device_status, status);
}

static u8 eea_pci_io_get_status(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;

	return cfg_read8(ep_dev->reg, device_status);
}

static void eea_add_status(struct eea_device *dev, u32 status)
{
	eea_pci_io_set_status(dev, eea_pci_io_get_status(dev) | status);
}

#define EEA_RESET_TIMEOUT_US (60 * 1000 * 1000)

int eea_device_reset(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;
	int err;
	u8 val;

	eea_pci_io_set_status(edev, 0);

	/* We are no longer waiting for device ack during the shutdown flow. */
	if (ep_dev->shutdown)
		return 0;

	/* A longer timeout is set here to handle edge cases, though it should
	 * return promptly in most scenarios.
	 *
	 * In our case, all replies are handled by the DPU software, so there is
	 * no race condition between the hardware processes and the register.
	 */
	err = read_poll_timeout(cfg_read8, val, (!val || val == 0xFF), 20,
				EEA_RESET_TIMEOUT_US,
				false, ep_dev->reg, device_status);

	/* Surprise PCIe Removal */
	if (val == 0xFF)
		return -EINVAL;

	return err;
}

int eea_pci_set_aq_up(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;
	u8 status = eea_pci_io_get_status(edev);
	int err;
	u8 val;

	eea_pci_io_set_status(edev, status | EEA_S_OK);

	/* A longer timeout is set here to handle edge cases, though it should
	 * return promptly in most scenarios.
	 *
	 * In our case, all replies are handled by the DPU software, so there is
	 * no race condition between the hardware processes and the register.
	 */
	err = read_poll_timeout(cfg_read8, val,
				val & (EEA_S_OK | EEA_S_FAILED),
				20, EEA_RESET_TIMEOUT_US,
				false, ep_dev->reg, device_status);

	/* Surprise PCIe Removal */
	if (val == 0xFF)
		return -EINVAL;

	/* device fail */
	if (val & EEA_S_FAILED)
		return -EINVAL;

	return err;
}

static int eea_negotiate(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev;
	u32 status;

	ep_dev = edev->ep_dev;

	edev->features = 0;

	cfg_write32(ep_dev->reg, drv_f_idx, 0);
	cfg_write32(ep_dev->reg, drv_f, lower_32_bits(edev->features));
	cfg_write32(ep_dev->reg, drv_f_idx, 1);
	cfg_write32(ep_dev->reg, drv_f, upper_32_bits(edev->features));

	eea_add_status(edev, EEA_S_FEATURE_DONE);
	status = eea_pci_io_get_status(edev);

	/* Surprise PCIe Removal */
	if (status == 0xFF)
		return -EINVAL;

	if (!(status & EEA_S_FEATURE_DONE))
		return -ENODEV;

	return 0;
}

static void eea_pci_release_resource(struct eea_pci_device *ep_dev)
{
	struct pci_dev *pci_dev = ep_dev->pci_dev;
	struct eea_device *edev;

	edev = &ep_dev->edev;

	if (edev->status < EEA_PCI_STATUS_READY)
		return;

	if (ep_dev->reg) {
		pci_iounmap(pci_dev, ep_dev->reg);
		ep_dev->reg = NULL;
	}

	if (ep_dev->msix_vec_n) {
		ep_dev->msix_vec_n = 0;
		pci_free_irq_vectors(ep_dev->pci_dev);
	}

	pci_clear_master(pci_dev);
	pci_release_regions(pci_dev);
	pci_disable_device(pci_dev);

	edev->status = EEA_PCI_STATUS_NONE;
}

static int eea_pci_setup(struct pci_dev *pci_dev, struct eea_pci_device *ep_dev)
{
	int err, n, ret, len;

	ep_dev->edev.status = EEA_PCI_STATUS_ERR;

	ep_dev->pci_dev = pci_dev;

	err = pci_enable_device(pci_dev);
	if (err)
		return err;

	err = pci_request_regions(pci_dev, "EEA");
	if (err)
		goto err_disable_dev;

	if (pci_resource_len(pci_dev, 0) < EEA_PCI_DB_OFFSET) {
		dev_err(&pci_dev->dev, "Bar 0 is too small %llu\n",
			(u64)pci_resource_len(pci_dev, 0));
		err = -EINVAL;
		goto err_release_regions;
	}

	ep_dev->reg = pci_iomap(pci_dev, 0, 0);
	if (!ep_dev->reg) {
		dev_err(&pci_dev->dev, "Failed to map pci bar!\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	err = eea_device_reset(&ep_dev->edev);
	if (err) {
		dev_err(&pci_dev->dev, "Failed to reset device for setup!\n");
		goto err_unmap_reg;
	}

	err = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pci_dev->dev, "Failed to enable 64-bit DMA.\n");
		goto err_unmap_reg;
	}

	pci_set_master(pci_dev);

	ep_dev->edev.rx_num = cfg_read32(ep_dev->reg, rx_num_max);
	ep_dev->edev.tx_num = cfg_read32(ep_dev->reg, tx_num_max);

	if (ep_dev->edev.rx_num > EEA_PCI_Q_MAX_NUM ||
	    ep_dev->edev.tx_num > EEA_PCI_Q_MAX_NUM) {
		dev_err(&pci_dev->dev, "Invalid queue num %u %u\n",
			ep_dev->edev.rx_num,
			ep_dev->edev.tx_num);
		err = -EINVAL;
		goto err_clear_master;
	}

	ep_dev->edev.db_blk_size = cfg_read32(ep_dev->reg, db_blk_size);
	if (!IS_ALIGNED(ep_dev->edev.db_blk_size, 8) ||
	    ep_dev->edev.db_blk_size > EEA_PCI_DB_MAX_SIZE ||
	    ep_dev->edev.db_blk_size < EEA_PCI_DB_MIN_SIZE) {
		dev_err(&pci_dev->dev, "Invalid db size %u\n",
			ep_dev->edev.db_blk_size);
		err = -EINVAL;
		goto err_clear_master;
	}

	ep_dev->db_len = ep_dev->edev.db_blk_size * (ep_dev->edev.rx_num +
						     ep_dev->edev.tx_num + 1);
	ep_dev->db_base = ep_dev->reg + EEA_PCI_DB_OFFSET;
	ep_dev->db_end = ep_dev->db_base + ep_dev->db_len;

	len = ep_dev->db_end - ep_dev->reg;

	if (pci_resource_len(pci_dev, 0) < len) {
		dev_err(&pci_dev->dev, "Bar 0 is too small %llu\n",
			(u64)pci_resource_len(pci_dev, 0));
		err = -EINVAL;
		goto err_clear_master;
	}

	/* In our design, the number of hardware interrupts matches the maximum
	 * number of queues. If pci_alloc_irq_vectors failed, return directly.
	 *
	 * 2: adminq, error handle
	 */
	n = ep_dev->edev.rx_num + 2;
	ret = pci_alloc_irq_vectors(ep_dev->pci_dev, n, n, PCI_IRQ_MSIX);
	if (ret != n) {
		err = ret;
		goto err_clear_master;
	}

	ep_dev->msix_vec_n = ret;

	ep_dev->edev.status = EEA_PCI_STATUS_READY;

	return 0;

err_clear_master:
	pci_clear_master(pci_dev);

err_unmap_reg:
	pci_iounmap(pci_dev, ep_dev->reg);
	ep_dev->reg = NULL;

err_release_regions:
	pci_release_regions(pci_dev);

err_disable_dev:
	pci_disable_device(pci_dev);

	return err;
}

void __iomem *eea_pci_db_addr(struct eea_device *edev, u32 off)
{
	u32 max_off;

	if (!IS_ALIGNED(off, 8))
		return NULL;

	max_off = edev->ep_dev->db_len - edev->db_blk_size;

	if (off > max_off)
		return NULL;

	return edev->ep_dev->db_base + off;
}

int eea_pci_active_aq(struct eea_ring *ering, int msix_vec)
{
	struct eea_pci_device *ep_dev = ering->edev->ep_dev;

	cfg_write16(ep_dev->reg, aq_size, ering->num);
	cfg_write16(ep_dev->reg, aq_msix_vector, msix_vec);

	cfg_write64(ep_dev->reg, aq_sq_addr, ering->sq.dma_addr);
	cfg_write64(ep_dev->reg, aq_cq_addr, ering->cq.dma_addr);

	ering->db = eea_pci_db_addr(ering->edev,
				    cfg_read32(ep_dev->reg, aq_db_off));

	if (!ering->db)
		return -EIO;

	return 0;
}

void eea_pci_free_irq(struct eea_irq_blk *blk)
{
	irq_update_affinity_hint(blk->irq, NULL);
	free_irq(blk->irq, blk);
}

int eea_pci_request_irq(struct eea_device *edev, struct eea_irq_blk *blk,
			irqreturn_t (*callback)(int irq, void *data))
{
	struct eea_pci_device *ep_dev = edev->ep_dev;
	int irq;

	snprintf(blk->irq_name, sizeof(blk->irq_name), "eea-q%d@%s", blk->idx,
		 pci_name(ep_dev->pci_dev));

	irq = pci_irq_vector(ep_dev->pci_dev, blk->msix_vec);

	blk->irq = irq;

	return request_irq(irq, callback, IRQF_NO_AUTOEN, blk->irq_name, blk);
}

static void eea_ha_handle_reset(struct eea_pci_device *ep_dev)
{
	struct eea_device *edev;
	struct pci_dev *pci_dev;
	u16 reset;
	int err;

	if (!ep_dev->reset_pos) {
		eea_queues_check_and_reset(&ep_dev->edev);
		return;
	}

	edev = &ep_dev->edev;

	pci_read_config_word(ep_dev->pci_dev, ep_dev->reset_pos, &reset);

	/* Clear bits using 0xFFFF and ignore all previous messages. */
	pci_write_config_word(ep_dev->pci_dev, ep_dev->reset_pos, 0xFFFF);

	if (reset & EEA_PCI_CAP_RESET_FLAG) {
		dev_warn(&ep_dev->pci_dev->dev, "recv device reset request.\n");

		pci_dev = ep_dev->pci_dev;

		/* The pci remove callback may hold this lock. If the
		 * pci remove callback is called, then we can ignore the
		 * ha interrupt.
		 */
		if (mutex_trylock(&edev->ha_lock)) {
			if (edev->status != EEA_PCI_STATUS_DONE) {
				dev_err(&ep_dev->pci_dev->dev, "ha: reset device: pci status is %d. skip it.\n",
					edev->status);

				mutex_unlock(&edev->ha_lock);
				return;
			}

			__eea_pci_remove(pci_dev, false);
			err = __eea_pci_probe(pci_dev, ep_dev, false);
			if (err)
				/* Currently, for some reason, PCI
				 * initialization or network device re-probing
				 * has failed. Waiting for the PCI subsystem to
				 * call the remove callback to release the
				 * remaining resources.
				 */
				dev_err(&ep_dev->pci_dev->dev,
					"ha: re-setup failed.\n");

			mutex_unlock(&edev->ha_lock);
		} else {
			/* Device removal is in progress, so return directly. */
			dev_warn(&ep_dev->pci_dev->dev,
				 "ha device reset: trylock failed.\n");
		}
		return;
	}

	eea_queues_check_and_reset(&ep_dev->edev);
}

/* ha handle code */
static void eea_ha_handle_work(struct work_struct *work)
{
	struct eea_pci_device *ep_dev;

	ep_dev = container_of(work, struct eea_pci_device, ha_handle_work);

	/* Ha interrupt is triggered, so there maybe some error, we may need to
	 * reset the device or reset some queues.
	 */
	dev_warn(&ep_dev->pci_dev->dev, "recv ha interrupt.\n");

	eea_ha_handle_reset(ep_dev);
}

static irqreturn_t eea_pci_ha_handle(int irq, void *data)
{
	struct eea_device *edev = data;

	schedule_work(&edev->ep_dev->ha_handle_work);

	return IRQ_HANDLED;
}

static void eea_pci_free_ha_irq(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;
	int irq;

	if (ep_dev->ha_ready) {
		irq = pci_irq_vector(ep_dev->pci_dev, 0);
		free_irq(irq, edev);
		ep_dev->ha_ready = false;
	}
}

static int eea_pci_ha_init(struct eea_device *edev, struct pci_dev *pci_dev,
			   bool pci_probe)
{
	int pos, cfg_type_off, cfg_drv_off, cfg_dev_off;
	struct eea_pci_device *ep_dev = edev->ep_dev;
	int irq, err;
	u8 type;

	snprintf(ep_dev->ha_irq_name, sizeof(ep_dev->ha_irq_name), "eea-ha@%s",
		 pci_name(ep_dev->pci_dev));

	irq = pci_irq_vector(ep_dev->pci_dev, 0);

	if (pci_probe)
		INIT_WORK(&ep_dev->ha_handle_work, eea_ha_handle_work);

	/* This irq is not only work for ha, so request it always. */
	err = request_irq(irq, eea_pci_ha_handle, IRQF_NO_AUTOEN,
			  ep_dev->ha_irq_name, edev);
	if (err)
		return err;

	ep_dev->ha_irq = irq;

	ep_dev->ha_ready = true;
	ep_dev->reset_pos = 0;

	cfg_type_off = offsetof(struct eea_pci_cap, cfg_type);
	cfg_drv_off = offsetof(struct eea_pci_reset_reg, driver);
	cfg_dev_off = offsetof(struct eea_pci_reset_reg, device);

	for (pos = pci_find_capability(pci_dev, PCI_CAP_ID_VNDR);
	     pos > 0;
	     pos = pci_find_next_capability(pci_dev, pos, PCI_CAP_ID_VNDR)) {
		pci_read_config_byte(pci_dev, pos + cfg_type_off, &type);

		if (type == EEA_PCI_CAP_RESET_DEVICE) {
			/* notify device, driver support this feature. */
			pci_write_config_word(pci_dev, pos + cfg_drv_off,
					      EEA_PCI_CAP_RESET_FLAG);
			pci_write_config_word(pci_dev, pos + cfg_dev_off,
					      0xFFFF);

			edev->ep_dev->reset_pos = pos + cfg_dev_off;
			return 0;
		}
	}

	/* irq just for event notify */
	dev_warn(&edev->ep_dev->pci_dev->dev, "Not Found reset cap.\n");
	return 0;
}

u64 eea_pci_device_ts(struct eea_device *edev)
{
	struct eea_pci_device *ep_dev = edev->ep_dev;

	return cfg_read64(ep_dev->reg, hw_ts);
}

static int eea_init_device(struct eea_device *edev)
{
	int err;

	err = eea_device_reset(edev);
	if (err)
		return err;

	eea_pci_io_set_status(edev, EEA_S_INIT);

	err = eea_negotiate(edev);
	if (err)
		goto err;

	err = eea_net_probe(edev);
	if (err)
		goto err;

	return 0;
err:
	eea_add_status(edev, EEA_S_FAILED);
	return err;
}

static int __eea_pci_probe(struct pci_dev *pci_dev,
			   struct eea_pci_device *ep_dev,
			   bool pci_probe)
{
	struct eea_device *edev;
	int err;

	pci_set_drvdata(pci_dev, ep_dev);

	edev = &ep_dev->edev;

	err = eea_pci_setup(pci_dev, ep_dev);
	if (err)
		return err;

	err = eea_init_device(&ep_dev->edev);
	if (err)
		goto err_pci_rel;

	err = eea_pci_ha_init(edev, pci_dev, pci_probe);
	if (err)
		goto err_net_rm;

	edev->status = EEA_PCI_STATUS_DONE;

	enable_irq(ep_dev->ha_irq);

	return 0;

err_net_rm:
	eea_net_remove(edev, !pci_probe);

err_pci_rel:
	eea_pci_release_resource(ep_dev);
	return err;
}

static void __eea_pci_remove(struct pci_dev *pci_dev, bool pci_remove)
{
	struct eea_pci_device *ep_dev = pci_get_drvdata(pci_dev);
	struct device *dev = get_device(&ep_dev->pci_dev->dev);
	struct eea_device *edev = &ep_dev->edev;

	eea_pci_free_ha_irq(edev);

	if (pci_remove)
		flush_work(&ep_dev->ha_handle_work);

	eea_net_remove(edev, !pci_remove);

	eea_pci_release_resource(ep_dev);

	put_device(dev);
}

static int eea_pci_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *id)
{
	struct eea_pci_device *ep_dev;
	struct eea_device *edev;
	int err;

	ep_dev = kzalloc(sizeof(*ep_dev), GFP_KERNEL);
	if (!ep_dev)
		return -ENOMEM;

	edev = &ep_dev->edev;

	edev->ep_dev = ep_dev;
	edev->dma_dev = &pci_dev->dev;

	ep_dev->pci_dev = pci_dev;

	mutex_init(&edev->ha_lock);

	err = __eea_pci_probe(pci_dev, ep_dev, true);
	if (err) {
		mutex_destroy(&edev->ha_lock);
		pci_set_drvdata(pci_dev, NULL);
		kfree(ep_dev);
	}

	return err;
}

static void eea_pci_remove(struct pci_dev *pci_dev)
{
	struct eea_pci_device *ep_dev = pci_get_drvdata(pci_dev);
	struct eea_device *edev;

	edev = &ep_dev->edev;

	mutex_lock(&edev->ha_lock);
	__eea_pci_remove(pci_dev, true);
	mutex_unlock(&edev->ha_lock);

	pci_set_drvdata(pci_dev, NULL);

	mutex_destroy(&edev->ha_lock);
	kfree(ep_dev);
}

static void eea_pci_shutdown(struct pci_dev *pci_dev)
{
	struct eea_pci_device *ep_dev = pci_get_drvdata(pci_dev);
	struct eea_device *edev;

	edev = &ep_dev->edev;

	ep_dev->shutdown = true;

	mutex_lock(&edev->ha_lock);
	eea_pci_free_ha_irq(edev);
	flush_work(&ep_dev->ha_handle_work);
	mutex_unlock(&edev->ha_lock);

	eea_net_shutdown(edev);

	pci_clear_master(pci_dev);
}

static const struct pci_device_id eea_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALIBABA, 0x500B) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, eea_pci_id_table);

static struct pci_driver eea_pci_driver = {
	.name            = "alibaba_eea",
	.id_table        = eea_pci_id_table,
	.probe           = eea_pci_probe,
	.remove          = eea_pci_remove,
	.shutdown        = eea_pci_shutdown,
	.sriov_configure = pci_sriov_configure_simple,
};

static __init int eea_pci_init(void)
{
	return pci_register_driver(&eea_pci_driver);
}

static __exit void eea_pci_exit(void)
{
	pci_unregister_driver(&eea_pci_driver);
}

module_init(eea_pci_init);
module_exit(eea_pci_exit);

MODULE_DESCRIPTION("Driver for Alibaba Elastic Ethernet Adapter");
MODULE_AUTHOR("Xuan Zhuo <xuanzhuo@linux.alibaba.com>");
MODULE_LICENSE("GPL");

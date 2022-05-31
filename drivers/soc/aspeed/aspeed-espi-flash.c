// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 ASPEED Technology Inc.
 */
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/miscdevice.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>

#include "aspeed-espi-ioc.h"
#include "aspeed-espi-ctrl.h"
#include "aspeed-espi-flash.h"

#define FLASH_MDEV_NAME	"aspeed-espi-flash"

static long aspeed_espi_flash_get_rx(struct file *fp,
				     struct aspeed_espi_ioc *ioc,
				     struct aspeed_espi_flash *espi_flash)
{
	int i, rc = 0;
	unsigned long flags;
	uint32_t reg;
	uint32_t cyc, tag, len;
	uint8_t *pkt;
	uint32_t pkt_len;
	struct espi_comm_hdr *hdr;
	struct aspeed_espi_ctrl *espi_ctrl = espi_flash->ctrl;

	if (fp->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&espi_flash->get_rx_mtx))
			return -EAGAIN;

		if (!espi_flash->rx_ready) {
			rc = -ENODATA;
			goto unlock_mtx_n_out;
		}
	} else {
		mutex_lock(&espi_flash->get_rx_mtx);

		if (!espi_flash->rx_ready) {
			rc = wait_event_interruptible(espi_flash->wq,
						      espi_flash->rx_ready);
			if (rc == -ERESTARTSYS) {
				rc = -EINTR;
				goto unlock_mtx_n_out;
			}
		}
	}

	/* common header (i.e. cycle type, tag, and length) is taken by HW */
	regmap_read(espi_ctrl->map, ESPI_FLASH_RX_CTRL, &reg);
	cyc = (reg & ESPI_FLASH_RX_CTRL_CYC_MASK) >> ESPI_FLASH_RX_CTRL_CYC_SHIFT;
	tag = (reg & ESPI_FLASH_RX_CTRL_TAG_MASK) >> ESPI_FLASH_RX_CTRL_TAG_SHIFT;
	len = (reg & ESPI_FLASH_RX_CTRL_LEN_MASK) >> ESPI_FLASH_RX_CTRL_LEN_SHIFT;

	/*
	 * calculate the length of the rest part of the
	 * eSPI packet to be read from HW and copied to
	 * user space.
	 */
	switch (cyc) {
	case ESPI_FLASH_WRITE:
		pkt_len = ((len) ? len : ESPI_PLD_LEN_MAX) +
			  sizeof(struct espi_flash_rwe);
		break;
	case ESPI_FLASH_READ:
	case ESPI_FLASH_ERASE:
		pkt_len = sizeof(struct espi_flash_rwe);
		break;
	case ESPI_FLASH_SUC_CMPLT_D_MIDDLE:
	case ESPI_FLASH_SUC_CMPLT_D_FIRST:
	case ESPI_FLASH_SUC_CMPLT_D_LAST:
	case ESPI_FLASH_SUC_CMPLT_D_ONLY:
		pkt_len = ((len) ? len : ESPI_PLD_LEN_MAX) +
			  sizeof(struct espi_flash_cmplt);
		break;
	case ESPI_FLASH_SUC_CMPLT:
	case ESPI_FLASH_UNSUC_CMPLT:
		pkt_len = sizeof(struct espi_flash_cmplt);
		break;
	default:
		rc = -EFAULT;
		goto unlock_mtx_n_out;
	}

	if (ioc->pkt_len < pkt_len) {
		rc = -EINVAL;
		goto unlock_mtx_n_out;
	}

	pkt = vmalloc(pkt_len);
	if (!pkt) {
		rc = -ENOMEM;
		goto unlock_mtx_n_out;
	}

	hdr = (struct espi_comm_hdr *)pkt;
	hdr->cyc = cyc;
	hdr->tag = tag;
	hdr->len_h = len >> 8;
	hdr->len_l = len & 0xff;

	if (espi_flash->dma_mode) {
		memcpy(hdr + 1, espi_flash->dma.rx_virt,
		       pkt_len - sizeof(*hdr));
	} else {
		for (i = sizeof(*hdr); i < pkt_len; ++i) {
			regmap_read(espi_ctrl->map,
				    ESPI_FLASH_RX_PORT, &reg);
			pkt[i] = reg & 0xff;
		}
	}

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&espi_flash->lock, flags);

	regmap_write_bits(espi_ctrl->map, ESPI_FLASH_RX_CTRL,
			  ESPI_FLASH_RX_CTRL_PEND_SERV,
			  ESPI_FLASH_RX_CTRL_PEND_SERV);

	espi_flash->rx_ready = 0;

	spin_unlock_irqrestore(&espi_flash->lock, flags);

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&espi_flash->get_rx_mtx);

	return rc;
}

static long aspeed_espi_flash_put_tx(struct file *fp,
				     struct aspeed_espi_ioc *ioc,
				     struct aspeed_espi_flash *espi_flash)
{
	int i, rc = 0;
	uint32_t reg;
	uint32_t cyc, tag, len;
	uint8_t *pkt;
	struct espi_comm_hdr *hdr;
	struct aspeed_espi_ctrl *espi_ctrl = espi_flash->ctrl;

	if (!mutex_trylock(&espi_flash->put_tx_mtx))
		return -EAGAIN;

	regmap_read(espi_ctrl->map, ESPI_FLASH_TX_CTRL, &reg);
	if (reg & ESPI_FLASH_TX_CTRL_TRIGGER) {
		rc = -EBUSY;
		goto unlock_mtx_n_out;
	}

	pkt = vmalloc(ioc->pkt_len);
	if (!pkt) {
		rc = -ENOMEM;
		goto unlock_mtx_n_out;
	}

	hdr = (struct espi_comm_hdr *)pkt;

	if (copy_from_user(pkt, (void __user *)ioc->pkt, ioc->pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	/*
	 * common header (i.e. cycle type, tag, and length)
	 * part is written to HW registers
	 */
	if (espi_flash->dma_mode) {
		memcpy(espi_flash->dma.tx_virt, hdr + 1,
		       ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			regmap_write(espi_ctrl->map,
				     ESPI_FLASH_TX_PORT, pkt[i]);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = ((cyc << ESPI_FLASH_TX_CTRL_CYC_SHIFT) & ESPI_FLASH_TX_CTRL_CYC_MASK)
		| ((tag << ESPI_FLASH_TX_CTRL_TAG_SHIFT) & ESPI_FLASH_TX_CTRL_TAG_MASK)
		| ((len << ESPI_FLASH_TX_CTRL_LEN_SHIFT) & ESPI_FLASH_TX_CTRL_LEN_MASK)
		| ESPI_FLASH_TX_CTRL_TRIGGER;

	regmap_write(espi_ctrl->map, ESPI_FLASH_TX_CTRL, reg);

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&espi_flash->put_tx_mtx);

	return rc;
}

static long aspeed_espi_flash_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct aspeed_espi_ioc ioc;
	struct aspeed_espi_flash *espi_flash = container_of(
			fp->private_data,
			struct aspeed_espi_flash,
			mdev);

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.pkt_len > ESPI_PKT_LEN_MAX)
		return -EINVAL;

	switch (cmd) {
	case ASPEED_ESPI_FLASH_GET_RX:
		return aspeed_espi_flash_get_rx(fp, &ioc, espi_flash);
	case ASPEED_ESPI_FLASH_PUT_TX:
		return aspeed_espi_flash_put_tx(fp, &ioc, espi_flash);
	};

	return -EINVAL;
}

void aspeed_espi_flash_event(uint32_t sts, struct aspeed_espi_flash *espi_flash)
{
	unsigned long flags;

	if (sts & ESPI_INT_STS_FLASH_RX_CMPLT) {
		spin_lock_irqsave(&espi_flash->lock, flags);
		espi_flash->rx_ready = 1;
		spin_unlock_irqrestore(&espi_flash->lock, flags);
		wake_up_interruptible(&espi_flash->wq);
	}
}

void aspeed_espi_flash_enable(struct aspeed_espi_flash *espi_flash)
{
	struct aspeed_espi_flash_dma *dma = &espi_flash->dma;
	struct aspeed_espi_ctrl *espi_ctrl = espi_flash->ctrl;

	regmap_update_bits(espi_ctrl->map, ESPI_CTRL,
			   ESPI_CTRL_FLASH_SW_MODE_MASK,
			   (espi_flash->safs_mode << ESPI_CTRL_FLASH_SW_MODE_SHIFT));

	if (espi_flash->dma_mode) {
		regmap_write(espi_ctrl->map, ESPI_FLASH_TX_DMA, dma->tx_addr);
		regmap_write(espi_ctrl->map, ESPI_FLASH_RX_DMA, dma->rx_addr);
		regmap_update_bits(espi_ctrl->map, ESPI_CTRL,
				   ESPI_CTRL_FLASH_TX_DMA_EN | ESPI_CTRL_FLASH_RX_DMA_EN,
				   ESPI_CTRL_FLASH_TX_DMA_EN | ESPI_CTRL_FLASH_RX_DMA_EN);
	}

	regmap_write(espi_ctrl->map, ESPI_INT_STS,
		     ESPI_INT_STS_FLASH_BITS);

	regmap_update_bits(espi_ctrl->map, ESPI_INT_EN,
			   ESPI_INT_EN_FLASH_BITS,
			   ESPI_INT_EN_FLASH_BITS);

	regmap_update_bits(espi_ctrl->map, ESPI_CTRL,
			   ESPI_CTRL_FLASH_SW_RDY,
			   ESPI_CTRL_FLASH_SW_RDY);
}

static const struct file_operations aspeed_espi_flash_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = aspeed_espi_flash_ioctl,
};

void *aspeed_espi_flash_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl)
{
	int rc = 0;
	struct aspeed_espi_flash *espi_flash;
	struct aspeed_espi_flash_dma *dma;

	espi_flash = devm_kzalloc(dev, sizeof(*espi_flash), GFP_KERNEL);
	if (!espi_flash)
		return ERR_PTR(-ENOMEM);

	espi_flash->ctrl = espi_ctrl;
	espi_flash->safs_mode = SAFS_MODE_HW;

	init_waitqueue_head(&espi_flash->wq);

	spin_lock_init(&espi_flash->lock);

	mutex_init(&espi_flash->put_tx_mtx);
	mutex_init(&espi_flash->get_rx_mtx);

	if (of_property_read_bool(dev->of_node, "flash,dma-mode"))
		espi_flash->dma_mode = 1;

	of_property_read_u32(dev->of_node, "flash,safs-mode", &espi_flash->safs_mode);
	if (espi_flash->safs_mode >= SAFS_MODES) {
		dev_err(dev, "invalid SAFS mode\n");
		return ERR_PTR(-EINVAL);
	}

	if (espi_flash->dma_mode) {
		dma = &espi_flash->dma;

		dma->tx_virt = dma_alloc_coherent(dev, PAGE_SIZE,
						  &dma->tx_addr, GFP_KERNEL);
		if (!dma->tx_virt) {
			dev_err(dev, "cannot allocate DMA TX buffer\n");
			return ERR_PTR(-ENOMEM);
		}

		dma->rx_virt = dma_alloc_coherent(dev, PAGE_SIZE,
						  &dma->rx_addr, GFP_KERNEL);
		if (!dma->rx_virt) {
			dev_err(dev, "cannot allocate DMA RX buffer\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	espi_flash->mdev.parent = dev;
	espi_flash->mdev.minor = MISC_DYNAMIC_MINOR;
	espi_flash->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s", FLASH_MDEV_NAME);
	espi_flash->mdev.fops = &aspeed_espi_flash_fops;
	rc = misc_register(&espi_flash->mdev);
	if (rc) {
		dev_err(dev, "cannot register device\n");
		return ERR_PTR(rc);
	}

	aspeed_espi_flash_enable(espi_flash);

	return espi_flash;
}

void aspeed_espi_flash_free(struct device *dev, struct aspeed_espi_flash *espi_flash)
{
	struct aspeed_espi_flash_dma *dma = &espi_flash->dma;

	if (espi_flash->dma_mode) {
		dma_free_coherent(dev, PAGE_SIZE, dma->tx_virt, dma->tx_addr);
		dma_free_coherent(dev, PAGE_SIZE, dma->rx_virt, dma->rx_addr);
	}

	mutex_destroy(&espi_flash->put_tx_mtx);
	mutex_destroy(&espi_flash->get_rx_mtx);

	misc_deregister(&espi_flash->mdev);
}

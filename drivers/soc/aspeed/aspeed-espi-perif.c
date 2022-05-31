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
#include "aspeed-espi-perif.h"

#define PERIF_MDEV_NAME		"aspeed-espi-peripheral"
#define PERIF_MEMCYC_UNLOCK_KEY	0xfedc756e
#define PERIF_MEMCYC_SIZE_MIN	0x10000

static long aspeed_espi_perif_pc_get_rx(struct file *fp,
					struct aspeed_espi_ioc *ioc,
					struct aspeed_espi_perif *espi_perif)
{
	int i, rc = 0;
	uint32_t reg;
	uint32_t cyc, tag, len;
	uint8_t *pkt;
	uint32_t pkt_len;
	struct espi_comm_hdr *hdr;
	unsigned long flags;
	struct aspeed_espi_ctrl *espi_ctrl = espi_perif->ctrl;

	if (fp->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&espi_perif->pc_rx_mtx))
			return -EAGAIN;

		if (!espi_perif->rx_ready) {
			rc = -ENODATA;
			goto unlock_mtx_n_out;
		}
	} else {
		mutex_lock(&espi_perif->pc_rx_mtx);

		if (!espi_perif->rx_ready) {
			rc = wait_event_interruptible(espi_perif->wq,
						      espi_perif->rx_ready);
			if (rc == -ERESTARTSYS) {
				rc = -EINTR;
				goto unlock_mtx_n_out;
			}
		}
	}

	/* common header (i.e. cycle type, tag, and length) is taken by HW */
	regmap_read(espi_ctrl->map, ESPI_PERIF_PC_RX_CTRL, &reg);
	cyc = (reg & ESPI_PERIF_PC_RX_CTRL_CYC_MASK) >> ESPI_PERIF_PC_RX_CTRL_CYC_SHIFT;
	tag = (reg & ESPI_PERIF_PC_RX_CTRL_TAG_MASK) >> ESPI_PERIF_PC_RX_CTRL_TAG_SHIFT;
	len = (reg & ESPI_PERIF_PC_RX_CTRL_LEN_MASK) >> ESPI_PERIF_PC_RX_CTRL_LEN_SHIFT;

	/*
	 * calculate the length of the rest part of the
	 * eSPI packet to be read from HW and copied to
	 * user space.
	 */
	switch (cyc) {
	case ESPI_PERIF_MSG:
		pkt_len = sizeof(struct espi_perif_msg);
		break;
	case ESPI_PERIF_MSG_D:
		pkt_len = ((len) ? len : ESPI_PLD_LEN_MAX) +
			  sizeof(struct espi_perif_msg);
		break;
	case ESPI_PERIF_SUC_CMPLT_D_MIDDLE:
	case ESPI_PERIF_SUC_CMPLT_D_FIRST:
	case ESPI_PERIF_SUC_CMPLT_D_LAST:
	case ESPI_PERIF_SUC_CMPLT_D_ONLY:
		pkt_len = ((len) ? len : ESPI_PLD_LEN_MAX) +
			  sizeof(struct espi_perif_cmplt);
		break;
	case ESPI_PERIF_SUC_CMPLT:
	case ESPI_PERIF_UNSUC_CMPLT:
		pkt_len = sizeof(struct espi_perif_cmplt);
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

	if (espi_perif->dma_mode) {
		memcpy(hdr + 1, espi_perif->dma.pc_rx_virt,
		       pkt_len - sizeof(*hdr));
	} else {
		for (i = sizeof(*hdr); i < pkt_len; ++i) {
			regmap_read(espi_ctrl->map,
				    ESPI_PERIF_PC_RX_PORT, &reg);
			pkt[i] = reg & 0xff;
		}
	}

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&espi_perif->lock, flags);

	regmap_write_bits(espi_ctrl->map, ESPI_PERIF_PC_RX_CTRL,
			  ESPI_PERIF_PC_RX_CTRL_PEND_SERV,
			  ESPI_PERIF_PC_RX_CTRL_PEND_SERV);

	espi_perif->rx_ready = 0;

	spin_unlock_irqrestore(&espi_perif->lock, flags);

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&espi_perif->pc_rx_mtx);

	return rc;
}

static long aspeed_espi_perif_pc_put_tx(struct file *fp,
					struct aspeed_espi_ioc *ioc,
					struct aspeed_espi_perif *espi_perif)
{
	int i, rc = 0;
	uint32_t reg;
	uint32_t cyc, tag, len;
	uint8_t *pkt;
	struct espi_comm_hdr *hdr;
	struct aspeed_espi_ctrl *espi_ctrl = espi_perif->ctrl;

	if (!mutex_trylock(&espi_perif->pc_tx_mtx))
		return -EAGAIN;

	regmap_read(espi_ctrl->map, ESPI_PERIF_PC_TX_CTRL, &reg);
	if (reg & ESPI_PERIF_PC_TX_CTRL_TRIGGER) {
		rc = -EBUSY;
		goto unlock_n_out;
	}

	pkt = vmalloc(ioc->pkt_len);
	if (!pkt) {
		rc = -ENOMEM;
		goto unlock_n_out;
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
	if (espi_perif->dma_mode) {
		memcpy(espi_perif->dma.pc_tx_virt, hdr + 1,
		       ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			regmap_write(espi_ctrl->map,
				     ESPI_PERIF_PC_TX_PORT, pkt[i]);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = ((cyc << ESPI_PERIF_PC_TX_CTRL_CYC_SHIFT) & ESPI_PERIF_PC_TX_CTRL_CYC_MASK)
		| ((tag << ESPI_PERIF_PC_TX_CTRL_TAG_SHIFT) & ESPI_PERIF_PC_TX_CTRL_TAG_MASK)
		| ((len << ESPI_PERIF_PC_TX_CTRL_LEN_SHIFT) & ESPI_PERIF_PC_TX_CTRL_LEN_MASK)
		| ESPI_PERIF_PC_TX_CTRL_TRIGGER;

	regmap_write(espi_ctrl->map, ESPI_PERIF_PC_TX_CTRL, reg);

free_n_out:
	vfree(pkt);

unlock_n_out:
	mutex_unlock(&espi_perif->pc_tx_mtx);

	return rc;
}

static long aspeed_espi_perif_np_put_tx(struct file *fp,
					struct aspeed_espi_ioc *ioc,
					struct aspeed_espi_perif *espi_perif)
{
	int i, rc = 0;
	uint32_t reg;
	uint32_t cyc, tag, len;
	uint8_t *pkt;
	struct espi_comm_hdr *hdr;
	struct aspeed_espi_ctrl *espi_ctrl = espi_perif->ctrl;

	if (!mutex_trylock(&espi_perif->np_tx_mtx))
		return -EAGAIN;

	regmap_read(espi_ctrl->map, ESPI_PERIF_NP_TX_CTRL, &reg);
	if (reg & ESPI_PERIF_NP_TX_CTRL_TRIGGER) {
		rc = -EBUSY;
		goto unlock_n_out;
	}

	pkt = vmalloc(ioc->pkt_len);
	if (!pkt) {
		rc = -ENOMEM;
		goto unlock_n_out;
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
	if (espi_perif->dma_mode) {
		memcpy(espi_perif->dma.np_tx_virt, hdr + 1,
		       ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			regmap_write(espi_ctrl->map,
				     ESPI_PERIF_NP_TX_PORT, pkt[i]);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = ((cyc << ESPI_PERIF_NP_TX_CTRL_CYC_SHIFT) & ESPI_PERIF_NP_TX_CTRL_CYC_MASK)
		| ((tag << ESPI_PERIF_NP_TX_CTRL_TAG_SHIFT) & ESPI_PERIF_NP_TX_CTRL_TAG_MASK)
		| ((len << ESPI_PERIF_NP_TX_CTRL_LEN_SHIFT) & ESPI_PERIF_NP_TX_CTRL_LEN_MASK)
		| ESPI_PERIF_NP_TX_CTRL_TRIGGER;

	regmap_write(espi_ctrl->map, ESPI_PERIF_NP_TX_CTRL, reg);

free_n_out:
	vfree(pkt);

unlock_n_out:
	mutex_unlock(&espi_perif->np_tx_mtx);

	return rc;

}

static long aspeed_espi_perif_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct aspeed_espi_ioc ioc;
	struct aspeed_espi_perif *espi_perif = container_of(
			fp->private_data,
			struct aspeed_espi_perif,
			mdev);

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.pkt_len > ESPI_PKT_LEN_MAX)
		return -EINVAL;

	switch (cmd) {
	case ASPEED_ESPI_PERIF_PC_GET_RX:
		return aspeed_espi_perif_pc_get_rx(fp, &ioc, espi_perif);
	case ASPEED_ESPI_PERIF_PC_PUT_TX:
		return aspeed_espi_perif_pc_put_tx(fp, &ioc, espi_perif);
	case ASPEED_ESPI_PERIF_NP_PUT_TX:
		return aspeed_espi_perif_np_put_tx(fp, &ioc, espi_perif);
	};

	return -EINVAL;
}

static int aspeed_espi_perif_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct aspeed_espi_perif *espi_perif = container_of(
			fp->private_data,
			struct aspeed_espi_perif,
			mdev);
	unsigned long vm_size = vma->vm_end - vma->vm_start;
	pgprot_t prot = vma->vm_page_prot;

	if (!espi_perif->mcyc_enable)
		return -EPERM;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > espi_perif->mcyc_size)
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (espi_perif->mcyc_taddr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, prot))
		return -EAGAIN;

	return 0;
}

void aspeed_espi_perif_event(uint32_t sts, struct aspeed_espi_perif *espi_perif)
{
	unsigned long flags;

	if (sts & ESPI_INT_STS_PERIF_PC_RX_CMPLT) {
		spin_lock_irqsave(&espi_perif->lock, flags);
		espi_perif->rx_ready = 1;
		spin_unlock_irqrestore(&espi_perif->lock, flags);

		wake_up_interruptible(&espi_perif->wq);
	}
}

void aspeed_espi_perif_enable(struct aspeed_espi_perif *espi_perif)
{
	struct aspeed_espi_perif_dma *dma = &espi_perif->dma;
	struct aspeed_espi_ctrl *espi_ctrl = espi_perif->ctrl;

	if (espi_perif->mcyc_enable) {
		if (espi_ctrl->model->version == ESPI_AST2500) {
			regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_MASK,
				     PERIF_MEMCYC_UNLOCK_KEY);
			regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_MASK,
				     espi_perif->mcyc_mask);
		} else {
			regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_MASK,
				     espi_perif->mcyc_mask | ESPI_PERIF_PC_RX_MASK_CFG_WP);
			regmap_update_bits(espi_ctrl->map, ESPI_CTRL2,
					   ESPI_CTRL2_MEMCYC_RD_DIS | ESPI_CTRL2_MEMCYC_WR_DIS, 0);
		}

		regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_SADDR, espi_perif->mcyc_saddr);
		regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_TADDR, espi_perif->mcyc_taddr);
	}

	if (espi_perif->dma_mode) {
		regmap_write(espi_ctrl->map, ESPI_PERIF_PC_RX_DMA, dma->pc_rx_addr);
		regmap_write(espi_ctrl->map, ESPI_PERIF_PC_TX_DMA, dma->pc_tx_addr);
		regmap_write(espi_ctrl->map, ESPI_PERIF_NP_TX_DMA, dma->np_tx_addr);

		regmap_update_bits(espi_ctrl->map, ESPI_CTRL,
				   ESPI_CTRL_PERIF_NP_TX_DMA_EN |
				   ESPI_CTRL_PERIF_PC_TX_DMA_EN |
				   ESPI_CTRL_PERIF_PC_RX_DMA_EN,
				   ESPI_CTRL_PERIF_NP_TX_DMA_EN |
				   ESPI_CTRL_PERIF_PC_TX_DMA_EN |
				   ESPI_CTRL_PERIF_PC_RX_DMA_EN);
	}

	regmap_write(espi_ctrl->map, ESPI_INT_STS,
		     ESPI_INT_STS_PERIF_BITS);

	regmap_update_bits(espi_ctrl->map, ESPI_INT_EN,
			   ESPI_INT_EN_PERIF_BITS,
			   ESPI_INT_EN_PERIF_BITS);

	regmap_update_bits(espi_ctrl->map, ESPI_CTRL,
			   ESPI_CTRL_PERIF_SW_RDY,
			   ESPI_CTRL_PERIF_SW_RDY);
}

static const struct file_operations aspeed_espi_perif_fops = {
	.owner = THIS_MODULE,
	.mmap = aspeed_espi_perif_mmap,
	.unlocked_ioctl = aspeed_espi_perif_ioctl,
};

void *aspeed_espi_perif_alloc(struct device *dev, struct aspeed_espi_ctrl *espi_ctrl)
{
	int rc;
	struct aspeed_espi_perif *espi_perif;
	struct aspeed_espi_perif_dma *dma;

	espi_perif = devm_kzalloc(dev, sizeof(*espi_perif), GFP_KERNEL);
	if (!espi_perif)
		return ERR_PTR(-ENOMEM);

	espi_perif->ctrl = espi_ctrl;

	init_waitqueue_head(&espi_perif->wq);

	spin_lock_init(&espi_perif->lock);

	mutex_init(&espi_perif->pc_rx_mtx);
	mutex_init(&espi_perif->pc_tx_mtx);
	mutex_init(&espi_perif->np_tx_mtx);

	espi_perif->mcyc_enable = of_property_read_bool(dev->of_node, "perif,memcyc-enable");

	do {
		if (!espi_perif->mcyc_enable)
			break;

		if (of_device_is_available(of_parse_phandle(dev->of_node, "aspeed,espi-mmbi", 0))) {
			dev_warn(dev, "memory cycle is occupied by MMBI\n");
			break;
		}

		rc = of_property_read_u32(dev->of_node, "perif,memcyc-src-addr",
					  &espi_perif->mcyc_saddr);
		if (rc) {
			dev_err(dev, "cannot get Host source address for memory cycle\n");
			return ERR_PTR(-ENODEV);
		}

		rc = of_property_read_u32(dev->of_node, "perif,memcyc-size",
					  &espi_perif->mcyc_size);
		if (rc) {
			dev_err(dev, "cannot get size for memory cycle\n");
			return ERR_PTR(-ENODEV);
		}

		if (espi_perif->mcyc_size < PERIF_MEMCYC_SIZE_MIN)
			espi_perif->mcyc_size = PERIF_MEMCYC_SIZE_MIN;
		else
			espi_perif->mcyc_size = roundup_pow_of_two(espi_perif->mcyc_size);

		espi_perif->mcyc_mask = ~(espi_perif->mcyc_size - 1);
		espi_perif->mcyc_virt = dma_alloc_coherent(dev, espi_perif->mcyc_size,
							   &espi_perif->mcyc_taddr, GFP_KERNEL);
		if (!espi_perif->mcyc_virt) {
			dev_err(dev, "cannot allocate memory cycle region\n");
			return ERR_PTR(-ENOMEM);
		}
	} while (0);

	if (of_property_read_bool(dev->of_node, "perif,dma-mode")) {
		dma = &espi_perif->dma;

		dma->pc_tx_virt = dma_alloc_coherent(dev, PAGE_SIZE,
						     &dma->pc_tx_addr, GFP_KERNEL);
		if (!dma->pc_tx_virt) {
			dev_err(dev, "cannot allocate posted TX DMA buffer\n");
			return ERR_PTR(-ENOMEM);
		}

		dma->pc_rx_virt = dma_alloc_coherent(dev, PAGE_SIZE,
						     &dma->pc_rx_addr, GFP_KERNEL);
		if (!dma->pc_rx_virt) {
			dev_err(dev, "cannot allocate posted RX DMA buffer\n");
			return ERR_PTR(-ENOMEM);
		}

		dma->np_tx_virt = dma_alloc_coherent(dev, PAGE_SIZE,
				&dma->np_tx_addr, GFP_KERNEL);
		if (!dma->np_tx_virt) {
			dev_err(dev, "cannot allocate non-posted TX DMA buffer\n");
			return ERR_PTR(-ENOMEM);
		}

		espi_perif->dma_mode = 1;
	}

	espi_perif->mdev.parent = dev;
	espi_perif->mdev.minor = MISC_DYNAMIC_MINOR;
	espi_perif->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s", PERIF_MDEV_NAME);
	espi_perif->mdev.fops = &aspeed_espi_perif_fops;
	rc = misc_register(&espi_perif->mdev);
	if (rc) {
		dev_err(dev, "cannot register device\n");
		return ERR_PTR(rc);
	}

	aspeed_espi_perif_enable(espi_perif);

	return espi_perif;
}

void aspeed_espi_perif_free(struct device *dev, struct aspeed_espi_perif *espi_perif)
{
	struct aspeed_espi_perif_dma *dma = &espi_perif->dma;

	if (espi_perif->mcyc_virt)
		dma_free_coherent(dev, espi_perif->mcyc_size,
				  espi_perif->mcyc_virt,
				  espi_perif->mcyc_taddr);

	if (espi_perif->dma_mode) {
		dma_free_coherent(dev, PAGE_SIZE, dma->pc_tx_virt,
				  dma->pc_tx_addr);
		dma_free_coherent(dev, PAGE_SIZE, dma->pc_rx_virt,
				  dma->pc_rx_addr);
		dma_free_coherent(dev, PAGE_SIZE, dma->np_tx_virt,
				  dma->np_tx_addr);
	}

	mutex_destroy(&espi_perif->pc_tx_mtx);
	mutex_destroy(&espi_perif->np_tx_mtx);

	misc_deregister(&espi_perif->mdev);
}

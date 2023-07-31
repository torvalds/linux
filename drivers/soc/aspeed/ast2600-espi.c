// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>
#include <linux/delay.h>

#include "ast2600-espi.h"

#define DEVICE_NAME		"aspeed-espi"

#define PERIF_MCYC_ALIGN	SZ_64K
#define PERIF_MMBI_ALIGN	SZ_64K
#define PERIF_MMBI_INST_NUM	8

#define OOB_DMA_DESC_NUM	8
#define OOB_DMA_DESC_CUSTOM	0x4

#define FLASH_SAFS_ALIGN	SZ_16M

struct ast2600_espi_perif_mmbi {
	void *b2h_virt;
	void *h2b_virt;
	dma_addr_t b2h_addr;
	dma_addr_t h2b_addr;
	struct miscdevice b2h_mdev;
	struct miscdevice h2b_mdev;
	bool host_rwp_update;
	wait_queue_head_t wq;
	struct ast2600_espi_perif *perif;
};

struct ast2600_espi_perif {
	struct {
		bool enable;
		int irq;
		void *virt;
		dma_addr_t taddr;
		uint32_t saddr;
		uint32_t size;
		uint32_t inst_size;
		struct ast2600_espi_perif_mmbi inst[PERIF_MMBI_INST_NUM];
	} mmbi;

	struct {
		bool enable;
		void *virt;
		dma_addr_t taddr;
		uint32_t saddr;
		uint32_t size;
	} mcyc;

	struct {
		bool enable;
		void *np_tx_virt;
		dma_addr_t np_tx_addr;
		void *pc_tx_virt;
		dma_addr_t pc_tx_addr;
		void *pc_rx_virt;
		dma_addr_t pc_rx_addr;
	} dma;

	bool rx_ready;
	wait_queue_head_t wq;

	spinlock_t lock;
	struct mutex np_tx_mtx;
	struct mutex pc_tx_mtx;
	struct mutex pc_rx_mtx;

	struct miscdevice mdev;
};

struct ast2600_espi_vw {
	struct {
		bool hw_mode;
		uint32_t grp;
		uint32_t dir;
		uint32_t val;
	} gpio;

	struct miscdevice mdev;
};

struct ast2600_espi_oob_dma_tx_desc {
	uint32_t data_addr;
	uint8_t cyc;
	uint16_t tag : 4;
	uint16_t len : 12;
	uint8_t msg_type : 3;
	uint8_t raz0 : 1;
	uint8_t pec : 1;
	uint8_t int_en : 1;
	uint8_t pause : 1;
	uint8_t raz1 : 1;
	uint32_t raz2;
	uint32_t raz3;
} __packed;

struct ast2600_espi_oob_dma_rx_desc {
	uint32_t data_addr;
	uint8_t cyc;
	uint16_t tag : 4;
	uint16_t len : 12;
	uint8_t raz : 7;
	uint8_t dirty : 1;
} __packed;

struct ast2600_espi_oob {
	struct {
		bool enable;
		struct ast2600_espi_oob_dma_tx_desc *txd_virt;
		dma_addr_t txd_addr;
		struct ast2600_espi_oob_dma_rx_desc *rxd_virt;
		dma_addr_t rxd_addr;
		void *tx_virt;
		dma_addr_t tx_addr;
		void *rx_virt;
		dma_addr_t rx_addr;
	} dma;

	bool rx_ready;
	wait_queue_head_t wq;

	spinlock_t lock;
	struct mutex tx_mtx;
	struct mutex rx_mtx;

	struct miscdevice mdev;
};

struct ast2600_espi_flash {
	struct {
		uint32_t mode;
		phys_addr_t taddr;
		uint32_t size;
	} safs;

	struct {
		bool enable;
		void *tx_virt;
		dma_addr_t tx_addr;
		void *rx_virt;
		dma_addr_t rx_addr;
	} dma;

	bool rx_ready;
	wait_queue_head_t wq;

	spinlock_t lock;
	struct mutex rx_mtx;
	struct mutex tx_mtx;

	struct miscdevice mdev;
};

struct ast2600_espi {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	int irq;

	struct ast2600_espi_perif perif;
	struct ast2600_espi_vw vw;
	struct ast2600_espi_oob oob;
	struct ast2600_espi_flash flash;
};

/* peripheral channel (CH0) */
static int ast2600_espi_mmbi_b2h_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct ast2600_espi_perif_mmbi *mmbi;
	struct ast2600_espi_perif *perif;
	struct ast2600_espi *espi;
	unsigned long vm_size;
	pgprot_t prot;

	mmbi = container_of(fp->private_data, struct ast2600_espi_perif_mmbi, b2h_mdev);

	perif = mmbi->perif;

	espi = container_of(perif, struct ast2600_espi, perif);

	vm_size = vma->vm_end - vma->vm_start;
	prot = vma->vm_page_prot;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > (SZ_4K << perif->mmbi.inst_size))
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (mmbi->b2h_addr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, prot))
		return -EAGAIN;

	return 0;
}

static int ast2600_espi_mmbi_h2b_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct ast2600_espi_perif_mmbi *mmbi;
	struct ast2600_espi_perif *perif;
	struct ast2600_espi *espi;
	unsigned long vm_size;
	pgprot_t prot;

	mmbi = container_of(fp->private_data, struct ast2600_espi_perif_mmbi, h2b_mdev);

	perif = mmbi->perif;

	espi = container_of(perif, struct ast2600_espi, perif);

	vm_size = vma->vm_end - vma->vm_start;
	prot = vma->vm_page_prot;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > (SZ_4K << perif->mmbi.inst_size))
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (mmbi->h2b_addr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, prot))
		return -EAGAIN;

	return 0;
}

static __poll_t ast2600_espi_mmbi_h2b_poll(struct file *fp, struct poll_table_struct *pt)
{
	struct ast2600_espi_perif_mmbi *mmbi;

	mmbi = container_of(fp->private_data, struct ast2600_espi_perif_mmbi, h2b_mdev);

	poll_wait(fp, &mmbi->wq, pt);

	if (!mmbi->host_rwp_update)
		return 0;

	mmbi->host_rwp_update = false;

	return EPOLLIN;
}

static long ast2600_espi_perif_pc_get_rx(struct file *fp,
					 struct ast2600_espi_perif *perif,
					 struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	unsigned long flags;
	uint32_t pkt_len;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(perif, struct ast2600_espi, perif);

	if (fp->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&perif->pc_rx_mtx))
			return -EAGAIN;

		if (!perif->rx_ready) {
			rc = -ENODATA;
			goto unlock_mtx_n_out;
		}
	} else {
		mutex_lock(&perif->pc_rx_mtx);

		if (!perif->rx_ready) {
			rc = wait_event_interruptible(perif->wq, perif->rx_ready);
			if (rc == -ERESTARTSYS) {
				rc = -EINTR;
				goto unlock_mtx_n_out;
			}
		}
	}

	/*
	 * common header (i.e. cycle type, tag, and length)
	 * part is written to HW registers
	 */
	reg = readl(espi->regs + ESPI_PERIF_PC_RX_CTRL);
	cyc = FIELD_GET(ESPI_PERIF_PC_RX_CTRL_CYC, reg);
	tag = FIELD_GET(ESPI_PERIF_PC_RX_CTRL_TAG, reg);
	len = FIELD_GET(ESPI_PERIF_PC_RX_CTRL_LEN, reg);

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
		pkt_len = ((len) ? len : ESPI_MAX_PLD_LEN) +
			  sizeof(struct espi_perif_msg);
		break;
	case ESPI_PERIF_SUC_CMPLT_D_MIDDLE:
	case ESPI_PERIF_SUC_CMPLT_D_FIRST:
	case ESPI_PERIF_SUC_CMPLT_D_LAST:
	case ESPI_PERIF_SUC_CMPLT_D_ONLY:
		pkt_len = ((len) ? len : ESPI_MAX_PLD_LEN) +
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

	if (perif->dma.enable) {
		memcpy(hdr + 1, perif->dma.pc_rx_virt, pkt_len - sizeof(*hdr));
	} else {
		for (i = sizeof(*hdr); i < pkt_len; ++i)
			reg = readl(espi->regs + ESPI_PERIF_PC_RX_DATA) & 0xff;
	}

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&perif->lock, flags);

	writel(ESPI_PERIF_PC_RX_CTRL_SERV_PEND, espi->regs + ESPI_PERIF_PC_RX_CTRL);
	perif->rx_ready = 0;

	spin_unlock_irqrestore(&perif->lock, flags);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&perif->pc_rx_mtx);

	return rc;
}

static long ast2600_espi_perif_pc_put_tx(struct file *fp,
					 struct ast2600_espi_perif *perif,
					 struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(perif, struct ast2600_espi, perif);

	if (!mutex_trylock(&perif->pc_tx_mtx))
		return -EAGAIN;

	reg = readl(espi->regs + ESPI_PERIF_PC_TX_CTRL);
	if (reg & ESPI_PERIF_PC_TX_CTRL_TRIG_PEND) {
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
	if (perif->dma.enable) {
		memcpy(perif->dma.pc_tx_virt, hdr + 1, ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			writel(pkt[i], espi->regs + ESPI_PERIF_PC_TX_DATA);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = FIELD_PREP(ESPI_PERIF_PC_TX_CTRL_CYC, cyc)
	      | FIELD_PREP(ESPI_PERIF_PC_TX_CTRL_TAG, tag)
	      | FIELD_PREP(ESPI_PERIF_PC_TX_CTRL_LEN, len)
	      | ESPI_PERIF_PC_TX_CTRL_TRIG_PEND;
	writel(reg, espi->regs + ESPI_PERIF_PC_TX_CTRL);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_n_out:
	mutex_unlock(&perif->pc_tx_mtx);

	return rc;
}

static long ast2600_espi_perif_np_put_tx(struct file *fp,
					 struct ast2600_espi_perif *perif,
					 struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(perif, struct ast2600_espi, perif);

	if (!mutex_trylock(&perif->np_tx_mtx))
		return -EAGAIN;

	reg = readl(espi->regs + ESPI_PERIF_NP_TX_CTRL);
	if (reg & ESPI_PERIF_NP_TX_CTRL_TRIG_PEND) {
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
	if (perif->dma.enable) {
		memcpy(perif->dma.np_tx_virt, hdr + 1, ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			writel(pkt[i], espi->regs + ESPI_PERIF_NP_TX_DATA);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = FIELD_PREP(ESPI_PERIF_NP_TX_CTRL_CYC, cyc)
	      | FIELD_PREP(ESPI_PERIF_NP_TX_CTRL_TAG, tag)
	      | FIELD_PREP(ESPI_PERIF_NP_TX_CTRL_LEN, len)
	      | ESPI_PERIF_NP_TX_CTRL_TRIG_PEND;
	writel(reg, espi->regs + ESPI_PERIF_NP_TX_CTRL);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_n_out:
	mutex_unlock(&perif->np_tx_mtx);

	return rc;
}

static long ast2600_espi_perif_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct ast2600_espi_perif *perif;
	struct aspeed_espi_ioc ioc;

	perif = container_of(fp->private_data, struct ast2600_espi_perif, mdev);

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.pkt_len > ESPI_MAX_PKT_LEN)
		return -EINVAL;

	switch (cmd) {
	case ASPEED_ESPI_PERIF_PC_GET_RX:
		return ast2600_espi_perif_pc_get_rx(fp, perif, &ioc);
	case ASPEED_ESPI_PERIF_PC_PUT_TX:
		return ast2600_espi_perif_pc_put_tx(fp, perif, &ioc);
	case ASPEED_ESPI_PERIF_NP_PUT_TX:
		return ast2600_espi_perif_np_put_tx(fp, perif, &ioc);
	default:
		break;
	};

	return -EINVAL;
}

static int ast2600_espi_perif_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct ast2600_espi_perif *perif;
	unsigned long vm_size;
	pgprot_t vm_prot;

	perif = container_of(fp->private_data, struct ast2600_espi_perif, mdev);
	if (!perif->mcyc.enable)
		return -EPERM;

	vm_size = vma->vm_end - vma->vm_start;
	vm_prot = vma->vm_page_prot;

	if (((vma->vm_pgoff << PAGE_SHIFT) + vm_size) > perif->mcyc.size)
		return -EINVAL;

	vm_prot = pgprot_noncached(vm_prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    (perif->mcyc.taddr >> PAGE_SHIFT) + vma->vm_pgoff,
			    vm_size, vm_prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations ast2600_espi_mmbi_b2h_fops = {
	.owner = THIS_MODULE,
	.mmap = ast2600_espi_mmbi_b2h_mmap,
};

static const struct file_operations ast2600_espi_mmbi_h2b_fops = {
	.owner = THIS_MODULE,
	.mmap = ast2600_espi_mmbi_h2b_mmap,
	.poll = ast2600_espi_mmbi_h2b_poll,
};

static const struct file_operations ast2600_espi_perif_fops = {
	.owner = THIS_MODULE,
	.mmap = ast2600_espi_perif_mmap,
	.unlocked_ioctl = ast2600_espi_perif_ioctl,
};

static irqreturn_t ast2600_espi_perif_mmbi_isr(int irq, void *arg)
{
	struct ast2600_espi_perif_mmbi *mmbi;
	struct ast2600_espi_perif *perif;
	struct ast2600_espi *espi;
	uint32_t sts, tmp;
	uint32_t *p;
	int i;

	espi = (struct ast2600_espi *)arg;

	perif = &espi->perif;

	sts = readl(espi->regs + ESPI_MMBI_INT_STS);
	if (!sts)
		return IRQ_NONE;

	for (i = 0, tmp = sts; i < PERIF_MMBI_INST_NUM; ++i, tmp >>= 2) {
		if (!(tmp & 0x3))
			continue;

		mmbi = &perif->mmbi.inst[i];

		p = (uint32_t *)mmbi->h2b_virt;
		p[0] = readl(espi->regs + ESPI_MMBI_HOST_RWP(i));
		p[1] = readl(espi->regs + ESPI_MMBI_HOST_RWP(i) + 4);

		mmbi->host_rwp_update = true;

		wake_up_interruptible(&mmbi->wq);
	}

	writel(sts, espi->regs + ESPI_MMBI_INT_STS);

	return IRQ_HANDLED;
}

static void ast2600_espi_perif_isr(struct ast2600_espi *espi)
{
	struct ast2600_espi_perif *perif;
	unsigned long flags;
	uint32_t sts;

	perif = &espi->perif;

	sts = readl(espi->regs + ESPI_INT_STS);

	if (sts & ESPI_INT_STS_PERIF_PC_RX_CMPLT) {
		writel(ESPI_INT_STS_PERIF_PC_RX_CMPLT, espi->regs + ESPI_INT_STS);

		spin_lock_irqsave(&perif->lock, flags);
		perif->rx_ready = true;
		spin_unlock_irqrestore(&perif->lock, flags);

		wake_up_interruptible(&perif->wq);
	}
}

static void ast2600_espi_perif_reset(struct ast2600_espi *espi)
{
	struct ast2600_espi_perif *perif;
	struct device *dev;
	uint32_t reg, mask;

	dev = espi->dev;

	perif = &espi->perif;

	if (perif->mmbi.enable) {
		mask = ~(perif->mmbi.size - 1);
		writel(mask, espi->regs + ESPI_PERIF_MMBI_MASK);
		writel(perif->mmbi.saddr, espi->regs + ESPI_PERIF_MMBI_SADDR);
		writel(perif->mmbi.taddr, espi->regs + ESPI_PERIF_MMBI_TADDR);

		reg = readl(espi->regs + ESPI_CTRL2) & ~(ESPI_CTRL2_MMBI_RD_DIS | ESPI_CTRL2_MMBI_WR_DIS);
		writel(reg, espi->regs + ESPI_CTRL2);

		writel(0xffffffff, espi->regs + ESPI_MMBI_INT_EN);

		reg = FIELD_PREP(ESPI_MMBI_CTRL_INST_SZ, perif->mmbi.inst_size)
			| FIELD_PREP(ESPI_MMBI_CTRL_TOTAL_SZ, perif->mmbi.inst_size)
			| ESPI_MMBI_CTRL_EN;
		writel(reg, espi->regs + ESPI_MMBI_CTRL);
	}

	if (perif->mcyc.enable) {
		mask = ~(perif->mcyc.size - 1);
		writel(mask, espi->regs + ESPI_PERIF_MCYC_MASK);
		writel(perif->mcyc.saddr, espi->regs + ESPI_PERIF_MCYC_SADDR);
		writel(perif->mcyc.taddr, espi->regs + ESPI_PERIF_MCYC_TADDR);

		reg = readl(espi->regs + ESPI_CTRL2) & ~(ESPI_CTRL2_MCYC_RD_DIS | ESPI_CTRL2_MCYC_WR_DIS);
		writel(reg, espi->regs + ESPI_CTRL2);
	}

	if (perif->dma.enable) {
		writel(perif->dma.np_tx_addr, espi->regs + ESPI_PERIF_NP_TX_DMA);
		writel(perif->dma.pc_tx_addr, espi->regs + ESPI_PERIF_PC_TX_DMA);
		writel(perif->dma.pc_rx_addr, espi->regs + ESPI_PERIF_PC_RX_DMA);

		reg = readl(espi->regs + ESPI_CTRL)
		      | ESPI_CTRL_PERIF_NP_TX_DMA_EN
		      | ESPI_CTRL_PERIF_PC_TX_DMA_EN
		      | ESPI_CTRL_PERIF_PC_RX_DMA_EN;
		writel(reg, espi->regs + ESPI_CTRL);
	}

	reg = readl(espi->regs + ESPI_INT_EN) | ESPI_INT_EN_PERIF_PC_RX_CMPLT;
	writel(reg, espi->regs + ESPI_INT_EN);

	reg = readl(espi->regs + ESPI_CTRL) | ESPI_CTRL_PERIF_SW_RDY;
	writel(reg, espi->regs + ESPI_CTRL);
}

static int ast2600_espi_perif_probe(struct ast2600_espi *espi)
{
	struct ast2600_espi_perif_mmbi *mmbi;
	struct ast2600_espi_perif *perif;
	struct platform_device *pdev;
	struct device *dev;
	int i, rc;

	dev = espi->dev;

	perif = &espi->perif;

	init_waitqueue_head(&perif->wq);

	spin_lock_init(&perif->lock);

	mutex_init(&perif->np_tx_mtx);
	mutex_init(&perif->pc_tx_mtx);
	mutex_init(&perif->pc_rx_mtx);

	perif->mmbi.enable = of_property_read_bool(dev->of_node, "perif-mmbi-enable");
	if (perif->mmbi.enable) {
		pdev = container_of(dev, struct platform_device, dev);

		perif->mmbi.irq = platform_get_irq(pdev, 1);
		if (perif->mmbi.irq < 0) {
			dev_err(dev, "cannot get MMBI IRQ number\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "perif-mmbi-src-addr", &perif->mmbi.saddr);
		if (rc || !IS_ALIGNED(perif->mmbi.saddr, PERIF_MMBI_ALIGN)) {
			dev_err(dev, "cannot get 64KB-aligned MMBI host address\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "perif-mmbi-instance-size", &perif->mmbi.inst_size);
		if (rc || perif->mmbi.inst_size >= MMBI_INST_SIZE_TYPES) {
			dev_err(dev, "cannot get valid MMBI instance size\n");
			return -EINVAL;
		}

		perif->mmbi.size = (SZ_8K << perif->mmbi.inst_size) * PERIF_MMBI_INST_NUM;
		perif->mmbi.virt = dmam_alloc_coherent(dev, perif->mmbi.size,
						       &perif->mmbi.taddr, GFP_KERNEL);
		if (!perif->mmbi.virt) {
			dev_err(dev, "cannot allocate MMBI\n");
			return -ENOMEM;
		}

		for (i = 0; i < PERIF_MMBI_INST_NUM; ++i) {
			mmbi = &perif->mmbi.inst[i];

			init_waitqueue_head(&mmbi->wq);

			mmbi->perif = perif;
			mmbi->host_rwp_update = false;

			mmbi->b2h_virt = perif->mmbi.virt + ((SZ_4K << perif->mmbi.inst_size) * i);
			mmbi->b2h_addr = perif->mmbi.taddr + ((SZ_4K << perif->mmbi.inst_size) * i);
			mmbi->b2h_mdev.parent = dev;
			mmbi->b2h_mdev.minor = MISC_DYNAMIC_MINOR;
			mmbi->b2h_mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-mmbi-b2h%d", DEVICE_NAME, i);
			mmbi->b2h_mdev.fops = &ast2600_espi_mmbi_b2h_fops;
			rc = misc_register(&mmbi->b2h_mdev);
			if (rc) {
				dev_err(dev, "cannot register device %s\n", mmbi->b2h_mdev.name);
				return rc;
			}

			mmbi->h2b_virt = perif->mmbi.virt + ((SZ_4K << perif->mmbi.inst_size) * (i + PERIF_MMBI_INST_NUM));
			mmbi->h2b_addr = perif->mmbi.taddr + ((SZ_4K << perif->mmbi.inst_size) * (i + PERIF_MMBI_INST_NUM));
			mmbi->h2b_mdev.parent = dev;
			mmbi->h2b_mdev.minor = MISC_DYNAMIC_MINOR;
			mmbi->h2b_mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-mmbi-h2b%d", DEVICE_NAME, i);
			mmbi->h2b_mdev.fops = &ast2600_espi_mmbi_h2b_fops;
			rc = misc_register(&mmbi->h2b_mdev);
			if (rc) {
				dev_err(dev, "cannot register device %s\n", mmbi->h2b_mdev.name);
				return rc;
			}
		}
	}

	perif->mcyc.enable = of_property_read_bool(dev->of_node, "perif-mcyc-enable");
	if (perif->mcyc.enable) {
		if (perif->mmbi.enable) {
			dev_err(dev, "cannot enable memory cycle, occupied by MMBI\n");
			return -EPERM;
		}

		rc = of_property_read_u32(dev->of_node, "perif-mcyc-src-addr", &perif->mcyc.saddr);
		if (rc || !IS_ALIGNED(perif->mcyc.saddr, PERIF_MCYC_ALIGN)) {
			dev_err(dev, "cannot get 64KB-aligned memory cycle host address\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "perif-mcyc-size", &perif->mcyc.size);
		if (rc || !IS_ALIGNED(perif->mcyc.size, PERIF_MCYC_ALIGN)) {
			dev_err(dev, "cannot get 64KB-aligned memory cycle size\n");
			return -EINVAL;
		}

		perif->mcyc.virt = dmam_alloc_coherent(dev, perif->mcyc.size,
						       &perif->mcyc.taddr, GFP_KERNEL);
		if (!perif->mcyc.virt) {
			dev_err(dev, "cannot allocate memory cycle\n");
			return -ENOMEM;
		}
	}

	perif->dma.enable = of_property_read_bool(dev->of_node, "perif-dma-mode");
	if (perif->dma.enable) {
		perif->dma.pc_tx_virt = dmam_alloc_coherent(dev, PAGE_SIZE,
							    &perif->dma.pc_tx_addr, GFP_KERNEL);
		if (!perif->dma.pc_tx_virt) {
			dev_err(dev, "cannot allocate posted TX DMA buffer\n");
			return -ENOMEM;
		}

		perif->dma.pc_rx_virt = dmam_alloc_coherent(dev, PAGE_SIZE,
							    &perif->dma.pc_rx_addr, GFP_KERNEL);
		if (!perif->dma.pc_rx_virt) {
			dev_err(dev, "cannot allocate posted RX DMA buffer\n");
			return -ENOMEM;
		}

		perif->dma.np_tx_virt = dmam_alloc_coherent(dev, PAGE_SIZE,
							    &perif->dma.np_tx_addr, GFP_KERNEL);
		if (!perif->dma.np_tx_virt) {
			dev_err(dev, "cannot allocate non-posted TX DMA buffer\n");
			return -ENOMEM;
		}
	}

	perif->mdev.parent = dev;
	perif->mdev.minor = MISC_DYNAMIC_MINOR;
	perif->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-peripheral", DEVICE_NAME);
	perif->mdev.fops = &ast2600_espi_perif_fops;
	rc = misc_register(&perif->mdev);
	if (rc) {
		dev_err(dev, "cannot register device %s\n", perif->mdev.name);
		return rc;
	}

	ast2600_espi_perif_reset(espi);

	if (perif->mmbi.enable) {
		rc = devm_request_irq(dev, espi->perif.mmbi.irq,
				      ast2600_espi_perif_mmbi_isr, 0, dev_name(dev), espi);
		if (rc) {
			dev_err(dev, "cannot request MMBI IRQ\n");
			return rc;
		}
	}

	return 0;
}

static int ast2600_espi_perif_remove(struct ast2600_espi *espi)
{
	struct ast2600_espi_perif_mmbi *mmbi;
	struct ast2600_espi_perif *perif;
	struct device *dev;
	int i;

	dev = espi->dev;

	perif = &espi->perif;

	if (perif->mmbi.enable) {
		for (i = 0; i < PERIF_MMBI_INST_NUM; ++i) {
			mmbi = &perif->mmbi.inst[i];
			misc_deregister(&mmbi->b2h_mdev);
			misc_deregister(&mmbi->h2b_mdev);
		}

		dmam_free_coherent(dev, perif->mmbi.size, perif->mmbi.virt,
				   perif->mmbi.taddr);
	}

	if (perif->mcyc.enable)
		dmam_free_coherent(dev, perif->mcyc.size, perif->mcyc.virt,
				   perif->mcyc.taddr);

	if (perif->dma.enable) {
		dmam_free_coherent(dev, PAGE_SIZE, perif->dma.np_tx_virt,
				   perif->dma.np_tx_addr);
		dmam_free_coherent(dev, PAGE_SIZE, perif->dma.pc_tx_virt,
				   perif->dma.pc_tx_addr);
		dmam_free_coherent(dev, PAGE_SIZE, perif->dma.pc_rx_virt,
				   perif->dma.pc_rx_addr);
	}

	mutex_destroy(&perif->np_tx_mtx);
	mutex_destroy(&perif->pc_tx_mtx);
	mutex_destroy(&perif->pc_rx_mtx);

	misc_deregister(&perif->mdev);

	return 0;
}

/* virtual wire channel (CH1) */
static long ast2600_espi_vw_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct ast2600_espi_vw *vw;
	struct ast2600_espi *espi;
	uint32_t gpio;

	vw = container_of(fp->private_data, struct ast2600_espi_vw, mdev);
	espi = container_of(vw, struct ast2600_espi, vw);
	gpio = vw->gpio.val;

	switch (cmd) {
	case ASPEED_ESPI_VW_GET_GPIO_VAL:
		if (put_user(gpio, (uint32_t __user *)arg))
			return -EFAULT;
		break;
	case ASPEED_ESPI_VW_PUT_GPIO_VAL:
		if (get_user(gpio, (uint32_t __user *)arg))
			return -EFAULT;

		writel(gpio, espi->regs + ESPI_VW_GPIO_VAL);
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

static const struct file_operations ast2600_espi_vw_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ast2600_espi_vw_ioctl,
};

static void ast2600_espi_vw_isr(struct ast2600_espi *espi)
{
	struct ast2600_espi_vw *vw;
	uint32_t sts;

	vw = &espi->vw;

	sts = readl(espi->regs + ESPI_INT_STS);

	if (sts & ESPI_INT_STS_VW_GPIO) {
		vw->gpio.val = readl(espi->regs + ESPI_VW_GPIO_VAL);
		writel(ESPI_INT_STS_VW_GPIO, espi->regs + ESPI_INT_STS);
	}
}

static void ast2600_espi_vw_reset(struct ast2600_espi *espi)
{
	uint32_t reg;
	struct ast2600_espi_vw *vw = &espi->vw;

	writel(vw->gpio.grp, espi->regs + ESPI_VW_GPIO_GRP);
	writel(vw->gpio.dir, espi->regs + ESPI_VW_GPIO_DIR);

	vw->gpio.val = readl(espi->regs + ESPI_VW_GPIO_VAL);

	writel(ESPI_INT_EN_VW_GPIO, espi->regs + ESPI_INT_EN);

	reg = readl(espi->regs + ESPI_CTRL2) & ~(ESPI_CTRL2_VW_TX_SORT);
	writel(reg, espi->regs + ESPI_CTRL2);

	reg = readl(espi->regs + ESPI_CTRL)
	      | ((vw->gpio.hw_mode) ? 0 : ESPI_CTRL_VW_GPIO_SW)
	      | ESPI_CTRL_VW_SW_RDY;
	writel(reg, espi->regs + ESPI_CTRL);
}

static int ast2600_espi_vw_probe(struct ast2600_espi *espi)
{
	int rc;
	struct device *dev = espi->dev;
	struct ast2600_espi_vw *vw = &espi->vw;

	vw->gpio.hw_mode = of_property_read_bool(dev->of_node, "vw-gpio-hw-mode");
	of_property_read_u32(dev->of_node, "vw-gpio-group", &vw->gpio.grp);
	of_property_read_u32(dev->of_node, "vw-gpio-direction", &vw->gpio.dir);

	vw->mdev.parent = dev;
	vw->mdev.minor = MISC_DYNAMIC_MINOR;
	vw->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-vw", DEVICE_NAME);
	vw->mdev.fops = &ast2600_espi_vw_fops;
	rc = misc_register(&vw->mdev);
	if (rc) {
		dev_err(dev, "cannot register device %s\n", vw->mdev.name);
		return rc;
	}

	ast2600_espi_vw_reset(espi);

	return 0;
}

static int ast2600_espi_vw_remove(struct ast2600_espi *espi)
{
	struct ast2600_espi_vw *vw;

	vw = &espi->vw;

	misc_deregister(&vw->mdev);

	return 0;
}

/* out-of-band channel (CH2) */
static long ast2600_espi_oob_dma_get_rx(struct file *fp,
					struct ast2600_espi_oob *oob,
					struct aspeed_espi_ioc *ioc)
{
	struct ast2600_espi_oob_dma_rx_desc *d;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint32_t wptr, pkt_len;
	unsigned long flags;
	uint8_t *pkt;
	int rc;

	espi = container_of(oob, struct ast2600_espi, oob);

	wptr = readl(espi->regs + ESPI_OOB_RX_DESC_WPTR);

	d = &oob->dma.rxd_virt[wptr];

	if (!d->dirty)
		return -EFAULT;

	pkt_len = ((d->len) ? d->len : ESPI_MAX_PLD_LEN) + sizeof(struct espi_comm_hdr);

	if (ioc->pkt_len < pkt_len)
		return -EINVAL;

	pkt = vmalloc(pkt_len);
	if (!pkt)
		return -ENOMEM;

	hdr = (struct espi_comm_hdr *)pkt;
	hdr->cyc = d->cyc;
	hdr->tag = d->tag;
	hdr->len_h = d->len >> 8;
	hdr->len_l = d->len & 0xff;
	memcpy(hdr + 1, oob->dma.rx_virt + (PAGE_SIZE * wptr), pkt_len - sizeof(*hdr));

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&oob->lock, flags);

	/* make current descriptor available again */
	d->dirty = 0;

	wptr = (wptr + 1) % OOB_DMA_DESC_NUM;
	writel(wptr | ESPI_OOB_RX_DESC_WPTR_RECV_EN, espi->regs + ESPI_OOB_RX_DESC_WPTR);

	/* set ready flag base on the next RX descriptor */
	oob->rx_ready = oob->dma.rxd_virt[wptr].dirty;

	spin_unlock_irqrestore(&oob->lock, flags);

	rc = 0;

free_n_out:
	vfree(pkt);

	return rc;
}

static long ast2600_espi_oob_get_rx(struct file *fp,
				    struct ast2600_espi_oob *oob,
				    struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	unsigned long flags;
	uint32_t pkt_len;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(oob, struct ast2600_espi, oob);

	if (fp->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&oob->rx_mtx))
			return -EAGAIN;

		if (!oob->rx_ready) {
			rc = -ENODATA;
			goto unlock_mtx_n_out;
		}
	} else {
		mutex_lock(&oob->rx_mtx);

		if (!oob->rx_ready) {
			rc = wait_event_interruptible(oob->wq, oob->rx_ready);
			if (rc == -ERESTARTSYS) {
				rc = -EINTR;
				goto unlock_mtx_n_out;
			}
		}
	}

	if (oob->dma.enable) {
		rc = ast2600_espi_oob_dma_get_rx(fp, oob, ioc);
		goto unlock_mtx_n_out;
	}

	/*
	 * common header (i.e. cycle type, tag, and length)
	 * part is written to HW registers
	 */
	reg = readl(espi->regs + ESPI_OOB_RX_CTRL);
	cyc = FIELD_GET(ESPI_OOB_RX_CTRL_CYC, reg);
	tag = FIELD_GET(ESPI_OOB_RX_CTRL_TAG, reg);
	len = FIELD_GET(ESPI_OOB_RX_CTRL_LEN, reg);

	/*
	 * calculate the length of the rest part of the
	 * eSPI packet to be read from HW and copied to
	 * user space.
	 */
	pkt_len = ((len) ? len : ESPI_MAX_PLD_LEN) + sizeof(struct espi_comm_hdr);

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

	for (i = sizeof(*hdr); i < pkt_len; ++i)
		pkt[i] = readl(espi->regs + ESPI_OOB_RX_DATA) & 0xff;

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&oob->lock, flags);

	writel(ESPI_OOB_RX_CTRL_SERV_PEND, espi->regs + ESPI_OOB_RX_CTRL);
	oob->rx_ready = 0;

	spin_unlock_irqrestore(&oob->lock, flags);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&oob->rx_mtx);

	return rc;
}

static long ast2600_espi_oob_dma_put_tx(struct file *fp,
					struct ast2600_espi_oob *oob,
					struct aspeed_espi_ioc *ioc)
{
	struct ast2600_espi_oob_dma_tx_desc *d;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint32_t rptr, wptr;
	uint8_t *pkt;
	int rc;

	espi = container_of(oob, struct ast2600_espi, oob);

	pkt = vzalloc(ioc->pkt_len);
	if (!pkt)
		return -ENOMEM;

	hdr = (struct espi_comm_hdr *)pkt;

	if (copy_from_user(pkt, (void __user *)ioc->pkt, ioc->pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	/* kick HW to update descriptor read/write pointer */
	writel(ESPI_OOB_TX_DESC_RPTR_UPDATE, espi->regs + ESPI_OOB_TX_DESC_RPTR);

	rptr = readl(espi->regs + ESPI_OOB_TX_DESC_RPTR);
	wptr = readl(espi->regs + ESPI_OOB_TX_DESC_WPTR);

	if (((wptr + 1) % OOB_DMA_DESC_NUM) == rptr) {
		rc = -EBUSY;
		goto free_n_out;
	}

	d = &oob->dma.txd_virt[wptr];
	d->cyc = hdr->cyc;
	d->tag = hdr->tag;
	d->len = (hdr->len_h << 8) | (hdr->len_l & 0xff);
	d->msg_type = OOB_DMA_DESC_CUSTOM;

	memcpy(oob->dma.tx_virt + (PAGE_SIZE * wptr), hdr + 1,  ioc->pkt_len - sizeof(*hdr));

	dma_wmb();

	wptr = (wptr + 1) % OOB_DMA_DESC_NUM;
	writel(wptr | ESPI_OOB_TX_DESC_WPTR_SEND_EN, espi->regs + ESPI_OOB_TX_DESC_WPTR);

	rc = 0;

free_n_out:
	vfree(pkt);

	return rc;
}

static long ast2600_espi_oob_put_tx(struct file *fp,
				    struct ast2600_espi_oob *oob,
				    struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(oob, struct ast2600_espi, oob);

	if (!mutex_trylock(&oob->tx_mtx))
		return -EAGAIN;

	if (oob->dma.enable) {
		rc = ast2600_espi_oob_dma_put_tx(fp, oob, ioc);
		goto unlock_mtx_n_out;
	}

	reg = readl(espi->regs + ESPI_OOB_TX_CTRL);
	if (reg & ESPI_OOB_TX_CTRL_TRIG_PEND) {
		rc = -EBUSY;
		goto unlock_mtx_n_out;
	}

	if (ioc->pkt_len > ESPI_MAX_PKT_LEN) {
		rc = -EINVAL;
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
	for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
		writel(pkt[i], espi->regs + ESPI_OOB_TX_DATA);

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = FIELD_PREP(ESPI_OOB_TX_CTRL_CYC, cyc)
	      | FIELD_PREP(ESPI_OOB_TX_CTRL_TAG, tag)
	      | FIELD_PREP(ESPI_OOB_TX_CTRL_LEN, len)
	      | ESPI_OOB_TX_CTRL_TRIG_PEND;
	writel(reg, espi->regs + ESPI_OOB_TX_CTRL);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&oob->tx_mtx);

	return rc;
}

static long ast2600_espi_oob_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct ast2600_espi_oob *oob;
	struct aspeed_espi_ioc ioc;

	oob = container_of(fp->private_data, struct ast2600_espi_oob, mdev);

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.pkt_len > ESPI_MAX_PKT_LEN)
		return -EINVAL;

	switch (cmd) {
	case ASPEED_ESPI_OOB_GET_RX:
		return ast2600_espi_oob_get_rx(fp, oob, &ioc);
	case ASPEED_ESPI_OOB_PUT_TX:
		return ast2600_espi_oob_put_tx(fp, oob, &ioc);
	};

	return -EINVAL;
}

static const struct file_operations ast2600_espi_oob_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ast2600_espi_oob_ioctl,
};

static void ast2600_espi_oob_isr(struct ast2600_espi *espi)
{
	struct ast2600_espi_oob *oob;
	unsigned long flags;
	uint32_t sts;

	oob = &espi->oob;

	sts = readl(espi->regs + ESPI_INT_STS);

	if (sts & ESPI_INT_STS_OOB_RX_CMPLT) {
		writel(ESPI_INT_STS_OOB_RX_CMPLT, espi->regs + ESPI_INT_STS);

		spin_lock_irqsave(&oob->lock, flags);
		oob->rx_ready = true;
		spin_unlock_irqrestore(&oob->lock, flags);

		wake_up_interruptible(&oob->wq);
	}
}

static void ast2600_espi_oob_reset(struct ast2600_espi *espi)
{
	struct ast2600_espi_oob *oob;
	dma_addr_t tx_addr, rx_addr;
	uint32_t reg;
	int i;

	oob = &espi->oob;
	if (oob->dma.enable) {
		tx_addr = oob->dma.tx_addr;
		rx_addr = oob->dma.rx_addr;

		for (i = 0; i < OOB_DMA_DESC_NUM; ++i) {
			oob->dma.txd_virt[i].data_addr = tx_addr;
			tx_addr += PAGE_SIZE;

			oob->dma.rxd_virt[i].data_addr = rx_addr;
			oob->dma.rxd_virt[i].dirty = 0;
			rx_addr += PAGE_SIZE;
		}

		reg = readl(espi->regs + ESPI_CTRL);
		writel(reg & ~ESPI_CTRL_OOB_RX_SW_RST, espi->regs + ESPI_CTRL);
		udelay(1);
		writel(reg, espi->regs + ESPI_CTRL);

		writel(oob->dma.txd_addr, espi->regs + ESPI_OOB_TX_DMA);
		writel(0x0, espi->regs + ESPI_OOB_TX_DESC_WPTR);
		writel(OOB_DMA_DESC_NUM, espi->regs + ESPI_OOB_TX_DESC_NUM);

		writel(oob->dma.rxd_addr, espi->regs + ESPI_OOB_RX_DMA);
		writel(0x0, espi->regs + ESPI_OOB_RX_DESC_WPTR);
		writel(OOB_DMA_DESC_NUM, espi->regs + ESPI_OOB_RX_DESC_NUM);

		reg = readl(espi->regs + ESPI_CTRL)
		      | ESPI_CTRL_OOB_TX_DMA_EN
		      | ESPI_CTRL_OOB_RX_DMA_EN;
		writel(reg, espi->regs + ESPI_CTRL);

		/* activate RX DMA to make OOB_FREE */
		writel(ESPI_OOB_RX_DESC_WPTR_RECV_EN, espi->regs + ESPI_OOB_RX_DESC_WPTR);
	}

	writel(ESPI_INT_EN_OOB_RX_CMPLT, espi->regs + ESPI_INT_EN);

	reg = readl(espi->regs + ESPI_CTRL) | ESPI_CTRL_OOB_SW_RDY;
	writel(reg, espi->regs + ESPI_CTRL);
}

static int ast2600_espi_oob_probe(struct ast2600_espi *espi)
{
	struct ast2600_espi_oob *oob;
	struct device *dev;
	int rc;

	dev = espi->dev;

	oob = &espi->oob;

	init_waitqueue_head(&oob->wq);

	spin_lock_init(&oob->lock);

	mutex_init(&oob->tx_mtx);
	mutex_init(&oob->rx_mtx);

	oob->dma.enable = of_property_read_bool(dev->of_node, "oob-dma-mode");
	if (oob->dma.enable) {
		oob->dma.txd_virt = dmam_alloc_coherent(dev, sizeof(*oob->dma.txd_virt) * OOB_DMA_DESC_NUM, &oob->dma.txd_addr, GFP_KERNEL);
		if (!oob->dma.txd_virt) {
			dev_err(dev, "cannot allocate DMA TX descriptor\n");
			return -ENOMEM;
		}
		oob->dma.tx_virt = dmam_alloc_coherent(dev, PAGE_SIZE * OOB_DMA_DESC_NUM, &oob->dma.tx_addr, GFP_KERNEL);
		if (!oob->dma.tx_virt) {
			dev_err(dev, "cannot allocate DMA TX buffer\n");
			return -ENOMEM;
		}

		oob->dma.rxd_virt = dmam_alloc_coherent(dev, sizeof(*oob->dma.rxd_virt) * OOB_DMA_DESC_NUM, &oob->dma.rxd_addr, GFP_KERNEL);
		if (!oob->dma.rxd_virt) {
			dev_err(dev, "cannot allocate DMA RX descriptor\n");
			return -ENOMEM;
		}

		oob->dma.rx_virt = dmam_alloc_coherent(dev, PAGE_SIZE * OOB_DMA_DESC_NUM, &oob->dma.rx_addr, GFP_KERNEL);
		if (!oob->dma.rx_virt) {
			dev_err(dev, "cannot allocate DMA TX buffer\n");
			return -ENOMEM;
		}
	}

	oob->mdev.parent = dev;
	oob->mdev.minor = MISC_DYNAMIC_MINOR;
	oob->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-oob", DEVICE_NAME);
	oob->mdev.fops = &ast2600_espi_oob_fops;
	rc = misc_register(&oob->mdev);
	if (rc) {
		dev_err(dev, "cannot register device %s\n", oob->mdev.name);
		return rc;
	}

	ast2600_espi_oob_reset(espi);

	return 0;
}

static int ast2600_espi_oob_remove(struct ast2600_espi *espi)
{
	struct ast2600_espi_oob *oob;
	struct device *dev;

	dev = espi->dev;

	oob = &espi->oob;

	if (oob->dma.enable) {
		dmam_free_coherent(dev, sizeof(*oob->dma.txd_virt) * OOB_DMA_DESC_NUM,
				   oob->dma.txd_virt, oob->dma.txd_addr);
		dmam_free_coherent(dev, PAGE_SIZE * OOB_DMA_DESC_NUM,
				   oob->dma.tx_virt, oob->dma.tx_addr);
		dmam_free_coherent(dev, sizeof(*oob->dma.rxd_virt) * OOB_DMA_DESC_NUM,
				   oob->dma.rxd_virt, oob->dma.rxd_addr);
		dmam_free_coherent(dev, PAGE_SIZE * OOB_DMA_DESC_NUM,
				   oob->dma.rx_virt, oob->dma.rx_addr);
	}

	mutex_destroy(&oob->tx_mtx);
	mutex_destroy(&oob->rx_mtx);

	misc_deregister(&oob->mdev);

	return 0;
}

/* flash channel (CH3) */
static long ast2600_espi_flash_get_rx(struct file *fp,
				      struct ast2600_espi_flash *flash,
				      struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	unsigned long flags;
	uint32_t pkt_len;
	uint8_t *pkt;
	int i, rc;

	rc = 0;

	espi = container_of(flash, struct ast2600_espi, flash);

	if (fp->f_flags & O_NONBLOCK) {
		if (!mutex_trylock(&flash->rx_mtx))
			return -EAGAIN;

		if (!flash->rx_ready) {
			rc = -ENODATA;
			goto unlock_mtx_n_out;
		}
	} else {
		mutex_lock(&flash->rx_mtx);

		if (!flash->rx_ready) {
			rc = wait_event_interruptible(flash->wq, flash->rx_ready);
			if (rc == -ERESTARTSYS) {
				rc = -EINTR;
				goto unlock_mtx_n_out;
			}
		}
	}

	/*
	 * common header (i.e. cycle type, tag, and length)
	 * part is written to HW registers
	 */
	reg = readl(espi->regs + ESPI_FLASH_RX_CTRL);
	cyc = FIELD_GET(ESPI_FLASH_RX_CTRL_CYC, reg);
	tag = FIELD_GET(ESPI_FLASH_RX_CTRL_TAG, reg);
	len = FIELD_GET(ESPI_FLASH_RX_CTRL_LEN, reg);

	/*
	 * calculate the length of the rest part of the
	 * eSPI packet to be read from HW and copied to
	 * user space.
	 */
	switch (cyc) {
	case ESPI_FLASH_WRITE:
		pkt_len = ((len) ? len : ESPI_MAX_PLD_LEN) +
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
		pkt_len = ((len) ? len : ESPI_MAX_PLD_LEN) +
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

	if (flash->dma.enable) {
		memcpy(hdr + 1, flash->dma.rx_virt, pkt_len - sizeof(*hdr));
	} else {
		for (i = sizeof(*hdr); i < pkt_len; ++i)
			pkt[i] = readl(espi->regs + ESPI_FLASH_RX_DATA) & 0xff;
	}

	if (copy_to_user((void __user *)ioc->pkt, pkt, pkt_len)) {
		rc = -EFAULT;
		goto free_n_out;
	}

	spin_lock_irqsave(&flash->lock, flags);

	writel(ESPI_FLASH_RX_CTRL_SERV_PEND, espi->regs + ESPI_FLASH_RX_CTRL);
	flash->rx_ready = 0;

	spin_unlock_irqrestore(&flash->lock, flags);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&flash->rx_mtx);

	return rc;
}

static long ast2600_espi_flash_put_tx(struct file *fp,
				      struct ast2600_espi_flash *flash,
				      struct aspeed_espi_ioc *ioc)
{
	uint32_t reg, cyc, tag, len;
	struct ast2600_espi *espi;
	struct espi_comm_hdr *hdr;
	uint8_t *pkt;
	int i, rc;

	espi = container_of(flash, struct ast2600_espi, flash);

	if (!mutex_trylock(&flash->tx_mtx))
		return -EAGAIN;

	reg = readl(espi->regs + ESPI_FLASH_TX_CTRL);
	if (reg & ESPI_FLASH_TX_CTRL_TRIG_PEND) {
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
	if (flash->dma.enable) {
		memcpy(flash->dma.tx_virt, hdr + 1, ioc->pkt_len - sizeof(*hdr));
		dma_wmb();
	} else {
		for (i = sizeof(*hdr); i < ioc->pkt_len; ++i)
			writel(pkt[i], espi->regs + ESPI_FLASH_TX_DATA);
	}

	cyc = hdr->cyc;
	tag = hdr->tag;
	len = (hdr->len_h << 8) | (hdr->len_l & 0xff);

	reg = FIELD_PREP(ESPI_FLASH_TX_CTRL_CYC, cyc)
	      | FIELD_PREP(ESPI_FLASH_TX_CTRL_TAG, tag)
	      | FIELD_PREP(ESPI_FLASH_TX_CTRL_LEN, len)
	      | ESPI_FLASH_TX_CTRL_TRIG_PEND;
	writel(reg, espi->regs + ESPI_FLASH_TX_CTRL);

	rc = 0;

free_n_out:
	vfree(pkt);

unlock_mtx_n_out:
	mutex_unlock(&flash->tx_mtx);

	return rc;
}

static long ast2600_espi_flash_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct ast2600_espi_flash *flash;
	struct aspeed_espi_ioc ioc;

	flash = container_of(fp->private_data, struct ast2600_espi_flash, mdev);

	if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
		return -EFAULT;

	if (ioc.pkt_len > ESPI_MAX_PKT_LEN)
		return -EINVAL;

	switch (cmd) {
	case ASPEED_ESPI_FLASH_GET_RX:
		return ast2600_espi_flash_get_rx(fp, flash, &ioc);
	case ASPEED_ESPI_FLASH_PUT_TX:
		return ast2600_espi_flash_put_tx(fp, flash, &ioc);
	default:
		break;
	};

	return -EINVAL;
}

static const struct file_operations ast2600_espi_flash_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ast2600_espi_flash_ioctl,
};

static void ast2600_espi_flash_isr(struct ast2600_espi *espi)
{
	struct ast2600_espi_flash *flash;
	unsigned long flags;
	uint32_t sts;

	flash = &espi->flash;

	sts = readl(espi->regs + ESPI_INT_STS_FLASH);

	if (sts & ESPI_INT_STS_FLASH_RX_CMPLT) {
		writel(ESPI_INT_STS_FLASH_RX_CMPLT, espi->regs + ESPI_INT_STS_FLASH);

		spin_lock_irqsave(&flash->lock, flags);
		flash->rx_ready = true;
		spin_unlock_irqrestore(&flash->lock, flags);

		wake_up_interruptible(&flash->wq);
	}
}

static void ast2600_espi_flash_reset(struct ast2600_espi *espi)
{
	struct ast2600_espi_flash *flash;
	uint32_t reg;

	flash = &espi->flash;

	reg = readl(espi->regs + ESPI_CTRL)
	      | FIELD_PREP(ESPI_CTRL_FLASH_SAFS_MODE, flash->safs.mode);
	writel(reg, espi->regs + ESPI_CTRL);

	if (flash->safs.mode == SAFS_MODE_MIX) {
		reg = FIELD_PREP(ESPI_FLASH_SAFS_TADDR_BASE, flash->safs.taddr >> 24)
			| FIELD_PREP(ESPI_FLASH_SAFS_TADDR_MASK, (~(flash->safs.size - 1)) >> 24);
		writel(reg, espi->regs + ESPI_FLASH_SAFS_TADDR);
	}

	if (flash->dma.enable) {
		writel(flash->dma.tx_addr, espi->regs + ESPI_FLASH_TX_DMA);
		writel(flash->dma.rx_addr, espi->regs + ESPI_FLASH_RX_DMA);

		reg = readl(espi->regs + ESPI_CTRL)
		      | ESPI_CTRL_FLASH_TX_DMA_EN
			  | ESPI_CTRL_FLASH_RX_DMA_EN;
		writel(reg, espi->regs + ESPI_CTRL);
	}

	reg = readl(espi->regs + ESPI_INT_EN) | ESPI_INT_EN_FLASH_RX_CMPLT;
	writel(reg, espi->regs + ESPI_INT_EN);

	reg = readl(espi->regs + ESPI_CTRL) | ESPI_CTRL_FLASH_SW_RDY;
	writel(reg, espi->regs + ESPI_CTRL);
}

static int ast2600_espi_flash_probe(struct ast2600_espi *espi)
{
	struct ast2600_espi_flash *flash;
	struct device *dev;
	int rc;

	dev = espi->dev;

	flash = &espi->flash;

	init_waitqueue_head(&flash->wq);

	spin_lock_init(&flash->lock);

	mutex_init(&flash->tx_mtx);
	mutex_init(&flash->rx_mtx);

	flash->safs.mode = SAFS_MODE_HW;

	of_property_read_u32(dev->of_node, "flash-safs-mode", &flash->safs.mode);
	if (flash->safs.mode == SAFS_MODE_MIX) {
		rc = of_property_read_u32(dev->of_node, "flash-safs-tgt-addr", &flash->safs.taddr);
		if (rc || !IS_ALIGNED(flash->safs.taddr, FLASH_SAFS_ALIGN)) {
			dev_err(dev, "cannot get 16MB-aligned SAFS target address\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "flash-safs-size", &flash->safs.size);
		if (rc || !IS_ALIGNED(flash->safs.size, FLASH_SAFS_ALIGN)) {
			dev_err(dev, "cannot get 16MB-aligned SAFS size\n");
			return -ENODEV;
		}
	}

	flash->dma.enable = of_property_read_bool(dev->of_node, "flash-dma-mode");
	if (flash->dma.enable) {
		flash->dma.tx_virt = dmam_alloc_coherent(dev, PAGE_SIZE, &flash->dma.tx_addr, GFP_KERNEL);
		if (!flash->dma.tx_virt) {
			dev_err(dev, "cannot allocate DMA TX buffer\n");
			return -ENOMEM;
		}

		flash->dma.rx_virt = dmam_alloc_coherent(dev, PAGE_SIZE, &flash->dma.rx_addr, GFP_KERNEL);
		if (!flash->dma.rx_virt) {
			dev_err(dev, "cannot allocate DMA RX buffer\n");
			return -ENOMEM;
		}
	}

	flash->mdev.parent = dev;
	flash->mdev.minor = MISC_DYNAMIC_MINOR;
	flash->mdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s-flash", DEVICE_NAME);
	flash->mdev.fops = &ast2600_espi_flash_fops;
	rc = misc_register(&flash->mdev);
	if (rc) {
		dev_err(dev, "cannot register device %s\n", flash->mdev.name);
		return rc;
	}

	ast2600_espi_flash_reset(espi);

	return 0;
}

static int ast2600_espi_flash_remove(struct ast2600_espi *espi)
{
	struct ast2600_espi_flash *flash;
	struct device *dev;

	dev = espi->dev;

	flash = &espi->flash;

	if (flash->dma.enable) {
		dmam_free_coherent(dev, PAGE_SIZE, flash->dma.tx_virt, flash->dma.tx_addr);
		dmam_free_coherent(dev, PAGE_SIZE, flash->dma.rx_virt, flash->dma.rx_addr);
	}

	mutex_destroy(&flash->tx_mtx);
	mutex_destroy(&flash->rx_mtx);

	misc_deregister(&flash->mdev);

	return 0;
}

/* global control */
static irqreturn_t ast2600_espi_isr(int irq, void *arg)
{
	struct ast2600_espi *espi;
	uint32_t sts;

	espi = (struct ast2600_espi *)arg;

	sts = readl(espi->regs + ESPI_INT_STS);
	if (!sts)
		return IRQ_NONE;

	if (sts & ESPI_INT_STS_PERIF)
		ast2600_espi_perif_isr(espi);

	if (sts & ESPI_INT_STS_VW)
		ast2600_espi_vw_isr(espi);

	if (sts & ESPI_INT_STS_OOB)
		ast2600_espi_oob_isr(espi);

	if (sts & ESPI_INT_STS_FLASH)
		ast2600_espi_flash_isr(espi);

	if (sts & ESPI_INT_STS_RST_DEASSERT) {
		ast2600_espi_perif_reset(espi);
		ast2600_espi_vw_reset(espi);
		ast2600_espi_oob_reset(espi);
		ast2600_espi_flash_reset(espi);
		writel(ESPI_INT_STS_RST_DEASSERT, espi->regs + ESPI_INT_STS);
	}

	return IRQ_HANDLED;
}

static int ast2600_espi_probe(struct platform_device *pdev)
{
	struct ast2600_espi *espi;
	struct resource *res;
	struct device *dev;
	uint32_t reg;
	int rc;

	dev = &pdev->dev;

	espi = devm_kzalloc(dev, sizeof(*espi), GFP_KERNEL);
	if (!espi)
		return -ENOMEM;

	espi->dev = dev;

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(dev, "cannot set 64-bits DMA mask\n");
		return rc;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot get resource\n");
		return -ENODEV;
	}

	espi->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(espi->regs)) {
		dev_err(dev, "cannot map registers\n");
		return PTR_ERR(espi->regs);
	}

	espi->irq = platform_get_irq(pdev, 0);
	if (espi->irq < 0) {
		dev_err(dev, "cannot get IRQ number\n");
		return -ENODEV;
	}

	espi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(espi->clk)) {
		dev_err(dev, "cannot get clock control\n");
		return PTR_ERR(espi->clk);
	}

	rc = clk_prepare_enable(espi->clk);
	if (rc) {
		dev_err(dev, "cannot enable clocks\n");
		return rc;
	}

	writel(0x0, espi->regs + ESPI_INT_EN);
	writel(0xffffffff, espi->regs + ESPI_INT_STS);

	writel(0x0, espi->regs + ESPI_MMBI_INT_EN);
	writel(0xffffffff, espi->regs + ESPI_MMBI_INT_STS);

	rc = ast2600_espi_perif_probe(espi);
	if (rc) {
		dev_err(dev, "cannot init peripheral channel, rc=%d\n", rc);
		return rc;
	}

	rc = ast2600_espi_vw_probe(espi);
	if (rc) {
		dev_err(dev, "cannot init vw channel, rc=%d\n", rc);
		return rc;
	}

	rc = ast2600_espi_oob_probe(espi);
	if (rc) {
		dev_err(dev, "cannot init oob channel, rc=%d\n", rc);
		return rc;
	}

	rc = ast2600_espi_flash_probe(espi);
	if (rc) {
		dev_err(dev, "cannot init flash channel, rc=%d\n", rc);
		return rc;
	}

	rc = devm_request_irq(dev, espi->irq, ast2600_espi_isr, 0, dev_name(dev), espi);
	if (rc) {
		dev_err(dev, "cannot request IRQ\n");
		return rc;
	}

	reg = readl(espi->regs + ESPI_INT_EN) | ESPI_INT_EN_RST_DEASSERT;
	writel(reg, espi->regs + ESPI_INT_EN);

	dev_set_drvdata(dev, espi);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int ast2600_espi_remove(struct platform_device *pdev)
{
	struct ast2600_espi *espi;
	struct device *dev;
	int rc;

	dev = &pdev->dev;

	espi = (struct ast2600_espi *)dev_get_drvdata(dev);

	rc = ast2600_espi_perif_remove(espi);
	if (rc)
		dev_warn(dev, "cannot remove peripheral channel, rc=%d\n", rc);

	rc = ast2600_espi_vw_remove(espi);
	if (rc)
		dev_warn(dev, "cannot remove peripheral channel, rc=%d\n", rc);

	rc = ast2600_espi_oob_remove(espi);
	if (rc)
		dev_warn(dev, "cannot remove peripheral channel, rc=%d\n", rc);

	rc = ast2600_espi_flash_remove(espi);
	if (rc)
		dev_warn(dev, "cannot remove peripheral channel, rc=%d\n", rc);

	return 0;
}

static const struct of_device_id ast2600_espi_of_matches[] = {
	{ .compatible = "aspeed,ast2600-espi" },
	{ },
};

static struct platform_driver ast2600_espi_driver = {
	.driver = {
		.name = "ast2600-espi",
		.of_match_table = ast2600_espi_of_matches,
	},
	.probe = ast2600_espi_probe,
	.remove = ast2600_espi_remove,
};

module_platform_driver(ast2600_espi_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_DESCRIPTION("Control of AST2600 eSPI Device");
MODULE_LICENSE("GPL");

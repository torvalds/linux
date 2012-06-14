/*
    saa7146.o - driver for generic saa7146-based hardware

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <media/saa7146.h>
#include <linux/module.h>

static int saa7146_num;

unsigned int saa7146_debug;

module_param(saa7146_debug, uint, 0644);
MODULE_PARM_DESC(saa7146_debug, "debug level (default: 0)");

#if 0
static void dump_registers(struct saa7146_dev* dev)
{
	int i = 0;

	pr_info(" @ %li jiffies:\n", jiffies);
	for (i = 0; i <= 0x148; i += 4)
		pr_info("0x%03x: 0x%08x\n", i, saa7146_read(dev, i));
}
#endif

/****************************************************************************
 * gpio and debi helper functions
 ****************************************************************************/

void saa7146_setgpio(struct saa7146_dev *dev, int port, u32 data)
{
	u32 value = 0;

	BUG_ON(port > 3);

	value = saa7146_read(dev, GPIO_CTRL);
	value &= ~(0xff << (8*port));
	value |= (data << (8*port));
	saa7146_write(dev, GPIO_CTRL, value);
}

/* This DEBI code is based on the saa7146 Stradis driver by Nathan Laredo */
static inline int saa7146_wait_for_debi_done_sleep(struct saa7146_dev *dev,
				unsigned long us1, unsigned long us2)
{
	unsigned long timeout;
	int err;

	/* wait for registers to be programmed */
	timeout = jiffies + usecs_to_jiffies(us1);
	while (1) {
		err = time_after(jiffies, timeout);
		if (saa7146_read(dev, MC2) & 2)
			break;
		if (err) {
			pr_err("%s: %s timed out while waiting for registers getting programmed\n",
			       dev->name, __func__);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	/* wait for transfer to complete */
	timeout = jiffies + usecs_to_jiffies(us2);
	while (1) {
		err = time_after(jiffies, timeout);
		if (!(saa7146_read(dev, PSR) & SPCI_DEBI_S))
			break;
		saa7146_read(dev, MC2);
		if (err) {
			DEB_S("%s: %s timed out while waiting for transfer completion\n",
			      dev->name, __func__);
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	return 0;
}

static inline int saa7146_wait_for_debi_done_busyloop(struct saa7146_dev *dev,
				unsigned long us1, unsigned long us2)
{
	unsigned long loops;

	/* wait for registers to be programmed */
	loops = us1;
	while (1) {
		if (saa7146_read(dev, MC2) & 2)
			break;
		if (!loops--) {
			pr_err("%s: %s timed out while waiting for registers getting programmed\n",
			       dev->name, __func__);
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	/* wait for transfer to complete */
	loops = us2 / 5;
	while (1) {
		if (!(saa7146_read(dev, PSR) & SPCI_DEBI_S))
			break;
		saa7146_read(dev, MC2);
		if (!loops--) {
			DEB_S("%s: %s timed out while waiting for transfer completion\n",
			      dev->name, __func__);
			return -ETIMEDOUT;
		}
		udelay(5);
	}

	return 0;
}

int saa7146_wait_for_debi_done(struct saa7146_dev *dev, int nobusyloop)
{
	if (nobusyloop)
		return saa7146_wait_for_debi_done_sleep(dev, 50000, 250000);
	else
		return saa7146_wait_for_debi_done_busyloop(dev, 50000, 250000);
}

/****************************************************************************
 * general helper functions
 ****************************************************************************/

/* this is videobuf_vmalloc_to_sg() from videobuf-dma-sg.c
   make sure virt has been allocated with vmalloc_32(), otherwise the BUG()
   may be triggered on highmem machines */
static struct scatterlist* vmalloc_to_sg(unsigned char *virt, int nr_pages)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kcalloc(nr_pages, sizeof(struct scatterlist), GFP_KERNEL);
	if (NULL == sglist)
		return NULL;
	sg_init_table(sglist, nr_pages);
	for (i = 0; i < nr_pages; i++, virt += PAGE_SIZE) {
		pg = vmalloc_to_page(virt);
		if (NULL == pg)
			goto err;
		BUG_ON(PageHighMem(pg));
		sg_set_page(&sglist[i], pg, PAGE_SIZE, 0);
	}
	return sglist;

 err:
	kfree(sglist);
	return NULL;
}

/********************************************************************************/
/* common page table functions */

void *saa7146_vmalloc_build_pgtable(struct pci_dev *pci, long length, struct saa7146_pgtable *pt)
{
	int pages = (length+PAGE_SIZE-1)/PAGE_SIZE;
	void *mem = vmalloc_32(length);
	int slen = 0;

	if (NULL == mem)
		goto err_null;

	if (!(pt->slist = vmalloc_to_sg(mem, pages)))
		goto err_free_mem;

	if (saa7146_pgtable_alloc(pci, pt))
		goto err_free_slist;

	pt->nents = pages;
	slen = pci_map_sg(pci,pt->slist,pt->nents,PCI_DMA_FROMDEVICE);
	if (0 == slen)
		goto err_free_pgtable;

	if (0 != saa7146_pgtable_build_single(pci, pt, pt->slist, slen))
		goto err_unmap_sg;

	return mem;

err_unmap_sg:
	pci_unmap_sg(pci, pt->slist, pt->nents, PCI_DMA_FROMDEVICE);
err_free_pgtable:
	saa7146_pgtable_free(pci, pt);
err_free_slist:
	kfree(pt->slist);
	pt->slist = NULL;
err_free_mem:
	vfree(mem);
err_null:
	return NULL;
}

void saa7146_vfree_destroy_pgtable(struct pci_dev *pci, void *mem, struct saa7146_pgtable *pt)
{
	pci_unmap_sg(pci, pt->slist, pt->nents, PCI_DMA_FROMDEVICE);
	saa7146_pgtable_free(pci, pt);
	kfree(pt->slist);
	pt->slist = NULL;
	vfree(mem);
}

void saa7146_pgtable_free(struct pci_dev *pci, struct saa7146_pgtable *pt)
{
	if (NULL == pt->cpu)
		return;
	pci_free_consistent(pci, pt->size, pt->cpu, pt->dma);
	pt->cpu = NULL;
}

int saa7146_pgtable_alloc(struct pci_dev *pci, struct saa7146_pgtable *pt)
{
	__le32       *cpu;
	dma_addr_t   dma_addr = 0;

	cpu = pci_alloc_consistent(pci, PAGE_SIZE, &dma_addr);
	if (NULL == cpu) {
		return -ENOMEM;
	}
	pt->size = PAGE_SIZE;
	pt->cpu  = cpu;
	pt->dma  = dma_addr;

	return 0;
}

int saa7146_pgtable_build_single(struct pci_dev *pci, struct saa7146_pgtable *pt,
	struct scatterlist *list, int sglen  )
{
	__le32 *ptr, fill;
	int nr_pages = 0;
	int i,p;

	BUG_ON(0 == sglen);
	BUG_ON(list->offset > PAGE_SIZE);

	/* if we have a user buffer, the first page may not be
	   aligned to a page boundary. */
	pt->offset = list->offset;

	ptr = pt->cpu;
	for (i = 0; i < sglen; i++, list++) {
/*
		pr_debug("i:%d, adr:0x%08x, len:%d, offset:%d\n",
			 i, sg_dma_address(list), sg_dma_len(list),
			 list->offset);
*/
		for (p = 0; p * 4096 < list->length; p++, ptr++) {
			*ptr = cpu_to_le32(sg_dma_address(list) + p * 4096);
			nr_pages++;
		}
	}


	/* safety; fill the page table up with the last valid page */
	fill = *(ptr-1);
	for(i=nr_pages;i<1024;i++) {
		*ptr++ = fill;
	}

/*
	ptr = pt->cpu;
	pr_debug("offset: %d\n", pt->offset);
	for(i=0;i<5;i++) {
		pr_debug("ptr1 %d: 0x%08x\n", i, ptr[i]);
	}
*/
	return 0;
}

/********************************************************************************/
/* interrupt handler */
static irqreturn_t interrupt_hw(int irq, void *dev_id)
{
	struct saa7146_dev *dev = dev_id;
	u32 isr;
	u32 ack_isr;

	/* read out the interrupt status register */
	ack_isr = isr = saa7146_read(dev, ISR);

	/* is this our interrupt? */
	if ( 0 == isr ) {
		/* nope, some other device */
		return IRQ_NONE;
	}

	if (dev->ext) {
		if (dev->ext->irq_mask & isr) {
			if (dev->ext->irq_func)
				dev->ext->irq_func(dev, &isr);
			isr &= ~dev->ext->irq_mask;
		}
	}
	if (0 != (isr & (MASK_27))) {
		DEB_INT("irq: RPS0 (0x%08x)\n", isr);
		if (dev->vv_data && dev->vv_callback)
			dev->vv_callback(dev,isr);
		isr &= ~MASK_27;
	}
	if (0 != (isr & (MASK_28))) {
		if (dev->vv_data && dev->vv_callback)
			dev->vv_callback(dev,isr);
		isr &= ~MASK_28;
	}
	if (0 != (isr & (MASK_16|MASK_17))) {
		SAA7146_IER_DISABLE(dev, MASK_16|MASK_17);
		/* only wake up if we expect something */
		if (0 != dev->i2c_op) {
			dev->i2c_op = 0;
			wake_up(&dev->i2c_wq);
		} else {
			u32 psr = saa7146_read(dev, PSR);
			u32 ssr = saa7146_read(dev, SSR);
			pr_warn("%s: unexpected i2c irq: isr %08x psr %08x ssr %08x\n",
				dev->name, isr, psr, ssr);
		}
		isr &= ~(MASK_16|MASK_17);
	}
	if( 0 != isr ) {
		ERR("warning: interrupt enabled, but not handled properly.(0x%08x)\n",
		    isr);
		ERR("disabling interrupt source(s)!\n");
		SAA7146_IER_DISABLE(dev,isr);
	}
	saa7146_write(dev, ISR, ack_isr);
	return IRQ_HANDLED;
}

/*********************************************************************************/
/* configuration-functions                                                       */

static int saa7146_init_one(struct pci_dev *pci, const struct pci_device_id *ent)
{
	struct saa7146_pci_extension_data *pci_ext = (struct saa7146_pci_extension_data *)ent->driver_data;
	struct saa7146_extension *ext = pci_ext->ext;
	struct saa7146_dev *dev;
	int err = -ENOMEM;

	/* clear out mem for sure */
	dev = kzalloc(sizeof(struct saa7146_dev), GFP_KERNEL);
	if (!dev) {
		ERR("out of memory\n");
		goto out;
	}

	DEB_EE("pci:%p\n", pci);

	err = pci_enable_device(pci);
	if (err < 0) {
		ERR("pci_enable_device() failed\n");
		goto err_free;
	}

	/* enable bus-mastering */
	pci_set_master(pci);

	dev->pci = pci;

	/* get chip-revision; this is needed to enable bug-fixes */
	dev->revision = pci->revision;

	/* remap the memory from virtual to physical address */

	err = pci_request_region(pci, 0, "saa7146");
	if (err < 0)
		goto err_disable;

	dev->mem = ioremap(pci_resource_start(pci, 0),
			   pci_resource_len(pci, 0));
	if (!dev->mem) {
		ERR("ioremap() failed\n");
		err = -ENODEV;
		goto err_release;
	}

	/* we don't do a master reset here anymore, it screws up
	   some boards that don't have an i2c-eeprom for configuration
	   values */
/*
	saa7146_write(dev, MC1, MASK_31);
*/

	/* disable all irqs */
	saa7146_write(dev, IER, 0);

	/* shut down all dma transfers and rps tasks */
	saa7146_write(dev, MC1, 0x30ff0000);

	/* clear out any rps-signals pending */
	saa7146_write(dev, MC2, 0xf8000000);

	/* request an interrupt for the saa7146 */
	err = request_irq(pci->irq, interrupt_hw, IRQF_SHARED | IRQF_DISABLED,
			  dev->name, dev);
	if (err < 0) {
		ERR("request_irq() failed\n");
		goto err_unmap;
	}

	err = -ENOMEM;

	/* get memory for various stuff */
	dev->d_rps0.cpu_addr = pci_alloc_consistent(pci, SAA7146_RPS_MEM,
						    &dev->d_rps0.dma_handle);
	if (!dev->d_rps0.cpu_addr)
		goto err_free_irq;
	memset(dev->d_rps0.cpu_addr, 0x0, SAA7146_RPS_MEM);

	dev->d_rps1.cpu_addr = pci_alloc_consistent(pci, SAA7146_RPS_MEM,
						    &dev->d_rps1.dma_handle);
	if (!dev->d_rps1.cpu_addr)
		goto err_free_rps0;
	memset(dev->d_rps1.cpu_addr, 0x0, SAA7146_RPS_MEM);

	dev->d_i2c.cpu_addr = pci_alloc_consistent(pci, SAA7146_RPS_MEM,
						   &dev->d_i2c.dma_handle);
	if (!dev->d_i2c.cpu_addr)
		goto err_free_rps1;
	memset(dev->d_i2c.cpu_addr, 0x0, SAA7146_RPS_MEM);

	/* the rest + print status message */

	/* create a nice device name */
	sprintf(dev->name, "saa7146 (%d)", saa7146_num);

	pr_info("found saa7146 @ mem %p (revision %d, irq %d) (0x%04x,0x%04x)\n",
		dev->mem, dev->revision, pci->irq,
		pci->subsystem_vendor, pci->subsystem_device);
	dev->ext = ext;

	mutex_init(&dev->v4l2_lock);
	spin_lock_init(&dev->int_slock);
	spin_lock_init(&dev->slock);

	mutex_init(&dev->i2c_lock);

	dev->module = THIS_MODULE;
	init_waitqueue_head(&dev->i2c_wq);

	/* set some sane pci arbitrition values */
	saa7146_write(dev, PCI_BT_V1, 0x1c00101f);

	/* TODO: use the status code of the callback */

	err = -ENODEV;

	if (ext->probe && ext->probe(dev)) {
		DEB_D("ext->probe() failed for %p. skipping device.\n", dev);
		goto err_free_i2c;
	}

	if (ext->attach(dev, pci_ext)) {
		DEB_D("ext->attach() failed for %p. skipping device.\n", dev);
		goto err_free_i2c;
	}
	/* V4L extensions will set the pci drvdata to the v4l2_device in the
	   attach() above. So for those cards that do not use V4L we have to
	   set it explicitly. */
	pci_set_drvdata(pci, &dev->v4l2_dev);

	saa7146_num++;

	err = 0;
out:
	return err;

err_free_i2c:
	pci_free_consistent(pci, SAA7146_RPS_MEM, dev->d_i2c.cpu_addr,
			    dev->d_i2c.dma_handle);
err_free_rps1:
	pci_free_consistent(pci, SAA7146_RPS_MEM, dev->d_rps1.cpu_addr,
			    dev->d_rps1.dma_handle);
err_free_rps0:
	pci_free_consistent(pci, SAA7146_RPS_MEM, dev->d_rps0.cpu_addr,
			    dev->d_rps0.dma_handle);
err_free_irq:
	free_irq(pci->irq, (void *)dev);
err_unmap:
	iounmap(dev->mem);
err_release:
	pci_release_region(pci, 0);
err_disable:
	pci_disable_device(pci);
err_free:
	kfree(dev);
	goto out;
}

static void saa7146_remove_one(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct saa7146_dev *dev = to_saa7146_dev(v4l2_dev);
	struct {
		void *addr;
		dma_addr_t dma;
	} dev_map[] = {
		{ dev->d_i2c.cpu_addr, dev->d_i2c.dma_handle },
		{ dev->d_rps1.cpu_addr, dev->d_rps1.dma_handle },
		{ dev->d_rps0.cpu_addr, dev->d_rps0.dma_handle },
		{ NULL, 0 }
	}, *p;

	DEB_EE("dev:%p\n", dev);

	dev->ext->detach(dev);
	/* Zero the PCI drvdata after use. */
	pci_set_drvdata(pdev, NULL);

	/* shut down all video dma transfers */
	saa7146_write(dev, MC1, 0x00ff0000);

	/* disable all irqs, release irq-routine */
	saa7146_write(dev, IER, 0);

	free_irq(pdev->irq, dev);

	for (p = dev_map; p->addr; p++)
		pci_free_consistent(pdev, SAA7146_RPS_MEM, p->addr, p->dma);

	iounmap(dev->mem);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(dev);

	saa7146_num--;
}

/*********************************************************************************/
/* extension handling functions                                                  */

int saa7146_register_extension(struct saa7146_extension* ext)
{
	DEB_EE("ext:%p\n", ext);

	ext->driver.name = ext->name;
	ext->driver.id_table = ext->pci_tbl;
	ext->driver.probe = saa7146_init_one;
	ext->driver.remove = saa7146_remove_one;

	pr_info("register extension '%s'\n", ext->name);
	return pci_register_driver(&ext->driver);
}

int saa7146_unregister_extension(struct saa7146_extension* ext)
{
	DEB_EE("ext:%p\n", ext);
	pr_info("unregister extension '%s'\n", ext->name);
	pci_unregister_driver(&ext->driver);
	return 0;
}

EXPORT_SYMBOL_GPL(saa7146_register_extension);
EXPORT_SYMBOL_GPL(saa7146_unregister_extension);

/* misc functions used by extension modules */
EXPORT_SYMBOL_GPL(saa7146_pgtable_alloc);
EXPORT_SYMBOL_GPL(saa7146_pgtable_free);
EXPORT_SYMBOL_GPL(saa7146_pgtable_build_single);
EXPORT_SYMBOL_GPL(saa7146_vmalloc_build_pgtable);
EXPORT_SYMBOL_GPL(saa7146_vfree_destroy_pgtable);
EXPORT_SYMBOL_GPL(saa7146_wait_for_debi_done);

EXPORT_SYMBOL_GPL(saa7146_setgpio);

EXPORT_SYMBOL_GPL(saa7146_i2c_adapter_prepare);

EXPORT_SYMBOL_GPL(saa7146_debug);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("driver for generic saa7146-based hardware");
MODULE_LICENSE("GPL");

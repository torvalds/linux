/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@gmx.net>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Miscelanous functionality used in the other GenWQE driver parts.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/page-flags.h>
#include <linux/scatterlist.h>
#include <linux/hugetlb.h>
#include <linux/iommu.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <asm/pgtable.h>

#include "genwqe_driver.h"
#include "card_base.h"
#include "card_ddcb.h"

/**
 * __genwqe_writeq() - Write 64-bit register
 * @cd:	        genwqe device descriptor
 * @byte_offs:  byte offset within BAR
 * @val:        64-bit value
 *
 * Return: 0 if success; < 0 if error
 */
int __genwqe_writeq(struct genwqe_dev *cd, u64 byte_offs, u64 val)
{
	struct pci_dev *pci_dev = cd->pci_dev;

	if (cd->err_inject & GENWQE_INJECT_HARDWARE_FAILURE)
		return -EIO;

	if (cd->mmio == NULL)
		return -EIO;

	if (pci_channel_offline(pci_dev))
		return -EIO;

	__raw_writeq((__force u64)cpu_to_be64(val), cd->mmio + byte_offs);
	return 0;
}

/**
 * __genwqe_readq() - Read 64-bit register
 * @cd:         genwqe device descriptor
 * @byte_offs:  offset within BAR
 *
 * Return: value from register
 */
u64 __genwqe_readq(struct genwqe_dev *cd, u64 byte_offs)
{
	if (cd->err_inject & GENWQE_INJECT_HARDWARE_FAILURE)
		return 0xffffffffffffffffull;

	if ((cd->err_inject & GENWQE_INJECT_GFIR_FATAL) &&
	    (byte_offs == IO_SLC_CFGREG_GFIR))
		return 0x000000000000ffffull;

	if ((cd->err_inject & GENWQE_INJECT_GFIR_INFO) &&
	    (byte_offs == IO_SLC_CFGREG_GFIR))
		return 0x00000000ffff0000ull;

	if (cd->mmio == NULL)
		return 0xffffffffffffffffull;

	return be64_to_cpu((__force __be64)__raw_readq(cd->mmio + byte_offs));
}

/**
 * __genwqe_writel() - Write 32-bit register
 * @cd:	        genwqe device descriptor
 * @byte_offs:  byte offset within BAR
 * @val:        32-bit value
 *
 * Return: 0 if success; < 0 if error
 */
int __genwqe_writel(struct genwqe_dev *cd, u64 byte_offs, u32 val)
{
	struct pci_dev *pci_dev = cd->pci_dev;

	if (cd->err_inject & GENWQE_INJECT_HARDWARE_FAILURE)
		return -EIO;

	if (cd->mmio == NULL)
		return -EIO;

	if (pci_channel_offline(pci_dev))
		return -EIO;

	__raw_writel((__force u32)cpu_to_be32(val), cd->mmio + byte_offs);
	return 0;
}

/**
 * __genwqe_readl() - Read 32-bit register
 * @cd:         genwqe device descriptor
 * @byte_offs:  offset within BAR
 *
 * Return: Value from register
 */
u32 __genwqe_readl(struct genwqe_dev *cd, u64 byte_offs)
{
	if (cd->err_inject & GENWQE_INJECT_HARDWARE_FAILURE)
		return 0xffffffff;

	if (cd->mmio == NULL)
		return 0xffffffff;

	return be32_to_cpu((__force __be32)__raw_readl(cd->mmio + byte_offs));
}

/**
 * genwqe_read_app_id() - Extract app_id
 *
 * app_unitcfg need to be filled with valid data first
 */
int genwqe_read_app_id(struct genwqe_dev *cd, char *app_name, int len)
{
	int i, j;
	u32 app_id = (u32)cd->app_unitcfg;

	memset(app_name, 0, len);
	for (i = 0, j = 0; j < min(len, 4); j++) {
		char ch = (char)((app_id >> (24 - j*8)) & 0xff);

		if (ch == ' ')
			continue;
		app_name[i++] = isprint(ch) ? ch : 'X';
	}
	return i;
}

/**
 * genwqe_init_crc32() - Prepare a lookup table for fast crc32 calculations
 *
 * Existing kernel functions seem to use a different polynom,
 * therefore we could not use them here.
 *
 * Genwqe's Polynomial = 0x20044009
 */
#define CRC32_POLYNOMIAL	0x20044009
static u32 crc32_tab[256];	/* crc32 lookup table */

void genwqe_init_crc32(void)
{
	int i, j;
	u32 crc;

	for (i = 0;  i < 256;  i++) {
		crc = i << 24;
		for (j = 0;  j < 8;  j++) {
			if (crc & 0x80000000)
				crc = (crc << 1) ^ CRC32_POLYNOMIAL;
			else
				crc = (crc << 1);
		}
		crc32_tab[i] = crc;
	}
}

/**
 * genwqe_crc32() - Generate 32-bit crc as required for DDCBs
 * @buff:       pointer to data buffer
 * @len:        length of data for calculation
 * @init:       initial crc (0xffffffff at start)
 *
 * polynomial = x^32 * + x^29 + x^18 + x^14 + x^3 + 1 (0x20044009)

 * Example: 4 bytes 0x01 0x02 0x03 0x04 with init=0xffffffff should
 * result in a crc32 of 0xf33cb7d3.
 *
 * The existing kernel crc functions did not cover this polynom yet.
 *
 * Return: crc32 checksum.
 */
u32 genwqe_crc32(u8 *buff, size_t len, u32 init)
{
	int i;
	u32 crc;

	crc = init;
	while (len--) {
		i = ((crc >> 24) ^ *buff++) & 0xFF;
		crc = (crc << 8) ^ crc32_tab[i];
	}
	return crc;
}

void *__genwqe_alloc_consistent(struct genwqe_dev *cd, size_t size,
			       dma_addr_t *dma_handle)
{
	if (get_order(size) > MAX_ORDER)
		return NULL;

	return dma_zalloc_coherent(&cd->pci_dev->dev, size, dma_handle,
				   GFP_KERNEL);
}

void __genwqe_free_consistent(struct genwqe_dev *cd, size_t size,
			     void *vaddr, dma_addr_t dma_handle)
{
	if (vaddr == NULL)
		return;

	dma_free_coherent(&cd->pci_dev->dev, size, vaddr, dma_handle);
}

static void genwqe_unmap_pages(struct genwqe_dev *cd, dma_addr_t *dma_list,
			      int num_pages)
{
	int i;
	struct pci_dev *pci_dev = cd->pci_dev;

	for (i = 0; (i < num_pages) && (dma_list[i] != 0x0); i++) {
		pci_unmap_page(pci_dev, dma_list[i],
			       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		dma_list[i] = 0x0;
	}
}

static int genwqe_map_pages(struct genwqe_dev *cd,
			   struct page **page_list, int num_pages,
			   dma_addr_t *dma_list)
{
	int i;
	struct pci_dev *pci_dev = cd->pci_dev;

	/* establish DMA mapping for requested pages */
	for (i = 0; i < num_pages; i++) {
		dma_addr_t daddr;

		dma_list[i] = 0x0;
		daddr = pci_map_page(pci_dev, page_list[i],
				     0,	 /* map_offs */
				     PAGE_SIZE,
				     PCI_DMA_BIDIRECTIONAL);  /* FIXME rd/rw */

		if (pci_dma_mapping_error(pci_dev, daddr)) {
			dev_err(&pci_dev->dev,
				"[%s] err: no dma addr daddr=%016llx!\n",
				__func__, (long long)daddr);
			goto err;
		}

		dma_list[i] = daddr;
	}
	return 0;

 err:
	genwqe_unmap_pages(cd, dma_list, num_pages);
	return -EIO;
}

static int genwqe_sgl_size(int num_pages)
{
	int len, num_tlb = num_pages / 7;

	len = sizeof(struct sg_entry) * (num_pages+num_tlb + 1);
	return roundup(len, PAGE_SIZE);
}

/**
 * genwqe_alloc_sync_sgl() - Allocate memory for sgl and overlapping pages
 *
 * Allocates memory for sgl and overlapping pages. Pages which might
 * overlap other user-space memory blocks are being cached for DMAs,
 * such that we do not run into syncronization issues. Data is copied
 * from user-space into the cached pages.
 */
int genwqe_alloc_sync_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl,
			  void __user *user_addr, size_t user_size, int write)
{
	int rc;
	struct pci_dev *pci_dev = cd->pci_dev;

	sgl->fpage_offs = offset_in_page((unsigned long)user_addr);
	sgl->fpage_size = min_t(size_t, PAGE_SIZE-sgl->fpage_offs, user_size);
	sgl->nr_pages = DIV_ROUND_UP(sgl->fpage_offs + user_size, PAGE_SIZE);
	sgl->lpage_size = (user_size - sgl->fpage_size) % PAGE_SIZE;

	dev_dbg(&pci_dev->dev, "[%s] uaddr=%p usize=%8ld nr_pages=%ld fpage_offs=%lx fpage_size=%ld lpage_size=%ld\n",
		__func__, user_addr, user_size, sgl->nr_pages,
		sgl->fpage_offs, sgl->fpage_size, sgl->lpage_size);

	sgl->user_addr = user_addr;
	sgl->user_size = user_size;
	sgl->write = write;
	sgl->sgl_size = genwqe_sgl_size(sgl->nr_pages);

	if (get_order(sgl->sgl_size) > MAX_ORDER) {
		dev_err(&pci_dev->dev,
			"[%s] err: too much memory requested!\n", __func__);
		return -ENOMEM;
	}

	sgl->sgl = __genwqe_alloc_consistent(cd, sgl->sgl_size,
					     &sgl->sgl_dma_addr);
	if (sgl->sgl == NULL) {
		dev_err(&pci_dev->dev,
			"[%s] err: no memory available!\n", __func__);
		return -ENOMEM;
	}

	/* Only use buffering on incomplete pages */
	if ((sgl->fpage_size != 0) && (sgl->fpage_size != PAGE_SIZE)) {
		sgl->fpage = __genwqe_alloc_consistent(cd, PAGE_SIZE,
						       &sgl->fpage_dma_addr);
		if (sgl->fpage == NULL)
			goto err_out;

		/* Sync with user memory */
		if (copy_from_user(sgl->fpage + sgl->fpage_offs,
				   user_addr, sgl->fpage_size)) {
			rc = -EFAULT;
			goto err_out;
		}
	}
	if (sgl->lpage_size != 0) {
		sgl->lpage = __genwqe_alloc_consistent(cd, PAGE_SIZE,
						       &sgl->lpage_dma_addr);
		if (sgl->lpage == NULL)
			goto err_out1;

		/* Sync with user memory */
		if (copy_from_user(sgl->lpage, user_addr + user_size -
				   sgl->lpage_size, sgl->lpage_size)) {
			rc = -EFAULT;
			goto err_out2;
		}
	}
	return 0;

 err_out2:
	__genwqe_free_consistent(cd, PAGE_SIZE, sgl->lpage,
				 sgl->lpage_dma_addr);
	sgl->lpage = NULL;
	sgl->lpage_dma_addr = 0;
 err_out1:
	__genwqe_free_consistent(cd, PAGE_SIZE, sgl->fpage,
				 sgl->fpage_dma_addr);
	sgl->fpage = NULL;
	sgl->fpage_dma_addr = 0;
 err_out:
	__genwqe_free_consistent(cd, sgl->sgl_size, sgl->sgl,
				 sgl->sgl_dma_addr);
	sgl->sgl = NULL;
	sgl->sgl_dma_addr = 0;
	sgl->sgl_size = 0;
	return -ENOMEM;
}

int genwqe_setup_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl,
		     dma_addr_t *dma_list)
{
	int i = 0, j = 0, p;
	unsigned long dma_offs, map_offs;
	dma_addr_t prev_daddr = 0;
	struct sg_entry *s, *last_s = NULL;
	size_t size = sgl->user_size;

	dma_offs = 128;		/* next block if needed/dma_offset */
	map_offs = sgl->fpage_offs; /* offset in first page */

	s = &sgl->sgl[0];	/* first set of 8 entries */
	p = 0;			/* page */
	while (p < sgl->nr_pages) {
		dma_addr_t daddr;
		unsigned int size_to_map;

		/* always write the chaining entry, cleanup is done later */
		j = 0;
		s[j].target_addr = cpu_to_be64(sgl->sgl_dma_addr + dma_offs);
		s[j].len	 = cpu_to_be32(128);
		s[j].flags	 = cpu_to_be32(SG_CHAINED);
		j++;

		while (j < 8) {
			/* DMA mapping for requested page, offs, size */
			size_to_map = min(size, PAGE_SIZE - map_offs);

			if ((p == 0) && (sgl->fpage != NULL)) {
				daddr = sgl->fpage_dma_addr + map_offs;

			} else if ((p == sgl->nr_pages - 1) &&
				   (sgl->lpage != NULL)) {
				daddr = sgl->lpage_dma_addr;
			} else {
				daddr = dma_list[p] + map_offs;
			}

			size -= size_to_map;
			map_offs = 0;

			if (prev_daddr == daddr) {
				u32 prev_len = be32_to_cpu(last_s->len);

				/* pr_info("daddr combining: "
					"%016llx/%08x -> %016llx\n",
					prev_daddr, prev_len, daddr); */

				last_s->len = cpu_to_be32(prev_len +
							  size_to_map);

				p++; /* process next page */
				if (p == sgl->nr_pages)
					goto fixup;  /* nothing to do */

				prev_daddr = daddr + size_to_map;
				continue;
			}

			/* start new entry */
			s[j].target_addr = cpu_to_be64(daddr);
			s[j].len	 = cpu_to_be32(size_to_map);
			s[j].flags	 = cpu_to_be32(SG_DATA);
			prev_daddr = daddr + size_to_map;
			last_s = &s[j];
			j++;

			p++;	/* process next page */
			if (p == sgl->nr_pages)
				goto fixup;  /* nothing to do */
		}
		dma_offs += 128;
		s += 8;		/* continue 8 elements further */
	}
 fixup:
	if (j == 1) {		/* combining happened on last entry! */
		s -= 8;		/* full shift needed on previous sgl block */
		j =  7;		/* shift all elements */
	}

	for (i = 0; i < j; i++)	/* move elements 1 up */
		s[i] = s[i + 1];

	s[i].target_addr = cpu_to_be64(0);
	s[i].len	 = cpu_to_be32(0);
	s[i].flags	 = cpu_to_be32(SG_END_LIST);
	return 0;
}

/**
 * genwqe_free_sync_sgl() - Free memory for sgl and overlapping pages
 *
 * After the DMA transfer has been completed we free the memory for
 * the sgl and the cached pages. Data is being transferred from cached
 * pages into user-space buffers.
 */
int genwqe_free_sync_sgl(struct genwqe_dev *cd, struct genwqe_sgl *sgl)
{
	int rc = 0;
	size_t offset;
	unsigned long res;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (sgl->fpage) {
		if (sgl->write) {
			res = copy_to_user(sgl->user_addr,
				sgl->fpage + sgl->fpage_offs, sgl->fpage_size);
			if (res) {
				dev_err(&pci_dev->dev,
					"[%s] err: copying fpage! (res=%lu)\n",
					__func__, res);
				rc = -EFAULT;
			}
		}
		__genwqe_free_consistent(cd, PAGE_SIZE, sgl->fpage,
					 sgl->fpage_dma_addr);
		sgl->fpage = NULL;
		sgl->fpage_dma_addr = 0;
	}
	if (sgl->lpage) {
		if (sgl->write) {
			offset = sgl->user_size - sgl->lpage_size;
			res = copy_to_user(sgl->user_addr + offset, sgl->lpage,
					   sgl->lpage_size);
			if (res) {
				dev_err(&pci_dev->dev,
					"[%s] err: copying lpage! (res=%lu)\n",
					__func__, res);
				rc = -EFAULT;
			}
		}
		__genwqe_free_consistent(cd, PAGE_SIZE, sgl->lpage,
					 sgl->lpage_dma_addr);
		sgl->lpage = NULL;
		sgl->lpage_dma_addr = 0;
	}
	__genwqe_free_consistent(cd, sgl->sgl_size, sgl->sgl,
				 sgl->sgl_dma_addr);

	sgl->sgl = NULL;
	sgl->sgl_dma_addr = 0x0;
	sgl->sgl_size = 0;
	return rc;
}

/**
 * genwqe_free_user_pages() - Give pinned pages back
 *
 * Documentation of get_user_pages is in mm/gup.c:
 *
 * If the page is written to, set_page_dirty (or set_page_dirty_lock,
 * as appropriate) must be called after the page is finished with, and
 * before put_page is called.
 */
static int genwqe_free_user_pages(struct page **page_list,
			unsigned int nr_pages, int dirty)
{
	unsigned int i;

	for (i = 0; i < nr_pages; i++) {
		if (page_list[i] != NULL) {
			if (dirty)
				set_page_dirty_lock(page_list[i]);
			put_page(page_list[i]);
		}
	}
	return 0;
}

/**
 * genwqe_user_vmap() - Map user-space memory to virtual kernel memory
 * @cd:         pointer to genwqe device
 * @m:          mapping params
 * @uaddr:      user virtual address
 * @size:       size of memory to be mapped
 *
 * We need to think about how we could speed this up. Of course it is
 * not a good idea to do this over and over again, like we are
 * currently doing it. Nevertheless, I am curious where on the path
 * the performance is spend. Most probably within the memory
 * allocation functions, but maybe also in the DMA mapping code.
 *
 * Restrictions: The maximum size of the possible mapping currently depends
 *               on the amount of memory we can get using kzalloc() for the
 *               page_list and pci_alloc_consistent for the sg_list.
 *               The sg_list is currently itself not scattered, which could
 *               be fixed with some effort. The page_list must be split into
 *               PAGE_SIZE chunks too. All that will make the complicated
 *               code more complicated.
 *
 * Return: 0 if success
 */
int genwqe_user_vmap(struct genwqe_dev *cd, struct dma_mapping *m, void *uaddr,
		     unsigned long size)
{
	int rc = -EINVAL;
	unsigned long data, offs;
	struct pci_dev *pci_dev = cd->pci_dev;

	if ((uaddr == NULL) || (size == 0)) {
		m->size = 0;	/* mark unused and not added */
		return -EINVAL;
	}
	m->u_vaddr = uaddr;
	m->size    = size;

	/* determine space needed for page_list. */
	data = (unsigned long)uaddr;
	offs = offset_in_page(data);
	m->nr_pages = DIV_ROUND_UP(offs + size, PAGE_SIZE);

	m->page_list = kcalloc(m->nr_pages,
			       sizeof(struct page *) + sizeof(dma_addr_t),
			       GFP_KERNEL);
	if (!m->page_list) {
		dev_err(&pci_dev->dev, "err: alloc page_list failed\n");
		m->nr_pages = 0;
		m->u_vaddr = NULL;
		m->size = 0;	/* mark unused and not added */
		return -ENOMEM;
	}
	m->dma_list = (dma_addr_t *)(m->page_list + m->nr_pages);

	/* pin user pages in memory */
	rc = get_user_pages_fast(data & PAGE_MASK, /* page aligned addr */
				 m->nr_pages,
				 m->write,		/* readable/writable */
				 m->page_list);	/* ptrs to pages */
	if (rc < 0)
		goto fail_get_user_pages;

	/* assumption: get_user_pages can be killed by signals. */
	if (rc < m->nr_pages) {
		genwqe_free_user_pages(m->page_list, rc, m->write);
		rc = -EFAULT;
		goto fail_get_user_pages;
	}

	rc = genwqe_map_pages(cd, m->page_list, m->nr_pages, m->dma_list);
	if (rc != 0)
		goto fail_free_user_pages;

	return 0;

 fail_free_user_pages:
	genwqe_free_user_pages(m->page_list, m->nr_pages, m->write);

 fail_get_user_pages:
	kfree(m->page_list);
	m->page_list = NULL;
	m->dma_list = NULL;
	m->nr_pages = 0;
	m->u_vaddr = NULL;
	m->size = 0;		/* mark unused and not added */
	return rc;
}

/**
 * genwqe_user_vunmap() - Undo mapping of user-space mem to virtual kernel
 *                        memory
 * @cd:         pointer to genwqe device
 * @m:          mapping params
 */
int genwqe_user_vunmap(struct genwqe_dev *cd, struct dma_mapping *m)
{
	struct pci_dev *pci_dev = cd->pci_dev;

	if (!dma_mapping_used(m)) {
		dev_err(&pci_dev->dev, "[%s] err: mapping %p not used!\n",
			__func__, m);
		return -EINVAL;
	}

	if (m->dma_list)
		genwqe_unmap_pages(cd, m->dma_list, m->nr_pages);

	if (m->page_list) {
		genwqe_free_user_pages(m->page_list, m->nr_pages, m->write);

		kfree(m->page_list);
		m->page_list = NULL;
		m->dma_list = NULL;
		m->nr_pages = 0;
	}

	m->u_vaddr = NULL;
	m->size = 0;		/* mark as unused and not added */
	return 0;
}

/**
 * genwqe_card_type() - Get chip type SLU Configuration Register
 * @cd:         pointer to the genwqe device descriptor
 * Return: 0: Altera Stratix-IV 230
 *         1: Altera Stratix-IV 530
 *         2: Altera Stratix-V A4
 *         3: Altera Stratix-V A7
 */
u8 genwqe_card_type(struct genwqe_dev *cd)
{
	u64 card_type = cd->slu_unitcfg;

	return (u8)((card_type & IO_SLU_UNITCFG_TYPE_MASK) >> 20);
}

/**
 * genwqe_card_reset() - Reset the card
 * @cd:         pointer to the genwqe device descriptor
 */
int genwqe_card_reset(struct genwqe_dev *cd)
{
	u64 softrst;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (!genwqe_is_privileged(cd))
		return -ENODEV;

	/* new SL */
	__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET, 0x1ull);
	msleep(1000);
	__genwqe_readq(cd, IO_HSU_FIR_CLR);
	__genwqe_readq(cd, IO_APP_FIR_CLR);
	__genwqe_readq(cd, IO_SLU_FIR_CLR);

	/*
	 * Read-modify-write to preserve the stealth bits
	 *
	 * For SL >= 039, Stealth WE bit allows removing
	 * the read-modify-wrote.
	 * r-m-w may require a mask 0x3C to avoid hitting hard
	 * reset again for error reset (should be 0, chicken).
	 */
	softrst = __genwqe_readq(cd, IO_SLC_CFGREG_SOFTRESET) & 0x3cull;
	__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET, softrst | 0x2ull);

	/* give ERRORRESET some time to finish */
	msleep(50);

	if (genwqe_need_err_masking(cd)) {
		dev_info(&pci_dev->dev,
			 "[%s] masking errors for old bitstreams\n", __func__);
		__genwqe_writeq(cd, IO_SLC_MISC_DEBUG, 0x0aull);
	}
	return 0;
}

int genwqe_read_softreset(struct genwqe_dev *cd)
{
	u64 bitstream;

	if (!genwqe_is_privileged(cd))
		return -ENODEV;

	bitstream = __genwqe_readq(cd, IO_SLU_BITSTREAM) & 0x1;
	cd->softreset = (bitstream == 0) ? 0x8ull : 0xcull;
	return 0;
}

/**
 * genwqe_set_interrupt_capability() - Configure MSI capability structure
 * @cd:         pointer to the device
 * Return: 0 if no error
 */
int genwqe_set_interrupt_capability(struct genwqe_dev *cd, int count)
{
	int rc;

	rc = pci_alloc_irq_vectors(cd->pci_dev, 1, count, PCI_IRQ_MSI);
	if (rc < 0)
		return rc;
	return 0;
}

/**
 * genwqe_reset_interrupt_capability() - Undo genwqe_set_interrupt_capability()
 * @cd:         pointer to the device
 */
void genwqe_reset_interrupt_capability(struct genwqe_dev *cd)
{
	pci_free_irq_vectors(cd->pci_dev);
}

/**
 * set_reg_idx() - Fill array with data. Ignore illegal offsets.
 * @cd:         card device
 * @r:          debug register array
 * @i:          index to desired entry
 * @m:          maximum possible entries
 * @addr:       addr which is read
 * @index:      index in debug array
 * @val:        read value
 */
static int set_reg_idx(struct genwqe_dev *cd, struct genwqe_reg *r,
		       unsigned int *i, unsigned int m, u32 addr, u32 idx,
		       u64 val)
{
	if (WARN_ON_ONCE(*i >= m))
		return -EFAULT;

	r[*i].addr = addr;
	r[*i].idx = idx;
	r[*i].val = val;
	++*i;
	return 0;
}

static int set_reg(struct genwqe_dev *cd, struct genwqe_reg *r,
		   unsigned int *i, unsigned int m, u32 addr, u64 val)
{
	return set_reg_idx(cd, r, i, m, addr, 0, val);
}

int genwqe_read_ffdc_regs(struct genwqe_dev *cd, struct genwqe_reg *regs,
			 unsigned int max_regs, int all)
{
	unsigned int i, j, idx = 0;
	u32 ufir_addr, ufec_addr, sfir_addr, sfec_addr;
	u64 gfir, sluid, appid, ufir, ufec, sfir, sfec;

	/* Global FIR */
	gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
	set_reg(cd, regs, &idx, max_regs, IO_SLC_CFGREG_GFIR, gfir);

	/* UnitCfg for SLU */
	sluid = __genwqe_readq(cd, IO_SLU_UNITCFG); /* 0x00000000 */
	set_reg(cd, regs, &idx, max_regs, IO_SLU_UNITCFG, sluid);

	/* UnitCfg for APP */
	appid = __genwqe_readq(cd, IO_APP_UNITCFG); /* 0x02000000 */
	set_reg(cd, regs, &idx, max_regs, IO_APP_UNITCFG, appid);

	/* Check all chip Units */
	for (i = 0; i < GENWQE_MAX_UNITS; i++) {

		/* Unit FIR */
		ufir_addr = (i << 24) | 0x008;
		ufir = __genwqe_readq(cd, ufir_addr);
		set_reg(cd, regs, &idx, max_regs, ufir_addr, ufir);

		/* Unit FEC */
		ufec_addr = (i << 24) | 0x018;
		ufec = __genwqe_readq(cd, ufec_addr);
		set_reg(cd, regs, &idx, max_regs, ufec_addr, ufec);

		for (j = 0; j < 64; j++) {
			/* wherever there is a primary 1, read the 2ndary */
			if (!all && (!(ufir & (1ull << j))))
				continue;

			sfir_addr = (i << 24) | (0x100 + 8 * j);
			sfir = __genwqe_readq(cd, sfir_addr);
			set_reg(cd, regs, &idx, max_regs, sfir_addr, sfir);

			sfec_addr = (i << 24) | (0x300 + 8 * j);
			sfec = __genwqe_readq(cd, sfec_addr);
			set_reg(cd, regs, &idx, max_regs, sfec_addr, sfec);
		}
	}

	/* fill with invalid data until end */
	for (i = idx; i < max_regs; i++) {
		regs[i].addr = 0xffffffff;
		regs[i].val = 0xffffffffffffffffull;
	}
	return idx;
}

/**
 * genwqe_ffdc_buff_size() - Calculates the number of dump registers
 */
int genwqe_ffdc_buff_size(struct genwqe_dev *cd, int uid)
{
	int entries = 0, ring, traps, traces, trace_entries;
	u32 eevptr_addr, l_addr, d_len, d_type;
	u64 eevptr, val, addr;

	eevptr_addr = GENWQE_UID_OFFS(uid) | IO_EXTENDED_ERROR_POINTER;
	eevptr = __genwqe_readq(cd, eevptr_addr);

	if ((eevptr != 0x0) && (eevptr != -1ull)) {
		l_addr = GENWQE_UID_OFFS(uid) | eevptr;

		while (1) {
			val = __genwqe_readq(cd, l_addr);

			if ((val == 0x0) || (val == -1ull))
				break;

			/* 38:24 */
			d_len  = (val & 0x0000007fff000000ull) >> 24;

			/* 39 */
			d_type = (val & 0x0000008000000000ull) >> 36;

			if (d_type) {	/* repeat */
				entries += d_len;
			} else {	/* size in bytes! */
				entries += d_len >> 3;
			}

			l_addr += 8;
		}
	}

	for (ring = 0; ring < 8; ring++) {
		addr = GENWQE_UID_OFFS(uid) | IO_EXTENDED_DIAG_MAP(ring);
		val = __genwqe_readq(cd, addr);

		if ((val == 0x0ull) || (val == -1ull))
			continue;

		traps = (val >> 24) & 0xff;
		traces = (val >> 16) & 0xff;
		trace_entries = val & 0xffff;

		entries += traps + (traces * trace_entries);
	}
	return entries;
}

/**
 * genwqe_ffdc_buff_read() - Implements LogoutExtendedErrorRegisters procedure
 */
int genwqe_ffdc_buff_read(struct genwqe_dev *cd, int uid,
			  struct genwqe_reg *regs, unsigned int max_regs)
{
	int i, traps, traces, trace, trace_entries, trace_entry, ring;
	unsigned int idx = 0;
	u32 eevptr_addr, l_addr, d_addr, d_len, d_type;
	u64 eevptr, e, val, addr;

	eevptr_addr = GENWQE_UID_OFFS(uid) | IO_EXTENDED_ERROR_POINTER;
	eevptr = __genwqe_readq(cd, eevptr_addr);

	if ((eevptr != 0x0) && (eevptr != 0xffffffffffffffffull)) {
		l_addr = GENWQE_UID_OFFS(uid) | eevptr;
		while (1) {
			e = __genwqe_readq(cd, l_addr);
			if ((e == 0x0) || (e == 0xffffffffffffffffull))
				break;

			d_addr = (e & 0x0000000000ffffffull);	    /* 23:0 */
			d_len  = (e & 0x0000007fff000000ull) >> 24; /* 38:24 */
			d_type = (e & 0x0000008000000000ull) >> 36; /* 39 */
			d_addr |= GENWQE_UID_OFFS(uid);

			if (d_type) {
				for (i = 0; i < (int)d_len; i++) {
					val = __genwqe_readq(cd, d_addr);
					set_reg_idx(cd, regs, &idx, max_regs,
						    d_addr, i, val);
				}
			} else {
				d_len >>= 3; /* Size in bytes! */
				for (i = 0; i < (int)d_len; i++, d_addr += 8) {
					val = __genwqe_readq(cd, d_addr);
					set_reg_idx(cd, regs, &idx, max_regs,
						    d_addr, 0, val);
				}
			}
			l_addr += 8;
		}
	}

	/*
	 * To save time, there are only 6 traces poplulated on Uid=2,
	 * Ring=1. each with iters=512.
	 */
	for (ring = 0; ring < 8; ring++) { /* 0 is fls, 1 is fds,
					      2...7 are ASI rings */
		addr = GENWQE_UID_OFFS(uid) | IO_EXTENDED_DIAG_MAP(ring);
		val = __genwqe_readq(cd, addr);

		if ((val == 0x0ull) || (val == -1ull))
			continue;

		traps = (val >> 24) & 0xff;	/* Number of Traps	*/
		traces = (val >> 16) & 0xff;	/* Number of Traces	*/
		trace_entries = val & 0xffff;	/* Entries per trace	*/

		/* Note: This is a combined loop that dumps both the traps */
		/* (for the trace == 0 case) as well as the traces 1 to    */
		/* 'traces'.						   */
		for (trace = 0; trace <= traces; trace++) {
			u32 diag_sel =
				GENWQE_EXTENDED_DIAG_SELECTOR(ring, trace);

			addr = (GENWQE_UID_OFFS(uid) |
				IO_EXTENDED_DIAG_SELECTOR);
			__genwqe_writeq(cd, addr, diag_sel);

			for (trace_entry = 0;
			     trace_entry < (trace ? trace_entries : traps);
			     trace_entry++) {
				addr = (GENWQE_UID_OFFS(uid) |
					IO_EXTENDED_DIAG_READ_MBX);
				val = __genwqe_readq(cd, addr);
				set_reg_idx(cd, regs, &idx, max_regs, addr,
					    (diag_sel<<16) | trace_entry, val);
			}
		}
	}
	return 0;
}

/**
 * genwqe_write_vreg() - Write register in virtual window
 *
 * Note, these registers are only accessible to the PF through the
 * VF-window. It is not intended for the VF to access.
 */
int genwqe_write_vreg(struct genwqe_dev *cd, u32 reg, u64 val, int func)
{
	__genwqe_writeq(cd, IO_PF_SLC_VIRTUAL_WINDOW, func & 0xf);
	__genwqe_writeq(cd, reg, val);
	return 0;
}

/**
 * genwqe_read_vreg() - Read register in virtual window
 *
 * Note, these registers are only accessible to the PF through the
 * VF-window. It is not intended for the VF to access.
 */
u64 genwqe_read_vreg(struct genwqe_dev *cd, u32 reg, int func)
{
	__genwqe_writeq(cd, IO_PF_SLC_VIRTUAL_WINDOW, func & 0xf);
	return __genwqe_readq(cd, reg);
}

/**
 * genwqe_base_clock_frequency() - Deteremine base clock frequency of the card
 *
 * Note: From a design perspective it turned out to be a bad idea to
 * use codes here to specifiy the frequency/speed values. An old
 * driver cannot understand new codes and is therefore always a
 * problem. Better is to measure out the value or put the
 * speed/frequency directly into a register which is always a valid
 * value for old as well as for new software.
 *
 * Return: Card clock in MHz
 */
int genwqe_base_clock_frequency(struct genwqe_dev *cd)
{
	u16 speed;		/*         MHz  MHz  MHz  MHz */
	static const int speed_grade[] = { 250, 200, 166, 175 };

	speed = (u16)((cd->slu_unitcfg >> 28) & 0x0full);
	if (speed >= ARRAY_SIZE(speed_grade))
		return 0;	/* illegal value */

	return speed_grade[speed];
}

/**
 * genwqe_stop_traps() - Stop traps
 *
 * Before reading out the analysis data, we need to stop the traps.
 */
void genwqe_stop_traps(struct genwqe_dev *cd)
{
	__genwqe_writeq(cd, IO_SLC_MISC_DEBUG_SET, 0xcull);
}

/**
 * genwqe_start_traps() - Start traps
 *
 * After having read the data, we can/must enable the traps again.
 */
void genwqe_start_traps(struct genwqe_dev *cd)
{
	__genwqe_writeq(cd, IO_SLC_MISC_DEBUG_CLR, 0xcull);

	if (genwqe_need_err_masking(cd))
		__genwqe_writeq(cd, IO_SLC_MISC_DEBUG, 0x0aull);
}

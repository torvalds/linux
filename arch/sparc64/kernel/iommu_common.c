/* $Id: iommu_common.c,v 1.9 2001/12/17 07:05:09 davem Exp $
 * iommu_common.c: UltraSparc SBUS/PCI common iommu code.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include "iommu_common.h"

/* You are _strongly_ advised to enable the following debugging code
 * any time you make changes to the sg code below, run it for a while
 * with filesystems mounted read-only before buying the farm... -DaveM
 */

#ifdef VERIFY_SG
static int verify_lengths(struct scatterlist *sg, int nents, int npages)
{
	int sg_len, dma_len;
	int i, pgcount;

	sg_len = 0;
	for (i = 0; i < nents; i++)
		sg_len += sg[i].length;

	dma_len = 0;
	for (i = 0; i < nents && sg[i].dma_length; i++)
		dma_len += sg[i].dma_length;

	if (sg_len != dma_len) {
		printk("verify_lengths: Error, different, sg[%d] dma[%d]\n",
		       sg_len, dma_len);
		return -1;
	}

	pgcount = 0;
	for (i = 0; i < nents && sg[i].dma_length; i++) {
		unsigned long start, end;

		start = sg[i].dma_address;
		start = start & IO_PAGE_MASK;

		end = sg[i].dma_address + sg[i].dma_length;
		end = (end + (IO_PAGE_SIZE - 1)) & IO_PAGE_MASK;

		pgcount += ((end - start) >> IO_PAGE_SHIFT);
	}

	if (pgcount != npages) {
		printk("verify_lengths: Error, page count wrong, "
		       "npages[%d] pgcount[%d]\n",
		       npages, pgcount);
		return -1;
	}

	/* This test passes... */
	return 0;
}

static int verify_one_map(struct scatterlist *dma_sg, struct scatterlist **__sg, int nents, iopte_t **__iopte)
{
	struct scatterlist *sg = *__sg;
	iopte_t *iopte = *__iopte;
	u32 dlen = dma_sg->dma_length;
	u32 daddr;
	unsigned int sglen;
	unsigned long sgaddr;

	daddr = dma_sg->dma_address;
	sglen = sg->length;
	sgaddr = (unsigned long) (page_address(sg->page) + sg->offset);
	while (dlen > 0) {
		unsigned long paddr;

		/* SG and DMA_SG must begin at the same sub-page boundary. */
		if ((sgaddr & ~IO_PAGE_MASK) != (daddr & ~IO_PAGE_MASK)) {
			printk("verify_one_map: Wrong start offset "
			       "sg[%08lx] dma[%08x]\n",
			       sgaddr, daddr);
			nents = -1;
			goto out;
		}

		/* Verify the IOPTE points to the right page. */
		paddr = iopte_val(*iopte) & IOPTE_PAGE;
		if ((paddr + PAGE_OFFSET) != (sgaddr & IO_PAGE_MASK)) {
			printk("verify_one_map: IOPTE[%08lx] maps the "
			       "wrong page, should be [%08lx]\n",
			       iopte_val(*iopte), (sgaddr & IO_PAGE_MASK) - PAGE_OFFSET);
			nents = -1;
			goto out;
		}

		/* If this SG crosses a page, adjust to that next page
		 * boundary and loop.
		 */
		if ((sgaddr & IO_PAGE_MASK) ^ ((sgaddr + sglen - 1) & IO_PAGE_MASK)) {
			unsigned long next_page, diff;

			next_page = (sgaddr + IO_PAGE_SIZE) & IO_PAGE_MASK;
			diff = next_page - sgaddr;
			sgaddr += diff;
			daddr += diff;
			sglen -= diff;
			dlen -= diff;
			if (dlen > 0)
				iopte++;
			continue;
		}

		/* SG wholly consumed within this page. */
		daddr += sglen;
		dlen -= sglen;

		if (dlen > 0 && ((daddr & ~IO_PAGE_MASK) == 0))
			iopte++;

		sg++;
		if (--nents <= 0)
			break;
		sgaddr = (unsigned long) (page_address(sg->page) + sg->offset);
		sglen = sg->length;
	}
	if (dlen < 0) {
		/* Transfer overrun, big problems. */
		printk("verify_one_map: Transfer overrun by %d bytes.\n",
		       -dlen);
		nents = -1;
	} else {
		/* Advance to next dma_sg implies that the next iopte will
		 * begin it.
		 */
		iopte++;
	}

out:
	*__sg = sg;
	*__iopte = iopte;
	return nents;
}

static int verify_maps(struct scatterlist *sg, int nents, iopte_t *iopte)
{
	struct scatterlist *dma_sg = sg;
	struct scatterlist *orig_dma_sg = dma_sg;
	int orig_nents = nents;

	for (;;) {
		nents = verify_one_map(dma_sg, &sg, nents, &iopte);
		if (nents <= 0)
			break;
		dma_sg++;
		if (dma_sg->dma_length == 0)
			break;
	}

	if (nents > 0) {
		printk("verify_maps: dma maps consumed by some sgs remain (%d)\n",
		       nents);
		return -1;
	}

	if (nents < 0) {
		printk("verify_maps: Error, messed up mappings, "
		       "at sg %d dma_sg %d\n",
		       (int) (orig_nents + nents), (int) (dma_sg - orig_dma_sg));
		return -1;
	}

	/* This test passes... */
	return 0;
}

void verify_sglist(struct scatterlist *sg, int nents, iopte_t *iopte, int npages)
{
	if (verify_lengths(sg, nents, npages) < 0 ||
	    verify_maps(sg, nents, iopte) < 0) {
		int i;

		printk("verify_sglist: Crap, messed up mappings, dumping, iodma at ");
		printk("%016lx.\n", sg->dma_address & IO_PAGE_MASK);

		for (i = 0; i < nents; i++) {
			printk("sg(%d): page_addr(%p) off(%x) length(%x) "
			       "dma_address[%016lx] dma_length[%016lx]\n",
			       i,
			       page_address(sg[i].page), sg[i].offset,
			       sg[i].length,
			       sg[i].dma_address, sg[i].dma_length);
		}
	}

	/* Seems to be ok */
}
#endif

unsigned long prepare_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *dma_sg = sg;
	unsigned long prev;
	u32 dent_addr, dent_len;

	prev  = (unsigned long) (page_address(sg->page) + sg->offset);
	prev += (unsigned long) (dent_len = sg->length);
	dent_addr = (u32) ((unsigned long)(page_address(sg->page) + sg->offset)
			   & (IO_PAGE_SIZE - 1UL));
	while (--nents) {
		unsigned long addr;

		sg++;
		addr = (unsigned long) (page_address(sg->page) + sg->offset);
		if (! VCONTIG(prev, addr)) {
			dma_sg->dma_address = dent_addr;
			dma_sg->dma_length = dent_len;
			dma_sg++;

			dent_addr = ((dent_addr +
				      dent_len +
				      (IO_PAGE_SIZE - 1UL)) >> IO_PAGE_SHIFT);
			dent_addr <<= IO_PAGE_SHIFT;
			dent_addr += addr & (IO_PAGE_SIZE - 1UL);
			dent_len = 0;
		}
		dent_len += sg->length;
		prev = addr + sg->length;
	}
	dma_sg->dma_address = dent_addr;
	dma_sg->dma_length = dent_len;

	return ((unsigned long) dent_addr +
		(unsigned long) dent_len +
		(IO_PAGE_SIZE - 1UL)) >> IO_PAGE_SHIFT;
}

/* psycho_common.c: Code common to PSYCHO and derivative PCI controllers.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>

#include <asm/upa.h>

#include "pci_impl.h"
#include "psycho_common.h"

#define PSYCHO_IOMMU_TAG		0xa580UL
#define PSYCHO_IOMMU_DATA		0xa600UL

static void psycho_iommu_flush(struct pci_pbm_info *pbm)
{
	int i;

	for (i = 0; i < 16; i++) {
		unsigned long off = i * 8;

		upa_writeq(0, pbm->controller_regs + PSYCHO_IOMMU_TAG + off);
		upa_writeq(0, pbm->controller_regs + PSYCHO_IOMMU_DATA + off);
	}
}

#define PSYCHO_IOMMU_CONTROL		0x0200UL
#define  PSYCHO_IOMMU_CTRL_TSBSZ	0x0000000000070000UL
#define  PSYCHO_IOMMU_TSBSZ_1K      	0x0000000000000000UL
#define  PSYCHO_IOMMU_TSBSZ_2K      	0x0000000000010000UL
#define  PSYCHO_IOMMU_TSBSZ_4K      	0x0000000000020000UL
#define  PSYCHO_IOMMU_TSBSZ_8K      	0x0000000000030000UL
#define  PSYCHO_IOMMU_TSBSZ_16K     	0x0000000000040000UL
#define  PSYCHO_IOMMU_TSBSZ_32K     	0x0000000000050000UL
#define  PSYCHO_IOMMU_TSBSZ_64K     	0x0000000000060000UL
#define  PSYCHO_IOMMU_TSBSZ_128K    	0x0000000000070000UL
#define  PSYCHO_IOMMU_CTRL_TBWSZ    	0x0000000000000004UL
#define  PSYCHO_IOMMU_CTRL_DENAB    	0x0000000000000002UL
#define  PSYCHO_IOMMU_CTRL_ENAB     	0x0000000000000001UL
#define PSYCHO_IOMMU_FLUSH		0x0210UL
#define PSYCHO_IOMMU_TSBBASE		0x0208UL

int psycho_iommu_init(struct pci_pbm_info *pbm, int tsbsize,
		      u32 dvma_offset, u32 dma_mask,
		      unsigned long write_complete_offset)
{
	struct iommu *iommu = pbm->iommu;
	u64 control;
	int err;

	iommu->iommu_control  = pbm->controller_regs + PSYCHO_IOMMU_CONTROL;
	iommu->iommu_tsbbase  = pbm->controller_regs + PSYCHO_IOMMU_TSBBASE;
	iommu->iommu_flush    = pbm->controller_regs + PSYCHO_IOMMU_FLUSH;
	iommu->iommu_tags     = pbm->controller_regs + PSYCHO_IOMMU_TAG;
	iommu->write_complete_reg = (pbm->controller_regs +
				     write_complete_offset);

	iommu->iommu_ctxflush = 0;

	control = upa_readq(iommu->iommu_control);
	control |= PSYCHO_IOMMU_CTRL_DENAB;
	upa_writeq(control, iommu->iommu_control);

	psycho_iommu_flush(pbm);

	/* Leave diag mode enabled for full-flushing done in pci_iommu.c */
	err = iommu_table_init(iommu, tsbsize * 1024 * 8,
			       dvma_offset, dma_mask, pbm->numa_node);
	if (err)
		return err;

	upa_writeq(__pa(iommu->page_table), iommu->iommu_tsbbase);

	control = upa_readq(iommu->iommu_control);
	control &= ~(PSYCHO_IOMMU_CTRL_TSBSZ | PSYCHO_IOMMU_CTRL_TBWSZ);
	control |= PSYCHO_IOMMU_CTRL_ENAB;

	switch (tsbsize) {
	case 64:
		control |= PSYCHO_IOMMU_TSBSZ_64K;
		break;
	case 128:
		control |= PSYCHO_IOMMU_TSBSZ_128K;
		break;
	default:
		return -EINVAL;
	}

	upa_writeq(control, iommu->iommu_control);

	return 0;

}

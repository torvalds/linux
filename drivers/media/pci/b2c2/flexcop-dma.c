/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-dma.c - configuring and controlling the DMA of the FlexCop
 * see flexcop.c for copyright information
 */
#include "flexcop.h"

int flexcop_dma_allocate(struct pci_dev *pdev,
		struct flexcop_dma *dma, u32 size)
{
	u8 *tcpu;
	dma_addr_t tdma = 0;

	if (size % 2) {
		err("dma buffersize has to be even.");
		return -EINVAL;
	}

	if ((tcpu = pci_alloc_consistent(pdev, size, &tdma)) != NULL) {
		dma->pdev = pdev;
		dma->cpu_addr0 = tcpu;
		dma->dma_addr0 = tdma;
		dma->cpu_addr1 = tcpu + size/2;
		dma->dma_addr1 = tdma + size/2;
		dma->size = size/2;
		return 0;
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(flexcop_dma_allocate);

void flexcop_dma_free(struct flexcop_dma *dma)
{
	pci_free_consistent(dma->pdev, dma->size*2,
			dma->cpu_addr0, dma->dma_addr0);
	memset(dma,0,sizeof(struct flexcop_dma));
}
EXPORT_SYMBOL(flexcop_dma_free);

int flexcop_dma_config(struct flexcop_device *fc,
		struct flexcop_dma *dma,
		flexcop_dma_index_t dma_idx)
{
	flexcop_ibi_value v0x0,v0x4,v0xc;
	v0x0.raw = v0x4.raw = v0xc.raw = 0;

	v0x0.dma_0x0.dma_address0 = dma->dma_addr0 >> 2;
	v0xc.dma_0xc.dma_address1 = dma->dma_addr1 >> 2;
	v0x4.dma_0x4_write.dma_addr_size = dma->size / 4;

	if ((dma_idx & FC_DMA_1) == dma_idx) {
		fc->write_ibi_reg(fc,dma1_000,v0x0);
		fc->write_ibi_reg(fc,dma1_004,v0x4);
		fc->write_ibi_reg(fc,dma1_00c,v0xc);
	} else if ((dma_idx & FC_DMA_2) == dma_idx) {
		fc->write_ibi_reg(fc,dma2_010,v0x0);
		fc->write_ibi_reg(fc,dma2_014,v0x4);
		fc->write_ibi_reg(fc,dma2_01c,v0xc);
	} else {
		err("either DMA1 or DMA2 can be configured within one "
			"flexcop_dma_config call.");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(flexcop_dma_config);

/* start the DMA transfers, but not the DMA IRQs */
int flexcop_dma_xfer_control(struct flexcop_device *fc,
		flexcop_dma_index_t dma_idx,
		flexcop_dma_addr_index_t index,
		int onoff)
{
	flexcop_ibi_value v0x0,v0xc;
	flexcop_ibi_register r0x0,r0xc;

	if ((dma_idx & FC_DMA_1) == dma_idx) {
		r0x0 = dma1_000;
		r0xc = dma1_00c;
	} else if ((dma_idx & FC_DMA_2) == dma_idx) {
		r0x0 = dma2_010;
		r0xc = dma2_01c;
	} else {
		err("either transfer DMA1 or DMA2 can be started within one "
			"flexcop_dma_xfer_control call.");
		return -EINVAL;
	}

	v0x0 = fc->read_ibi_reg(fc,r0x0);
	v0xc = fc->read_ibi_reg(fc,r0xc);

	deb_rdump("reg: %03x: %x\n",r0x0,v0x0.raw);
	deb_rdump("reg: %03x: %x\n",r0xc,v0xc.raw);

	if (index & FC_DMA_SUBADDR_0)
		v0x0.dma_0x0.dma_0start = onoff;

	if (index & FC_DMA_SUBADDR_1)
		v0xc.dma_0xc.dma_1start = onoff;

	fc->write_ibi_reg(fc,r0x0,v0x0);
	fc->write_ibi_reg(fc,r0xc,v0xc);

	deb_rdump("reg: %03x: %x\n",r0x0,v0x0.raw);
	deb_rdump("reg: %03x: %x\n",r0xc,v0xc.raw);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_xfer_control);

static int flexcop_dma_remap(struct flexcop_device *fc,
		flexcop_dma_index_t dma_idx,
		int onoff)
{
	flexcop_ibi_register r = (dma_idx & FC_DMA_1) ? dma1_00c : dma2_01c;
	flexcop_ibi_value v = fc->read_ibi_reg(fc,r);
	deb_info("%s\n",__func__);
	v.dma_0xc.remap_enable = onoff;
	fc->write_ibi_reg(fc,r,v);
	return 0;
}

int flexcop_dma_control_size_irq(struct flexcop_device *fc,
		flexcop_dma_index_t no,
		int onoff)
{
	flexcop_ibi_value v = fc->read_ibi_reg(fc,ctrl_208);

	if (no & FC_DMA_1)
		v.ctrl_208.DMA1_IRQ_Enable_sig = onoff;

	if (no & FC_DMA_2)
		v.ctrl_208.DMA2_IRQ_Enable_sig = onoff;

	fc->write_ibi_reg(fc,ctrl_208,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_control_size_irq);

int flexcop_dma_control_timer_irq(struct flexcop_device *fc,
		flexcop_dma_index_t no,
		int onoff)
{
	flexcop_ibi_value v = fc->read_ibi_reg(fc,ctrl_208);

	if (no & FC_DMA_1)
		v.ctrl_208.DMA1_Timer_Enable_sig = onoff;

	if (no & FC_DMA_2)
		v.ctrl_208.DMA2_Timer_Enable_sig = onoff;

	fc->write_ibi_reg(fc,ctrl_208,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_control_timer_irq);

/* 1 cycles = 1.97 msec */
int flexcop_dma_config_timer(struct flexcop_device *fc,
		flexcop_dma_index_t dma_idx, u8 cycles)
{
	flexcop_ibi_register r = (dma_idx & FC_DMA_1) ? dma1_004 : dma2_014;
	flexcop_ibi_value v = fc->read_ibi_reg(fc,r);

	flexcop_dma_remap(fc,dma_idx,0);

	deb_info("%s\n",__func__);
	v.dma_0x4_write.dmatimer = cycles;
	fc->write_ibi_reg(fc,r,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_config_timer);


/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-dma.c - methods for configuring and controlling the DMA of the FlexCop.
 *
 * see flexcop.c for copyright information.
 */
#include "flexcop.h"

int flexcop_dma_allocate(struct pci_dev *pdev, struct flexcop_dma *dma, u32 size)
{
	u8 *tcpu;
	dma_addr_t tdma;

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
	pci_free_consistent(dma->pdev, dma->size*2,dma->cpu_addr0, dma->dma_addr0);
	memset(dma,0,sizeof(struct flexcop_dma));
}
EXPORT_SYMBOL(flexcop_dma_free);

int flexcop_dma_control_timer_irq(struct flexcop_device *fc, flexcop_dma_index_t no, int onoff)
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

int flexcop_dma_control_size_irq(struct flexcop_device *fc, flexcop_dma_index_t no, int onoff)
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

int flexcop_dma_control_packet_irq(struct flexcop_device *fc, flexcop_dma_index_t no, int onoff)
{
	flexcop_ibi_value v = fc->read_ibi_reg(fc,ctrl_208);

	if (no & FC_DMA_1)
		v.ctrl_208.DMA1_Size_IRQ_Enable_sig = onoff;

	if (no & FC_DMA_2)
		v.ctrl_208.DMA2_Size_IRQ_Enable_sig = onoff;

	fc->write_ibi_reg(fc,ctrl_208,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_control_packet_irq);

int flexcop_dma_config(struct flexcop_device *fc, struct flexcop_dma *dma, flexcop_dma_index_t dma_idx,flexcop_dma_addr_index_t index)
{

	flexcop_ibi_value v0x0,v0x4,v0xc;
	v0x0.raw = v0x4.raw = v0xc.raw = 0;

	v0x0.dma_0x0.dma_address0        = dma->dma_addr0 >> 2;
	v0xc.dma_0xc.dma_address1        = dma->dma_addr1 >> 2;
	v0x4.dma_0x4_write.dma_addr_size = dma->size / 4;

	if (index & FC_DMA_SUBADDR_0)
		v0x0.dma_0x0.dma_0start = 1;

	if (index & FC_DMA_SUBADDR_1)
		v0xc.dma_0xc.dma_1start = 1;

	if (dma_idx & FC_DMA_1) {
		fc->write_ibi_reg(fc,dma1_000,v0x0);
		fc->write_ibi_reg(fc,dma1_004,v0x4);
		fc->write_ibi_reg(fc,dma1_00c,v0xc);
	} else { /* (dma_idx & FC_DMA_2) */
		fc->write_ibi_reg(fc,dma2_010,v0x0);
		fc->write_ibi_reg(fc,dma2_014,v0x4);
		fc->write_ibi_reg(fc,dma2_01c,v0xc);
	}

	return 0;
}
EXPORT_SYMBOL(flexcop_dma_config);

static int flexcop_dma_remap(struct flexcop_device *fc, flexcop_dma_index_t dma_idx, int onoff)
{
	flexcop_ibi_register r = (dma_idx & FC_DMA_1) ? dma1_00c : dma2_01c;
	flexcop_ibi_value v = fc->read_ibi_reg(fc,r);
	v.dma_0xc.remap_enable = onoff;
	fc->write_ibi_reg(fc,r,v);
	return 0;
}

/* 1 cycles = 1.97 msec */
int flexcop_dma_config_timer(struct flexcop_device *fc, flexcop_dma_index_t dma_idx, u8 cycles)
{
	flexcop_ibi_register r = (dma_idx & FC_DMA_1) ? dma1_004 : dma2_014;
	flexcop_ibi_value v = fc->read_ibi_reg(fc,r);

	flexcop_dma_remap(fc,dma_idx,0);

	v.dma_0x4_write.dmatimer = cycles >> 1;
	fc->write_ibi_reg(fc,r,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_config_timer);

int flexcop_dma_config_packet_count(struct flexcop_device *fc, flexcop_dma_index_t dma_idx, u8 packets)
{
	flexcop_ibi_register r = (dma_idx & FC_DMA_1) ? dma1_004 : dma2_014;
	flexcop_ibi_value v = fc->read_ibi_reg(fc,r);

	flexcop_dma_remap(fc,dma_idx,1);

	v.dma_0x4_remap.DMA_maxpackets = packets;
	fc->write_ibi_reg(fc,r,v);
	return 0;
}
EXPORT_SYMBOL(flexcop_dma_config_packet_count);

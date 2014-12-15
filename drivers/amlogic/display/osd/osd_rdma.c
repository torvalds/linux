/*
 * Amlogic Meson
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author: Amlogic Platform-BJ
 *
 * description
 *     rdma table work as REGISTER Cache for read write.
 */
 #include "osd_rdma.h"
static rdma_table_item_t* rdma_table=NULL;
static u32		   table_paddr=0; 
static u32		   rdma_enable=0;
static u32		   item_count=0;

static bool		osd_rdma_init_flat = false;
static int ctrl_ahb_rd_burst_size = 3;
static int ctrl_ahb_wr_burst_size = 3;

static int  osd_rdma_init(void);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define Wr(adr,val) WRITE_VCBUS_REG(adr, val)
#define Rd(adr)    READ_VCBUS_REG(adr)
#define Wr_reg_bits(adr, val, start, len)  WRITE_VCBUS_REG_BITS(adr, val, start, len)
#define Wr_set_reg_bits_mask(adr, _mask)	 SET_VCBUS_REG_MASK(adr, _mask);
#define Wr_clr_reg_bits_mask(adr, _mask)	 CLEAR_VCBUS_REG_MASK(adr, _mask);
#else
#define Wr(adr,val) WRITE_MPEG_REG(adr, val)
#define Rd(adr)    READ_MPEG_REG(adr)
#define Wr_reg_bits(adr, val, start, len)  WRITE_MPEG_REG_BITS(adr, val, start, len)
#define Wr_set_reg_bits_mask(adr, _mask)	 SET_MPEG_REG_MASK(adr, _mask);
#define Wr_clr_reg_bits_mask(adr, _mask)	 CLEAR_MPEG_REG_MASK(adr, _mask);
#endif

static int  update_table_item(u32 addr,u32 val)
{
	if(item_count > (MAX_TABLE_ITEM-1)) return -1;

	//new comer,then add it .
	rdma_table[item_count].addr=addr;
	rdma_table[item_count].val=val;
	item_count++;
	aml_write_reg32(END_ADDR,(table_paddr + item_count*8-1));
	return 0;
}

u32  VSYNCOSD_RD_MPEG_REG(unsigned long addr)
{
	int  i;

	if(rdma_enable)
	{
		for(i=(item_count -1); i>=0; i--)
		{
			if(addr==rdma_table[i].addr)
			return rdma_table[i].val;
		}
	}
	return Rd(addr);
}    
EXPORT_SYMBOL(VSYNCOSD_RD_MPEG_REG);

int VSYNCOSD_WR_MPEG_REG(unsigned long addr, unsigned long val)
{
	if(rdma_enable)
	{
		update_table_item(addr,val);
	}else{
		Wr(addr,val);
	}
	return 0;
}
EXPORT_SYMBOL(VSYNCOSD_WR_MPEG_REG);

int VSYNCOSD_WR_MPEG_REG_BITS(unsigned long addr, unsigned long val, unsigned long start, unsigned long len)
{
	unsigned long read_val;
	unsigned long write_val;

	if(rdma_enable){
		read_val=VSYNCOSD_RD_MPEG_REG(addr);
		write_val = (read_val & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start));
		update_table_item(addr,write_val);
	}else{
		Wr_reg_bits(addr,val,start,len);
	}
    
	return 0;
}
EXPORT_SYMBOL(VSYNCOSD_WR_MPEG_REG_BITS);

int VSYNCOSD_SET_MPEG_REG_MASK(unsigned long addr, unsigned long _mask)
{
	unsigned long read_val;
	unsigned long write_val;

	if(rdma_enable){
		read_val=VSYNCOSD_RD_MPEG_REG(addr);
		write_val = read_val|_mask ;
		update_table_item(addr,write_val);
	}else{
		Wr_set_reg_bits_mask(addr,_mask);
	}	

	return 0;
}
EXPORT_SYMBOL(VSYNCOSD_SET_MPEG_REG_MASK);

int VSYNCOSD_CLR_MPEG_REG_MASK(unsigned long addr, unsigned long _mask)
{
	unsigned long read_val;
	unsigned long write_val;

	if(rdma_enable){
		read_val=VSYNCOSD_RD_MPEG_REG(addr);
		write_val = read_val&(~_mask) ;
		update_table_item(addr,write_val);
	}else{
		Wr_clr_reg_bits_mask(addr,_mask);
	}

	return 0;
}
EXPORT_SYMBOL(VSYNCOSD_CLR_MPEG_REG_MASK);

static int start_osd_rdma(char channel)
{
	char intr_bit=8*channel;
	char rw_bit=4+channel;
	char inc_bit=channel;
	u32 data32;

	data32  = 0;
	data32 |= 0                         << 6;   // [31: 6] Rsrv.
	data32 |= ctrl_ahb_wr_burst_size    << 4;   // [ 5: 4] ctrl_ahb_wr_burst_size. 0=16; 1=24; 2=32; 3=48.
	data32 |= ctrl_ahb_rd_burst_size    << 2;   // [ 3: 2] ctrl_ahb_rd_burst_size. 0=16; 1=24; 2=32; 3=48.
	data32 |= 0                         << 1;   // [    1] ctrl_sw_reset.
	data32 |= 0                         << 0;   // [    0] ctrl_free_clk_enable.
	aml_write_reg32(P_RDMA_CTRL, data32);

	data32  = aml_read_reg32(P_RDMA_ACCESS_AUTO);
	data32 |= 0x1 << intr_bit;   // [23: 16] interrupt inputs enable mask for auto-start 1: vsync int bit 0
	data32 |= 1 << rw_bit;   // [    6] ctrl_cbus_write_1. 1=Register write; 0=Register read.
	data32 &= ~(1<<inc_bit);   // [    2] ctrl_cbus_addr_incr_1. 1=Incremental register access; 0=Non-incremental.

	aml_write_reg32(P_RDMA_ACCESS_AUTO, data32);
	return 1;
}

static int stop_rdma(char channel)
{
	char intr_bit=8*channel;
	u32 data32 = 0x0;

	data32  = aml_read_reg32(P_RDMA_ACCESS_AUTO);
	data32 &= ~(0x1 << intr_bit);   // [23: 16] interrupt inputs enable mask for auto-start 1: vsync int bit 0
	aml_write_reg32(P_RDMA_ACCESS_AUTO, data32);
	return 0;
}

int reset_rdma(void)
{
	item_count=0;
	aml_write_reg32(END_ADDR,(table_paddr + item_count*8-1));
	//start_rdma(RDMA_CHANNEL_INDEX);
	return 0;
}
EXPORT_SYMBOL(reset_rdma);

int osd_rdma_enable(u32  enable)
{
	if (!osd_rdma_init_flat){
		osd_rdma_init();
	}

	if(enable == rdma_enable) return 0;
	rdma_enable = enable;
	if(enable){
		aml_write_reg32(START_ADDR,table_paddr);
		//enable then start it.
		reset_rdma();
		start_osd_rdma(RDMA_CHANNEL_INDEX);
	}else{
		stop_rdma(RDMA_CHANNEL_INDEX);
	}
	return 1;
}
EXPORT_SYMBOL(osd_rdma_enable);

static int  osd_rdma_init(void)
{
	// alloc map table .
	static ulong table_vaddr;
	osd_rdma_init_flat = true;
	table_vaddr= __get_free_pages(GFP_KERNEL, get_order(PAGE_SIZE));
	if (!table_vaddr) {
		printk("%s: failed to alloc rmda_table\n", __func__);
		return -1;
	}

	table_paddr = virt_to_phys((u8 *)table_vaddr);
	//remap addr nocache.
	rdma_table=(rdma_table_item_t*) ioremap_nocache(table_paddr, PAGE_SIZE);

	if (NULL==rdma_table) {
		printk("%s: failed to remap rmda_table_addr\n", __func__);
		return -1;
	}

	return 0;
}

MODULE_PARM_DESC(item_count, "\n item_count\n");
module_param(item_count, uint, 0664);

MODULE_PARM_DESC(table_paddr, "\n table_paddr\n");
module_param(table_paddr, uint, 0664);


/*
 * AMLOGIC Audio/Video streaming port driver.
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
 * Author:  Rain Zhang <rain.zhang@amlogic.com>
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>

#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/clk.h>
#include <linux/amlogic/logo/logo.h>

#define CONFIG_RDMA_IN_RDMAIRQ

#ifndef MESON_CPU_TYPE_MESON8
#define MESON_CPU_TYPE_MESON8 (MESON_CPU_TYPE_MESON6TV+1)
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define Wr(adr,val) WRITE_VCBUS_REG(adr, val)
#define Rd(adr)    READ_VCBUS_REG(adr)
#define Wr_reg_bits(adr, val, start, len)  WRITE_VCBUS_REG_BITS(adr, val, start, len)
#else
#define Wr(adr,val) WRITE_MPEG_REG(adr, val)
#define Rd(adr)    READ_MPEG_REG(adr)
#define Wr_reg_bits(adr, val, start, len)  WRITE_MPEG_REG_BITS(adr, val, start, len)
#endif

//#define RDMA_CHECK_PRE
#define READ_BACK_SUPPORT

#ifdef READ_BACK_SUPPORT
static ulong* rmda_rd_table = NULL;
static ulong rmda_rd_table_phy_addr = 0, * rmda_rd_table_addr_remap = NULL;
static int rmda_rd_item_count = 0;
static int rmda_rd_item_count_pre = 0;
#endif



#define RDMA_TABLE_SIZE                    2*(PAGE_SIZE)

static ulong* rmda_table = NULL;

static ulong rmda_table_phy_addr = 0, * rmda_table_addr_remap = NULL;

static int rmda_item_count = 0;

#ifdef RDMA_CHECK_PRE    
static ulong* rmda_table_pre = NULL;
static int rmda_item_count_pre = 0;
#endif

static int irq_count = 0;

static int rdma_done_line_max = 0;

static int enable = 0;

static int enable_mask = 0x400ff;

static int pre_enable_ = 0;

static int debug_flag = 0;

static int post_line_start = 0;

static bool rdma_start = false;
static bool vsync_rdma_config_delay_flag = false;
// burst size 0=16; 1=24; 2=32; 3=48.
static int ctrl_ahb_rd_burst_size = 3;
static int ctrl_ahb_wr_burst_size = 3;

static int rdma_isr_cfg_count=0;
static int vsync_cfg_count=0;

static void set_output_mode_rdma(void)
{
    const vinfo_t *info;
    info = get_current_vinfo();

    if(info){
        if((strncmp(info->name, "480cvbs", 7) == 0)||(strncmp(info->name, "576cvbs", 7) == 0)||
            (strncmp(info->name, "480i", 4) == 0) ||(strncmp(info->name, "576i", 4) == 0)||
            (strncmp(info->name, "1080i", 5) == 0)){
            enable = 0;
        }
        else{
            enable = 1;
        }
    }    
}    

static int display_notify_callback_v(struct notifier_block *block, unsigned long cmd , void *para)
{
    if((enable_mask>>18)&0x1){
        return 0;
    }

    if (cmd != VOUT_EVENT_MODE_CHANGE)
        return 0;
    
    set_output_mode_rdma();
    
    return 0;
}

static struct notifier_block display_mode_notifier_nb_v = {
    .notifier_call    = display_notify_callback_v,
};

void rdma_table_prepare_write(unsigned long reg_adr, unsigned long val)
{
    if(((rmda_item_count<<1)+1)<(RDMA_TABLE_SIZE/4)){
        rmda_table[rmda_item_count<<1] = reg_adr; //CBUS_REG_ADDR(reg_adr);
        rmda_table[(rmda_item_count<<1)+1] = val;
        //printk("%s %d: %x %x\n",__func__, rmda_item_count, rmda_table[rmda_item_count<<1], rmda_table[(rmda_item_count<<1)+1]);
        rmda_item_count++;
    }
    else{
        printk("%s fail: %d, %lx %lx\n",__func__, rmda_item_count, reg_adr, val);
    }
}

#ifdef READ_BACK_SUPPORT
void rdma_table_prepare_read(unsigned long reg_adr)
{
    if(((rmda_rd_item_count<<1)+1)<(RDMA_TABLE_SIZE/4)){
        rmda_rd_table[rmda_rd_item_count] = reg_adr; //CBUS_REG_ADDR(reg_adr);
        //printk("%s %d: %x %x\n",__func__, rmda_item_count, rmda_table[rmda_item_count<<1], rmda_table[(rmda_item_count<<1)+1]);
        rmda_rd_item_count++;
    }
    else{
        printk("%s fail: %d, %lx\n",__func__, rmda_rd_item_count, reg_adr);
    }
}
#endif

EXPORT_SYMBOL(rdma_table_prepare_write);
static int rdma_config(unsigned char type)
{
    unsigned long data32;
    //if(rmda_item_count <= 0)
    //    return 0;
    //printk("%s %x %x %x\n", __func__, rmda_table_phy_addr, rmda_table_addr_remap, rmda_table);
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
    Wr(GCLK_REG_MISC_RDMA, Rd(GCLK_REG_MISC_RDMA)|GCLK_MASK_MISC_RDMA);
#endif
    if(rmda_item_count>0){
    memcpy(rmda_table_addr_remap, rmda_table, rmda_item_count*8);
#ifdef RDMA_CHECK_PRE    
        memcpy(rmda_table_pre, rmda_table, rmda_item_count*8);
#endif        
    }
#ifdef RDMA_CHECK_PRE    
    rmda_item_count_pre = rmda_item_count;
#endif
    data32  = 0;
    data32 |= 1                         << 6;   // [31: 6] Rsrv.
    data32 |= ctrl_ahb_wr_burst_size    << 4;   // [ 5: 4] ctrl_ahb_wr_burst_size. 0=16; 1=24; 2=32; 3=48.
    data32 |= ctrl_ahb_rd_burst_size    << 2;   // [ 3: 2] ctrl_ahb_rd_burst_size. 0=16; 1=24; 2=32; 3=48.
    data32 |= 0                         << 1;   // [    1] ctrl_sw_reset.
    data32 |= 0                         << 0;   // [    0] ctrl_free_clk_enable.
    Wr(RDMA_CTRL, data32);

    if(type == 0){ //manual RDMA
        Wr(RDMA_AHB_START_ADDR_MAN, rmda_table_phy_addr);
        Wr(RDMA_AHB_END_ADDR_MAN,   rmda_table_phy_addr + rmda_item_count*8 - 1);

        data32  = 0;
        data32 |= 0                         << 3;   // [31: 3] Rsrv.
        data32 |= 1                         << 2;   // [    2] ctrl_cbus_write_man. 1=Register write; 0=Register read.
        data32 |= 0                         << 1;   // [    1] ctrl_cbus_addr_incr_man. 1=Incremental register access; 0=Non-incremental.
        data32 |= 0                         << 0;   // [    0] ctrl_start_man pulse.
        Wr(RDMA_ACCESS_MAN, data32);
        // Manual-start RDMA
        if(rmda_item_count > 0){
            Wr(RDMA_ACCESS_MAN, Rd(RDMA_ACCESS_MAN) | 1);
        }
    
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
        do{
            if((Rd(RDMA_STATUS)&0xffffff0f) == 0){
                    break;
            }
        }while(1);
#endif        
    }
    else if(type == 1){ //vsync trigger RDMA
        Wr(RDMA_AHB_START_ADDR_1, rmda_table_phy_addr);
        Wr(RDMA_AHB_END_ADDR_1,   rmda_table_phy_addr + rmda_item_count*8 - 1);

	data32 = Rd(RDMA_ACCESS_AUTO);
	if(rmda_item_count > 0){
		data32 |= 0x1                       << 8;   // [15: 8] interrupt inputs enable mask for auto-start 1: vsync int bit 0
		data32 |= 1                         << 5;   // [    5] ctrl_cbus_write_1. 1=Register write; 0=Register read.
		data32 |= 0                         << 1;   // [    1] ctrl_cbus_addr_incr_1. 1=Incremental register access; 0=Non-incremental.
		Wr(RDMA_ACCESS_AUTO, data32);
	}
	else{
		data32 &= 0xffffedd;
		Wr(RDMA_ACCESS_AUTO, data32);
	}
#ifdef READ_BACK_SUPPORT
        if(rmda_rd_item_count > 0){
            memset(rmda_rd_table_addr_remap, 0xff, rmda_rd_item_count*8);
            memcpy(rmda_rd_table_addr_remap, rmda_rd_table, rmda_rd_item_count*4);
            
            Wr(RDMA_AHB_START_ADDR_2, rmda_rd_table_phy_addr);
            Wr(RDMA_AHB_END_ADDR_2,   rmda_rd_table_phy_addr + rmda_rd_item_count*8 - 1);
    
            data32 |= 0x1                       << 16;   // [23: 16] interrupt inputs enable mask for auto-start 2: vsync int bit 0
            data32 |= 0                         << 6;   // [    6] ctrl_cbus_write_2. 1=Register write; 0=Register read.
            data32 |= 0                         << 2;   // [    2] ctrl_cbus_addr_incr_2. 1=Incremental register access; 0=Non-incremental.
            Wr(RDMA_ACCESS_AUTO, data32);
        }
        else{
            data32 &= (~(1<<16));    
        }
        rmda_rd_item_count_pre = rmda_rd_item_count;
        rmda_rd_item_count = 0;
#endif        
    }
    else if(type == 2){
        int i;
        for(i=0; i<rmda_item_count; i++){
            Wr(rmda_table_addr_remap[i<<1], rmda_table_addr_remap[(i<<1)+1]);
            if(debug_flag&1)
                printk("VSYNC_WR(%lx)<=%lx\n", rmda_table_addr_remap[i<<1], rmda_table_addr_remap[(i<<1)+1]);
        }   
    }
    else if(type == 3){
        int i;
        for(i=0; i<rmda_item_count; i++){
            Wr(rmda_table[i<<1], rmda_table[(i<<1)+1]);
            if(debug_flag&1)
                printk("VSYNC_WR(%lx)<=%lx\n", rmda_table[i<<1], rmda_table[(i<<1)+1]);
        }   
    }
    //printk("%s %d\n", __func__, rmda_item_count);
    rmda_item_count = 0;
    return 0;
}

void vsync_rdma_config(void)
{
    u32 data32;
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;


    if(pre_enable_ != enable_){
        if(((enable_mask>>17)&0x1)==0){
            data32  = Rd(RDMA_ACCESS_AUTO);
            data32 &= 0xffffedd;
            Wr(RDMA_ACCESS_MAN, 0);
            Wr(RDMA_ACCESS_AUTO, data32);
        }
        vsync_rdma_config_delay_flag = false;
    }
    
    if(enable_ == 1){
#ifdef CONFIG_RDMA_IN_RDMAIRQ
        if(pre_enable_!=enable_){
            rdma_config(1); //triggered by next vsync    
        }
#else        
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        rdma_config(1); //triggered by next vsync    
        vsync_cfg_count++;
#else
        if((Rd(RDMA_STATUS)&0xffffff0f) == 0){
            rdma_config(1); //triggered by next vsync    
            vsync_cfg_count++;
        }
        else{
            vsync_rdma_config_delay_flag = true;
            rdma_isr_cfg_count++;
        }
#endif        
#endif
    }
    else if( enable_ == 2){
        rdma_config(0); //manually in cur vsync
    }
    else if(enable_ == 3){
    }
    else if( enable_  == 4){
        rdma_config(2); //for debug
    }
    else if( enable_  == 5){
        rdma_config(3); //for debug
    }
    else if( enable_ == 6){
    }

    pre_enable_ = enable_;
}    
EXPORT_SYMBOL(vsync_rdma_config);

void vsync_rdma_config_pre(void)
{
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff; 
    if(enable_ == 3){ //manually in next vsync
        rdma_config(0); 
    }
    else if(enable_ == 6){
        rdma_config(2);    
    }    
}    
EXPORT_SYMBOL(vsync_rdma_config_pre);

static irqreturn_t rdma_isr(int irq, void *dev_id)
{
#ifdef CONFIG_RDMA_IN_RDMAIRQ
     int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
     if(enable_ == 1){
        rdma_config(1); //triggered by next vsync    
     }
    irq_count++;
#else
    int enc_line;
    u32 data32;

    irq_count++;
    if(post_line_start){
        while(((Rd(ENCL_INFO_READ)>>16)&0x1fff)<post_line_start){
	
        }
    }

    if(vsync_rdma_config_delay_flag){
        data32  = Rd(RDMA_ACCESS_AUTO);
        data32 &= 0xffffedd;
        Wr(RDMA_ACCESS_MAN, 0);
        Wr(RDMA_ACCESS_AUTO, data32);
        rdma_config(1);
        vsync_rdma_config_delay_flag = false;
    }
    
    switch(Rd(VPU_VIU_VENC_MUX_CTRL)&0x3){
        case 0:
            enc_line = (Rd(ENCL_INFO_READ)>>16)&0x1fff;
            break;
        case 1:
            enc_line = (Rd(ENCI_INFO_READ)>>16)&0x1fff;
            break;
        case 2:
            enc_line = (Rd(ENCP_INFO_READ)>>16)&0x1fff;
            break;
        case 3:    
            enc_line = (Rd(ENCT_INFO_READ)>>16)&0x1fff;
            break;
    }
    if(enc_line > rdma_done_line_max){
        rdma_done_line_max = enc_line;
    }
#endif    
    return IRQ_HANDLED;
}
static int __init rmda_early_init(void)
{
    ulong rmda_table_addr;
#ifdef READ_BACK_SUPPORT
    ulong rmda_rd_table_addr;
#endif    
    rmda_table_addr = __get_free_pages(GFP_KERNEL, get_order(RDMA_TABLE_SIZE));
    if (!rmda_table_addr) {
        printk("%s: failed to alloc rmda_table_addr\n", __func__);
        return -1;
    }

    rmda_table_phy_addr = virt_to_phys((u8 *)rmda_table_addr);
    
    rmda_table_addr_remap = ioremap_nocache(rmda_table_phy_addr, RDMA_TABLE_SIZE);
    if (!rmda_table_addr_remap) {
            printk("%s: failed to remap rmda_table_addr\n", __func__);
            return -1;
    }
    
    rmda_table = kmalloc(RDMA_TABLE_SIZE, GFP_KERNEL);
#ifdef RDMA_CHECK_PRE    
    rmda_table_pre = kmalloc(RDMA_TABLE_SIZE, GFP_KERNEL);
#endif    
#ifdef READ_BACK_SUPPORT
    rmda_rd_table_addr = __get_free_pages(GFP_KERNEL, get_order(RDMA_TABLE_SIZE));
    if (!rmda_rd_table_addr) {
        printk("%s: failed to alloc rmda_rd_table_addr\n", __func__);
        return -1;
    }

    rmda_rd_table_phy_addr = virt_to_phys((u8 *)rmda_rd_table_addr);
    
    rmda_rd_table_addr_remap = ioremap_nocache(rmda_rd_table_phy_addr, RDMA_TABLE_SIZE);
    if (!rmda_rd_table_addr_remap) {
            printk("%s: failed to remap rmda_rd_table_addr\n", __func__);
            return -1;
    }
    
    rmda_rd_table = kmalloc(RDMA_TABLE_SIZE, GFP_KERNEL);
#endif

#ifdef CONFIG_RDMA_IN_RDMAIRQ
    if(request_irq(INT_RDMA, &rdma_isr,
                    IRQF_SHARED, "rdma",
                    (void *)"rdma"))
		return -1;

#else
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
    if(request_irq(INT_RDMA, &rdma_isr,
                    IRQF_SHARED, "rdma",
                    (void *)"rdma"))
		return -1;
#endif    
#endif
    //printk("%s phy_addr %x remap %x table %x\n", __func__, rmda_table_phy_addr, rmda_table_addr_remap, rmda_table); 
    
    return 0;        
}        

arch_initcall(rmda_early_init);

MODULE_PARM_DESC(enable, "\n enable\n");
module_param(enable, uint, 0664);

MODULE_PARM_DESC(enable_mask, "\n enable_mask\n");
module_param(enable_mask, uint, 0664);

MODULE_PARM_DESC(irq_count, "\n irq_count\n");
module_param(irq_count, uint, 0664);

MODULE_PARM_DESC(post_line_start, "\n post_line_start\n");
module_param(post_line_start, uint, 0664);

MODULE_PARM_DESC(rdma_done_line_max, "\n rdma_done_line_max\n");
module_param(rdma_done_line_max, uint, 0664);

MODULE_PARM_DESC(ctrl_ahb_rd_burst_size, "\n ctrl_ahb_rd_burst_size\n");
module_param(ctrl_ahb_rd_burst_size, uint, 0664);

MODULE_PARM_DESC(ctrl_ahb_wr_burst_size, "\n ctrl_ahb_wr_burst_size\n");
module_param(ctrl_ahb_wr_burst_size, uint, 0664);

MODULE_PARM_DESC(debug_flag, "\n debug_flag\n");
module_param(debug_flag, uint, 0664);

MODULE_PARM_DESC(rdma_isr_cfg_count, "\n rdma_isr_cfg_count\n");
module_param(rdma_isr_cfg_count, uint, 0664);

MODULE_PARM_DESC(vsync_cfg_count, "\n vsync_cfg_count\n");
module_param(vsync_cfg_count, uint, 0664);

unsigned long VSYNC_RD_MPEG_REG(unsigned long adr)
{
    int i;
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    unsigned long read_val = Rd(adr);
    if((enable_!=0)&&rdma_start){
#ifdef RDMA_CHECK_PRE    
        for(i=(rmda_item_count_pre-1); i>=0; i--){
            if(rmda_table_pre[i<<1]==adr){
                read_val = rmda_table_pre[(i<<1)+1];
                break;
            }
        }   
#endif        
        for(i=(rmda_item_count-1); i>=0; i--){
            if(rmda_table[i<<1]==adr){
                read_val = rmda_table[(i<<1)+1];
                break;
            }
        }   
    }
    return read_val;    
}    
EXPORT_SYMBOL(VSYNC_RD_MPEG_REG);

int VSYNC_WR_MPEG_REG(unsigned long adr, unsigned long val)
{ 
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    if((enable_!=0)&&rdma_start){
        if(debug_flag&1)
            printk("RDMA_WR %d(%lx)<=%lx\n", rmda_item_count, adr, val);

        rdma_table_prepare_write(adr, val);
    }
    else{
        Wr(adr,val);
        if(debug_flag&1)
            printk("VSYNC_WR(%lx)<=%lx\n", adr, val);
    }
    return 0;
}
EXPORT_SYMBOL(VSYNC_WR_MPEG_REG);

int VSYNC_WR_MPEG_REG_BITS(unsigned long adr, unsigned long val, unsigned long start, unsigned long len)
{
    int i;
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    if((enable_!=0)&&rdma_start){
        unsigned long read_val = Rd(adr);
        unsigned long write_val;
#ifdef RDMA_CHECK_PRE    
        for(i=(rmda_item_count_pre-1); i>=0; i--){
            if(rmda_table_pre[i<<1]==adr){
                read_val = rmda_table_pre[(i<<1)+1];
                break;
            }
        }   
#endif
        for(i=(rmda_item_count-1); i>=0; i--){
            if(rmda_table[i<<1]==adr){
                read_val = rmda_table[(i<<1)+1];
                break;
            }
        }   
        write_val = (read_val & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start));
        if(debug_flag&1)
            printk("RDMA_WR %d(%lx)<=%lx\n", rmda_item_count, adr, write_val);

        rdma_table_prepare_write(adr, write_val);
    }
    else{
        unsigned long read_val = Rd(adr);
        unsigned long write_val = (read_val & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start));
        Wr(adr, write_val);
        if(debug_flag&1)
            printk("VSYNC_WR(%lx)<=%lx\n", adr, write_val);
        //Wr_reg_bits(adr, val, start, len);        
    }
    return 0;
}
EXPORT_SYMBOL(VSYNC_WR_MPEG_REG_BITS);

unsigned long RDMA_READ_REG(unsigned long adr)
{
    unsigned long read_val = 0xffffffff;
#ifdef READ_BACK_SUPPORT
    int i;
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    if((enable_!=0)&&rdma_start){
        for(i=(rmda_rd_item_count_pre-1); i>=0; i--){
            if(rmda_rd_table_addr_remap[i]==adr){
                read_val = rmda_rd_table_addr_remap[rmda_rd_item_count_pre + i];
                break;
            }
        }   
    }
#endif
    return read_val;    
}    
EXPORT_SYMBOL(RDMA_READ_REG);

int RDMA_SET_READ(unsigned long adr)
{ 
#ifdef READ_BACK_SUPPORT
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    if((enable_!=0)&&rdma_start){
        if(debug_flag&1)
            printk("RDMA_SET_READ %d(%lx)\n", rmda_rd_item_count, adr);

        rdma_table_prepare_read(adr);
    }
#endif
    return 0;
}
EXPORT_SYMBOL(RDMA_SET_READ);

bool is_vsync_rdma_enable(void)
{
    int enable_ = ((enable&enable_mask)|(enable_mask>>8))&0xff;
    //return ((enable_==1)||(enable_==3));
    return (enable_!=0)&&(((enable_mask>>19)&0x1)==0);
}
EXPORT_SYMBOL(is_vsync_rdma_enable);

void start_rdma(void)
{
    if(((enable_mask>>16)&0x1)==0)
        rdma_start = true;
}    
EXPORT_SYMBOL(start_rdma);

void enable_rdma_log(int flag)
{
    if(flag)
        debug_flag |= 0x1;
    else
        debug_flag &= (~0x1);
}    
EXPORT_SYMBOL(enable_rdma_log);

void enable_rdma(int enable_flag)
{
    enable = enable_flag;
}    
EXPORT_SYMBOL(enable_rdma);

static int  __init rdma_init(void)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
	if(IS_MESON_M8M2_CPU){
		WRITE_VCBUS_REG(VPU_VDISP_ASYNC_HOLD_CTRL, 0x18101810);
		WRITE_VCBUS_REG(VPU_VPUARB2_ASYNC_HOLD_CTRL, 0x18101810);
	}
#endif

#if 1 // MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
    //set_output_mode_rdma();
    enable = 1;
    vout_register_client(&display_mode_notifier_nb_v);
#endif    
    return 0;
}



module_init(rdma_init);

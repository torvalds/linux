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
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/vformat.h>
#include <mach/am_regs.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif

#include "vdec_reg.h"
#include "vdec.h"
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "amports_config.h"
#include "amvdec.h"

#include "vdec_clk.h"

static DEFINE_SPINLOCK(lock);

#define MC_SIZE (4096 * 4)

#define SUPPORT_VCODEC_NUM  1
static int inited_vcodec_num = 0;
static int poweron_clock_level = 0;
static unsigned int debug_trace_num = 16*20;
static struct platform_device *vdec_device = NULL;
static struct platform_device *vdec_core_device = NULL;
struct am_reg {
    char *name;
    int offset;
};

static struct vdec_dev_reg_s vdec_dev_reg;

static const char *vdec_device_name[] = {
    "amvdec_mpeg12",
    "amvdec_mpeg4",
    "amvdec_h264",
    "amvdec_mjpeg",
    "amvdec_real",
    "amjpegdec",
    "amvdec_vc1",
    "amvdec_avs",
    "amvdec_yuv",
    "amvdec_h264mvc",
    "amvdec_h264_4k2k",
    "amvdec_h265"
};

void vdec_set_decinfo(struct dec_sysinfo *p)
{
    vdec_dev_reg.sys_info = p;
}

int vdec_set_resource(unsigned long start, unsigned long end, struct device *p)
{
    if (inited_vcodec_num != 0) {
        printk("ERROR:We can't support the change resource at code running\n");
        return -1;
    }

    vdec_dev_reg.mem_start = start;
    vdec_dev_reg.mem_end   = end;
    vdec_dev_reg.cma_dev   = p;

    return 0;
}

s32 vdec_init(vformat_t vf)
{
    s32 r;

    if (inited_vcodec_num >= SUPPORT_VCODEC_NUM) {
        printk("We only support the one video code at each time\n");
        return -EIO;
    }

    inited_vcodec_num++;

    vdec_device = platform_device_register_data(&vdec_core_device->dev, vdec_device_name[vf], -1,
                                            &vdec_dev_reg, sizeof(vdec_dev_reg));

    if (IS_ERR(vdec_device)) {
        r = PTR_ERR(vdec_device);
        printk("vdec: Decoder device register failed (%d)\n", r);
        goto error;
    }

    return 0;

error:
    vdec_device = NULL;

    inited_vcodec_num--;

    return r;
}

s32 vdec_release(vformat_t vf)
{
    if (vdec_device) {
        platform_device_unregister(vdec_device);
    }

    inited_vcodec_num--;

    vdec_device = NULL;

    return 0;
}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
void vdec_poweron(vdec_type_t core)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (core == VDEC_1) {
        // vdec1 power on
        WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & ~0xc);
        // wait 10uS
        udelay(10);
        // vdec1 soft reset
        WRITE_VREG(DOS_SW_RESET0, 0xfffffffc);
        WRITE_VREG(DOS_SW_RESET0, 0);
        // enable vdec1 clock
        /*add power on vdec clock level setting,only for m8 chip,
         m8baby and m8m2 can dynamic adjust vdec clock,power on with default clock level*/
        if(poweron_clock_level == 1 && IS_MESON_M8_CPU) { 
            vdec_clock_hi_enable();            
        } else {
            vdec_clock_enable();
        }
        // power up vdec memories
        WRITE_VREG(DOS_MEM_PD_VDEC, 0);
        // remove vdec1 isolation
        WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) & ~0xC0);
        // reset DOS top registers
        WRITE_VREG(DOS_VDEC_MCRCC_STALL_CTRL, 0);
    } else if (core == VDEC_2) {
        if (HAS_VDEC2) {
            // vdec2 power on
            WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & ~0x30);
            // wait 10uS
            udelay(10);
            // vdec2 soft reset
            WRITE_VREG(DOS_SW_RESET2, 0xffffffff);
            WRITE_VREG(DOS_SW_RESET2, 0);
            // enable vdec1 clock
            vdec2_clock_enable();
            // power up vdec memories
            WRITE_VREG(DOS_MEM_PD_VDEC2, 0);
            // remove vdec2 isolation
            WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) & ~0x300);
            // reset DOS top registers
            WRITE_VREG(DOS_VDEC2_MCRCC_STALL_CTRL, 0);
        }
    } else if (core == VDEC_HCODEC) {
#if HAS_HDEC
        // hcodec power on
        WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & ~0x3);
        // wait 10uS
        udelay(10);
        // hcodec soft reset
        WRITE_VREG(DOS_SW_RESET1, 0xffffffff);
        WRITE_VREG(DOS_SW_RESET1, 0);
        // enable hcodec clock
        hcodec_clock_enable();
        // power up hcodec memories
        WRITE_VREG(DOS_MEM_PD_HCODEC, 0);
        // remove hcodec isolation
        WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) & ~0x30);
#endif		
    }
    else if (core == VDEC_HEVC) {
        if (HAS_HEVC_VDEC) {
            // hevc power on
            WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & ~0xc0);
            // wait 10uS
            udelay(10);
            // hevc soft reset
            WRITE_VREG(DOS_SW_RESET3, 0xffffffff);
            WRITE_VREG(DOS_SW_RESET3, 0);
            // enable hevc clock
            hevc_clock_enable();
            // power up hevc memories
            WRITE_VREG(DOS_MEM_PD_HEVC, 0);
            // remove hevc isolation
            WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) & ~0xc00);
        }
    }

    spin_unlock_irqrestore(&lock, flags);
}

void vdec_poweroff(vdec_type_t core)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (core == VDEC_1) {
        // enable vdec1 isolation
        WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) | 0xc0);
        // power off vdec1 memories
        WRITE_VREG(DOS_MEM_PD_VDEC, 0xffffffffUL);
        // disable vdec1 clock
        vdec_clock_off();
        // vdec1 power off
        WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 0xc);
    } else if (core == VDEC_2) {
        if (HAS_VDEC2) { 
            // enable vdec2 isolation
            WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) | 0x300);
            // power off vdec2 memories
            WRITE_VREG(DOS_MEM_PD_VDEC2, 0xffffffffUL);
            // disable vdec2 clock
            vdec2_clock_off();
            // vdec2 power off
            WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 0x30);
        }
    } else if (core == VDEC_HCODEC) {
#if  HAS_HDEC    
        // enable hcodec isolation
        WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) | 0x30);
        // power off hcodec memories
        WRITE_VREG(DOS_MEM_PD_HCODEC, 0xffffffffUL);
        // disable hcodec clock
        hcodec_clock_off();
        // hcodec power off
        WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 3);
#endif
    } else if (core == VDEC_HEVC) {
        if (HAS_HEVC_VDEC) {
            // enable hevc isolation
            WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) | 0xc00);
            // power off hevc memories
            WRITE_VREG(DOS_MEM_PD_HEVC, 0xffffffffUL);
            // disable hevc clock
            hevc_clock_off();
            // hevc power off
            WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 0xc0);
        }
    }

    spin_unlock_irqrestore(&lock, flags);
}

bool vdec_on(vdec_type_t core)
{
    bool ret = false;

    if (core == VDEC_1) {
        if (((READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & 0xc) == 0) &&
            (READ_MPEG_REG(HHI_VDEC_CLK_CNTL) & 0x100)) {
            ret = true;
        }
    } else if (core == VDEC_2) {
        if (HAS_VDEC2) {
            if (((READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & 0x30) == 0) &&
                (READ_MPEG_REG(HHI_VDEC2_CLK_CNTL) & 0x100)) {
                ret = true;
            }
        }
    } else if (core == VDEC_HCODEC) {
#if  HAS_HDEC 
        if (((READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & 0x3) == 0) &&
            (READ_MPEG_REG(HHI_VDEC_CLK_CNTL) & 0x1000000)) {
            ret = true;
        }
#endif
    } else if (core == VDEC_HEVC) {
        if (HAS_HEVC_VDEC) { 
            if (((READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) & 0xc0) == 0) &&
                (READ_MPEG_REG(HHI_VDEC2_CLK_CNTL) & 0x1000000)) {
                ret = true;
            }
        }
    }

    return ret;
}

#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
void vdec_poweron(vdec_type_t core)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (core == VDEC_1) {
        // vdec1 soft reset
        WRITE_VREG(DOS_SW_RESET0, 0xfffffffc);
        WRITE_VREG(DOS_SW_RESET0, 0);
        // enable vdec1 clock
        vdec_clock_enable();
        // reset DOS top registers
        WRITE_VREG(DOS_VDEC_MCRCC_STALL_CTRL, 0);
    } else if (core == VDEC_2) {
        // vdec2 soft reset
        WRITE_VREG(DOS_SW_RESET2, 0xffffffff);
        WRITE_VREG(DOS_SW_RESET2, 0);
        // enable vdec2 clock
        vdec2_clock_enable();
        // reset DOS top registers
        WRITE_VREG(DOS_VDEC2_MCRCC_STALL_CTRL, 0);
    } else if (core == VDEC_HCODEC) {
        // hcodec soft reset
        WRITE_VREG(DOS_SW_RESET1, 0xffffffff);
        WRITE_VREG(DOS_SW_RESET1, 0);
        // enable hcodec clock
        hcodec_clock_enable();
    }

    spin_unlock_irqrestore(&lock, flags);
}

void vdec_poweroff(vdec_type_t core)
{
    ulong flags;

    spin_lock_irqsave(&lock, flags);

    if (core == VDEC_1) {
        // disable vdec1 clock
        vdec_clock_off();
    } else if (core == VDEC_2) {
        // disable vdec2 clock
        vdec2_clock_off();
    } else if (core == VDEC_HCODEC) {
        // disable hcodec clock
        hcodec_clock_off();
    }

    spin_unlock_irqrestore(&lock, flags);
}

bool vdec_on(vdec_type_t core)
{
    bool ret = false;

    if (core == VDEC_1) {
        if (READ_MPEG_REG(HHI_VDEC_CLK_CNTL) & 0x100) {
            ret = true;
        }
    } else if (core == VDEC_2) {
        if (READ_MPEG_REG(HHI_VDEC2_CLK_CNTL) & 0x100) {
            ret = true;
        }
    } else if (core == VDEC_HCODEC) {
        if (READ_MPEG_REG(HHI_VDEC_CLK_CNTL) & 0x1000000) {
            ret = true;
        }
    }

    return ret;
}
#endif

void vdec_power_mode(int level)
{
    /* todo: add level routines for clock adjustment per chips */
    ulong flags;
    ulong fiq_flag;

    if (vdec_clock_level(VDEC_1) == level) {
        return;
    }

    spin_lock_irqsave(&lock, flags);
    raw_local_save_flags(fiq_flag);
    local_fiq_disable();

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (!IS_MESON_M8_CPU) {
        vdec_clock_prepare_switch();
    }
#endif

    if (level == 0) {
        vdec_clock_enable();
    } else {
        vdec_clock_hi_enable();
    }

    raw_local_irq_restore(fiq_flag);
    spin_unlock_irqrestore(&lock, flags);
}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
void vdec2_power_mode(int level)
{
    if (HAS_VDEC2) {
        /* todo: add level routines for clock adjustment per chips */
        ulong flags;
        ulong fiq_flag;

        if (vdec_clock_level(VDEC_2) == level) {
            return;
        }

        spin_lock_irqsave(&lock, flags);
        raw_local_save_flags(fiq_flag);
        local_fiq_disable();

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        if (!IS_MESON_M8_CPU) {
            vdec_clock_prepare_switch();
        }
#endif

        if (level == 0) {
            vdec2_clock_enable();
        } else {
            vdec2_clock_hi_enable();
        }

        raw_local_irq_restore(fiq_flag);
        spin_unlock_irqrestore(&lock, flags);
    }
}

static vdec2_usage_t vdec2_usage = USAGE_NONE;
void set_vdec2_usage(vdec2_usage_t usage)
{
    if (HAS_VDEC2) {
        ulong flags;
        spin_lock_irqsave(&lock, flags);
        vdec2_usage = usage;
        spin_unlock_irqrestore(&lock, flags);
    }
}

vdec2_usage_t get_vdec2_usage(void)
{
    if (HAS_VDEC2) {
        return vdec2_usage;
    } else {
        return 0;
    }
}

#endif


static struct am_reg am_risc[] = {
    {"MSP", 0x300},
    {"MPSR", 0x301 },
    {"MCPU_INT_BASE", 0x302 },
    {"MCPU_INTR_GRP", 0x303 },
    {"MCPU_INTR_MSK", 0x304 },
    {"MCPU_INTR_REQ", 0x305 },
    {"MPC-P", 0x306 },
    {"MPC-D", 0x307 },
    {"MPC_E", 0x308 },
    {"MPC_W", 0x309 }
};

static ssize_t amrisc_regs_show(struct class *class, struct class_attribute *attr, char *buf)
{
    char *pbuf = buf;
    struct am_reg *regs = am_risc;
    int rsize = sizeof(am_risc) / sizeof(struct am_reg);
    int i;
    unsigned  val;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    unsigned long flags;
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    spin_lock_irqsave(&lock, flags);
    if(!vdec_on(VDEC_1)){
        spin_unlock_irqrestore(&lock, flags);
        pbuf += sprintf(pbuf, "amrisc is power off\n");
        return (pbuf - buf);
    }
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_VDEC, 1);
#endif
    pbuf += sprintf(pbuf, "amrisc registers show:\n");
    for (i = 0; i < rsize; i++) {
        val = READ_VREG(regs[i].offset);
        pbuf += sprintf(pbuf, "%s(%#x)\t:%#x(%d)\n",
                        regs[i].name, regs[i].offset, val, val);
    }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    spin_unlock_irqrestore(&lock, flags);
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_VDEC, 0);
#endif
    return (pbuf - buf);
}
static ssize_t dump_trace_show(struct class *class, struct class_attribute *attr, char *buf)
{
	int i;
	char *pbuf = buf;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	unsigned long flags;
#endif
	u16 *trace_buf=kmalloc(debug_trace_num*2,GFP_KERNEL);
	if(!trace_buf){
		pbuf += sprintf(pbuf, "No Memory bug\n");
		return (pbuf - buf);
	}
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    spin_lock_irqsave(&lock, flags);
	if(!vdec_on(VDEC_1)){
		spin_unlock_irqrestore(&lock, flags);
		kfree(trace_buf);
		pbuf += sprintf(pbuf, "amrisc is power off\n");
		return (pbuf - buf);
	}
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_VDEC, 1);
#endif
	printk("dump trace steps:%d start \n",debug_trace_num);
	i=0;
	while(i<=debug_trace_num-16){
		trace_buf[i] = READ_VREG(MPC_E);
		trace_buf[i+1] = READ_VREG(MPC_E);
		trace_buf[i+2] = READ_VREG(MPC_E);
		trace_buf[i+3] = READ_VREG(MPC_E);
		trace_buf[i+4] = READ_VREG(MPC_E);
		trace_buf[i+5] = READ_VREG(MPC_E);
		trace_buf[i+6] = READ_VREG(MPC_E);
		trace_buf[i+7] = READ_VREG(MPC_E);
		trace_buf[i+8] = READ_VREG(MPC_E);
		trace_buf[i+9] = READ_VREG(MPC_E);
		trace_buf[i+10] = READ_VREG(MPC_E);
		trace_buf[i+11] = READ_VREG(MPC_E);
		trace_buf[i+12] = READ_VREG(MPC_E);
		trace_buf[i+13] = READ_VREG(MPC_E);
		trace_buf[i+14] = READ_VREG(MPC_E);
		trace_buf[i+15] = READ_VREG(MPC_E);
		i+=16;
	};
    printk("dump trace steps:%d finished \n",debug_trace_num);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    spin_unlock_irqrestore(&lock, flags);
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_type(MOD_VDEC, 0);
#endif
	for(i=0;i<debug_trace_num;i++){
        if(i%4 == 0){
	        if(i%16 == 0)
	        	pbuf += sprintf(pbuf, "\n");
	        else if(i%8 == 0)
	        	pbuf += sprintf(pbuf, "  ");
	        else // 4
	        	pbuf += sprintf(pbuf, " ");
        }
		pbuf += sprintf(pbuf, "%04x:",trace_buf[i]);
	}while(i<debug_trace_num);
	kfree(trace_buf);
	pbuf += sprintf(pbuf, "\n");
	return (pbuf - buf);
}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
static ssize_t clock_level_show(struct class *class, struct class_attribute *attr, char *buf)
{
    char *pbuf = buf;

    pbuf += sprintf(pbuf, "%d\n", vdec_clock_level(VDEC_1));

    if (HAS_VDEC2) {
        pbuf += sprintf(pbuf, "%d\n", vdec_clock_level(VDEC_2));
    }

#ifdef HAD_VDEC_HEVC
    pbuf += sprintf(pbuf, "%d\n", vdec_clock_level(VDEC_HEVC);
#endif

    return (pbuf - buf);
}
static ssize_t store_poweron_clock_level(struct class *class, struct class_attribute *attr, const char *buf, size_t size)

{
    unsigned val;
    ssize_t ret;

    ret = sscanf(buf, "%d", &val);     
    if(ret != 1 ) {
        return -EINVAL;
    }  
    poweron_clock_level = val;
    return size;
}
static ssize_t show_poweron_clock_level(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", poweron_clock_level);;
}
#endif

static struct class_attribute vdec_class_attrs[] = {
    __ATTR_RO(amrisc_regs),
	__ATTR_RO(dump_trace),
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    __ATTR_RO(clock_level),
    __ATTR(poweron_clock_level, S_IRUGO | S_IWUSR | S_IWGRP, show_poweron_clock_level, store_poweron_clock_level),    
#endif
    __ATTR_NULL
};

static struct class vdec_class = {
        .name = "vdec",
        .class_attrs = vdec_class_attrs,
    };

static int vdec_probe(struct platform_device *pdev)
{
    s32 r;
    const void * name;
    int offset, size;
    unsigned long start, end;

    r = class_register(&vdec_class);
    if (r) {
        printk("vdec class create fail.\n");
        return r;
    }

    vdec_core_device = pdev;

    r = find_reserve_block(pdev->dev.of_node->name,0);

    if(r < 0){
        name = of_get_property(pdev->dev.of_node,"share-memory-name",NULL);
	if(!name){
            printk("can not find %s%d reserve block1\n",vdec_class.name,0);
            r = -EFAULT;
            goto error;

	}else{
            r= find_reserve_block_by_name(name);
            if(r<0){
                printk("can not find %s%d reserve block2\n",vdec_class.name,0);
                r = -EFAULT;
                goto error;
            }
            name= of_get_property(pdev->dev.of_node,"share-memory-offset",NULL);
            if(name)
                offset= of_read_ulong(name,1);
            else{
                printk("can not find %s%d reserve block3\n",vdec_class.name,0);
                r = -EFAULT;
                goto error;
            }
            name= of_get_property(pdev->dev.of_node,"share-memory-size",NULL);
            if(name)
                size= of_read_ulong(name,1);
            else{
                printk("can not find %s%d reserve block4\n",vdec_class.name,0);
                r = -EFAULT;
                goto error;
            }			
            start = (phys_addr_t)get_reserve_block_addr(r)+ offset;
            end = start+ size-1;
        }
    }else{
        start = (phys_addr_t)get_reserve_block_addr(r);
        end = start+ (phys_addr_t)get_reserve_block_size(r)-1;
    }

    printk("init vdec memsource %lx->%lx\n", start, end);

    vdec_set_resource(start, end, &pdev->dev);

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6TVD
    /* default to 250MHz */
    vdec_clock_hi_enable();
#endif
    return 0;

error:
    class_unregister(&vdec_class);

    return r;
}

static int  vdec_remove(struct platform_device *pdev)
{
    class_unregister(&vdec_class);

    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_vdec_dt_match[]={
	{	.compatible = "amlogic,vdec",
	},
	{},
};
#else
#define amlogic_vdec_dt_match NULL
#endif

static struct platform_driver
        vdec_driver = {
    .probe      = vdec_probe,
    .remove     = vdec_remove,
    .driver     = {
        .name   = "vdec",
        .of_match_table = amlogic_vdec_dt_match,
    }
};

static int __init vdec_module_init(void)
{
    if (platform_driver_register(&vdec_driver)) {
        printk("failed to register amstream module\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit vdec_module_exit(void)
{
    platform_driver_unregister(&vdec_driver);
    return ;
}
module_param(debug_trace_num, uint, 0664);

module_init(vdec_module_init);
module_exit(vdec_module_exit);

MODULE_DESCRIPTION("AMLOGIC vdec  driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");


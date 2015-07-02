/*
 * Amlogic Apollo
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
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <mach/am_regs.h>

#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/logo/logo.h>
#include "tvoutc.h"
#include "tvconf.h"
#include <linux/clk.h>
#include <plat/io.h>
#include <mach/tvregs.h>
#include <mach/mod_gate.h>
#include <linux/amlogic/vout/enc_clk_config.h>
#include <linux/amlogic/vout/vout_notify.h>

static u32 curr_vdac_setting=DEFAULT_VDAC_SEQUENCE;

#define  SET_VDAC(index,val)   (aml_write_reg32(CBUS_REG_ADDR(index+VENC_VDAC_DACSEL0),val))
static const unsigned int  signal_set[SIGNAL_SET_MAX][3]=
{
	{	VIDEO_SIGNAL_TYPE_INTERLACE_Y,     // component interlace
		VIDEO_SIGNAL_TYPE_INTERLACE_PB,
		VIDEO_SIGNAL_TYPE_INTERLACE_PR,
	},
	{
		VIDEO_SIGNAL_TYPE_CVBS,            	//cvbs&svideo
		VIDEO_SIGNAL_TYPE_SVIDEO_LUMA,
    	VIDEO_SIGNAL_TYPE_SVIDEO_CHROMA,
	},
	{	VIDEO_SIGNAL_TYPE_PROGRESSIVE_Y,     //progressive.
		VIDEO_SIGNAL_TYPE_PROGRESSIVE_PB,
		VIDEO_SIGNAL_TYPE_PROGRESSIVE_PR,
	},
	{
	    VIDEO_SIGNAL_TYPE_PROGEESSIVE_B,     //Analog RGB for VGA.
		VIDEO_SIGNAL_TYPE_PROGEESSIVE_G,
		VIDEO_SIGNAL_TYPE_PROGEESSIVE_R,
	},

};
static  const  char*   signal_table[]={
	"INTERLACE_Y ", /**< Interlace Y signal */
    	"CVBS",            /**< CVBS signal */
    	"SVIDEO_LUMA",     /**< S-Video luma signal */
    	"SVIDEO_CHROMA",   /**< S-Video chroma signal */
    	"INTERLACE_PB",    /**< Interlace Pb signal */
    	"INTERLACE_PR",    /**< Interlace Pr signal */
    	"INTERLACE_R",     /**< Interlace R signal */
         "INTERLACE_G",     /**< Interlace G signal */
         "INTERLACE_B",     /**< Interlace B signal */
         "PROGRESSIVE_Y",   /**< Progressive Y signal */
         "PROGRESSIVE_PB",  /**< Progressive Pb signal */
         "PROGRESSIVE_PR",  /**< Progressive Pr signal */
         "PROGEESSIVE_R",   /**< Progressive R signal */
         "PROGEESSIVE_G",   /**< Progressive G signal */
         "PROGEESSIVE_B",   /**< Progressive B signal */

	};
int 	 get_current_vdac_setting(void)
{
	return curr_vdac_setting;
}

extern unsigned int clk_util_clk_msr(unsigned int clk_mux);

//120120
void  change_vdac_setting(unsigned int  vdec_setting,vmode_t  mode)
{
	unsigned  int  signal_set_index=0;
	unsigned int  idx=0,bit=5,i;
	switch(mode )
	{
		case VMODE_480I:
		case VMODE_576I:
		signal_set_index=0;
		bit=5;
		break;
		case VMODE_480CVBS:
		case VMODE_576CVBS:
		signal_set_index=1;
		bit=2;
		break;
		case VMODE_SVGA:
		case VMODE_XGA:
		case VMODE_VGA:
		signal_set_index=3;
		bit=5;
		break;
		default :
		signal_set_index=2;
		bit=5;
		break;
	}
	for(i=0;i<3;i++)
	{
		idx=vdec_setting>>(bit<<2)&0xf;
		printk("dac index:%d ,signal:%s\n",idx,signal_table[signal_set[signal_set_index][i]]);
		SET_VDAC(idx,signal_set[signal_set_index][i]);
		bit--;
	}
	curr_vdac_setting=vdec_setting;
}

#if 0
static void enable_vsync_interrupt(void)
{
    printk("enable_vsync_interrupt\n");

    CLEAR_CBUS_REG_MASK(HHI_MPEG_CLK_CNTL, 1<<11);

    if (READ_MPEG_REG(ENCP_VIDEO_EN) & 1) {
        WRITE_MPEG_REG(VENC_INTCTRL, 0x200);

#ifdef CONFIG_ARCH_MESON1
        while ((READ_MPEG_REG(VENC_INTFLAG) & 0x200) == 0) {
            u32 line1, line2;

            line1 = line2 = READ_MPEG_REG(VENC_ENCP_LINE);

            while (line1 >= line2) {
                line2 = line1;
                line1 = READ_MPEG_REG(VENC_ENCP_LINE);
            }

            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            if (READ_MPEG_REG(VENC_INTFLAG) & 0x200) {
                break;
            }

            WRITE_MPEG_REG(ENCP_VIDEO_EN, 0);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);

            WRITE_MPEG_REG(ENCP_VIDEO_EN, 1);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
            READ_MPEG_REG(VENC_INTFLAG);
        }
#else
        while ((READ_MPEG_REG(VENC_INTFLAG) & 0x200) == 0) {
            mdelay(50);
            WRITE_MPEG_REG(ENCP_VIDEO_EN, 0);
            READ_MPEG_REG(VENC_INTFLAG);
            WRITE_MPEG_REG(ENCP_VIDEO_EN, 1);
            printk("recycle TV encoder\n");
        }
#endif
    }
    else{
        WRITE_MPEG_REG(VENC_INTCTRL, 0x2);
    }

    printk("Enable vsync done\n");
}
#endif

int tvoutc_setclk(tvmode_t mode)
{
	struct clk *clk;
	const  reg_t *sd,*hd;
	int xtal;

	sd=tvreg_vclk_sd;
	hd=tvreg_vclk_hd;

	clk=clk_get_sys("clk_xtal", NULL);
	if(!clk)
	{
		printk(KERN_ERR "can't find clk %s for VIDEO PLL SETTING!\n\n","clk_xtal");
		return -1;
	}
	xtal=clk_get_rate(clk);
	xtal=xtal/1000000;
	if(xtal>=24 && xtal <=25)/*current only support 24,25*/
		{
		xtal-=24;
		}
	else
		{
		printk(KERN_WARNING "UNsupport xtal setting for vidoe xtal=%d,default to 24M\n",xtal);
		xtal=0;
		}
	switch(mode)
	{
		case TVMODE_480I:
		case TVMODE_480I_RPT:
		case TVMODE_480CVBS:
		case TVMODE_480P:
		case TVMODE_480P_RPT:
		case TVMODE_576I:
		case TVMODE_576I_RPT:
		case TVMODE_576CVBS:
		case TVMODE_576P:
			  setreg(&sd[xtal]);
			  break;
		case TVMODE_720P:
		case TVMODE_800P:
        case TVMODE_800X480P_60HZ:
        case TVMODE_720P_50HZ:
		case TVMODE_1080I:
		case TVMODE_1080I_50HZ:
		case TVMODE_1080P:
		case TVMODE_1080P_50HZ:
		case TVMODE_SVGA:
		case TVMODE_SXGA:
		case TVMODE_1920x1200:
			  setreg(&hd[xtal]);
			  if(xtal == 1)
			  {
				WRITE_MPEG_REG(HHI_VID_CLK_DIV, 4);
			  }
			  break;
		default:
			//printk(KERN_ERR "unsupport tv mode,video clk is not set!!\n");
            break;
	}

	return 0 ;
}

static void set_tvmode_misc(tvmode_t mode)
{
    set_vmode_clk(mode);
}

/*
 * uboot_display_already() uses to judge whether display has already
 * be set in uboot.
 * Here, first read the value of reg P_ENCP_VIDEO_MAX_PXCNT and
 * P_ENCP_VIDEO_MAX_LNCNT, then compare with value of tvregsTab[mode]
 */
static int uboot_display_already(tvmode_t mode)
{
    tvmode_t source = vmode_to_tvmode(get_resolution_vmode());
    if(source == mode)
        return 1;
    else
        return 0;
    /*
    const  reg_t *s = tvregsTab[mode];
    unsigned int pxcnt_tab = 0;
    unsigned int lncnt_tab = 0;

    while(s->reg != MREG_END_MARKER) {
        if(s->reg == P_ENCP_VIDEO_MAX_PXCNT) {
            pxcnt_tab = s->val;
        }
        if(s->reg == P_ENCP_VIDEO_MAX_LNCNT) {
            lncnt_tab = s->val;
        }
        s++;
    }

    if((pxcnt_tab == aml_read_reg32(P_ENCP_VIDEO_MAX_PXCNT)) &&
       (lncnt_tab == aml_read_reg32(P_ENCP_VIDEO_MAX_LNCNT))) {
        return 1;
    } else {
        return 0;
    }
    */
}

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
static unsigned int vdac_cfg_valid = 0, vdac_cfg_value = 0;
static unsigned int cvbs_get_trimming_version(unsigned int flag)
{
	unsigned int version = 0xff;
	
	if( (flag&0xf0) == 0xa0 )
		version = 5;
	else if( (flag&0xf0) == 0x40 )
		version = 2;
	else if( (flag&0xc0) == 0x80 )
		version = 1;
	else if( (flag&0xc0) == 0x00 )
		version = 0;

	return version;
}

void cvbs_config_vdac(unsigned int flag, unsigned int cfg)
{
	unsigned char version = 0;

	vdac_cfg_value = cfg&0x7;

	version = cvbs_get_trimming_version(flag);

	// flag 1/0 for validity of vdac config
	if( (version==1) || (version==2) || (version==5) )
		vdac_cfg_valid = 1;
	else
		vdac_cfg_valid = 0;

	printk("cvbs trimming.%d.v%d: 0x%x, 0x%x\n", vdac_cfg_valid, version, flag, cfg);

	return ;
}

void cvbs_cntl_output(unsigned int open)
{
	unsigned int cntl0=0, cntl1=0;

	if( open == 0 )// close
	{
		cntl0 = 0;
		cntl1 = 8;

		WRITE_MPEG_REG(HHI_VDAC_CNTL0, cntl0);
		WRITE_MPEG_REG(HHI_VDAC_CNTL1, cntl1);
	}
	else if( open == 1 )// open
	{
		cntl0 = 0x1;
		cntl1 = (vdac_cfg_valid==0)?0:vdac_cfg_value;

		printk("vdac open.%d = 0x%x, 0x%x\n", vdac_cfg_valid, cntl0, cntl1);

		WRITE_MPEG_REG(HHI_VDAC_CNTL1, cntl1);
		WRITE_MPEG_REG(HHI_VDAC_CNTL0, cntl0);
	}

	return ;
}
#endif

static unsigned int cvbs_performance_index = 0xff;// 0xff for none config from uboot
void cvbs_performance_config(unsigned int index)
{
	cvbs_performance_index = index;
	return ;
}

#ifdef CONFIG_CVBS_PERFORMANCE_COMPATIBLITY_SUPPORT
static void cvbs_performance_enhancement(tvmode_t mode)
{
	const reg_t *s;
	unsigned int index = cvbs_performance_index;
	unsigned int max = sizeof(tvregs_576cvbs_performance)/sizeof(reg_t*);

	if( TVMODE_576CVBS != mode )
		return ;

	if( 0xff == index )
		return ;

	index = (index>=max)?0:index;
	printk("cvbs performance use table = %d\n", index);
	s = tvregs_576cvbs_performance[index];
	while (MREG_END_MARKER != s->reg)
	{
    	setreg(s++);
	}
	return ;
}

#endif// end of CVBS_PERFORMANCE_COMPATIBLITY_SUPPORT

static DEFINE_MUTEX(setmode_mutex);

int tvoutc_setmode(tvmode_t mode)
{
    const  reg_t *s;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    static int uboot_display_flag = 1;
#else
    static int uboot_display_flag = 0;
#endif
    if (mode >= TVMODE_MAX) {
        printk(KERN_ERR "Invalid video output modes.\n");
        return -ENODEV;
    }
    mutex_lock(&setmode_mutex);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
//TODO
//    switch_mod_gate_by_name("venc", 1);
#endif
    printk("TV mode %s selected.\n", tvinfoTab[mode].id);

#ifdef CONFIG_ARCH_MESON8B
	if( (mode!=TVMODE_480CVBS) && (mode!=TVMODE_576CVBS) )
	{
		CLK_GATE_OFF(CTS_VDAC);
		CLK_GATE_OFF(DAC_CLK);
	}
	if( (mode!=TVMODE_480I) && (mode!=TVMODE_480CVBS) &&
		(mode!=TVMODE_576I) && (mode!=TVMODE_576CVBS) )
	{
		CLK_GATE_OFF(CTS_ENCI);
		CLK_GATE_OFF(VCLK2_ENCI);
		CLK_GATE_OFF(VCLK2_VENCI1);
	}
#endif

    s = tvregsTab[mode];

    if(uboot_display_flag) {
        uboot_display_flag = 0;
        if(uboot_display_already(mode)) {
            printk("already display in uboot\n");
            mutex_unlock(&setmode_mutex);
            return 0;
        }
    }

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
	// for hdmi mode, disable HPLL as soon as possible
	if( (mode==TVMODE_480I) || (mode==TVMODE_480P) ||
		(mode==TVMODE_576I) || (mode==TVMODE_576P) ||
		(mode==TVMODE_720P) || (mode==TVMODE_720P_50HZ) ||
		(mode==TVMODE_1080I) || (mode==TVMODE_1080I_50HZ) ||
		(mode==TVMODE_1080P) || (mode==TVMODE_1080P_50HZ) ||
		(mode==TVMODE_1080P_24HZ) || (mode==TVMODE_4K2K_24HZ) ||
		(mode==TVMODE_4K2K_25HZ) || (mode==TVMODE_4K2K_30HZ) ||
		(mode==TVMODE_4K2K_SMPTE) )
	{
		WRITE_CBUS_REG_BITS(HHI_VID_PLL_CNTL, 0x0, 30, 1);
	}

    cvbs_cntl_output(0);
#endif

    while (MREG_END_MARKER != s->reg)
        setreg(s++);
    printk("%s[%d]\n", __func__, __LINE__);

#ifdef CONFIG_CVBS_PERFORMANCE_COMPATIBLITY_SUPPORT
	cvbs_performance_enhancement(mode);
#endif

    if(mode >= TVMODE_VGA && mode <= TVMODE_FHDVGA){ //set VGA pinmux
        aml_write_reg32(P_PERIPHS_PIN_MUX_0, (aml_read_reg32(P_PERIPHS_PIN_MUX_0)|(3<<20)));
    }else{
	aml_write_reg32(P_PERIPHS_PIN_MUX_0, (aml_read_reg32(P_PERIPHS_PIN_MUX_0)&(~(3<<20))));
    }

#if ((defined CONFIG_ARCH_MESON8) || (defined CONFIG_ARCH_MESON8B))
	// for hdmi mode, leave the hpll setting to be done by hdmi module.
	if( (mode==TVMODE_480CVBS) || (mode==TVMODE_576CVBS) )
		set_tvmode_misc(mode);
#else
	set_tvmode_misc(mode);
#endif

#ifdef CONFIG_ARCH_MESON1
	tvoutc_setclk(mode);
    printk("%s[%d]\n", __func__, __LINE__);
    enable_vsync_interrupt();
#endif
#ifdef CONFIG_AM_TV_OUTPUT2
	switch(mode)
	{
		case TVMODE_480I:
		case TVMODE_480I_RPT:
		case TVMODE_480CVBS:
		case TVMODE_576I:
		case TVMODE_576I_RPT:
		case TVMODE_576CVBS:
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 1, 0, 2); //reg0x271a, select ENCI to VIU1
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 1, 4, 4); //reg0x271a, Select encI clock to VDIN
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 1, 8, 4); //reg0x271a,Enable VIU of ENC_I domain to VDIN;
			  break;
		case TVMODE_480P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_480P_59HZ:
#endif
		case TVMODE_480P_RPT:
		case TVMODE_576P:
		case TVMODE_576P_RPT:
        case TVMODE_800X480P_60HZ:
		case TVMODE_720P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_720P_59HZ:
#endif
		case TVMODE_720P_50HZ:
		case TVMODE_1080I: //??
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_1080I_59HZ:
#endif
		case TVMODE_1080I_50HZ: //??
		case TVMODE_1080P:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_1080P_59HZ:
#endif
		case TVMODE_1080P_50HZ:
		case TVMODE_1080P_24HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_1080P_23HZ:
#endif
        case TVMODE_4K2K_30HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_4K2K_29HZ:
#endif
        case TVMODE_4K2K_25HZ:
        case TVMODE_4K2K_24HZ:
#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
		case TVMODE_4K2K_23HZ:
#endif
        case TVMODE_4K2K_SMPTE:
		case TVMODE_VGA:
		case TVMODE_SVGA:
		case TVMODE_XGA:
		case TVMODE_SXGA:
		case TVMODE_WSXGA:
		case TVMODE_FHDVGA:
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 2, 0, 2); //reg0x271a, select ENCP to VIU1
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 2, 4, 4); //reg0x271a, Select encP clock to VDIN
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 2, 8, 4); //reg0x271a,Enable VIU of ENC_P domain to VDIN;
        break;
		default:
			printk(KERN_ERR "unsupport tv mode,video clk is not set!!\n");
	}
#endif

    aml_write_reg32(P_VPP_POSTBLEND_H_SIZE, tvinfoTab[mode].xres);

#ifdef CONFIG_ARCH_MESON3
printk(" clk_util_clk_msr 6 = %d\n", clk_util_clk_msr(6));
printk(" clk_util_clk_msr 7 = %d\n", clk_util_clk_msr(7));
printk(" clk_util_clk_msr 8 = %d\n", clk_util_clk_msr(8));
printk(" clk_util_clk_msr 9 = %d\n", clk_util_clk_msr(9));
printk(" clk_util_clk_msr 10 = %d\n", clk_util_clk_msr(10));
printk(" clk_util_clk_msr 27 = %d\n", clk_util_clk_msr(27));
printk(" clk_util_clk_msr 29 = %d\n", clk_util_clk_msr(29));
#endif

#ifdef CONFIG_ARCH_MESON6
	if( (mode==TVMODE_480CVBS) || (mode==TVMODE_576CVBS) )
	{
		msleep(1000);

		if(get_power_level() == 0) {
		    aml_write_reg32(P_VENC_VDAC_SETTING, 0x5);
		} else {
		    aml_write_reg32(P_VENC_VDAC_SETTING, 0x7);
		}
	} else {
		if(get_power_level() == 0) {
		    aml_write_reg32(P_VENC_VDAC_SETTING, 0x0);
		} else {
		    aml_write_reg32(P_VENC_VDAC_SETTING, 0x7);
		}
	}
#endif

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
    if( (mode==TVMODE_480CVBS) || (mode==TVMODE_576CVBS) )
    {
        msleep(1000);
#ifdef CONFIG_ARCH_MESON8B
		CLK_GATE_ON(VCLK2_ENCI);
		CLK_GATE_ON(VCLK2_VENCI1);
        CLK_GATE_ON(CTS_ENCI);
        CLK_GATE_ON(CTS_VDAC);
		CLK_GATE_ON(DAC_CLK);
#endif
        cvbs_cntl_output(1);
    }
#endif
//while(1);
    mutex_unlock(&setmode_mutex);
    return 0;
}


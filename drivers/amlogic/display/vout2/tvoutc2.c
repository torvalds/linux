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

#include <mach/am_regs.h>

#include <linux/amlogic/vout/vinfo.h>
#include "tvoutc.h"
#include <linux/clk.h>
#include <mach/mod_gate.h>


static u32 curr_vdac_setting=DEFAULT_VDAC_SEQUENCE;
#include "tvregs.h"

#define  SET_VDAC(index,val)   (WRITE_MPEG_REG((index+VENC_VDAC_DACSEL0),val))
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
int 	 get_current_vdac_setting2(void)
{
	return curr_vdac_setting;
}

extern unsigned int clk_util_clk_msr(unsigned int clk_mux);

//120120
void  change_vdac_setting2(unsigned int  vdec_setting,vmode_t  mode)
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

#if 0
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
int tvoutc_setclk2(tvmode_t mode)
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
		case TVMODE_480CVBS:
		case TVMODE_480P:
		case TVMODE_576I:
		case TVMODE_576CVBS:
		case TVMODE_576P:
			  setreg(&sd[xtal]);
			  break;
		case TVMODE_720P:
		case TVMODE_720P_50HZ:
		case TVMODE_1080I:
		case TVMODE_1080I_50HZ:
		case TVMODE_1080P:
		case TVMODE_1080P_50HZ:
			  setreg(&hd[xtal]);
			  if(xtal == 1)
			  {
				WRITE_MPEG_REG(HHI_VID_CLK_DIV, 4);
			  }
			  break;
		default:
			printk(KERN_ERR "unsupport tv mode,video clk is not set!!\n");	
	}

	return 0;
}

int tvoutc_setmode2(tvmode_t mode)
{
    const  reg_t *s;

    if (mode >= TVMODE_MAX) {
        printk(KERN_ERR "Invalid video output modes.\n");
        return -ENODEV;
    }

    printk(KERN_DEBUG "TV mode %s selected.\n", tvinfoTab[mode].id);
   
    s = tvregsTab[mode];
			
    while (MREG_END_MARKER != s->reg)
        setreg(s++);
	//tvoutc_setclk2(mode);
    //enable_vsync_interrupt();
	switch(mode)
	{
		case TVMODE_480I:
		case TVMODE_480CVBS:
		case TVMODE_480P:
		case TVMODE_576I:
		case TVMODE_576CVBS:
		case TVMODE_576P:
        //WRITE_MPEG_REG(HHI_VIID_CLK_DIV, (READ_MPEG_REG(HHI_VIID_CLK_DIV)&(~(0xff)))|0x3); // reg 0x104a
			  break;
		case TVMODE_720P:
		case TVMODE_720P_50HZ:
		case TVMODE_1080I:
		case TVMODE_1080I_50HZ:
        //WRITE_MPEG_REG(HHI_VIID_CLK_DIV, (READ_MPEG_REG(HHI_VIID_CLK_DIV)&(~(0xff)))|0x0); // reg 0x104a
        break;		    
		case TVMODE_1080P:
		case TVMODE_1080P_50HZ:
        //WRITE_MPEG_REG(HHI_VIID_CLK_DIV, (READ_MPEG_REG(HHI_VIID_CLK_DIV)&(~(0xff)))|0x1); // reg 0x104a
        break;		    
		default:
			printk(KERN_ERR "unsupport tv mode,video clk is not set!!\n");	
	}
    
        //WRITE_MPEG_REG(HHI_VIID_CLK_CNTL, 0x8001f); //reg 0x104b, select vid_pll_clk as source of v2_clk_divN
        //WRITE_MPEG_REG(HHI_VID_CLK_DIV, (READ_MPEG_REG(HHI_VID_CLK_DIV)&(~(0xff<<24)))|(0x88<<24)); // reg 0x1059, select cts_encp_clk and cts_enci_clk from v2_clk_div1
        aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, 2, 2, 2); //reg0x271a, select ENCP to VIU2

    
    aml_write_reg32(P_VPP2_POSTBLEND_H_SIZE, tvinfoTab[mode].xres);
    
// For debug only
#if 0
printk(" clk_util_clk_msr 6 = %d\n", clk_util_clk_msr(6));
printk(" clk_util_clk_msr 7 = %d\n", clk_util_clk_msr(7));
printk(" clk_util_clk_msr 8 = %d\n", clk_util_clk_msr(8));
printk(" clk_util_clk_msr 9 = %d\n", clk_util_clk_msr(9));
printk(" clk_util_clk_msr 10 = %d\n", clk_util_clk_msr(10));
printk(" clk_util_clk_msr 27 = %d\n", clk_util_clk_msr(27));
printk(" clk_util_clk_msr 29 = %d\n", clk_util_clk_msr(29));
#endif
//while(1);


    return 0;
}




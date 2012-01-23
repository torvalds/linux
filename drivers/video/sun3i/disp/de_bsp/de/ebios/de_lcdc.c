
#include "lcd_tv_bsp_i.h"

__u32 lcdc_reg_base0 = 0;
__u32 lcdc_reg_base1 = 0;

#define ____SEPARATOR_LCDC____

__s32 LCDC_set_reg_base(__u32 sel, __u32 address)
{
    if(sel == 0)
    {
	    lcdc_reg_base0 = address;
	}
	else if(sel == 1)
	{
	    lcdc_reg_base1 = address;
	}
	return 0;
}

__u32 LCDC_get_reg_base(__u32 sel)
{
    if(sel == 0)
    {
	    return lcdc_reg_base0;
	}
	else if(sel == 1)
	{
	    return lcdc_reg_base1;
	}
	return 0;
}

__s32 LCDC_init(__u32 sel)
{
	LCDC_enable_int(sel, VBI_LCD_EN);
	LCDC_enable_int(sel, VBI_HD_EN);
	LCDC_enable_int(sel, LINE_TRG_LCD_EN);
	LCDC_enable_int(sel, LINE_TRG_HD_EN);

	TCON0_select_src(sel,0);
	TCON1_select_src(sel,0);

	LCDC_open(sel);

	return 0;
}


__s32 LCDC_exit(__u32 sel)
{
	LCDC_disable_int(sel, ALL_INT);

	LCDC_close(sel);

	return 0;
}

void LCDC_open(__u32 sel)
{
    __u32 tmp;

    //lcd clock enable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_DCLK_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_DCLK_OFF,1,tmp | 0x8000);

	//whole module enable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_CTL_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_CTL_OFF,1,tmp | 0x8000);
}

void LCDC_close(__u32 sel)
{
    __u32 tmp;

    //lcd clock disable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_DCLK_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_DCLK_OFF,1,tmp & 0x7fff);

	//whole module disable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_CTL_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_CTL_OFF,1,tmp & 0x7fff);
}


__s32 LCDC_set_start_delay(__u32 sel, __u8 delay)
{
	__u32 tmp;

    tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF)&0xffffff83;//clear bit6:2
    tmp |= ((delay&0x1f)<<2);
    LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp);
    return 0;
}



__s32 LCDC_get_start_delay(__u32 sel)
{
	__u32 tmp;

    tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF)&0x0000007c;
    tmp >>= 2;

    return tmp;
}

__s32 LCDC_get_cur_line_num(__u32 sel, __u32 tcon_index)
{
	__s32 tmp = -1;

    if(tcon_index == 0)
    {
        tmp = LCDC_RUINT32(sel, LCDC_DUBUG_OFF)&0x03ff0000;
        tmp >>= 16;
    }
    else if(tcon_index == 1)
    {
        tmp = LCDC_RUINT32(sel, LCDC_DUBUG_OFF)&0x00000fff;
    }

    return tmp;
}

__s32 LCDC_enable_output(__u32 sel)
{
	__u32 tmp;

	tmp = LCDC_RUINT16IDX(sel, LCDC_MODE_OFF,1);
	tmp = tmp|0x02;
	LCDC_WUINT16IDX(sel, LCDC_MODE_OFF,1,tmp);

	return 0;
}

__s32 LCDC_disable_output(__u32 sel)
{
	__u32 tmp;

	tmp = LCDC_RUINT16IDX(sel, LCDC_MODE_OFF,1);
	tmp = tmp&(~0x03);
	tmp = tmp|0x02;
	LCDC_WUINT16IDX(sel, LCDC_MODE_OFF,1,tmp);

	return 0;
}

__s32 LCDC_set_output(__u32 sel, __bool value)
{
	__u32 tmp;

	tmp = LCDC_RUINT16IDX(sel, LCDC_MODE_OFF,1);
	if(value)
	{
		tmp = tmp|(0x03);
	}
	else
	{
		tmp = tmp&(~0x03);
		tmp = tmp|0x02;
	}
	LCDC_WUINT16IDX(sel, LCDC_MODE_OFF,1,tmp);

	return 0;
}

__s32 LCDC_set_tcon0_int_line(__u32 sel, __u32 line)
{
    __u32 tmp;

	LCDC_disable_int(sel, LINE_TRG_LCD_EN);

    tmp = LCDC_RUINT32(sel, LCDC_STS_OFF);
    tmp = (tmp & 0xfff003ff)|((line&0x3ff)<<10);
	LCDC_WUINT32(sel, LCDC_STS_OFF,tmp);

	LCDC_enable_int(sel, LINE_TRG_LCD_EN);

	return 0;
}


__s32 LCDC_set_tcon1_int_line(__u32 sel, __u32 line)
{
    __u32 tmp;

	LCDC_disable_int(sel, LINE_TRG_HD_EN);

    tmp = LCDC_RUINT32(sel, LCDC_STS_OFF);
    tmp = (tmp & 0xfffffc00) | (line&0x3ff);
	LCDC_WUINT32(sel, LCDC_STS_OFF,tmp);

	LCDC_enable_int(sel, LINE_TRG_HD_EN);

	return 0;
}

__s32 LCDC_enable_int(__u32 sel, __u32 irqsrc)
{
    __u8 tmp;

	tmp = LCDC_RUINT8IDX(sel, LCDC_STS_OFF,3);
	tmp = tmp | (irqsrc & 0xf0);
	LCDC_WUINT8IDX(sel, LCDC_STS_OFF,3,tmp );

    return 0;
}
__s32 LCDC_disable_int(__u32 sel, __u32 irqsrc)
{
    __u8 tmp;

    tmp = LCDC_RUINT8IDX(sel, LCDC_STS_OFF,3);
    tmp = tmp &( (~irqsrc)| 0x0f);
    LCDC_WUINT8IDX(sel, LCDC_STS_OFF,3,tmp );

    return 0;
}
__u32 LCDC_query_int(__u32 sel)
{
   __u32 ret;

   ret = LCDC_RUINT8IDX(sel, LCDC_STS_OFF,3)   & 0x0f;

   return ret;
}

__s32 LCDC_clear_int(__u32 sel, __u8 int_mun)
{
	__u8 tmp;

	tmp = LCDC_RUINT8IDX(sel, LCDC_STS_OFF,3);
	tmp = (tmp & 0xf0) | ((~int_mun) & 0x0f);
	LCDC_WUINT8IDX(sel, LCDC_STS_OFF,3,tmp );

	return 0;
}

#define ____SEPARATOR_TCON0____


__s32 TCON0_open(__u32 sel)
{
    __u32 tmp;

    //lcd timming generator enable
    tmp = LCDC_RUINT16IDX   (sel, LCDC_CTL_OFF,1);
    LCDC_WUINT16IDX         (sel, LCDC_CTL_OFF,1,tmp | 0x4000);

    return 0;
}

__s32 TCON0_close(__u32 sel)
{
	__u32 tmp;

	//lcd timming generator disable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_CTL_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_CTL_OFF,1,tmp & 0xbfff);

	LCDC_WUINT32(sel, LCDC_IOCTL2_OFF, 0xffffffff);

	return 0;
}

void TCON0_cfg(__u32 sel, __ebios_panel_para_t * info)
{
    __u32 tmp;
	__u32 vblank_len;

    vblank_len = info->lcd_vt/2 - info->lcd_y;
	if(vblank_len > 30)
	{
		info->start_delay	= 30;
	}
	else
	{
		info->start_delay	= vblank_len - 1;
	}


    tmp = LCDC_RUINT32(sel, LCDC_CTL_OFF);
    LCDC_WUINT32(sel, LCDC_CTL_OFF,tmp | (info->lcd_if <<28) | (info->lcd_swap<< 20) | ((info->lcd_x - 1)<< 10)| (info->lcd_y - 1));

	tmp = LCDC_RUINT32(sel, LCDC_DCLK_OFF);
	LCDC_WUINT32(sel, LCDC_DCLK_OFF, tmp |((__u32)(1<<31/*lcd clk enable*/)));

	LCDC_WUINT32(sel, LCDC_BASIC0_OFF,(info->lcd_uf <<31)|(info->lcd_ht <<10)|info->lcd_hbp);

	LCDC_WUINT32(sel, LCDC_BASIC1_OFF,(info->lcd_vt <<10)|info->lcd_vbp);

	if(info->lcd_if == LCDIF_HV)
	{
        LCDC_WUINT32(sel, LCDC_MODE_OFF,(info->lcd_hv_if<<31)     | (info->lcd_hv_smode<<30)   |(info->lcd_hv_s888_if<<24)    |
                                    (info->lcd_hv_syuv_if<<20)| (info->lcd_hv_lde_used<<17)|(info->lcd_hv_lde_iovalue<<16)|
                                    (info->lcd_hv_vspw<<10)   | info->lcd_hv_hspw);
	}
	else if(info->lcd_if == LCDIF_TTL)
	{
	    LCDC_WUINT32(sel, LCDC_MODE_OFF,(info->lcd_ttl_stvh<<20) | (info->lcd_ttl_stvdl<<10) |(info->lcd_ttl_stvdp));
	}
	else if(info->lcd_if == LCDIF_CPU)
	{
		LCDC_WUINT32(sel, LCDC_MODE_OFF,(info->lcd_cpu_if<<29) |(1<<26));
	}
	else
	{
	   LCDC_WUINT32(sel, LCDC_MODE_OFF,0);
	}

	LCDC_WUINT32(sel, LCDC_TTL1_OFF,(info->lcd_ttl_ckvt<<30) |(info->lcd_ttl_ckvh<<10) | (info->lcd_ttl_ckvd<<0));

	LCDC_WUINT32(sel, LCDC_TTL2_OFF,(info->lcd_ttl_oevt<<30) |(info->lcd_ttl_oevh<<10) | (info->lcd_ttl_oevd<<0));

	LCDC_WUINT32(sel, LCDC_TTL3_OFF,(info->lcd_ttl_sthh<<26) |(info->lcd_ttl_sthd<<16) | (info->lcd_ttl_oehh<<10) |
	                    (info->lcd_ttl_oehd<<0));

	LCDC_WUINT32(sel, LCDC_TTL4_OFF,(info->lcd_ttl_datarate<<23) |(info->lcd_ttl_revsel<<22) | (info->lcd_ttl_datainv_en<<21) |
	                    (info->lcd_ttl_datainv_sel<<20) |info->lcd_ttl_revd);

    tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
    tmp &= 0x00200000;//keep the gamma_corrention_sel setting
    LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp | (1<<27/*rgb*/) | (0<<25/*disalbe hdif*/) | (0<<24/*yuv444 output*/) | (0<<20/*not interlace*/) | (info->start_delay<<2/*start delay*/));

    LCDC_WUINT32(sel, LCDC_SRGB_OFF,info->lcd_srgb);
    LCDC_WUINT32(sel, LCDC_IOCTL1_OFF,info->lcd_io_cfg0);
    LCDC_WUINT32(sel, LCDC_IOCTL2_OFF,info->lcd_io_cfg1);

    LCDC_set_tcon0_int_line(sel, info->start_delay+2);
}


__s32 TCON0_select_src(__u32 sel, __u8 src)
{
    __u32 tmp;

    tmp = LCDC_RUINT16IDX(sel, LCDC_CTL_OFF,1);
    tmp = tmp&0xf9df;
    switch(src)
    {
        case SRC_DE_CH1:
             tmp = tmp|0x0000;
             break;

        case SRC_DE_CH2:
             tmp = tmp|0x0200;
             break;

        case SRC_DMA:
             tmp = tmp|0x0400;
             break;

        case SRC_WHITE:
             tmp = tmp|0x0600;
             break;

        case SRC_BLACK:
             tmp = tmp|0x0620;
             break;
    }

    LCDC_WUINT16IDX(sel, LCDC_CTL_OFF,1,tmp);

    return 0;
}


__bool TCON0_in_valid_regn(__u32 sel, __u32 juststd)
{
   __u32         tmp;
   __u32         SY1;
   __u32         VT;

   tmp  = LCDC_RUINT32(sel, LCDC_DUBUG_OFF);
   SY1      = (tmp>>16)&0x3ff;

   tmp  = LCDC_RUINT32(sel, LCDC_BASIC1_OFF);
   VT       = (tmp>>11)&0x3ff;

   if(SY1 < juststd || SY1 > (VT - 1))
   {
       return 1;
   }

   return 0;
}


__s32 TCON0_get_width(__u32 sel)
{
    __u32    width;
    __u32    tmp;

    tmp  = LCDC_RUINT32(sel, LCDC_CTL_OFF);
    width    = ((tmp & 0x000ffc00)>>10) + 1;

    return width;
}

__s32 TCON0_get_height(__u32 sel)
{
    __u32    height;
    __u32    tmp;

    tmp  = LCDC_RUINT32(sel, LCDC_CTL_OFF);
    height   = (tmp & 0x000003ff) + 1;

    return height;
}

__s32 TCON0_set_dclk_div(__u32 sel, __u32 div)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_DCLK_OFF);
	tmp &= ~(0x000000ff);
	tmp |= (div & 0x000000ff);
	LCDC_WUINT32(sel, LCDC_DCLK_OFF, tmp);

	return 0;
}

#define ____SEPARATOR_TCON1____

__u32 TCON1_open(__u32 sel)
{
	__u32 tmp;

	//hd module enable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_HDTV0_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_HDTV0_OFF,1,tmp | 0x8000);

	return 0;
}

__u32 TCON1_close(__u32 sel)
{
	__u32  tmp;

	//hd module disable
	tmp = LCDC_RUINT16IDX   (sel, LCDC_HDTV0_OFF,1);
	LCDC_WUINT16IDX         (sel, LCDC_HDTV0_OFF,1,tmp & 0x7fff);

	tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
	tmp &= (~(1 << 25));//disable hdif
	LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp);

	LCDC_WUINT32(sel, LCDC_IOCTL2_OFF, 0xffffffff);

	return 0;
}

__u32  TCON1_cfg(__u32 sel, __tcon1_cfg_t *cfg)
{
	__u32 vblank_len;
    __u32 reg_val;

	vblank_len = cfg->vt/2 - cfg->src_y;
	if(vblank_len > 30)
	{
		cfg->start_delay	= 30;
	}
	else
	{
		cfg->start_delay	= vblank_len - 1;
	}

    reg_val = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
    reg_val &= 0xf4efff83;
    if (cfg->b_interlace)
    {
        reg_val |= (1<<20);
    }
    if (cfg->b_rgb_internal_hd)
    {
        reg_val |= (1<<27);
    }
    if (cfg->b_rgb_remap_io)
    {
        reg_val |= (1<<24);
    }
    if (cfg->b_remap_if)
    {
        reg_val |= (1<<25);
    }
    reg_val |= ((cfg->start_delay&0x1f)<<2);
    LCDC_WUINT32(sel, LCDC_HDTV0_OFF,reg_val);

    LCDC_WUINT32(sel, LCDC_HDTV1_OFF,(((cfg->src_x - 1)&0xfff)<<16)|((cfg->src_y - 1)&0xfff));
    LCDC_WUINT32(sel, LCDC_HDTV2_OFF,(((cfg->scl_x - 1)&0xfff)<<16)|((cfg->scl_y - 1)&0xfff));
    LCDC_WUINT32(sel, LCDC_HDTV3_OFF,(((cfg->out_x - 1)&0xfff)<<16)|((cfg->out_y - 1)&0xfff));
    LCDC_WUINT32(sel, LCDC_HDTV4_OFF,(((cfg->ht)&0xfff)<<16)|((cfg->hbp)&0xfff));
    LCDC_WUINT32(sel, LCDC_HDTV5_OFF,(((cfg->vt)&0xfff)<<16)|((cfg->vbp)&0xfff));
    LCDC_WUINT32(sel, LCDC_HDTV6_OFF,(((cfg->vspw)&0x3ff)<<16)|((cfg->hspw)&0x3ff));
    LCDC_WUINT32(sel, LCDC_IOCTL1_OFF,cfg->io_pol);//add
    LCDC_WUINT32(sel, LCDC_IOCTL2_OFF,cfg->io_out);//add

    /*set reg to yuv csc */
    LCDC_WUINT32(sel, LCDC_CSC0_OFF,0x08440832);
    LCDC_WUINT32(sel, LCDC_CSC1_OFF,0x24ca50e0);
    LCDC_WUINT32(sel, LCDC_CSC2_OFF,0x0e0af224);
    LCDC_WUINT32(sel, LCDC_CSC3_OFF,0x00108080);

	LCDC_set_tcon1_int_line(sel, cfg->start_delay+2);

    return 0;
}

__u32 TCON1_cfg_ex(__u32 sel, __ebios_panel_para_t * info)
{
    __tcon1_cfg_t tcon1_cfg;

    tcon1_cfg.b_interlace = 0;
    tcon1_cfg.b_rgb_internal_hd = 0;
    tcon1_cfg.b_rgb_remap_io = 1;//rgb
    tcon1_cfg.b_remap_if = 0;
    tcon1_cfg.src_x = info->lcd_x;
    tcon1_cfg.src_y = info->lcd_y;
    tcon1_cfg.scl_x = info->lcd_x;
    tcon1_cfg.scl_y = info->lcd_y;
    tcon1_cfg.out_x = info->lcd_x;
    tcon1_cfg.out_y = info->lcd_y;
    tcon1_cfg.ht = info->lcd_ht;
    tcon1_cfg.hbp = info->lcd_hbp;
    tcon1_cfg.vt = info->lcd_vt;
    tcon1_cfg.vbp = info->lcd_vbp;
    tcon1_cfg.vspw = info->lcd_hv_vspw;
    tcon1_cfg.hspw = info->lcd_hv_hspw;
    tcon1_cfg.io_pol = info->lcd_io_cfg0;
    tcon1_cfg.io_out = info->lcd_io_cfg1;

    TCON1_cfg(sel, &tcon1_cfg);

    return 0;
}

__u32 TCON1_set_hdmi_mode(__u32 sel, __u8 mode)
{
	__tcon1_cfg_t cfg;

	switch(mode)
	{
        case DISP_TV_MOD_480I:
        cfg.b_interlace   = 1;
        cfg.src_x       = 720;
        cfg.src_y       = 240;
        cfg.scl_x       = 720;
        cfg.scl_y       = 240;
        cfg.out_x       = 720;
        cfg.out_y       = 240;
        cfg.ht       = 857;
        cfg.hbp      = 118;
        cfg.vt       = 525;
        cfg.vbp      = 18;
        cfg.vspw     = 2;
        cfg.hspw     = 61;
        cfg.io_pol      = 0x04000000;
        break;
     case DISP_TV_MOD_576I:
        cfg.b_interlace   = 1;
        cfg.src_x       = 720;
        cfg.src_y       = 288;
        cfg.scl_x       = 720;
        cfg.scl_y       = 288;
        cfg.out_x       = 720;
        cfg.out_y       = 288;
        cfg.ht       = 863;
        cfg.hbp      = 131;
        cfg.vt       = 625;
        cfg.vbp      = 22;
        cfg.vspw     = 2;
        cfg.hspw     = 62;
        cfg.io_pol      = 0x04000000;
        break;
     case DISP_TV_MOD_480P:
        cfg.b_interlace   = 0;
        cfg.src_x       = 720;
        cfg.src_y       = 480;
        cfg.scl_x       = 720;
        cfg.scl_y       = 480;
        cfg.out_x       = 720;
        cfg.out_y       = 480;
        cfg.ht       = 857;
        cfg.hbp      = 121;
        cfg.vt       = 1050;
        cfg.vbp      = 42 - 6;
        cfg.vspw     = 5;
        cfg.hspw     = 61;
        cfg.io_pol      = 0x04000000;
        break;
     case DISP_TV_MOD_576P:
        cfg.b_interlace   = 0;
        cfg.src_x       = 720;
        cfg.src_y       = 576;
        cfg.scl_x       = 720;
        cfg.scl_y       = 576;
        cfg.out_x       = 720;
        cfg.out_y       = 576;
        cfg.ht       = 863;
        cfg.hbp      = 131;
        cfg.vt       = 1250;
        cfg.vbp      = 44;
        cfg.vspw     = 4;
        cfg.hspw     = 63;
        cfg.io_pol      = 0x04000000;
        break;

    case DISP_TV_MOD_720P_50HZ:
        cfg.b_interlace   = 0;
        cfg.src_x      = 1280;
        cfg.src_y      = 720;
        cfg.scl_x      = 1280;
        cfg.scl_y      = 720;
        cfg.out_x      = 1280;
        cfg.out_y      = 720;
        cfg.ht       = 1979;
        cfg.hbp      = 259;
        cfg.vt       = 1500;
        cfg.vbp      = 25;
        cfg.vspw     = 4;
        cfg.hspw     = 39;
        cfg.io_pol      = 0x07000000;
        break;
    case DISP_TV_MOD_720P_60HZ:
        cfg.b_interlace   = 0;
        cfg.src_x       = 1280;
        cfg.src_y       = 720;
        cfg.scl_x       = 1280;
        cfg.scl_y       = 720;
        cfg.out_x       = 1280;
        cfg.out_y       = 720;
        cfg.ht       = 1649;
        cfg.hbp      = 259;
        cfg.vt       = 1500;
        cfg.vbp      = 25;
        cfg.vspw     = 4;
        cfg.hspw     = 39;
        cfg.io_pol      = 0x07000000;
        break;
    case DISP_TV_MOD_1080I_50HZ:
        cfg.b_interlace   = 1;
        cfg.src_x       = 1920;
        cfg.src_y       = 540;
        cfg.scl_x       = 1920;
        cfg.scl_y       = 540;
        cfg.out_x       = 1920;
        cfg.out_y       = 540;
        cfg.ht       = 2639;
        cfg.hbp      = 191;
        cfg.vt       = 1125;
        cfg.vbp      = 20;
        cfg.vspw     = 4;
        cfg.hspw     = 43;
        cfg.io_pol      = 0x07000000;
        break;
    case DISP_TV_MOD_1080I_60HZ:
        cfg.b_interlace   = 1;
        cfg.src_x       = 1920;
        cfg.src_y       = 540;
        cfg.scl_x       = 1920;
        cfg.scl_y       = 540;
        cfg.out_x       = 1920;
        cfg.out_y       = 540;
        cfg.ht       = 2199;
        cfg.hbp      = 191;
        cfg.vt       = 1125;
        cfg.vbp      = 20;
        cfg.vspw     = 4;
        cfg.hspw     = 43;
        cfg.io_pol      = 0x07000000;
        break;
    case DISP_TV_MOD_1080P_24HZ:
		cfg.b_interlace   = 0;
        cfg.src_x       = 1920;
        cfg.src_y       = 1080;
        cfg.scl_x       = 1920;
        cfg.scl_y       = 1080;
        cfg.out_x       = 1920;
        cfg.out_y       = 1080;
        cfg.ht       = 2749;
        cfg.hbp      = 191;
        cfg.vt       = 2250;
        cfg.vbp      = 41;
        cfg.vspw     = 4;
        cfg.hspw     = 43;
        cfg.io_pol      = 0x07000000;
        break;
     case DISP_TV_MOD_1080P_50HZ:
        cfg.b_interlace   = 0;
        cfg.src_x       = 1920;
        cfg.src_y       = 1080;
        cfg.scl_x       = 1920;
        cfg.scl_y       = 1080;
        cfg.out_x       = 1920;
        cfg.out_y       = 1080;
        cfg.ht       = 2639;
        cfg.hbp      = 191;
        cfg.vt       = 2250;
        cfg.vbp      = 41;
        cfg.vspw     = 4;
        cfg.hspw     = 43;
        cfg.io_pol      = 0x07000000;
        break;
     case DISP_TV_MOD_1080P_60HZ:
        cfg.b_interlace   = 0;
        cfg.src_x       = 1920;
        cfg.src_y       = 1080;
        cfg.scl_x       = 1920;
        cfg.scl_y       = 1080;
        cfg.out_x       = 1920;
        cfg.out_y       = 1080;
        cfg.ht       = 2199;
        cfg.hbp      = 191;
        cfg.vt       = 2250;
        cfg.vbp      = 41;
        cfg.vspw     = 4;
        cfg.hspw     = 43;
        cfg.io_pol      = 0x07000000;
        break;
    default:
        return 0;
    }
	cfg.io_out      = 0x00000000;
	cfg.b_rgb_internal_hd = 0;
	cfg.b_rgb_remap_io = 1;//rgb
	cfg.b_remap_if      = 1;
	TCON1_cfg(sel, &cfg);

    return 0;
}

__u32 TCON1_set_tv_mode(__u32 sel, __u8 mode)
{
    __tcon1_cfg_t          cfg;

    switch(mode)
    {
        case DISP_TV_MOD_576I:
        case DISP_TV_MOD_PAL:
       	case DISP_TV_MOD_PAL_SVIDEO:
        case DISP_TV_MOD_PAL_CVBS_SVIDEO:
        case DISP_TV_MOD_PAL_NC:
        case DISP_TV_MOD_PAL_NC_SVIDEO:
        case DISP_TV_MOD_PAL_NC_CVBS_SVIDEO:
            cfg.b_interlace   = 1;
            cfg.src_x       = 720;
            cfg.src_y       = 288;
            cfg.scl_x       = 720;
            cfg.scl_y       = 288;
            cfg.out_x       = 720;
            cfg.out_y       = 288;
            cfg.ht       = 863;
            cfg.hbp      = 138;
            cfg.vt       = 625;
            cfg.vbp      = 22;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_480I:
        case DISP_TV_MOD_NTSC:
        case DISP_TV_MOD_NTSC_SVIDEO:
        case DISP_TV_MOD_NTSC_CVBS_SVIDEO:
        case DISP_TV_MOD_PAL_M:
        case DISP_TV_MOD_PAL_M_SVIDEO:
        case DISP_TV_MOD_PAL_M_CVBS_SVIDEO:
            cfg.b_interlace   = 1;
            cfg.src_x       = 720;
            cfg.src_y       = 240;
            cfg.scl_x       = 720;
            cfg.scl_y       = 240;
            cfg.out_x       = 720;
            cfg.out_y       = 240;
            cfg.ht       = 857;
            cfg.hbp      = 117;
            cfg.vt       = 525;
            cfg.vbp      = 18;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_480P:
        	cfg.b_interlace   = 0;
            cfg.src_x       = 720;
            cfg.src_y       = 480;
            cfg.scl_x       = 720;
            cfg.scl_y       = 480;
            cfg.out_x       = 720;
            cfg.out_y       = 480;
            cfg.ht       = 857;
            cfg.hbp      = 117;
            cfg.vt       = 1050;
            cfg.vbp      = 22;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_576P:
        	cfg.b_interlace   = 0;
            cfg.src_x       = 720;
            cfg.src_y       = 576;
            cfg.scl_x       = 720;
            cfg.scl_y       = 576;
            cfg.out_x       = 720;
            cfg.out_y       = 576;
            cfg.ht       = 863;
            cfg.hbp      = 138;
            cfg.vt       = 1250;
            cfg.vbp      = 22;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_720P_50HZ:
       	 	cfg.b_interlace   = 0;
            cfg.src_x       = 1280;
            cfg.src_y       = 720;
            cfg.scl_x       = 1280;
            cfg.scl_y       = 720;
            cfg.out_x       = 1280;
            cfg.out_y       = 720;
            cfg.ht       = 1979;
            cfg.hbp      = 259;
            cfg.vt       = 1500;
            cfg.vbp      = 24;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_720P_60HZ:
        	cfg.b_interlace   = 0;
            cfg.src_x       = 1280;
            cfg.src_y       = 720;
            cfg.scl_x       = 1280;
            cfg.scl_y       = 720;
            cfg.out_x       = 1280;
            cfg.out_y       = 720;
            cfg.ht       = 1649;
            cfg.hbp      = 259;
            cfg.vt       = 1500;
            cfg.vbp      = 24;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_1080I_50HZ:
            cfg.b_interlace   = 0;
            cfg.src_x       = 1920;
            cfg.src_y       = 540;
            cfg.scl_x       = 1920;
            cfg.scl_y       = 540;
            cfg.out_x       = 1920;
            cfg.out_y       = 540;
            cfg.ht       = 2639;
            cfg.hbp      = 191;
            cfg.vt       = 1125;
            cfg.vbp      = 16;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_1080I_60HZ:
            cfg.b_interlace   = 1;
            cfg.src_x       = 1920;
            cfg.src_y       = 540;
            cfg.scl_x       = 1920;
            cfg.scl_y       = 540;
            cfg.out_x       = 1920;
            cfg.out_y       = 540;
            cfg.ht       = 2199;
            cfg.hbp      = 191;
            cfg.vt       = 1125;
            cfg.vbp      = 16;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_1080P_50HZ:
            cfg.b_interlace   = 0;
            cfg.src_x       = 1920;
            cfg.src_y       = 1080;
            cfg.scl_x       = 1920;
            cfg.scl_y       = 1080;
            cfg.out_x       = 1920;
            cfg.out_y       = 1080;
            cfg.ht       = 2639;
            cfg.hbp      = 191;
            cfg.vt       = 2250;
            cfg.vbp      = 44;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        case DISP_TV_MOD_1080P_60HZ:
            cfg.b_interlace   = 0;
            cfg.src_x       = 1920;
            cfg.src_y       = 1080;
            cfg.scl_x       = 1920;
            cfg.scl_y       = 1080;
            cfg.out_x       = 1920;
            cfg.out_y       = 1080;
            cfg.ht       = 2199;
            cfg.hbp      = 191;
            cfg.vt       = 2250;
            cfg.vbp      = 44;
            cfg.vspw     = 1;
            cfg.hspw     = 1;
            break;

        default:
            return 0;
    }
    cfg.io_pol      = 0x00000000;
    cfg.io_out      = 0x0fffffff;
    cfg.b_rgb_internal_hd = 0;//yuv
    cfg.b_rgb_remap_io = 0;
    cfg.b_remap_if      = 0;
    TCON1_cfg(sel, &cfg);

    return 0;
}



// set mode
////////////////////////////////////////////////////////////////////////////////
__s32 TCON1_set_vga_mode(__u32 sel, __u8 mode)
{
    __tcon1_cfg_t          cfg;

	switch(mode)
	{
	case DISP_VGA_H640_V480:
      cfg.src_x = cfg.scl_x = cfg.out_x = 640;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 480;//VA
      cfg.ht       = 0x31f;//HT-1=-1
      cfg.hbp      = 0x8f;//HS+HBP-1=+-1
      cfg.vt       = 0x41a;//VT*2=*2
      cfg.vbp      = 0x22;//VS+VBP-1=+-1
      cfg.vspw     = 0x1;//VS-1=-1
      cfg.hspw     = 0x5f;//HS-1=-1
		break;
	case DISP_VGA_H800_V600:
      cfg.src_x = cfg.scl_x = cfg.out_x = 800;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 600;//VA
      cfg.ht       = 0x41f;//HT-1=-1
      cfg.hbp      = 0xd7;//HS+HBP-1=+-1
      cfg.vt       = 0x4e8;//VT*2=*2
      cfg.vbp      = 0x1a;//VS+VBP-1=+-1
      cfg.vspw     = 0x3;//VS-1=-1
      cfg.hspw     = 0x7f;//HS-1=-1
		break;
	case  DISP_VGA_H1024_V768:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1024;
      cfg.src_y = cfg.scl_y = cfg.out_y = 768;
      cfg.ht       = 1343;//HT-1=1344-1
      cfg.hbp      = 295;//HS+HBP-1=136+160-1
      cfg.vt       = 1612;//VT*2=806*2
      cfg.vbp      = 34;//VS+VBP-1=6+29-1
      cfg.vspw     = 5;//VS-1=6-1
      cfg.hspw     = 135;//HS-1=136-1
		break;
	case  DISP_VGA_H1280_V1024:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1280;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 1024;//VA
      cfg.ht       = 0x697;//HT-1=-1
      cfg.hbp      = 0x167;//HS+HBP-1=+-1
      cfg.vt       = 0x854;//VT*2=*2
      cfg.vbp      = 0x28;//VS+VBP-1=+-1
      cfg.vspw     = 0x2;//VS-1=-1
      cfg.hspw     = 0x6f;//HS-1=-1
		break;
	case  DISP_VGA_H1360_V768:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1360;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 768;//VA
      cfg.ht       = 0x6ff;//HT-1=-1
      cfg.hbp      = 0x16f;//HS+HBP-1=+-1
      cfg.vt       = 0x636;//VT*2=*2
      cfg.vbp      = 0x17;//VS+VBP-1=+-1
      cfg.vspw     = 0x5;//VS-1=-1
      cfg.hspw     = 0x6f;//HS-1=-1
		break;
	case  DISP_VGA_H1440_V900:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1440;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 900;//VA
      cfg.ht       = 0x76f;//HT-1=-1
      cfg.hbp      = 0x17f;//HS+HBP-1=+-1
      cfg.vt       = 0x74c;//VT*2=*2
      cfg.vbp      = 0x1e;//VS+VBP-1=+-1
      cfg.vspw     = 0x5;//VS-1=-1
      cfg.hspw     = 0x97;//HS-1=-1
		break;
	case  DISP_VGA_H1680_V1050:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1680;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 1050;//VA
      cfg.ht       = 2239;//HT-1=-1
      cfg.hbp      = 463;//HS+HBP-1=+-1
      cfg.vt       = 2178;//VT*2=*2
      cfg.vbp      = 35;//VS+VBP-1=+-1
      cfg.vspw     = 5;//VS-1=-1
      cfg.hspw     = 175;//HS-1=-1
		break;
	case  DISP_VGA_H1920_V1080_RB:
      cfg.src_x = cfg.scl_x = cfg.out_x = 1920;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 1080;//VA
      cfg.ht       = 2016;//HT-1=-1
      cfg.hbp      = 62;//HS+HBP-1=+-1
      cfg.vt       = 2222;//VT*2=*2
      cfg.vbp      = 27;//VS+VBP-1=+-1
      cfg.vspw     = 4;//VS-1=-1
      cfg.hspw     = 31;//HS-1=-1
		break;
	case  DISP_VGA_H1920_V1080://TBD
      cfg.src_x = cfg.scl_x = cfg.out_x = 1920;//HA
      cfg.src_y = cfg.scl_y = cfg.out_y = 1080;//VA
      cfg.ht       = 2200-1;//HT-1=-1
      cfg.hbp      = 148+44-1;//HS+HBP-1=+-1
      cfg.vt       = 1125*2;//VT*2=*2
      cfg.vbp      = 36+5;//VS+VBP-1=+-1
      cfg.vspw     = 5-1;//VS-1=-1
      cfg.hspw     = 44-1;//HS-1=-1
      cfg.io_pol   = 0x03000000;
		break;
	default:
		return 0;
	}
    cfg.b_interlace   = 0;
    cfg.io_pol      = 0x00000000;
    cfg.io_out      = 0x0cffffff;//hs vs is use
    cfg.b_rgb_internal_hd = 1;//rgb
    cfg.b_rgb_remap_io = 0;
    cfg.b_remap_if      = 1;
    TCON1_cfg(sel, &cfg);

    return 0;
}


__s32 TCON1_select_src(__u32 sel, __u8 src)
{
    __u32 tv_tmp;

	tv_tmp = LCDC_RUINT16IDX(sel, LCDC_HDTV0_OFF,0);

    tv_tmp = tv_tmp&0xfffc;
	if(src == SRC_DE_CH1)
	{
		tv_tmp = tv_tmp|0x00;
	}
	else if(src == SRC_DE_CH2)
	{
		tv_tmp = tv_tmp|0x01;
	}
	else if(src == SRC_BLUE)
	{
		tv_tmp = tv_tmp|0x02;
	}

	LCDC_WUINT16IDX(sel, LCDC_HDTV0_OFF,0,tv_tmp);

	return 0;
}


__bool TCON1_in_valid_regn(__u32 sel, __u32 juststd)
{
   __u32         readval;
   __u32         SY2;
   __u32         VT;

   readval      = LCDC_RUINT32(sel, LCDC_HDTV5_OFF);
   VT           = (readval & 0xffff0000)>>17;

   readval      = LCDC_RUINT32(sel, LCDC_DUBUG_OFF);
   SY2          = (readval)&0x3ff;

   if((SY2 < juststd) ||(SY2 > VT))
   {
       return 1;
   }
   else
   {
       return 0;
   }
}

__s32 TCON1_get_width(__u32 sel)
{
    __u32 width;
    __u32 readval;

    readval  = LCDC_RUINT32(sel, LCDC_HDTV1_OFF);
    width    = ((readval & 0x0fff0000)>>16) + 1;

    return width;
}

__s32 TCON1_get_height(__u32 sel)
{
    __u32 height;
    __u32 readval;
    __u32 interlace;

    readval  = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
    interlace = (readval & 0x00100000)>>20;
    interlace += 1;

    readval  = LCDC_RUINT32(sel, LCDC_HDTV1_OFF);
    height   = ((readval & 0x00000fff) + 1) * interlace;

    return height;
}

__s32 TCON1_set_gamma_table(__u32 sel, __u32 address,__u32 size)
{
    __u32 tmp;

	__s32 *pmem_align_dest;
    __s32 *pmem_align_src;
    __s32 *pmem_dest_cur;

	if(size > LCDC_GAMMA_TABLE_SIZE)
    {
        size = LCDC_GAMMA_TABLE_SIZE;
    }
    else
    {
        size = size;
    }

    tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
    LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp&0xffdfffff);//disable gamma correction sel

	pmem_dest_cur = (__s32*)(LCDC_get_reg_base(sel)+LCDC_GAMMA_TABLE_OFF);
	pmem_align_src = (__s32*)address;
	pmem_align_dest = pmem_dest_cur + (size>>2);

    while(pmem_dest_cur < pmem_align_dest)
    {
    	*(volatile __u32 *)pmem_dest_cur++ = *pmem_align_src++;
    }

    LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp);

    return 0;
}

__s32 TCON1_set_gamma_Enable(__u32 sel, __bool enable)
{
	__u32 tmp;

	tmp = LCDC_RUINT32(sel, LCDC_HDTV0_OFF);
	if(enable)
	{
		LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp|0x00200000);//set bit21
	}
	else
	{
		LCDC_WUINT32(sel, LCDC_HDTV0_OFF,tmp&0xffdfffff);//clear bit21
	}
	return 0;
}

#define ____SEPARATOR_CPU____

#if 0
__asm void my_stmia(int addr,int data1,int data2)
{
    stmia r0!, {r1,r2}
    BX    lr
}
#endif

void LCD_CPU_Burst_Write(__u32 sel, int addr,int data1,int data2)
{
	//my_stmia(LCDC_get_reg_base(sel) + addr,data1,data2);
	LCDC_WUINT32(sel,addr,data1);
}


void LCD_CPU_WR(__u32 sel, __u32 index, __u32 data)
{
	__u32 lcd_cpu;

	LCDC_CLR_BIT(sel, LCDC_MODE_OFF,LCDC_BIT25);         		//ca =0
	LCD_CPU_Burst_Write(sel, LCDC_CPUWR_OFF, index, index);			// write data on 8080 bus

	do{
		lcd_cpu = LCDC_RUINT32(sel, LCDC_MODE_OFF);
	} while(lcd_cpu&LCDC_BIT23);                             	//check wr finish


	LCDC_SET_BIT(sel, LCDC_MODE_OFF,LCDC_BIT25);     			//ca =1
	LCD_CPU_Burst_Write(sel, LCDC_CPUWR_OFF, data,data);

	do{
		lcd_cpu = LCDC_RUINT32(sel, LCDC_MODE_OFF);
	} while(lcd_cpu&LCDC_BIT23);                             //check wr finish
}

void LCD_CPU_WR_INDEX(__u32 sel, __u32 index)
{
	__u32 lcd_cpu;

	LCDC_CLR_BIT(sel, LCDC_MODE_OFF,LCDC_BIT25);         		//ca =0
	LCD_CPU_Burst_Write(sel, LCDC_CPUWR_OFF, index,index);			// write data on 8080 bus

	do{
		lcd_cpu = LCDC_RUINT32(sel, LCDC_MODE_OFF);
	} while(lcd_cpu&LCDC_BIT23);                             	//check wr finish
}

void LCD_CPU_WR_DATA(__u32 sel, __u32 data)
{
	__u32 lcd_cpu;

	LCDC_SET_BIT(sel, LCDC_MODE_OFF,LCDC_BIT25);     			//ca =1
	LCD_CPU_Burst_Write(sel, LCDC_CPUWR_OFF, data,data);

	do{
		lcd_cpu = LCDC_RUINT32(sel, LCDC_MODE_OFF);
	} while(lcd_cpu&LCDC_BIT23);                             //check wr finish
}

void LCD_CPU_RD(__u32 sel, __u32 index, __u32 *data)
{
}

void LCD_CPU_AUTO_FLUSH(__u32 sel, __u8 en)
{
	if(en ==0)
		LCDC_CLR_BIT(sel, LCDC_MODE_OFF,LCDC_BIT28);
	else
		LCDC_SET_BIT(sel, LCDC_MODE_OFF,LCDC_BIT28);
}

void LCD_CPU_DMA_FLUSH(__u32 sel, __u8 en)
{
	if(en ==0)
		LCDC_CLR_BIT(sel, LCDC_MODE_OFF,LCDC_BIT27);
	else
		LCDC_SET_BIT(sel, LCDC_MODE_OFF,LCDC_BIT27);
}

void LCD_XY_SWAP(__u32 sel)
{
	__u32 reg,x,y;
	reg = LCDC_RUINT32(sel, LCDC_CTL_OFF);
	y   = reg & 0x3ff;
	x   = (reg>>10) & 0x3ff;
	LCDC_WUINT32(sel, LCDC_CTL_OFF,(reg&0xfff00000) | (y<<10) | x);
}




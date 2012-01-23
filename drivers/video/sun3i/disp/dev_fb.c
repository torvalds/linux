#include "drv_disp_i.h"
#include "drv_disp.h"

extern fb_info_t g_fbi;

__s32 layer_hdl_to_fb_id(__u32 sel, __u32 hdl)
{
	__s32 i = 0;

	for(i = 0; i<FB_MAX; i++)
	{
	    if(g_fbi.fb_screen_id[i] == sel && g_fbi.layer_hdl[i] == hdl)
	    {
	        return i;
	    }
	}
	return -1;
}

__s32 var_to_fb(__disp_fb_t *fb, struct fb_var_screeninfo *var, struct fb_fix_screeninfo * fix)
{
    if(var->nonstd == 0)//argb
    {
			var->reserved[0] = DISP_MOD_INTERLEAVED;
			var->reserved[2] = DISP_SEQ_ARGB;
			var->reserved[3] = 0;

		switch (var->bits_per_pixel)
		{
		case 1:
			var->red.offset	= var->green.offset = var->blue.offset	= 0;
			var->red.length	= var->green.length = var->blue.length	= 1;
			var->reserved[1] = DISP_FORMAT_1BPP;
			break;

		case 2:
			var->red.offset	= var->green.offset = var->blue.offset	= 0;
			var->red.length	= var->green.length = var->blue.length	= 2;
			var->reserved[1] = DISP_FORMAT_2BPP;
			break;

		case 4:
			var->red.offset	= var->green.offset = var->blue.offset	= 0;
			var->red.length	= var->green.length = var->blue.length	= 4;
			var->reserved[1] = DISP_FORMAT_4BPP;
			break;

		case 8:
			var->red.offset	= var->green.offset = var->blue.offset	= 0;
			var->red.length	= var->green.length = var->blue.length	= 8;
			var->reserved[1] = DISP_FORMAT_8BPP;
			break;

		case 16:
			if(var->red.length==6 && var->green.length==5 && var->blue.length==5)
			{
				var->red.offset = 10;
				var->green.offset = 5;
				var->blue.offset = 0;
				var->reserved[1] = DISP_FORMAT_RGB655;
			}
			else if(var->red.length==5 && var->green.length==6 && var->blue.length==5)
			{
				var->red.offset = 11;
				var->green.offset = 5;
				var->blue.offset = 0;
				var->reserved[1] = DISP_FORMAT_RGB565;
			}
			else if(var->red.length==5 && var->green.length==5 && var->blue.length==6)
			{
				var->red.offset = 11;
				var->green.offset = 6;
				var->blue.offset = 0;
				var->reserved[1] = DISP_FORMAT_RGB556;
			}
			else if(var->transp.length==1 && var->red.length==5 && var->green.length==5 && var->blue.length==5)
			{
				var->transp.offset = 15;
				var->red.offset = 10;
				var->green.offset = 5;
				var->blue.offset = 0;
				var->reserved[1] = DISP_FORMAT_ARGB1555;
			}
			else
			{
			    __wrn("invalid bits_per_pixel :%d in var_to_fb\n", var->bits_per_pixel);
				return -EINVAL;
			}
			break;

		case 24:
			var->red.offset		= 16;
			var->green.offset	= 8;
			var->blue.offset	= 0;
			var->red.length		= 8;
			var->green.length	= 8;
			var->blue.length	= 8;
			var->reserved[1] = DISP_FORMAT_RGB888;
			break;

		case 32:
			var->transp.offset  = 24;
			var->red.offset		= 16;
			var->green.offset	= 8;
			var->blue.offset	= 0;
			var->transp.length  = 8;
			var->red.length		= 8;
			var->green.length	= 8;
			var->blue.length	= 8;
			var->reserved[1] = DISP_FORMAT_ARGB8888;
			break;

		default:
		    __wrn("invalid bits_per_pixel :%d in var_to_fb\n", var->bits_per_pixel);
			return -EINVAL;
		}
	}
    else
    {
        switch(var->reserved[1])
        {
        case DISP_FORMAT_1BPP:
            var->bits_per_pixel = 1;
            break;

        case DISP_FORMAT_2BPP:
            var->bits_per_pixel = 2;
            break;

        case DISP_FORMAT_4BPP:
            var->bits_per_pixel = 4;
            break;

        case DISP_FORMAT_8BPP:
        case DISP_FORMAT_YUV444:
        case DISP_FORMAT_YUV422:
        case DISP_FORMAT_YUV420:
        case DISP_FORMAT_YUV411:
            var->bits_per_pixel = 8;
            break;

        case DISP_FORMAT_RGB655:
        case DISP_FORMAT_RGB565:
        case DISP_FORMAT_RGB556:
        case DISP_FORMAT_ARGB1555:
        case DISP_FORMAT_RGBA5551:
            var->bits_per_pixel = 16;
            break;

        case DISP_FORMAT_RGB888:
            var->bits_per_pixel = 24;
            break;

        case DISP_FORMAT_CSIRGB:
        case DISP_FORMAT_ARGB8888:
            var->bits_per_pixel = 32;
            break;

        default:
            __wrn("invalid format :%d in var_to_fb\n", var->reserved[1]);
            return -EINVAL;
        }
    }

    fb->mode = var->reserved[0];
    fb->format = var->reserved[1];
    fb->seq = var->reserved[2];
    fb->br_swap = var->reserved[3];
    fb->size.width = var->xres_virtual;

    fix->line_length = (var->xres_virtual * var->bits_per_pixel) / 8;
   	//fix->line_length = (var->xres * var->bits_per_pixel) / 8;

	return 0;
}


static int __init Fb_map_video_memory(struct fb_info *info)
{
	unsigned map_size = PAGE_ALIGN(info->fix.smem_len);
	struct page *page;

	page = alloc_pages(GFP_KERNEL,get_order(map_size));
	if(page != NULL)
	{
		info->screen_base = page_address(page);
		info->fix.smem_start = virt_to_phys(info->screen_base);
		memset(info->screen_base,0,info->fix.smem_len);
		__msg("map_video_memory: pa=%08lx va=%p size:%x\n",info->fix.smem_start, info->screen_base, info->fix.smem_len);
		return 0;
	}
	else
	{
		__wrn("fail to alloc memory!\n");
		return -ENOMEM;
	}
}


static inline void Fb_unmap_video_memory(struct fb_info *info)
{
	unsigned map_size = PAGE_ALIGN(info->fix.smem_len);

	free_pages((unsigned long)info->screen_base,get_order(map_size));
}


static int Fb_open(struct fb_info *info, int user)
{
	return 0;
}
static int Fb_release(struct fb_info *info, int user)
{
	return 0;
}

static int Fb_pan_display(struct fb_var_screeninfo *var,struct fb_info *info)
{
	__s32 hdl = g_fbi.layer_hdl[info->node];
	__u32 sel = g_fbi.fb_screen_id[info->node];
	__disp_layer_info_t layer_para;

	__msg("Fb_pan_display\n");

	BSP_disp_layer_get_para(sel, hdl, &layer_para);

	if(layer_para.mode == DISP_LAYER_WORK_MODE_SCALER)
	{
    	layer_para.src_win.x = var->xoffset;
    	layer_para.src_win.y = var->yoffset;
    	layer_para.src_win.width = var->xres;
    	layer_para.src_win.height = var->yres;
    }
    else
    {
    	layer_para.src_win.x = var->xoffset;
    	layer_para.src_win.y = var->yoffset;
    	layer_para.src_win.width = var->xres;
    	layer_para.src_win.height = var->yres;

    	layer_para.scn_win.width = var->xres;
    	layer_para.scn_win.height = var->yres;
    }

    BSP_disp_layer_set_para(sel, hdl, &layer_para);

	return 0;
}

static int Fb_ioctl(struct fb_info *info, unsigned int cmd,unsigned long arg)
{
	__s32 hid = g_fbi.layer_hdl[info->node];
	void __user *argp = (void __user *)arg;
	long ret = 0;

	switch (cmd)
	{
	case FBIOGET_LAYER_HDL:
		__msg("Fb_ioctl:FBIOGET_LAYER_HDL,layer:%d\n",info->node);
		if(copy_to_user(argp, &hid, sizeof(hid)))
		{
			return -EFAULT;
		}
		break;
	default:
		break;
	}
	return ret;
}

static int Fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)//todo
{
	__msg("Fb_check_var\n");

	return 0;
}

static int Fb_set_par(struct fb_info *info)//todo
{
	struct fb_var_screeninfo *var = &info->var;
	struct fb_fix_screeninfo * fix = &info->fix;
	__disp_layer_info_t layer_para;
	__s32 hdl = g_fbi.layer_hdl[info->node];
	__u32 sel = g_fbi.fb_screen_id[info->node];

	__msg("Fb_set_par\n");

    BSP_disp_layer_get_para(sel, hdl, &layer_para);

	layer_para.src_win.x = var->xoffset;
	layer_para.src_win.y = var->yoffset;
	layer_para.src_win.width = var->xres;
	layer_para.src_win.height = var->yres;

    var_to_fb(&(layer_para.fb), var, fix);

    BSP_disp_layer_set_para(sel, hdl, &layer_para);

	return 0;
}


static int Fb_setcolreg(unsigned regno,unsigned red, unsigned green, unsigned blue,unsigned transp, struct fb_info *info)
{
	unsigned int val;
	__u32 sel = g_fbi.fb_screen_id[info->node];

	 __msg("Fb_setcolreg,regno=%d,a=%d,r=%d,g=%d,b=%d\n",regno, transp,red, green, blue);

	switch (info->fix.visual)
	{
	case FB_VISUAL_PSEUDOCOLOR:
		if (regno < 256)
		{
			val = (transp<<24) | (red<<16) | (green<<8) | blue;
			BSP_disp_set_palette_table(sel, &val, regno*4, 4);
		}
		break;

	default:
		break;
	}

	return 0;
}

static int Fb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	unsigned int i = 0, val = 0;
	unsigned char hred, hgreen, hblue, htransp = 0xff;
	unsigned short *red, *green, *blue, *transp;
	__u32 sel = g_fbi.fb_screen_id[info->node];

	__msg("Fb_setcmap\n");

    red = cmap->red;
    green = cmap->green;
    blue = cmap->blue;
    transp = cmap->transp;

	for (i = 0; i < cmap->len; i++)
	{
		hred = (*red++)&0xff;
		hgreen = (*green++)&0xff;
		hblue = (*blue++)&0xff;
		if (transp)
		{
			htransp = (*transp++)&0xff;
		}
		else
		{
		    htransp = 0xff;
		}

		val = (htransp<<24) | (hred<<16) | (hgreen<<8) |hblue;
		BSP_disp_set_palette_table(sel, &val, (cmap->start + i) * 4, 4);
	}
	return 0;
}

int Fb_blank(int blank_mode, struct fb_info *info)
{
    __u32 sel = g_fbi.fb_screen_id[info->node];
    __s32 hdl = g_fbi.layer_hdl[info->node];

	__msg("Fb_blank,mode:%d\n",blank_mode);

	if (blank_mode == FB_BLANK_POWERDOWN)
	{
		BSP_disp_layer_close(sel, hdl);
	}
	else
	{
		BSP_disp_layer_open(sel, hdl);
	}

	return 0;
}

static int Fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
    __msg("Fb_cursor\n");

    return 0;
}

static struct fb_ops dispfb_ops =
{
	.owner		    = THIS_MODULE,
	.fb_open        = Fb_open,
	.fb_release     = Fb_release,
	.fb_pan_display	= Fb_pan_display,
	.fb_ioctl       = Fb_ioctl,
	.fb_check_var   = Fb_check_var,
	.fb_set_par     = Fb_set_par,
	.fb_setcolreg   = Fb_setcolreg,
	.fb_setcmap     = Fb_setcmap,
	.fb_blank       = Fb_blank,
	.fb_cursor      = Fb_cursor,
};

__s32 Display_Fb_Request(__u32 sel, __disp_fb_create_para_t *fb_para)
{
	struct fb_info *fbinfo = NULL;
	fb_info_t * fbi = &g_fbi;
	__s32 hdl = 0;
	__disp_layer_info_t layer_para;
	int ret;

	__msg("Display_Fb_Request, sel=%d\n", sel);

	fbinfo = framebuffer_alloc(0, fbi->dev);

	fbinfo->fbops   = &dispfb_ops;
	fbinfo->flags   = 0;
	fbinfo->device  = fbi->dev;
	fbinfo->par     = fbi;

	fbinfo->var.xoffset         = 0;
	fbinfo->var.yoffset         = 0;
	fbinfo->var.xres            = 1;
	fbinfo->var.yres            = 1;
	fbinfo->var.xres_virtual    = 1;
	fbinfo->var.yres_virtual    = 1;

	fbinfo->fix.type	    = FB_TYPE_PACKED_PIXELS;
	fbinfo->fix.type_aux	= 0;
	fbinfo->fix.visual 		= FB_VISUAL_TRUECOLOR;
	fbinfo->fix.xpanstep	= 1;
	fbinfo->fix.ypanstep	= 1;
	fbinfo->fix.ywrapstep	= 0;
	fbinfo->fix.accel	    = FB_ACCEL_NONE;
	fbinfo->fix.line_length = 0;
	fbinfo->fix.smem_len 	= fb_para->smem_len;

	ret = Fb_map_video_memory(fbinfo);
	if (ret)
	{
		__wrn("Failed to allocate video RAM: %d\n", ret);
		return DIS_FAIL;
	}

    hdl = BSP_disp_layer_request(sel, fb_para->mode);

    layer_para.mode = fb_para->mode;
    layer_para.pipe = 0;
    layer_para.alpha_en = 0;
    layer_para.alpha_val = 0xff;
    layer_para.ck_enable = 0;
    layer_para.src_win.x = 0;
    layer_para.src_win.y = 0;
    layer_para.src_win.width = 1;
    layer_para.src_win.height = 1;
    layer_para.scn_win.x = 0;
    layer_para.scn_win.y = 0;
    layer_para.scn_win.width = 1;
    layer_para.scn_win.height = 1;
    layer_para.fb.addr[0] = (__u32)fbinfo->fix.smem_start;
    layer_para.fb.addr[1] = (__u32)fbinfo->fix.smem_start+fb_para->ch1_offset;
    layer_para.fb.addr[2] = (__u32)fbinfo->fix.smem_start+fb_para->ch2_offset;
    layer_para.fb.size.width = 1;
    layer_para.fb.size.height = 1;
    layer_para.fb.format = DISP_FORMAT_ARGB8888;
    layer_para.fb.seq = DISP_SEQ_ARGB;
    layer_para.fb.mode = DISP_MOD_INTERLEAVED;
    layer_para.fb.br_swap = 0;
    layer_para.fb.cs_mode = DISP_BT601;
    BSP_disp_layer_set_para(sel, hdl, &layer_para);

    BSP_disp_layer_open(sel, hdl);

	register_framebuffer(fbinfo);

	fbi->fb_screen_id[fbinfo->node] = sel;
	fbi->layer_hdl[fbinfo->node] = hdl;
	fbi->fbinfo[fbinfo->node] = fbinfo;
	fbi->fb_num++;

    return hdl;
}

__s32 Display_Fb_Release(__u32 sel, __s32 hdl)
{
	__s32 fb_id = layer_hdl_to_fb_id(sel, hdl);

    __msg("Display_Fb_Release call\n");

	if(fb_id >= 0)
	{
	    fb_info_t * fbi = &g_fbi;
        struct fb_info *fbinfo = fbi->fbinfo[fb_id];

        BSP_disp_layer_release(sel, hdl);

    	unregister_framebuffer(fbinfo);
    	Fb_unmap_video_memory(fbinfo);
    	framebuffer_release(fbinfo);

    	fbi->fb_screen_id[fbinfo->node] = -1;
    	fbi->layer_hdl[fb_id] = 0;
    	fbi->fbinfo[fb_id] = NULL;
    	fbi->fb_num--;

	    return DIS_SUCCESS;
	}
	else
	{
	    __wrn("invalid paras (sel:%d,hdl:%d) in Display_Fb_Release\n", sel, hdl);
	    return DIS_FAIL;
	}

}


__s32 Fb_Init(void)
{
	return 0;
}

__s32 Fb_Exit(void)
{
	__u8 sel = 0;
	__u8 fb_id=0;

	for(sel = 0; sel<2; sel++)
	{
		for(fb_id=0; fb_id<FB_MAX; fb_id++)
		{
			if(g_fbi.fb_screen_id[fb_id] == sel && g_fbi.fbinfo[fb_id] != NULL)
			{
				Display_Fb_Release(sel, g_fbi.layer_hdl[fb_id]);
			}
		}
	}

	return 0;
}


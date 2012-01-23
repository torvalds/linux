#include "dev_disp.h"
#include "drv_disp.h"

extern __disp_drv_t    g_disp_drv;

struct disp_mm {

	unsigned long mem_start;	/* Start of frame buffer mem */
					/* (physical address) */
	__u32 mem_len;			/* Length of frame buffer mem */

	unsigned long mmio_start;	/* Start of Memory Mapped I/O   */
	unsigned long phy_adr;				/* (physical address) */

	__u32 accel;			/* Indicate to driver which	*/
	__u16 reserved[3];		/* Reserved for future compatibility */
};


struct info_mm {
	void *info_base;	/* Virtual address */
	struct disp_mm mm_disp;

};




struct info_mm  g_disp_mm[2];
static int g_disp_mm_sel = 0;

__s32 disp_get_free_event(__u32 sel)
{
    __u32 i = 0;

    for(i=0; i<MAX_EVENT_SEM; i++)
    {
        if(!g_disp_drv.event_used[sel][i])
        {
            g_disp_drv.event_used[sel][i] = 1;
            return i;
        }
    }
    return -1;
}

__s32 disp_cmd_before(__u32 sel)
{
    if(g_disp_drv.b_cache[sel] == 0 && BSP_disp_get_output_type(sel)!= DISP_OUTPUT_TYPE_NONE)
    {
        __s32 event_id = 0;

        event_id = disp_get_free_event(sel);
        if(event_id >= 0)
        {
        	g_disp_drv.event_sem[sel][event_id] = kmalloc(sizeof(struct semaphore),GFP_KERNEL | __GFP_ZERO);
            if(!g_disp_drv.event_sem[sel][event_id])
            {
                __wrn("create scaler_finished_sem[0] fail!\n");
                return -1;
            }
        	sema_init(g_disp_drv.event_sem[sel][event_id],0);
        }
        else
        {
            __wrn("disp_get_free_event() fail!\n");

        }
        return event_id;
    }
    return 0;

}

void disp_cmd_after(__u32 sel, __s32 event_id)
{
    if(g_disp_drv.b_cache[sel] == 0 && BSP_disp_get_output_type(sel)!= DISP_OUTPUT_TYPE_NONE)
    {
        if(event_id >= 0 && g_disp_drv.event_sem[sel][event_id] != NULL)
        {
            down(g_disp_drv.event_sem[sel][event_id]);

            kfree(g_disp_drv.event_sem[sel][event_id]);
            g_disp_drv.event_sem[sel][event_id] = NULL;
        }
        else
        {
            __wrn("no event sem in disp_cmd_after!\n");
        }
    }
}

__s32 disp_int_process(__u32 sel)
{
    __u32 i = 0;

    for(i=0; i<MAX_EVENT_SEM; i++)
    {
        if(g_disp_drv.event_sem[sel][i] != NULL)
        {
            up(g_disp_drv.event_sem[sel][i]);
            g_disp_drv.event_used[sel][i] = 0;
        }
    }
    return 0;
}

int disp_open(struct inode *inode, struct file *file)
{
    __msg("disp_open\n");
    return 0;
}

int disp_release(struct inode *inode, struct file *file)
{
    __msg("disp_release\n");
    return 0;
}
ssize_t disp_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	__msg("disp_read\n");
	return 0;
}

ssize_t disp_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	__msg("disp_write\n");
    return 0;
}


int disp_mem_request(int sel,__u32 size)
{
	unsigned map_size = 0;
	struct page *page;
	__msg("disp_mem_request,sel = %d,size = %d\n",sel,size);
	if(g_disp_mm[sel].info_base != 0)
		return -EINVAL;

	g_disp_mm[sel].mm_disp.mem_len = size;
	map_size = PAGE_ALIGN(g_disp_mm[sel].mm_disp.mem_len);

	page = alloc_pages(GFP_KERNEL,get_order(map_size));
	if(page != NULL)
	{
		g_disp_mm[sel].info_base = page_address(page);
		if(g_disp_mm[sel].info_base == 0)
		{
			free_pages((unsigned long)(page),get_order(map_size));
			__wrn("line %d:fail to alloc memory!\n",__LINE__);
			return -ENOMEM;
		}
		g_disp_mm[sel].mm_disp.mem_start = virt_to_phys(g_disp_mm[sel].info_base);
		memset(g_disp_mm[sel].info_base,0,size);
	//	SetPageReserved(g_disp_mm[sel].info_base);
		__msg("map_video_memory[%d]: pa=%08lx va=%p size:%x\n",sel,g_disp_mm[sel].mm_disp.mem_start, g_disp_mm[sel].info_base, size);
		return 0;
	}
	else
	{
		__wrn("fail to alloc memory!\n");
		return -ENOMEM;
	}
}


int disp_mem_release(int sel)
{
	unsigned map_size = PAGE_ALIGN(g_disp_mm[sel].mm_disp.mem_len);
	unsigned page_size = map_size;

	__msg("disp_mem_release sel = %d\n",sel);
	if(g_disp_mm[sel].info_base == 0)
		return -EINVAL;
	//ClearPageReserved(g_disp_mm[sel].info_base);
	free_pages((unsigned long)(g_disp_mm[sel].info_base),get_order(page_size));
	memset(&g_disp_mm[sel],0,sizeof(struct info_mm));
	return 0;
}

/* FIXME: commented out by benn for useless */
#if 0
static void my_vm_open(struct vm_area_struct *vma)
{
	printk("VMA open\n");
}

static void my_vm_close(struct vm_area_struct *vma)
{
	printk("VMA close.\n");
}
#endif

int disp_mmap(struct file *file, struct vm_area_struct * vma)
{
	unsigned long  physics =  g_disp_mm[g_disp_mm_sel].mm_disp.mem_start;// - PAGE_OFFSET;
	unsigned long mypfn = physics >> PAGE_SHIFT;
	unsigned long vmsize = vma->vm_end-vma->vm_start;

	__msg("disp_mmap(g_disp_mm_sel = %d)\n",g_disp_mm_sel);
	if(remap_pfn_range(vma,vma->vm_start,mypfn,vmsize,vma->vm_page_prot))
		return -EAGAIN;

	__msg("disp_mmap(SUCCESS)\n");

	return 0;


}



extern __s32 Display_Fb_Request(__u32 sel, __disp_fb_create_para_t *fb_para);
extern __s32 Display_Fb_Release(__u32 sel, __s32 hdl);

static unsigned int gbuffer[4096];

long disp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long karg[4];
	unsigned long ubuffer[4] = {0};
	__s32 ret = 0;
    __s32 event_id = 0;

	if (copy_from_user((void*)karg,(void __user*)arg,4*sizeof(unsigned long)))
	{
		__wrn("copy_from_user fail\n");
		return -EFAULT;
	}

	ubuffer[0] = *(unsigned long*)karg;
	ubuffer[1] = (*(unsigned long*)(karg+1));
	ubuffer[2] = (*(unsigned long*)(karg+2));
	ubuffer[3] = (*(unsigned long*)(karg+3));

    if((cmd != DISP_CMD_MEM_REQUEST) && (cmd != DISP_CMD_MEM_RELASE) && (cmd != DISP_CMD_MEM_SELIDX) && (cmd != DISP_CMD_MEM_GETADR))
    {
        if((ubuffer[0] != 0) && (ubuffer[0] != 1))
        {
            __wrn("para err in disp_ioctl, screen id = %d\n", (int)ubuffer[0]);
            return -1;
        }
    }

	__msg("disp_ioctl,cmd:%x\n",cmd);
    switch(cmd)
    {
    //----disp global----
    	case DISP_CMD_SET_BKCOLOR:
	    {
	        __disp_color_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_color_t)))
    		{
    			return  -EFAULT;
    		}
		    ret = BSP_disp_set_bk_color(ubuffer[0], &para);
		    break;
	    }

    	case DISP_CMD_SET_COLORKEY:
    	{
    	    __disp_colorkey_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_colorkey_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_set_color_key(ubuffer[0], &para);
		    break;
		}

    	case DISP_CMD_SET_PALETTE_TBL:
    	    if((ubuffer[1] == 0) || ((int)ubuffer[3] <= 0))
    	    {
    	        __wrn("para invalid in display ioctrl DISP_CMD_SET_PALETTE_TBL\n");
    	        return -1;
    	    }
    		if(copy_from_user(gbuffer, (void __user *)ubuffer[1],ubuffer[3]))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_set_palette_table(ubuffer[0], (__u32 *)gbuffer, ubuffer[2], ubuffer[3]);
    		break;

    	case DISP_CMD_GET_PALETTE_TBL:
    	    if((ubuffer[1] == 0) || ((int)ubuffer[3] <= 0))
    	    {
    	        __wrn("para invalid in display ioctrl DISP_CMD_GET_PALETTE_TBL\n");
    	        return -1;
    	    }
    		ret = BSP_disp_get_palette_table(ubuffer[0], (__u32 *)gbuffer, ubuffer[2], ubuffer[3]);
    		if(copy_to_user((void __user *)ubuffer[1], gbuffer,ubuffer[3]))
    		{
    			return  -EFAULT;
    		}
    		break;

    	case DISP_CMD_START_CMD_CACHE:
    		ret = BSP_disp_cmd_cache(ubuffer[0]);
    		g_disp_drv.b_cache[ubuffer[0]] = 1;
    		break;

    	case DISP_CMD_EXECUTE_CMD_AND_STOP_CACHE:
    	    g_disp_drv.b_cache[ubuffer[0]] = 0;
    	    event_id = disp_cmd_before(ubuffer[0]);
    		ret = BSP_disp_cmd_submit(ubuffer[0]);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;

    	case DISP_CMD_GET_OUTPUT_TYPE:
    		ret =  BSP_disp_get_output_type(ubuffer[0]);
    		break;

    	case DISP_CMD_SCN_GET_WIDTH:
    		ret = BSP_disp_get_screen_width(ubuffer[0]);
    		break;

    	case DISP_CMD_SCN_GET_HEIGHT:
    		ret = BSP_disp_get_screen_height(ubuffer[0]);
    		break;

    	case DISP_CMD_SET_GAMMA_TABLE:
    	    if((ubuffer[1] == 0) || ((int)ubuffer[2] <= 0))
    	    {
    	        __wrn("para invalid in display ioctrl DISP_CMD_SET_GAMMA_TABLE\n");
    	        return -1;
    	    }
    		if(copy_from_user(gbuffer, (void __user *)ubuffer[1],ubuffer[2]))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_set_gamma_table(ubuffer[0], (__u32 *)gbuffer, ubuffer[2]);
    		break;

    	case DISP_CMD_GAMMA_CORRECTION_ON:
    		ret = BSP_disp_gamma_correction_enable(ubuffer[0]);
    		break;

    	case DISP_CMD_GAMMA_CORRECTION_OFF:
    		ret = BSP_disp_gamma_correction_disable(ubuffer[0]);
    		break;

        case DISP_CMD_SET_BRIGHT:
            ret = BSP_disp_set_bright(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_GET_BRIGHT:
            ret = BSP_disp_get_bright(ubuffer[0]);
    		break;

        case DISP_CMD_SET_CONTRAST:
            ret = BSP_disp_set_contrast(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_GET_CONTRAST:
            ret = BSP_disp_get_contrast(ubuffer[0]);
    		break;

        case DISP_CMD_SET_SATURATION:
            ret = BSP_disp_set_saturation(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_GET_SATURATION:
            ret = BSP_disp_get_saturation(ubuffer[0]);
    		break;

        case DISP_CMD_ENHANCE_ON:
            ret = BSP_disp_enhance_enable(ubuffer[0], 1);
    		break;

        case DISP_CMD_ENHANCE_OFF:
            ret = BSP_disp_enhance_enable(ubuffer[0], 0);
    		break;

        case DISP_CMD_GET_ENHANCE_EN:
            ret = BSP_disp_get_enhance_enable(ubuffer[0]);
    		break;

    //----layer----
    	case DISP_CMD_LAYER_REQUEST:
    		ret = BSP_disp_layer_request(ubuffer[0], (__disp_layer_work_mode_t)ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_RELEASE:
    		ret = BSP_disp_layer_release(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_OPEN:
    		ret = BSP_disp_layer_open(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_CLOSE:
    		ret = BSP_disp_layer_close(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_SET_FB:
    	{
    	    __disp_fb_t para;

    	    event_id = disp_cmd_before(ubuffer[0]);
    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_fb_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_layer_set_framebuffer(ubuffer[0], ubuffer[1], &para);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;
    	}

    	case DISP_CMD_LAYER_GET_FB:
    	{
    	    __disp_fb_t para;

    		ret = BSP_disp_layer_get_framebuffer(ubuffer[0], ubuffer[1], &para);
    		if(copy_to_user((void __user *)ubuffer[2], &para,sizeof(__disp_fb_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_LAYER_SET_SRC_WINDOW:
    	{
    	    __disp_rect_t para;

    	    event_id = disp_cmd_before(ubuffer[0]);
    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_layer_set_src_window(ubuffer[0],ubuffer[1], &para);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;
        }

    	case DISP_CMD_LAYER_GET_SRC_WINDOW:
    	{
    	    __disp_rect_t para;

    		ret = BSP_disp_layer_get_src_window(ubuffer[0],ubuffer[1], &para);
    		if(copy_to_user((void __user *)ubuffer[2], &para, sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_LAYER_SET_SCN_WINDOW:
    	{
    	    __disp_rect_t para;

    	    event_id = disp_cmd_before(ubuffer[0]);
    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_layer_set_screen_window(ubuffer[0],ubuffer[1], &para);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;
        }

    	case DISP_CMD_LAYER_GET_SCN_WINDOW:
    	{
    	    __disp_rect_t para;

    		ret = BSP_disp_layer_get_screen_window(ubuffer[0],ubuffer[1], &para);
    		if(copy_to_user((void __user *)ubuffer[2], &para, sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_LAYER_SET_PARA:
    	{
    	    __disp_layer_info_t para;

    	    event_id = disp_cmd_before(ubuffer[0]);
    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_layer_info_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_layer_set_para(ubuffer[0], ubuffer[1], &para);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;
        }

    	case DISP_CMD_LAYER_GET_PARA:
    	{
    	    __disp_layer_info_t para;

    		ret = BSP_disp_layer_get_para(ubuffer[0], ubuffer[1], &para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_layer_info_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_LAYER_TOP:
    		ret = BSP_disp_layer_set_top(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_BOTTOM:
    		ret = BSP_disp_layer_set_bottom(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_ALPHA_ON:
    		ret = BSP_disp_layer_alpha_enable(ubuffer[0], ubuffer[1], 1);
    		break;

    	case DISP_CMD_LAYER_ALPHA_OFF:
    		ret = BSP_disp_layer_alpha_enable(ubuffer[0], ubuffer[1], 0);
    		break;

    	case DISP_CMD_LAYER_SET_ALPHA_VALUE:
    	    event_id = disp_cmd_before(ubuffer[0]);
    		ret = BSP_disp_layer_set_alpha_value(ubuffer[0], ubuffer[1], ubuffer[2]);
    		disp_cmd_after(ubuffer[0], event_id);
    		break;

    	case DISP_CMD_LAYER_CK_ON:
    		ret = BSP_disp_layer_colorkey_enable(ubuffer[0], ubuffer[1], 1);
    		break;

    	case DISP_CMD_LAYER_CK_OFF:
    		ret = BSP_disp_layer_colorkey_enable(ubuffer[0], ubuffer[1], 0);
    		break;

    	case DISP_CMD_LAYER_SET_PIPE:
    		ret = BSP_disp_layer_set_pipe(ubuffer[0], ubuffer[1], ubuffer[2]);
    		break;

    	case DISP_CMD_LAYER_GET_ALPHA_VALUE:
    		ret = BSP_disp_layer_get_alpha_value(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_GET_ALPHA_EN:
    		ret = BSP_disp_layer_get_alpha_enable(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_GET_CK_EN:
    		ret = BSP_disp_layer_get_colorkey_enable(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_GET_PRIO:
    		ret = BSP_disp_layer_get_piro(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LAYER_GET_PIPE:
    		ret = BSP_disp_layer_get_pipe(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_SET_SMOOTH:
            ret = BSP_disp_layer_set_smooth(ubuffer[0], ubuffer[1],(__disp_video_smooth_t) ubuffer[2]);
    		break;

        case DISP_CMD_LAYER_GET_SMOOTH:
            ret = BSP_disp_layer_get_smooth(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_SET_BRIGHT:
            ret = BSP_disp_layer_set_bright(ubuffer[0], ubuffer[1], ubuffer[2]);
    		break;

        case DISP_CMD_LAYER_GET_BRIGHT:
            ret = BSP_disp_layer_get_bright(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_SET_CONTRAST:
            ret = BSP_disp_layer_set_contrast(ubuffer[0], ubuffer[1], ubuffer[2]);
    		break;

        case DISP_CMD_LAYER_GET_CONTRAST:
            ret = BSP_disp_layer_get_contrast(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_SET_SATURATION:
            ret = BSP_disp_layer_set_saturation(ubuffer[0], ubuffer[1], ubuffer[2]);
    		break;

        case DISP_CMD_LAYER_GET_SATURATION:
            ret = BSP_disp_layer_get_saturation(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_SET_HUE:
            ret = BSP_disp_layer_set_hue(ubuffer[0], ubuffer[1], ubuffer[2]);
    		break;

        case DISP_CMD_LAYER_GET_HUE:
            ret = BSP_disp_layer_get_hue(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_LAYER_ENHANCE_ON:
            ret = BSP_disp_layer_enhance_enable(ubuffer[0], ubuffer[1], 1);
    		break;

        case DISP_CMD_LAYER_ENHANCE_OFF:
            ret = BSP_disp_layer_enhance_enable(ubuffer[0], ubuffer[1], 0);
    		break;

        case DISP_CMD_LAYER_GET_ENHANCE_EN:
            ret = BSP_disp_layer_get_enhance_enable(ubuffer[0], ubuffer[1]);
    		break;

    //----scaler----
    	case DISP_CMD_SCALER_REQUEST:
    		ret = BSP_disp_scaler_request();
    		break;

    	case DISP_CMD_SCALER_RELEASE:
    		ret = BSP_disp_scaler_release(ubuffer[1]);
    		break;

    	case DISP_CMD_SCALER_EXECUTE:
    	{
    	    __disp_scaler_para_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_scaler_para_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_scaler_start(ubuffer[1],&para);
    		break;
        }

    //----hwc----
    	case DISP_CMD_HWC_OPEN:
    		ret =  BSP_disp_hwc_enable(ubuffer[0], 1);
    		break;

    	case DISP_CMD_HWC_CLOSE:
    		ret =  BSP_disp_hwc_enable(ubuffer[0], 0);
    		break;

    	case DISP_CMD_HWC_SET_POS:
    	{
    	    __disp_pos_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_pos_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_hwc_set_pos(ubuffer[0], &para);
    		break;
        }

    	case DISP_CMD_HWC_GET_POS:
    	{
    	    __disp_pos_t para;

    		ret = BSP_disp_hwc_get_pos(ubuffer[0], &para);
    		if(copy_to_user((void __user *)ubuffer[1],&para, sizeof(__disp_pos_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_HWC_SET_FB:
    	{
    	    __disp_hwc_pattern_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_hwc_pattern_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_hwc_set_framebuffer(ubuffer[0], &para);
    		break;
        }

    	case DISP_CMD_HWC_SET_PALETTE_TABLE:
			if((ubuffer[1] == 0) || ((int)ubuffer[3] <= 0))
            {
                __wrn("para invalid in display ioctrl DISP_CMD_HWC_SET_PALETTE_TABLE\n");
                return -1;
            }
    		if(copy_from_user(gbuffer, (void __user *)ubuffer[1],ubuffer[3]))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_hwc_set_palette(ubuffer[0], (void*)gbuffer, ubuffer[2], ubuffer[3]);
    		break;


    //----video----
    	case DISP_CMD_VIDEO_START:
    		ret = BSP_disp_video_start(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_VIDEO_STOP:
    		ret = BSP_disp_video_stop(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_VIDEO_SET_FB:
    	{
    	    __disp_video_fb_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_video_fb_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_video_set_fb(ubuffer[0], ubuffer[1], &para);
    		break;
        }

        case DISP_CMD_VIDEO_GET_FRAME_ID:
            ret = BSP_disp_video_get_frame_id(ubuffer[0], ubuffer[1]);
    		break;

        case DISP_CMD_VIDEO_GET_DIT_INFO:
        {
            __disp_dit_info_t para;

            ret = BSP_disp_video_get_dit_info(ubuffer[0], ubuffer[1],&para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_dit_info_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    //----lcd----
    	case DISP_CMD_LCD_ON:
    		ret = DRV_lcd_open(ubuffer[0]);
    		break;

    	case DISP_CMD_LCD_OFF:
    		ret = DRV_lcd_close(ubuffer[0]);
    		break;

    	case DISP_CMD_LCD_SET_BRIGHTNESS:
    		ret = BSP_disp_lcd_set_bright(ubuffer[0], (__disp_lcd_bright_t)ubuffer[1]);
    		break;

    	case DISP_CMD_LCD_GET_BRIGHTNESS:
    		ret = BSP_disp_lcd_get_bright(ubuffer[0]);
    		break;

    	case DISP_CMD_LCD_CPUIF_XY_SWITCH:
    		ret = BSP_disp_lcd_xy_switch(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_LCD_SET_SRC:
    		ret = BSP_disp_lcd_set_src(ubuffer[0], (__disp_lcdc_src_t)ubuffer[1]);
    		break;

    //----tv----
    	case DISP_CMD_TV_ON:
    		ret = BSP_disp_tv_open(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_OFF:
    		ret = BSP_disp_tv_close(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_SET_MODE:
    		ret = BSP_disp_tv_set_mode(ubuffer[0], (__disp_tv_mode_t)ubuffer[1]);
    		break;

    	case DISP_CMD_TV_GET_MODE:
    		ret = BSP_disp_tv_get_mode(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_AUTOCHECK_ON:
    		ret = BSP_disp_tv_auto_check_enable(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_AUTOCHECK_OFF:
    		ret = BSP_disp_tv_auto_check_disable(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_GET_INTERFACE:
    		ret = BSP_disp_tv_get_interface(ubuffer[0]);
    		break;

    	case DISP_CMD_TV_SET_SRC:
    		ret = BSP_disp_tv_set_src(ubuffer[0], (__disp_lcdc_src_t)ubuffer[1]);
    		break;

        case DISP_CMD_TV_GET_DAC_STATUS:
            ret =  BSP_disp_tv_get_dac_status(ubuffer[0], ubuffer[1]);
            break;

        case DISP_CMD_TV_SET_DAC_SOURCE:
            ret =  BSP_disp_tv_set_dac_source(ubuffer[0], ubuffer[1], (__disp_tv_dac_source)ubuffer[2]);
            break;

        case DISP_CMD_TV_GET_DAC_SOURCE:
            ret =  BSP_disp_tv_get_dac_source(ubuffer[0], ubuffer[1]);
            break;

    //----hdmi----
    	case DISP_CMD_HDMI_ON:
    		ret = BSP_disp_hdmi_open(ubuffer[0]);
    		break;

    	case DISP_CMD_HDMI_OFF:
    		ret = BSP_disp_hdmi_close(ubuffer[0]);
    		break;

    	case DISP_CMD_HDMI_SET_MODE:
    		ret = BSP_disp_hdmi_set_mode(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_HDMI_GET_MODE:
    		ret = BSP_disp_hdmi_get_mode(ubuffer[0]);
    		break;

    	case DISP_CMD_HDMI_GET_HPD_STATUS:
    	    ret = BSP_disp_hdmi_get_hpd_status(ubuffer[0]);
    		break;

    	case DISP_CMD_HDMI_SUPPORT_MODE:
    		ret = BSP_disp_hdmi_check_support_mode(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_HDMI_SET_SRC:
    		ret = BSP_disp_hdmi_set_src(ubuffer[0], (__disp_lcdc_src_t)ubuffer[1]);
    		break;

    //----vga----
    	case DISP_CMD_VGA_ON:
    		ret = BSP_disp_vga_open(ubuffer[0]);
    		break;

    	case DISP_CMD_VGA_OFF:
    		ret = BSP_disp_vga_close(ubuffer[0]);
    		break;

    	case DISP_CMD_VGA_SET_MODE:
    		ret = BSP_disp_vga_set_mode(ubuffer[0], (__disp_vga_mode_t)ubuffer[1]);
    		break;

    	case DISP_CMD_VGA_GET_MODE:
    		ret = BSP_disp_vga_get_mode(ubuffer[0]);
    		break;

    	case DISP_CMD_VGA_SET_SRC:
    		ret = BSP_disp_vga_set_src(ubuffer[0], (__disp_lcdc_src_t)ubuffer[1]);
    		break;

    //----sprite----
    	case DISP_CMD_SPRITE_OPEN:
    		ret = BSP_disp_sprite_open(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_CLOSE:
    		ret = BSP_disp_sprite_close(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_SET_FORMAT:
    		ret = BSP_disp_sprite_set_format(ubuffer[0], (__disp_pixel_fmt_t)ubuffer[1], (__disp_pixel_seq_t)ubuffer[2]);
    		break;

    	case DISP_CMD_SPRITE_GLOBAL_ALPHA_ENABLE:
    		ret = BSP_disp_sprite_alpha_enable(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_GLOBAL_ALPHA_DISABLE:
    		ret = BSP_disp_sprite_alpha_disable(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_ENABLE:
    		ret = BSP_disp_sprite_get_alpha_enable(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_SET_GLOBAL_ALPHA_VALUE:
    		ret = BSP_disp_sprite_set_alpha_vale(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_GET_GLOBAL_ALPHA_VALUE:
    		ret = BSP_disp_sprite_get_alpha_value(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_SET_ORDER:
    		ret = BSP_disp_sprite_set_order(ubuffer[0], ubuffer[1],ubuffer[2]);
    		break;

    	case DISP_CMD_SPRITE_GET_TOP_BLOCK:
    		ret = BSP_disp_sprite_get_top_block(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_GET_BOTTOM_BLOCK:
    		ret = BSP_disp_sprite_get_bottom_block(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_SET_PALETTE_TBL:
            if((ubuffer[1] == 0) || ((int)ubuffer[3] <= 0))
            {
                __wrn("para invalid in display ioctrl DISP_CMD_SPRITE_SET_PALETTE_TBL\n");
                return -1;
            }
    		if(copy_from_user(gbuffer, (void __user *)ubuffer[1],ubuffer[3]))
    		{
    			return  -EFAULT;
    		}
    		ret =  BSP_disp_sprite_set_palette_table(ubuffer[0], (__u32 * )gbuffer,ubuffer[2],ubuffer[3]);
    		break;

    	case DISP_CMD_SPRITE_GET_BLOCK_NUM:
    		ret = BSP_disp_sprite_get_block_number(ubuffer[0]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_REQUEST:
    	{
    	    __disp_sprite_block_para_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_sprite_block_para_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_sprite_block_request(ubuffer[0], &para);
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_RELEASE:
    		ret = BSP_disp_sprite_block_release(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_SET_SCREEN_WINDOW:
    	{
    	    __disp_rect_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_sprite_block_set_screen_win(ubuffer[0], ubuffer[1],&para);
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_GET_SCREEN_WINDOW:
    	{
    	    __disp_rect_t para;

    		ret = BSP_disp_sprite_block_get_srceen_win(ubuffer[0], ubuffer[1],&para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_SET_SOURCE_WINDOW:
    	{
    	    __disp_rect_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_sprite_block_set_src_win(ubuffer[0], ubuffer[1],&para);
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_GET_SOURCE_WINDOW:
    	{
    	    __disp_rect_t para;

    		ret = BSP_disp_sprite_block_get_src_win(ubuffer[0], ubuffer[1],&para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_rect_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_SET_FB:
    	{
    	    __disp_fb_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_fb_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_sprite_block_set_framebuffer(ubuffer[0], ubuffer[1],&para);
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_GET_FB:
    	{
    	    __disp_fb_t para;

    		ret = BSP_disp_sprite_block_get_framebufer(ubuffer[0], ubuffer[1],&para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_fb_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_SET_TOP:
    		ret = BSP_disp_sprite_block_set_top(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_SET_BOTTOM:
    		ret = BSP_disp_sprite_block_set_bottom(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_GET_PREV_BLOCK:
    		ret = BSP_disp_sprite_block_get_pre_block(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_GET_NEXT_BLOCK:
    		ret = BSP_disp_sprite_block_get_next_block(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_GET_PRIO:
    		ret = BSP_disp_sprite_block_get_prio(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_OPEN:
    		ret = BSP_disp_sprite_block_open(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_CLOSE:
    		ret = BSP_disp_sprite_block_close(ubuffer[0], ubuffer[1]);
    		break;

    	case DISP_CMD_SPRITE_BLOCK_SET_PARA:
    	{
    	    __disp_sprite_block_para_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[2],sizeof(__disp_sprite_block_para_t)))
    		{
    			return  -EFAULT;
    		}
    		ret = BSP_disp_sprite_block_set_para(ubuffer[0], ubuffer[1],&para);
    		break;
        }

    	case DISP_CMD_SPRITE_BLOCK_GET_PARA:
    	{
    	    __disp_sprite_block_para_t para;

    		ret = BSP_disp_sprite_block_get_para(ubuffer[0], ubuffer[1],&para);
    		if(copy_to_user((void __user *)ubuffer[2],&para, sizeof(__disp_sprite_block_para_t)))
    		{
    			return  -EFAULT;
    		}
    		break;
        }

	//----framebuffer----
    	case DISP_CMD_FB_REQUEST:
    	{
    	    __disp_fb_create_para_t para;

    		if(copy_from_user(&para, (void __user *)ubuffer[1],sizeof(__disp_fb_create_para_t)))
    		{
    			return  -EFAULT;
    		}
			ret = Display_Fb_Request(ubuffer[0], &para);
			break;
        }

		case DISP_CMD_FB_RELEASE:
			ret = Display_Fb_Release(ubuffer[0], ubuffer[1]);
			break;

		case DISP_CMD_MEM_REQUEST:
			ret =  disp_mem_request(ubuffer[0],ubuffer[1]);
			break;

		case DISP_CMD_MEM_RELASE:
			ret =  disp_mem_release(ubuffer[0]);
			break;
		case DISP_CMD_MEM_SELIDX:
			g_disp_mm_sel = ubuffer[0];
			break;
		case DISP_CMD_MEM_GETADR:
			return g_disp_mm[ubuffer[0]].mm_disp.mem_start;
		case DISP_CMD_SUSPEND:
			BSP_disp_clk_off();
			break;
		case DISP_CMD_RELEASE:
			BSP_disp_clk_on();
			break;
		default:
		    break;
    }

	return ret;
}


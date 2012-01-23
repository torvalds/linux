#include "drv_disp.h"
#include "dev_disp.h"

fb_info_t g_fbi;
__disp_drv_t g_disp_drv;

static struct cdev *my_cdev;
static dev_t devid ;
static struct class *disp_class;
__disp_drv_t    g_disp_drv;

static struct resource disp_resource[DISP_IO_NUM] =
{
	[DISP_IO_SCALER0] = {
		.start = 0x01e00000,
		.end   = 0x01e0077f,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_SCALER1] = {
		.start = 0x01e20000,
		.end   = 0x01e2077f,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_IMAGE0] = {
		.start = 0x01e60000,
		.end   = 0x01e633ff,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_IMAGE1] = {
		.start = 0x01e40000,
		.end   = 0x01e4005f,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_LCDC0] = {
		.start = 0x01c0c000,
		.end   = 0x01c0cfff,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_LCDC1] = {
		.start = 0x01c17000,
		.end   = 0x01c17fff,
		.flags = IORESOURCE_MEM,
	},
	[DISP_IO_TVEC] = {
		.start = 0x01c0a000,
		.end   = 0x01c0afff,
		.flags = IORESOURCE_MEM,
	},
};


struct platform_device disp_device =
{
	.name           = "disp",
	.id		        = -1,
	.num_resources  = ARRAY_SIZE(disp_resource),
	.resource	    = disp_resource,
	.dev            = {}
};



__s32 DRV_lcd_open(__u32 sel)
{
    __u32 i = 0;
    __lcd_flow_t *flow;

	if(g_disp_drv.b_lcd_open[sel] == 0)
	{
	    BSP_disp_lcd_open_before(sel);

	    flow = BSP_disp_lcd_get_open_flow(sel);
	    for(i=0; i<flow->func_num; i++)
	    {
	        __u32 timeout = flow->func[i].delay*HZ/1000;

	        flow->func[i].func(sel);

	    	set_current_state(TASK_INTERRUPTIBLE);
	    	schedule_timeout(timeout);

	    }

	    BSP_disp_lcd_open_after(sel);

		g_disp_drv.b_lcd_open[sel] = 1;
	}

    return 0;
}

__s32 DRV_lcd_close(__u32 sel)
{
    __u32 i = 0;
    __lcd_flow_t *flow;

	if(g_disp_drv.b_lcd_open[sel] == 1)
	{
	    BSP_disp_lcd_close_befor(sel);

	    flow = BSP_disp_lcd_get_close_flow(sel);
	    for(i=0; i<flow->func_num; i++)
	    {
	        __u32 timeout = flow->func[i].delay*HZ/1000;

	        flow->func[i].func(sel);

	    	set_current_state(TASK_INTERRUPTIBLE);
	    	schedule_timeout(timeout);

	    }

	    BSP_disp_lcd_close_after(sel);

		g_disp_drv.b_lcd_open[sel] = 0;
	}
    return 0;
}

__s32 DRV_scaler_begin(__u32 sel)
{
    down(g_disp_drv.scaler_finished_sem[sel]);
    return 0;
}

void DRV_scaler_finish(__u32 sel)
{
    up(g_disp_drv.scaler_finished_sem[sel]);
}


void DRV_tve_interrup(__u32 sel)
{
}
extern __s32 Hdmi_open(void);
extern __s32 Hdmi_close(void);
extern __s32 Hdmi_set_display_mode(__u8 mode);
extern __s32 Hdmi_mode_support(__u8 mode);
extern __s32 Hdmi_get_HPD_status(void);

__s32 DRV_Hdmi_open(void)
{
    return Hdmi_open();
}

__s32 DRV_Hdmi_close(void)
{
    return Hdmi_close();
}

__s32 DRV_hdmi_set_mode(__disp_tv_mode_t mode)
{
    return Hdmi_set_display_mode(mode);
}

__s32 DRV_hdmi_mode_support(__u8 mode)
{
    return Hdmi_mode_support(mode);
}

__s32 DRV_hdmi_get_HPD_status(void)
{
    return Hdmi_get_HPD_status();
}

__s32 DRV_DISP_Init(void)
{
    __disp_bsp_init_para para;
    __u32 i = 0;

    para.base_image0    = (__u32)g_fbi.io[DISP_IO_IMAGE0];
    para.base_image1    = (__u32)g_fbi.io[DISP_IO_IMAGE1];
    para.base_scaler0   = (__u32)g_fbi.io[DISP_IO_SCALER0];
    para.base_scaler1   = (__u32)g_fbi.io[DISP_IO_SCALER1];
    para.base_lcdc0     = (__u32)g_fbi.io[DISP_IO_LCDC0];
    para.base_lcdc1     = (__u32)g_fbi.io[DISP_IO_LCDC1];
    para.base_tvec      = (__u32)g_fbi.io[DISP_IO_TVEC];
    para.base_ccmu      = (__u32)g_fbi.base_ccmu;
    para.base_sdram     = (__u32)g_fbi.base_sdram;
    para.base_pioc      = (__u32)g_fbi.base_pio;
    para.scaler_begin   		= DRV_scaler_begin;
    para.scaler_finish  		= DRV_scaler_finish;
    para.tve_interrup   		= DRV_tve_interrup;
	para.hdmi_set_mode  		= DRV_hdmi_set_mode;
	para.Hdmi_open  			= DRV_Hdmi_open;
	para.Hdmi_close  			= DRV_Hdmi_close;
	para.hdmi_mode_support		= DRV_hdmi_mode_support;
	para.hdmi_get_HPD_status	= DRV_hdmi_get_HPD_status;
	para.disp_int_process       = disp_int_process;

	memset(&g_disp_drv, 0, sizeof(__disp_drv_t));

	g_disp_drv.scaler_finished_sem[0] = kmalloc(sizeof(struct semaphore),GFP_KERNEL | __GFP_ZERO);
    if(!g_disp_drv.scaler_finished_sem[0])
    {
        __wrn("create scaler_finished_sem[0] fail!\n");
        return -1;
    }
	sema_init(g_disp_drv.scaler_finished_sem[0],0);


	g_disp_drv.scaler_finished_sem[1] = kmalloc(sizeof(struct semaphore),GFP_KERNEL | __GFP_ZERO);
    if(!g_disp_drv.scaler_finished_sem[1])
    {
        __wrn("create scaler_finished_sem[1] fail!\n");
        return -1;
    }
	sema_init(g_disp_drv.scaler_finished_sem[1],0);

    for(i = 0; i<MAX_EVENT_SEM; i++)
    {
    	g_disp_drv.event_sem[0][i] = NULL;
     	g_disp_drv.event_sem[1][i] = NULL;
    }

    BSP_disp_init(&para);
    BSP_disp_open();
    Fb_Init();

    return 0;
}

__s32 DRV_DISP_Exit(void)
{
    Fb_Exit();
    BSP_disp_close();
    BSP_disp_exit(g_disp_drv.exit_mode);

	kfree(g_disp_drv.scaler_finished_sem[0]);
	kfree(g_disp_drv.scaler_finished_sem[1]);

    return 0;
}

static int __init disp_probe(struct platform_device *pdev)//called when platform_driver_register
{
	fb_info_t * info = NULL;
	struct resource *res;
	int ret = 0;
	int size;
	int i;

	__msg("disp_probe call\n");

	info = &g_fbi;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev,info);

	for(i=0;i<DISP_IO_NUM;i++)
	{
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res == NULL)
		{
			__wrn("failed to get memory registers\n");
			ret = -ENXIO;
			if(i==DISP_IO_SCALER0)
			{
				goto dealloc_fb;
			}
			else if(i==DISP_IO_SCALER1)
			{
				goto release_regs0;
			}
			else if(i==DISP_IO_IMAGE0)
			{
				goto release_regs1;
			}
			else if(i==DISP_IO_IMAGE1)
			{
				goto release_regs2;
			}
			else if(i==DISP_IO_LCDC0)
			{
				goto release_regs3;
			}
			else if(i==DISP_IO_LCDC1)
			{
				goto release_regs4;
			}
			else if(i==DISP_IO_TVEC)
			{
				goto release_regs5;
			}
		}

		size = (res->end - res->start) + 1;
		info->mem[i] = request_mem_region(res->start, size, pdev->name);
		if (info->mem[i] == NULL)
		{
			__wrn("failed to get memory region\n");
			ret = -ENOENT;
			if(i==DISP_IO_SCALER0)
			{
				goto dealloc_fb;
			}
			else if(i==DISP_IO_SCALER1)
			{
				goto release_regs0;
			}
			else if(i==DISP_IO_IMAGE0)
			{
				goto release_regs1;
			}
			else if(i==DISP_IO_IMAGE1)
			{
				goto release_regs2;
			}
			else if(i==DISP_IO_LCDC0)
			{
				goto release_regs3;
			}
			else if(i==DISP_IO_LCDC1)
			{
				goto release_regs4;
			}
			else if(i==DISP_IO_TVEC)
			{
				goto release_regs5;
			}
		}

		info->io[i] = ioremap(res->start, size);
		if (info->io[i] == NULL)
		{
			__wrn("ioremap() of registers failed\n");
			ret = -ENXIO;
			if(i==DISP_IO_SCALER0)
			{
				goto release_mem0;
			}
			else if(i==DISP_IO_SCALER1)
			{
				goto release_mem1;
			}
			else if(i==DISP_IO_IMAGE0)
			{
				goto release_mem2;
			}
			else if(i==DISP_IO_IMAGE1)
			{
				goto release_mem3;
			}
			else if(i==DISP_IO_LCDC0)
			{
				goto release_mem4;
			}
			else if(i==DISP_IO_LCDC1)
			{
				goto release_mem5;
			}
			else if(i==DISP_IO_TVEC)
			{
				goto release_mem6;
			}
	    }
	}

	info->base_ccmu = 0xf1c20000;
	info->base_sdram = 0xf1c01000;
	info->base_pio = 0xf1c20800;

	__msg("SCALER0 base 0x%08x\n", (__u32)info->io[DISP_IO_SCALER0]);
	__msg("SCALER1 base 0x%08x\n", (__u32)info->io[DISP_IO_SCALER1]);
	__msg("IMAGE0 base 0x%08x\n", (__u32)info->io[DISP_IO_IMAGE0]);
	__msg("IMAGE1 base 0x%08x\n", (__u32)info->io[DISP_IO_IMAGE1]);
	__msg("LCDC0 base 0x%08x\n", (__u32)info->io[DISP_IO_LCDC0]);
	__msg("LCDC1 base 0x%08x\n", (__u32)info->io[DISP_IO_LCDC1]);
	__msg("TVEC base 0x%08x\n", (__u32)info->io[DISP_IO_TVEC]);
	__msg("CCMU base 0x%08x\n", info->base_ccmu);
	__msg("SDRAM base 0x%08x\n", info->base_sdram);
	__msg("PIO base 0x%08x\n", info->base_pio);

    DRV_DISP_Init();

//tmp use begin!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//    writel(0x00000709, g_fbi.base_sdram+0x70);
//    writel(0x00000709, g_fbi.base_sdram+0x84);
//tmp use end!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	return 0;

release_mem6:
	release_resource(info->mem[6]);
	kfree(info->mem[6]);

release_regs5:
	iounmap(info->io[5]);
release_mem5:
	release_resource(info->mem[5]);
	kfree(info->mem[5]);

release_regs4:
	iounmap(info->io[4]);
release_mem4:
	release_resource(info->mem[4]);
	kfree(info->mem[4]);

release_regs3:
	iounmap(info->io[3]);
release_mem3:
	release_resource(info->mem[3]);
	kfree(info->mem[3]);

release_regs2:
	iounmap(info->io[2]);
release_mem2:
	release_resource(info->mem[2]);
	kfree(info->mem[2]);

release_regs1:
	iounmap(info->io[1]);
release_mem1:
	release_resource(info->mem[1]);
	kfree(info->mem[1]);

release_regs0:
	iounmap(info->io[0]);
release_mem0:
	release_resource(info->mem[0]);
	kfree(info->mem[0]);

dealloc_fb:
	platform_set_drvdata(pdev, NULL);
	kfree(info);

	return ret;
}


static int disp_remove(struct platform_device *pdev)
{
	fb_info_t *info = platform_get_drvdata(pdev);
	int i;

	__msg("disp_remove call\n");

	for(i=0;i<DISP_IO_NUM - 3;i++)
	{
		iounmap(info->io[i]);

		release_resource(info->mem[i]);
		kfree(info->mem[i]);
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static __u32 output_type[2] = {0,0};
int disp_suspend(struct platform_device *pdev, pm_message_t state)
{
    int i = 0;

    __msg("disp_suspend call\n");

    for(i=0; i<2; i++)
    {
        output_type[i] = BSP_disp_get_output_type(i);
        if(output_type[i] == DISP_OUTPUT_TYPE_LCD)
        {
            DRV_lcd_close(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_TV)
        {
            BSP_disp_tv_close(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_VGA)
        {
            BSP_disp_vga_close(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_HDMI)
        {
            BSP_disp_hdmi_close(i);
        }
    }

    BSP_disp_clk_off();

    return 0;
}

int disp_resume(struct platform_device *pdev)
{
    int i = 0;

    __msg("disp_resume call\n");
    BSP_disp_clk_on();

    for(i=0; i<2; i++)
    {
        if(output_type[i] == DISP_OUTPUT_TYPE_LCD)
        {
            DRV_lcd_open(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_TV)
        {
            BSP_disp_tv_open(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_VGA)
        {
            BSP_disp_vga_open(i);
        }
        else if(output_type[i] == DISP_OUTPUT_TYPE_HDMI)
        {
            BSP_disp_hdmi_open(i);
        }
    }

    return 0;
}

static struct platform_driver disp_driver =
{
	.probe		= disp_probe,
	.remove		= disp_remove,
	.suspend    = disp_suspend,
	.resume    = disp_resume,
	.driver		=
	{
		.name	= "disp",
		.owner	= THIS_MODULE,
	},
};


static const struct file_operations disp_fops =
{
	.owner		= THIS_MODULE,
	.open		= disp_open,
	.release    = disp_release,
	.write      = disp_write,
	.read		= disp_read,
	.unlocked_ioctl	= disp_ioctl,
	.mmap       = disp_mmap,
};

int __init disp_module_init(void)
{
	int ret, err;

	__msg("disp_module_init call\n");

    alloc_chrdev_region(&devid, 0, 1, "disp_chrdev");
    my_cdev = cdev_alloc();
    cdev_init(my_cdev, &disp_fops);
    my_cdev->owner = THIS_MODULE;
    err = cdev_add(my_cdev, devid, 1);
    if (err)
    {
        __wrn("I was assigned major number %d.\n", MAJOR(devid));
        return -1;
    }

    disp_class = class_create(THIS_MODULE, "disp_class");
    if (IS_ERR(disp_class))
    {
        __wrn("create class error\n");
        return -1;
    }

    device_create(disp_class, NULL, devid, NULL, "disp");

	ret = platform_device_register(&disp_device);

	if (ret == 0)
	{
		ret = platform_driver_register(&disp_driver);
	}


	return ret;
}

static void __exit disp_module_exit(void)
{
	__msg("disp_module_exit\n");

    DRV_DISP_Exit();

	platform_driver_unregister(&disp_driver);
	platform_device_unregister(&disp_device);

    device_destroy(disp_class,  devid);
    class_destroy(disp_class);

    cdev_del(my_cdev);
}

module_init(disp_module_init);
module_exit(disp_module_exit);

MODULE_AUTHOR("danling_xiao");
MODULE_DESCRIPTION("display driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:disp");



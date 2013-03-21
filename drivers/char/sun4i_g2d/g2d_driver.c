/*
 * drivers/char/sun4i_g2d/g2d_driver.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include"g2d_driver_i.h"
#include<linux/g2d_driver.h>
#include"g2d.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define G2D_BYTE_ALIGN(x) ( ( (x + (4*1024-1)) >> 12) << 12)             /* alloc based on 4K byte */
static struct g2d_alloc_struct boot_heap_head,boot_heap_tail;
static struct info_mem g2d_mem[MAX_G2D_MEM_INDEX];
static int	g2d_mem_sel = 0;

static struct class	*g2d_class;
static struct cdev	*g2d_cdev;
static dev_t		 devid ;
__g2d_drv_t			 g2d_ext_hd;
__g2d_info_t		 para;

static struct resource g2d_resource[2] =
{
	[0] = {
		.start	= 0x01e80000,
		.end	= 0x01e8ffff,
		.flags	= IORESOURCE_MEM,
	},

	[1] = {
		.start	= INTC_IRQNO_DE_MIX,
		.end	= INTC_IRQNO_DE_MIX,
		.flags	= IORESOURCE_IRQ,
	},

};

struct platform_device g2d_device =
{
	.name           = "g2d",
	.id		        = -1,
	.num_resources  = ARRAY_SIZE(g2d_resource),
	.resource	    = g2d_resource,
	.dev            = {}
};

int drv_g2d_begin(void)
{
	int result = 0;

	result = down_interruptible(g2d_ext_hd.g2d_finished_sem);
	return result;
}

int drv_g2d_finish(void)
{
	int result = 0;

	up(g2d_ext_hd.g2d_finished_sem);

	return result;

}

extern unsigned long g2d_start;
extern unsigned long g2d_size;

__s32 g2d_create_heap(__u32 pHeapHead, __u32 nHeapSize)
{
	if(pHeapHead <(__u32)__va(0x40000000))
	{
	    ERR("Invalid pHeapHead:%x\n", pHeapHead);
	    return -1;/* check valid */
	}

    boot_heap_head.size    = boot_heap_tail.size = 0;
    boot_heap_head.address = pHeapHead;
    boot_heap_tail.address = pHeapHead + nHeapSize;
    boot_heap_head.next    = &boot_heap_tail;
    boot_heap_tail.next    = 0;

    INFO("head:%x,tail:%x\n" ,boot_heap_head.address, boot_heap_tail.address);
    return 0;
}

__s32 drv_g2d_init(void)
{
    g2d_init_para init_para;

    DBG("drv_g2d_init\n");
    init_para.g2d_base		= (__u32)para.io;
    init_para.g2d_begin		= drv_g2d_begin;
    init_para.g2d_finish	= drv_g2d_finish;
    memset(&g2d_ext_hd, 0, sizeof(__g2d_drv_t));
    g2d_ext_hd.g2d_finished_sem = kmalloc(sizeof(struct semaphore),GFP_KERNEL | __GFP_ZERO);
    if(!g2d_ext_hd.g2d_finished_sem)
    {
        WARNING("create g2d_finished_sem fail!\n");
        return -1;
    }
    sema_init(g2d_ext_hd.g2d_finished_sem, 0);
    g2d_ext_hd.event_sem = 0;
    init_waitqueue_head(&g2d_ext_hd.queue);
	g2d_init(&init_para);

if(g2d_size !=0){
    INFO("g2dmem: g2d_start=%x, g2d_size=%x\n", (unsigned int)g2d_start, (unsigned int)g2d_size);
    g2d_create_heap((unsigned long)(__va(g2d_start)), g2d_size);
}

    return 0;
}

void *g2d_malloc(__u32 bytes_num)
{
	__u32 actual_bytes;
	struct g2d_alloc_struct *ptr, *newptr;

	if(!bytes_num)return 0;
	actual_bytes = G2D_BYTE_ALIGN(bytes_num);
	ptr = &boot_heap_head;
	while(ptr && ptr->next)
	{
		if(ptr->next->address >= (ptr->address + ptr->size +(8*1024)+ actual_bytes))
		{
			break;
		}
		ptr = ptr->next;
	}

    if (!ptr->next)
    {
        ERR(" it has reached the boot_heap_tail of the heap now\n");
        return 0;                   /* it has reached the boot_heap_tail of the heap now              */
    }

    newptr = (struct g2d_alloc_struct *)(ptr->address + ptr->size);
                                                /* create a new node for the memory block             */
    if (!newptr)
    {
        ERR(" create the node failed, can't manage the block\n");
        return 0;                               /* create the node failed, can't manage the block     */
    }

    /* set the memory block chain, insert the node to the chain */
    newptr->address = ptr->address + ptr->size + 4*1024;
    newptr->size    = actual_bytes;
    newptr->u_size  = bytes_num;
    newptr->next    = ptr->next;
    ptr->next       = newptr;

    return (void *)newptr->address;
}

void g2d_free(void *p)
{
    struct g2d_alloc_struct *ptr, *prev;

	if( p == NULL )
		return;

    ptr = &boot_heap_head;						/* look for the node which po__s32 this memory block                   */
    while (ptr && ptr->next)
    {
        if (ptr->next->address == (__u32)p)
            break;								/* find the node which need to be release                              */
        ptr = ptr->next;
    }

	prev = ptr;
	ptr = ptr->next;
    if (!ptr) return;							/* the node is heap boot_heap_tail                                     */

    prev->next = ptr->next;						/* delete the node which need be released from the memory block chain  */

    return;
}

__s32 g2d_get_free_mem_index(void)
{
    __u32 i = 0;

    for(i=0; i<MAX_G2D_MEM_INDEX; i++)
    {
        if(g2d_mem[i].b_used == 0)
        {
            return i;
        }
    }
    return -1;
}

int g2d_mem_request(__u32 size)
{
if (g2d_size ==0){
	__s32		 sel;
	struct page	*page;
	unsigned	 map_size = 0;

    sel = g2d_get_free_mem_index();
    if(sel < 0)
    {
        ERR("g2d_get_free_mem_index fail!\n");
        return -EINVAL;
    }

	map_size = (size + 4095) & 0xfffff000;//4k 对齐
	page = alloc_pages(GFP_KERNEL,get_order(map_size));

	if(page != NULL)
	{
		g2d_mem[sel].virt_addr = page_address(page);
		if(g2d_mem[sel].virt_addr == 0)
		{
			free_pages((unsigned long)(page),get_order(map_size));
			ERR("line %d:fail to alloc memory!\n",__LINE__);
			return -ENOMEM;
		}
		memset(g2d_mem[sel].virt_addr,0,size);
		g2d_mem[sel].phy_addr = virt_to_phys(g2d_mem[sel].virt_addr);
	    g2d_mem[sel].mem_len = size;
		g2d_mem[sel].b_used = 1;

		INFO("map_g2d_memory[%d]: pa=%08lx va=%p size:%x\n",sel,g2d_mem[sel].phy_addr, g2d_mem[sel].virt_addr, size);
		return sel;
	}
	else
	{
		ERR("fail to alloc memory!\n");
		return -ENOMEM;
	}
}
else{
	__s32 sel;
	__u32 ret = 0;

    sel = g2d_get_free_mem_index();
    if(sel < 0)
    {
        ERR("g2d_get_free_mem_index fail!\n");
        return -EINVAL;
    }

	ret = (__u32)g2d_malloc(size);
	if(ret != 0)
	{
	    g2d_mem[sel].virt_addr = (void*)ret;
	    memset(g2d_mem[sel].virt_addr,0,size);
		g2d_mem[sel].phy_addr = virt_to_phys(g2d_mem[sel].virt_addr);
		g2d_mem[sel].mem_len = size;
		g2d_mem[sel].b_used = 1;

		INFO("map_g2d_memory[%d]: pa=%08lx va=%p size:%x\n",sel,g2d_mem[sel].phy_addr, g2d_mem[sel].virt_addr, size);
		return sel;
	}
	else
	{
		ERR("fail to alloc reserved memory!\n");
		return -ENOMEM;
	}
}
}

int g2d_mem_release(__u32 sel)
{
if(g2d_size ==0){
	unsigned map_size = PAGE_ALIGN(g2d_mem[sel].mem_len);
	unsigned page_size = map_size;

	if(g2d_mem[sel].b_used == 0)
	{
	    ERR("mem not used in g2d_mem_release,%d\n",sel);
		return -EINVAL;
    }

	free_pages((unsigned long)(g2d_mem[sel].virt_addr),get_order(page_size));
	memset(&g2d_mem[sel],0,sizeof(struct info_mem));
}
else{

	if(g2d_mem[sel].b_used == 0)
	{
	    ERR("mem not used in g2d_mem_release,%d\n",sel);
		return -EINVAL;
    }

	g2d_free((void *)g2d_mem[sel].virt_addr);
	memset(&g2d_mem[sel],0,sizeof(struct info_mem));
}

	return 0;
}

int g2d_mmap(struct file *file, struct vm_area_struct * vma)
{
	unsigned long physics;
	unsigned long mypfn;
	unsigned long vmsize = vma->vm_end-vma->vm_start;

    if(g2d_mem[g2d_mem_sel].b_used == 0)
    {
        ERR("mem not used in g2d_mmap,%d\n",g2d_mem_sel);
        return -EINVAL;
    }

	physics =  g2d_mem[g2d_mem_sel].phy_addr;
	mypfn = physics >> PAGE_SHIFT;

	if(remap_pfn_range(vma,vma->vm_start,mypfn,vmsize,vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int g2d_open(struct inode *inode, struct file *file)
{
	g2d_clk_on();
	return 0;
}

static int g2d_release(struct inode *inode, struct file *file)
{
	g2d_clk_off();
	return 0;
}

long g2d_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	__s32	ret = 0;

	if(!mutex_trylock(&para.mutex)) {
			mutex_lock(&para.mutex);
	}
	switch (cmd) {

	/* Proceed to the operation */
	case G2D_CMD_BITBLT:{
		g2d_blt blit_para;
		if(copy_from_user(&blit_para, (g2d_blt *)arg, sizeof(g2d_blt)))
		{
			kfree(&blit_para);
			ret = -EFAULT;
			goto err_noput;
		}
	    ret = g2d_blit(&blit_para);
    	break;
	}
	case G2D_CMD_FILLRECT:{
		g2d_fillrect fill_para;
		if(copy_from_user(&fill_para, (g2d_fillrect *)arg, sizeof(g2d_fillrect)))
		{
			kfree(&fill_para);
			ret = -EFAULT;
			goto err_noput;
		}
	    ret = g2d_fill(&fill_para);
    	break;
	}
	case G2D_CMD_STRETCHBLT:{
		g2d_stretchblt stre_para;
		if(copy_from_user(&stre_para, (g2d_stretchblt *)arg, sizeof(g2d_stretchblt)))
		{
			kfree(&stre_para);
			ret = -EFAULT;
			goto err_noput;
		}
	    ret = g2d_stretchblit(&stre_para);
    	break;
	}
	case G2D_CMD_PALETTE_TBL:{
		g2d_palette pale_para;
		if(copy_from_user(&pale_para, (g2d_palette *)arg, sizeof(g2d_palette)))
		{
			kfree(&pale_para);
			ret = -EFAULT;
			goto err_noput;
		}
	    ret = g2d_set_palette_table(&pale_para);
    	break;
	}

	/* just management memory for test */
	case G2D_CMD_MEM_REQUEST:
		ret =  g2d_mem_request(arg);
		break;

	case G2D_CMD_MEM_RELEASE:
		ret =  g2d_mem_release(arg);
		break;

	case G2D_CMD_MEM_SELIDX:
		g2d_mem_sel = arg;
		break;

	case G2D_CMD_MEM_GETADR:
	    if(g2d_mem[arg].b_used)
	    {
		    ret = g2d_mem[arg].phy_addr;
		}
		else
		{
			ERR("mem not used in G2D_CMD_MEM_GETADR\n");
		    ret = -1;
		}
		break;

	/* Invalid IOCTL call */
	default:
		return -EINVAL;
	}

err_noput:
	mutex_unlock(&para.mutex);

	return ret;
}

static struct file_operations g2d_fops = {
	.owner				= THIS_MODULE,
	.open				= g2d_open,
	.release			= g2d_release,
	.unlocked_ioctl		= g2d_ioctl,
	.mmap				= g2d_mmap,
};

static int g2d_probe(struct platform_device *pdev)
{
	int size;
	int	ret = 0;
	struct resource	*res;
	__g2d_info_t	*info = NULL;

	info = &para;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev,info);

	/* get the clk */
	g2d_openclk();
//	g2d_clk_on();

	/* get the memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(res == NULL)
		{
			ERR("failed to get memory register\n");
			ret = -ENXIO;
			goto  dealloc_fb;
		}

	/* reserve the memory */
	size = (res->end - res->start) + 1;
	info->mem = request_mem_region(res->start, size, pdev->name);
	if(info->mem == NULL)
		{
			ERR("failed to get memory region\n");
			ret = -ENOENT;
			goto  relaese_regs;
		}

	/* map the memory */
	info->io = ioremap(res->start, size);
	if(info->io == NULL)
		{
			ERR("iormap() of register failed\n");
			ret = -ENXIO;
			goto  release_mem;
		}

	/* get the irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if(res == NULL)
		{
			ERR("failed to get irq resource\n");
			ret = -ENXIO;
			goto relaese_regs;
		}

	/* request the irq */
	info->irq = res->start;
	ret = request_irq(info->irq, g2d_handle_irq, 0, g2d_device.name, info);
	if(ret)
		{
			ERR("failed to install irq resource\n");
			goto relaese_regs;
		}

	drv_g2d_init();
	mutex_init(&info->mutex);
	return 0;

	relaese_regs:
		iounmap(info->io);
	release_mem:
		release_resource(info->mem);
		kfree(info->mem);
	dealloc_fb:
		platform_set_drvdata(pdev, NULL);
		kfree(info);

	return ret;
}

static int g2d_remove(struct platform_device *pdev)
{
	__g2d_info_t *info = platform_get_drvdata(pdev);

	/* power down */
	g2d_closeclk();

	free_irq(info->irq, info);
	iounmap(info->io);
	release_resource(info->mem);
	kfree(info->mem);

	platform_set_drvdata(pdev, NULL);

	INFO("Driver unloaded succesfully.\n");
	return 0;
}

static int g2d_suspend(struct platform_device *pdev, pm_message_t state)
{
	g2d_clk_off();
	INFO("g2d_suspend succesfully.\n");

	return 0;
}

static int g2d_resume(struct platform_device *pdev)
{
	INFO("%s. \n", __func__);
	g2d_clk_on();
	INFO("g2d_resume succesfully.\n");

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void g2d_early_suspend(struct early_suspend *h)
{
//    g2d_suspend(NULL, PMSG_SUSPEND);
}

void g2d_late_resume(struct early_suspend *h)
{
//    g2d_resume(NULL);
}

static struct early_suspend g2d_early_suspend_handler =
{
    .level   = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.suspend = g2d_early_suspend,
	.resume = g2d_late_resume,
};
#endif


static struct platform_driver g2d_driver = {
	.probe          = g2d_probe,
	.remove         = g2d_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend        = g2d_suspend,
	.resume         = g2d_resume,
#else
	.suspend        = NULL,
	.resume         = NULL,
#endif
	.driver			=
	{
		.owner		= THIS_MODULE,
		.name		= "g2d",
	},
};

int __init g2d_module_init(void)
{
	int ret, err;

    alloc_chrdev_region(&devid, 0, 1, "g2d_chrdev");
    g2d_cdev = cdev_alloc();
    cdev_init(g2d_cdev, &g2d_fops);
    g2d_cdev->owner = THIS_MODULE;
    err = cdev_add(g2d_cdev, devid, 1);
    if (err)
    {
        ERR("I was assigned major number %d.\n", MAJOR(devid));
        return -1;
    }

    g2d_class = class_create(THIS_MODULE, "g2d_class");
    if (IS_ERR(g2d_class))
    {
        ERR("create class error\n");
        return -1;
    }

	device_create(g2d_class, NULL, devid, NULL, "g2d");
	ret = platform_device_register(&g2d_device);
	if (ret == 0)
	{
		ret = platform_driver_register(&g2d_driver);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
    register_early_suspend(&g2d_early_suspend_handler);
#endif
	INFO("Module initialized.major:%d\n", MAJOR(devid));
	return ret;
}

static void __exit g2d_module_exit(void)
{
	INFO("g2d_module_exit\n");
	kfree(g2d_ext_hd.g2d_finished_sem);

#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&g2d_early_suspend_handler);
#endif

	platform_driver_unregister(&g2d_driver);
	platform_device_unregister(&g2d_device);

    device_destroy(g2d_class,  devid);
    class_destroy(g2d_class);

    cdev_del(g2d_cdev);
}

module_init(g2d_module_init);
module_exit(g2d_module_exit);

MODULE_AUTHOR("yupu_tang");
MODULE_DESCRIPTION("g2d driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:g2d");


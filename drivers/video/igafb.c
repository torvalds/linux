/*
 *  linux/drivers/video/igafb.c -- Frame buffer device for IGA 1682
 *
 *      Copyright (C) 1998  Vladimir Roganov and Gleb Raiko
 *
 *  This driver is partly based on the Frame buffer device for ATI Mach64
 *  and partially on VESA-related code.
 *
 *      Copyright (C) 1997-1998  Geert Uytterhoeven
 *      Copyright (C) 1998  Bernd Harries
 *      Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/******************************************************************************

  TODO:
       Despite of IGA Card has advanced graphic acceleration, 
       initial version is almost dummy and does not support it.
       Support for video modes and acceleration must be added
       together with accelerated X-Windows driver implementation.

       Most important thing at this moment is that we have working
       JavaEngine1  console & X  with new console interface.

******************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>

#include <asm/io.h>

#ifdef CONFIG_SPARC
#include <asm/prom.h>
#include <asm/pcic.h>
#endif

#include <video/iga.h>

struct pci_mmap_map {
    unsigned long voff;
    unsigned long poff;
    unsigned long size;
    unsigned long prot_flag;
    unsigned long prot_mask;
};

struct iga_par {
	struct pci_mmap_map *mmap_map;
	unsigned long frame_buffer_phys;
	unsigned long io_base;
};

struct fb_info fb_info;

struct fb_fix_screeninfo igafb_fix __initdata = {
        .id		= "IGA 1682",
	.type		= FB_TYPE_PACKED_PIXELS,
	.mmio_len 	= 1000
};

struct fb_var_screeninfo default_var = {
	/* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel	= 8,
	.red		= {0, 8, 0 },
	.green		= {0, 8, 0 },
	.blue		= {0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	.pixclock	= 39722,
	.left_margin	= 48,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED
};

#ifdef CONFIG_SPARC
struct fb_var_screeninfo default_var_1024x768 __initdata = {
	/* 1024x768, 75 Hz, Non-Interlaced (78.75 MHz dotclock) */
	.xres		= 1024,
	.yres		= 768,
	.xres_virtual	= 1024,
	.yres_virtual	= 768,
	.bits_per_pixel	= 8,
	.red		= {0, 8, 0 },
	.green		= {0, 8, 0 },
	.blue		= {0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	.pixclock	= 12699,
	.left_margin	= 176,
	.right_margin	= 16,
	.upper_margin	= 28,
	.lower_margin	= 1,
	.hsync_len	= 96,
	.vsync_len	= 3,
	.vmode		= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

struct fb_var_screeninfo default_var_1152x900 __initdata = {
	/* 1152x900, 76 Hz, Non-Interlaced (110.0 MHz dotclock) */
	.xres		= 1152,
	.yres		= 900,
	.xres_virtual	= 1152,
	.yres_virtual	= 900,
	.bits_per_pixel	= 8,
	.red		= { 0, 8, 0 },
	.green		= { 0, 8, 0 },
	.blue		= { 0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.accel_flags	= FB_ACCEL_NONE,
	.pixclock	= 9091,
	.left_margin	= 234,
	.right_margin	= 24,
	.upper_margin	= 34,
	.lower_margin	= 3,
	.hsync_len	= 100,
	.vsync_len	= 3,
	.vmode		= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

struct fb_var_screeninfo default_var_1280x1024 __initdata = {
	/* 1280x1024, 75 Hz, Non-Interlaced (135.00 MHz dotclock) */
	.xres		= 1280,
	.yres		= 1024,
	.xres_virtual	= 1280,
	.yres_virtual	= 1024,
	.bits_per_pixel	= 8,
	.red		= {0, 8, 0 }, 
	.green		= {0, 8, 0 },
	.blue		= {0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.accel_flags	= 0,
	.pixclock	= 7408,
	.left_margin	= 248,
	.right_margin	= 16,
	.upper_margin	= 38,
	.lower_margin	= 1,
	.hsync_len	= 144,
	.vsync_len	= 3,
	.vmode		= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
};

/*
 *   Memory-mapped I/O functions for Sparc PCI
 *
 * On sparc we happen to access I/O with memory mapped functions too.
 */ 
#define pci_inb(par, reg)        readb(par->io_base+(reg))
#define pci_outb(par, val, reg)  writeb(val, par->io_base+(reg))

static inline unsigned int iga_inb(struct iga_par *par, unsigned int reg,
				   unsigned int idx)
{
        pci_outb(par, idx, reg);
        return pci_inb(par, reg + 1);
}

static inline void iga_outb(struct iga_par *par, unsigned char val,
			    unsigned int reg, unsigned int idx )
{
        pci_outb(par, idx, reg);
        pci_outb(par, val, reg+1);
}

#endif /* CONFIG_SPARC */

/*
 *  Very important functionality for the JavaEngine1 computer:
 *  make screen border black (usign special IGA registers) 
 */
static void iga_blank_border(struct iga_par *par)
{
        int i;
#if 0
	/*
	 * PROM does this for us, so keep this code as a reminder
	 * about required read from 0x3DA and writing of 0x20 in the end.
	 */
	(void) pci_inb(par, 0x3DA);		/* required for every access */
	pci_outb(par, IGA_IDX_VGA_OVERSCAN, IGA_ATTR_CTL);
	(void) pci_inb(par, IGA_ATTR_CTL+1);
	pci_outb(par, 0x38, IGA_ATTR_CTL);
	pci_outb(par, 0x20, IGA_ATTR_CTL);	/* re-enable visual */
#endif
	/*
	 * This does not work as it was designed because the overscan
	 * color is looked up in the palette. Therefore, under X11
	 * overscan changes color.
	 */
	for (i=0; i < 3; i++)
		iga_outb(par, 0, IGA_EXT_CNTRL, IGA_IDX_OVERSCAN_COLOR + i);
}

#ifdef CONFIG_SPARC
static int igafb_mmap(struct fb_info *info,
		      struct vm_area_struct *vma)
{
	struct iga_par *par = (struct iga_par *)info->par;
	unsigned int size, page, map_size = 0;
	unsigned long map_offset = 0;
	int i;

	if (!par->mmap_map)
		return -ENXIO;

	size = vma->vm_end - vma->vm_start;

	/* Each page, see which map applies */
	for (page = 0; page < size; ) {
		map_size = 0;
		for (i = 0; par->mmap_map[i].size; i++) {
			unsigned long start = par->mmap_map[i].voff;
			unsigned long end = start + par->mmap_map[i].size;
			unsigned long offset = (vma->vm_pgoff << PAGE_SHIFT) + page;

			if (start > offset)
				continue;
			if (offset >= end)
				continue;

			map_size = par->mmap_map[i].size - (offset - start);
			map_offset = par->mmap_map[i].poff + (offset - start);
			break;
		}
		if (!map_size) {
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;

		pgprot_val(vma->vm_page_prot) &= ~(par->mmap_map[i].prot_mask);
		pgprot_val(vma->vm_page_prot) |= par->mmap_map[i].prot_flag;

		if (remap_pfn_range(vma, vma->vm_start + page,
			map_offset >> PAGE_SHIFT, map_size, vma->vm_page_prot))
			return -EAGAIN;

		page += map_size;
	}

	if (!map_size)
		return -EINVAL;

	vma->vm_flags |= VM_IO;
	return 0;
}
#endif /* CONFIG_SPARC */

static int igafb_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info)
{
        /*
         *  Set a single color register. The values supplied are
         *  already rounded down to the hardware's capabilities
         *  (according to the entries in the `var' structure). Return
         *  != 0 for invalid regno.
         */
	struct iga_par *par = (struct iga_par *)info->par;

        if (regno >= info->cmap.len)
                return 1;

	pci_outb(par, regno, DAC_W_INDEX);
	pci_outb(par, red,   DAC_DATA);
	pci_outb(par, green, DAC_DATA);
	pci_outb(par, blue,  DAC_DATA);

	if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:
			((u16*)(info->pseudo_palette))[regno] = 
				(regno << 10) | (regno << 5) | regno;
			break;
		case 24:
			((u32*)(info->pseudo_palette))[regno] = 
				(regno << 16) | (regno << 8) | regno;
		break;
		case 32:
			{ int i;
			i = (regno << 8) | regno;
			((u32*)(info->pseudo_palette))[regno] = (i << 16) | i;
			}
			break;
		}
	}
	return 0;
}

/*
 * Framebuffer option structure
 */
static struct fb_ops igafb_ops = {
	.owner 		= THIS_MODULE,
	.fb_setcolreg 	= igafb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
#ifdef CONFIG_SPARC
	.fb_mmap 	= igafb_mmap,
#endif
};

static int __init iga_init(struct fb_info *info, struct iga_par *par)
{
        char vramsz = iga_inb(par, IGA_EXT_CNTRL, IGA_IDX_EXT_BUS_CNTL) 
		                                         & MEM_SIZE_ALIAS;
	int video_cmap_len;

        switch (vramsz) {
        case MEM_SIZE_1M:
                info->fix.smem_len = 0x100000;
                break;
        case MEM_SIZE_2M:
                info->fix.smem_len = 0x200000;
                break;
        case MEM_SIZE_4M:
        case MEM_SIZE_RESERVED:
                info->fix.smem_len = 0x400000;
                break;
        }

        if (info->var.bits_per_pixel > 8) 
                video_cmap_len = 16;
        else 
                video_cmap_len = 256;

	info->fbops = &igafb_ops;
	info->flags = FBINFO_DEFAULT;

	fb_alloc_cmap(&info->cmap, video_cmap_len, 0);

	if (register_framebuffer(info) < 0)
		return 0;

	printk("fb%d: %s frame buffer device at 0x%08lx [%dMB VRAM]\n",
	       info->node, info->fix.id, 
	       par->frame_buffer_phys, info->fix.smem_len >> 20);

	iga_blank_border(par); 
	return 1;
}

int __init igafb_init(void)
{
        struct fb_info *info;
        struct pci_dev *pdev;
        struct iga_par *par;
	unsigned long addr;
	int size, iga2000 = 0;

	if (fb_get_options("igafb", NULL))
		return -ENODEV;

        pdev = pci_get_device(PCI_VENDOR_ID_INTERG,
                               PCI_DEVICE_ID_INTERG_1682, 0);
	if (pdev == NULL) {
		/*
		 * XXX We tried to use cyber2000fb.c for IGS 2000.
		 * But it does not initialize the chip in JavaStation-E, alas.
		 */
        	pdev = pci_get_device(PCI_VENDOR_ID_INTERG, 0x2000, 0);
        	if(pdev == NULL) {
        	        return -ENXIO;
		}
		iga2000 = 1;
	}
	/* We leak a reference here but as it cannot be unloaded this is
	   fine. If you write unload code remember to free it in unload */
	
	size = sizeof(struct fb_info) + sizeof(struct iga_par) + sizeof(u32)*16;

        info = kzalloc(size, GFP_ATOMIC);
        if (!info) {
                printk("igafb_init: can't alloc fb_info\n");
                return -ENOMEM;
        }

	par = (struct iga_par *) (info + 1);
	

	if ((addr = pdev->resource[0].start) == 0) {
                printk("igafb_init: no memory start\n");
		kfree(info);
		return -ENXIO;
	}

	if ((info->screen_base = ioremap(addr, 1024*1024*2)) == 0) {
                printk("igafb_init: can't remap %lx[2M]\n", addr);
		kfree(info);
		return -ENXIO;
	}

	par->frame_buffer_phys = addr & PCI_BASE_ADDRESS_MEM_MASK;

#ifdef CONFIG_SPARC
	/*
	 * The following is sparc specific and this is why:
	 *
	 * IGS2000 has its I/O memory mapped and we want
	 * to generate memory cycles on PCI, e.g. do ioremap(),
	 * then readb/writeb() as in Documentation/IO-mapping.txt.
	 *
	 * IGS1682 is more traditional, it responds to PCI I/O
	 * cycles, so we want to access it with inb()/outb().
	 *
	 * On sparc, PCIC converts CPU memory access within
	 * phys window 0x3000xxxx into PCI I/O cycles. Therefore
	 * we may use readb/writeb to access them with IGS1682.
	 *
	 * We do not take io_base_phys from resource[n].start
	 * on IGS1682 because that chip is BROKEN. It does not
	 * have a base register for I/O. We just "know" what its
	 * I/O addresses are.
	 */
	if (iga2000) {
		igafb_fix.mmio_start = par->frame_buffer_phys | 0x00800000;
	} else {
		igafb_fix.mmio_start = 0x30000000;	/* XXX */
	}
	if ((par->io_base = (int) ioremap(igafb_fix.mmio_start, igafb_fix.smem_len)) == 0) {
                printk("igafb_init: can't remap %lx[4K]\n", igafb_fix.mmio_start);
		iounmap((void *)info->screen_base);
		kfree(info);
		return -ENXIO;
	}

	/*
	 * Figure mmap addresses from PCI config space.
	 * We need two regions: for video memory and for I/O ports.
	 * Later one can add region for video coprocessor registers.
	 * However, mmap routine loops until size != 0, so we put
	 * one additional region with size == 0. 
	 */

	par->mmap_map = kzalloc(4 * sizeof(*par->mmap_map), GFP_ATOMIC);
	if (!par->mmap_map) {
		printk("igafb_init: can't alloc mmap_map\n");
		iounmap((void *)par->io_base);
		iounmap(info->screen_base);
		kfree(info);
		return -ENOMEM;
	}

	/*
	 * Set default vmode and cmode from PROM properties.
	 */
	{
		struct device_node *dp = pci_device_to_OF_node(pdev);
                int node = dp->node;
                int width = prom_getintdefault(node, "width", 1024);
                int height = prom_getintdefault(node, "height", 768);
                int depth = prom_getintdefault(node, "depth", 8);
                switch (width) {
                    case 1024:
                        if (height == 768)
                            default_var = default_var_1024x768;
                        break;
                    case 1152:
                        if (height == 900)
                            default_var = default_var_1152x900;
                        break;
                    case 1280:
                        if (height == 1024)
                            default_var = default_var_1280x1024;
                        break;
                    default:
                        break;
                }

                switch (depth) {
                    case 8:
                        default_var.bits_per_pixel = 8;
                        break;
                    case 16:
                        default_var.bits_per_pixel = 16;
                        break;
                    case 24:
                        default_var.bits_per_pixel = 24;
                        break;
                    case 32:
                        default_var.bits_per_pixel = 32;
                        break;
                    default:
                        break;
                }
            }

#endif
	igafb_fix.smem_start = (unsigned long) info->screen_base;
	igafb_fix.line_length = default_var.xres*(default_var.bits_per_pixel/8);
	igafb_fix.visual = default_var.bits_per_pixel <= 8 ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;

	info->var = default_var;
	info->fix = igafb_fix;
	info->pseudo_palette = (void *)(par + 1);
	info->device = &pdev->dev;

	if (!iga_init(info, par)) {
		iounmap((void *)par->io_base);
		iounmap(info->screen_base);
		kfree(par->mmap_map);
		kfree(info);
        }

#ifdef CONFIG_SPARC
	    /*
	     * Add /dev/fb mmap values.
	     */
	    
	    /* First region is for video memory */
	    par->mmap_map[0].voff = 0x0;  
	    par->mmap_map[0].poff = par->frame_buffer_phys & PAGE_MASK;
	    par->mmap_map[0].size = info->fix.smem_len & PAGE_MASK;
	    par->mmap_map[0].prot_mask = SRMMU_CACHE;
	    par->mmap_map[0].prot_flag = SRMMU_WRITE;

	    /* Second region is for I/O ports */
	    par->mmap_map[1].voff = par->frame_buffer_phys & PAGE_MASK;
	    par->mmap_map[1].poff = info->fix.smem_start & PAGE_MASK;
	    par->mmap_map[1].size = PAGE_SIZE * 2; /* X wants 2 pages */
	    par->mmap_map[1].prot_mask = SRMMU_CACHE;
	    par->mmap_map[1].prot_flag = SRMMU_WRITE;
#endif /* CONFIG_SPARC */

	return 0;
}

int __init igafb_setup(char *options)
{
    char *this_opt;

    if (!options || !*options)
        return 0;

    while ((this_opt = strsep(&options, ",")) != NULL) {
    }
    return 0;
}

module_init(igafb_init);
MODULE_LICENSE("GPL");
static struct pci_device_id igafb_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_1682,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};

MODULE_DEVICE_TABLE(pci, igafb_pci_tbl);

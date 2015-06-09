/*
 * Copyright (c) Intel Corp. 2007.
 * All Rights Reserved.
 *
 * Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 * develop this driver.
 *
 * This file is part of the Vermilion Range fb driver.
 * The Vermilion Range fb driver is free software;
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Vermilion Range fb driver is distributed
 * in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 *   Michel Dänzer <michel-at-tungstengraphics-dot-com>
 *   Alan Hourihane <alanh-at-tungstengraphics-dot-com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/mmzone.h>

/* #define VERMILION_DEBUG */

#include "vermilion.h"

#define MODULE_NAME "vmlfb"

#define VML_TOHW(_val, _width) ((((_val) << (_width)) + 0x7FFF - (_val)) >> 16)

static struct mutex vml_mutex;
static struct list_head global_no_mode;
static struct list_head global_has_mode;
static struct fb_ops vmlfb_ops;
static struct vml_sys *subsys = NULL;
static char *vml_default_mode = "1024x768@60";
static struct fb_videomode defaultmode = {
	NULL, 60, 1024, 768, 12896, 144, 24, 29, 3, 136, 6,
	0, FB_VMODE_NONINTERLACED
};

static u32 vml_mem_requested = (10 * 1024 * 1024);
static u32 vml_mem_contig = (4 * 1024 * 1024);
static u32 vml_mem_min = (4 * 1024 * 1024);

static u32 vml_clocks[] = {
	6750,
	13500,
	27000,
	29700,
	37125,
	54000,
	59400,
	74250,
	120000,
	148500
};

static u32 vml_num_clocks = ARRAY_SIZE(vml_clocks);

/*
 * Allocate a contiguous vram area and make its linear kernel map
 * uncached.
 */

static int vmlfb_alloc_vram_area(struct vram_area *va, unsigned max_order,
				 unsigned min_order)
{
	gfp_t flags;
	unsigned long i;

	max_order++;
	do {
		/*
		 * Really try hard to get the needed memory.
		 * We need memory below the first 32MB, so we
		 * add the __GFP_DMA flag that guarantees that we are
		 * below the first 16MB.
		 */

		flags = __GFP_DMA | __GFP_HIGH;
		va->logical =
			 __get_free_pages(flags, --max_order);
	} while (va->logical == 0 && max_order > min_order);

	if (!va->logical)
		return -ENOMEM;

	va->phys = virt_to_phys((void *)va->logical);
	va->size = PAGE_SIZE << max_order;
	va->order = max_order;

	/*
	 * It seems like __get_free_pages only ups the usage count
	 * of the first page. This doesn't work with fault mapping, so
	 * up the usage count once more (XXX: should use split_page or
	 * compound page).
	 */

	memset((void *)va->logical, 0x00, va->size);
	for (i = va->logical; i < va->logical + va->size; i += PAGE_SIZE) {
		get_page(virt_to_page(i));
	}

	/*
	 * Change caching policy of the linear kernel map to avoid
	 * mapping type conflicts with user-space mappings.
	 */
	set_pages_uc(virt_to_page(va->logical), va->size >> PAGE_SHIFT);

	printk(KERN_DEBUG MODULE_NAME
	       ": Allocated %ld bytes vram area at 0x%08lx\n",
	       va->size, va->phys);

	return 0;
}

/*
 * Free a contiguous vram area and reset its linear kernel map
 * mapping type.
 */

static void vmlfb_free_vram_area(struct vram_area *va)
{
	unsigned long j;

	if (va->logical) {

		/*
		 * Reset the linear kernel map caching policy.
		 */

		set_pages_wb(virt_to_page(va->logical),
				 va->size >> PAGE_SHIFT);

		/*
		 * Decrease the usage count on the pages we've used
		 * to compensate for upping when allocating.
		 */

		for (j = va->logical; j < va->logical + va->size;
		     j += PAGE_SIZE) {
			(void)put_page_testzero(virt_to_page(j));
		}

		printk(KERN_DEBUG MODULE_NAME
		       ": Freeing %ld bytes vram area at 0x%08lx\n",
		       va->size, va->phys);
		free_pages(va->logical, va->order);

		va->logical = 0;
	}
}

/*
 * Free allocated vram.
 */

static void vmlfb_free_vram(struct vml_info *vinfo)
{
	int i;

	for (i = 0; i < vinfo->num_areas; ++i) {
		vmlfb_free_vram_area(&vinfo->vram[i]);
	}
	vinfo->num_areas = 0;
}

/*
 * Allocate vram. Currently we try to allocate contiguous areas from the
 * __GFP_DMA zone and puzzle them together. A better approach would be to
 * allocate one contiguous area for scanout and use one-page allocations for
 * offscreen areas. This requires user-space and GPU virtual mappings.
 */

static int vmlfb_alloc_vram(struct vml_info *vinfo,
			    size_t requested,
			    size_t min_total, size_t min_contig)
{
	int i, j;
	int order;
	int contiguous;
	int err;
	struct vram_area *va;
	struct vram_area *va2;

	vinfo->num_areas = 0;
	for (i = 0; i < VML_VRAM_AREAS; ++i) {
		va = &vinfo->vram[i];
		order = 0;

		while (requested > (PAGE_SIZE << order) && order < MAX_ORDER)
			order++;

		err = vmlfb_alloc_vram_area(va, order, 0);

		if (err)
			break;

		if (i == 0) {
			vinfo->vram_start = va->phys;
			vinfo->vram_logical = (void __iomem *) va->logical;
			vinfo->vram_contig_size = va->size;
			vinfo->num_areas = 1;
		} else {
			contiguous = 0;

			for (j = 0; j < i; ++j) {
				va2 = &vinfo->vram[j];
				if (va->phys + va->size == va2->phys ||
				    va2->phys + va2->size == va->phys) {
					contiguous = 1;
					break;
				}
			}

			if (contiguous) {
				vinfo->num_areas++;
				if (va->phys < vinfo->vram_start) {
					vinfo->vram_start = va->phys;
					vinfo->vram_logical =
						(void __iomem *)va->logical;
				}
				vinfo->vram_contig_size += va->size;
			} else {
				vmlfb_free_vram_area(va);
				break;
			}
		}

		if (requested < va->size)
			break;
		else
			requested -= va->size;
	}

	if (vinfo->vram_contig_size > min_total &&
	    vinfo->vram_contig_size > min_contig) {

		printk(KERN_DEBUG MODULE_NAME
		       ": Contiguous vram: %ld bytes at physical 0x%08lx.\n",
		       (unsigned long)vinfo->vram_contig_size,
		       (unsigned long)vinfo->vram_start);

		return 0;
	}

	printk(KERN_ERR MODULE_NAME
	       ": Could not allocate requested minimal amount of vram.\n");

	vmlfb_free_vram(vinfo);

	return -ENOMEM;
}

/*
 * Find the GPU to use with our display controller.
 */

static int vmlfb_get_gpu(struct vml_par *par)
{
	mutex_lock(&vml_mutex);

	par->gpu = pci_get_device(PCI_VENDOR_ID_INTEL, VML_DEVICE_GPU, NULL);

	if (!par->gpu) {
		mutex_unlock(&vml_mutex);
		return -ENODEV;
	}

	mutex_unlock(&vml_mutex);

	if (pci_enable_device(par->gpu) < 0)
		return -ENODEV;

	return 0;
}

/*
 * Find a contiguous vram area that contains a given offset from vram start.
 */
static int vmlfb_vram_offset(struct vml_info *vinfo, unsigned long offset)
{
	unsigned long aoffset;
	unsigned i;

	for (i = 0; i < vinfo->num_areas; ++i) {
		aoffset = offset - (vinfo->vram[i].phys - vinfo->vram_start);

		if (aoffset < vinfo->vram[i].size) {
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * Remap the MMIO register spaces of the VDC and the GPU.
 */

static int vmlfb_enable_mmio(struct vml_par *par)
{
	int err;

	par->vdc_mem_base = pci_resource_start(par->vdc, 0);
	par->vdc_mem_size = pci_resource_len(par->vdc, 0);
	if (!request_mem_region(par->vdc_mem_base, par->vdc_mem_size, "vmlfb")) {
		printk(KERN_ERR MODULE_NAME
		       ": Could not claim display controller MMIO.\n");
		return -EBUSY;
	}
	par->vdc_mem = ioremap_nocache(par->vdc_mem_base, par->vdc_mem_size);
	if (par->vdc_mem == NULL) {
		printk(KERN_ERR MODULE_NAME
		       ": Could not map display controller MMIO.\n");
		err = -ENOMEM;
		goto out_err_0;
	}

	par->gpu_mem_base = pci_resource_start(par->gpu, 0);
	par->gpu_mem_size = pci_resource_len(par->gpu, 0);
	if (!request_mem_region(par->gpu_mem_base, par->gpu_mem_size, "vmlfb")) {
		printk(KERN_ERR MODULE_NAME ": Could not claim GPU MMIO.\n");
		err = -EBUSY;
		goto out_err_1;
	}
	par->gpu_mem = ioremap_nocache(par->gpu_mem_base, par->gpu_mem_size);
	if (par->gpu_mem == NULL) {
		printk(KERN_ERR MODULE_NAME ": Could not map GPU MMIO.\n");
		err = -ENOMEM;
		goto out_err_2;
	}

	return 0;

out_err_2:
	release_mem_region(par->gpu_mem_base, par->gpu_mem_size);
out_err_1:
	iounmap(par->vdc_mem);
out_err_0:
	release_mem_region(par->vdc_mem_base, par->vdc_mem_size);
	return err;
}

/*
 * Unmap the VDC and GPU register spaces.
 */

static void vmlfb_disable_mmio(struct vml_par *par)
{
	iounmap(par->gpu_mem);
	release_mem_region(par->gpu_mem_base, par->gpu_mem_size);
	iounmap(par->vdc_mem);
	release_mem_region(par->vdc_mem_base, par->vdc_mem_size);
}

/*
 * Release and uninit the VDC and GPU.
 */

static void vmlfb_release_devices(struct vml_par *par)
{
	if (atomic_dec_and_test(&par->refcount)) {
		pci_disable_device(par->gpu);
		pci_disable_device(par->vdc);
	}
}

/*
 * Free up allocated resources for a device.
 */

static void vml_pci_remove(struct pci_dev *dev)
{
	struct fb_info *info;
	struct vml_info *vinfo;
	struct vml_par *par;

	info = pci_get_drvdata(dev);
	if (info) {
		vinfo = container_of(info, struct vml_info, info);
		par = vinfo->par;
		mutex_lock(&vml_mutex);
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		vmlfb_free_vram(vinfo);
		vmlfb_disable_mmio(par);
		vmlfb_release_devices(par);
		kfree(vinfo);
		kfree(par);
		mutex_unlock(&vml_mutex);
	}
}

static void vmlfb_set_pref_pixel_format(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 16:
		var->blue.offset = 0;
		var->blue.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->red.offset = 10;
		var->red.length = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
		break;
	case 32:
		var->blue.offset = 0;
		var->blue.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->transp.offset = 24;
		var->transp.length = 0;
		break;
	default:
		break;
	}

	var->blue.msb_right = var->green.msb_right =
	    var->red.msb_right = var->transp.msb_right = 0;
}

/*
 * Device initialization.
 * We initialize one vml_par struct per device and one vml_info
 * struct per pipe. Currently we have only one pipe.
 */

static int vml_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct vml_info *vinfo;
	struct fb_info *info;
	struct vml_par *par;
	int err = 0;

	par = kzalloc(sizeof(*par), GFP_KERNEL);
	if (par == NULL)
		return -ENOMEM;

	vinfo = kzalloc(sizeof(*vinfo), GFP_KERNEL);
	if (vinfo == NULL) {
		err = -ENOMEM;
		goto out_err_0;
	}

	vinfo->par = par;
	par->vdc = dev;
	atomic_set(&par->refcount, 1);

	switch (id->device) {
	case VML_DEVICE_VDC:
		if ((err = vmlfb_get_gpu(par)))
			goto out_err_1;
		pci_set_drvdata(dev, &vinfo->info);
		break;
	default:
		err = -ENODEV;
		goto out_err_1;
	}

	info = &vinfo->info;
	info->flags = FBINFO_DEFAULT | FBINFO_PARTIAL_PAN_OK;

	err = vmlfb_enable_mmio(par);
	if (err)
		goto out_err_2;

	err = vmlfb_alloc_vram(vinfo, vml_mem_requested,
			       vml_mem_contig, vml_mem_min);
	if (err)
		goto out_err_3;

	strcpy(info->fix.id, "Vermilion Range");
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.smem_start = vinfo->vram_start;
	info->fix.smem_len = vinfo->vram_contig_size;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.ypanstep = 1;
	info->fix.xpanstep = 1;
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_NONE;
	info->screen_base = vinfo->vram_logical;
	info->pseudo_palette = vinfo->pseudo_palette;
	info->par = par;
	info->fbops = &vmlfb_ops;
	info->device = &dev->dev;

	INIT_LIST_HEAD(&vinfo->head);
	vinfo->pipe_disabled = 1;
	vinfo->cur_blank_mode = FB_BLANK_UNBLANK;

	info->var.grayscale = 0;
	info->var.bits_per_pixel = 16;
	vmlfb_set_pref_pixel_format(&info->var);

	if (!fb_find_mode
	    (&info->var, info, vml_default_mode, NULL, 0, &defaultmode, 16)) {
		printk(KERN_ERR MODULE_NAME ": Could not find initial mode\n");
	}

	if (fb_alloc_cmap(&info->cmap, 256, 1) < 0) {
		err = -ENOMEM;
		goto out_err_4;
	}

	err = register_framebuffer(info);
	if (err) {
		printk(KERN_ERR MODULE_NAME ": Register framebuffer error.\n");
		goto out_err_5;
	}

	printk("Initialized vmlfb\n");

	return 0;

out_err_5:
	fb_dealloc_cmap(&info->cmap);
out_err_4:
	vmlfb_free_vram(vinfo);
out_err_3:
	vmlfb_disable_mmio(par);
out_err_2:
	vmlfb_release_devices(par);
out_err_1:
	kfree(vinfo);
out_err_0:
	kfree(par);
	return err;
}

static int vmlfb_open(struct fb_info *info, int user)
{
	/*
	 * Save registers here?
	 */
	return 0;
}

static int vmlfb_release(struct fb_info *info, int user)
{
	/*
	 * Restore registers here.
	 */

	return 0;
}

static int vml_nearest_clock(int clock)
{

	int i;
	int cur_index;
	int cur_diff;
	int diff;

	cur_index = 0;
	cur_diff = clock - vml_clocks[0];
	cur_diff = (cur_diff < 0) ? -cur_diff : cur_diff;
	for (i = 1; i < vml_num_clocks; ++i) {
		diff = clock - vml_clocks[i];
		diff = (diff < 0) ? -diff : diff;
		if (diff < cur_diff) {
			cur_index = i;
			cur_diff = diff;
		}
	}
	return vml_clocks[cur_index];
}

static int vmlfb_check_var_locked(struct fb_var_screeninfo *var,
				  struct vml_info *vinfo)
{
	u32 pitch;
	u64 mem;
	int nearest_clock;
	int clock;
	int clock_diff;
	struct fb_var_screeninfo v;

	v = *var;
	clock = PICOS2KHZ(var->pixclock);

	if (subsys && subsys->nearest_clock) {
		nearest_clock = subsys->nearest_clock(subsys, clock);
	} else {
		nearest_clock = vml_nearest_clock(clock);
	}

	/*
	 * Accept a 20% diff.
	 */

	clock_diff = nearest_clock - clock;
	clock_diff = (clock_diff < 0) ? -clock_diff : clock_diff;
	if (clock_diff > clock / 5) {
#if 0
		printk(KERN_DEBUG MODULE_NAME ": Diff failure. %d %d\n",clock_diff,clock);
#endif
		return -EINVAL;
	}

	v.pixclock = KHZ2PICOS(nearest_clock);

	if (var->xres > VML_MAX_XRES || var->yres > VML_MAX_YRES) {
		printk(KERN_DEBUG MODULE_NAME ": Resolution failure.\n");
		return -EINVAL;
	}
	if (var->xres_virtual > VML_MAX_XRES_VIRTUAL) {
		printk(KERN_DEBUG MODULE_NAME
		       ": Virtual resolution failure.\n");
		return -EINVAL;
	}
	switch (v.bits_per_pixel) {
	case 0 ... 16:
		v.bits_per_pixel = 16;
		break;
	case 17 ... 32:
		v.bits_per_pixel = 32;
		break;
	default:
		printk(KERN_DEBUG MODULE_NAME ": Invalid bpp: %d.\n",
		       var->bits_per_pixel);
		return -EINVAL;
	}

	pitch = ALIGN((var->xres * var->bits_per_pixel) >> 3, 0x40);
	mem = pitch * var->yres_virtual;
	if (mem > vinfo->vram_contig_size) {
		return -ENOMEM;
	}

	switch (v.bits_per_pixel) {
	case 16:
		if (var->blue.offset != 0 ||
		    var->blue.length != 5 ||
		    var->green.offset != 5 ||
		    var->green.length != 5 ||
		    var->red.offset != 10 ||
		    var->red.length != 5 ||
		    var->transp.offset != 15 || var->transp.length != 1) {
			vmlfb_set_pref_pixel_format(&v);
		}
		break;
	case 32:
		if (var->blue.offset != 0 ||
		    var->blue.length != 8 ||
		    var->green.offset != 8 ||
		    var->green.length != 8 ||
		    var->red.offset != 16 ||
		    var->red.length != 8 ||
		    (var->transp.length != 0 && var->transp.length != 8) ||
		    (var->transp.length == 8 && var->transp.offset != 24)) {
			vmlfb_set_pref_pixel_format(&v);
		}
		break;
	default:
		return -EINVAL;
	}

	*var = v;

	return 0;
}

static int vmlfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct vml_info *vinfo = container_of(info, struct vml_info, info);
	int ret;

	mutex_lock(&vml_mutex);
	ret = vmlfb_check_var_locked(var, vinfo);
	mutex_unlock(&vml_mutex);

	return ret;
}

static void vml_wait_vblank(struct vml_info *vinfo)
{
	/* Wait for vblank. For now, just wait for a 50Hz cycle (20ms)) */
	mdelay(20);
}

static void vmlfb_disable_pipe(struct vml_info *vinfo)
{
	struct vml_par *par = vinfo->par;

	/* Disable the MDVO pad */
	VML_WRITE32(par, VML_RCOMPSTAT, 0);
	while (!(VML_READ32(par, VML_RCOMPSTAT) & VML_MDVO_VDC_I_RCOMP)) ;

	/* Disable display planes */
	VML_WRITE32(par, VML_DSPCCNTR,
		    VML_READ32(par, VML_DSPCCNTR) & ~VML_GFX_ENABLE);
	(void)VML_READ32(par, VML_DSPCCNTR);
	/* Wait for vblank for the disable to take effect */
	vml_wait_vblank(vinfo);

	/* Next, disable display pipes */
	VML_WRITE32(par, VML_PIPEACONF, 0);
	(void)VML_READ32(par, VML_PIPEACONF);

	vinfo->pipe_disabled = 1;
}

#ifdef VERMILION_DEBUG
static void vml_dump_regs(struct vml_info *vinfo)
{
	struct vml_par *par = vinfo->par;

	printk(KERN_DEBUG MODULE_NAME ": Modesetting register dump:\n");
	printk(KERN_DEBUG MODULE_NAME ": \tHTOTAL_A         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_HTOTAL_A));
	printk(KERN_DEBUG MODULE_NAME ": \tHBLANK_A         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_HBLANK_A));
	printk(KERN_DEBUG MODULE_NAME ": \tHSYNC_A          : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_HSYNC_A));
	printk(KERN_DEBUG MODULE_NAME ": \tVTOTAL_A         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_VTOTAL_A));
	printk(KERN_DEBUG MODULE_NAME ": \tVBLANK_A         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_VBLANK_A));
	printk(KERN_DEBUG MODULE_NAME ": \tVSYNC_A          : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_VSYNC_A));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPCSTRIDE       : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPCSTRIDE));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPCSIZE         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPCSIZE));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPCPOS          : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPCPOS));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPARB           : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPARB));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPCADDR         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPCADDR));
	printk(KERN_DEBUG MODULE_NAME ": \tBCLRPAT_A        : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_BCLRPAT_A));
	printk(KERN_DEBUG MODULE_NAME ": \tCANVSCLR_A       : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_CANVSCLR_A));
	printk(KERN_DEBUG MODULE_NAME ": \tPIPEASRC         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_PIPEASRC));
	printk(KERN_DEBUG MODULE_NAME ": \tPIPEACONF        : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_PIPEACONF));
	printk(KERN_DEBUG MODULE_NAME ": \tDSPCCNTR         : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_DSPCCNTR));
	printk(KERN_DEBUG MODULE_NAME ": \tRCOMPSTAT        : 0x%08x\n",
	       (unsigned)VML_READ32(par, VML_RCOMPSTAT));
	printk(KERN_DEBUG MODULE_NAME ": End of modesetting register dump.\n");
}
#endif

static int vmlfb_set_par_locked(struct vml_info *vinfo)
{
	struct vml_par *par = vinfo->par;
	struct fb_info *info = &vinfo->info;
	struct fb_var_screeninfo *var = &info->var;
	u32 htotal, hactive, hblank_start, hblank_end, hsync_start, hsync_end;
	u32 vtotal, vactive, vblank_start, vblank_end, vsync_start, vsync_end;
	u32 dspcntr;
	int clock;

	vinfo->bytes_per_pixel = var->bits_per_pixel >> 3;
	vinfo->stride = ALIGN(var->xres_virtual * vinfo->bytes_per_pixel, 0x40);
	info->fix.line_length = vinfo->stride;

	if (!subsys)
		return 0;

	htotal =
	    var->xres + var->right_margin + var->hsync_len + var->left_margin;
	hactive = var->xres;
	hblank_start = var->xres;
	hblank_end = htotal;
	hsync_start = hactive + var->right_margin;
	hsync_end = hsync_start + var->hsync_len;

	vtotal =
	    var->yres + var->lower_margin + var->vsync_len + var->upper_margin;
	vactive = var->yres;
	vblank_start = var->yres;
	vblank_end = vtotal;
	vsync_start = vactive + var->lower_margin;
	vsync_end = vsync_start + var->vsync_len;

	dspcntr = VML_GFX_ENABLE | VML_GFX_GAMMABYPASS;
	clock = PICOS2KHZ(var->pixclock);

	if (subsys->nearest_clock) {
		clock = subsys->nearest_clock(subsys, clock);
	} else {
		clock = vml_nearest_clock(clock);
	}
	printk(KERN_DEBUG MODULE_NAME
	       ": Set mode Hfreq : %d kHz, Vfreq : %d Hz.\n", clock / htotal,
	       ((clock / htotal) * 1000) / vtotal);

	switch (var->bits_per_pixel) {
	case 16:
		dspcntr |= VML_GFX_ARGB1555;
		break;
	case 32:
		if (var->transp.length == 8)
			dspcntr |= VML_GFX_ARGB8888 | VML_GFX_ALPHAMULT;
		else
			dspcntr |= VML_GFX_RGB0888;
		break;
	default:
		return -EINVAL;
	}

	vmlfb_disable_pipe(vinfo);
	mb();

	if (subsys->set_clock)
		subsys->set_clock(subsys, clock);
	else
		return -EINVAL;

	VML_WRITE32(par, VML_HTOTAL_A, ((htotal - 1) << 16) | (hactive - 1));
	VML_WRITE32(par, VML_HBLANK_A,
		    ((hblank_end - 1) << 16) | (hblank_start - 1));
	VML_WRITE32(par, VML_HSYNC_A,
		    ((hsync_end - 1) << 16) | (hsync_start - 1));
	VML_WRITE32(par, VML_VTOTAL_A, ((vtotal - 1) << 16) | (vactive - 1));
	VML_WRITE32(par, VML_VBLANK_A,
		    ((vblank_end - 1) << 16) | (vblank_start - 1));
	VML_WRITE32(par, VML_VSYNC_A,
		    ((vsync_end - 1) << 16) | (vsync_start - 1));
	VML_WRITE32(par, VML_DSPCSTRIDE, vinfo->stride);
	VML_WRITE32(par, VML_DSPCSIZE,
		    ((var->yres - 1) << 16) | (var->xres - 1));
	VML_WRITE32(par, VML_DSPCPOS, 0x00000000);
	VML_WRITE32(par, VML_DSPARB, VML_FIFO_DEFAULT);
	VML_WRITE32(par, VML_BCLRPAT_A, 0x00000000);
	VML_WRITE32(par, VML_CANVSCLR_A, 0x00000000);
	VML_WRITE32(par, VML_PIPEASRC,
		    ((var->xres - 1) << 16) | (var->yres - 1));

	wmb();
	VML_WRITE32(par, VML_PIPEACONF, VML_PIPE_ENABLE);
	wmb();
	VML_WRITE32(par, VML_DSPCCNTR, dspcntr);
	wmb();
	VML_WRITE32(par, VML_DSPCADDR, (u32) vinfo->vram_start +
		    var->yoffset * vinfo->stride +
		    var->xoffset * vinfo->bytes_per_pixel);

	VML_WRITE32(par, VML_RCOMPSTAT, VML_MDVO_PAD_ENABLE);

	while (!(VML_READ32(par, VML_RCOMPSTAT) &
		 (VML_MDVO_VDC_I_RCOMP | VML_MDVO_PAD_ENABLE))) ;

	vinfo->pipe_disabled = 0;
#ifdef VERMILION_DEBUG
	vml_dump_regs(vinfo);
#endif

	return 0;
}

static int vmlfb_set_par(struct fb_info *info)
{
	struct vml_info *vinfo = container_of(info, struct vml_info, info);
	int ret;

	mutex_lock(&vml_mutex);
	list_move(&vinfo->head, (subsys) ? &global_has_mode : &global_no_mode);
	ret = vmlfb_set_par_locked(vinfo);

	mutex_unlock(&vml_mutex);
	return ret;
}

static int vmlfb_blank_locked(struct vml_info *vinfo)
{
	struct vml_par *par = vinfo->par;
	u32 cur = VML_READ32(par, VML_PIPEACONF);

	switch (vinfo->cur_blank_mode) {
	case FB_BLANK_UNBLANK:
		if (vinfo->pipe_disabled) {
			vmlfb_set_par_locked(vinfo);
		}
		VML_WRITE32(par, VML_PIPEACONF, cur & ~VML_PIPE_FORCE_BORDER);
		(void)VML_READ32(par, VML_PIPEACONF);
		break;
	case FB_BLANK_NORMAL:
		if (vinfo->pipe_disabled) {
			vmlfb_set_par_locked(vinfo);
		}
		VML_WRITE32(par, VML_PIPEACONF, cur | VML_PIPE_FORCE_BORDER);
		(void)VML_READ32(par, VML_PIPEACONF);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
		if (!vinfo->pipe_disabled) {
			vmlfb_disable_pipe(vinfo);
		}
		break;
	case FB_BLANK_POWERDOWN:
		if (!vinfo->pipe_disabled) {
			vmlfb_disable_pipe(vinfo);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vmlfb_blank(int blank_mode, struct fb_info *info)
{
	struct vml_info *vinfo = container_of(info, struct vml_info, info);
	int ret;

	mutex_lock(&vml_mutex);
	vinfo->cur_blank_mode = blank_mode;
	ret = vmlfb_blank_locked(vinfo);
	mutex_unlock(&vml_mutex);
	return ret;
}

static int vmlfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct vml_info *vinfo = container_of(info, struct vml_info, info);
	struct vml_par *par = vinfo->par;

	mutex_lock(&vml_mutex);
	VML_WRITE32(par, VML_DSPCADDR, (u32) vinfo->vram_start +
		    var->yoffset * vinfo->stride +
		    var->xoffset * vinfo->bytes_per_pixel);
	(void)VML_READ32(par, VML_DSPCADDR);
	mutex_unlock(&vml_mutex);

	return 0;
}

static int vmlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *info)
{
	u32 v;

	if (regno >= 16)
		return -EINVAL;

	if (info->var.grayscale) {
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

	if (info->fix.visual != FB_VISUAL_TRUECOLOR)
		return -EINVAL;

	red = VML_TOHW(red, info->var.red.length);
	blue = VML_TOHW(blue, info->var.blue.length);
	green = VML_TOHW(green, info->var.green.length);
	transp = VML_TOHW(transp, info->var.transp.length);

	v = (red << info->var.red.offset) |
	    (green << info->var.green.offset) |
	    (blue << info->var.blue.offset) |
	    (transp << info->var.transp.offset);

	switch (info->var.bits_per_pixel) {
	case 16:
		((u32 *) info->pseudo_palette)[regno] = v;
		break;
	case 24:
	case 32:
		((u32 *) info->pseudo_palette)[regno] = v;
		break;
	}
	return 0;
}

static int vmlfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct vml_info *vinfo = container_of(info, struct vml_info, info);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int ret;
	unsigned long prot;

	ret = vmlfb_vram_offset(vinfo, offset);
	if (ret)
		return -EINVAL;

	prot = pgprot_val(vma->vm_page_prot) & ~_PAGE_CACHE_MASK;
	pgprot_val(vma->vm_page_prot) =
		prot | cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS);

	return vm_iomap_memory(vma, vinfo->vram_start,
			vinfo->vram_contig_size);
}

static int vmlfb_sync(struct fb_info *info)
{
	return 0;
}

static int vmlfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	return -EINVAL;	/* just to force soft_cursor() call */
}

static struct fb_ops vmlfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = vmlfb_open,
	.fb_release = vmlfb_release,
	.fb_check_var = vmlfb_check_var,
	.fb_set_par = vmlfb_set_par,
	.fb_blank = vmlfb_blank,
	.fb_pan_display = vmlfb_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_cursor = vmlfb_cursor,
	.fb_sync = vmlfb_sync,
	.fb_mmap = vmlfb_mmap,
	.fb_setcolreg = vmlfb_setcolreg
};

static struct pci_device_id vml_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, VML_DEVICE_VDC)},
	{0}
};

static struct pci_driver vmlfb_pci_driver = {
	.name = "vmlfb",
	.id_table = vml_ids,
	.probe = vml_pci_probe,
	.remove = vml_pci_remove,
};

static void __exit vmlfb_cleanup(void)
{
	pci_unregister_driver(&vmlfb_pci_driver);
}

static int __init vmlfb_init(void)
{

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options(MODULE_NAME, &option))
		return -ENODEV;
#endif

	printk(KERN_DEBUG MODULE_NAME ": initializing\n");
	mutex_init(&vml_mutex);
	INIT_LIST_HEAD(&global_no_mode);
	INIT_LIST_HEAD(&global_has_mode);

	return pci_register_driver(&vmlfb_pci_driver);
}

int vmlfb_register_subsys(struct vml_sys *sys)
{
	struct vml_info *entry;
	struct list_head *list;
	u32 save_activate;

	mutex_lock(&vml_mutex);
	if (subsys != NULL) {
		subsys->restore(subsys);
	}
	subsys = sys;
	subsys->save(subsys);

	/*
	 * We need to restart list traversal for each item, since we
	 * release the list mutex in the loop.
	 */

	list = global_no_mode.next;
	while (list != &global_no_mode) {
		list_del_init(list);
		entry = list_entry(list, struct vml_info, head);

		/*
		 * First, try the current mode which might not be
		 * completely validated with respect to the pixel clock.
		 */

		if (!vmlfb_check_var_locked(&entry->info.var, entry)) {
			vmlfb_set_par_locked(entry);
			list_add_tail(list, &global_has_mode);
		} else {

			/*
			 * Didn't work. Try to find another mode,
			 * that matches this subsys.
			 */

			mutex_unlock(&vml_mutex);
			save_activate = entry->info.var.activate;
			entry->info.var.bits_per_pixel = 16;
			vmlfb_set_pref_pixel_format(&entry->info.var);
			if (fb_find_mode(&entry->info.var,
					 &entry->info,
					 vml_default_mode, NULL, 0, NULL, 16)) {
				entry->info.var.activate |=
				    FB_ACTIVATE_FORCE | FB_ACTIVATE_NOW;
				fb_set_var(&entry->info, &entry->info.var);
			} else {
				printk(KERN_ERR MODULE_NAME
				       ": Sorry. no mode found for this subsys.\n");
			}
			entry->info.var.activate = save_activate;
			mutex_lock(&vml_mutex);
		}
		vmlfb_blank_locked(entry);
		list = global_no_mode.next;
	}
	mutex_unlock(&vml_mutex);

	printk(KERN_DEBUG MODULE_NAME ": Registered %s subsystem.\n",
				subsys->name ? subsys->name : "unknown");
	return 0;
}

EXPORT_SYMBOL_GPL(vmlfb_register_subsys);

void vmlfb_unregister_subsys(struct vml_sys *sys)
{
	struct vml_info *entry, *next;

	mutex_lock(&vml_mutex);
	if (subsys != sys) {
		mutex_unlock(&vml_mutex);
		return;
	}
	subsys->restore(subsys);
	subsys = NULL;
	list_for_each_entry_safe(entry, next, &global_has_mode, head) {
		printk(KERN_DEBUG MODULE_NAME ": subsys disable pipe\n");
		vmlfb_disable_pipe(entry);
		list_move_tail(&entry->head, &global_no_mode);
	}
	mutex_unlock(&vml_mutex);
}

EXPORT_SYMBOL_GPL(vmlfb_unregister_subsys);

module_init(vmlfb_init);
module_exit(vmlfb_cleanup);

MODULE_AUTHOR("Tungsten Graphics");
MODULE_DESCRIPTION("Initialization of the Vermilion display devices");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");

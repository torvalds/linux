/*
 * intelfb
 *
 * Linux framebuffer driver for Intel(R) 830M/845G/852GM/855GM/865G/915G/915GM
 * integrated graphics chips.
 *
 * Copyright © 2002, 2003 David Dawes <dawes@xfree86.org>
 *                   2004 Sylvain Meyer
 *
 * This driver consists of two parts.  The first part (intelfbdrv.c) provides
 * the basic fbdev interfaces, is derived in part from the radeonfb and
 * vesafb drivers, and is covered by the GPL.  The second part (intelfbhw.c)
 * provides the code to program the hardware.  Most of it is derived from
 * the i810/i830 XFree86 driver.  The HW-specific code is covered here
 * under a dual license (GPL and MIT/XFree86 license).
 *
 * Author: David Dawes
 *
 */

/* $DHD: intelfb/intelfbdrv.c,v 1.20 2003/06/27 15:17:40 dawes Exp $ */

/*
 * Changes:
 *    01/2003 - Initial driver (0.1.0), no mode switching, no acceleration.
 *		This initial version is a basic core that works a lot like
 *		the vesafb driver.  It must be built-in to the kernel,
 *		and the initial video mode must be set with vga=XXX at
 *		boot time.  (David Dawes)
 *
 *    01/2003 - Version 0.2.0: Mode switching added, colormap support
 *		implemented, Y panning, and soft screen blanking implemented.
 *		No acceleration yet.  (David Dawes)
 *
 *    01/2003 - Version 0.3.0: fbcon acceleration support added.  Module
 *		option handling added.  (David Dawes)
 *
 *    01/2003 - Version 0.4.0: fbcon HW cursor support added.  (David Dawes)
 *
 *    01/2003 - Version 0.4.1: Add auto-generation of built-in modes.
 *		(David Dawes)
 *
 *    02/2003 - Version 0.4.2: Add check for active non-CRT devices, and
 *		mode validation checks.  (David Dawes)
 *
 *    02/2003 - Version 0.4.3: Check when the VC is in graphics mode so that
 *		acceleration is disabled while an XFree86 server is running.
 *		(David Dawes)
 *
 *    02/2003 - Version 0.4.4: Monitor DPMS support.  (David Dawes)
 *
 *    02/2003 - Version 0.4.5: Basic XFree86 + fbdev working.  (David Dawes)
 *
 *    02/2003 - Version 0.5.0: Modify to work with the 2.5.32 kernel as well
 *		as 2.4.x kernels.  (David Dawes)
 *
 *    02/2003 - Version 0.6.0: Split out HW-specifics into a separate file.
 *		(David Dawes)
 *
 *    02/2003 - Version 0.7.0: Test on 852GM/855GM.  Acceleration and HW
 *		cursor are disabled on this platform.  (David Dawes)
 *
 *    02/2003 - Version 0.7.1: Test on 845G.  Acceleration is disabled
 *		on this platform.  (David Dawes)
 *
 *    02/2003 - Version 0.7.2: Test on 830M.  Acceleration and HW
 *		cursor are disabled on this platform.  (David Dawes)
 *
 *    02/2003 - Version 0.7.3: Fix 8-bit modes for mobile platforms
 *		(David Dawes)
 *
 *    02/2003 - Version 0.7.4: Add checks for FB and FBCON_HAS_CFB* configured
 *		in the kernel, and add mode bpp verification and default
 *		bpp selection based on which FBCON_HAS_CFB* are configured.
 *		(David Dawes)
 *
 *    02/2003 - Version 0.7.5: Add basic package/install scripts based on the
 *		DRI packaging scripts.  (David Dawes)
 *
 *    04/2003 - Version 0.7.6: Fix typo that affects builds with SMP-enabled
 *		kernels.  (David Dawes, reported by Anupam).
 *
 *    06/2003 - Version 0.7.7:
 *              Fix Makefile.kernel build problem (Tsutomu Yasuda).
 *		Fix mis-placed #endif (2.4.21 kernel).
 *
 *    09/2004 - Version 0.9.0 - by Sylvain Meyer
 *              Port to linux 2.6 kernel fbdev
 *              Fix HW accel and HW cursor on i845G
 *              Use of agpgart for fb memory reservation
 *              Add mtrr support
 *
 *    10/2004 - Version 0.9.1
 *              Use module_param instead of old MODULE_PARM
 *              Some cleanup
 *
 *    11/2004 - Version 0.9.2
 *              Add vram option to reserve more memory than stolen by BIOS
 *              Fix intelfbhw_pan_display typo
 *              Add __initdata annotations
 *
 * TODO:
 *
 *
 * Wish List:
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>

#include <asm/io.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "intelfb.h"
#include "intelfbhw.h"

static void __devinit get_initial_mode(struct intelfb_info *dinfo);
static void update_dinfo(struct intelfb_info *dinfo,
			 struct fb_var_screeninfo *var);
static int intelfb_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info);
static int intelfb_set_par(struct fb_info *info);
static int intelfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info);

static int intelfb_blank(int blank, struct fb_info *info);
static int intelfb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info);

static void intelfb_fillrect(struct fb_info *info,
			     const struct fb_fillrect *rect);
static void intelfb_copyarea(struct fb_info *info,
			     const struct fb_copyarea *region);
static void intelfb_imageblit(struct fb_info *info,
			      const struct fb_image *image);
static int intelfb_cursor(struct fb_info *info,
			   struct fb_cursor *cursor);

static int intelfb_sync(struct fb_info *info);

static int intelfb_ioctl(struct fb_info *info,
			 unsigned int cmd, unsigned long arg);

static int __devinit intelfb_pci_register(struct pci_dev *pdev,
					  const struct pci_device_id *ent);
static void __devexit intelfb_pci_unregister(struct pci_dev *pdev);
static int __devinit intelfb_set_fbinfo(struct intelfb_info *dinfo);

/*
 * Limiting the class to PCI_CLASS_DISPLAY_VGA prevents function 1 of the
 * mobile chipsets from being registered.
 */
#if DETECT_VGA_CLASS_ONLY
#define INTELFB_CLASS_MASK ~0 << 8
#else
#define INTELFB_CLASS_MASK 0
#endif

static struct pci_device_id intelfb_pci_table[] __devinitdata = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_830M, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_830M },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_845G, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_845G },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_85XGM, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_85XGM },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_865G, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_865G },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_915G, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_915G },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_915GM, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8, INTELFB_CLASS_MASK, INTEL_915GM },
	{ 0, }
};

/* Global data */
static int num_registered = 0;

/* fb ops */
static struct fb_ops intel_fb_ops = {
	.owner =		THIS_MODULE,
	.fb_check_var =         intelfb_check_var,
	.fb_set_par =           intelfb_set_par,
	.fb_setcolreg =		intelfb_setcolreg,
	.fb_blank =		intelfb_blank,
	.fb_pan_display =       intelfb_pan_display,
	.fb_fillrect  =         intelfb_fillrect,
	.fb_copyarea  =         intelfb_copyarea,
	.fb_imageblit =         intelfb_imageblit,
	.fb_cursor =            intelfb_cursor,
	.fb_sync =              intelfb_sync,
	.fb_ioctl =		intelfb_ioctl
};

/* PCI driver module table */
static struct pci_driver intelfb_driver = {
	.name =		"intelfb",
	.id_table =	intelfb_pci_table,
	.probe =	intelfb_pci_register,
	.remove =	__devexit_p(intelfb_pci_unregister)
};

/* Module description/parameters */
MODULE_AUTHOR("David Dawes <dawes@tungstengraphics.com>, "
	      "Sylvain Meyer <sylvain.meyer@worldonline.fr>");
MODULE_DESCRIPTION(
	"Framebuffer driver for Intel(R) " SUPPORTED_CHIPSETS " chipsets");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, intelfb_pci_table);

static int accel        = 1;
static int vram         = 4;
static int hwcursor     = 0;
static int mtrr         = 1;
static int fixed        = 0;
static int noinit       = 0;
static int noregister   = 0;
static int probeonly    = 0;
static int idonly       = 0;
static int bailearly    = 0;
static int voffset	= 48;
static char *mode       = NULL;

module_param(accel, bool, S_IRUGO);
MODULE_PARM_DESC(accel, "Enable hardware acceleration");
module_param(vram, int, S_IRUGO);
MODULE_PARM_DESC(vram, "System RAM to allocate to framebuffer in MiB");
module_param(voffset, int, S_IRUGO);
MODULE_PARM_DESC(voffset, "Offset of framebuffer in MiB");
module_param(hwcursor, bool, S_IRUGO);
MODULE_PARM_DESC(hwcursor, "Enable HW cursor");
module_param(mtrr, bool, S_IRUGO);
MODULE_PARM_DESC(mtrr, "Enable MTRR support");
module_param(fixed, bool, S_IRUGO);
MODULE_PARM_DESC(fixed, "Disable mode switching");
module_param(noinit, bool, 0);
MODULE_PARM_DESC(noinit, "Don't initialise graphics mode when loading");
module_param(noregister, bool, 0);
MODULE_PARM_DESC(noregister, "Don't register, just probe and exit (debug)");
module_param(probeonly, bool, 0);
MODULE_PARM_DESC(probeonly, "Do a minimal probe (debug)");
module_param(idonly, bool, 0);
MODULE_PARM_DESC(idonly, "Just identify without doing anything else (debug)");
module_param(bailearly, bool, 0);
MODULE_PARM_DESC(bailearly, "Bail out early, depending on value (debug)");
module_param(mode, charp, S_IRUGO);
MODULE_PARM_DESC(mode,
		 "Initial video mode \"<xres>x<yres>[-<depth>][@<refresh>]\"");

#ifndef MODULE
#define OPT_EQUAL(opt, name) (!strncmp(opt, name, strlen(name)))
#define OPT_INTVAL(opt, name) simple_strtoul(opt + strlen(name), NULL, 0)
#define OPT_STRVAL(opt, name) (opt + strlen(name))

static __inline__ char *
get_opt_string(const char *this_opt, const char *name)
{
	const char *p;
	int i;
	char *ret;

	p = OPT_STRVAL(this_opt, name);
	i = 0;
	while (p[i] && p[i] != ' ' && p[i] != ',')
		i++;
	ret = kmalloc(i + 1, GFP_KERNEL);
	if (ret) {
		strncpy(ret, p, i);
		ret[i] = '\0';
	}
	return ret;
}

static __inline__ int
get_opt_int(const char *this_opt, const char *name, int *ret)
{
	if (!ret)
		return 0;

	if (!OPT_EQUAL(this_opt, name))
		return 0;

	*ret = OPT_INTVAL(this_opt, name);
	return 1;
}

static __inline__ int
get_opt_bool(const char *this_opt, const char *name, int *ret)
{
	if (!ret)
		return 0;

	if (OPT_EQUAL(this_opt, name)) {
		if (this_opt[strlen(name)] == '=')
			*ret = simple_strtoul(this_opt + strlen(name) + 1,
					      NULL, 0);
		else
			*ret = 1;
	} else {
		if (OPT_EQUAL(this_opt, "no") && OPT_EQUAL(this_opt + 2, name))
			*ret = 0;
		else
			return 0;
	}
	return 1;
}

static int __init
intelfb_setup(char *options)
{
	char *this_opt;

	DBG_MSG("intelfb_setup\n");

	if (!options || !*options) {
		DBG_MSG("no options\n");
		return 0;
	} else
		DBG_MSG("options: %s\n", options);

	/*
	 * These are the built-in options analogous to the module parameters
	 * defined above.
	 *
	 * The syntax is:
	 *
	 *    video=intelfb:[mode][,<param>=<val>] ...
	 *
	 * e.g.,
	 *
	 *    video=intelfb:1024x768-16@75,accel=0
	 */

	while ((this_opt = strsep(&options, ","))) {
		if (!*this_opt)
			continue;
		if (get_opt_bool(this_opt, "accel", &accel))
			;
 		else if (get_opt_int(this_opt, "vram", &vram))
			;
		else if (get_opt_bool(this_opt, "hwcursor", &hwcursor))
			;
		else if (get_opt_bool(this_opt, "mtrr", &mtrr))
			;
		else if (get_opt_bool(this_opt, "fixed", &fixed))
			;
		else if (get_opt_bool(this_opt, "init", &noinit))
			noinit = !noinit;
		else if (OPT_EQUAL(this_opt, "mode="))
			mode = get_opt_string(this_opt, "mode=");
		else
			mode = this_opt;
	}

	return 0;
}

#endif

static int __init
intelfb_init(void)
{
#ifndef MODULE
	char *option = NULL;
#endif

	DBG_MSG("intelfb_init\n");

	INF_MSG("Framebuffer driver for "
		"Intel(R) " SUPPORTED_CHIPSETS " chipsets\n");
	INF_MSG("Version " INTELFB_VERSION "\n");

	if (idonly)
		return -ENODEV;

#ifndef MODULE
	if (fb_get_options("intelfb", &option))
		return -ENODEV;
	intelfb_setup(option);
#endif

	return pci_register_driver(&intelfb_driver);
}

static void __exit
intelfb_exit(void)
{
	DBG_MSG("intelfb_exit\n");
	pci_unregister_driver(&intelfb_driver);
}

module_init(intelfb_init);
module_exit(intelfb_exit);

/***************************************************************
 *                     mtrr support functions                  *
 ***************************************************************/

#ifdef CONFIG_MTRR
static inline void __devinit set_mtrr(struct intelfb_info *dinfo)
{
	dinfo->mtrr_reg = mtrr_add(dinfo->aperture.physical,
				   dinfo->aperture.size, MTRR_TYPE_WRCOMB, 1);
	if (dinfo->mtrr_reg < 0) {
		ERR_MSG("unable to set MTRR\n");
		return;
	}
	dinfo->has_mtrr = 1;
}
static inline void unset_mtrr(struct intelfb_info *dinfo)
{
  	if (dinfo->has_mtrr)
  		mtrr_del(dinfo->mtrr_reg, dinfo->aperture.physical,
			 dinfo->aperture.size);
}
#else
#define set_mtrr(x) WRN_MSG("MTRR is disabled in the kernel\n")

#define unset_mtrr(x) do { } while (0)
#endif /* CONFIG_MTRR */

/***************************************************************
 *                        driver init / cleanup                *
 ***************************************************************/

static void
cleanup(struct intelfb_info *dinfo)
{
	DBG_MSG("cleanup\n");

	if (!dinfo)
		return;

	fb_dealloc_cmap(&dinfo->info->cmap);
	kfree(dinfo->info->pixmap.addr);

	if (dinfo->registered)
		unregister_framebuffer(dinfo->info);

	unset_mtrr(dinfo);

	if (dinfo->fbmem_gart && dinfo->gtt_fb_mem) {
		agp_unbind_memory(dinfo->gtt_fb_mem);
		agp_free_memory(dinfo->gtt_fb_mem);
	}
	if (dinfo->gtt_cursor_mem) {
		agp_unbind_memory(dinfo->gtt_cursor_mem);
		agp_free_memory(dinfo->gtt_cursor_mem);
	}
	if (dinfo->gtt_ring_mem) {
		agp_unbind_memory(dinfo->gtt_ring_mem);
		agp_free_memory(dinfo->gtt_ring_mem);
	}

	if (dinfo->mmio_base)
		iounmap((void __iomem *)dinfo->mmio_base);
	if (dinfo->aperture.virtual)
		iounmap((void __iomem *)dinfo->aperture.virtual);

	if (dinfo->flag & INTELFB_MMIO_ACQUIRED)
		release_mem_region(dinfo->mmio_base_phys, INTEL_REG_SIZE);
	if (dinfo->flag & INTELFB_FB_ACQUIRED)
		release_mem_region(dinfo->aperture.physical,
				   dinfo->aperture.size);
	framebuffer_release(dinfo->info);
}

#define bailout(dinfo) do {						\
	DBG_MSG("bailout\n");						\
	cleanup(dinfo);							\
	INF_MSG("Not going to register framebuffer, exiting...\n");	\
	return -ENODEV;							\
} while (0)


static int __devinit
intelfb_pci_register(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct fb_info *info;
	struct intelfb_info *dinfo;
	int i, err, dvo;
	int aperture_size, stolen_size;
	struct agp_kern_info gtt_info;
	int agp_memtype;
	const char *s;
	struct agp_bridge_data *bridge;
 	int aperture_bar = 0;
 	int mmio_bar = 1;
	int offset;

	DBG_MSG("intelfb_pci_register\n");

	num_registered++;
	if (num_registered != 1) {
		ERR_MSG("Attempted to register %d devices "
			"(should be only 1).\n", num_registered);
		return -ENODEV;
	}

	info = framebuffer_alloc(sizeof(struct intelfb_info), &pdev->dev);
	if (!info) {
		ERR_MSG("Could not allocate memory for intelfb_info.\n");
		return -ENODEV;
	}
	if (fb_alloc_cmap(&info->cmap, 256, 1) < 0) {
		ERR_MSG("Could not allocate cmap for intelfb_info.\n");
		goto err_out_cmap;
		return -ENODEV;
	}

	dinfo = info->par;
	dinfo->info  = info;
	dinfo->fbops = &intel_fb_ops;
	dinfo->pdev  = pdev;

	/* Reserve pixmap space. */
	info->pixmap.addr = kmalloc(64 * 1024, GFP_KERNEL);
	if (info->pixmap.addr == NULL) {
		ERR_MSG("Cannot reserve pixmap memory.\n");
		goto err_out_pixmap;
	}
	memset(info->pixmap.addr, 0, 64 * 1024);

	/* set early this option because it could be changed by tv encoder
	   driver */
	dinfo->fixed_mode = fixed;

	/* Enable device. */
	if ((err = pci_enable_device(pdev))) {
		ERR_MSG("Cannot enable device.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	/* Set base addresses. */
	if ((ent->device == PCI_DEVICE_ID_INTEL_915G) ||
			(ent->device == PCI_DEVICE_ID_INTEL_915GM)) {
		aperture_bar = 2;
		mmio_bar = 0;
		/* Disable HW cursor on 915G/M (not implemented yet) */
		hwcursor = 0;
	}
	dinfo->aperture.physical = pci_resource_start(pdev, aperture_bar);
	dinfo->aperture.size     = pci_resource_len(pdev, aperture_bar);
	dinfo->mmio_base_phys    = pci_resource_start(pdev, mmio_bar);
	DBG_MSG("fb aperture: 0x%llx/0x%llx, MMIO region: 0x%llx/0x%llx\n",
		(unsigned long long)pci_resource_start(pdev, aperture_bar),
		(unsigned long long)pci_resource_len(pdev, aperture_bar),
		(unsigned long long)pci_resource_start(pdev, mmio_bar),
		(unsigned long long)pci_resource_len(pdev, mmio_bar));

	/* Reserve the fb and MMIO regions */
	if (!request_mem_region(dinfo->aperture.physical, dinfo->aperture.size,
				INTELFB_MODULE_NAME)) {
		ERR_MSG("Cannot reserve FB region.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	dinfo->flag |= INTELFB_FB_ACQUIRED;

	if (!request_mem_region(dinfo->mmio_base_phys,
				INTEL_REG_SIZE,
				INTELFB_MODULE_NAME)) {
		ERR_MSG("Cannot reserve MMIO region.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	dinfo->flag |= INTELFB_MMIO_ACQUIRED;

	/* Get the chipset info. */
	dinfo->pci_chipset = pdev->device;

	if (intelfbhw_get_chipset(pdev, dinfo)) {
		cleanup(dinfo);
		return -ENODEV;
	}

	if (intelfbhw_get_memory(pdev, &aperture_size,&stolen_size)) {
		cleanup(dinfo);
		return -ENODEV;
	}

	INF_MSG("%02x:%02x.%d: %s, aperture size %dMB, "
		"stolen memory %dkB\n",
		pdev->bus->number, PCI_SLOT(pdev->devfn),
		PCI_FUNC(pdev->devfn), dinfo->name,
		BtoMB(aperture_size), BtoKB(stolen_size));

	/* Set these from the options. */
	dinfo->accel    = accel;
	dinfo->hwcursor = hwcursor;

	if (NOACCEL_CHIPSET(dinfo) && dinfo->accel == 1) {
		INF_MSG("Acceleration is not supported for the %s chipset.\n",
			dinfo->name);
		dinfo->accel = 0;
	}

	/* Framebuffer parameters - Use all the stolen memory if >= vram */
	if (ROUND_UP_TO_PAGE(stolen_size) >= MB(vram)) {
		dinfo->fb.size = ROUND_UP_TO_PAGE(stolen_size);
		dinfo->fbmem_gart = 0;
	} else {
		dinfo->fb.size =  MB(vram);
		dinfo->fbmem_gart = 1;
	}

	/* Allocate space for the ring buffer and HW cursor if enabled. */
	if (dinfo->accel) {
		dinfo->ring.size = RINGBUFFER_SIZE;
		dinfo->ring_tail_mask = dinfo->ring.size - 1;
	}
	if (dinfo->hwcursor) {
		dinfo->cursor.size = HW_CURSOR_SIZE;
	}

	/* Use agpgart to manage the GATT */
	if (!(bridge = agp_backend_acquire(pdev))) {
		ERR_MSG("cannot acquire agp\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	/* get the current gatt info */
	if (agp_copy_info(bridge, &gtt_info)) {
		ERR_MSG("cannot get agp info\n");
		agp_backend_release(bridge);
		cleanup(dinfo);
		return -ENODEV;
	}

	if (MB(voffset) < stolen_size)
		offset = (stolen_size >> 12);
	else
		offset = ROUND_UP_TO_PAGE(MB(voffset))/GTT_PAGE_SIZE;

	/* set the mem offsets - set them after the already used pages */
	if (dinfo->accel) {
		dinfo->ring.offset = offset + gtt_info.current_memory;
	}
	if (dinfo->hwcursor) {
		dinfo->cursor.offset = offset +
			+ gtt_info.current_memory + (dinfo->ring.size >> 12);
	}
	if (dinfo->fbmem_gart) {
		dinfo->fb.offset = offset +
			+ gtt_info.current_memory + (dinfo->ring.size >> 12)
			+ (dinfo->cursor.size >> 12);
	}

	/* Allocate memories (which aren't stolen) */
	/* Map the fb and MMIO regions */
	/* ioremap only up to the end of used aperture */
	dinfo->aperture.virtual = (u8 __iomem *)ioremap_nocache
		(dinfo->aperture.physical, ((offset + dinfo->fb.offset) << 12)
		 + dinfo->fb.size);
	if (!dinfo->aperture.virtual) {
		ERR_MSG("Cannot remap FB region.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	dinfo->mmio_base =
		(u8 __iomem *)ioremap_nocache(dinfo->mmio_base_phys,
					       INTEL_REG_SIZE);
	if (!dinfo->mmio_base) {
		ERR_MSG("Cannot remap MMIO region.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	if (dinfo->accel) {
		if (!(dinfo->gtt_ring_mem =
		      agp_allocate_memory(bridge, dinfo->ring.size >> 12,
					  AGP_NORMAL_MEMORY))) {
			ERR_MSG("cannot allocate ring buffer memory\n");
			agp_backend_release(bridge);
			cleanup(dinfo);
			return -ENOMEM;
		}
		if (agp_bind_memory(dinfo->gtt_ring_mem,
				    dinfo->ring.offset)) {
			ERR_MSG("cannot bind ring buffer memory\n");
			agp_backend_release(bridge);
			cleanup(dinfo);
			return -EBUSY;
		}
		dinfo->ring.physical = dinfo->aperture.physical
			+ (dinfo->ring.offset << 12);
		dinfo->ring.virtual  = dinfo->aperture.virtual
			+ (dinfo->ring.offset << 12);
		dinfo->ring_head = dinfo->ring.virtual;
	}
	if (dinfo->hwcursor) {
		agp_memtype = dinfo->mobile ? AGP_PHYSICAL_MEMORY
			: AGP_NORMAL_MEMORY;
		if (!(dinfo->gtt_cursor_mem =
		      agp_allocate_memory(bridge, dinfo->cursor.size >> 12,
					  agp_memtype))) {
			ERR_MSG("cannot allocate cursor memory\n");
			agp_backend_release(bridge);
			cleanup(dinfo);
			return -ENOMEM;
		}
		if (agp_bind_memory(dinfo->gtt_cursor_mem,
				    dinfo->cursor.offset)) {
			ERR_MSG("cannot bind cursor memory\n");
			agp_backend_release(bridge);
			cleanup(dinfo);
			return -EBUSY;
		}
		if (dinfo->mobile)
			dinfo->cursor.physical
				= dinfo->gtt_cursor_mem->physical;
		else
			dinfo->cursor.physical = dinfo->aperture.physical
				+ (dinfo->cursor.offset << 12);
		dinfo->cursor.virtual = dinfo->aperture.virtual
			+ (dinfo->cursor.offset << 12);
	}
	if (dinfo->fbmem_gart) {
		if (!(dinfo->gtt_fb_mem =
		      agp_allocate_memory(bridge, dinfo->fb.size >> 12,
					  AGP_NORMAL_MEMORY))) {
			WRN_MSG("cannot allocate framebuffer memory - use "
				"the stolen one\n");
			dinfo->fbmem_gart = 0;
		}
		if (agp_bind_memory(dinfo->gtt_fb_mem,
				    dinfo->fb.offset)) {
			WRN_MSG("cannot bind framebuffer memory - use "
				"the stolen one\n");
			dinfo->fbmem_gart = 0;
		}
	}

	/* update framebuffer memory parameters */
	if (!dinfo->fbmem_gart)
		dinfo->fb.offset = 0;   /* starts at offset 0 */
	dinfo->fb.physical = dinfo->aperture.physical
		+ (dinfo->fb.offset << 12);
	dinfo->fb.virtual = dinfo->aperture.virtual + (dinfo->fb.offset << 12);
	dinfo->fb_start = dinfo->fb.offset << 12;

	/* release agpgart */
	agp_backend_release(bridge);

	if (mtrr)
		set_mtrr(dinfo);

	DBG_MSG("fb: 0x%x(+ 0x%x)/0x%x (0x%x)\n",
		dinfo->fb.physical, dinfo->fb.offset, dinfo->fb.size,
		(u32 __iomem ) dinfo->fb.virtual);
	DBG_MSG("MMIO: 0x%x/0x%x (0x%x)\n",
		dinfo->mmio_base_phys, INTEL_REG_SIZE,
		(u32 __iomem) dinfo->mmio_base);
	DBG_MSG("ring buffer: 0x%x/0x%x (0x%x)\n",
		dinfo->ring.physical, dinfo->ring.size,
		(u32 __iomem ) dinfo->ring.virtual);
	DBG_MSG("HW cursor: 0x%x/0x%x (0x%x) (offset 0x%x) (phys 0x%x)\n",
		dinfo->cursor.physical, dinfo->cursor.size,
		(u32 __iomem ) dinfo->cursor.virtual, dinfo->cursor.offset,
		dinfo->cursor.physical);

	DBG_MSG("options: vram = %d, accel = %d, hwcursor = %d, fixed = %d, "
		"noinit = %d\n", vram, accel, hwcursor, fixed, noinit);
	DBG_MSG("options: mode = \"%s\"\n", mode ? mode : "");

	if (probeonly)
		bailout(dinfo);

	/*
	 * Check if the LVDS port or any DVO ports are enabled.  If so,
	 * don't allow mode switching
	 */
	dvo = intelfbhw_check_non_crt(dinfo);
	if (dvo) {
		dinfo->fixed_mode = 1;
		WRN_MSG("Non-CRT device is enabled ( ");
		i = 0;
		while (dvo) {
			if (dvo & 1) {
				s = intelfbhw_dvo_to_string(1 << i);
				if (s)
					printk("%s ", s);
			}
			dvo >>= 1;
			++i;
		}
		printk(").  Disabling mode switching.\n");
	}

	if (bailearly == 1)
		bailout(dinfo);

	if (FIXED_MODE(dinfo) && ORIG_VIDEO_ISVGA != VIDEO_TYPE_VLFB) {
		ERR_MSG("Video mode must be programmed at boot time.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	if (bailearly == 2)
		bailout(dinfo);

	/* Initialise dinfo and related data. */
	/* If an initial mode was programmed at boot time, get its details. */
	if (ORIG_VIDEO_ISVGA == VIDEO_TYPE_VLFB)
		get_initial_mode(dinfo);

	if (bailearly == 3)
		bailout(dinfo);

	if (FIXED_MODE(dinfo)) {
		/* remap fb address */
		update_dinfo(dinfo, &dinfo->initial_var);
	}

	if (bailearly == 4)
		bailout(dinfo);


	if (intelfb_set_fbinfo(dinfo)) {
		cleanup(dinfo);
		return -ENODEV;
	}

	if (bailearly == 5)
		bailout(dinfo);

	if (bailearly == 6)
		bailout(dinfo);

	pci_set_drvdata(pdev, dinfo);

	/* Save the initial register state. */
	i = intelfbhw_read_hw_state(dinfo, &dinfo->save_state,
				    bailearly > 6 ? bailearly - 6 : 0);
	if (i != 0) {
		DBG_MSG("intelfbhw_read_hw_state returned %d\n", i);
		bailout(dinfo);
	}

	intelfbhw_print_hw_state(dinfo, &dinfo->save_state);

	if (bailearly == 18)
		bailout(dinfo);

	/* Cursor initialisation */
	if (dinfo->hwcursor) {
		intelfbhw_cursor_init(dinfo);
		intelfbhw_cursor_reset(dinfo);
	}

	if (bailearly == 19)
		bailout(dinfo);

	/* 2d acceleration init */
	if (dinfo->accel)
		intelfbhw_2d_start(dinfo);

	if (bailearly == 20)
		bailout(dinfo);

	if (noregister)
		bailout(dinfo);

	if (register_framebuffer(dinfo->info) < 0) {
		ERR_MSG("Cannot register framebuffer.\n");
		cleanup(dinfo);
		return -ENODEV;
	}

	dinfo->registered = 1;

	return 0;

err_out_pixmap:
	fb_dealloc_cmap(&info->cmap);
err_out_cmap:
	framebuffer_release(info);
	return -ENODEV;
}

static void __devexit
intelfb_pci_unregister(struct pci_dev *pdev)
{
	struct intelfb_info *dinfo = pci_get_drvdata(pdev);

	DBG_MSG("intelfb_pci_unregister\n");

	if (!dinfo)
		return;

	cleanup(dinfo);

	pci_set_drvdata(pdev, NULL);
}

/***************************************************************
 *                       helper functions                      *
 ***************************************************************/

int __inline__
intelfb_var_to_depth(const struct fb_var_screeninfo *var)
{
	DBG_MSG("intelfb_var_to_depth: bpp: %d, green.length is %d\n",
		var->bits_per_pixel, var->green.length);

	switch (var->bits_per_pixel) {
	case 16:
		return (var->green.length == 6) ? 16 : 15;
	case 32:
		return 24;
	default:
		return var->bits_per_pixel;
	}
}


static __inline__ int
var_to_refresh(const struct fb_var_screeninfo *var)
{
	int xtot = var->xres + var->left_margin + var->right_margin +
		   var->hsync_len;
	int ytot = var->yres + var->upper_margin + var->lower_margin +
		   var->vsync_len;

	return (1000000000 / var->pixclock * 1000 + 500) / xtot / ytot;
}

/***************************************************************
 *                Various intialisation functions              *
 ***************************************************************/

static void __devinit
get_initial_mode(struct intelfb_info *dinfo)
{
	struct fb_var_screeninfo *var;
	int xtot, ytot;

	DBG_MSG("get_initial_mode\n");

	dinfo->initial_vga = 1;
	dinfo->initial_fb_base = screen_info.lfb_base;
	dinfo->initial_video_ram = screen_info.lfb_size * KB(64);
	dinfo->initial_pitch = screen_info.lfb_linelength;

	var = &dinfo->initial_var;
	memset(var, 0, sizeof(*var));
	var->xres = screen_info.lfb_width;
	var->yres = screen_info.lfb_height;
	var->bits_per_pixel = screen_info.lfb_depth;
	switch (screen_info.lfb_depth) {
	case 15:
		var->bits_per_pixel = 16;
		break;
	case 24:
		var->bits_per_pixel = 32;
		break;
	}

	DBG_MSG("Initial info: FB is 0x%x/0x%x (%d kByte)\n",
		dinfo->initial_fb_base, dinfo->initial_video_ram,
		BtoKB(dinfo->initial_video_ram));

	DBG_MSG("Initial info: mode is %dx%d-%d (%d)\n",
		var->xres, var->yres, var->bits_per_pixel,
		dinfo->initial_pitch);

	/* Dummy timing values (assume 60Hz) */
	var->left_margin = (var->xres / 8) & 0xf8;
	var->right_margin = 32;
	var->upper_margin = 16;
	var->lower_margin = 4;
	var->hsync_len = (var->xres / 8) & 0xf8;
	var->vsync_len = 4;

	xtot = var->xres + var->left_margin +
		var->right_margin + var->hsync_len;
	ytot = var->yres + var->upper_margin +
		var->lower_margin + var->vsync_len;
	var->pixclock = 10000000 / xtot * 1000 / ytot * 100 / 60;

	var->height = -1;
	var->width = -1;

	if (var->bits_per_pixel > 8) {
		var->red.offset = screen_info.red_pos;
		var->red.length = screen_info.red_size;
		var->green.offset = screen_info.green_pos;
		var->green.length = screen_info.green_size;
		var->blue.offset = screen_info.blue_pos;
		var->blue.length = screen_info.blue_size;
		var->transp.offset = screen_info.rsvd_pos;
		var->transp.length = screen_info.rsvd_size;
	} else {
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
	}
}

static int __devinit
intelfb_init_var(struct intelfb_info *dinfo)
{
	struct fb_var_screeninfo *var;
	int msrc = 0;

	DBG_MSG("intelfb_init_var\n");

	var = &dinfo->info->var;
	if (FIXED_MODE(dinfo)) {
	        memcpy(var, &dinfo->initial_var,
		       sizeof(struct fb_var_screeninfo));
		msrc = 5;
	} else {
		if (mode) {
			msrc = fb_find_mode(var, dinfo->info, mode,
					    vesa_modes, VESA_MODEDB_SIZE,
					    NULL, 0);
			if (msrc)
				msrc |= 8;
		}
		if (!msrc) {
			msrc = fb_find_mode(var, dinfo->info, PREFERRED_MODE,
					    vesa_modes, VESA_MODEDB_SIZE,
					    NULL, 0);
		}
	}

	if (!msrc) {
		ERR_MSG("Cannot find a suitable video mode.\n");
		return 1;
	}

	INF_MSG("Initial video mode is %dx%d-%d@%d.\n", var->xres, var->yres,
		var->bits_per_pixel, var_to_refresh(var));

	DBG_MSG("Initial video mode is from %d.\n", msrc);

#if ALLOCATE_FOR_PANNING
	/* Allow use of half of the video ram for panning */
	var->xres_virtual = var->xres;
	var->yres_virtual =
		dinfo->fb.size / 2 / (var->bits_per_pixel * var->xres);
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
#else
	var->yres_virtual = var->yres;
#endif

	if (dinfo->accel)
		var->accel_flags |= FB_ACCELF_TEXT;
	else
		var->accel_flags &= ~FB_ACCELF_TEXT;

	return 0;
}

static int __devinit
intelfb_set_fbinfo(struct intelfb_info *dinfo)
{
	struct fb_info *info = dinfo->info;

	DBG_MSG("intelfb_set_fbinfo\n");

	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &intel_fb_ops;
	info->pseudo_palette = dinfo->pseudo_palette;

	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	if (intelfb_init_var(dinfo))
		return 1;

	info->pixmap.scan_align = 1;
	strcpy(info->fix.id, dinfo->name);
	info->fix.smem_start = dinfo->fb.physical;
	info->fix.smem_len = dinfo->fb.size;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 8;
	info->fix.ypanstep = 1;
	info->fix.ywrapstep = 0;
	info->fix.mmio_start = dinfo->mmio_base_phys;
	info->fix.mmio_len = INTEL_REG_SIZE;
	info->fix.accel = FB_ACCEL_I830;
	update_dinfo(dinfo, &info->var);

	return 0;
}

/* Update dinfo to match the active video mode. */
static void
update_dinfo(struct intelfb_info *dinfo, struct fb_var_screeninfo *var)
{
	DBG_MSG("update_dinfo\n");

	dinfo->bpp = var->bits_per_pixel;
	dinfo->depth = intelfb_var_to_depth(var);
	dinfo->xres = var->xres;
	dinfo->yres = var->xres;
	dinfo->pixclock = var->pixclock;

	dinfo->info->fix.visual = dinfo->visual;
	dinfo->info->fix.line_length = dinfo->pitch;

	switch (dinfo->bpp) {
	case 8:
		dinfo->visual = FB_VISUAL_PSEUDOCOLOR;
		dinfo->pitch = var->xres_virtual;
		break;
	case 16:
		dinfo->visual = FB_VISUAL_TRUECOLOR;
		dinfo->pitch = var->xres_virtual * 2;
		break;
	case 32:
		dinfo->visual = FB_VISUAL_TRUECOLOR;
		dinfo->pitch = var->xres_virtual * 4;
		break;
	}

	/* Make sure the line length is a aligned correctly. */
	dinfo->pitch = ROUND_UP_TO(dinfo->pitch, STRIDE_ALIGNMENT);

	if (FIXED_MODE(dinfo))
		dinfo->pitch = dinfo->initial_pitch;

	dinfo->info->screen_base = (char __iomem *)dinfo->fb.virtual;
	dinfo->info->fix.line_length = dinfo->pitch;
	dinfo->info->fix.visual = dinfo->visual;
}

/* fbops functions */

/***************************************************************
 *                       fbdev interface                       *
 ***************************************************************/

static int
intelfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int change_var = 0;
	struct fb_var_screeninfo v;
	struct intelfb_info *dinfo;
	static int first = 1;

	DBG_MSG("intelfb_check_var: accel_flags is %d\n", var->accel_flags);

	dinfo = GET_DINFO(info);

	if (intelfbhw_validate_mode(dinfo, var) != 0)
		return -EINVAL;

	v = *var;

	/* Check for a supported bpp. */
	if (v.bits_per_pixel <= 8) {
		v.bits_per_pixel = 8;
	} else if (v.bits_per_pixel <= 16) {
		if (v.bits_per_pixel == 16)
			v.green.length = 6;
		v.bits_per_pixel = 16;
	} else if (v.bits_per_pixel <= 32) {
		v.bits_per_pixel = 32;
	} else
		return -EINVAL;

	change_var = ((info->var.xres != var->xres) ||
		      (info->var.yres != var->yres) ||
		      (info->var.xres_virtual != var->xres_virtual) ||
		      (info->var.yres_virtual != var->yres_virtual) ||
		      (info->var.bits_per_pixel != var->bits_per_pixel) ||
		      memcmp(&info->var.red, &var->red, sizeof(var->red)) ||
		      memcmp(&info->var.green, &var->green,
			     sizeof(var->green)) ||
		      memcmp(&info->var.blue, &var->blue, sizeof(var->blue)));

	if (FIXED_MODE(dinfo) &&
	    (change_var ||
	     var->yres_virtual > dinfo->initial_var.yres_virtual ||
	     var->yres_virtual < dinfo->initial_var.yres ||
	     var->xoffset || var->nonstd)) {
		if (first) {
			ERR_MSG("Changing the video mode is not supported.\n");
			first = 0;
		}
		return -EINVAL;
	}

	switch (intelfb_var_to_depth(&v)) {
	case 8:
		v.red.offset = v.green.offset = v.blue.offset = 0;
		v.red.length = v.green.length = v.blue.length = 8;
		v.transp.offset = v.transp.length = 0;
		break;
	case 15:
		v.red.offset = 10;
		v.green.offset = 5;
		v.blue.offset = 0;
		v.red.length = v.green.length = v.blue.length = 5;
		v.transp.offset = v.transp.length = 0;
		break;
	case 16:
		v.red.offset = 11;
		v.green.offset = 5;
		v.blue.offset = 0;
		v.red.length = 5;
		v.green.length = 6;
		v.blue.length = 5;
		v.transp.offset = v.transp.length = 0;
		break;
	case 24:
		v.red.offset = 16;
		v.green.offset = 8;
		v.blue.offset = 0;
		v.red.length = v.green.length = v.blue.length = 8;
		v.transp.offset = v.transp.length = 0;
		break;
	case 32:
		v.red.offset = 16;
		v.green.offset = 8;
		v.blue.offset = 0;
		v.red.length = v.green.length = v.blue.length = 8;
		v.transp.offset = 24;
		v.transp.length = 8;
		break;
	}

	if (v.xoffset < 0)
		v.xoffset = 0;
	if (v.yoffset < 0)
		v.yoffset = 0;

	if (v.xoffset > v.xres_virtual - v.xres)
		v.xoffset = v.xres_virtual - v.xres;
	if (v.yoffset > v.yres_virtual - v.yres)
		v.yoffset = v.yres_virtual - v.yres;

	v.red.msb_right = v.green.msb_right = v.blue.msb_right =
			  v.transp.msb_right = 0;

        *var = v;

	return 0;
}

static int
intelfb_set_par(struct fb_info *info)
{
 	struct intelfb_hwstate *hw;
        struct intelfb_info *dinfo = GET_DINFO(info);

	if (FIXED_MODE(dinfo)) {
		ERR_MSG("Changing the video mode is not supported.\n");
		return -EINVAL;
	}

 	hw = kmalloc(sizeof(*hw), GFP_ATOMIC);
 	if (!hw)
 		return -ENOMEM;

	DBG_MSG("intelfb_set_par (%dx%d-%d)\n", info->var.xres,
		info->var.yres, info->var.bits_per_pixel);

	intelfb_blank(FB_BLANK_POWERDOWN, info);

	if (ACCEL(dinfo, info))
		intelfbhw_2d_stop(dinfo);

 	memcpy(hw, &dinfo->save_state, sizeof(*hw));
 	if (intelfbhw_mode_to_hw(dinfo, hw, &info->var))
 		goto invalid_mode;
 	if (intelfbhw_program_mode(dinfo, hw, 0))
 		goto invalid_mode;

#if REGDUMP > 0
 	intelfbhw_read_hw_state(dinfo, hw, 0);
 	intelfbhw_print_hw_state(dinfo, hw);
#endif

	update_dinfo(dinfo, &info->var);

	if (ACCEL(dinfo, info))
		intelfbhw_2d_start(dinfo);

	intelfb_pan_display(&info->var, info);

	intelfb_blank(FB_BLANK_UNBLANK, info);

	if (ACCEL(dinfo, info)) {
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN |
		FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_IMAGEBLIT;
	} else {
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	}
	kfree(hw);
	return 0;
invalid_mode:
	kfree(hw);
	return -EINVAL;
}

static int
intelfb_setcolreg(unsigned regno, unsigned red, unsigned green,
		  unsigned blue, unsigned transp, struct fb_info *info)
{
	struct intelfb_info *dinfo = GET_DINFO(info);

#if VERBOSE > 0
	DBG_MSG("intelfb_setcolreg: regno %d, depth %d\n", regno, dinfo->depth);
#endif

	if (regno > 255)
		return 1;

	if (dinfo->depth == 8) {
		red >>= 8;
		green >>= 8;
		blue >>= 8;

		intelfbhw_setcolreg(dinfo, regno, red, green, blue,
				    transp);
	}

	if (regno < 16) {
		switch (dinfo->depth) {
		case 15:
			dinfo->pseudo_palette[regno] = ((red & 0xf800) >>  1) |
				((green & 0xf800) >>  6) |
				((blue & 0xf800) >> 11);
			break;
		case 16:
			dinfo->pseudo_palette[regno] = (red & 0xf800) |
				((green & 0xfc00) >>  5) |
				((blue  & 0xf800) >> 11);
			break;
		case 24:
			dinfo->pseudo_palette[regno] = ((red & 0xff00) << 8) |
				(green & 0xff00) |
				((blue  & 0xff00) >> 8);
			break;
		}
	}

	return 0;
}

static int
intelfb_blank(int blank, struct fb_info *info)
{
	intelfbhw_do_blank(blank, info);
	return 0;
}

static int
intelfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	intelfbhw_pan_display(var, info);
	return 0;
}

/* When/if we have our own ioctls. */
static int
intelfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	return retval;
}

static void
intelfb_fillrect (struct fb_info *info, const struct fb_fillrect *rect)
{
        struct intelfb_info *dinfo = GET_DINFO(info);
	u32 rop, color;

#if VERBOSE > 0
	DBG_MSG("intelfb_fillrect\n");
#endif

	if (!ACCEL(dinfo, info) || dinfo->depth == 4)
		return cfb_fillrect(info, rect);

	if (rect->rop == ROP_COPY)
		rop = PAT_ROP_GXCOPY;
	else // ROP_XOR
		rop = PAT_ROP_GXXOR;

	if (dinfo->depth != 8)
		color = dinfo->pseudo_palette[rect->color];
	else
		color = rect->color;

	intelfbhw_do_fillrect(dinfo, rect->dx, rect->dy,
			      rect->width, rect->height, color,
			      dinfo->pitch, info->var.bits_per_pixel,
			      rop);
}

static void
intelfb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
        struct intelfb_info *dinfo = GET_DINFO(info);

#if VERBOSE > 0
	DBG_MSG("intelfb_copyarea\n");
#endif

	if (!ACCEL(dinfo, info) || dinfo->depth == 4)
		return cfb_copyarea(info, region);

	intelfbhw_do_bitblt(dinfo, region->sx, region->sy, region->dx,
			    region->dy, region->width, region->height,
			    dinfo->pitch, info->var.bits_per_pixel);
}

static void
intelfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
        struct intelfb_info *dinfo = GET_DINFO(info);
	u32 fgcolor, bgcolor;

#if VERBOSE > 0
	DBG_MSG("intelfb_imageblit\n");
#endif

	if (!ACCEL(dinfo, info) || dinfo->depth == 4
	    || image->depth != 1)
		return cfb_imageblit(info, image);

	if (dinfo->depth != 8) {
		fgcolor = dinfo->pseudo_palette[image->fg_color];
		bgcolor = dinfo->pseudo_palette[image->bg_color];
	} else {
		fgcolor = image->fg_color;
		bgcolor = image->bg_color;
	}

	if (!intelfbhw_do_drawglyph(dinfo, fgcolor, bgcolor, image->width,
				    image->height, image->data,
				    image->dx, image->dy,
				    dinfo->pitch, info->var.bits_per_pixel))
		return cfb_imageblit(info, image);
}

static int
intelfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
        struct intelfb_info *dinfo = GET_DINFO(info);

#if VERBOSE > 0
	DBG_MSG("intelfb_cursor\n");
#endif

	if (!dinfo->hwcursor)
		return -ENODEV;

	intelfbhw_cursor_hide(dinfo);

	/* If XFree killed the cursor - restore it */
	if (INREG(CURSOR_A_BASEADDR) != dinfo->cursor.offset << 12) {
		u32 fg, bg;

		DBG_MSG("the cursor was killed - restore it !!\n");
		DBG_MSG("size %d, %d   pos %d, %d\n",
			cursor->image.width, cursor->image.height,
			cursor->image.dx, cursor->image.dy);

		intelfbhw_cursor_init(dinfo);
		intelfbhw_cursor_reset(dinfo);
		intelfbhw_cursor_setpos(dinfo, cursor->image.dx,
					cursor->image.dy);

		if (dinfo->depth != 8) {
			fg =dinfo->pseudo_palette[cursor->image.fg_color];
			bg =dinfo->pseudo_palette[cursor->image.bg_color];
		} else {
			fg = cursor->image.fg_color;
			bg = cursor->image.bg_color;
		}
		intelfbhw_cursor_setcolor(dinfo, bg, fg);
		intelfbhw_cursor_load(dinfo, cursor->image.width,
				      cursor->image.height,
				      dinfo->cursor_src);

		if (cursor->enable)
			intelfbhw_cursor_show(dinfo);
		return 0;
	}

	if (cursor->set & FB_CUR_SETPOS) {
		u32 dx, dy;

		dx = cursor->image.dx - info->var.xoffset;
		dy = cursor->image.dy - info->var.yoffset;

		intelfbhw_cursor_setpos(dinfo, dx, dy);
	}

	if (cursor->set & FB_CUR_SETSIZE) {
		if (cursor->image.width > 64 || cursor->image.height > 64)
			return -ENXIO;

		intelfbhw_cursor_reset(dinfo);
	}

	if (cursor->set & FB_CUR_SETCMAP) {
		u32 fg, bg;

		if (dinfo->depth != 8) {
			fg = dinfo->pseudo_palette[cursor->image.fg_color];
			bg = dinfo->pseudo_palette[cursor->image.bg_color];
		} else {
			fg = cursor->image.fg_color;
			bg = cursor->image.bg_color;
		}

		intelfbhw_cursor_setcolor(dinfo, bg, fg);
	}

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		u32 s_pitch = (ROUND_UP_TO(cursor->image.width, 8) / 8);
		u32 size = s_pitch * cursor->image.height;
		u8 *dat = (u8 *) cursor->image.data;
		u8 *msk = (u8 *) cursor->mask;
		u8 src[64];
		u32 i;

		if (cursor->image.depth != 1)
			return -ENXIO;

		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				src[i] = dat[i] ^ msk[i];
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < size; i++)
				src[i] = dat[i] & msk[i];
			break;
		}

		/* save the bitmap to restore it when XFree will
		   make the cursor dirty */
		memcpy(dinfo->cursor_src, src, size);

		intelfbhw_cursor_load(dinfo, cursor->image.width,
				      cursor->image.height, src);
	}

	if (cursor->enable)
		intelfbhw_cursor_show(dinfo);

	return 0;
}

static int
intelfb_sync(struct fb_info *info)
{
        struct intelfb_info *dinfo = GET_DINFO(info);

#if VERBOSE > 0
	DBG_MSG("intelfb_sync\n");
#endif

	if (dinfo->ring_lockup)
		return 0;

	intelfbhw_do_sync(dinfo);
	return 0;
}


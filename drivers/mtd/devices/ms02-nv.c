/*
 *	Copyright (c) 2001 Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/kn02.h>
#include <asm/dec/kn03.h>
#include <asm/io.h>
#include <asm/paccess.h>

#include "ms02-nv.h"


static char version[] __initdata =
	"ms02-nv.c: v.1.0.0  13 Aug 2001  Maciej W. Rozycki.\n";

MODULE_AUTHOR("Maciej W. Rozycki <macro@linux-mips.org>");
MODULE_DESCRIPTION("DEC MS02-NV NVRAM module driver");
MODULE_LICENSE("GPL");


/*
 * Addresses we probe for an MS02-NV at.  Modules may be located
 * at any 8MiB boundary within a 0MiB up to 112MiB range or at any 32MiB
 * boundary within a 0MiB up to 448MiB range.  We don't support a module
 * at 0MiB, though.
 */
static ulong ms02nv_addrs[] __initdata = {
	0x07000000, 0x06800000, 0x06000000, 0x05800000, 0x05000000,
	0x04800000, 0x04000000, 0x03800000, 0x03000000, 0x02800000,
	0x02000000, 0x01800000, 0x01000000, 0x00800000
};

static const char ms02nv_name[] = "DEC MS02-NV NVRAM";
static const char ms02nv_res_diag_ram[] = "Diagnostic RAM";
static const char ms02nv_res_user_ram[] = "General-purpose RAM";
static const char ms02nv_res_csr[] = "Control and status register";

static struct mtd_info *root_ms02nv_mtd;


static int ms02nv_read(struct mtd_info *mtd, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	struct ms02nv_private *mp = mtd->priv;

	memcpy(buf, mp->uaddr + from, len);
	*retlen = len;
	return 0;
}

static int ms02nv_write(struct mtd_info *mtd, loff_t to,
			size_t len, size_t *retlen, const u_char *buf)
{
	struct ms02nv_private *mp = mtd->priv;

	memcpy(mp->uaddr + to, buf, len);
	*retlen = len;
	return 0;
}


static inline uint ms02nv_probe_one(ulong addr)
{
	ms02nv_uint *ms02nv_diagp;
	ms02nv_uint *ms02nv_magicp;
	uint ms02nv_diag;
	uint ms02nv_magic;
	size_t size;

	int err;

	/*
	 * The firmware writes MS02NV_ID at MS02NV_MAGIC and also
	 * a diagnostic status at MS02NV_DIAG.
	 */
	ms02nv_diagp = (ms02nv_uint *)(CKSEG1ADDR(addr + MS02NV_DIAG));
	ms02nv_magicp = (ms02nv_uint *)(CKSEG1ADDR(addr + MS02NV_MAGIC));
	err = get_dbe(ms02nv_magic, ms02nv_magicp);
	if (err)
		return 0;
	if (ms02nv_magic != MS02NV_ID)
		return 0;

	ms02nv_diag = *ms02nv_diagp;
	size = (ms02nv_diag & MS02NV_DIAG_SIZE_MASK) << MS02NV_DIAG_SIZE_SHIFT;
	if (size > MS02NV_CSR)
		size = MS02NV_CSR;

	return size;
}

static int __init ms02nv_init_one(ulong addr)
{
	struct mtd_info *mtd;
	struct ms02nv_private *mp;
	struct resource *mod_res;
	struct resource *diag_res;
	struct resource *user_res;
	struct resource *csr_res;
	ulong fixaddr;
	size_t size, fixsize;

	static int version_printed;

	int ret = -ENODEV;

	/* The module decodes 8MiB of address space. */
	mod_res = kzalloc(sizeof(*mod_res), GFP_KERNEL);
	if (!mod_res)
		return -ENOMEM;

	mod_res->name = ms02nv_name;
	mod_res->start = addr;
	mod_res->end = addr + MS02NV_SLOT_SIZE - 1;
	mod_res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, mod_res) < 0)
		goto err_out_mod_res;

	size = ms02nv_probe_one(addr);
	if (!size)
		goto err_out_mod_res_rel;

	if (!version_printed) {
		printk(KERN_INFO "%s", version);
		version_printed = 1;
	}

	ret = -ENOMEM;
	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		goto err_out_mod_res_rel;
	mp = kzalloc(sizeof(*mp), GFP_KERNEL);
	if (!mp)
		goto err_out_mtd;

	mtd->priv = mp;
	mp->resource.module = mod_res;

	/* Firmware's diagnostic NVRAM area. */
	diag_res = kzalloc(sizeof(*diag_res), GFP_KERNEL);
	if (!diag_res)
		goto err_out_mp;

	diag_res->name = ms02nv_res_diag_ram;
	diag_res->start = addr;
	diag_res->end = addr + MS02NV_RAM - 1;
	diag_res->flags = IORESOURCE_BUSY;
	request_resource(mod_res, diag_res);

	mp->resource.diag_ram = diag_res;

	/* User-available general-purpose NVRAM area. */
	user_res = kzalloc(sizeof(*user_res), GFP_KERNEL);
	if (!user_res)
		goto err_out_diag_res;

	user_res->name = ms02nv_res_user_ram;
	user_res->start = addr + MS02NV_RAM;
	user_res->end = addr + size - 1;
	user_res->flags = IORESOURCE_BUSY;
	request_resource(mod_res, user_res);

	mp->resource.user_ram = user_res;

	/* Control and status register. */
	csr_res = kzalloc(sizeof(*csr_res), GFP_KERNEL);
	if (!csr_res)
		goto err_out_user_res;

	csr_res->name = ms02nv_res_csr;
	csr_res->start = addr + MS02NV_CSR;
	csr_res->end = addr + MS02NV_CSR + 3;
	csr_res->flags = IORESOURCE_BUSY;
	request_resource(mod_res, csr_res);

	mp->resource.csr = csr_res;

	mp->addr = phys_to_virt(addr);
	mp->size = size;

	/*
	 * Hide the firmware's diagnostic area.  It may get destroyed
	 * upon a reboot.  Take paging into account for mapping support.
	 */
	fixaddr = (addr + MS02NV_RAM + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	fixsize = (size - (fixaddr - addr)) & ~(PAGE_SIZE - 1);
	mp->uaddr = phys_to_virt(fixaddr);

	mtd->type = MTD_RAM;
	mtd->flags = MTD_CAP_RAM;
	mtd->size = fixsize;
	mtd->name = (char *)ms02nv_name;
	mtd->owner = THIS_MODULE;
	mtd->_read = ms02nv_read;
	mtd->_write = ms02nv_write;
	mtd->writesize = 1;

	ret = -EIO;
	if (mtd_device_register(mtd, NULL, 0)) {
		printk(KERN_ERR
			"ms02-nv: Unable to register MTD device, aborting!\n");
		goto err_out_csr_res;
	}

	printk(KERN_INFO "mtd%d: %s at 0x%08lx, size %zuMiB.\n",
		mtd->index, ms02nv_name, addr, size >> 20);

	mp->next = root_ms02nv_mtd;
	root_ms02nv_mtd = mtd;

	return 0;


err_out_csr_res:
	release_resource(csr_res);
	kfree(csr_res);
err_out_user_res:
	release_resource(user_res);
	kfree(user_res);
err_out_diag_res:
	release_resource(diag_res);
	kfree(diag_res);
err_out_mp:
	kfree(mp);
err_out_mtd:
	kfree(mtd);
err_out_mod_res_rel:
	release_resource(mod_res);
err_out_mod_res:
	kfree(mod_res);
	return ret;
}

static void __exit ms02nv_remove_one(void)
{
	struct mtd_info *mtd = root_ms02nv_mtd;
	struct ms02nv_private *mp = mtd->priv;

	root_ms02nv_mtd = mp->next;

	mtd_device_unregister(mtd);

	release_resource(mp->resource.csr);
	kfree(mp->resource.csr);
	release_resource(mp->resource.user_ram);
	kfree(mp->resource.user_ram);
	release_resource(mp->resource.diag_ram);
	kfree(mp->resource.diag_ram);
	release_resource(mp->resource.module);
	kfree(mp->resource.module);
	kfree(mp);
	kfree(mtd);
}


static int __init ms02nv_init(void)
{
	volatile u32 *csr;
	uint stride = 0;
	int count = 0;
	int i;

	switch (mips_machtype) {
	case MACH_DS5000_200:
		csr = (volatile u32 *)CKSEG1ADDR(KN02_SLOT_BASE + KN02_CSR);
		if (*csr & KN02_CSR_BNK32M)
			stride = 2;
		break;
	case MACH_DS5000_2X0:
	case MACH_DS5900:
		csr = (volatile u32 *)CKSEG1ADDR(KN03_SLOT_BASE + IOASIC_MCR);
		if (*csr & KN03_MCR_BNK32M)
			stride = 2;
		break;
	default:
		return -ENODEV;
		break;
	}

	for (i = 0; i < ARRAY_SIZE(ms02nv_addrs); i++)
		if (!ms02nv_init_one(ms02nv_addrs[i] << stride))
			count++;

	return (count > 0) ? 0 : -ENODEV;
}

static void __exit ms02nv_cleanup(void)
{
	while (root_ms02nv_mtd)
		ms02nv_remove_one();
}


module_init(ms02nv_init);
module_exit(ms02nv_cleanup);

/*
 * Common Flash Interface support:
 *   Generic utility functions not dependant on command set
 *
 * Copyright (C) 2002 Red Hat
 * Copyright (C) 2003 STMicroelectronics Limited
 *
 * This code is covered by the GPL.
 *
 * $Id: cfi_util.c,v 1.8 2004/12/14 19:55:56 nico Exp $
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/xip.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/compatmac.h>

struct cfi_extquery *
__xipram cfi_read_pri(struct map_info *map, __u16 adr, __u16 size, const char* name)
{
	struct cfi_private *cfi = map->fldrv_priv;
	__u32 base = 0; // cfi->chips[0].start;
	int ofs_factor = cfi->interleave * cfi->device_type;
	int i;
	struct cfi_extquery *extp = NULL;

	printk(" %s Extended Query Table at 0x%4.4X\n", name, adr);
	if (!adr)
		goto out;

	extp = kmalloc(size, GFP_KERNEL);
	if (!extp) {
		printk(KERN_ERR "Failed to allocate memory\n");
		goto out;
	}

#ifdef CONFIG_MTD_XIP
	local_irq_disable();
#endif

	/* Switch it into Query Mode */
	cfi_send_gen_cmd(0x98, 0x55, base, map, cfi, cfi->device_type, NULL);

	/* Read in the Extended Query Table */
	for (i=0; i<size; i++) {
		((unsigned char *)extp)[i] = 
			cfi_read_query(map, base+((adr+i)*ofs_factor));
	}

	/* Make sure it returns to read mode */
	cfi_send_gen_cmd(0xf0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xff, 0, base, map, cfi, cfi->device_type, NULL);

#ifdef CONFIG_MTD_XIP
	(void) map_read(map, base);
	asm volatile (".rep 8; nop; .endr");
	local_irq_enable();
#endif

	if (extp->MajorVersion != '1' || 
	    (extp->MinorVersion < '0' || extp->MinorVersion > '3')) {
		printk(KERN_WARNING "  Unknown %s Extended Query "
		       "version %c.%c.\n",  name, extp->MajorVersion,
		       extp->MinorVersion);
		kfree(extp);
		extp = NULL;
	}

 out:	return extp;
}

EXPORT_SYMBOL(cfi_read_pri);

void cfi_fixup(struct mtd_info *mtd, struct cfi_fixup *fixups)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	struct cfi_fixup *f;

	for (f=fixups; f->fixup; f++) {
		if (((f->mfr == CFI_MFR_ANY) || (f->mfr == cfi->mfr)) &&
		    ((f->id  == CFI_ID_ANY)  || (f->id  == cfi->id))) {
			f->fixup(mtd, f->param);
		}
	}
}

EXPORT_SYMBOL(cfi_fixup);

int cfi_varsize_frob(struct mtd_info *mtd, varsize_frob_t frob,
				     loff_t ofs, size_t len, void *thunk)
{
	struct map_info *map = mtd->priv;
	struct cfi_private *cfi = map->fldrv_priv;
	unsigned long adr;
	int chipnum, ret = 0;
	int i, first;
	struct mtd_erase_region_info *regions = mtd->eraseregions;

	if (ofs > mtd->size)
		return -EINVAL;

	if ((len + ofs) > mtd->size)
		return -EINVAL;

	/* Check that both start and end of the requested erase are
	 * aligned with the erasesize at the appropriate addresses.
	 */

	i = 0;

	/* Skip all erase regions which are ended before the start of 
	   the requested erase. Actually, to save on the calculations,
	   we skip to the first erase region which starts after the
	   start of the requested erase, and then go back one.
	*/
	
	while (i < mtd->numeraseregions && ofs >= regions[i].offset)
	       i++;
	i--;

	/* OK, now i is pointing at the erase region in which this 
	   erase request starts. Check the start of the requested
	   erase range is aligned with the erase size which is in
	   effect here.
	*/

	if (ofs & (regions[i].erasesize-1))
		return -EINVAL;

	/* Remember the erase region we start on */
	first = i;

	/* Next, check that the end of the requested erase is aligned
	 * with the erase region at that address.
	 */

	while (i<mtd->numeraseregions && (ofs + len) >= regions[i].offset)
		i++;

	/* As before, drop back one to point at the region in which
	   the address actually falls
	*/
	i--;
	
	if ((ofs + len) & (regions[i].erasesize-1))
		return -EINVAL;

	chipnum = ofs >> cfi->chipshift;
	adr = ofs - (chipnum << cfi->chipshift);

	i=first;

	while(len) {
		int size = regions[i].erasesize;

		ret = (*frob)(map, &cfi->chips[chipnum], adr, size, thunk);
		
		if (ret)
			return ret;

		adr += size;
		ofs += size;
		len -= size;

		if (ofs == regions[i].offset + size * regions[i].numblocks)
			i++;

		if (adr >> cfi->chipshift) {
			adr = 0;
			chipnum++;
			
			if (chipnum >= cfi->numchips)
			break;
		}
	}

	return 0;
}

EXPORT_SYMBOL(cfi_varsize_frob);

MODULE_LICENSE("GPL");

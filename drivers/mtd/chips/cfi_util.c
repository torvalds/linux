/*
 * Common Flash Interface support:
 *   Generic utility functions not dependent on command set
 *
 * Copyright (C) 2002 Red Hat
 * Copyright (C) 2003 STMicroelectronics Limited
 *
 * This code is covered by the GPL.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
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

int __xipram cfi_qry_present(struct map_info *map, __u32 base,
			     struct cfi_private *cfi)
{
	int osf = cfi->interleave * cfi->device_type;	/* scale factor */
	map_word val[3];
	map_word qry[3];

	qry[0] = cfi_build_cmd('Q', map, cfi);
	qry[1] = cfi_build_cmd('R', map, cfi);
	qry[2] = cfi_build_cmd('Y', map, cfi);

	val[0] = map_read(map, base + osf*0x10);
	val[1] = map_read(map, base + osf*0x11);
	val[2] = map_read(map, base + osf*0x12);

	if (!map_word_equal(map, qry[0], val[0]))
		return 0;

	if (!map_word_equal(map, qry[1], val[1]))
		return 0;

	if (!map_word_equal(map, qry[2], val[2]))
		return 0;

	return 1; 	/* "QRY" found */
}
EXPORT_SYMBOL_GPL(cfi_qry_present);

int __xipram cfi_qry_mode_on(uint32_t base, struct map_info *map,
			     struct cfi_private *cfi)
{
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x55, base, map, cfi, cfi->device_type, NULL);
	if (cfi_qry_present(map, base, cfi))
		return 1;
	/* QRY not found probably we deal with some odd CFI chips */
	/* Some revisions of some old Intel chips? */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xFF, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x55, base, map, cfi, cfi->device_type, NULL);
	if (cfi_qry_present(map, base, cfi))
		return 1;
	/* ST M29DW chips */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x555, base, map, cfi, cfi->device_type, NULL);
	if (cfi_qry_present(map, base, cfi))
		return 1;
	/* some old SST chips, e.g. 39VF160x/39VF320x */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xAA, 0x5555, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x55, 0x2AAA, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x5555, base, map, cfi, cfi->device_type, NULL);
	if (cfi_qry_present(map, base, cfi))
		return 1;
	/* SST 39VF640xB */
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xAA, 0x555, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x55, 0x2AA, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x555, base, map, cfi, cfi->device_type, NULL);
	if (cfi_qry_present(map, base, cfi))
		return 1;
	/* QRY not found */
	return 0;
}
EXPORT_SYMBOL_GPL(cfi_qry_mode_on);

void __xipram cfi_qry_mode_off(uint32_t base, struct map_info *map,
			       struct cfi_private *cfi)
{
	cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xFF, 0, base, map, cfi, cfi->device_type, NULL);
	/* M29W128G flashes require an additional reset command
	   when exit qry mode */
	if ((cfi->mfr == CFI_MFR_ST) && (cfi->id == 0x227E || cfi->id == 0x7E))
		cfi_send_gen_cmd(0xF0, 0, base, map, cfi, cfi->device_type, NULL);
}
EXPORT_SYMBOL_GPL(cfi_qry_mode_off);

struct cfi_extquery *
__xipram cfi_read_pri(struct map_info *map, __u16 adr, __u16 size, const char* name)
{
	struct cfi_private *cfi = map->fldrv_priv;
	__u32 base = 0; // cfi->chips[0].start;
	int ofs_factor = cfi->interleave * cfi->device_type;
	int i;
	struct cfi_extquery *extp = NULL;

	if (!adr)
		goto out;

	printk(KERN_INFO "%s Extended Query Table at 0x%4.4X\n", name, adr);

	extp = kmalloc(size, GFP_KERNEL);
	if (!extp) {
		printk(KERN_ERR "Failed to allocate memory\n");
		goto out;
	}

#ifdef CONFIG_MTD_XIP
	local_irq_disable();
#endif

	/* Switch it into Query Mode */
	cfi_qry_mode_on(base, map, cfi);
	/* Read in the Extended Query Table */
	for (i=0; i<size; i++) {
		((unsigned char *)extp)[i] =
			cfi_read_query(map, base+((adr+i)*ofs_factor));
	}

	/* Make sure it returns to read mode */
	cfi_qry_mode_off(base, map, cfi);

#ifdef CONFIG_MTD_XIP
	(void) map_read(map, base);
	xip_iprefetch();
	local_irq_enable();
#endif

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
			f->fixup(mtd);
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

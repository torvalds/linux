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

void cfi_udelay(int us)
{
	if (us >= 1000) {
		msleep((us+999)/1000);
	} else {
		udelay(us);
		cond_resched();
	}
}
EXPORT_SYMBOL(cfi_udelay);

/*
 * Returns the command address according to the given geometry.
 */
uint32_t cfi_build_cmd_addr(uint32_t cmd_ofs,
				struct map_info *map, struct cfi_private *cfi)
{
	unsigned bankwidth = map_bankwidth(map);
	unsigned interleave = cfi_interleave(cfi);
	unsigned type = cfi->device_type;
	uint32_t addr;

	addr = (cmd_ofs * type) * interleave;

	/* Modify the unlock address if we are in compatibility mode.
	 * For 16bit devices on 8 bit busses
	 * and 32bit devices on 16 bit busses
	 * set the low bit of the alternating bit sequence of the address.
	 */
	if (((type * interleave) > bankwidth) && ((cmd_ofs & 0xff) == 0xaa))
		addr |= (type >> 1)*interleave;

	return  addr;
}
EXPORT_SYMBOL(cfi_build_cmd_addr);

/*
 * Transforms the CFI command for the given geometry (bus width & interleave).
 * It looks too long to be inline, but in the common case it should almost all
 * get optimised away.
 */
map_word cfi_build_cmd(u_long cmd, struct map_info *map, struct cfi_private *cfi)
{
	map_word val = { {0} };
	int wordwidth, words_per_bus, chip_mode, chips_per_word;
	unsigned long onecmd;
	int i;

	/* We do it this way to give the compiler a fighting chance
	   of optimising away all the crap for 'bankwidth' larger than
	   an unsigned long, in the common case where that support is
	   disabled */
	if (map_bankwidth_is_large(map)) {
		wordwidth = sizeof(unsigned long);
		words_per_bus = (map_bankwidth(map)) / wordwidth; // i.e. normally 1
	} else {
		wordwidth = map_bankwidth(map);
		words_per_bus = 1;
	}

	chip_mode = map_bankwidth(map) / cfi_interleave(cfi);
	chips_per_word = wordwidth * cfi_interleave(cfi) / map_bankwidth(map);

	/* First, determine what the bit-pattern should be for a single
	   device, according to chip mode and endianness... */
	switch (chip_mode) {
	default: BUG();
	case 1:
		onecmd = cmd;
		break;
	case 2:
		onecmd = cpu_to_cfi16(map, cmd);
		break;
	case 4:
		onecmd = cpu_to_cfi32(map, cmd);
		break;
	}

	/* Now replicate it across the size of an unsigned long, or
	   just to the bus width as appropriate */
	switch (chips_per_word) {
	default: BUG();
#if BITS_PER_LONG >= 64
	case 8:
		onecmd |= (onecmd << (chip_mode * 32));
#endif
		/* fall through */
	case 4:
		onecmd |= (onecmd << (chip_mode * 16));
		/* fall through */
	case 2:
		onecmd |= (onecmd << (chip_mode * 8));
		/* fall through */
	case 1:
		;
	}

	/* And finally, for the multi-word case, replicate it
	   in all words in the structure */
	for (i=0; i < words_per_bus; i++) {
		val.x[i] = onecmd;
	}

	return val;
}
EXPORT_SYMBOL(cfi_build_cmd);

unsigned long cfi_merge_status(map_word val, struct map_info *map,
					   struct cfi_private *cfi)
{
	int wordwidth, words_per_bus, chip_mode, chips_per_word;
	unsigned long onestat, res = 0;
	int i;

	/* We do it this way to give the compiler a fighting chance
	   of optimising away all the crap for 'bankwidth' larger than
	   an unsigned long, in the common case where that support is
	   disabled */
	if (map_bankwidth_is_large(map)) {
		wordwidth = sizeof(unsigned long);
		words_per_bus = (map_bankwidth(map)) / wordwidth; // i.e. normally 1
	} else {
		wordwidth = map_bankwidth(map);
		words_per_bus = 1;
	}

	chip_mode = map_bankwidth(map) / cfi_interleave(cfi);
	chips_per_word = wordwidth * cfi_interleave(cfi) / map_bankwidth(map);

	onestat = val.x[0];
	/* Or all status words together */
	for (i=1; i < words_per_bus; i++) {
		onestat |= val.x[i];
	}

	res = onestat;
	switch(chips_per_word) {
	default: BUG();
#if BITS_PER_LONG >= 64
	case 8:
		res |= (onestat >> (chip_mode * 32));
#endif
		/* fall through */
	case 4:
		res |= (onestat >> (chip_mode * 16));
		/* fall through */
	case 2:
		res |= (onestat >> (chip_mode * 8));
		/* fall through */
	case 1:
		;
	}

	/* Last, determine what the bit-pattern should be for a single
	   device, according to chip mode and endianness... */
	switch (chip_mode) {
	case 1:
		break;
	case 2:
		res = cfi16_to_cpu(map, res);
		break;
	case 4:
		res = cfi32_to_cpu(map, res);
		break;
	default: BUG();
	}
	return res;
}
EXPORT_SYMBOL(cfi_merge_status);

/*
 * Sends a CFI command to a bank of flash for the given geometry.
 *
 * Returns the offset in flash where the command was written.
 * If prev_val is non-null, it will be set to the value at the command address,
 * before the command was written.
 */
uint32_t cfi_send_gen_cmd(u_char cmd, uint32_t cmd_addr, uint32_t base,
				struct map_info *map, struct cfi_private *cfi,
				int type, map_word *prev_val)
{
	map_word val;
	uint32_t addr = base + cfi_build_cmd_addr(cmd_addr, map, cfi);
	val = cfi_build_cmd(cmd, map, cfi);

	if (prev_val)
		*prev_val = map_read(map, addr);

	map_write(map, val, addr);

	return addr - base;
}
EXPORT_SYMBOL(cfi_send_gen_cmd);

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
	if (!extp)
		goto out;

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

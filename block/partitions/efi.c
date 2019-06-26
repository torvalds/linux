// SPDX-License-Identifier: GPL-2.0-or-later
/************************************************************
 * EFI GUID Partition Table handling
 *
 * http://www.uefi.org/specs/
 * http://www.intel.com/technology/efi/
 *
 * efi.[ch] by Matt Domsch <Matt_Domsch@dell.com>
 *   Copyright 2000,2001,2002,2004 Dell Inc.
 *
 * TODO:
 *
 * Changelog:
 * Mon August 5th, 2013 Davidlohr Bueso <davidlohr@hp.com>
 * - detect hybrid MBRs, tighter pMBR checking & cleanups.
 *
 * Mon Nov 09 2004 Matt Domsch <Matt_Domsch@dell.com>
 * - test for valid PMBR and valid PGPT before ever reading
 *   AGPT, allow override with 'gpt' kernel command line option.
 * - check for first/last_usable_lba outside of size of disk
 *
 * Tue  Mar 26 2002 Matt Domsch <Matt_Domsch@dell.com>
 * - Ported to 2.5.7-pre1 and 2.5.7-dj2
 * - Applied patch to avoid fault in alternate header handling
 * - cleaned up find_valid_gpt
 * - On-disk structure and copy in memory is *always* LE now - 
 *   swab fields as needed
 * - remove print_gpt_header()
 * - only use first max_p partition entries, to keep the kernel minor number
 *   and partition numbers tied.
 *
 * Mon  Feb 04 2002 Matt Domsch <Matt_Domsch@dell.com>
 * - Removed __PRIPTR_PREFIX - not being used
 *
 * Mon  Jan 14 2002 Matt Domsch <Matt_Domsch@dell.com>
 * - Ported to 2.5.2-pre11 + library crc32 patch Linus applied
 *
 * Thu Dec 6 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Added compare_gpts().
 * - moved le_efi_guid_to_cpus() back into this file.  GPT is the only
 *   thing that keeps EFI GUIDs on disk.
 * - Changed gpt structure names and members to be simpler and more Linux-like.
 * 
 * Wed Oct 17 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Removed CONFIG_DEVFS_VOLUMES_UUID code entirely per Martin Wilck
 *
 * Wed Oct 10 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Changed function comments to DocBook style per Andreas Dilger suggestion.
 *
 * Mon Oct 08 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Change read_lba() to use the page cache per Al Viro's work.
 * - print u64s properly on all architectures
 * - fixed debug_printk(), now Dprintk()
 *
 * Mon Oct 01 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Style cleanups
 * - made most functions static
 * - Endianness addition
 * - remove test for second alternate header, as it's not per spec,
 *   and is unnecessary.  There's now a method to read/write the last
 *   sector of an odd-sized disk from user space.  No tools have ever
 *   been released which used this code, so it's effectively dead.
 * - Per Asit Mallick of Intel, added a test for a valid PMBR.
 * - Added kernel command line option 'gpt' to override valid PMBR test.
 *
 * Wed Jun  6 2001 Martin Wilck <Martin.Wilck@Fujitsu-Siemens.com>
 * - added devfs volume UUID support (/dev/volumes/uuids) for
 *   mounting file systems by the partition GUID. 
 *
 * Tue Dec  5 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Moved crc32() to linux/lib, added efi_crc32().
 *
 * Thu Nov 30 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Replaced Intel's CRC32 function with an equivalent
 *   non-license-restricted version.
 *
 * Wed Oct 25 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Fixed the last_lba() call to return the proper last block
 *
 * Thu Oct 12 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Thanks to Andries Brouwer for his debugging assistance.
 * - Code works, detects all the partitions.
 *
 ************************************************************/
#include <linux/kernel.h>
#include <linux/crc32.h>
#include <linux/ctype.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include "check.h"
#include "efi.h"

/* This allows a kernel command line option 'gpt' to override
 * the test for invalid PMBR.  Not __initdata because reloading
 * the partition tables happens after init too.
 */
static int force_gpt;
static int __init
force_gpt_fn(char *str)
{
	force_gpt = 1;
	return 1;
}
__setup("gpt", force_gpt_fn);


/**
 * efi_crc32() - EFI version of crc32 function
 * @buf: buffer to calculate crc32 of
 * @len: length of buf
 *
 * Description: Returns EFI-style CRC32 value for @buf
 * 
 * This function uses the little endian Ethernet polynomial
 * but seeds the function with ~0, and xor's with ~0 at the end.
 * Note, the EFI Specification, v1.02, has a reference to
 * Dr. Dobbs Journal, May 1994 (actually it's in May 1992).
 */
static inline u32
efi_crc32(const void *buf, unsigned long len)
{
	return (crc32(~0L, buf, len) ^ ~0L);
}

/**
 * last_lba(): return number of last logical block of device
 * @bdev: block device
 * 
 * Description: Returns last LBA value on success, 0 on error.
 * This is stored (by sd and ide-geometry) in
 *  the part[0] entry for this disk, and is the number of
 *  physical sectors available on the disk.
 */
static u64 last_lba(struct block_device *bdev)
{
	if (!bdev || !bdev->bd_inode)
		return 0;
	return div_u64(bdev->bd_inode->i_size,
		       bdev_logical_block_size(bdev)) - 1ULL;
}

static inline int pmbr_part_valid(gpt_mbr_record *part)
{
	if (part->os_type != EFI_PMBR_OSTYPE_EFI_GPT)
		goto invalid;

	/* set to 0x00000001 (i.e., the LBA of the GPT Partition Header) */
	if (le32_to_cpu(part->starting_lba) != GPT_PRIMARY_PARTITION_TABLE_LBA)
		goto invalid;

	return GPT_MBR_PROTECTIVE;
invalid:
	return 0;
}

/**
 * is_pmbr_valid(): test Protective MBR for validity
 * @mbr: pointer to a legacy mbr structure
 * @total_sectors: amount of sectors in the device
 *
 * Description: Checks for a valid protective or hybrid
 * master boot record (MBR). The validity of a pMBR depends
 * on all of the following properties:
 *  1) MSDOS signature is in the last two bytes of the MBR
 *  2) One partition of type 0xEE is found
 *
 * In addition, a hybrid MBR will have up to three additional
 * primary partitions, which point to the same space that's
 * marked out by up to three GPT partitions.
 *
 * Returns 0 upon invalid MBR, or GPT_MBR_PROTECTIVE or
 * GPT_MBR_HYBRID depending on the device layout.
 */
static int is_pmbr_valid(legacy_mbr *mbr, sector_t total_sectors)
{
	uint32_t sz = 0;
	int i, part = 0, ret = 0; /* invalid by default */

	if (!mbr || le16_to_cpu(mbr->signature) != MSDOS_MBR_SIGNATURE)
		goto done;

	for (i = 0; i < 4; i++) {
		ret = pmbr_part_valid(&mbr->partition_record[i]);
		if (ret == GPT_MBR_PROTECTIVE) {
			part = i;
			/*
			 * Ok, we at least know that there's a protective MBR,
			 * now check if there are other partition types for
			 * hybrid MBR.
			 */
			goto check_hybrid;
		}
	}

	if (ret != GPT_MBR_PROTECTIVE)
		goto done;
check_hybrid:
	for (i = 0; i < 4; i++)
		if ((mbr->partition_record[i].os_type !=
			EFI_PMBR_OSTYPE_EFI_GPT) &&
		    (mbr->partition_record[i].os_type != 0x00))
			ret = GPT_MBR_HYBRID;

	/*
	 * Protective MBRs take up the lesser of the whole disk
	 * or 2 TiB (32bit LBA), ignoring the rest of the disk.
	 * Some partitioning programs, nonetheless, choose to set
	 * the size to the maximum 32-bit limitation, disregarding
	 * the disk size.
	 *
	 * Hybrid MBRs do not necessarily comply with this.
	 *
	 * Consider a bad value here to be a warning to support dd'ing
	 * an image from a smaller disk to a larger disk.
	 */
	if (ret == GPT_MBR_PROTECTIVE) {
		sz = le32_to_cpu(mbr->partition_record[part].size_in_lba);
		if (sz != (uint32_t) total_sectors - 1 && sz != 0xFFFFFFFF)
			pr_debug("GPT: mbr size in lba (%u) different than whole disk (%u).\n",
				 sz, min_t(uint32_t,
					   total_sectors - 1, 0xFFFFFFFF));
	}
done:
	return ret;
}

/**
 * read_lba(): Read bytes from disk, starting at given LBA
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * @buffer: destination buffer
 * @count: bytes to read
 *
 * Description: Reads @count bytes from @state->bdev into @buffer.
 * Returns number of bytes read on success, 0 on error.
 */
static size_t read_lba(struct parsed_partitions *state,
		       u64 lba, u8 *buffer, size_t count)
{
	size_t totalreadcount = 0;
	struct block_device *bdev = state->bdev;
	sector_t n = lba * (bdev_logical_block_size(bdev) / 512);

	if (!buffer || lba > last_lba(bdev))
                return 0;

	while (count) {
		int copied = 512;
		Sector sect;
		unsigned char *data = read_part_sector(state, n++, &sect);
		if (!data)
			break;
		if (copied > count)
			copied = count;
		memcpy(buffer, data, copied);
		put_dev_sector(sect);
		buffer += copied;
		totalreadcount +=copied;
		count -= copied;
	}
	return totalreadcount;
}

/**
 * alloc_read_gpt_entries(): reads partition entries from disk
 * @state: disk parsed partitions
 * @gpt: GPT header
 * 
 * Description: Returns ptes on success,  NULL on error.
 * Allocates space for PTEs based on information found in @gpt.
 * Notes: remember to free pte when you're done!
 */
static gpt_entry *alloc_read_gpt_entries(struct parsed_partitions *state,
					 gpt_header *gpt)
{
	size_t count;
	gpt_entry *pte;

	if (!gpt)
		return NULL;

	count = (size_t)le32_to_cpu(gpt->num_partition_entries) *
                le32_to_cpu(gpt->sizeof_partition_entry);
	if (!count)
		return NULL;
	pte = kmalloc(count, GFP_KERNEL);
	if (!pte)
		return NULL;

	if (read_lba(state, le64_to_cpu(gpt->partition_entry_lba),
			(u8 *) pte, count) < count) {
		kfree(pte);
                pte=NULL;
		return NULL;
	}
	return pte;
}

/**
 * alloc_read_gpt_header(): Allocates GPT header, reads into it from disk
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * 
 * Description: returns GPT header on success, NULL on error.   Allocates
 * and fills a GPT header starting at @ from @state->bdev.
 * Note: remember to free gpt when finished with it.
 */
static gpt_header *alloc_read_gpt_header(struct parsed_partitions *state,
					 u64 lba)
{
	gpt_header *gpt;
	unsigned ssz = bdev_logical_block_size(state->bdev);

	gpt = kmalloc(ssz, GFP_KERNEL);
	if (!gpt)
		return NULL;

	if (read_lba(state, lba, (u8 *) gpt, ssz) < ssz) {
		kfree(gpt);
                gpt=NULL;
		return NULL;
	}

	return gpt;
}

/**
 * is_gpt_valid() - tests one GPT header and PTEs for validity
 * @state: disk parsed partitions
 * @lba: logical block address of the GPT header to test
 * @gpt: GPT header ptr, filled on return.
 * @ptes: PTEs ptr, filled on return.
 *
 * Description: returns 1 if valid,  0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 */
static int is_gpt_valid(struct parsed_partitions *state, u64 lba,
			gpt_header **gpt, gpt_entry **ptes)
{
	u32 crc, origcrc;
	u64 lastlba, pt_size;

	if (!ptes)
		return 0;
	if (!(*gpt = alloc_read_gpt_header(state, lba)))
		return 0;

	/* Check the GUID Partition Table signature */
	if (le64_to_cpu((*gpt)->signature) != GPT_HEADER_SIGNATURE) {
		pr_debug("GUID Partition Table Header signature is wrong:"
			 "%lld != %lld\n",
			 (unsigned long long)le64_to_cpu((*gpt)->signature),
			 (unsigned long long)GPT_HEADER_SIGNATURE);
		goto fail;
	}

	/* Check the GUID Partition Table header size is too big */
	if (le32_to_cpu((*gpt)->header_size) >
			bdev_logical_block_size(state->bdev)) {
		pr_debug("GUID Partition Table Header size is too large: %u > %u\n",
			le32_to_cpu((*gpt)->header_size),
			bdev_logical_block_size(state->bdev));
		goto fail;
	}

	/* Check the GUID Partition Table header size is too small */
	if (le32_to_cpu((*gpt)->header_size) < sizeof(gpt_header)) {
		pr_debug("GUID Partition Table Header size is too small: %u < %zu\n",
			le32_to_cpu((*gpt)->header_size),
			sizeof(gpt_header));
		goto fail;
	}

	/* Check the GUID Partition Table CRC */
	origcrc = le32_to_cpu((*gpt)->header_crc32);
	(*gpt)->header_crc32 = 0;
	crc = efi_crc32((const unsigned char *) (*gpt), le32_to_cpu((*gpt)->header_size));

	if (crc != origcrc) {
		pr_debug("GUID Partition Table Header CRC is wrong: %x != %x\n",
			 crc, origcrc);
		goto fail;
	}
	(*gpt)->header_crc32 = cpu_to_le32(origcrc);

	/* Check that the my_lba entry points to the LBA that contains
	 * the GUID Partition Table */
	if (le64_to_cpu((*gpt)->my_lba) != lba) {
		pr_debug("GPT my_lba incorrect: %lld != %lld\n",
			 (unsigned long long)le64_to_cpu((*gpt)->my_lba),
			 (unsigned long long)lba);
		goto fail;
	}

	/* Check the first_usable_lba and last_usable_lba are
	 * within the disk.
	 */
	lastlba = last_lba(state->bdev);
	if (le64_to_cpu((*gpt)->first_usable_lba) > lastlba) {
		pr_debug("GPT: first_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)le64_to_cpu((*gpt)->first_usable_lba),
			 (unsigned long long)lastlba);
		goto fail;
	}
	if (le64_to_cpu((*gpt)->last_usable_lba) > lastlba) {
		pr_debug("GPT: last_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)le64_to_cpu((*gpt)->last_usable_lba),
			 (unsigned long long)lastlba);
		goto fail;
	}
	if (le64_to_cpu((*gpt)->last_usable_lba) < le64_to_cpu((*gpt)->first_usable_lba)) {
		pr_debug("GPT: last_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)le64_to_cpu((*gpt)->last_usable_lba),
			 (unsigned long long)le64_to_cpu((*gpt)->first_usable_lba));
		goto fail;
	}
	/* Check that sizeof_partition_entry has the correct value */
	if (le32_to_cpu((*gpt)->sizeof_partition_entry) != sizeof(gpt_entry)) {
		pr_debug("GUID Partition Entry Size check failed.\n");
		goto fail;
	}

	/* Sanity check partition table size */
	pt_size = (u64)le32_to_cpu((*gpt)->num_partition_entries) *
		le32_to_cpu((*gpt)->sizeof_partition_entry);
	if (pt_size > KMALLOC_MAX_SIZE) {
		pr_debug("GUID Partition Table is too large: %llu > %lu bytes\n",
			 (unsigned long long)pt_size, KMALLOC_MAX_SIZE);
		goto fail;
	}

	if (!(*ptes = alloc_read_gpt_entries(state, *gpt)))
		goto fail;

	/* Check the GUID Partition Entry Array CRC */
	crc = efi_crc32((const unsigned char *) (*ptes), pt_size);

	if (crc != le32_to_cpu((*gpt)->partition_entry_array_crc32)) {
		pr_debug("GUID Partition Entry Array CRC check failed.\n");
		goto fail_ptes;
	}

	/* We're done, all's well */
	return 1;

 fail_ptes:
	kfree(*ptes);
	*ptes = NULL;
 fail:
	kfree(*gpt);
	*gpt = NULL;
	return 0;
}

/**
 * is_pte_valid() - tests one PTE for validity
 * @pte:pte to check
 * @lastlba: last lba of the disk
 *
 * Description: returns 1 if valid,  0 on error.
 */
static inline int
is_pte_valid(const gpt_entry *pte, const u64 lastlba)
{
	if ((!efi_guidcmp(pte->partition_type_guid, NULL_GUID)) ||
	    le64_to_cpu(pte->starting_lba) > lastlba         ||
	    le64_to_cpu(pte->ending_lba)   > lastlba)
		return 0;
	return 1;
}

/**
 * compare_gpts() - Search disk for valid GPT headers and PTEs
 * @pgpt: primary GPT header
 * @agpt: alternate GPT header
 * @lastlba: last LBA number
 *
 * Description: Returns nothing.  Sanity checks pgpt and agpt fields
 * and prints warnings on discrepancies.
 * 
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, u64 lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (le64_to_cpu(pgpt->my_lba) != le64_to_cpu(agpt->alternate_lba)) {
		pr_warn("GPT:Primary header LBA != Alt. header alternate_lba\n");
		pr_warn("GPT:%lld != %lld\n",
		       (unsigned long long)le64_to_cpu(pgpt->my_lba),
                       (unsigned long long)le64_to_cpu(agpt->alternate_lba));
		error_found++;
	}
	if (le64_to_cpu(pgpt->alternate_lba) != le64_to_cpu(agpt->my_lba)) {
		pr_warn("GPT:Primary header alternate_lba != Alt. header my_lba\n");
		pr_warn("GPT:%lld != %lld\n",
		       (unsigned long long)le64_to_cpu(pgpt->alternate_lba),
                       (unsigned long long)le64_to_cpu(agpt->my_lba));
		error_found++;
	}
	if (le64_to_cpu(pgpt->first_usable_lba) !=
            le64_to_cpu(agpt->first_usable_lba)) {
		pr_warn("GPT:first_usable_lbas don't match.\n");
		pr_warn("GPT:%lld != %lld\n",
		       (unsigned long long)le64_to_cpu(pgpt->first_usable_lba),
                       (unsigned long long)le64_to_cpu(agpt->first_usable_lba));
		error_found++;
	}
	if (le64_to_cpu(pgpt->last_usable_lba) !=
            le64_to_cpu(agpt->last_usable_lba)) {
		pr_warn("GPT:last_usable_lbas don't match.\n");
		pr_warn("GPT:%lld != %lld\n",
		       (unsigned long long)le64_to_cpu(pgpt->last_usable_lba),
                       (unsigned long long)le64_to_cpu(agpt->last_usable_lba));
		error_found++;
	}
	if (efi_guidcmp(pgpt->disk_guid, agpt->disk_guid)) {
		pr_warn("GPT:disk_guids don't match.\n");
		error_found++;
	}
	if (le32_to_cpu(pgpt->num_partition_entries) !=
            le32_to_cpu(agpt->num_partition_entries)) {
		pr_warn("GPT:num_partition_entries don't match: "
		       "0x%x != 0x%x\n",
		       le32_to_cpu(pgpt->num_partition_entries),
		       le32_to_cpu(agpt->num_partition_entries));
		error_found++;
	}
	if (le32_to_cpu(pgpt->sizeof_partition_entry) !=
            le32_to_cpu(agpt->sizeof_partition_entry)) {
		pr_warn("GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n",
                       le32_to_cpu(pgpt->sizeof_partition_entry),
		       le32_to_cpu(agpt->sizeof_partition_entry));
		error_found++;
	}
	if (le32_to_cpu(pgpt->partition_entry_array_crc32) !=
            le32_to_cpu(agpt->partition_entry_array_crc32)) {
		pr_warn("GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n",
                       le32_to_cpu(pgpt->partition_entry_array_crc32),
		       le32_to_cpu(agpt->partition_entry_array_crc32));
		error_found++;
	}
	if (le64_to_cpu(pgpt->alternate_lba) != lastlba) {
		pr_warn("GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		pr_warn("GPT:%lld != %lld\n",
			(unsigned long long)le64_to_cpu(pgpt->alternate_lba),
			(unsigned long long)lastlba);
		error_found++;
	}

	if (le64_to_cpu(agpt->my_lba) != lastlba) {
		pr_warn("GPT:Alternate GPT header not at the end of the disk.\n");
		pr_warn("GPT:%lld != %lld\n",
			(unsigned long long)le64_to_cpu(agpt->my_lba),
			(unsigned long long)lastlba);
		error_found++;
	}

	if (error_found)
		pr_warn("GPT: Use GNU Parted to correct GPT errors.\n");
	return;
}

/**
 * find_valid_gpt() - Search disk for valid GPT headers and PTEs
 * @state: disk parsed partitions
 * @gpt: GPT header ptr, filled on return.
 * @ptes: PTEs ptr, filled on return.
 *
 * Description: Returns 1 if valid, 0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 * Validity depends on PMBR being valid (or being overridden by the
 * 'gpt' kernel command line option) and finding either the Primary
 * GPT header and PTEs valid, or the Alternate GPT header and PTEs
 * valid.  If the Primary GPT header is not valid, the Alternate GPT header
 * is not checked unless the 'gpt' kernel command line option is passed.
 * This protects against devices which misreport their size, and forces
 * the user to decide to use the Alternate GPT.
 */
static int find_valid_gpt(struct parsed_partitions *state, gpt_header **gpt,
			  gpt_entry **ptes)
{
	int good_pgpt = 0, good_agpt = 0, good_pmbr = 0;
	gpt_header *pgpt = NULL, *agpt = NULL;
	gpt_entry *pptes = NULL, *aptes = NULL;
	legacy_mbr *legacymbr;
	sector_t total_sectors = i_size_read(state->bdev->bd_inode) >> 9;
	u64 lastlba;

	if (!ptes)
		return 0;

	lastlba = last_lba(state->bdev);
        if (!force_gpt) {
		/* This will be added to the EFI Spec. per Intel after v1.02. */
		legacymbr = kzalloc(sizeof(*legacymbr), GFP_KERNEL);
		if (!legacymbr)
			goto fail;

		read_lba(state, 0, (u8 *)legacymbr, sizeof(*legacymbr));
		good_pmbr = is_pmbr_valid(legacymbr, total_sectors);
		kfree(legacymbr);

		if (!good_pmbr)
			goto fail;

		pr_debug("Device has a %s MBR\n",
			 good_pmbr == GPT_MBR_PROTECTIVE ?
						"protective" : "hybrid");
	}

	good_pgpt = is_gpt_valid(state, GPT_PRIMARY_PARTITION_TABLE_LBA,
				 &pgpt, &pptes);
        if (good_pgpt)
		good_agpt = is_gpt_valid(state,
					 le64_to_cpu(pgpt->alternate_lba),
					 &agpt, &aptes);
        if (!good_agpt && force_gpt)
                good_agpt = is_gpt_valid(state, lastlba, &agpt, &aptes);

        /* The obviously unsuccessful case */
        if (!good_pgpt && !good_agpt)
                goto fail;

        compare_gpts(pgpt, agpt, lastlba);

        /* The good cases */
        if (good_pgpt) {
                *gpt  = pgpt;
                *ptes = pptes;
                kfree(agpt);
                kfree(aptes);
		if (!good_agpt)
                        pr_warn("Alternate GPT is invalid, using primary GPT.\n");
                return 1;
        }
        else if (good_agpt) {
                *gpt  = agpt;
                *ptes = aptes;
                kfree(pgpt);
                kfree(pptes);
		pr_warn("Primary GPT is invalid, using alternate GPT.\n");
                return 1;
        }

 fail:
        kfree(pgpt);
        kfree(agpt);
        kfree(pptes);
        kfree(aptes);
        *gpt = NULL;
        *ptes = NULL;
        return 0;
}

/**
 * efi_partition(struct parsed_partitions *state)
 * @state: disk parsed partitions
 *
 * Description: called from check.c, if the disk contains GPT
 * partitions, sets up partition entries in the kernel.
 *
 * If the first block on the disk is a legacy MBR,
 * it will get handled by msdos_partition().
 * If it's a Protective MBR, we'll handle it here.
 *
 * We do not create a Linux partition for GPT, but
 * only for the actual data partitions.
 * Returns:
 * -1 if unable to read the partition table
 *  0 if this isn't our partition table
 *  1 if successful
 *
 */
int efi_partition(struct parsed_partitions *state)
{
	gpt_header *gpt = NULL;
	gpt_entry *ptes = NULL;
	u32 i;
	unsigned ssz = bdev_logical_block_size(state->bdev) / 512;

	if (!find_valid_gpt(state, &gpt, &ptes) || !gpt || !ptes) {
		kfree(gpt);
		kfree(ptes);
		return 0;
	}

	pr_debug("GUID Partition Table is valid!  Yea!\n");

	for (i = 0; i < le32_to_cpu(gpt->num_partition_entries) && i < state->limit-1; i++) {
		struct partition_meta_info *info;
		unsigned label_count = 0;
		unsigned label_max;
		u64 start = le64_to_cpu(ptes[i].starting_lba);
		u64 size = le64_to_cpu(ptes[i].ending_lba) -
			   le64_to_cpu(ptes[i].starting_lba) + 1ULL;

		if (!is_pte_valid(&ptes[i], last_lba(state->bdev)))
			continue;

		put_partition(state, i+1, start * ssz, size * ssz);

		/* If this is a RAID volume, tell md */
		if (!efi_guidcmp(ptes[i].partition_type_guid, PARTITION_LINUX_RAID_GUID))
			state->parts[i + 1].flags = ADDPART_FLAG_RAID;

		info = &state->parts[i + 1].info;
		efi_guid_to_str(&ptes[i].unique_partition_guid, info->uuid);

		/* Naively convert UTF16-LE to 7 bits. */
		label_max = min(ARRAY_SIZE(info->volname) - 1,
				ARRAY_SIZE(ptes[i].partition_name));
		info->volname[label_max] = 0;
		while (label_count < label_max) {
			u8 c = ptes[i].partition_name[label_count] & 0xff;
			if (c && !isprint(c))
				c = '!';
			info->volname[label_count] = c;
			label_count++;
		}
		state->parts[i + 1].has_info = true;
	}
	kfree(ptes);
	kfree(gpt);
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	return 1;
}

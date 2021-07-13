// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/partitions/msdos.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  Support for DiskManager v6.0x added by Mark Lord,
 *  with information provided by OnTrack.  This now works for linux fdisk
 *  and LILO, as well as loadlin and bootln.  Note that disks other than
 *  /dev/hda *must* have a "DOS" type 0x51 partition in the first slot (hda1).
 *
 *  More flexible handling of extended partitions - aeb, 950831
 *
 *  Check partition table on IDE disks for common CHS translations
 *
 *  Re-organised Feb 1998 Russell King
 *
 *  BSD disklabel support by Yossi Gottlieb <yogo@math.tau.ac.il>
 *  updated by Marc Espie <Marc.Espie@openbsd.org>
 *
 *  Unixware slices support by Andrzej Krzysztofowicz <ankry@mif.pg.gda.pl>
 *  and Krzysztof G. Baranowski <kgb@knm.org.pl>
 */
#include <linux/msdos_fs.h>
#include <linux/msdos_partition.h>

#include "check.h"
#include "efi.h"

/*
 * Many architectures don't like unaligned accesses, while
 * the nr_sects and start_sect partition table entries are
 * at a 2 (mod 4) address.
 */
#include <asm/unaligned.h>

static inline sector_t nr_sects(struct msdos_partition *p)
{
	return (sector_t)get_unaligned_le32(&p->nr_sects);
}

static inline sector_t start_sect(struct msdos_partition *p)
{
	return (sector_t)get_unaligned_le32(&p->start_sect);
}

static inline int is_extended_partition(struct msdos_partition *p)
{
	return (p->sys_ind == DOS_EXTENDED_PARTITION ||
		p->sys_ind == WIN98_EXTENDED_PARTITION ||
		p->sys_ind == LINUX_EXTENDED_PARTITION);
}

#define MSDOS_LABEL_MAGIC1	0x55
#define MSDOS_LABEL_MAGIC2	0xAA

static inline int
msdos_magic_present(unsigned char *p)
{
	return (p[0] == MSDOS_LABEL_MAGIC1 && p[1] == MSDOS_LABEL_MAGIC2);
}

/* Value is EBCDIC 'IBMA' */
#define AIX_LABEL_MAGIC1	0xC9
#define AIX_LABEL_MAGIC2	0xC2
#define AIX_LABEL_MAGIC3	0xD4
#define AIX_LABEL_MAGIC4	0xC1
static int aix_magic_present(struct parsed_partitions *state, unsigned char *p)
{
	struct msdos_partition *pt = (struct msdos_partition *) (p + 0x1be);
	Sector sect;
	unsigned char *d;
	int slot, ret = 0;

	if (!(p[0] == AIX_LABEL_MAGIC1 &&
		p[1] == AIX_LABEL_MAGIC2 &&
		p[2] == AIX_LABEL_MAGIC3 &&
		p[3] == AIX_LABEL_MAGIC4))
		return 0;

	/*
	 * Assume the partition table is valid if Linux partitions exists.
	 * Note that old Solaris/x86 partitions use the same indicator as
	 * Linux swap partitions, so we consider that a Linux partition as
	 * well.
	 */
	for (slot = 1; slot <= 4; slot++, pt++) {
		if (pt->sys_ind == SOLARIS_X86_PARTITION ||
		    pt->sys_ind == LINUX_RAID_PARTITION ||
		    pt->sys_ind == LINUX_DATA_PARTITION ||
		    pt->sys_ind == LINUX_LVM_PARTITION ||
		    is_extended_partition(pt))
			return 0;
	}
	d = read_part_sector(state, 7, &sect);
	if (d) {
		if (d[0] == '_' && d[1] == 'L' && d[2] == 'V' && d[3] == 'M')
			ret = 1;
		put_dev_sector(sect);
	}
	return ret;
}

static void set_info(struct parsed_partitions *state, int slot,
		     u32 disksig)
{
	struct partition_meta_info *info = &state->parts[slot].info;

	snprintf(info->uuid, sizeof(info->uuid), "%08x-%02x", disksig,
		 slot);
	info->volname[0] = 0;
	state->parts[slot].has_info = true;
}

/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */

static void parse_extended(struct parsed_partitions *state,
			   sector_t first_sector, sector_t first_size,
			   u32 disksig)
{
	struct msdos_partition *p;
	Sector sect;
	unsigned char *data;
	sector_t this_sector, this_size;
	sector_t sector_size = bdev_logical_block_size(state->bdev) / 512;
	int loopct = 0;		/* number of links followed
				   without finding a data partition */
	int i;

	this_sector = first_sector;
	this_size = first_size;

	while (1) {
		if (++loopct > 100)
			return;
		if (state->next == state->limit)
			return;
		data = read_part_sector(state, this_sector, &sect);
		if (!data)
			return;

		if (!msdos_magic_present(data + 510))
			goto done;

		p = (struct msdos_partition *) (data + 0x1be);

		/*
		 * Usually, the first entry is the real data partition,
		 * the 2nd entry is the next extended partition, or empty,
		 * and the 3rd and 4th entries are unused.
		 * However, DRDOS sometimes has the extended partition as
		 * the first entry (when the data partition is empty),
		 * and OS/2 seems to use all four entries.
		 */

		/*
		 * First process the data partition(s)
		 */
		for (i = 0; i < 4; i++, p++) {
			sector_t offs, size, next;

			if (!nr_sects(p) || is_extended_partition(p))
				continue;

			/* Check the 3rd and 4th entries -
			   these sometimes contain random garbage */
			offs = start_sect(p)*sector_size;
			size = nr_sects(p)*sector_size;
			next = this_sector + offs;
			if (i >= 2) {
				if (offs + size > this_size)
					continue;
				if (next < first_sector)
					continue;
				if (next + size > first_sector + first_size)
					continue;
			}

			put_partition(state, state->next, next, size);
			set_info(state, state->next, disksig);
			if (p->sys_ind == LINUX_RAID_PARTITION)
				state->parts[state->next].flags = ADDPART_FLAG_RAID;
			loopct = 0;
			if (++state->next == state->limit)
				goto done;
		}
		/*
		 * Next, process the (first) extended partition, if present.
		 * (So far, there seems to be no reason to make
		 *  parse_extended()  recursive and allow a tree
		 *  of extended partitions.)
		 * It should be a link to the next logical partition.
		 */
		p -= 4;
		for (i = 0; i < 4; i++, p++)
			if (nr_sects(p) && is_extended_partition(p))
				break;
		if (i == 4)
			goto done;	 /* nothing left to do */

		this_sector = first_sector + start_sect(p) * sector_size;
		this_size = nr_sects(p) * sector_size;
		put_dev_sector(sect);
	}
done:
	put_dev_sector(sect);
}

#define SOLARIS_X86_NUMSLICE	16
#define SOLARIS_X86_VTOC_SANE	(0x600DDEEEUL)

struct solaris_x86_slice {
	__le16 s_tag;		/* ID tag of partition */
	__le16 s_flag;		/* permission flags */
	__le32 s_start;		/* start sector no of partition */
	__le32 s_size;		/* # of blocks in partition */
};

struct solaris_x86_vtoc {
	unsigned int v_bootinfo[3];	/* info needed by mboot */
	__le32 v_sanity;		/* to verify vtoc sanity */
	__le32 v_version;		/* layout version */
	char	v_volume[8];		/* volume name */
	__le16	v_sectorsz;		/* sector size in bytes */
	__le16	v_nparts;		/* number of partitions */
	unsigned int v_reserved[10];	/* free space */
	struct solaris_x86_slice
		v_slice[SOLARIS_X86_NUMSLICE]; /* slice headers */
	unsigned int timestamp[SOLARIS_X86_NUMSLICE]; /* timestamp */
	char	v_asciilabel[128];	/* for compatibility */
};

/* james@bpgc.com: Solaris has a nasty indicator: 0x82 which also
   indicates linux swap.  Be careful before believing this is Solaris. */

static void parse_solaris_x86(struct parsed_partitions *state,
			      sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_SOLARIS_X86_PARTITION
	Sector sect;
	struct solaris_x86_vtoc *v;
	int i;
	short max_nparts;

	v = read_part_sector(state, offset + 1, &sect);
	if (!v)
		return;
	if (le32_to_cpu(v->v_sanity) != SOLARIS_X86_VTOC_SANE) {
		put_dev_sector(sect);
		return;
	}
	{
		char tmp[1 + BDEVNAME_SIZE + 10 + 11 + 1];

		snprintf(tmp, sizeof(tmp), " %s%d: <solaris:", state->name, origin);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
	}
	if (le32_to_cpu(v->v_version) != 1) {
		char tmp[64];

		snprintf(tmp, sizeof(tmp), "  cannot handle version %d vtoc>\n",
			 le32_to_cpu(v->v_version));
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		put_dev_sector(sect);
		return;
	}
	/* Ensure we can handle previous case of VTOC with 8 entries gracefully */
	max_nparts = le16_to_cpu(v->v_nparts) > 8 ? SOLARIS_X86_NUMSLICE : 8;
	for (i = 0; i < max_nparts && state->next < state->limit; i++) {
		struct solaris_x86_slice *s = &v->v_slice[i];
		char tmp[3 + 10 + 1 + 1];

		if (s->s_size == 0)
			continue;
		snprintf(tmp, sizeof(tmp), " [s%d]", i);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		/* solaris partitions are relative to current MS-DOS
		 * one; must add the offset of the current partition */
		put_partition(state, state->next++,
				 le32_to_cpu(s->s_start)+offset,
				 le32_to_cpu(s->s_size));
	}
	put_dev_sector(sect);
	strlcat(state->pp_buf, " >\n", PAGE_SIZE);
#endif
}

/* check against BSD src/sys/sys/disklabel.h for consistency */
#define BSD_DISKMAGIC	(0x82564557UL)	/* The disk magic number */
#define BSD_MAXPARTITIONS	16
#define OPENBSD_MAXPARTITIONS	16
#define BSD_FS_UNUSED		0 /* disklabel unused partition entry ID */
struct bsd_disklabel {
	__le32	d_magic;		/* the magic number */
	__s16	d_type;			/* drive type */
	__s16	d_subtype;		/* controller/d_type specific */
	char	d_typename[16];		/* type name, e.g. "eagle" */
	char	d_packname[16];		/* pack identifier */
	__u32	d_secsize;		/* # of bytes per sector */
	__u32	d_nsectors;		/* # of data sectors per track */
	__u32	d_ntracks;		/* # of tracks per cylinder */
	__u32	d_ncylinders;		/* # of data cylinders per unit */
	__u32	d_secpercyl;		/* # of data sectors per cylinder */
	__u32	d_secperunit;		/* # of data sectors per unit */
	__u16	d_sparespertrack;	/* # of spare sectors per track */
	__u16	d_sparespercyl;		/* # of spare sectors per cylinder */
	__u32	d_acylinders;		/* # of alt. cylinders per unit */
	__u16	d_rpm;			/* rotational speed */
	__u16	d_interleave;		/* hardware sector interleave */
	__u16	d_trackskew;		/* sector 0 skew, per track */
	__u16	d_cylskew;		/* sector 0 skew, per cylinder */
	__u32	d_headswitch;		/* head switch time, usec */
	__u32	d_trkseek;		/* track-to-track seek, usec */
	__u32	d_flags;		/* generic flags */
#define NDDATA 5
	__u32	d_drivedata[NDDATA];	/* drive-type specific information */
#define NSPARE 5
	__u32	d_spare[NSPARE];	/* reserved for future use */
	__le32	d_magic2;		/* the magic number (again) */
	__le16	d_checksum;		/* xor of data incl. partitions */

			/* filesystem and partition information: */
	__le16	d_npartitions;		/* number of partitions in following */
	__le32	d_bbsize;		/* size of boot area at sn0, bytes */
	__le32	d_sbsize;		/* max size of fs superblock, bytes */
	struct	bsd_partition {		/* the partition table */
		__le32	p_size;		/* number of sectors in partition */
		__le32	p_offset;	/* starting sector */
		__le32	p_fsize;	/* filesystem basic fragment size */
		__u8	p_fstype;	/* filesystem type, see below */
		__u8	p_frag;		/* filesystem fragments per block */
		__le16	p_cpg;		/* filesystem cylinders per group */
	} d_partitions[BSD_MAXPARTITIONS];	/* actually may be more */
};

#if defined(CONFIG_BSD_DISKLABEL)
/*
 * Create devices for BSD partitions listed in a disklabel, under a
 * dos-like partition. See parse_extended() for more information.
 */
static void parse_bsd(struct parsed_partitions *state,
		      sector_t offset, sector_t size, int origin, char *flavour,
		      int max_partitions)
{
	Sector sect;
	struct bsd_disklabel *l;
	struct bsd_partition *p;
	char tmp[64];

	l = read_part_sector(state, offset + 1, &sect);
	if (!l)
		return;
	if (le32_to_cpu(l->d_magic) != BSD_DISKMAGIC) {
		put_dev_sector(sect);
		return;
	}

	snprintf(tmp, sizeof(tmp), " %s%d: <%s:", state->name, origin, flavour);
	strlcat(state->pp_buf, tmp, PAGE_SIZE);

	if (le16_to_cpu(l->d_npartitions) < max_partitions)
		max_partitions = le16_to_cpu(l->d_npartitions);
	for (p = l->d_partitions; p - l->d_partitions < max_partitions; p++) {
		sector_t bsd_start, bsd_size;

		if (state->next == state->limit)
			break;
		if (p->p_fstype == BSD_FS_UNUSED)
			continue;
		bsd_start = le32_to_cpu(p->p_offset);
		bsd_size = le32_to_cpu(p->p_size);
		/* FreeBSD has relative offset if C partition offset is zero */
		if (memcmp(flavour, "bsd\0", 4) == 0 &&
		    le32_to_cpu(l->d_partitions[2].p_offset) == 0)
			bsd_start += offset;
		if (offset == bsd_start && size == bsd_size)
			/* full parent partition, we have it already */
			continue;
		if (offset > bsd_start || offset+size < bsd_start+bsd_size) {
			strlcat(state->pp_buf, "bad subpartition - ignored\n", PAGE_SIZE);
			continue;
		}
		put_partition(state, state->next++, bsd_start, bsd_size);
	}
	put_dev_sector(sect);
	if (le16_to_cpu(l->d_npartitions) > max_partitions) {
		snprintf(tmp, sizeof(tmp), " (ignored %d more)",
			 le16_to_cpu(l->d_npartitions) - max_partitions);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
	}
	strlcat(state->pp_buf, " >\n", PAGE_SIZE);
}
#endif

static void parse_freebsd(struct parsed_partitions *state,
			  sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_BSD_DISKLABEL
	parse_bsd(state, offset, size, origin, "bsd", BSD_MAXPARTITIONS);
#endif
}

static void parse_netbsd(struct parsed_partitions *state,
			 sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_BSD_DISKLABEL
	parse_bsd(state, offset, size, origin, "netbsd", BSD_MAXPARTITIONS);
#endif
}

static void parse_openbsd(struct parsed_partitions *state,
			  sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_BSD_DISKLABEL
	parse_bsd(state, offset, size, origin, "openbsd",
		  OPENBSD_MAXPARTITIONS);
#endif
}

#define UNIXWARE_DISKMAGIC     (0xCA5E600DUL)	/* The disk magic number */
#define UNIXWARE_DISKMAGIC2    (0x600DDEEEUL)	/* The slice table magic nr */
#define UNIXWARE_NUMSLICE      16
#define UNIXWARE_FS_UNUSED     0		/* Unused slice entry ID */

struct unixware_slice {
	__le16   s_label;	/* label */
	__le16   s_flags;	/* permission flags */
	__le32   start_sect;	/* starting sector */
	__le32   nr_sects;	/* number of sectors in slice */
};

struct unixware_disklabel {
	__le32	d_type;			/* drive type */
	__le32	d_magic;		/* the magic number */
	__le32	d_version;		/* version number */
	char	d_serial[12];		/* serial number of the device */
	__le32	d_ncylinders;		/* # of data cylinders per device */
	__le32	d_ntracks;		/* # of tracks per cylinder */
	__le32	d_nsectors;		/* # of data sectors per track */
	__le32	d_secsize;		/* # of bytes per sector */
	__le32	d_part_start;		/* # of first sector of this partition*/
	__le32	d_unknown1[12];		/* ? */
	__le32	d_alt_tbl;		/* byte offset of alternate table */
	__le32	d_alt_len;		/* byte length of alternate table */
	__le32	d_phys_cyl;		/* # of physical cylinders per device */
	__le32	d_phys_trk;		/* # of physical tracks per cylinder */
	__le32	d_phys_sec;		/* # of physical sectors per track */
	__le32	d_phys_bytes;		/* # of physical bytes per sector */
	__le32	d_unknown2;		/* ? */
	__le32	d_unknown3;		/* ? */
	__le32	d_pad[8];		/* pad */

	struct unixware_vtoc {
		__le32	v_magic;		/* the magic number */
		__le32	v_version;		/* version number */
		char	v_name[8];		/* volume name */
		__le16	v_nslices;		/* # of slices */
		__le16	v_unknown1;		/* ? */
		__le32	v_reserved[10];		/* reserved */
		struct unixware_slice
			v_slice[UNIXWARE_NUMSLICE];	/* slice headers */
	} vtoc;
};  /* 408 */

/*
 * Create devices for Unixware partitions listed in a disklabel, under a
 * dos-like partition. See parse_extended() for more information.
 */
static void parse_unixware(struct parsed_partitions *state,
			   sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_UNIXWARE_DISKLABEL
	Sector sect;
	struct unixware_disklabel *l;
	struct unixware_slice *p;

	l = read_part_sector(state, offset + 29, &sect);
	if (!l)
		return;
	if (le32_to_cpu(l->d_magic) != UNIXWARE_DISKMAGIC ||
	    le32_to_cpu(l->vtoc.v_magic) != UNIXWARE_DISKMAGIC2) {
		put_dev_sector(sect);
		return;
	}
	{
		char tmp[1 + BDEVNAME_SIZE + 10 + 12 + 1];

		snprintf(tmp, sizeof(tmp), " %s%d: <unixware:", state->name, origin);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
	}
	p = &l->vtoc.v_slice[1];
	/* I omit the 0th slice as it is the same as whole disk. */
	while (p - &l->vtoc.v_slice[0] < UNIXWARE_NUMSLICE) {
		if (state->next == state->limit)
			break;

		if (p->s_label != UNIXWARE_FS_UNUSED)
			put_partition(state, state->next++,
				      le32_to_cpu(p->start_sect),
				      le32_to_cpu(p->nr_sects));
		p++;
	}
	put_dev_sector(sect);
	strlcat(state->pp_buf, " >\n", PAGE_SIZE);
#endif
}

#define MINIX_NR_SUBPARTITIONS  4

/*
 * Minix 2.0.0/2.0.2 subpartition support.
 * Anand Krishnamurthy <anandk@wiproge.med.ge.com>
 * Rajeev V. Pillai    <rajeevvp@yahoo.com>
 */
static void parse_minix(struct parsed_partitions *state,
			sector_t offset, sector_t size, int origin)
{
#ifdef CONFIG_MINIX_SUBPARTITION
	Sector sect;
	unsigned char *data;
	struct msdos_partition *p;
	int i;

	data = read_part_sector(state, offset, &sect);
	if (!data)
		return;

	p = (struct msdos_partition *)(data + 0x1be);

	/* The first sector of a Minix partition can have either
	 * a secondary MBR describing its subpartitions, or
	 * the normal boot sector. */
	if (msdos_magic_present(data + 510) &&
	    p->sys_ind == MINIX_PARTITION) { /* subpartition table present */
		char tmp[1 + BDEVNAME_SIZE + 10 + 9 + 1];

		snprintf(tmp, sizeof(tmp), " %s%d: <minix:", state->name, origin);
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		for (i = 0; i < MINIX_NR_SUBPARTITIONS; i++, p++) {
			if (state->next == state->limit)
				break;
			/* add each partition in use */
			if (p->sys_ind == MINIX_PARTITION)
				put_partition(state, state->next++,
					      start_sect(p), nr_sects(p));
		}
		strlcat(state->pp_buf, " >\n", PAGE_SIZE);
	}
	put_dev_sector(sect);
#endif /* CONFIG_MINIX_SUBPARTITION */
}

static struct {
	unsigned char id;
	void (*parse)(struct parsed_partitions *, sector_t, sector_t, int);
} subtypes[] = {
	{FREEBSD_PARTITION, parse_freebsd},
	{NETBSD_PARTITION, parse_netbsd},
	{OPENBSD_PARTITION, parse_openbsd},
	{MINIX_PARTITION, parse_minix},
	{UNIXWARE_PARTITION, parse_unixware},
	{SOLARIS_X86_PARTITION, parse_solaris_x86},
	{NEW_SOLARIS_X86_PARTITION, parse_solaris_x86},
	{0, NULL},
};

int msdos_partition(struct parsed_partitions *state)
{
	sector_t sector_size = bdev_logical_block_size(state->bdev) / 512;
	Sector sect;
	unsigned char *data;
	struct msdos_partition *p;
	struct fat_boot_sector *fb;
	int slot;
	u32 disksig;

	data = read_part_sector(state, 0, &sect);
	if (!data)
		return -1;

	/*
	 * Note order! (some AIX disks, e.g. unbootable kind,
	 * have no MSDOS 55aa)
	 */
	if (aix_magic_present(state, data)) {
		put_dev_sector(sect);
#ifdef CONFIG_AIX_PARTITION
		return aix_partition(state);
#else
		strlcat(state->pp_buf, " [AIX]", PAGE_SIZE);
		return 0;
#endif
	}

	if (!msdos_magic_present(data + 510)) {
		put_dev_sector(sect);
		return 0;
	}

	/*
	 * Now that the 55aa signature is present, this is probably
	 * either the boot sector of a FAT filesystem or a DOS-type
	 * partition table. Reject this in case the boot indicator
	 * is not 0 or 0x80.
	 */
	p = (struct msdos_partition *) (data + 0x1be);
	for (slot = 1; slot <= 4; slot++, p++) {
		if (p->boot_ind != 0 && p->boot_ind != 0x80) {
			/*
			 * Even without a valid boot indicator value
			 * its still possible this is valid FAT filesystem
			 * without a partition table.
			 */
			fb = (struct fat_boot_sector *) data;
			if (slot == 1 && fb->reserved && fb->fats
				&& fat_valid_media(fb->media)) {
				strlcat(state->pp_buf, "\n", PAGE_SIZE);
				put_dev_sector(sect);
				return 1;
			} else {
				put_dev_sector(sect);
				return 0;
			}
		}
	}

#ifdef CONFIG_EFI_PARTITION
	p = (struct msdos_partition *) (data + 0x1be);
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		/* If this is an EFI GPT disk, msdos should ignore it. */
		if (p->sys_ind == EFI_PMBR_OSTYPE_EFI_GPT) {
			put_dev_sector(sect);
			return 0;
		}
	}
#endif
	p = (struct msdos_partition *) (data + 0x1be);

	disksig = le32_to_cpup((__le32 *)(data + 0x1b8));

	/*
	 * Look for partitions in two passes:
	 * First find the primary and DOS-type extended partitions.
	 * On the second pass look inside *BSD, Unixware and Solaris partitions.
	 */

	state->next = 5;
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		sector_t start = start_sect(p)*sector_size;
		sector_t size = nr_sects(p)*sector_size;

		if (!size)
			continue;
		if (is_extended_partition(p)) {
			/*
			 * prevent someone doing mkfs or mkswap on an
			 * extended partition, but leave room for LILO
			 * FIXME: this uses one logical sector for > 512b
			 * sector, although it may not be enough/proper.
			 */
			sector_t n = 2;

			n = min(size, max(sector_size, n));
			put_partition(state, slot, start, n);

			strlcat(state->pp_buf, " <", PAGE_SIZE);
			parse_extended(state, start, size, disksig);
			strlcat(state->pp_buf, " >", PAGE_SIZE);
			continue;
		}
		put_partition(state, slot, start, size);
		set_info(state, slot, disksig);
		if (p->sys_ind == LINUX_RAID_PARTITION)
			state->parts[slot].flags = ADDPART_FLAG_RAID;
		if (p->sys_ind == DM6_PARTITION)
			strlcat(state->pp_buf, "[DM]", PAGE_SIZE);
		if (p->sys_ind == EZD_PARTITION)
			strlcat(state->pp_buf, "[EZD]", PAGE_SIZE);
	}

	strlcat(state->pp_buf, "\n", PAGE_SIZE);

	/* second pass - output for each on a separate line */
	p = (struct msdos_partition *) (0x1be + data);
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		unsigned char id = p->sys_ind;
		int n;

		if (!nr_sects(p))
			continue;

		for (n = 0; subtypes[n].parse && id != subtypes[n].id; n++)
			;

		if (!subtypes[n].parse)
			continue;
		subtypes[n].parse(state, start_sect(p) * sector_size,
				  nr_sects(p) * sector_size, slot);
	}
	put_dev_sector(sect);
	return 1;
}

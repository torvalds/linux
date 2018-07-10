/*
 * Copyright (c) 1987, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgment:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if ENABLE_FEATURE_OSF_LABEL

#ifndef BSD_DISKMAGIC
#define BSD_DISKMAGIC     ((uint32_t) 0x82564557)
#endif

#ifndef BSD_MAXPARTITIONS
#define BSD_MAXPARTITIONS 16
#endif

#define BSD_LINUX_BOOTDIR "/usr/ucb/mdec"

#if defined(__alpha__) \
 || defined(__powerpc__) \
 || defined(__ia64__) \
 || defined(__hppa__)
# define BSD_LABELSECTOR   0
# define BSD_LABELOFFSET   64
#else
# define BSD_LABELSECTOR   1
# define BSD_LABELOFFSET   0
#endif

#define BSD_BBSIZE        8192          /* size of boot area, with label */
#define BSD_SBSIZE        8192          /* max size of fs superblock */

struct xbsd_disklabel {
	uint32_t   d_magic;                /* the magic number */
	int16_t    d_type;                 /* drive type */
	int16_t    d_subtype;              /* controller/d_type specific */
	char       d_typename[16];         /* type name, e.g. "eagle" */
	char       d_packname[16];         /* pack identifier */
	/* disk geometry: */
	uint32_t   d_secsize;              /* # of bytes per sector */
	uint32_t   d_nsectors;             /* # of data sectors per track */
	uint32_t   d_ntracks;              /* # of tracks per cylinder */
	uint32_t   d_ncylinders;           /* # of data cylinders per unit */
	uint32_t   d_secpercyl;            /* # of data sectors per cylinder */
	uint32_t   d_secperunit;           /* # of data sectors per unit */
	/*
	 * Spares (bad sector replacements) below
	 * are not counted in d_nsectors or d_secpercyl.
	 * Spare sectors are assumed to be physical sectors
	 * which occupy space at the end of each track and/or cylinder.
	 */
	uint16_t   d_sparespertrack;       /* # of spare sectors per track */
	uint16_t   d_sparespercyl;         /* # of spare sectors per cylinder */
	/*
	 * Alternate cylinders include maintenance, replacement,
	 * configuration description areas, etc.
	 */
	uint32_t   d_acylinders;           /* # of alt. cylinders per unit */

	/* hardware characteristics: */
	/*
	 * d_interleave, d_trackskew and d_cylskew describe perturbations
	 * in the media format used to compensate for a slow controller.
	 * Interleave is physical sector interleave, set up by the formatter
	 * or controller when formatting.  When interleaving is in use,
	 * logically adjacent sectors are not physically contiguous,
	 * but instead are separated by some number of sectors.
	 * It is specified as the ratio of physical sectors traversed
	 * per logical sector.  Thus an interleave of 1:1 implies contiguous
	 * layout, while 2:1 implies that logical sector 0 is separated
	 * by one sector from logical sector 1.
	 * d_trackskew is the offset of sector 0 on track N
	 * relative to sector 0 on track N-1 on the same cylinder.
	 * Finally, d_cylskew is the offset of sector 0 on cylinder N
	 * relative to sector 0 on cylinder N-1.
	 */
	uint16_t   d_rpm;                  /* rotational speed */
	uint16_t   d_interleave;           /* hardware sector interleave */
	uint16_t   d_trackskew;            /* sector 0 skew, per track */
	uint16_t   d_cylskew;              /* sector 0 skew, per cylinder */
	uint32_t   d_headswitch;           /* head switch time, usec */
	uint32_t   d_trkseek;              /* track-to-track seek, usec */
	uint32_t   d_flags;                /* generic flags */
#define NDDATA 5
	uint32_t   d_drivedata[NDDATA];    /* drive-type specific information */
#define NSPARE 5
	uint32_t   d_spare[NSPARE];        /* reserved for future use */
	uint32_t   d_magic2;               /* the magic number (again) */
	uint16_t   d_checksum;             /* xor of data incl. partitions */
	/* filesystem and partition information: */
	uint16_t   d_npartitions;          /* number of partitions in following */
	uint32_t   d_bbsize;               /* size of boot area at sn0, bytes */
	uint32_t   d_sbsize;               /* max size of fs superblock, bytes */
	struct xbsd_partition { /* the partition table */
		uint32_t   p_size;         /* number of sectors in partition */
		uint32_t   p_offset;       /* starting sector */
		uint32_t   p_fsize;        /* filesystem basic fragment size */
		uint8_t    p_fstype;       /* filesystem type, see below */
		uint8_t    p_frag;         /* filesystem fragments per block */
		uint16_t   p_cpg;          /* filesystem cylinders per group */
	} d_partitions[BSD_MAXPARTITIONS]; /* actually may be more */
};

/* d_type values: */
#define BSD_DTYPE_SMD           1               /* SMD, XSMD; VAX hp/up */
#define BSD_DTYPE_MSCP          2               /* MSCP */
#define BSD_DTYPE_DEC           3               /* other DEC (rk, rl) */
#define BSD_DTYPE_SCSI          4               /* SCSI */
#define BSD_DTYPE_ESDI          5               /* ESDI interface */
#define BSD_DTYPE_ST506         6               /* ST506 etc. */
#define BSD_DTYPE_HPIB          7               /* CS/80 on HP-IB */
#define BSD_DTYPE_HPFL          8               /* HP Fiber-link */
#define BSD_DTYPE_FLOPPY        10              /* floppy */

/* d_subtype values: */
#define BSD_DSTYPE_INDOSPART    0x8             /* is inside dos partition */
#define BSD_DSTYPE_DOSPART(s)   ((s) & 3)       /* dos partition number */
#define BSD_DSTYPE_GEOMETRY     0x10            /* drive params in label */

static const char *const xbsd_dktypenames[] = {
	"unknown",
	"SMD",
	"MSCP",
	"old DEC",
	"SCSI",
	"ESDI",
	"ST506",
	"HP-IB",
	"HP-FL",
	"type 9",
	"floppy",
	0
};


/*
 * Filesystem type and version.
 * Used to interpret other filesystem-specific
 * per-partition information.
 */
#define BSD_FS_UNUSED   0               /* unused */
#define BSD_FS_SWAP     1               /* swap */
#define BSD_FS_V6       2               /* Sixth Edition */
#define BSD_FS_V7       3               /* Seventh Edition */
#define BSD_FS_SYSV     4               /* System V */
#define BSD_FS_V71K     5               /* V7 with 1K blocks (4.1, 2.9) */
#define BSD_FS_V8       6               /* Eighth Edition, 4K blocks */
#define BSD_FS_BSDFFS   7               /* 4.2BSD fast file system */
#define BSD_FS_BSDLFS   9               /* 4.4BSD log-structured file system */
#define BSD_FS_OTHER    10              /* in use, but unknown/unsupported */
#define BSD_FS_HPFS     11              /* OS/2 high-performance file system */
#define BSD_FS_ISO9660  12              /* ISO-9660 filesystem (cdrom) */
#define BSD_FS_ISOFS    BSD_FS_ISO9660
#define BSD_FS_BOOT     13              /* partition contains bootstrap */
#define BSD_FS_ADOS     14              /* AmigaDOS fast file system */
#define BSD_FS_HFS      15              /* Macintosh HFS */
#define BSD_FS_ADVFS    16              /* Digital Unix AdvFS */

/* this is annoying, but it's also the way it is :-( */
#ifdef __alpha__
#define BSD_FS_EXT2     8               /* ext2 file system */
#else
#define BSD_FS_MSDOS    8               /* MS-DOS file system */
#endif

static const char *const xbsd_fstypes[] = {
	"\x00" "unused",            /* BSD_FS_UNUSED  */
	"\x01" "swap",              /* BSD_FS_SWAP    */
	"\x02" "Version 6",         /* BSD_FS_V6      */
	"\x03" "Version 7",         /* BSD_FS_V7      */
	"\x04" "System V",          /* BSD_FS_SYSV    */
	"\x05" "4.1BSD",            /* BSD_FS_V71K    */
	"\x06" "Eighth Edition",    /* BSD_FS_V8      */
	"\x07" "4.2BSD",            /* BSD_FS_BSDFFS  */
#ifdef __alpha__
	"\x08" "ext2",              /* BSD_FS_EXT2    */
#else
	"\x08" "MS-DOS",            /* BSD_FS_MSDOS   */
#endif
	"\x09" "4.4LFS",            /* BSD_FS_BSDLFS  */
	"\x0a" "unknown",           /* BSD_FS_OTHER   */
	"\x0b" "HPFS",              /* BSD_FS_HPFS    */
	"\x0c" "ISO-9660",          /* BSD_FS_ISO9660 */
	"\x0d" "boot",              /* BSD_FS_BOOT    */
	"\x0e" "ADOS",              /* BSD_FS_ADOS    */
	"\x0f" "HFS",               /* BSD_FS_HFS     */
	"\x10" "AdvFS",             /* BSD_FS_ADVFS   */
	NULL
};


/*
 * flags shared by various drives:
 */
#define BSD_D_REMOVABLE 0x01            /* removable media */
#define BSD_D_ECC       0x02            /* supports ECC */
#define BSD_D_BADSECT   0x04            /* supports bad sector forw. */
#define BSD_D_RAMDISK   0x08            /* disk emulator */
#define BSD_D_CHAIN     0x10            /* can do back-back transfers */
#define BSD_D_DOSPART   0x20            /* within MSDOS partition */

/*
   Changes:
   19990319 - Arnaldo Carvalho de Melo <acme@conectiva.com.br> - i18n/nls

   20000101 - David Huggins-Daines <dhuggins@linuxcare.com> - Better
   support for OSF/1 disklabels on Alpha.
   Also fixed unaligned accesses in alpha_bootblock_checksum()
*/

#define FREEBSD_PARTITION       0xa5
#define NETBSD_PARTITION        0xa9

static void xbsd_delete_part(void);
static void xbsd_new_part(void);
static void xbsd_write_disklabel(void);
static int xbsd_create_disklabel(void);
static void xbsd_edit_disklabel(void);
static void xbsd_write_bootstrap(void);
static void xbsd_change_fstype(void);
static int xbsd_get_part_index(int max);
static int xbsd_check_new_partition(int *i);
static void xbsd_list_types(void);
static uint16_t xbsd_dkcksum(struct xbsd_disklabel *lp);
static int xbsd_initlabel(struct partition *p);
static int xbsd_readlabel(struct partition *p);
static int xbsd_writelabel(struct partition *p);

#if defined(__alpha__)
static void alpha_bootblock_checksum(char *boot);
#endif

#if !defined(__alpha__)
static int xbsd_translate_fstype(int linux_type);
static void xbsd_link_part(void);
static struct partition *xbsd_part;
static int xbsd_part_index;
#endif


/* Group big globals data and allocate it in one go */
struct bsd_globals {
/* We access this through a uint64_t * when checksumming */
/* hopefully xmalloc gives us required alignment */
	char disklabelbuffer[BSD_BBSIZE];
	struct xbsd_disklabel xbsd_dlabel;
};

static struct bsd_globals *bsd_globals_ptr;

#define disklabelbuffer (bsd_globals_ptr->disklabelbuffer)
#define xbsd_dlabel     (bsd_globals_ptr->xbsd_dlabel)


/* Code */

#define bsd_cround(n) \
	(display_in_cyl_units ? ((n)/xbsd_dlabel.d_secpercyl) + 1 : (n))

/*
 * Test whether the whole disk has BSD disk label magic.
 *
 * Note: often reformatting with DOS-type label leaves the BSD magic,
 * so this does not mean that there is a BSD disk label.
 */
static int
check_osf_label(void)
{
	if (xbsd_readlabel(NULL) == 0)
		return 0;
	return 1;
}

static int
bsd_trydev(const char * dev)
{
	if (xbsd_readlabel(NULL) == 0)
		return -1;
	printf("\nBSD label for device: %s\n", dev);
	xbsd_print_disklabel(0);
	return 0;
}

static void
bsd_menu(void)
{
	puts("Command Action");
	puts("d\tdelete a BSD partition");
	puts("e\tedit drive data");
	puts("i\tinstall bootstrap");
	puts("l\tlist known filesystem types");
	puts("n\tadd a new BSD partition");
	puts("p\tprint BSD partition table");
	puts("q\tquit without saving changes");
	puts("r\treturn to main menu");
	puts("s\tshow complete disklabel");
	puts("t\tchange a partition's filesystem id");
	puts("u\tchange units (cylinders/sectors)");
	puts("w\twrite disklabel to disk");
#if !defined(__alpha__)
	puts("x\tlink BSD partition to non-BSD partition");
#endif
}

#if !defined(__alpha__)
static int
hidden(int type)
{
	return type ^ 0x10;
}

static int
is_bsd_partition_type(int type)
{
	return (type == FREEBSD_PARTITION ||
		type == hidden(FREEBSD_PARTITION) ||
		type == NETBSD_PARTITION ||
		type == hidden(NETBSD_PARTITION));
}
#endif

static void
bsd_select(void)
{
#if !defined(__alpha__)
	int t, ss;
	struct partition *p;

	for (t = 0; t < 4; t++) {
		p = get_part_table(t);
		if (p && is_bsd_partition_type(p->sys_ind)) {
			xbsd_part = p;
			xbsd_part_index = t;
			ss = get_start_sect(xbsd_part);
			if (ss == 0) {
				printf("Partition %s has invalid starting sector 0\n",
					partname(disk_device, t+1, 0));
				return;
			}
				printf("Reading disklabel of %s at sector %u\n",
					partname(disk_device, t+1, 0), ss + BSD_LABELSECTOR);
			if (xbsd_readlabel(xbsd_part) == 0) {
				if (xbsd_create_disklabel() == 0)
					return;
				break;
			}
		}
	}

	if (t == 4) {
		printf("There is no *BSD partition on %s\n", disk_device);
		return;
	}

#elif defined(__alpha__)

	if (xbsd_readlabel(NULL) == 0)
		if (xbsd_create_disklabel() == 0)
			exit(EXIT_SUCCESS);

#endif

	while (1) {
		bb_putchar('\n');
		switch (tolower(read_nonempty("BSD disklabel command (m for help): "))) {
		case 'd':
			xbsd_delete_part();
			break;
		case 'e':
			xbsd_edit_disklabel();
			break;
		case 'i':
			xbsd_write_bootstrap();
			break;
		case 'l':
			xbsd_list_types();
			break;
		case 'n':
			xbsd_new_part();
			break;
		case 'p':
			xbsd_print_disklabel(0);
			break;
		case 'q':
			if (ENABLE_FEATURE_CLEAN_UP)
				close_dev_fd();
			exit(EXIT_SUCCESS);
		case 'r':
			return;
		case 's':
			xbsd_print_disklabel(1);
			break;
		case 't':
			xbsd_change_fstype();
			break;
		case 'u':
			change_units();
			break;
		case 'w':
			xbsd_write_disklabel();
			break;
#if !defined(__alpha__)
		case 'x':
			xbsd_link_part();
			break;
#endif
		default:
			bsd_menu();
			break;
		}
	}
}

static void
xbsd_delete_part(void)
{
	int i;

	i = xbsd_get_part_index(xbsd_dlabel.d_npartitions);
	xbsd_dlabel.d_partitions[i].p_size   = 0;
	xbsd_dlabel.d_partitions[i].p_offset = 0;
	xbsd_dlabel.d_partitions[i].p_fstype = BSD_FS_UNUSED;
	if (xbsd_dlabel.d_npartitions == i + 1)
		while (xbsd_dlabel.d_partitions[xbsd_dlabel.d_npartitions-1].p_size == 0)
			xbsd_dlabel.d_npartitions--;
}

static void
xbsd_new_part(void)
{
	off_t begin, end;
	char mesg[256];
	int i;

	if (!xbsd_check_new_partition(&i))
		return;

#if !defined(__alpha__) && !defined(__powerpc__) && !defined(__hppa__)
	begin = get_start_sect(xbsd_part);
	end = begin + get_nr_sects(xbsd_part) - 1;
#else
	begin = 0;
	end = xbsd_dlabel.d_secperunit - 1;
#endif

	snprintf(mesg, sizeof(mesg), "First %s", str_units(SINGULAR));
	begin = read_int(bsd_cround(begin), bsd_cround(begin), bsd_cround(end),
		0, mesg);

	if (display_in_cyl_units)
		begin = (begin - 1) * xbsd_dlabel.d_secpercyl;

	snprintf(mesg, sizeof(mesg), "Last %s or +size or +sizeM or +sizeK",
		str_units(SINGULAR));
	end = read_int(bsd_cround(begin), bsd_cround(end), bsd_cround(end),
		bsd_cround(begin), mesg);

	if (display_in_cyl_units)
		end = end * xbsd_dlabel.d_secpercyl - 1;

	xbsd_dlabel.d_partitions[i].p_size   = end - begin + 1;
	xbsd_dlabel.d_partitions[i].p_offset = begin;
	xbsd_dlabel.d_partitions[i].p_fstype = BSD_FS_UNUSED;
}

static void
xbsd_print_disklabel(int show_all)
{
	struct xbsd_disklabel *lp = &xbsd_dlabel;
	struct xbsd_partition *pp;
	int i, j;

	if (show_all) {
		static const int d_masks[] = { BSD_D_REMOVABLE, BSD_D_ECC, BSD_D_BADSECT };

#if defined(__alpha__)
		printf("# %s:\n", disk_device);
#else
		printf("# %s:\n", partname(disk_device, xbsd_part_index+1, 0));
#endif
		if ((unsigned) lp->d_type < ARRAY_SIZE(xbsd_dktypenames)-1)
			printf("type: %s\n", xbsd_dktypenames[lp->d_type]);
		else
			printf("type: %u\n", lp->d_type);
		printf("disk: %.*s\n", (int) sizeof(lp->d_typename), lp->d_typename);
		printf("label: %.*s\n", (int) sizeof(lp->d_packname), lp->d_packname);
		printf("flags: ");
		print_flags_separated(d_masks, "removable\0""ecc\0""badsect\0", lp->d_flags, " ");
		bb_putchar('\n');
		/* On various machines the fields of *lp are short/int/long */
		/* In order to avoid problems, we cast them all to long. */
		printf("bytes/sector: %lu\n", (long) lp->d_secsize);
		printf("sectors/track: %lu\n", (long) lp->d_nsectors);
		printf("tracks/cylinder: %lu\n", (long) lp->d_ntracks);
		printf("sectors/cylinder: %lu\n", (long) lp->d_secpercyl);
		printf("cylinders: %lu\n", (long) lp->d_ncylinders);
		printf("rpm: %u\n", lp->d_rpm);
		printf("interleave: %u\n", lp->d_interleave);
		printf("trackskew: %u\n", lp->d_trackskew);
		printf("cylinderskew: %u\n", lp->d_cylskew);
		printf("headswitch: %lu\t\t# milliseconds\n",
			(long) lp->d_headswitch);
		printf("track-to-track seek: %lu\t# milliseconds\n",
			(long) lp->d_trkseek);
		printf("drivedata: ");
		for (i = NDDATA - 1; i >= 0; i--)
			if (lp->d_drivedata[i])
				break;
		if (i < 0)
			i = 0;
		for (j = 0; j <= i; j++)
			printf("%lu ", (long) lp->d_drivedata[j]);
	}
	printf("\n%u partitions:\n", lp->d_npartitions);
	printf("#       start       end      size     fstype   [fsize bsize   cpg]\n");
	pp = lp->d_partitions;
	for (i = 0; i < lp->d_npartitions; i++, pp++) {
		if (pp->p_size) {
			if (display_in_cyl_units && lp->d_secpercyl) {
				printf("  %c: %8lu%c %8lu%c %8lu%c  ",
					'a' + i,
					(unsigned long) pp->p_offset / lp->d_secpercyl + 1,
					(pp->p_offset % lp->d_secpercyl) ? '*' : ' ',
					(unsigned long) (pp->p_offset + pp->p_size + lp->d_secpercyl - 1) / lp->d_secpercyl,
					((pp->p_offset + pp->p_size) % lp->d_secpercyl) ? '*' : ' ',
					(long) pp->p_size / lp->d_secpercyl,
					(pp->p_size % lp->d_secpercyl) ? '*' : ' '
				);
			} else {
				printf("  %c: %8lu  %8lu  %8lu   ",
					'a' + i,
					(long) pp->p_offset,
					(long) pp->p_offset + pp->p_size - 1,
					(long) pp->p_size
				);
			}

			if ((unsigned) pp->p_fstype < ARRAY_SIZE(xbsd_fstypes)-1)
				printf("%8.8s", xbsd_fstypes[pp->p_fstype]);
			else
				printf("%8x", pp->p_fstype);

			switch (pp->p_fstype) {
			case BSD_FS_UNUSED:
				printf("    %5lu %5lu %5.5s ",
					(long) pp->p_fsize, (long) pp->p_fsize * pp->p_frag, "");
				break;
			case BSD_FS_BSDFFS:
				printf("    %5lu %5lu %5u ",
					(long) pp->p_fsize, (long) pp->p_fsize * pp->p_frag, pp->p_cpg);
				break;
			default:
				printf("%22.22s", "");
				break;
			}
			bb_putchar('\n');
		}
	}
}

static void
xbsd_write_disklabel(void)
{
#if defined(__alpha__)
	printf("Writing disklabel to %s\n", disk_device);
	xbsd_writelabel(NULL);
#else
	printf("Writing disklabel to %s\n",
		partname(disk_device, xbsd_part_index + 1, 0));
	xbsd_writelabel(xbsd_part);
#endif
	reread_partition_table(0);      /* no exit yet */
}

static int
xbsd_create_disklabel(void)
{
	char c;

#if defined(__alpha__)
	printf("%s contains no disklabel\n", disk_device);
#else
	printf("%s contains no disklabel\n",
		partname(disk_device, xbsd_part_index + 1, 0));
#endif

	while (1) {
		c = read_nonempty("Do you want to create a disklabel? (y/n) ");
		if ((c|0x20) == 'y') {
			if (xbsd_initlabel(
#if defined(__alpha__) || defined(__powerpc__) || defined(__hppa__) || \
	defined(__s390__) || defined(__s390x__)
				NULL
#else
				xbsd_part
#endif
			) == 1) {
				xbsd_print_disklabel(1);
				return 1;
			}
			return 0;
		}
		if ((c|0x20) == 'n')
			return 0;
	}
}

static int
edit_int(int def, const char *mesg)
{
	mesg = xasprintf("%s (%u): ", mesg, def);
	do {
		if (!read_line(mesg))
			goto ret;
	} while (!isdigit(*line_ptr));
	def = atoi(line_ptr);
 ret:
	free((char*)mesg);
	return def;
}

static void
xbsd_edit_disklabel(void)
{
	struct xbsd_disklabel *d;

	d = &xbsd_dlabel;

#if defined(__alpha__) || defined(__ia64__)
	d->d_secsize    = edit_int(d->d_secsize     , "bytes/sector");
	d->d_nsectors   = edit_int(d->d_nsectors    , "sectors/track");
	d->d_ntracks    = edit_int(d->d_ntracks     , "tracks/cylinder");
	d->d_ncylinders = edit_int(d->d_ncylinders  , "cylinders");
#endif

	/* d->d_secpercyl can be != d->d_nsectors * d->d_ntracks */
	while (1) {
		d->d_secpercyl = edit_int(d->d_nsectors * d->d_ntracks,
				"sectors/cylinder");
		if (d->d_secpercyl <= d->d_nsectors * d->d_ntracks)
			break;

		printf("Must be <= sectors/track * tracks/cylinder (default)\n");
	}
	d->d_rpm        = edit_int(d->d_rpm       , "rpm");
	d->d_interleave = edit_int(d->d_interleave, "interleave");
	d->d_trackskew  = edit_int(d->d_trackskew , "trackskew");
	d->d_cylskew    = edit_int(d->d_cylskew   , "cylinderskew");
	d->d_headswitch = edit_int(d->d_headswitch, "headswitch");
	d->d_trkseek    = edit_int(d->d_trkseek   , "track-to-track seek");

	d->d_secperunit = d->d_secpercyl * d->d_ncylinders;
}

static int
xbsd_get_bootstrap(char *path, void *ptr, int size)
{
	int fdb;

	fdb = open_or_warn(path, O_RDONLY);
	if (fdb < 0) {
		return 0;
	}
	if (full_read(fdb, ptr, size) < 0) {
		bb_simple_perror_msg(path);
		close(fdb);
		return 0;
	}
	printf(" ... %s\n", path);
	close(fdb);
	return 1;
}

static void
sync_disks(void)
{
	printf("Syncing disks\n");
	sync();
	/* sleep(4); What? */
}

static void
xbsd_write_bootstrap(void)
{
#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif
	char path[MAXPATHLEN];
	const char *bootdir = BSD_LINUX_BOOTDIR;
	const char *dkbasename;
	struct xbsd_disklabel dl;
	char *d, *p, *e;
	int sector;

	if (xbsd_dlabel.d_type == BSD_DTYPE_SCSI)
		dkbasename = "sd";
	else
		dkbasename = "wd";

	snprintf(path, sizeof(path), "Bootstrap: %sboot -> boot%s (%s): ",
		dkbasename, dkbasename, dkbasename);
	if (read_line(path)) {
		dkbasename = line_ptr;
	}
	snprintf(path, sizeof(path), "%s/%sboot", bootdir, dkbasename);
	if (!xbsd_get_bootstrap(path, disklabelbuffer, (int) xbsd_dlabel.d_secsize))
		return;

/* We need a backup of the disklabel (xbsd_dlabel might have changed). */
	d = &disklabelbuffer[BSD_LABELSECTOR * SECTOR_SIZE];
	memmove(&dl, d, sizeof(struct xbsd_disklabel));

/* The disklabel will be overwritten by 0's from bootxx anyway */
	memset(d, 0, sizeof(struct xbsd_disklabel));

	snprintf(path, sizeof(path), "%s/boot%s", bootdir, dkbasename);
	if (!xbsd_get_bootstrap(path, &disklabelbuffer[xbsd_dlabel.d_secsize],
			(int) xbsd_dlabel.d_bbsize - xbsd_dlabel.d_secsize))
		return;

	e = d + sizeof(struct xbsd_disklabel);
	for (p = d; p < e; p++)
		if (*p) {
			printf("Bootstrap overlaps with disk label!\n");
			exit(EXIT_FAILURE);
		}

	memmove(d, &dl, sizeof(struct xbsd_disklabel));

#if defined(__powerpc__) || defined(__hppa__)
	sector = 0;
#elif defined(__alpha__)
	sector = 0;
	alpha_bootblock_checksum(disklabelbuffer);
#else
	sector = get_start_sect(xbsd_part);
#endif

	seek_sector(sector);
	xwrite(dev_fd, disklabelbuffer, BSD_BBSIZE);

#if defined(__alpha__)
	printf("Bootstrap installed on %s\n", disk_device);
#else
	printf("Bootstrap installed on %s\n",
		partname(disk_device, xbsd_part_index+1, 0));
#endif

	sync_disks();
}

static void
xbsd_change_fstype(void)
{
	int i;

	i = xbsd_get_part_index(xbsd_dlabel.d_npartitions);
	xbsd_dlabel.d_partitions[i].p_fstype = read_hex(xbsd_fstypes);
}

static int
xbsd_get_part_index(int max)
{
	char prompt[sizeof("Partition (a-%c): ") + 16];
	char l;

	snprintf(prompt, sizeof(prompt), "Partition (a-%c): ", 'a' + max - 1);
	do
		l = tolower(read_nonempty(prompt));
	while (l < 'a' || l > 'a' + max - 1);
	return l - 'a';
}

static int
xbsd_check_new_partition(int *i)
{
	/* room for more? various BSD flavours have different maxima */
	if (xbsd_dlabel.d_npartitions == BSD_MAXPARTITIONS) {
		int t;

		for (t = 0; t < BSD_MAXPARTITIONS; t++)
			if (xbsd_dlabel.d_partitions[t].p_size == 0)
				break;

		if (t == BSD_MAXPARTITIONS) {
			printf("The maximum number of partitions has been created\n");
			return 0;
		}
	}

	*i = xbsd_get_part_index(BSD_MAXPARTITIONS);

	if (*i >= xbsd_dlabel.d_npartitions)
		xbsd_dlabel.d_npartitions = (*i) + 1;

	if (xbsd_dlabel.d_partitions[*i].p_size != 0) {
		printf("This partition already exists\n");
		return 0;
	}

	return 1;
}

static void
xbsd_list_types(void)
{
	list_types(xbsd_fstypes);
}

static uint16_t
xbsd_dkcksum(struct xbsd_disklabel *lp)
{
	uint16_t *start, *end;
	uint16_t sum = 0;

	start = (uint16_t *) lp;
	end = (uint16_t *) &lp->d_partitions[lp->d_npartitions];
	while (start < end)
		sum ^= *start++;
	return sum;
}

static int
xbsd_initlabel(struct partition *p)
{
	struct xbsd_disklabel *d = &xbsd_dlabel;
	struct xbsd_partition *pp;

	get_geometry();
	memset(d, 0, sizeof(struct xbsd_disklabel));

	d->d_magic = BSD_DISKMAGIC;

	if (is_prefixed_with(disk_device, "/dev/sd"))
		d->d_type = BSD_DTYPE_SCSI;
	else
		d->d_type = BSD_DTYPE_ST506;

#if !defined(__alpha__)
	d->d_flags = BSD_D_DOSPART;
#else
	d->d_flags = 0;
#endif
	d->d_secsize = SECTOR_SIZE;           /* bytes/sector  */
	d->d_nsectors = g_sectors;            /* sectors/track */
	d->d_ntracks = g_heads;               /* tracks/cylinder (heads) */
	d->d_ncylinders = g_cylinders;
	d->d_secpercyl  = g_sectors * g_heads;/* sectors/cylinder */
	if (d->d_secpercyl == 0)
		d->d_secpercyl = 1;           /* avoid segfaults */
	d->d_secperunit = d->d_secpercyl * d->d_ncylinders;

	d->d_rpm = 3600;
	d->d_interleave = 1;
	d->d_trackskew = 0;
	d->d_cylskew = 0;
	d->d_headswitch = 0;
	d->d_trkseek = 0;

	d->d_magic2 = BSD_DISKMAGIC;
	d->d_bbsize = BSD_BBSIZE;
	d->d_sbsize = BSD_SBSIZE;

#if !defined(__alpha__)
	d->d_npartitions = 4;
	pp = &d->d_partitions[2]; /* Partition C should be NetBSD partition */

	pp->p_offset = get_start_sect(p);
	pp->p_size   = get_nr_sects(p);
	pp->p_fstype = BSD_FS_UNUSED;
	pp = &d->d_partitions[3]; /* Partition D should be whole disk */

	pp->p_offset = 0;
	pp->p_size   = d->d_secperunit;
	pp->p_fstype = BSD_FS_UNUSED;
#else
	d->d_npartitions = 3;
	pp = &d->d_partitions[2]; /* Partition C should be the whole disk */
	pp->p_offset = 0;
	pp->p_size   = d->d_secperunit;
	pp->p_fstype = BSD_FS_UNUSED;
#endif

	return 1;
}

/*
 * Read a xbsd_disklabel from sector 0 or from the starting sector of p.
 * If it has the right magic, return 1.
 */
static int
xbsd_readlabel(struct partition *p)
{
	struct xbsd_disklabel *d;
	int t, sector;

	if (!bsd_globals_ptr)
		bsd_globals_ptr = xzalloc(sizeof(*bsd_globals_ptr));

	d = &xbsd_dlabel;

	/* p is used only to get the starting sector */
#if !defined(__alpha__)
	sector = (p ? get_start_sect(p) : 0);
#else
	sector = 0;
#endif

	seek_sector(sector);
	if (BSD_BBSIZE != full_read(dev_fd, disklabelbuffer, BSD_BBSIZE))
		fdisk_fatal(unable_to_read);

	memmove(d, &disklabelbuffer[BSD_LABELSECTOR * SECTOR_SIZE + BSD_LABELOFFSET],
			sizeof(struct xbsd_disklabel));

	if (d->d_magic != BSD_DISKMAGIC || d->d_magic2 != BSD_DISKMAGIC)
		return 0;

	for (t = d->d_npartitions; t < BSD_MAXPARTITIONS; t++) {
		d->d_partitions[t].p_size   = 0;
		d->d_partitions[t].p_offset = 0;
		d->d_partitions[t].p_fstype = BSD_FS_UNUSED;
	}

	if (d->d_npartitions > BSD_MAXPARTITIONS)
		printf("Warning: too many partitions (%u, maximum is %u)\n",
			d->d_npartitions, BSD_MAXPARTITIONS);
	return 1;
}

static int
xbsd_writelabel(struct partition *p)
{
	struct xbsd_disklabel *d = &xbsd_dlabel;
	unsigned int sector;

#if !defined(__alpha__) && !defined(__powerpc__) && !defined(__hppa__)
	sector = get_start_sect(p) + BSD_LABELSECTOR;
#else
	(void)p; /* silence warning */
	sector = BSD_LABELSECTOR;
#endif

	d->d_checksum = 0;
	d->d_checksum = xbsd_dkcksum(d);

	/* This is necessary if we want to write the bootstrap later,
	   otherwise we'd write the old disklabel with the bootstrap.
	*/
	memmove(&disklabelbuffer[BSD_LABELSECTOR * SECTOR_SIZE + BSD_LABELOFFSET],
		d, sizeof(struct xbsd_disklabel));

#if defined(__alpha__) && BSD_LABELSECTOR == 0
	alpha_bootblock_checksum(disklabelbuffer);
	seek_sector(0);
	xwrite(dev_fd, disklabelbuffer, BSD_BBSIZE);
#else
	seek_sector(sector);
	lseek(dev_fd, BSD_LABELOFFSET, SEEK_CUR);
	xwrite(dev_fd, d, sizeof(*d));
#endif
	sync_disks();
	return 1;
}


#if !defined(__alpha__)
static int
xbsd_translate_fstype(int linux_type)
{
	switch (linux_type) {
	case 0x01: /* DOS 12-bit FAT   */
	case 0x04: /* DOS 16-bit <32M  */
	case 0x06: /* DOS 16-bit >=32M */
	case 0xe1: /* DOS access       */
	case 0xe3: /* DOS R/O          */
	case 0xf2: /* DOS secondary    */
		return BSD_FS_MSDOS;
	case 0x07: /* OS/2 HPFS        */
		return BSD_FS_HPFS;
	default:
		return BSD_FS_OTHER;
	}
}

static void
xbsd_link_part(void)
{
	int k, i;
	struct partition *p;

	k = get_partition(1, g_partitions);

	if (!xbsd_check_new_partition(&i))
		return;

	p = get_part_table(k);

	xbsd_dlabel.d_partitions[i].p_size   = get_nr_sects(p);
	xbsd_dlabel.d_partitions[i].p_offset = get_start_sect(p);
	xbsd_dlabel.d_partitions[i].p_fstype = xbsd_translate_fstype(p->sys_ind);
}
#endif

#if defined(__alpha__)
static void
alpha_bootblock_checksum(char *boot)
{
	uint64_t *dp, sum;
	int i;

	dp = (uint64_t *)boot;
	sum = 0;
	for (i = 0; i < 63; i++)
		sum += dp[i];
	dp[63] = sum;
}
#endif /* __alpha__ */

/* Undefine 'global' tricks */
#undef disklabelbuffer
#undef xbsd_dlabel

#endif /* OSF_LABEL */

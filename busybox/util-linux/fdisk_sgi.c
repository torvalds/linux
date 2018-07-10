/*
 * Copyright (C) Andreas Neuper, Sep 1998.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#if ENABLE_FEATURE_SGI_LABEL

#define SGI_DEBUG 0

#define SGI_VOLHDR      0x00
/* 1 and 2 were used for drive types no longer supported by SGI */
#define SGI_SWAP        0x03
/* 4 and 5 were for filesystem types SGI haven't ever supported on MIPS CPUs */
#define SGI_VOLUME      0x06
#define SGI_EFS         0x07
#define SGI_LVOL        0x08
#define SGI_RLVOL       0x09
#define SGI_XFS         0x0a
#define SGI_XFSLOG      0x0b
#define SGI_XLV         0x0c
#define SGI_XVM         0x0d
#define SGI_ENTIRE_DISK SGI_VOLUME

struct device_parameter { /* 48 bytes */
	unsigned char  skew;
	unsigned char  gap1;
	unsigned char  gap2;
	unsigned char  sparecyl;
	unsigned short pcylcount;
	unsigned short head_vol0;
	unsigned short ntrks;   /* tracks in cyl 0 or vol 0 */
	unsigned char  cmd_tag_queue_depth;
	unsigned char  unused0;
	unsigned short unused1;
	unsigned short nsect;   /* sectors/tracks in cyl 0 or vol 0 */
	unsigned short bytes;
	unsigned short ilfact;
	unsigned int   flags;   /* controller flags */
	unsigned int   datarate;
	unsigned int   retries_on_error;
	unsigned int   ms_per_word;
	unsigned short xylogics_gap1;
	unsigned short xylogics_syncdelay;
	unsigned short xylogics_readdelay;
	unsigned short xylogics_gap2;
	unsigned short xylogics_readgate;
	unsigned short xylogics_writecont;
};

/*
 * controller flags
 */
#define SECTOR_SLIP     0x01
#define SECTOR_FWD      0x02
#define TRACK_FWD       0x04
#define TRACK_MULTIVOL  0x08
#define IGNORE_ERRORS   0x10
#define RESEEK          0x20
#define ENABLE_CMDTAGQ  0x40

typedef struct {
	unsigned int   magic;            /* expect SGI_LABEL_MAGIC */
	unsigned short boot_part;        /* active boot partition */
	unsigned short swap_part;        /* active swap partition */
	unsigned char  boot_file[16];    /* name of the bootfile */
	struct device_parameter devparam;       /*  1 * 48 bytes */
	struct volume_directory {               /* 15 * 16 bytes */
		unsigned char vol_file_name[8]; /* a character array */
		unsigned int  vol_file_start;   /* number of logical block */
		unsigned int  vol_file_size;    /* number of bytes */
	} directory[15];
	struct sgi_partinfo {                   /* 16 * 12 bytes */
		unsigned int num_sectors;       /* number of blocks */
		unsigned int start_sector;      /* must be cylinder aligned */
		unsigned int id;
	} partitions[16];
	unsigned int   csum;
	unsigned int   fillbytes;
} sgi_partition;

typedef struct {
	unsigned int   magic;           /* looks like a magic number */
	unsigned int   a2;
	unsigned int   a3;
	unsigned int   a4;
	unsigned int   b1;
	unsigned short b2;
	unsigned short b3;
	unsigned int   c[16];
	unsigned short d[3];
	unsigned char  scsi_string[50];
	unsigned char  serial[137];
	unsigned short check1816;
	unsigned char  installer[225];
} sgiinfo;

#define SGI_LABEL_MAGIC         0x0be5a941
#define SGI_LABEL_MAGIC_SWAPPED 0x41a9e50b
#define SGI_INFO_MAGIC          0x00072959
#define SGI_INFO_MAGIC_SWAPPED  0x59290700

#define SGI_SSWAP16(x) (sgi_other_endian ? fdisk_swap16(x) : (uint16_t)(x))
#define SGI_SSWAP32(x) (sgi_other_endian ? fdisk_swap32(x) : (uint32_t)(x))

#define sgilabel ((sgi_partition *)MBRbuffer)
#define sgiparam (sgilabel->devparam)

/*
 *
 * fdisksgilabel.c
 *
 * Copyright (C) Andreas Neuper, Sep 1998.
 *      This file may be modified and redistributed under
 *      the terms of the GNU Public License.
 *
 * Sat Mar 20 EST 1999 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *      Internationalization
 */


static smallint sgi_other_endian; /* bool */
static smallint sgi_volumes = 1; /* max 15 */

/*
 * only dealing with free blocks here
 */

typedef struct {
	unsigned int first;
	unsigned int last;
} freeblocks;
static freeblocks freelist[17]; /* 16 partitions can produce 17 vacant slots */

static void
setfreelist(int i, unsigned int f, unsigned int l)
{
	freelist[i].first = f;
	freelist[i].last = l;
}

static void
add2freelist(unsigned int f, unsigned int l)
{
	int i;
	for (i = 0; i < 17; i++)
		if (freelist[i].last == 0)
			break;
	setfreelist(i, f, l);
}

static void
clearfreelist(void)
{
	int i;

	for (i = 0; i < 17; i++)
		setfreelist(i, 0, 0);
}

static unsigned int
isinfreelist(unsigned int b)
{
	int i;

	for (i = 0; i < 17; i++)
		if (freelist[i].first <= b && freelist[i].last >= b)
			return freelist[i].last;
	return 0;
}
	/* return last vacant block of this stride (never 0). */
	/* the '>=' is not quite correct, but simplifies the code */
/*
 * end of free blocks section
 */

static const char *const sgi_sys_types[] = {
/* SGI_VOLHDR   */	"\x00" "SGI volhdr"  ,
/* 0x01         */	"\x01" "SGI trkrepl" ,
/* 0x02         */	"\x02" "SGI secrepl" ,
/* SGI_SWAP     */	"\x03" "SGI raw"     ,
/* 0x04         */	"\x04" "SGI bsd"     ,
/* 0x05         */	"\x05" "SGI sysv"    ,
/* SGI_ENTIRE_DISK  */	"\x06" "SGI volume"  ,
/* SGI_EFS      */	"\x07" "SGI efs"     ,
/* 0x08         */	"\x08" "SGI lvol"    ,
/* 0x09         */	"\x09" "SGI rlvol"   ,
/* SGI_XFS      */	"\x0a" "SGI xfs"     ,
/* SGI_XFSLOG   */	"\x0b" "SGI xfslog"  ,
/* SGI_XLV      */	"\x0c" "SGI xlv"     ,
/* SGI_XVM      */	"\x0d" "SGI xvm"     ,
/* LINUX_SWAP   */	"\x82" "Linux swap"  ,
/* LINUX_NATIVE */	"\x83" "Linux native",
/* LINUX_LVM    */	"\x8d" "Linux LVM"   ,
/* LINUX_RAID   */	"\xfd" "Linux RAID"  ,
			NULL
};


static int
sgi_get_nsect(void)
{
	return SGI_SSWAP16(sgilabel->devparam.nsect);
}

static int
sgi_get_ntrks(void)
{
	return SGI_SSWAP16(sgilabel->devparam.ntrks);
}

static unsigned int
two_s_complement_32bit_sum(unsigned int* base, int size /* in bytes */)
{
	int i = 0;
	unsigned int sum = 0;

	size /= sizeof(unsigned int);
	for (i = 0; i < size; i++)
		sum -= SGI_SSWAP32(base[i]);
	return sum;
}

void BUG_bad_sgi_partition_size(void);

static int
check_sgi_label(void)
{
	if (sizeof(sgi_partition) > 512) {
		/* According to MIPS Computer Systems, Inc the label
		 * must not contain more than 512 bytes */
		BUG_bad_sgi_partition_size();
	}

	if (sgilabel->magic != SGI_LABEL_MAGIC
	 && sgilabel->magic != SGI_LABEL_MAGIC_SWAPPED
	) {
		current_label_type = LABEL_DOS;
		return 0;
	}

	sgi_other_endian = (sgilabel->magic == SGI_LABEL_MAGIC_SWAPPED);
	/*
	 * test for correct checksum
	 */
	if (two_s_complement_32bit_sum((unsigned int*)sgilabel,
				sizeof(*sgilabel))) {
		printf("Detected sgi disklabel with wrong checksum\n");
	}
	update_units();
	current_label_type = LABEL_SGI;
	g_partitions = 16;
	sgi_volumes = 15;
	return 1;
}

static unsigned int
sgi_get_start_sector(int i)
{
	return SGI_SSWAP32(sgilabel->partitions[i].start_sector);
}

static unsigned int
sgi_get_num_sectors(int i)
{
	return SGI_SSWAP32(sgilabel->partitions[i].num_sectors);
}

static int
sgi_get_sysid(int i)
{
	return SGI_SSWAP32(sgilabel->partitions[i].id);
}

static int
sgi_get_bootpartition(void)
{
	return SGI_SSWAP16(sgilabel->boot_part);
}

static int
sgi_get_swappartition(void)
{
	return SGI_SSWAP16(sgilabel->swap_part);
}

static void
sgi_list_table(int xtra)
{
	int i, w, wd;
	int kpi = 0;                /* kernel partition ID */

	if (xtra) {
		printf("\nDisk %s (SGI disk label): %u heads, %u sectors\n"
			"%u cylinders, %u physical cylinders\n"
			"%u extra sects/cyl, interleave %u:1\n"
			"%s\n"
			"Units = %s of %u * 512 bytes\n\n",
			disk_device, g_heads, g_sectors, g_cylinders,
			SGI_SSWAP16(sgiparam.pcylcount),
			SGI_SSWAP16(sgiparam.sparecyl),
			SGI_SSWAP16(sgiparam.ilfact),
			(char *)sgilabel,
			str_units(PLURAL), units_per_sector);
	} else {
		printf("\nDisk %s (SGI disk label): "
			"%u heads, %u sectors, %u cylinders\n"
			"Units = %s of %u * 512 bytes\n\n",
			disk_device, g_heads, g_sectors, g_cylinders,
			str_units(PLURAL), units_per_sector );
	}

	w = strlen(disk_device);
	wd = sizeof("Device") - 1;
	if (w < wd)
	w = wd;

	printf("----- partitions -----\n"
		"Pt# %*s  Info     Start       End   Sectors  Id  System\n",
		w + 2, "Device");
	for (i = 0; i < g_partitions; i++) {
		if (sgi_get_num_sectors(i) || SGI_DEBUG) {
			uint32_t start = sgi_get_start_sector(i);
			uint32_t len = sgi_get_num_sectors(i);
			kpi++;              /* only count nonempty partitions */
			printf(
			"%2u: %s %4s %9lu %9lu %9lu  %2x  %s\n",
/* fdisk part number */	i+1,
/* device */            partname(disk_device, kpi, w+3),
/* flags */             (sgi_get_swappartition() == i) ? "swap" :
/* flags */             (sgi_get_bootpartition() == i) ? "boot" : "    ",
/* start */             (long) scround(start),
/* end */               (long) scround(start+len)-1,
/* no odd flag on end */(long) len,
/* type id */           sgi_get_sysid(i),
/* type name */         partition_type(sgi_get_sysid(i)));
		}
	}
	printf("----- Bootinfo -----\nBootfile: %s\n"
		"----- Directory Entries -----\n",
		sgilabel->boot_file);
	for (i = 0; i < sgi_volumes; i++) {
		if (sgilabel->directory[i].vol_file_size) {
			uint32_t start = SGI_SSWAP32(sgilabel->directory[i].vol_file_start);
			uint32_t len = SGI_SSWAP32(sgilabel->directory[i].vol_file_size);
			unsigned char *name = sgilabel->directory[i].vol_file_name;

			printf("%2u: %-10s sector%5u size%8u\n",
				i, (char*)name, (unsigned int) start, (unsigned int) len);
		}
	}
}

static void
sgi_set_bootpartition(int i)
{
	sgilabel->boot_part = SGI_SSWAP16(((short)i));
}

static unsigned int
sgi_get_lastblock(void)
{
	return g_heads * g_sectors * g_cylinders;
}

static void
sgi_set_swappartition(int i)
{
	sgilabel->swap_part = SGI_SSWAP16(((short)i));
}

static int
sgi_check_bootfile(const char* aFile)
{
	if (strlen(aFile) < 3) /* "/a\n" is minimum */ {
		printf("\nInvalid Bootfile!\n"
			"\tThe bootfile must be an absolute non-zero pathname,\n"
			"\te.g. \"/unix\" or \"/unix.save\".\n");
		return 0;
	}
	if (strlen(aFile) > 16) {
		printf("\nName of Bootfile too long (>16 bytes)\n");
		return 0;
	}
	if (aFile[0] != '/') {
		printf("\nBootfile must have a fully qualified pathname\n");
		return 0;
	}
	if (strncmp(aFile, (char*)sgilabel->boot_file, 16)) {
		printf("\nBe aware, that the bootfile is not checked for existence.\n"
			 "\tSGI's default is \"/unix\" and for backup \"/unix.save\".\n");
		/* filename is correct and did change */
		return 1;
	}
	return 0;   /* filename did not change */
}

static const char *
sgi_get_bootfile(void)
{
	return (char*)sgilabel->boot_file;
}

static void
sgi_set_bootfile(const char* aFile)
{
	int i = 0;

	if (sgi_check_bootfile(aFile)) {
		while (i < 16) {
			if ((aFile[i] != '\n')  /* in principle caught again by next line */
			 && (strlen(aFile) > i))
				sgilabel->boot_file[i] = aFile[i];
			else
				sgilabel->boot_file[i] = 0;
			i++;
		}
		printf("\n\tBootfile is changed to \"%s\"\n", sgilabel->boot_file);
	}
}

static void
create_sgiinfo(void)
{
	/* I keep SGI's habit to write the sgilabel to the second block */
	sgilabel->directory[0].vol_file_start = SGI_SSWAP32(2);
	sgilabel->directory[0].vol_file_size = SGI_SSWAP32(sizeof(sgiinfo));
	memcpy((char*)sgilabel->directory[0].vol_file_name, "sgilabel", 8);
}

static sgiinfo *fill_sgiinfo(void);

static void
sgi_write_table(void)
{
	sgilabel->csum = 0;
	sgilabel->csum = SGI_SSWAP32(two_s_complement_32bit_sum(
			(unsigned int*)sgilabel, sizeof(*sgilabel)));
	assert(two_s_complement_32bit_sum(
		(unsigned int*)sgilabel, sizeof(*sgilabel)) == 0);

	write_sector(0, sgilabel);
	if (is_prefixed_with((char*)sgilabel->directory[0].vol_file_name, "sgilabel")) {
		/*
		 * keep this habit of first writing the "sgilabel".
		 * I never tested whether it works without (AN 981002).
		 */
		sgiinfo *info = fill_sgiinfo();
		int infostartblock = SGI_SSWAP32(sgilabel->directory[0].vol_file_start);
		write_sector(infostartblock, info);
		free(info);
	}
}

static int
compare_start(int *x, int *y)
{
	/*
	 * sort according to start sectors
	 * and prefers largest partition:
	 * entry zero is entire disk entry
	 */
	unsigned int i = *x;
	unsigned int j = *y;
	unsigned int a = sgi_get_start_sector(i);
	unsigned int b = sgi_get_start_sector(j);
	unsigned int c = sgi_get_num_sectors(i);
	unsigned int d = sgi_get_num_sectors(j);

	if (a == b)
		return (d > c) ? 1 : (d == c) ? 0 : -1;
	return (a > b) ? 1 : -1;
}


static int
verify_sgi(int verbose)
{
	int Index[16];      /* list of valid partitions */
	int sortcount = 0;  /* number of used partitions, i.e. non-zero lengths */
	int entire = 0, i = 0;
	unsigned int start = 0;
	long long gap = 0;      /* count unused blocks */
	unsigned int lastblock = sgi_get_lastblock();

	clearfreelist();
	for (i = 0; i < 16; i++) {
		if (sgi_get_num_sectors(i) != 0) {
			Index[sortcount++] = i;
			if (sgi_get_sysid(i) == SGI_ENTIRE_DISK) {
				if (entire++ == 1) {
					if (verbose)
						printf("More than one entire disk entry present\n");
				}
			}
		}
	}
	if (sortcount == 0) {
		if (verbose)
			printf("No partitions defined\n");
		return (lastblock > 0) ? 1 : (lastblock == 0) ? 0 : -1;
	}
	qsort(Index, sortcount, sizeof(Index[0]), (void*)compare_start);
	if (sgi_get_sysid(Index[0]) == SGI_ENTIRE_DISK) {
		if ((Index[0] != 10) && verbose)
			printf("IRIX likes when Partition 11 covers the entire disk\n");
		if ((sgi_get_start_sector(Index[0]) != 0) && verbose) {
			printf("The entire disk partition should start "
				"at block 0,\n"
				"not at diskblock %u\n",
				sgi_get_start_sector(Index[0]));
		}
		if (SGI_DEBUG) {     /* I do not understand how some disks fulfil it */
			if ((sgi_get_num_sectors(Index[0]) != lastblock) && verbose)
				printf("The entire disk partition is only %u diskblock large,\n"
					"but the disk is %u diskblocks long\n",
					sgi_get_num_sectors(Index[0]), lastblock);
			lastblock = sgi_get_num_sectors(Index[0]);
		}
	} else {
		if (verbose)
			printf("One Partition (#11) should cover the entire disk\n");
		if (SGI_DEBUG > 2)
			printf("sysid=%u\tpartition=%u\n",
				sgi_get_sysid(Index[0]), Index[0]+1);
	}
	for (i = 1, start = 0; i < sortcount; i++) {
		int cylsize = sgi_get_nsect() * sgi_get_ntrks();

		if ((sgi_get_start_sector(Index[i]) % cylsize) != 0) {
			if (SGI_DEBUG)      /* I do not understand how some disks fulfil it */
				if (verbose)
					printf("Partition %u does not start on cylinder boundary\n",
						Index[i]+1);
		}
		if (sgi_get_num_sectors(Index[i]) % cylsize != 0) {
			if (SGI_DEBUG)      /* I do not understand how some disks fulfil it */
				if (verbose)
					printf("Partition %u does not end on cylinder boundary\n",
						Index[i]+1);
		}
		/* We cannot handle several "entire disk" entries. */
		if (sgi_get_sysid(Index[i]) == SGI_ENTIRE_DISK) continue;
		if (start > sgi_get_start_sector(Index[i])) {
			if (verbose)
				printf("Partitions %u and %u overlap by %u sectors\n",
					Index[i-1]+1, Index[i]+1,
					start - sgi_get_start_sector(Index[i]));
			if (gap > 0) gap = -gap;
			if (gap == 0) gap = -1;
		}
		if (start < sgi_get_start_sector(Index[i])) {
			if (verbose)
				printf("Unused gap of %u sectors - sectors %u-%u\n",
					sgi_get_start_sector(Index[i]) - start,
					start, sgi_get_start_sector(Index[i])-1);
			gap += sgi_get_start_sector(Index[i]) - start;
			add2freelist(start, sgi_get_start_sector(Index[i]));
		}
		start = sgi_get_start_sector(Index[i])
			   + sgi_get_num_sectors(Index[i]);
		if (SGI_DEBUG > 1) {
			if (verbose)
				printf("%2u:%12u\t%12u\t%12u\n", Index[i],
					sgi_get_start_sector(Index[i]),
					sgi_get_num_sectors(Index[i]),
					sgi_get_sysid(Index[i]));
		}
	}
	if (start < lastblock) {
		if (verbose)
			printf("Unused gap of %u sectors - sectors %u-%u\n",
				lastblock - start, start, lastblock-1);
		gap += lastblock - start;
		add2freelist(start, lastblock);
	}
	/*
	 * Done with arithmetics
	 * Go for details now
	 */
	if (verbose) {
		if (!sgi_get_num_sectors(sgi_get_bootpartition())) {
			printf("\nThe boot partition does not exist\n");
		}
		if (!sgi_get_num_sectors(sgi_get_swappartition())) {
			printf("\nThe swap partition does not exist\n");
		} else {
			if ((sgi_get_sysid(sgi_get_swappartition()) != SGI_SWAP)
			 && (sgi_get_sysid(sgi_get_swappartition()) != LINUX_SWAP))
				printf("\nThe swap partition has no swap type\n");
		}
		if (sgi_check_bootfile("/unix"))
			printf("\tYou have chosen an unusual boot file name\n");
	}
	return (gap > 0) ? 1 : (gap == 0) ? 0 : -1;
}

static int
sgi_gaps(void)
{
	/*
	 * returned value is:
	 *  = 0 : disk is properly filled to the rim
	 *  < 0 : there is an overlap
	 *  > 0 : there is still some vacant space
	 */
	return verify_sgi(0);
}

static void
sgi_change_sysid(int i, int sys)
{
	if (sgi_get_num_sectors(i) == 0) { /* caught already before, ... */
		printf("Sorry you may change the Tag of non-empty partitions\n");
		return;
	}
	if ((sys != SGI_ENTIRE_DISK) && (sys != SGI_VOLHDR)
	 && (sgi_get_start_sector(i) < 1)
	) {
		read_maybe_empty(
			"It is highly recommended that the partition at offset 0\n"
			"is of type \"SGI volhdr\", the IRIX system will rely on it to\n"
			"retrieve from its directory standalone tools like sash and fx.\n"
			"Only the \"SGI volume\" entire disk section may violate this.\n"
			"Type YES if you are sure about tagging this partition differently.\n");
		if (strcmp(line_ptr, "YES\n") != 0)
			return;
	}
	sgilabel->partitions[i].id = SGI_SSWAP32(sys);
}

/* returns partition index of first entry marked as entire disk */
static int
sgi_entire(void)
{
	int i;

	for (i = 0; i < 16; i++)
		if (sgi_get_sysid(i) == SGI_VOLUME)
			return i;
	return -1;
}

static void
sgi_set_partition(int i, unsigned int start, unsigned int length, int sys)
{
	sgilabel->partitions[i].id = SGI_SSWAP32(sys);
	sgilabel->partitions[i].num_sectors = SGI_SSWAP32(length);
	sgilabel->partitions[i].start_sector = SGI_SSWAP32(start);
	set_changed(i);
	if (sgi_gaps() < 0)     /* rebuild freelist */
		printf("Partition overlap detected\n");
}

static void
sgi_set_entire(void)
{
	int n;

	for (n = 10; n < g_partitions; n++) {
		if (!sgi_get_num_sectors(n) ) {
			sgi_set_partition(n, 0, sgi_get_lastblock(), SGI_VOLUME);
			break;
		}
	}
}

static void
sgi_set_volhdr(void)
{
	int n;

	for (n = 8; n < g_partitions; n++) {
		if (!sgi_get_num_sectors(n)) {
			/*
			 * 5 cylinders is an arbitrary value I like
			 * IRIX 5.3 stored files in the volume header
			 * (like sash, symmon, fx, ide) with ca. 3200
			 * sectors.
			 */
			if (g_heads * g_sectors * 5 < sgi_get_lastblock()) {
				sgi_set_partition(n, 0, g_heads * g_sectors * 5, SGI_VOLHDR);
				break;
			}
		}
	}
}

static void
sgi_delete_partition(int i)
{
	sgi_set_partition(i, 0, 0, 0);
}

static void
sgi_add_partition(int n, int sys)
{
	char mesg[256];
	unsigned int first = 0, last = 0;

	if (n == 10) {
		sys = SGI_VOLUME;
	} else if (n == 8) {
		sys = 0;
	}
	if (sgi_get_num_sectors(n)) {
		printf(msg_part_already_defined, n + 1);
		return;
	}
	if ((sgi_entire() == -1) && (sys != SGI_VOLUME)) {
		printf("Attempting to generate entire disk entry automatically\n");
		sgi_set_entire();
		sgi_set_volhdr();
	}
	if ((sgi_gaps() == 0) && (sys != SGI_VOLUME)) {
		printf("The entire disk is already covered with partitions\n");
		return;
	}
	if (sgi_gaps() < 0) {
		printf("You got a partition overlap on the disk. Fix it first!\n");
		return;
	}
	snprintf(mesg, sizeof(mesg), "First %s", str_units(SINGULAR));
	while (1) {
		if (sys == SGI_VOLUME) {
			last = sgi_get_lastblock();
			first = read_int(0, 0, last-1, 0, mesg);
			if (first != 0) {
				printf("It is highly recommended that eleventh partition\n"
						"covers the entire disk and is of type 'SGI volume'\n");
			}
		} else {
			first = freelist[0].first;
			last  = freelist[0].last;
			first = read_int(scround(first), scround(first), scround(last)-1,
				0, mesg);
		}
		if (display_in_cyl_units)
			first *= units_per_sector;
		else
			first = first; /* align to cylinder if you know how ... */
		if (!last )
			last = isinfreelist(first);
		if (last != 0)
			break;
		printf("You will get a partition overlap on the disk. "
				"Fix it first!\n");
	}
	snprintf(mesg, sizeof(mesg), " Last %s", str_units(SINGULAR));
	last = read_int(scround(first), scround(last)-1, scround(last)-1,
			scround(first), mesg)+1;
	if (display_in_cyl_units)
		last *= units_per_sector;
	else
		last = last; /* align to cylinder if You know how ... */
	if ( (sys == SGI_VOLUME) && (first != 0 || last != sgi_get_lastblock() ) )
		printf("It is highly recommended that eleventh partition\n"
			"covers the entire disk and is of type 'SGI volume'\n");
	sgi_set_partition(n, first, last-first, sys);
}

#if ENABLE_FEATURE_FDISK_ADVANCED
static void
create_sgilabel(void)
{
	struct hd_geometry geometry;
	struct {
		unsigned int start;
		unsigned int nsect;
		int sysid;
	} old[4];
	int i = 0;
	long longsectors;               /* the number of sectors on the device */
	int res;                        /* the result from the ioctl */
	int sec_fac;                    /* the sector factor */

	sec_fac = sector_size / 512;    /* determine the sector factor */

	printf(msg_building_new_label, "SGI disklabel");

	sgi_other_endian = BB_LITTLE_ENDIAN;
	res = ioctl(dev_fd, BLKGETSIZE, &longsectors);
	if (!ioctl(dev_fd, HDIO_GETGEO, &geometry)) {
		g_heads = geometry.heads;
		g_sectors = geometry.sectors;
		if (res == 0) {
			/* the get device size ioctl was successful */
			g_cylinders = longsectors / (g_heads * g_sectors);
			g_cylinders /= sec_fac;
		} else {
			/* otherwise print error and use truncated version */
			g_cylinders = geometry.cylinders;
			printf(
"Warning: BLKGETSIZE ioctl failed on %s.  Using geometry cylinder value of %u.\n"
"This value may be truncated for devices > 33.8 GB.\n", disk_device, g_cylinders);
		}
	}
	for (i = 0; i < 4; i++) {
		old[i].sysid = 0;
		if (valid_part_table_flag(MBRbuffer)) {
			if (get_part_table(i)->sys_ind) {
				old[i].sysid = get_part_table(i)->sys_ind;
				old[i].start = get_start_sect(get_part_table(i));
				old[i].nsect = get_nr_sects(get_part_table(i));
				printf("Trying to keep parameters of partition %u\n", i);
				if (SGI_DEBUG)
					printf("ID=%02x\tSTART=%u\tLENGTH=%u\n",
				old[i].sysid, old[i].start, old[i].nsect);
			}
		}
	}

	memset(MBRbuffer, 0, sizeof(MBRbuffer));
	/* fields with '//' are already zeroed out by memset above */

	sgilabel->magic = SGI_SSWAP32(SGI_LABEL_MAGIC);
	//sgilabel->boot_part = SGI_SSWAP16(0);
	sgilabel->swap_part = SGI_SSWAP16(1);

	//memset(sgilabel->boot_file, 0, 16);
	strcpy((char*)sgilabel->boot_file, "/unix"); /* sizeof(sgilabel->boot_file) == 16 > 6 */

	//sgilabel->devparam.skew                     = (0);
	//sgilabel->devparam.gap1                     = (0);
	//sgilabel->devparam.gap2                     = (0);
	//sgilabel->devparam.sparecyl                 = (0);
	sgilabel->devparam.pcylcount                = SGI_SSWAP16(geometry.cylinders);
	//sgilabel->devparam.head_vol0                = SGI_SSWAP16(0);
	/* tracks/cylinder (heads) */
	sgilabel->devparam.ntrks                    = SGI_SSWAP16(geometry.heads);
	//sgilabel->devparam.cmd_tag_queue_depth      = (0);
	//sgilabel->devparam.unused0                  = (0);
	//sgilabel->devparam.unused1                  = SGI_SSWAP16(0);
	/* sectors/track */
	sgilabel->devparam.nsect                    = SGI_SSWAP16(geometry.sectors);
	sgilabel->devparam.bytes                    = SGI_SSWAP16(512);
	sgilabel->devparam.ilfact                   = SGI_SSWAP16(1);
	sgilabel->devparam.flags                    = SGI_SSWAP32(TRACK_FWD|
							IGNORE_ERRORS|RESEEK);
	//sgilabel->devparam.datarate                 = SGI_SSWAP32(0);
	sgilabel->devparam.retries_on_error         = SGI_SSWAP32(1);
	//sgilabel->devparam.ms_per_word              = SGI_SSWAP32(0);
	//sgilabel->devparam.xylogics_gap1            = SGI_SSWAP16(0);
	//sgilabel->devparam.xylogics_syncdelay       = SGI_SSWAP16(0);
	//sgilabel->devparam.xylogics_readdelay       = SGI_SSWAP16(0);
	//sgilabel->devparam.xylogics_gap2            = SGI_SSWAP16(0);
	//sgilabel->devparam.xylogics_readgate        = SGI_SSWAP16(0);
	//sgilabel->devparam.xylogics_writecont       = SGI_SSWAP16(0);
	//memset( &(sgilabel->directory), 0, sizeof(struct volume_directory)*15 );
	//memset( &(sgilabel->partitions), 0, sizeof(struct sgi_partinfo)*16 );
	current_label_type = LABEL_SGI;
	g_partitions = 16;
	sgi_volumes = 15;
	sgi_set_entire();
	sgi_set_volhdr();
	for (i = 0; i < 4; i++) {
		if (old[i].sysid) {
			sgi_set_partition(i, old[i].start, old[i].nsect, old[i].sysid);
		}
	}
}

static void
sgi_set_xcyl(void)
{
	/* do nothing in the beginning */
}
#endif /* FEATURE_FDISK_ADVANCED */

/* _____________________________________________________________
 */

static sgiinfo *
fill_sgiinfo(void)
{
	sgiinfo *info = xzalloc(sizeof(sgiinfo));

	info->magic = SGI_SSWAP32(SGI_INFO_MAGIC);
	info->b1 = SGI_SSWAP32(-1);
	info->b2 = SGI_SSWAP16(-1);
	info->b3 = SGI_SSWAP16(1);
	/* You may want to replace this string !!!!!!! */
	strcpy( (char*)info->scsi_string, "IBM OEM 0662S12         3 30" );
	strcpy( (char*)info->serial, "0000" );
	info->check1816 = SGI_SSWAP16(18*256 +16 );
	strcpy( (char*)info->installer, "Sfx version 5.3, Oct 18, 1994" );
	return info;
}
#endif /* SGI_LABEL */

/*
 * fdisk_sun.c
 *
 * I think this is mostly, or entirely, due to
 *      Jakub Jelinek (jj@sunsite.mff.cuni.cz), July 1996
 *
 * Merged with fdisk for other architectures, aeb, June 1998.
 *
 * Sat Mar 20 EST 1999 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *      Internationalization
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#if ENABLE_FEATURE_SUN_LABEL

#define SUNOS_SWAP 3
#define SUN_WHOLE_DISK 5

#define SUN_LABEL_MAGIC          0xDABE
#define SUN_LABEL_MAGIC_SWAPPED  0xBEDA
#define SUN_SSWAP16(x) (sun_other_endian ? fdisk_swap16(x) : (uint16_t)(x))
#define SUN_SSWAP32(x) (sun_other_endian ? fdisk_swap32(x) : (uint32_t)(x))

/* Copied from linux/major.h */
#define FLOPPY_MAJOR    2

#define SCSI_IOCTL_GET_IDLUN 0x5382

static smallint sun_other_endian;
static smallint scsi_disk;
static smallint floppy;

#ifndef IDE0_MAJOR
#define IDE0_MAJOR 3
#endif
#ifndef IDE1_MAJOR
#define IDE1_MAJOR 22
#endif

static void
guess_device_type(void)
{
	struct stat bootstat;

	if (fstat(dev_fd, &bootstat) < 0) {
		scsi_disk = 0;
		floppy = 0;
	} else if (S_ISBLK(bootstat.st_mode)
		&& (major(bootstat.st_rdev) == IDE0_MAJOR ||
		    major(bootstat.st_rdev) == IDE1_MAJOR)) {
		scsi_disk = 0;
		floppy = 0;
	} else if (S_ISBLK(bootstat.st_mode)
		&& major(bootstat.st_rdev) == FLOPPY_MAJOR) {
		scsi_disk = 0;
		floppy = 1;
	} else {
		scsi_disk = 1;
		floppy = 0;
	}
}

static const char *const sun_sys_types[] = {
	"\x00" "Empty"       , /* 0            */
	"\x01" "Boot"        , /* 1            */
	"\x02" "SunOS root"  , /* 2            */
	"\x03" "SunOS swap"  , /* SUNOS_SWAP   */
	"\x04" "SunOS usr"   , /* 4            */
	"\x05" "Whole disk"  , /* SUN_WHOLE_DISK   */
	"\x06" "SunOS stand" , /* 6            */
	"\x07" "SunOS var"   , /* 7            */
	"\x08" "SunOS home"  , /* 8            */
	"\x82" "Linux swap"  , /* LINUX_SWAP   */
	"\x83" "Linux native", /* LINUX_NATIVE */
	"\x8e" "Linux LVM"   , /* 0x8e         */
/* New (2.2.x) raid partition with autodetect using persistent superblock */
	"\xfd" "Linux raid autodetect", /* 0xfd         */
	NULL
};


static void
set_sun_partition(int i, unsigned start, unsigned stop, int sysid)
{
	sunlabel->infos[i].id = sysid;
	sunlabel->partitions[i].start_cylinder =
		SUN_SSWAP32(start / (g_heads * g_sectors));
	sunlabel->partitions[i].num_sectors =
		SUN_SSWAP32(stop - start);
	set_changed(i);
}

static int
check_sun_label(void)
{
	unsigned short *ush;
	int csum;

	if (sunlabel->magic != SUN_LABEL_MAGIC
	 && sunlabel->magic != SUN_LABEL_MAGIC_SWAPPED
	) {
		current_label_type = LABEL_DOS;
		sun_other_endian = 0;
		return 0;
	}
	sun_other_endian = (sunlabel->magic == SUN_LABEL_MAGIC_SWAPPED);
	ush = ((unsigned short *) (sunlabel + 1)) - 1;
	for (csum = 0; ush >= (unsigned short *)sunlabel;) csum ^= *ush--;
	if (csum) {
		printf("Detected sun disklabel with wrong checksum.\n"
"Probably you'll have to set all the values,\n"
"e.g. heads, sectors, cylinders and partitions\n"
"or force a fresh label (s command in main menu)\n");
	} else {
		g_heads = SUN_SSWAP16(sunlabel->ntrks);
		g_cylinders = SUN_SSWAP16(sunlabel->ncyl);
		g_sectors = SUN_SSWAP16(sunlabel->nsect);
	}
	update_units();
	current_label_type = LABEL_SUN;
	g_partitions = 8;
	return 1;
}

static const struct sun_predefined_drives {
	const char *vendor;
	const char *model;
	unsigned short sparecyl;
	unsigned short ncyl;
	unsigned short nacyl;
	unsigned short pcylcount;
	unsigned short ntrks;
	unsigned short nsect;
	unsigned short rspeed;
} sun_drives[] = {
	{ "Quantum","ProDrive 80S",1,832,2,834,6,34,3662},
	{ "Quantum","ProDrive 105S",1,974,2,1019,6,35,3662},
	{ "CDC","Wren IV 94171-344",3,1545,2,1549,9,46,3600},
	{ "IBM","DPES-31080",0,4901,2,4903,4,108,5400},
	{ "IBM","DORS-32160",0,1015,2,1017,67,62,5400},
	{ "IBM","DNES-318350",0,11199,2,11474,10,320,7200},
	{ "SEAGATE","ST34371",0,3880,2,3882,16,135,7228},
	{ "","SUN0104",1,974,2,1019,6,35,3662},
	{ "","SUN0207",4,1254,2,1272,9,36,3600},
	{ "","SUN0327",3,1545,2,1549,9,46,3600},
	{ "","SUN0340",0,1538,2,1544,6,72,4200},
	{ "","SUN0424",2,1151,2,2500,9,80,4400},
	{ "","SUN0535",0,1866,2,2500,7,80,5400},
	{ "","SUN0669",5,1614,2,1632,15,54,3600},
	{ "","SUN1.0G",5,1703,2,1931,15,80,3597},
	{ "","SUN1.05",0,2036,2,2038,14,72,5400},
	{ "","SUN1.3G",6,1965,2,3500,17,80,5400},
	{ "","SUN2.1G",0,2733,2,3500,19,80,5400},
	{ "IOMEGA","Jaz",0,1019,2,1021,64,32,5394},
};

static const struct sun_predefined_drives *
sun_autoconfigure_scsi(void)
{
	const struct sun_predefined_drives *p = NULL;

#ifdef SCSI_IOCTL_GET_IDLUN
	unsigned int id[2];
	char buffer[2048];
	char buffer2[2048];
	FILE *pfd;
	char *vendor;
	char *model;
	char *q;
	int i;

	if (ioctl(dev_fd, SCSI_IOCTL_GET_IDLUN, &id))
		return NULL;

	sprintf(buffer,
		"Host: scsi%u Channel: %02u Id: %02u Lun: %02u\n",
		/* This is very wrong (works only if you have one HBA),
		   but I haven't found a way how to get hostno
		   from the current kernel */
		0,
		(id[0]>>16) & 0xff,
		id[0] & 0xff,
		(id[0]>>8) & 0xff
	);
	pfd = fopen_for_read("/proc/scsi/scsi");
	if (!pfd) {
		return NULL;
	}
	while (fgets(buffer2, 2048, pfd)) {
		if (strcmp(buffer, buffer2))
			continue;
		if (!fgets(buffer2, 2048, pfd))
			break;
		q = strstr(buffer2, "Vendor: ");
		if (!q)
			break;
		q += 8;
		vendor = q;
		q = strstr(q, " ");
		*q++ = '\0';   /* truncate vendor name */
		q = strstr(q, "Model: ");
		if (!q)
			break;
		*q = '\0';
		q += 7;
		model = q;
		q = strstr(q, " Rev: ");
		if (!q)
			break;
		*q = '\0';
		for (i = 0; i < ARRAY_SIZE(sun_drives); i++) {
			if (*sun_drives[i].vendor && strcasecmp(sun_drives[i].vendor, vendor))
				continue;
			if (!strstr(model, sun_drives[i].model))
				continue;
			printf("Autoconfigure found a %s%s%s\n",
					sun_drives[i].vendor,
					(*sun_drives[i].vendor) ? " " : "",
					sun_drives[i].model);
			p = sun_drives + i;
			break;
		}
		break;
	}
	fclose(pfd);
#endif
	return p;
}

static void
create_sunlabel(void)
{
	struct hd_geometry geometry;
	unsigned ndiv;
	unsigned char c;
	const struct sun_predefined_drives *p = NULL;

	printf(msg_building_new_label, "sun disklabel");

	sun_other_endian = BB_LITTLE_ENDIAN;
	memset(MBRbuffer, 0, sizeof(MBRbuffer));
	sunlabel->magic = SUN_SSWAP16(SUN_LABEL_MAGIC);
	if (!floppy) {
		unsigned i;
		puts("Drive type\n"
		 "   ?   auto configure\n"
		 "   0   custom (with hardware detected defaults)");
		for (i = 0; i < ARRAY_SIZE(sun_drives); i++) {
			printf("   %c   %s%s%s\n",
				i + 'a', sun_drives[i].vendor,
				(*sun_drives[i].vendor) ? " " : "",
				sun_drives[i].model);
		}
		while (1) {
			c = read_nonempty("Select type (? for auto, 0 for custom): ");
			if (c == '0') {
				break;
			}
			if (c >= 'a' && c < 'a' + ARRAY_SIZE(sun_drives)) {
				p = sun_drives + c - 'a';
				break;
			}
			if (c >= 'A' && c < 'A' + ARRAY_SIZE(sun_drives)) {
				p = sun_drives + c - 'A';
				break;
			}
			if (c == '?' && scsi_disk) {
				p = sun_autoconfigure_scsi();
				if (p)
					break;
				printf("Autoconfigure failed\n");
			}
		}
	}
	if (!p || floppy) {
		if (!ioctl(dev_fd, HDIO_GETGEO, &geometry)) {
			g_heads = geometry.heads;
			g_sectors = geometry.sectors;
			g_cylinders = geometry.cylinders;
		} else {
			g_heads = 0;
			g_sectors = 0;
			g_cylinders = 0;
		}
		if (floppy) {
			sunlabel->nacyl = 0;
			sunlabel->pcylcount = SUN_SSWAP16(g_cylinders);
			sunlabel->rspeed = SUN_SSWAP16(300);
			sunlabel->ilfact = SUN_SSWAP16(1);
			sunlabel->sparecyl = 0;
		} else {
			g_heads = read_int(1, g_heads, 1024, 0, "Heads");
			g_sectors = read_int(1, g_sectors, 1024, 0, "Sectors/track");
		if (g_cylinders)
			g_cylinders = read_int(1, g_cylinders - 2, 65535, 0, "Cylinders");
		else
			g_cylinders = read_int(1, 0, 65535, 0, "Cylinders");
			sunlabel->nacyl = SUN_SSWAP16(read_int(0, 2, 65535, 0, "Alternate cylinders"));
			sunlabel->pcylcount = SUN_SSWAP16(read_int(0, g_cylinders + SUN_SSWAP16(sunlabel->nacyl), 65535, 0, "Physical cylinders"));
			sunlabel->rspeed = SUN_SSWAP16(read_int(1, 5400, 100000, 0, "Rotation speed (rpm)"));
			sunlabel->ilfact = SUN_SSWAP16(read_int(1, 1, 32, 0, "Interleave factor"));
			sunlabel->sparecyl = SUN_SSWAP16(read_int(0, 0, g_sectors, 0, "Extra sectors per cylinder"));
		}
	} else {
		sunlabel->sparecyl = SUN_SSWAP16(p->sparecyl);
		sunlabel->ncyl = SUN_SSWAP16(p->ncyl);
		sunlabel->nacyl = SUN_SSWAP16(p->nacyl);
		sunlabel->pcylcount = SUN_SSWAP16(p->pcylcount);
		sunlabel->ntrks = SUN_SSWAP16(p->ntrks);
		sunlabel->nsect = SUN_SSWAP16(p->nsect);
		sunlabel->rspeed = SUN_SSWAP16(p->rspeed);
		sunlabel->ilfact = SUN_SSWAP16(1);
		g_cylinders = p->ncyl;
		g_heads = p->ntrks;
		g_sectors = p->nsect;
		puts("You may change all the disk params from the x menu");
	}

	snprintf((char *)(sunlabel->info), sizeof(sunlabel->info),
		"%s%s%s cyl %u alt %u hd %u sec %u",
		p ? p->vendor : "", (p && *p->vendor) ? " " : "",
		p ? p->model : (floppy ? "3,5\" floppy" : "Linux custom"),
		g_cylinders, SUN_SSWAP16(sunlabel->nacyl), g_heads, g_sectors);

	sunlabel->ntrks = SUN_SSWAP16(g_heads);
	sunlabel->nsect = SUN_SSWAP16(g_sectors);
	sunlabel->ncyl = SUN_SSWAP16(g_cylinders);
	if (floppy)
		set_sun_partition(0, 0, g_cylinders * g_heads * g_sectors, LINUX_NATIVE);
	else {
		if (g_cylinders * g_heads * g_sectors >= 150 * 2048) {
			ndiv = g_cylinders - (50 * 2048 / (g_heads * g_sectors)); /* 50M swap */
		} else
			ndiv = g_cylinders * 2 / 3;
		set_sun_partition(0, 0, ndiv * g_heads * g_sectors, LINUX_NATIVE);
		set_sun_partition(1, ndiv * g_heads * g_sectors, g_cylinders * g_heads * g_sectors, LINUX_SWAP);
		sunlabel->infos[1].flags |= 0x01; /* Not mountable */
	}
	set_sun_partition(2, 0, g_cylinders * g_heads * g_sectors, SUN_WHOLE_DISK);
	{
		unsigned short *ush = (unsigned short *)sunlabel;
		unsigned short csum = 0;
		while (ush < (unsigned short *)(&sunlabel->csum))
			csum ^= *ush++;
		sunlabel->csum = csum;
	}

	set_all_unchanged();
	set_changed(0);
	check_sun_label();
	get_boot(CREATE_EMPTY_SUN);
}

static void
toggle_sunflags(int i, unsigned char mask)
{
	if (sunlabel->infos[i].flags & mask)
		sunlabel->infos[i].flags &= ~mask;
	else
		sunlabel->infos[i].flags |= mask;
	set_changed(i);
}

static void
fetch_sun(unsigned *starts, unsigned *lens, unsigned *start, unsigned *stop)
{
	int i, continuous = 1;

	*start = 0;
	*stop = g_cylinders * g_heads * g_sectors;
	for (i = 0; i < g_partitions; i++) {
		if (sunlabel->partitions[i].num_sectors
		 && sunlabel->infos[i].id
		 && sunlabel->infos[i].id != SUN_WHOLE_DISK) {
			starts[i] = SUN_SSWAP32(sunlabel->partitions[i].start_cylinder) * g_heads * g_sectors;
			lens[i] = SUN_SSWAP32(sunlabel->partitions[i].num_sectors);
			if (continuous) {
				if (starts[i] == *start)
					*start += lens[i];
				else if (starts[i] + lens[i] >= *stop)
					*stop = starts[i];
				else
					continuous = 0;
					/* There will be probably more gaps
					  than one, so lets check afterwards */
			}
		} else {
			starts[i] = 0;
			lens[i] = 0;
		}
	}
}

static unsigned *verify_sun_starts;

static int
verify_sun_cmp(int *a, int *b)
{
	if (*a == -1) return 1;
	if (*b == -1) return -1;
	if (verify_sun_starts[*a] > verify_sun_starts[*b]) return 1;
	return -1;
}

static void
verify_sun(void)
{
	unsigned starts[8], lens[8], start, stop;
	int i,j,k,starto,endo;
	int array[8];

	verify_sun_starts = starts;
	fetch_sun(starts, lens, &start, &stop);
	for (k = 0; k < 7; k++) {
		for (i = 0; i < 8; i++) {
			if (k && (lens[i] % (g_heads * g_sectors))) {
				printf("Partition %u doesn't end on cylinder boundary\n", i+1);
			}
			if (lens[i]) {
				for (j = 0; j < i; j++)
					if (lens[j]) {
						if (starts[j] == starts[i]+lens[i]) {
							starts[j] = starts[i]; lens[j] += lens[i];
							lens[i] = 0;
						} else if (starts[i] == starts[j]+lens[j]){
							lens[j] += lens[i];
							lens[i] = 0;
						} else if (!k) {
							if (starts[i] < starts[j]+lens[j]
							 && starts[j] < starts[i]+lens[i]) {
								starto = starts[i];
								if (starts[j] > starto)
									starto = starts[j];
								endo = starts[i]+lens[i];
								if (starts[j]+lens[j] < endo)
									endo = starts[j]+lens[j];
								printf("Partition %u overlaps with others in "
									"sectors %u-%u\n", i+1, starto, endo);
							}
						}
					}
			}
		}
	}
	for (i = 0; i < 8; i++) {
		if (lens[i])
			array[i] = i;
		else
			array[i] = -1;
	}
	qsort(array, ARRAY_SIZE(array), sizeof(array[0]),
		(int (*)(const void *,const void *)) verify_sun_cmp);
	if (array[0] == -1) {
		printf("No partitions defined\n");
		return;
	}
	stop = g_cylinders * g_heads * g_sectors;
	if (starts[array[0]])
		printf("Unused gap - sectors %u-%u\n", 0, starts[array[0]]);
	for (i = 0; i < 7 && array[i+1] != -1; i++) {
		printf("Unused gap - sectors %u-%u\n", starts[array[i]]+lens[array[i]], starts[array[i+1]]);
	}
	start = starts[array[i]] + lens[array[i]];
	if (start < stop)
		printf("Unused gap - sectors %u-%u\n", start, stop);
}

static void
add_sun_partition(int n, int sys)
{
	unsigned start, stop, stop2;
	unsigned starts[8], lens[8];
	int whole_disk = 0;

	char mesg[256];
	int i, first, last;

	if (sunlabel->partitions[n].num_sectors && sunlabel->infos[n].id) {
		printf(msg_part_already_defined, n + 1);
		return;
	}

	fetch_sun(starts, lens, &start, &stop);
	if (stop <= start) {
		if (n == 2)
			whole_disk = 1;
		else {
			printf("Other partitions already cover the whole disk.\n"
				"Delete/shrink them before retry.\n");
			return;
		}
	}
	snprintf(mesg, sizeof(mesg), "First %s", str_units(SINGULAR));
	while (1) {
		if (whole_disk)
			first = read_int(0, 0, 0, 0, mesg);
		else
			first = read_int(scround(start), scround(stop)+1,
					 scround(stop), 0, mesg);
		if (display_in_cyl_units) {
			first *= units_per_sector;
		} else {
			/* Starting sector has to be properly aligned */
			first = (first + g_heads * g_sectors - 1) /
				(g_heads * g_sectors);
			first *= g_heads * g_sectors;
		}
		if (n == 2 && first != 0)
			printf("\
It is highly recommended that the third partition covers the whole disk\n\
and is of type 'Whole disk'\n");
		/* ewt asks to add: "don't start a partition at cyl 0"
		   However, edmundo@rano.demon.co.uk writes:
		   "In addition to having a Sun partition table, to be able to
		   boot from the disc, the first partition, /dev/sdX1, must
		   start at cylinder 0. This means that /dev/sdX1 contains
		   the partition table and the boot block, as these are the
		   first two sectors of the disc. Therefore you must be
		   careful what you use /dev/sdX1 for. In particular, you must
		   not use a partition starting at cylinder 0 for Linux swap,
		   as that would overwrite the partition table and the boot
		   block. You may, however, use such a partition for a UFS
		   or EXT2 file system, as these file systems leave the first
		   1024 bytes undisturbed. */
		/* On the other hand, one should not use partitions
		   starting at block 0 in an md, or the label will
		   be trashed. */
		for (i = 0; i < g_partitions; i++)
			if (lens[i] && starts[i] <= first && starts[i] + lens[i] > first)
				break;
		if (i < g_partitions && !whole_disk) {
			if (n == 2 && !first) {
				whole_disk = 1;
				break;
			}
			printf("Sector %u is already allocated\n", first);
		} else
			break;
	}
	stop = g_cylinders * g_heads * g_sectors;
	stop2 = stop;
	for (i = 0; i < g_partitions; i++) {
		if (starts[i] > first && starts[i] < stop)
			stop = starts[i];
	}
	snprintf(mesg, sizeof(mesg),
		"Last %s or +size or +sizeM or +sizeK",
		str_units(SINGULAR));
	if (whole_disk)
		last = read_int(scround(stop2), scround(stop2), scround(stop2),
				0, mesg);
	else if (n == 2 && !first)
		last = read_int(scround(first), scround(stop2), scround(stop2),
				scround(first), mesg);
	else
		last = read_int(scround(first), scround(stop), scround(stop),
				scround(first), mesg);
	if (display_in_cyl_units)
		last *= units_per_sector;
	if (n == 2 && !first) {
		if (last >= stop2) {
			whole_disk = 1;
			last = stop2;
		} else if (last > stop) {
			printf(
"You haven't covered the whole disk with the 3rd partition,\n"
"but your value %u %s covers some other partition.\n"
"Your entry has been changed to %u %s\n",
				scround(last), str_units(SINGULAR),
				scround(stop), str_units(SINGULAR));
			last = stop;
		}
	} else if (!whole_disk && last > stop)
		last = stop;

	if (whole_disk)
		sys = SUN_WHOLE_DISK;
	set_sun_partition(n, first, last, sys);
}

static void
sun_delete_partition(int i)
{
	unsigned int nsec;

	if (i == 2
	 && sunlabel->infos[i].id == SUN_WHOLE_DISK
	 && !sunlabel->partitions[i].start_cylinder
	 && (nsec = SUN_SSWAP32(sunlabel->partitions[i].num_sectors)) == g_heads * g_sectors * g_cylinders)
		printf("If you want to maintain SunOS/Solaris compatibility, "
			"consider leaving this\n"
			"partition as Whole disk (5), starting at 0, with %u "
			"sectors\n", nsec);
	sunlabel->infos[i].id = 0;
	sunlabel->partitions[i].num_sectors = 0;
}

static void
sun_change_sysid(int i, int sys)
{
	if (sys == LINUX_SWAP && !sunlabel->partitions[i].start_cylinder) {
		read_maybe_empty(
			"It is highly recommended that the partition at offset 0\n"
			"is UFS, EXT2FS filesystem or SunOS swap. Putting Linux swap\n"
			"there may destroy your partition table and bootblock.\n"
			"Type YES if you're very sure you would like that partition\n"
			"tagged with 82 (Linux swap): ");
		if (strcmp (line_ptr, "YES\n"))
			return;
	}
	switch (sys) {
	case SUNOS_SWAP:
	case LINUX_SWAP:
		/* swaps are not mountable by default */
		sunlabel->infos[i].flags |= 0x01;
		break;
	default:
		/* assume other types are mountable;
		   user can change it anyway */
		sunlabel->infos[i].flags &= ~0x01;
		break;
	}
	sunlabel->infos[i].id = sys;
}

static void
sun_list_table(int xtra)
{
	int i, w;

	w = strlen(disk_device);
	if (xtra)
		printf(
		"\nDisk %s (Sun disk label): %u heads, %u sectors, %u rpm\n"
		"%u cylinders, %u alternate cylinders, %u physical cylinders\n"
		"%u extra sects/cyl, interleave %u:1\n"
		"%s\n"
		"Units = %s of %u * 512 bytes\n\n",
			disk_device, g_heads, g_sectors, SUN_SSWAP16(sunlabel->rspeed),
			g_cylinders, SUN_SSWAP16(sunlabel->nacyl),
			SUN_SSWAP16(sunlabel->pcylcount),
			SUN_SSWAP16(sunlabel->sparecyl),
			SUN_SSWAP16(sunlabel->ilfact),
			(char *)sunlabel,
			str_units(PLURAL), units_per_sector);
	else
		printf(
	"\nDisk %s (Sun disk label): %u heads, %u sectors, %u cylinders\n"
	"Units = %s of %u * 512 bytes\n\n",
			disk_device, g_heads, g_sectors, g_cylinders,
			str_units(PLURAL), units_per_sector);

	printf("%*s Flag    Start       End    Blocks   Id  System\n",
		w + 1, "Device");
	for (i = 0; i < g_partitions; i++) {
		if (sunlabel->partitions[i].num_sectors) {
			uint32_t start = SUN_SSWAP32(sunlabel->partitions[i].start_cylinder) * g_heads * g_sectors;
			uint32_t len = SUN_SSWAP32(sunlabel->partitions[i].num_sectors);
			printf("%s %c%c %9lu %9lu %9lu%c  %2x  %s\n",
				partname(disk_device, i+1, w),                  /* device */
				(sunlabel->infos[i].flags & 0x01) ? 'u' : ' ',  /* flags */
				(sunlabel->infos[i].flags & 0x10) ? 'r' : ' ',
				(long) scround(start),                          /* start */
				(long) scround(start+len),                      /* end */
				(long) len / 2, len & 1 ? '+' : ' ',            /* odd flag on end */
				sunlabel->infos[i].id,                          /* type id */
				partition_type(sunlabel->infos[i].id));         /* type name */
		}
	}
}

#if ENABLE_FEATURE_FDISK_ADVANCED

static void
sun_set_alt_cyl(void)
{
	sunlabel->nacyl =
		SUN_SSWAP16(read_int(0, SUN_SSWAP16(sunlabel->nacyl), 65535, 0,
				"Number of alternate cylinders"));
}

static void
sun_set_ncyl(int cyl)
{
	sunlabel->ncyl = SUN_SSWAP16(cyl);
}

static void
sun_set_xcyl(void)
{
	sunlabel->sparecyl =
		SUN_SSWAP16(read_int(0, SUN_SSWAP16(sunlabel->sparecyl), g_sectors, 0,
				"Extra sectors per cylinder"));
}

static void
sun_set_ilfact(void)
{
	sunlabel->ilfact =
		SUN_SSWAP16(read_int(1, SUN_SSWAP16(sunlabel->ilfact), 32, 0,
				"Interleave factor"));
}

static void
sun_set_rspeed(void)
{
	sunlabel->rspeed =
		SUN_SSWAP16(read_int(1, SUN_SSWAP16(sunlabel->rspeed), 100000, 0,
				"Rotation speed (rpm)"));
}

static void
sun_set_pcylcount(void)
{
	sunlabel->pcylcount =
		SUN_SSWAP16(read_int(0, SUN_SSWAP16(sunlabel->pcylcount), 65535, 0,
				"Number of physical cylinders"));
}
#endif /* FEATURE_FDISK_ADVANCED */

static void
sun_write_table(void)
{
	unsigned short *ush = (unsigned short *)sunlabel;
	unsigned short csum = 0;

	while (ush < (unsigned short *)(&sunlabel->csum))
		csum ^= *ush++;
	sunlabel->csum = csum;
	write_sector(0, sunlabel);
}
#endif /* SUN_LABEL */

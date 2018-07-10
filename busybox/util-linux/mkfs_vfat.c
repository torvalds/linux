/* vi: set sw=4 ts=4: */
/*
 * mkfs_vfat: utility to create FAT32 filesystem
 * inspired by dosfstools
 *
 * Busybox'ed (2009) by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MKDOSFS
//config:	bool "mkdosfs (6.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Utility to create FAT32 filesystems.
//config:
//config:config MKFS_VFAT
//config:	bool "mkfs.vfat (6.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Alias to "mkdosfs".

//                    APPLET_ODDNAME:name       main       location     suid_type     help
//applet:IF_MKDOSFS(  APPLET_ODDNAME(mkdosfs,   mkfs_vfat, BB_DIR_SBIN, BB_SUID_DROP, mkfs_vfat))
//applet:IF_MKFS_VFAT(APPLET_ODDNAME(mkfs.vfat, mkfs_vfat, BB_DIR_SBIN, BB_SUID_DROP, mkfs_vfat))

//kbuild:lib-$(CONFIG_MKDOSFS) += mkfs_vfat.o
//kbuild:lib-$(CONFIG_MKFS_VFAT) += mkfs_vfat.o

//usage:#define mkfs_vfat_trivial_usage
//usage:       "[-v] [-n LABEL] BLOCKDEV [KBYTES]"
/* Accepted but ignored:
       "[-c] [-C] [-I] [-l bad-block-file] [-b backup-boot-sector] "
       "[-m boot-msg-file] [-i volume-id] "
       "[-s sectors-per-cluster] [-S logical-sector-size] [-f number-of-FATs] "
       "[-h hidden-sectors] [-F fat-size] [-r root-dir-entries] [-R reserved-sectors] "
*/
//usage:#define mkfs_vfat_full_usage "\n\n"
//usage:       "Make a FAT32 filesystem\n"
/* //usage:  "\n	-c	Check device for bad blocks" */
//usage:     "\n	-v	Verbose"
/* //usage:  "\n	-I	Allow to use entire disk device (e.g. /dev/hda)" */
//usage:     "\n	-n LBL	Volume label"

#include "libbb.h"

#include <linux/hdreg.h> /* HDIO_GETGEO */
#include <linux/fd.h>    /* FDGETPRM */
#include <sys/mount.h>   /* BLKSSZGET */
#if !defined(BLKSSZGET)
# define BLKSSZGET _IO(0x12, 104)
#endif
//#include <linux/msdos_fs.h>

#define SECTOR_SIZE             512

#define SECTORS_PER_BLOCK	(BLOCK_SIZE / SECTOR_SIZE)

// M$ says the high 4 bits of a FAT32 FAT entry are reserved
#define EOF_FAT32       0x0FFFFFF8
#define BAD_FAT32       0x0FFFFFF7
#define MAX_CLUST_32    0x0FFFFFF0

#define ATTR_VOLUME     8

#define NUM_FATS        2

/* FAT32 filesystem looks like this:
 * sector -nn...-1: "hidden" sectors, all sectors before this partition
 * (-h hidden-sectors sets it. Useful only for boot loaders,
 *  they need to know _disk_ offset in order to be able to correctly
 *  address sectors relative to start of disk)
 * sector 0: boot sector
 * sector 1: info sector
 * sector 2: set aside for boot code which didn't fit into sector 0
 * ...(zero-filled sectors)...
 * sector B: backup copy of sector 0 [B set by -b backup-boot-sector]
 * sector B+1: backup copy of sector 1
 * sector B+2: backup copy of sector 2
 * ...(zero-filled sectors)...
 * sector R: FAT#1 [R set by -R reserved-sectors]
 * ...(FAT#1)...
 * sector R+fat_size: FAT#2
 * ...(FAT#2)...
 * sector R+fat_size*2: cluster #2
 * ...(cluster #2)...
 * sector R+fat_size*2+clust_size: cluster #3
 * ...(the rest is filled by clusters till the end)...
 */

enum {
// Perhaps this should remain constant
	info_sector_number = 1,
// TODO: make these cmdline options
// dont forget sanity check: backup_boot_sector + 3 <= reserved_sect
	backup_boot_sector = 3,
	reserved_sect      = 6,
};

// how many blocks we try to read while testing
#define TEST_BUFFER_BLOCKS      16

struct msdos_dir_entry {
	char     name[11];       /* 000 name and extension */
	uint8_t  attr;           /* 00b attribute bits */
	uint8_t  lcase;          /* 00c case for base and extension */
	uint8_t  ctime_cs;       /* 00d creation time, centiseconds (0-199) */
	uint16_t ctime;          /* 00e creation time */
	uint16_t cdate;          /* 010 creation date */
	uint16_t adate;          /* 012 last access date */
	uint16_t starthi;        /* 014 high 16 bits of cluster in FAT32 */
	uint16_t time;           /* 016 time */
	uint16_t date;           /* 018 date */
	uint16_t start;          /* 01a first cluster */
	uint32_t size;           /* 01c file size in bytes */
} PACKED;

/* Example of boot sector's beginning:
0000  eb 58 90 4d 53 57 49 4e  34 2e 31 00 02 08 26 00  |...MSWIN4.1...&.|
0010  02 00 00 00 00 f8 00 00  3f 00 ff 00 3f 00 00 00  |........?...?...|
0020  54 9b d0 00 0d 34 00 00  00 00 00 00 02 00 00 00  |T....4..........|
0030  01 00 06 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
0040  80 00 29 71 df 51 e0 4e  4f 20 4e 41 4d 45 20 20  |..)q.Q.NO NAME  |
0050  20 20 46 41 54 33 32 20  20 20 33 c9 8e d1 bc f4  |  FAT32   3.....|
*/
struct msdos_volume_info { /* (offsets are relative to start of boot sector) */
	uint8_t  drive_number;    /* 040 BIOS drive number */
	uint8_t  reserved;        /* 041 unused */
	uint8_t  ext_boot_sign;	  /* 042 0x29 if fields below exist (DOS 3.3+) */
	uint32_t volume_id32;     /* 043 volume ID number */
	char     volume_label[11];/* 047 volume label */
	char     fs_type[8];      /* 052 typically "FATnn" */
} PACKED;                         /* 05a end. Total size 26 (0x1a) bytes */

struct msdos_boot_sector {
	/* We use strcpy to fill both, and gcc-4.4.x complains if they are separate */
	char     boot_jump_and_sys_id[3+8]; /* 000 short or near jump instruction */
	/*char   system_id[8];*/     /* 003 name - can be used to special case partition manager volumes */
	uint16_t bytes_per_sect;     /* 00b bytes per logical sector */
	uint8_t  sect_per_clust;     /* 00d sectors/cluster */
	uint16_t reserved_sect;      /* 00e reserved sectors (sector offset of 1st FAT relative to volume start) */
	uint8_t  fats;               /* 010 number of FATs */
	uint16_t dir_entries;        /* 011 root directory entries */
	uint16_t volume_size_sect;   /* 013 volume size in sectors */
	uint8_t  media_byte;         /* 015 media code */
	uint16_t sect_per_fat;       /* 016 sectors/FAT */
	uint16_t sect_per_track;     /* 018 sectors per track */
	uint16_t heads;              /* 01a number of heads */
	uint32_t hidden;             /* 01c hidden sectors (sector offset of volume within physical disk) */
	uint32_t fat32_volume_size_sect; /* 020 volume size in sectors (if volume_size_sect == 0) */
	uint32_t fat32_sect_per_fat; /* 024 sectors/FAT */
	uint16_t fat32_flags;        /* 028 bit 8: fat mirroring, low 4: active fat */
	uint8_t  fat32_version[2];   /* 02a major, minor filesystem version (I see 0,0) */
	uint32_t fat32_root_cluster; /* 02c first cluster in root directory */
	uint16_t fat32_info_sector;  /* 030 filesystem info sector (usually 1) */
	uint16_t fat32_backup_boot;  /* 032 backup boot sector (usually 6) */
	uint32_t reserved2[3];       /* 034 unused */
	struct msdos_volume_info vi; /* 040 */
	char     boot_code[0x200 - 0x5a - 2]; /* 05a */
#define BOOT_SIGN 0xAA55
	uint16_t boot_sign;          /* 1fe */
} PACKED;

#define FAT_FSINFO_SIG1 0x41615252
#define FAT_FSINFO_SIG2 0x61417272
struct fat32_fsinfo {
	uint32_t signature1;         /* 0x52,0x52,0x41,0x61, "RRaA" */
	uint32_t reserved1[128 - 8];
	uint32_t signature2;         /* 0x72,0x72,0x61,0x41, "rrAa" */
	uint32_t free_clusters;      /* free cluster count.  -1 if unknown */
	uint32_t next_cluster;       /* most recently allocated cluster */
	uint32_t reserved2[3];
	uint16_t reserved3;          /* 1fc */
	uint16_t boot_sign;          /* 1fe */
} PACKED;

struct bug_check {
	char BUG1[sizeof(struct msdos_dir_entry  ) == 0x20 ? 1 : -1];
	char BUG2[sizeof(struct msdos_volume_info) == 0x1a ? 1 : -1];
	char BUG3[sizeof(struct msdos_boot_sector) == 0x200 ? 1 : -1];
	char BUG4[sizeof(struct fat32_fsinfo     ) == 0x200 ? 1 : -1];
};

static const char boot_code[] ALIGN1 =
	"\x0e"          /* 05a:         push  cs */
	"\x1f"          /* 05b:         pop   ds */
	"\xbe\x77\x7c"  /*  write_msg:  mov   si, offset message_txt */
	"\xac"          /* 05f:         lodsb */
	"\x22\xc0"      /* 060:         and   al, al */
	"\x74\x0b"      /* 062:         jz    key_press */
	"\x56"          /* 064:         push  si */
	"\xb4\x0e"      /* 065:         mov   ah, 0eh */
	"\xbb\x07\x00"  /* 067:         mov   bx, 0007h */
	"\xcd\x10"      /* 06a:         int   10h */
	"\x5e"          /* 06c:         pop   si */
	"\xeb\xf0"      /* 06d:         jmp   write_msg */
	"\x32\xe4"      /*  key_press:  xor   ah, ah */
	"\xcd\x16"      /* 071:         int   16h */
	"\xcd\x19"      /* 073:         int   19h */
	"\xeb\xfe"      /*  foo:        jmp   foo */
	/* 077: message_txt: */
	"This is not a bootable disk\r\n";


#define MARK_CLUSTER(cluster, value) \
	((uint32_t *)fat)[cluster] = SWAP_LE32(value)

void BUG_unsupported_field_size(void);
#define STORE_LE(field, value) \
do { \
	if (sizeof(field) == 4) \
		field = SWAP_LE32((uint32_t)(value)); \
	else if (sizeof(field) == 2) \
		field = SWAP_LE16((uint16_t)(value)); \
	else if (sizeof(field) == 1) \
		field = (uint8_t)(value); \
	else \
		BUG_unsupported_field_size(); \
} while (0)

/* compat:
 * mkdosfs 2.11 (12 Mar 2005)
 * Usage: mkdosfs [-A] [-c] [-C] [-v] [-I] [-l bad-block-file]
 *        [-b backup-boot-sector]
 *        [-m boot-msg-file] [-n volume-name] [-i volume-id]
 *        [-s sectors-per-cluster] [-S logical-sector-size]
 *        [-f number-of-FATs]
 *        [-h hidden-sectors] [-F fat-size] [-r root-dir-entries]
 *        [-R reserved-sectors]
 *        /dev/name [blocks]
 */
int mkfs_vfat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mkfs_vfat_main(int argc UNUSED_PARAM, char **argv)
{
	struct stat st;
	const char *volume_label = "";
	char *buf;
	char *device_name;
	uoff_t volume_size_bytes;
	uoff_t volume_size_sect;
	uint32_t total_clust;
	uint32_t volume_id;
	int dev;
	unsigned bytes_per_sect;
	unsigned sect_per_fat;
	unsigned opts;
	uint16_t sect_per_track;
	uint8_t media_byte;
	uint8_t sect_per_clust;
	uint8_t heads;
	enum {
		OPT_A = 1 << 0,  // [IGNORED] atari format
		OPT_b = 1 << 1,	 // [IGNORED] location of backup boot sector
		OPT_c = 1 << 2,	 // [IGNORED] check filesystem
		OPT_C = 1 << 3,	 // [IGNORED] create a new file
		OPT_f = 1 << 4,	 // [IGNORED] number of FATs
		OPT_F = 1 << 5,	 // [IGNORED, implied 32] choose FAT size
		OPT_h = 1 << 6,	 // [IGNORED] number of hidden sectors
		OPT_I = 1 << 7,	 // [IGNORED] don't bark at entire disk devices
		OPT_i = 1 << 8,	 // [IGNORED] volume ID
		OPT_l = 1 << 9,	 // [IGNORED] bad block filename
		OPT_m = 1 << 10, // [IGNORED] message file
		OPT_n = 1 << 11, // volume label
		OPT_r = 1 << 12, // [IGNORED] root directory entries
		OPT_R = 1 << 13, // [IGNORED] number of reserved sectors
		OPT_s = 1 << 14, // [IGNORED] sectors per cluster
		OPT_S = 1 << 15, // [IGNORED] sector size
		OPT_v = 1 << 16, // verbose
	};

	opts = getopt32(argv, "^"
		"Ab:cCf:F:h:Ii:l:m:n:r:R:s:S:v"
		"\0" "-1", //:b+:f+:F+:h+:r+:R+:s+:S+:vv:c--l:l--c
		NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, &volume_label, NULL, NULL, NULL, NULL);
	argv += optind;

	// cache device name
	device_name = argv[0];
	// default volume ID = creation time
	volume_id = time(NULL);

	dev = xopen(device_name, O_RDWR);
	xfstat(dev, &st, device_name);

	//
	// Get image size and sector size
	//
	bytes_per_sect = SECTOR_SIZE;
	if (!S_ISBLK(st.st_mode)) {
		if (!S_ISREG(st.st_mode)) {
			if (!argv[1])
				bb_error_msg_and_die("image size must be specified");
		}
		// not a block device, skip bad sectors check
		opts &= ~OPT_c;
	} else {
		int min_bytes_per_sect;
#if 0
		unsigned device_num;
		// for true block devices we do check sanity
		device_num = st.st_rdev & 0xff3f;
		// do we allow to format the whole disk device?
		if (!(opts & OPT_I) && (
			device_num == 0x0300 || // hda, hdb
			(device_num & 0xff0f) == 0x0800 || // sd
			device_num == 0x0d00 || // xd
			device_num == 0x1600 )  // hdc, hdd
		)
			bb_error_msg_and_die("will not try to make filesystem on full-disk device (use -I if wanted)");
		// can't work on mounted filesystems
		if (find_mount_point(device_name, 0))
			bb_error_msg_and_die("can't format mounted filesystem");
#endif
		// get true sector size
		// (parameter must be int*, not long* or size_t*)
		xioctl(dev, BLKSSZGET, &min_bytes_per_sect);
		if (min_bytes_per_sect > SECTOR_SIZE) {
			bytes_per_sect = min_bytes_per_sect;
			bb_error_msg("for this device sector size is %u", min_bytes_per_sect);
		}
	}
	volume_size_bytes = get_volume_size_in_bytes(dev, argv[1], 1024, /*extend:*/ 1);
	volume_size_sect = volume_size_bytes / bytes_per_sect;

	//
	// Find out or guess media parameters
	//
	media_byte = 0xf8;
	heads = 255;
	sect_per_track = 63;
	sect_per_clust = 1;
	{
		struct hd_geometry geometry;
		// size (in sectors), sect (per track), head
		struct floppy_struct param;

		// N.B. whether to use HDIO_GETGEO or HDIO_REQ?
		if (ioctl(dev, HDIO_GETGEO, &geometry) == 0
		 && geometry.sectors
		 && geometry.heads
		) {
			// hard drive
			sect_per_track = geometry.sectors;
			heads = geometry.heads;

 set_cluster_size:
			/* For FAT32, try to do the same as M$'s format command
			 * (see http://www.win.tue.nl/~aeb/linux/fs/fat/fatgen103.pdf p. 20):
			 * fs size <= 260M: 0.5k clusters
			 * fs size <=   8G: 4k clusters
			 * fs size <=  16G: 8k clusters
			 * fs size >   16G: 16k clusters
			 */
			sect_per_clust = 1;
			if (volume_size_bytes >= 260*1024*1024) {
				sect_per_clust = 8;
				/* fight gcc: */
				/* "error: integer overflow in expression" */
				/* "error: right shift count >= width of type" */
				if (sizeof(off_t) > 4) {
					unsigned t = (volume_size_bytes >> 31 >> 1);
					if (t >= 8/4)
						sect_per_clust = 16;
					if (t >= 16/4)
						sect_per_clust = 32;
				}
			}
		} else {
			// floppy, loop, or regular file
			int not_floppy = ioctl(dev, FDGETPRM, &param);
			if (not_floppy == 0) {
				// floppy disk
				sect_per_track = param.sect;
				heads = param.head;
				volume_size_sect = param.size;
				volume_size_bytes = param.size * SECTOR_SIZE;
			}
			// setup the media descriptor byte
			switch (volume_size_sect) {
			case 2*360:	// 5.25", 2, 9, 40 - 360K
				media_byte = 0xfd;
				break;
			case 2*720:	// 3.5", 2, 9, 80 - 720K
			case 2*1200:	// 5.25", 2, 15, 80 - 1200K
				media_byte = 0xf9;
				break;
			default:	// anything else
				if (not_floppy)
					goto set_cluster_size;
			case 2*1440:	// 3.5", 2, 18, 80 - 1440K
			case 2*2880:	// 3.5", 2, 36, 80 - 2880K
				media_byte = 0xf0;
				break;
			}
			// not floppy, but size matches floppy exactly.
			// perhaps it is a floppy image.
			// we already set media_byte as if it is a floppy,
			// now set sect_per_track and heads.
			heads = 2;
			sect_per_track = (unsigned)volume_size_sect / 160;
			if (sect_per_track < 9)
				sect_per_track = 9;
		}
	}

	//
	// Calculate number of clusters, sectors/cluster, sectors/FAT
	// (an initial guess for sect_per_clust should already be set)
	//
	// "mkdosfs -v -F 32 image5k 5" is the minimum:
	// 2 sectors for FATs and 2 data sectors
	if ((off_t)(volume_size_sect - reserved_sect) < 4)
		bb_error_msg_and_die("the image is too small for FAT32");
	sect_per_fat = 1;
	while (1) {
		while (1) {
			int spf_adj;
			uoff_t tcl = (volume_size_sect - reserved_sect - NUM_FATS * sect_per_fat) / sect_per_clust;
			// tcl may be > MAX_CLUST_32 here, but it may be
			// because sect_per_fat is underestimated,
			// and with increased sect_per_fat it still may become
			// <= MAX_CLUST_32. Therefore, we do not check
			// against MAX_CLUST_32, but against a bigger const:
			if (tcl > 0x80ffffff)
				goto next;
			total_clust = tcl; // fits in uint32_t
			// Every cluster needs 4 bytes in FAT. +2 entries since
			// FAT has space for non-existent clusters 0 and 1.
			// Let's see how many sectors that needs.
			//May overflow at "*4":
			//spf_adj = ((total_clust+2) * 4 + bytes_per_sect-1) / bytes_per_sect - sect_per_fat;
			//Same in the more obscure, non-overflowing form:
			spf_adj = ((total_clust+2) + (bytes_per_sect/4)-1) / (bytes_per_sect/4) - sect_per_fat;
#if 0
			bb_error_msg("sect_per_clust:%u sect_per_fat:%u total_clust:%u",
					sect_per_clust, sect_per_fat, (int)tcl);
			bb_error_msg("adjust to sect_per_fat:%d", spf_adj);
#endif
			if (spf_adj <= 0) {
				// do not need to adjust sect_per_fat.
				// so, was total_clust too big after all?
				if (total_clust <= MAX_CLUST_32)
					goto found_total_clust; // no
				// yes, total_clust is _a bit_ too big
				goto next;
			}
			// adjust sect_per_fat, go back and recalc total_clust
			// (note: just "sect_per_fat += spf_adj" isn't ok)
			sect_per_fat += ((unsigned)spf_adj / 2) | 1;
		}
 next:
		if (sect_per_clust == 128)
			bb_error_msg_and_die("can't make FAT32 with >128 sectors/cluster");
		sect_per_clust *= 2;
		sect_per_fat = (sect_per_fat / 2) | 1;
	}
 found_total_clust:

	//
	// Print info
	//
	if (opts & OPT_v) {
		fprintf(stderr,
			"Device '%s':\n"
			"heads:%u, sectors/track:%u, bytes/sector:%u\n"
			"media descriptor:%02x\n"
			"total sectors:%"OFF_FMT"u, clusters:%u, sectors/cluster:%u\n"
			"FATs:2, sectors/FAT:%u\n"
			"volumeID:%08x, label:'%s'\n",
			device_name,
			heads, sect_per_track, bytes_per_sect,
			(int)media_byte,
			volume_size_sect, (int)total_clust, (int)sect_per_clust,
			sect_per_fat,
			(int)volume_id, volume_label
		);
	}

	//
	// Write filesystem image sequentially (no seeking)
	//
	{
		// (a | b) is poor man's max(a, b)
		unsigned bufsize = reserved_sect;
		//bufsize |= sect_per_fat; // can be quite large
		bufsize |= 2; // use this instead
		bufsize |= sect_per_clust;
		buf = xzalloc(bufsize * bytes_per_sect);
	}

	{ // boot and fsinfo sectors, and their copies
		struct msdos_boot_sector *boot_blk = (void*)buf;
		struct fat32_fsinfo *info = (void*)(buf + bytes_per_sect);

		strcpy(boot_blk->boot_jump_and_sys_id, "\xeb\x58\x90" "mkdosfs");
		STORE_LE(boot_blk->bytes_per_sect, bytes_per_sect);
		STORE_LE(boot_blk->sect_per_clust, sect_per_clust);
		// cast in needed on big endian to suppress a warning
		STORE_LE(boot_blk->reserved_sect, (uint16_t)reserved_sect);
		STORE_LE(boot_blk->fats, 2);
		//STORE_LE(boot_blk->dir_entries, 0); // for FAT32, stays 0
		if (volume_size_sect <= 0xffff)
			STORE_LE(boot_blk->volume_size_sect, volume_size_sect);
		STORE_LE(boot_blk->media_byte, media_byte);
		// wrong: this would make Linux think that it's fat12/16:
		//if (sect_per_fat <= 0xffff)
		//	STORE_LE(boot_blk->sect_per_fat, sect_per_fat);
		// works:
		//STORE_LE(boot_blk->sect_per_fat, 0);
		STORE_LE(boot_blk->sect_per_track, sect_per_track);
		STORE_LE(boot_blk->heads, heads);
		//STORE_LE(boot_blk->hidden, 0);
		STORE_LE(boot_blk->fat32_volume_size_sect, volume_size_sect);
		STORE_LE(boot_blk->fat32_sect_per_fat, sect_per_fat);
		//STORE_LE(boot_blk->fat32_flags, 0);
		//STORE_LE(boot_blk->fat32_version[2], 0,0);
		STORE_LE(boot_blk->fat32_root_cluster, 2);
		STORE_LE(boot_blk->fat32_info_sector, info_sector_number);
		STORE_LE(boot_blk->fat32_backup_boot, backup_boot_sector);
		//STORE_LE(boot_blk->reserved2[3], 0,0,0);
		STORE_LE(boot_blk->vi.ext_boot_sign, 0x29);
		STORE_LE(boot_blk->vi.volume_id32, volume_id);
		memcpy(boot_blk->vi.fs_type, "FAT32   ", sizeof(boot_blk->vi.fs_type));
		strncpy(boot_blk->vi.volume_label, volume_label, sizeof(boot_blk->vi.volume_label));
		memcpy(boot_blk->boot_code, boot_code, sizeof(boot_code));
		STORE_LE(boot_blk->boot_sign, BOOT_SIGN);

		STORE_LE(info->signature1, FAT_FSINFO_SIG1);
		STORE_LE(info->signature2, FAT_FSINFO_SIG2);
		// we've allocated cluster 2 for the root dir
		STORE_LE(info->free_clusters, (total_clust - 1));
		STORE_LE(info->next_cluster, 2);
		STORE_LE(info->boot_sign, BOOT_SIGN);

		// 1st copy
		xwrite(dev, buf, bytes_per_sect * backup_boot_sector);
		// 2nd copy and possibly zero sectors
		xwrite(dev, buf, bytes_per_sect * (reserved_sect - backup_boot_sector));
	}

	{ // file allocation tables
		unsigned i,j;
		unsigned char *fat = (void*)buf;

		memset(buf, 0, bytes_per_sect * 2);
		// initial FAT entries
		MARK_CLUSTER(0, 0x0fffff00 | media_byte);
		MARK_CLUSTER(1, 0xffffffff);
		// mark cluster 2 as EOF (used for root dir)
		MARK_CLUSTER(2, EOF_FAT32);
		for (i = 0; i < NUM_FATS; i++) {
			xwrite(dev, buf, bytes_per_sect);
			for (j = 1; j < sect_per_fat; j++)
				xwrite(dev, buf + bytes_per_sect, bytes_per_sect);
		}
	}

	// root directory
	// empty directory is just a set of zero bytes
	memset(buf, 0, sect_per_clust * bytes_per_sect);
	if (volume_label[0]) {
		// create dir entry for volume_label
		struct msdos_dir_entry *de;
#if 0
		struct tm tm_time;
		uint16_t t, d;
#endif
		de = (void*)buf;
		strncpy(de->name, volume_label, sizeof(de->name));
		STORE_LE(de->attr, ATTR_VOLUME);
#if 0
		localtime_r(&create_time, &tm_time);
		t = (tm_time.tm_sec >> 1) + (tm_time.tm_min << 5) + (tm_time.tm_hour << 11);
		d = tm_time.tm_mday + ((tm_time.tm_mon+1) << 5) + ((tm_time.tm_year-80) << 9);
		STORE_LE(de->time, t);
		STORE_LE(de->date, d);
		//STORE_LE(de->ctime_cs, 0);
		de->ctime = de->time;
		de->cdate = de->date;
		de->adate = de->date;
#endif
	}
	xwrite(dev, buf, sect_per_clust * bytes_per_sect);

#if 0
	if (opts & OPT_c) {
		uoff_t volume_size_blocks;
		unsigned start_data_sector;
		unsigned start_data_block;
		unsigned badblocks = 0;
		int try, got;
		off_t currently_testing;
		char *blkbuf = xmalloc(BLOCK_SIZE * TEST_BUFFER_BLOCKS);

		volume_size_blocks = (volume_size_bytes >> BLOCK_SIZE_BITS);
		// N.B. the two following vars are in hard sectors, i.e. SECTOR_SIZE byte sectors!
		start_data_sector = (reserved_sect + NUM_FATS * sect_per_fat) * (bytes_per_sect / SECTOR_SIZE);
		start_data_block = (start_data_sector + SECTORS_PER_BLOCK - 1) / SECTORS_PER_BLOCK;

		bb_error_msg("searching for bad blocks");
		currently_testing = 0;
		try = TEST_BUFFER_BLOCKS;
		while (currently_testing < volume_size_blocks) {
			if (currently_testing + try > volume_size_blocks)
				try = volume_size_blocks - currently_testing;
			// perform a test on a block. return the number of blocks
			// that could be read successfully.
			// seek to the correct location
			xlseek(dev, currently_testing * BLOCK_SIZE, SEEK_SET);
			// try reading
			got = read(dev, blkbuf, try * BLOCK_SIZE);
			if (got < 0)
				got = 0;
			if (got & (BLOCK_SIZE - 1))
				bb_error_msg("unexpected values in do_check: probably bugs");
			got /= BLOCK_SIZE;
			currently_testing += got;
			if (got == try) {
				try = TEST_BUFFER_BLOCKS;
				continue;
			}
			try = 1;
			if (currently_testing < start_data_block)
				bb_error_msg_and_die("bad blocks before data-area: cannot make fs");

			// mark all of the sectors in the block as bad
			for (i = 0; i < SECTORS_PER_BLOCK; i++) {
				int cluster = (currently_testing * SECTORS_PER_BLOCK + i - start_data_sector) / (int) (sect_per_clust) / (bytes_per_sect / SECTOR_SIZE);
				if (cluster < 0)
					bb_error_msg_and_die("invalid cluster number in mark_sector: probably bug!");
				MARK_CLUSTER(cluster, BAD_FAT32);
			}
			badblocks++;
			currently_testing++;
		}
		free(blkbuf);
		if (badblocks)
			bb_error_msg("%d bad block(s)", badblocks);
	}
#endif

	// cleanup
	if (ENABLE_FEATURE_CLEAN_UP) {
		free(buf);
		close(dev);
	}

	return 0;
}

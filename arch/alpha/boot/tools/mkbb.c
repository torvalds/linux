/* This utility makes a bootblock suitable for the SRM console/miniloader */

/* Usage:
 *	mkbb <device> <lxboot>
 *
 * Where <device> is the name of the device to install the bootblock on,
 * and <lxboot> is the name of a bootblock to merge in.  This bootblock
 * contains the offset and size of the bootloader.  It must be exactly
 * 512 bytes long.
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

/* Minimal definition of disklabel, so we don't have to include
 * asm/disklabel.h (confuses make)
 */
#ifndef MAXPARTITIONS
#define MAXPARTITIONS   8                       /* max. # of partitions */
#endif

#ifndef u8
#define u8 unsigned char
#endif

#ifndef u16
#define u16 unsigned short
#endif

#ifndef u32
#define u32 unsigned int
#endif

struct disklabel {
    u32	d_magic;				/* must be DISKLABELMAGIC */
    u16	d_type, d_subtype;
    u8	d_typename[16];
    u8	d_packname[16];
    u32	d_secsize;
    u32	d_nsectors;
    u32	d_ntracks;
    u32	d_ncylinders;
    u32	d_secpercyl;
    u32	d_secprtunit;
    u16	d_sparespertrack;
    u16	d_sparespercyl;
    u32	d_acylinders;
    u16	d_rpm, d_interleave, d_trackskew, d_cylskew;
    u32	d_headswitch, d_trkseek, d_flags;
    u32	d_drivedata[5];
    u32	d_spare[5];
    u32	d_magic2;				/* must be DISKLABELMAGIC */
    u16	d_checksum;
    u16	d_npartitions;
    u32	d_bbsize, d_sbsize;
    struct d_partition {
	u32	p_size;
	u32	p_offset;
	u32	p_fsize;
	u8	p_fstype;
	u8	p_frag;
	u16	p_cpg;
    } d_partitions[MAXPARTITIONS];
};


typedef union __bootblock {
    struct {
        char			__pad1[64];
        struct disklabel	__label;
    } __u1;
    struct {
	unsigned long		__pad2[63];
	unsigned long		__checksum;
    } __u2;
    char		bootblock_bytes[512];
    unsigned long	bootblock_quadwords[64];
} bootblock;

#define	bootblock_label		__u1.__label
#define bootblock_checksum	__u2.__checksum

main(int argc, char ** argv)
{
    bootblock		bootblock_from_disk;
    bootblock		bootloader_image;
    int			dev, fd;
    int			i;
    int			nread;

    /* Make sure of the arg count */
    if(argc != 3) {
	fprintf(stderr, "Usage: %s device lxboot\n", argv[0]);
	exit(0);
    }

    /* First, open the device and make sure it's accessible */
    dev = open(argv[1], O_RDWR);
    if(dev < 0) {
	perror(argv[1]);
	exit(0);
    }

    /* Now open the lxboot and make sure it's reasonable */
    fd = open(argv[2], O_RDONLY);
    if(fd < 0) {
	perror(argv[2]);
	close(dev);
	exit(0);
    }

    /* Read in the lxboot */
    nread = read(fd, &bootloader_image, sizeof(bootblock));
    if(nread != sizeof(bootblock)) {
	perror("lxboot read");
	fprintf(stderr, "expected %d, got %d\n", sizeof(bootblock), nread);
	exit(0);
    }

    /* Read in the bootblock from disk. */
    nread = read(dev, &bootblock_from_disk, sizeof(bootblock));
    if(nread != sizeof(bootblock)) {
	perror("bootblock read");
	fprintf(stderr, "expected %d, got %d\n", sizeof(bootblock), nread);
	exit(0);
    }

    /* Swap the bootblock's disklabel into the bootloader */
    bootloader_image.bootblock_label = bootblock_from_disk.bootblock_label;

    /* Calculate the bootblock checksum */
    bootloader_image.bootblock_checksum = 0;
    for(i = 0; i < 63; i++) {
	bootloader_image.bootblock_checksum += 
			bootloader_image.bootblock_quadwords[i];
    }

    /* Write the whole thing out! */
    lseek(dev, 0L, SEEK_SET);
    if(write(dev, &bootloader_image, sizeof(bootblock)) != sizeof(bootblock)) {
	perror("bootblock write");
	exit(0);
    }

    close(fd);
    close(dev);
    exit(0);
}



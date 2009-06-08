/*
 * Makes a tree bootable image for IBM Evaluation boards.
 * Basically, just take a zImage, skip the ELF header, and stuff
 * a 32 byte header on the front.
 *
 * We use htonl, which is a network macro, to make sure we're doing
 * The Right Thing on an LE machine.  It's non-obvious, but it should
 * work on anything BSD'ish.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#ifdef __sun__
#include <inttypes.h>
#else
#include <stdint.h>
#endif

/* This gets tacked on the front of the image.  There are also a few
 * bytes allocated after the _start label used by the boot rom (see
 * head.S for details).
 */
typedef struct boot_block {
	uint32_t bb_magic;		/* 0x0052504F */
	uint32_t bb_dest;		/* Target address of the image */
	uint32_t bb_num_512blocks;	/* Size, rounded-up, in 512 byte blks */
	uint32_t bb_debug_flag;	/* Run debugger or image after load */
	uint32_t bb_entry_point;	/* The image address to start */
	uint32_t bb_checksum;	/* 32 bit checksum including header */
	uint32_t reserved[2];
} boot_block_t;

#define IMGBLK	512
char	tmpbuf[IMGBLK];

int main(int argc, char *argv[])
{
	int	in_fd, out_fd;
	int	nblks, i;
	unsigned int	cksum, *cp;
	struct	stat	st;
	boot_block_t	bt;

	if (argc < 5) {
		fprintf(stderr, "usage: %s <zImage-file> <boot-image> <load address> <entry point>\n",argv[0]);
		exit(1);
	}

	if (stat(argv[1], &st) < 0) {
		perror("stat");
		exit(2);
	}

	nblks = (st.st_size + IMGBLK) / IMGBLK;

	bt.bb_magic = htonl(0x0052504F);

	/* If we have the optional entry point parameter, use it */
	bt.bb_dest = htonl(strtoul(argv[3], NULL, 0));
	bt.bb_entry_point = htonl(strtoul(argv[4], NULL, 0));

	/* We know these from the linker command.
	 * ...and then move it up into memory a little more so the
	 * relocation can happen.
	 */
	bt.bb_num_512blocks = htonl(nblks);
	bt.bb_debug_flag = 0;

	bt.bb_checksum = 0;

	/* To be neat and tidy :-).
	*/
	bt.reserved[0] = 0;
	bt.reserved[1] = 0;

	if ((in_fd = open(argv[1], O_RDONLY)) < 0) {
		perror("zImage open");
		exit(3);
	}

	if ((out_fd = open(argv[2], (O_RDWR | O_CREAT | O_TRUNC), 0666)) < 0) {
		perror("bootfile open");
		exit(3);
	}

	cksum = 0;
	cp = (void *)&bt;
	for (i = 0; i < sizeof(bt) / sizeof(unsigned int); i++)
		cksum += *cp++;

	/* Assume zImage is an ELF file, and skip the 64K header.
	*/
	if (read(in_fd, tmpbuf, IMGBLK) != IMGBLK) {
		fprintf(stderr, "%s is too small to be an ELF image\n",
				argv[1]);
		exit(4);
	}

	if ((*(unsigned int *)tmpbuf) != htonl(0x7f454c46)) {
		fprintf(stderr, "%s is not an ELF image\n", argv[1]);
		exit(4);
	}

	if (lseek(in_fd, (64 * 1024), SEEK_SET) < 0) {
		fprintf(stderr, "%s failed to seek in ELF image\n", argv[1]);
		exit(4);
	}

	nblks -= (64 * 1024) / IMGBLK;

	/* And away we go......
	*/
	if (write(out_fd, &bt, sizeof(bt)) != sizeof(bt)) {
		perror("boot-image write");
		exit(5);
	}

	while (nblks-- > 0) {
		if (read(in_fd, tmpbuf, IMGBLK) < 0) {
			perror("zImage read");
			exit(5);
		}
		cp = (unsigned int *)tmpbuf;
		for (i = 0; i < sizeof(tmpbuf) / sizeof(unsigned int); i++)
			cksum += *cp++;
		if (write(out_fd, tmpbuf, sizeof(tmpbuf)) != sizeof(tmpbuf)) {
			perror("boot-image write");
			exit(5);
		}
	}

	/* rewrite the header with the computed checksum.
	*/
	bt.bb_checksum = htonl(cksum);
	if (lseek(out_fd, 0, SEEK_SET) < 0) {
		perror("rewrite seek");
		exit(1);
	}
	if (write(out_fd, &bt, sizeof(bt)) != sizeof(bt)) {
		perror("boot-image rewrite");
		exit(1);
	}

	exit(0);
}

/*
 * Makes a Motorola PPCBUG ROM bootable image which can be flashed
 * into one of the FLASH banks on a Motorola PowerPlus board.
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#define ELF_HEADER_SIZE	65536

#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#ifdef __sun__
#include <inttypes.h>
#else
#include <stdint.h>
#endif

/* size of read buffer */
#define SIZE 0x1000

/* PPCBUG ROM boot header */
typedef struct bug_boot_header {
  uint8_t	magic_word[4];		/* "BOOT" */
  uint32_t	entry_offset;		/* Offset from top of header to code */
  uint32_t	routine_length;		/* Length of code */
  uint8_t	routine_name[8];	/* Name of the boot code */
} bug_boot_header_t;

#define HEADER_SIZE	sizeof(bug_boot_header_t)

void update_checksum(void *buf, size_t size, uint16_t *sum)
{
	uint32_t csum = *sum;

	while (size) {
		csum += *(uint16_t *)buf;
		if (csum > 0xffff)
			csum -= 0xffff;
		buf = (uint16_t *)buf + 1;
		size -= 2;
	}
	*sum = csum;
}

uint32_t copy_image(int in_fd, int out_fd, uint16_t *sum)
{
	uint8_t buf[SIZE];
	int offset = 0;
	int n;
	uint32_t image_size = 0;

	lseek(in_fd, ELF_HEADER_SIZE, SEEK_SET);

	/* Copy an image while recording its size */
	while ( (n = read(in_fd, buf + offset, SIZE - offset)) > 0 ) {
		n += offset;
		offset = n & 1;
		n -= offset;
		image_size = image_size + n;
		/* who's going to deal with short writes? */
		write(out_fd, buf, n);
		update_checksum(buf, n, sum);
		if (offset)
			buf[0] = buf[n];
	}

	/* BUG romboot requires that our size is divisible by 2 */
	/* align image to 2 byte boundary */
	if (offset) {
		image_size += 2;
		buf[1] = '\0';
		write(out_fd, buf, 2);
		update_checksum(buf, 2, sum);
	}
	return image_size;
}

void write_bugboot_header(int out_fd, uint32_t boot_size, uint16_t *sum)
{
	static bug_boot_header_t bbh = {
		.magic_word = "BOOT",
		.routine_name = "LINUXROM"
	};

	/* Fill in the PPCBUG ROM boot header */
	bbh.entry_offset = htonl(HEADER_SIZE);	/* Entry address */
	bbh.routine_length= htonl(HEADER_SIZE+boot_size+2);	/* Routine length */

	/* Output the header and bootloader to the file */
	write(out_fd, &bbh, sizeof(bug_boot_header_t));
	update_checksum(&bbh, sizeof(bug_boot_header_t), sum);
}

int main(int argc, char *argv[])
{
	int image_fd, bugboot_fd;
	uint32_t kernel_size = 0;
	uint16_t checksum = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <kernel_image> <bugboot>\n",argv[0]);
		exit(-1);
	}

	/* Get file args */

	/* kernel image file */
	if ((image_fd = open(argv[1] , 0)) < 0)
		exit(-1);

	/* bugboot file */
	if (!strcmp(argv[2], "-"))
		bugboot_fd = 1;			/* stdout */
	else if ((bugboot_fd = creat(argv[2] , 0755)) < 0)
		exit(-1);

	/* Set file position after ROM header block where zImage will be written */
	lseek(bugboot_fd, HEADER_SIZE, SEEK_SET);

	/* Copy kernel image into bugboot image */
	kernel_size = copy_image(image_fd, bugboot_fd, &checksum);

	/* Set file position to beginning where header/romboot will be written */
	lseek(bugboot_fd, 0, SEEK_SET);

	/* Write out BUG header/romboot */
	write_bugboot_header(bugboot_fd, kernel_size, &checksum);

	/* Write out the calculated checksum */
	lseek(bugboot_fd, 0, SEEK_END);
	write(bugboot_fd, &checksum, 2);

	/* Close bugboot file */
	close(bugboot_fd);
	return 0;
}

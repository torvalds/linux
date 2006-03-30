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
#ifdef __sun__
#include <inttypes.h>
#else
#include <stdint.h>
#endif

#ifdef __i386__
#define cpu_to_be32(x) le32_to_cpu(x)
#define cpu_to_be16(x) le16_to_cpu(x)
#else
#define cpu_to_be32(x) (x)
#define cpu_to_be16(x) (x)
#endif

#define cpu_to_le32(x) le32_to_cpu((x))
unsigned long le32_to_cpu(unsigned long x)
{
     	return (((x & 0x000000ffU) << 24) |
		((x & 0x0000ff00U) <<  8) |
		((x & 0x00ff0000U) >>  8) |
		((x & 0xff000000U) >> 24));
}

#define cpu_to_le16(x) le16_to_cpu((x))
unsigned short le16_to_cpu(unsigned short x)
{
	return (((x & 0x00ff) << 8) |
		((x & 0xff00) >> 8));
}

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

uint32_t copy_image(int32_t in_fd, int32_t out_fd)
{
  uint8_t buf[SIZE];
  int n;
  uint32_t image_size = 0;
  uint8_t zero = 0;

  lseek(in_fd, ELF_HEADER_SIZE, SEEK_SET);

  /* Copy an image while recording its size */
  while ( (n = read(in_fd, buf, SIZE)) > 0 )
    {
    image_size = image_size + n;
    write(out_fd, buf, n);
    }

  /* BUG romboot requires that our size is divisible by 2 */
  /* align image to 2 byte boundary */
  if (image_size % 2)
    {
    image_size++;
    write(out_fd, &zero, 1);
    }

  return image_size;
}

void write_bugboot_header(int32_t out_fd, uint32_t boot_size)
{
  uint8_t header_block[HEADER_SIZE];
  bug_boot_header_t *bbh = (bug_boot_header_t *)&header_block[0];

  memset(header_block, 0, HEADER_SIZE);

  /* Fill in the PPCBUG ROM boot header */
  strncpy(bbh->magic_word, "BOOT", 4);		/* PPCBUG magic word */
  bbh->entry_offset = cpu_to_be32(HEADER_SIZE);	/* Entry address */
  bbh->routine_length= cpu_to_be32(HEADER_SIZE+boot_size+2);	/* Routine length */
  strncpy(bbh->routine_name, "LINUXROM", 8);		/* Routine name   */

  /* Output the header and bootloader to the file */
  write(out_fd, header_block, HEADER_SIZE);
}

uint16_t calc_checksum(int32_t bug_fd)
{
  uint32_t checksum_var = 0;
  uint8_t buf[2];
  int n;

  /* Checksum loop */
  while ( (n = read(bug_fd, buf, 2) ) )
  {
    checksum_var = checksum_var + *(uint16_t *)buf;

    /* If we carry out, mask it and add one to the checksum */
    if (checksum_var >> 16)
      checksum_var = (checksum_var & 0x0000ffff) + 1;
  }

  return checksum_var;
}

int main(int argc, char *argv[])
{
  int32_t image_fd, bugboot_fd;
  int argptr = 1;
  uint32_t kernel_size = 0;
  uint16_t checksum = 0;
  uint8_t bugbootname[256];

  if ( (argc != 3) )
  {
    fprintf(stderr, "usage: %s <kernel_image> <bugboot>\n",argv[0]);
    exit(-1);
  }

  /* Get file args */

  /* kernel image file */
    if ((image_fd = open( argv[argptr] , 0)) < 0)
      exit(-1);
  argptr++;

  /* bugboot file */
  if ( !strcmp( argv[argptr], "-" ) )
    bugboot_fd = 1;			/* stdout */
  else
    if ((bugboot_fd = creat( argv[argptr] , 0755)) < 0)
      exit(-1);
    else
      strcpy(bugbootname, argv[argptr]);
  argptr++;

  /* Set file position after ROM header block where zImage will be written */
  lseek(bugboot_fd, HEADER_SIZE, SEEK_SET);

  /* Copy kernel image into bugboot image */
  kernel_size = copy_image(image_fd, bugboot_fd);
  close(image_fd);

  /* Set file position to beginning where header/romboot will be written */
  lseek(bugboot_fd, 0, SEEK_SET);

  /* Write out BUG header/romboot */
  write_bugboot_header(bugboot_fd, kernel_size);

  /* Close bugboot file */
  close(bugboot_fd);

  /* Reopen it as read/write */
  bugboot_fd = open(bugbootname, O_RDWR);

  /* Calculate checksum */
  checksum = calc_checksum(bugboot_fd);

  /* Write out the calculated checksum */
  write(bugboot_fd, &checksum, 2);

  return 0;
}

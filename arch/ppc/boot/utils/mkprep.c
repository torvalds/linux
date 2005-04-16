/*
 * Makes a prep bootable image which can be dd'd onto
 * a disk device to make a bootdisk.  Will take
 * as input a elf executable, strip off the header
 * and write out a boot image as:
 * 1) default - strips elf header
 *      suitable as a network boot image
 * 2) -pbp - strips elf header and writes out prep boot partition image
 *      cat or dd onto disk for booting
 * 3) -asm - strips elf header and writes out as asm data
 *      useful for generating data for a compressed image
 *                  -- Cort
 *
 * Modified for x86 hosted builds by Matt Porter <porter@neta.com>
 * Modified for Sparc hosted builds by Peter Wahl <PeterWahl@web.de>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

#define cpu_to_be32(x) (x)
#define be32_to_cpu(x) (x)
#define cpu_to_be16(x) (x)
#define be16_to_cpu(x) (x)

/* size of read buffer */
#define SIZE 0x1000


typedef unsigned long dword_t;
typedef unsigned short word_t;
typedef unsigned char byte_t;
typedef byte_t block_t[512];
typedef byte_t page_t[4096];


/*
 * Partition table entry
 *  - from the PReP spec
 */
typedef struct partition_entry {
  byte_t	boot_indicator;
  byte_t	starting_head;
  byte_t	starting_sector;
  byte_t	starting_cylinder;

  byte_t	system_indicator;
  byte_t	ending_head;
  byte_t	ending_sector;
  byte_t	ending_cylinder;

  dword_t	beginning_sector;
  dword_t	number_of_sectors;
} partition_entry_t;

#define BootActive	0x80
#define SystemPrep	0x41

void copy_image(int , int);
void write_prep_partition(int , int );
void write_asm_data( int in, int out );

unsigned int elfhdr_size = 65536;

int main(int argc, char *argv[])
{
  int in_fd, out_fd;
  int argptr = 1;
  unsigned int prep = 0;
  unsigned int asmoutput = 0;

  if ( (argc < 3) || (argc > 4) )
  {
    fprintf(stderr, "usage: %s [-pbp] [-asm] <boot-file> <image>\n",argv[0]);
    exit(-1);
  }

  /* needs to handle args more elegantly -- but this is a small/simple program */

  /* check for -pbp */
  if ( !strcmp( argv[argptr], "-pbp" ) )
  {
    prep = 1;
    argptr++;
  }

  /* check for -asm */
  if ( !strcmp( argv[argptr], "-asm" ) )
  {
    asmoutput = 1;
    argptr++;
  }

  /* input file */
  if ( !strcmp( argv[argptr], "-" ) )
    in_fd = 0;			/* stdin */
  else
    if ((in_fd = open( argv[argptr] , 0)) < 0)
      exit(-1);
  argptr++;

  /* output file */
  if ( !strcmp( argv[argptr], "-" ) )
    out_fd = 1;			/* stdout */
  else
    if ((out_fd = creat( argv[argptr] , 0755)) < 0)
      exit(-1);
  argptr++;

  /* skip elf header in input file */
  /*if ( !prep )*/
  lseek(in_fd, elfhdr_size, SEEK_SET);

  /* write prep partition if necessary */
  if ( prep )
	  write_prep_partition( in_fd, out_fd );

  /* write input image to bootimage */
  if ( asmoutput )
	  write_asm_data( in_fd, out_fd );
  else
	  copy_image(in_fd, out_fd);

  return 0;
}

void write_prep_partition(int in, int out)
{
  unsigned char block[512];
  partition_entry_t pe;
  dword_t *entry  = (dword_t *)&block[0];
  dword_t *length = (dword_t *)&block[sizeof(long)];
  struct stat info;

  if (fstat(in, &info) < 0)
  {
    fprintf(stderr,"info failed\n");
    exit(-1);
  }

  bzero( block, sizeof block );

  /* set entry point and boot image size skipping over elf header */
#ifdef __i386__
  *entry = 0x400/*+65536*/;
  *length = info.st_size-elfhdr_size+0x400;
#else
  *entry = cpu_to_le32(0x400/*+65536*/);
  *length = cpu_to_le32(info.st_size-elfhdr_size+0x400);
#endif /* __i386__ */

  /* sets magic number for msdos partition (used by linux) */
  block[510] = 0x55;
  block[511] = 0xAA;

  /*
   * Build a "PReP" partition table entry in the boot record
   *  - "PReP" may only look at the system_indicator
   */
  pe.boot_indicator   = BootActive;
  pe.system_indicator = SystemPrep;
  /*
   * The first block of the diskette is used by this "boot record" which
   * actually contains the partition table. (The first block of the
   * partition contains the boot image, but I digress...)  We'll set up
   * one partition on the diskette and it shall contain the rest of the
   * diskette.
   */
  pe.starting_head     = 0;	/* zero-based			     */
  pe.starting_sector   = 2;	/* one-based			     */
  pe.starting_cylinder = 0;	/* zero-based			     */
  pe.ending_head       = 1;	/* assumes two heads		     */
  pe.ending_sector     = 18;	/* assumes 18 sectors/track	     */
  pe.ending_cylinder   = 79;	/* assumes 80 cylinders/diskette     */

  /*
   * The "PReP" software ignores the above fields and just looks at
   * the next two.
   *   - size of the diskette is (assumed to be)
   *     (2 tracks/cylinder)(18 sectors/tracks)(80 cylinders/diskette)
   *   - unlike the above sector numbers, the beginning sector is zero-based!
   */
#if 0
  pe.beginning_sector  = cpu_to_le32(1);
#else
  /* This has to be 0 on the PowerStack? */
#ifdef __i386__
  pe.beginning_sector  = 0;
#else
  pe.beginning_sector  = cpu_to_le32(0);
#endif /* __i386__ */
#endif

#ifdef __i386__
  pe.number_of_sectors = 2*18*80-1;
#else
  pe.number_of_sectors = cpu_to_le32(2*18*80-1);
#endif /* __i386__ */

  memcpy(&block[0x1BE], &pe, sizeof(pe));

  write( out, block, sizeof(block) );
  write( out, entry, sizeof(*entry) );
  write( out, length, sizeof(*length) );
  /* set file position to 2nd sector where image will be written */
  lseek( out, 0x400, SEEK_SET );
}



void
copy_image(int in, int out)
{
  char buf[SIZE];
  int n;

  while ( (n = read(in, buf, SIZE)) > 0 )
    write(out, buf, n);
}


void
write_asm_data( int in, int out )
{
  int i, cnt, pos, len;
  unsigned int cksum, val;
  unsigned char *lp;
  unsigned char buf[SIZE];
  unsigned char str[256];

  write( out, "\t.data\n\t.globl input_data\ninput_data:\n",
	 strlen( "\t.data\n\t.globl input_data\ninput_data:\n" ) );
  pos = 0;
  cksum = 0;
  while ((len = read(in, buf, sizeof(buf))) > 0)
  {
    cnt = 0;
    lp = (unsigned char *)buf;
    len = (len + 3) & ~3;  /* Round up to longwords */
    for (i = 0;  i < len;  i += 4)
    {
      if (cnt == 0)
      {
	write( out, "\t.long\t", strlen( "\t.long\t" ) );
      }
      sprintf( str, "0x%02X%02X%02X%02X", lp[0], lp[1], lp[2], lp[3]);
      write( out, str, strlen(str) );
      val = *(unsigned long *)lp;
      cksum ^= val;
      lp += 4;
      if (++cnt == 4)
      {
	cnt = 0;
	sprintf( str, " # %x \n", pos+i-12);
	write( out, str, strlen(str) );
      } else
      {
	write( out, ",", 1 );
      }
    }
    if (cnt)
    {
      write( out, "0\n", 2 );
    }
    pos += len;
  }
  sprintf(str, "\t.globl input_len\ninput_len:\t.long\t0x%x\n", pos);
  write( out, str, strlen(str) );

  fprintf(stderr, "cksum = %x\n", cksum);
}

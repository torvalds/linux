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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* size of read buffer */
#define SIZE 0x1000

/*
 * Partition table entry
 *  - from the PReP spec
 */
typedef struct partition_entry {
	unsigned char boot_indicator;
	unsigned char starting_head;
	unsigned char starting_sector;
	unsigned char starting_cylinder;

	unsigned char system_indicator;
	unsigned char ending_head;
	unsigned char ending_sector;
	unsigned char ending_cylinder;

	unsigned char beginning_sector[4];
	unsigned char number_of_sectors[4];
} partition_entry_t;

#define BootActive	0x80
#define SystemPrep	0x41

void copy_image(FILE *, FILE *);
void write_prep_partition(FILE *, FILE *);
void write_asm_data(FILE *, FILE *);

unsigned int elfhdr_size = 65536;

int main(int argc, char *argv[])
{
	FILE *in, *out;
	int argptr = 1;
	int prep = 0;
	int asmoutput = 0;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: %s [-pbp] [-asm] <boot-file> <image>\n",
			argv[0]);
		exit(-1);
	}

/* needs to handle args more elegantly -- but this is a small/simple program */

	/* check for -pbp */
	if (!strcmp(argv[argptr], "-pbp")) {
		prep = 1;
		argptr++;
	}

	/* check for -asm */
	if (!strcmp(argv[argptr], "-asm")) {
		asmoutput = 1;
		argptr++;
	}

	/* input file */
	if (!strcmp(argv[argptr], "-"))
		in = stdin;
	else if (!(in = fopen(argv[argptr], "r")))
		exit(-1);
	argptr++;

	/* output file */
	if (!strcmp(argv[argptr], "-"))
		out = stdout;
	else if (!(out = fopen(argv[argptr], "w")))
		exit(-1);
	argptr++;

	/* skip elf header in input file */
	/*if ( !prep )*/
	fseek(in, elfhdr_size, SEEK_SET);

	/* write prep partition if necessary */
	if (prep)
		write_prep_partition(in, out);

	/* write input image to bootimage */
	if (asmoutput)
		write_asm_data(in, out);
	else
		copy_image(in, out);

	return 0;
}

void store_le32(unsigned int v, unsigned char *p)
{
	p[0] = v;
	p[1] = v >>= 8;
	p[2] = v >>= 8;
	p[3] = v >> 8;
}

void write_prep_partition(FILE *in, FILE *out)
{
	unsigned char block[512];
	partition_entry_t pe;
	unsigned char *entry  = block;
	unsigned char *length = block + 4;
	long pos = ftell(in), size;

	if (fseek(in, 0, SEEK_END) < 0) {
		fprintf(stderr,"info failed\n");
		exit(-1);
	}
	size = ftell(in);
	if (fseek(in, pos, SEEK_SET) < 0) {
		fprintf(stderr,"info failed\n");
		exit(-1);
	}

	memset(block, '\0', sizeof(block));

	/* set entry point and boot image size skipping over elf header */
	store_le32(0x400/*+65536*/, entry);
	store_le32(size-elfhdr_size+0x400, length);

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
	store_le32(1, pe.beginning_sector);
#else
	/* This has to be 0 on the PowerStack? */
	store_le32(0, pe.beginning_sector);
#endif

	store_le32(2*18*80-1, pe.number_of_sectors);

	memcpy(&block[0x1BE], &pe, sizeof(pe));

	fwrite(block, sizeof(block), 1, out);
	fwrite(entry, 4, 1, out);
	fwrite(length, 4, 1, out);
	/* set file position to 2nd sector where image will be written */
	fseek( out, 0x400, SEEK_SET );
}



void copy_image(FILE *in, FILE *out)
{
	char buf[SIZE];
	int n;

	while ( (n = fread(buf, 1, SIZE, in)) > 0 )
		fwrite(buf, 1, n, out);
}


void
write_asm_data(FILE *in, FILE *out)
{
	int i, cnt, pos = 0;
	unsigned int cksum = 0, val;
	unsigned char *lp;
	unsigned char buf[SIZE];
	size_t len;

	fputs("\t.data\n\t.globl input_data\ninput_data:\n", out);
	while ((len = fread(buf, 1, sizeof(buf), in)) > 0) {
		cnt = 0;
		lp = buf;
		/* Round up to longwords */
		while (len & 3)
			buf[len++] = '\0';
		for (i = 0;  i < len;  i += 4) {
			if (cnt == 0)
				fputs("\t.long\t", out);
			fprintf(out, "0x%02X%02X%02X%02X",
				lp[0], lp[1], lp[2], lp[3]);
			val = *(unsigned long *)lp;
			cksum ^= val;
			lp += 4;
			if (++cnt == 4) {
				cnt = 0;
				fprintf(out, " # %x \n", pos+i-12);
			} else {
				fputs(",", out);
			}
		}
		if (cnt)
			fputs("0\n", out);
		pos += len;
	}
	fprintf(out, "\t.globl input_len\ninput_len:\t.long\t0x%x\n", pos);
	fprintf(stderr, "cksum = %x\n", cksum);
}

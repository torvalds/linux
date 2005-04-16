#include <stdio.h>
#include <stdlib.h>
#include <byteswap.h>
#include <sys/types.h>
#include <sys/stat.h>

void xlate( char * inb, char * trb, unsigned len )
{
    unsigned i;
    for (  i=0; i<len; ++i ) {
	char c = *inb++;
	char c1 = c >> 4;
	char c2 = c & 0xf;
	if ( c1 > 9 )
	    c1 = c1 + 'A' - 10;
	else
	    c1 = c1 + '0';
	if ( c2 > 9 )
	    c2 = c2 + 'A' - 10;
	else
	    c2 = c2 + '0';
	*trb++ = c1;
	*trb++ = c2;
    }
    *trb = 0;
}

#define ElfHeaderSize  (64 * 1024)
#define ElfPages  (ElfHeaderSize / 4096)
#define KERNELBASE (0xc0000000)

void get4k( /*istream *inf*/FILE *file, char *buf )
{
    unsigned j;
    unsigned num = fread(buf, 1, 4096, file);
    for (  j=num; j<4096; ++j )
	buf[j] = 0;
}

void put4k( /*ostream *outf*/FILE *file, char *buf )
{
    fwrite(buf, 1, 4096, file);
}

int main(int argc, char **argv)
{
    char inbuf[4096];
    FILE *ramDisk = NULL;
    FILE *inputVmlinux = NULL;
    FILE *outputVmlinux = NULL;
    unsigned i = 0;
    unsigned long ramFileLen = 0;
    unsigned long ramLen = 0;
    unsigned long roundR = 0;
    unsigned long kernelLen = 0;
    unsigned long actualKernelLen = 0;
    unsigned long round = 0;
    unsigned long roundedKernelLen = 0;
    unsigned long ramStartOffs = 0;
    unsigned long ramPages = 0;
    unsigned long roundedKernelPages = 0;
    if ( argc < 2 ) {
	printf("Name of System Map file missing.\n");
	exit(1);
    }

    if ( argc < 3 ) {
	printf("Name of vmlinux file missing.\n");
	exit(1);
    }

    if ( argc < 4 ) {
	printf("Name of vmlinux output file missing.\n");
	exit(1);
    }

    ramDisk = fopen(argv[1], "r");
    if ( ! ramDisk ) {
	printf("System Map file \"%s\" failed to open.\n", argv[1]);
	exit(1);
    }
    inputVmlinux = fopen(argv[2], "r");
    if ( ! inputVmlinux ) {
	printf("vmlinux file \"%s\" failed to open.\n", argv[2]);
	exit(1);
    }
    outputVmlinux = fopen(argv[3], "w");
    if ( ! outputVmlinux ) {
	printf("output vmlinux file \"%s\" failed to open.\n", argv[3]);
	exit(1);
    }
    fseek(ramDisk, 0, SEEK_END);
    ramFileLen = ftell(ramDisk);
    fseek(ramDisk, 0, SEEK_SET);
    printf("%s file size = %ld\n", argv[1], ramFileLen);

    ramLen = ramFileLen;

    roundR = 4096 - (ramLen % 4096);
    if ( roundR ) {
	printf("Rounding System Map file up to a multiple of 4096, adding %ld\n", roundR);
	ramLen += roundR;
    }

    printf("Rounded System Map size is %ld\n", ramLen);
    fseek(inputVmlinux, 0, SEEK_END);
    kernelLen = ftell(inputVmlinux);
    fseek(inputVmlinux, 0, SEEK_SET);
    printf("kernel file size = %ld\n", kernelLen);
    if ( kernelLen == 0 ) {
	printf("You must have a linux kernel specified as argv[2]\n");
	exit(1);
    }

    actualKernelLen = kernelLen - ElfHeaderSize;

    printf("actual kernel length (minus ELF header) = %ld\n", actualKernelLen);

    round = actualKernelLen % 4096;
    roundedKernelLen = actualKernelLen;
    if ( round )
	roundedKernelLen += (4096 - round);

    printf("actual kernel length rounded up to a 4k multiple = %ld\n", roundedKernelLen);

    ramStartOffs = roundedKernelLen;
    ramPages = ramLen / 4096;

    printf("System map pages to copy = %ld\n", ramPages);

    // Copy 64K ELF header
      for (i=0; i<(ElfPages); ++i) {
	  get4k( inputVmlinux, inbuf );
	  put4k( outputVmlinux, inbuf );
      }



    roundedKernelPages = roundedKernelLen / 4096;

    fseek(inputVmlinux, ElfHeaderSize, SEEK_SET);

    {
	for ( i=0; i<roundedKernelPages; ++i ) {
	    get4k( inputVmlinux, inbuf );
	    if ( i == 0 ) {
		unsigned long * p;
		printf("Storing embedded_sysmap_start at 0x3c\n");
		p = (unsigned long *)(inbuf + 0x3c);

#if (BYTE_ORDER == __BIG_ENDIAN)
		*p = ramStartOffs;
#else
		*p = bswap_32(ramStartOffs);
#endif

		printf("Storing embedded_sysmap_end at 0x44\n");
		p = (unsigned long *)(inbuf + 0x44);
#if (BYTE_ORDER == __BIG_ENDIAN)
		*p = ramStartOffs + ramFileLen;
#else
		*p = bswap_32(ramStartOffs + ramFileLen);
#endif
	    }
	    put4k( outputVmlinux, inbuf );
	}
    }

    {
	for ( i=0; i<ramPages; ++i ) {
	    get4k( ramDisk, inbuf );
	    put4k( outputVmlinux, inbuf );
	}
    }


    fclose(ramDisk);
    fclose(inputVmlinux);
    fclose(outputVmlinux);
    /* Set permission to executable */
    chmod(argv[3], S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

    return 0;

}


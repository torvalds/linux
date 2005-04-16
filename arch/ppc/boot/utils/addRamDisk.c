#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define ElfHeaderSize  (64 * 1024)
#define ElfPages  (ElfHeaderSize / 4096)
#define KERNELBASE (0xc0000000)

void get4k(FILE *file, char *buf )
{
    unsigned j;
    unsigned num = fread(buf, 1, 4096, file);
    for (  j=num; j<4096; ++j )
	buf[j] = 0;
}

void put4k(FILE *file, char *buf )
{
    fwrite(buf, 1, 4096, file);
}

void death(const char *msg, FILE *fdesc, const char *fname)
{
    printf(msg);
    fclose(fdesc);
    unlink(fname);
    exit(1);
}

int main(int argc, char **argv)
{
    char inbuf[4096];
    FILE *ramDisk = NULL;
    FILE *inputVmlinux = NULL;
    FILE *outputVmlinux = NULL;
    unsigned i = 0;
    u_int32_t ramFileLen = 0;
    u_int32_t ramLen = 0;
    u_int32_t roundR = 0;
    u_int32_t kernelLen = 0;
    u_int32_t actualKernelLen = 0;
    u_int32_t round = 0;
    u_int32_t roundedKernelLen = 0;
    u_int32_t ramStartOffs = 0;
    u_int32_t ramPages = 0;
    u_int32_t roundedKernelPages = 0;
    u_int32_t hvReleaseData = 0;
    u_int32_t eyeCatcher = 0xc8a5d9c4;
    u_int32_t naca = 0;
    u_int32_t xRamDisk = 0;
    u_int32_t xRamDiskSize = 0;
    if ( argc < 2 ) {
	printf("Name of RAM disk file missing.\n");
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
	printf("RAM disk file \"%s\" failed to open.\n", argv[1]);
	exit(1);
    }
    inputVmlinux = fopen(argv[2], "r");
    if ( ! inputVmlinux ) {
	printf("vmlinux file \"%s\" failed to open.\n", argv[2]);
	exit(1);
    }
    outputVmlinux = fopen(argv[3], "w+");
    if ( ! outputVmlinux ) {
	printf("output vmlinux file \"%s\" failed to open.\n", argv[3]);
	exit(1);
    }
    fseek(ramDisk, 0, SEEK_END);
    ramFileLen = ftell(ramDisk);
    fseek(ramDisk, 0, SEEK_SET);
    printf("%s file size = %d\n", argv[1], ramFileLen);

    ramLen = ramFileLen;

    roundR = 4096 - (ramLen % 4096);
    if ( roundR ) {
	printf("Rounding RAM disk file up to a multiple of 4096, adding %d\n", roundR);
	ramLen += roundR;
    }

    printf("Rounded RAM disk size is %d\n", ramLen);
    fseek(inputVmlinux, 0, SEEK_END);
    kernelLen = ftell(inputVmlinux);
    fseek(inputVmlinux, 0, SEEK_SET);
    printf("kernel file size = %d\n", kernelLen);
    if ( kernelLen == 0 ) {
	printf("You must have a linux kernel specified as argv[2]\n");
	exit(1);
    }

    actualKernelLen = kernelLen - ElfHeaderSize;

    printf("actual kernel length (minus ELF header) = %d\n", actualKernelLen);

    round = actualKernelLen % 4096;
    roundedKernelLen = actualKernelLen;
    if ( round )
	roundedKernelLen += (4096 - round);

    printf("actual kernel length rounded up to a 4k multiple = %d\n", roundedKernelLen);

    ramStartOffs = roundedKernelLen;
    ramPages = ramLen / 4096;

    printf("RAM disk pages to copy = %d\n", ramPages);

    // Copy 64K ELF header
      for (i=0; i<(ElfPages); ++i) {
	  get4k( inputVmlinux, inbuf );
	  put4k( outputVmlinux, inbuf );
      }

    roundedKernelPages = roundedKernelLen / 4096;

    fseek(inputVmlinux, ElfHeaderSize, SEEK_SET);

    for ( i=0; i<roundedKernelPages; ++i ) {
	get4k( inputVmlinux, inbuf );
	put4k( outputVmlinux, inbuf );
    }

    for ( i=0; i<ramPages; ++i ) {
	get4k( ramDisk, inbuf );
	put4k( outputVmlinux, inbuf );
    }

    /* Close the input files */
    fclose(ramDisk);
    fclose(inputVmlinux);
    /* And flush the written output file */
    fflush(outputVmlinux);

    /* fseek to the hvReleaseData pointer */
    fseek(outputVmlinux, ElfHeaderSize + 0x24, SEEK_SET);
    if (fread(&hvReleaseData, 4, 1, outputVmlinux) != 1) {
        death("Could not read hvReleaseData pointer\n", outputVmlinux, argv[3]);
    }
    hvReleaseData = ntohl(hvReleaseData); /* Convert to native int */
    printf("hvReleaseData is at %08x\n", hvReleaseData);

    /* fseek to the hvReleaseData */
    fseek(outputVmlinux, ElfHeaderSize + hvReleaseData, SEEK_SET);
    if (fread(inbuf, 0x40, 1, outputVmlinux) != 1) {
        death("Could not read hvReleaseData\n", outputVmlinux, argv[3]);
    }
    /* Check hvReleaseData sanity */
    if (memcmp(inbuf, &eyeCatcher, 4) != 0) {
        death("hvReleaseData is invalid\n", outputVmlinux, argv[3]);
    }
    /* Get the naca pointer */
    naca = ntohl(*((u_int32_t *) &inbuf[0x0c])) - KERNELBASE;
    printf("naca is at %08x\n", naca);

    /* fseek to the naca */
    fseek(outputVmlinux, ElfHeaderSize + naca, SEEK_SET);
    if (fread(inbuf, 0x18, 1, outputVmlinux) != 1) {
        death("Could not read naca\n", outputVmlinux, argv[3]);
    }
    xRamDisk = ntohl(*((u_int32_t *) &inbuf[0x0c]));
    xRamDiskSize = ntohl(*((u_int32_t *) &inbuf[0x14]));
    /* Make sure a RAM disk isn't already present */
    if ((xRamDisk != 0) || (xRamDiskSize != 0)) {
        death("RAM disk is already attached to this kernel\n", outputVmlinux, argv[3]);
    }
    /* Fill in the values */
    *((u_int32_t *) &inbuf[0x0c]) = htonl(ramStartOffs);
    *((u_int32_t *) &inbuf[0x14]) = htonl(ramPages);

    /* Write out the new naca */
    fflush(outputVmlinux);
    fseek(outputVmlinux, ElfHeaderSize + naca, SEEK_SET);
    if (fwrite(inbuf, 0x18, 1, outputVmlinux) != 1) {
        death("Could not write naca\n", outputVmlinux, argv[3]);
    }
    printf("RAM Disk of 0x%x pages size is attached to the kernel at offset 0x%08x\n",
            ramPages, ramStartOffs);

    /* Done */
    fclose(outputVmlinux);
    /* Set permission to executable */
    chmod(argv[3], S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

    return 0;
}


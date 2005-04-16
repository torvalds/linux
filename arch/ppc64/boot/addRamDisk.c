#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define ElfHeaderSize  (64 * 1024)
#define ElfPages  (ElfHeaderSize / 4096)
#define KERNELBASE (0xc000000000000000)

void get4k(FILE *file, char *buf )
{
	unsigned j;
	unsigned num = fread(buf, 1, 4096, file);
	for ( j=num; j<4096; ++j )
		buf[j] = 0;
}

void put4k(FILE *file, char *buf )
{
	fwrite(buf, 1, 4096, file);
}

void death(const char *msg, FILE *fdesc, const char *fname) 
{
	fprintf(stderr, msg);
	fclose(fdesc);
	unlink(fname);
	exit(1);
}

int main(int argc, char **argv)
{
	char inbuf[4096];
	FILE *ramDisk = NULL;
	FILE *sysmap = NULL;
	FILE *inputVmlinux = NULL;
	FILE *outputVmlinux = NULL;
  
	unsigned i = 0;
	unsigned long ramFileLen = 0;
	unsigned long ramLen = 0;
	unsigned long roundR = 0;
  
	unsigned long sysmapFileLen = 0;
	unsigned long sysmapLen = 0;
	unsigned long sysmapPages = 0;
	char* ptr_end = NULL; 
	unsigned long offset_end = 0;

	unsigned long kernelLen = 0;
	unsigned long actualKernelLen = 0;
	unsigned long round = 0;
	unsigned long roundedKernelLen = 0;
	unsigned long ramStartOffs = 0;
	unsigned long ramPages = 0;
	unsigned long roundedKernelPages = 0;
	unsigned long hvReleaseData = 0;
	u_int32_t eyeCatcher = 0xc8a5d9c4;
	unsigned long naca = 0;
	unsigned long xRamDisk = 0;
	unsigned long xRamDiskSize = 0;
	long padPages = 0;
  
  
	if (argc < 2) {
		fprintf(stderr, "Name of RAM disk file missing.\n");
		exit(1);
	}

	if (argc < 3) {
		fprintf(stderr, "Name of System Map input file is missing.\n");
		exit(1);
	}
  
	if (argc < 4) {
		fprintf(stderr, "Name of vmlinux file missing.\n");
		exit(1);
	}

	if (argc < 5) {
		fprintf(stderr, "Name of vmlinux output file missing.\n");
		exit(1);
	}


	ramDisk = fopen(argv[1], "r");
	if ( ! ramDisk ) {
		fprintf(stderr, "RAM disk file \"%s\" failed to open.\n", argv[1]);
		exit(1);
	}

	sysmap = fopen(argv[2], "r");
	if ( ! sysmap ) {
		fprintf(stderr, "System Map file \"%s\" failed to open.\n", argv[2]);
		exit(1);
	}
  
	inputVmlinux = fopen(argv[3], "r");
	if ( ! inputVmlinux ) {
		fprintf(stderr, "vmlinux file \"%s\" failed to open.\n", argv[3]);
		exit(1);
	}
  
	outputVmlinux = fopen(argv[4], "w+");
	if ( ! outputVmlinux ) {
		fprintf(stderr, "output vmlinux file \"%s\" failed to open.\n", argv[4]);
		exit(1);
	}
  
  
  
	/* Input Vmlinux file */
	fseek(inputVmlinux, 0, SEEK_END);
	kernelLen = ftell(inputVmlinux);
	fseek(inputVmlinux, 0, SEEK_SET);
	printf("kernel file size = %d\n", kernelLen);
	if ( kernelLen == 0 ) {
		fprintf(stderr, "You must have a linux kernel specified as argv[3]\n");
		exit(1);
	}

	actualKernelLen = kernelLen - ElfHeaderSize;

	printf("actual kernel length (minus ELF header) = %d\n", actualKernelLen);

	round = actualKernelLen % 4096;
	roundedKernelLen = actualKernelLen;
	if ( round )
		roundedKernelLen += (4096 - round);
	printf("Vmlinux length rounded up to a 4k multiple = %ld/0x%lx \n", roundedKernelLen, roundedKernelLen);
	roundedKernelPages = roundedKernelLen / 4096;
	printf("Vmlinux pages to copy = %ld/0x%lx \n", roundedKernelPages, roundedKernelPages);



	/* Input System Map file */
	/* (needs to be processed simply to determine if we need to add pad pages due to the static variables not being included in the vmlinux) */
	fseek(sysmap, 0, SEEK_END);
	sysmapFileLen = ftell(sysmap);
	fseek(sysmap, 0, SEEK_SET);
	printf("%s file size = %ld/0x%lx \n", argv[2], sysmapFileLen, sysmapFileLen);

	sysmapLen = sysmapFileLen;

	roundR = 4096 - (sysmapLen % 4096);
	if (roundR) {
		printf("Rounding System Map file up to a multiple of 4096, adding %ld/0x%lx \n", roundR, roundR);
		sysmapLen += roundR;
	}
	printf("Rounded System Map size is %ld/0x%lx \n", sysmapLen, sysmapLen);
  
	/* Process the Sysmap file to determine where _end is */
	sysmapPages = sysmapLen / 4096;
	/* read the whole file line by line, expect that it doesn't fail */
	while ( fgets(inbuf, 4096, sysmap) )  ;
	/* search for _end in the last page of the system map */
	ptr_end = strstr(inbuf, " _end");
	if (!ptr_end) {
		fprintf(stderr, "Unable to find _end in the sysmap file \n");
		fprintf(stderr, "inbuf: \n");
		fprintf(stderr, "%s \n", inbuf);
		exit(1);
	}
	printf("Found _end in the last page of the sysmap - backing up 10 characters it looks like %s", ptr_end-10);
	/* convert address of _end in system map to hex offset. */
	offset_end = (unsigned int)strtol(ptr_end-10, NULL, 16);
	/* calc how many pages we need to insert between the vmlinux and the start of the ram disk */
	padPages = offset_end/4096 - roundedKernelPages;

	/* Check and see if the vmlinux is already larger than _end in System.map */
	if (padPages < 0) {
		/* vmlinux is larger than _end - adjust the offset to the start of the embedded ram disk */ 
		offset_end = roundedKernelLen;
		printf("vmlinux is larger than _end indicates it needs to be - offset_end = %lx \n", offset_end);
		padPages = 0;
		printf("will insert %lx pages between the vmlinux and the start of the ram disk \n", padPages);
	}
	else {
		/* _end is larger than vmlinux - use the offset to _end that we calculated from the system map */
		printf("vmlinux is smaller than _end indicates is needed - offset_end = %lx \n", offset_end);
		printf("will insert %lx pages between the vmlinux and the start of the ram disk \n", padPages);
	}



	/* Input Ram Disk file */
	// Set the offset that the ram disk will be started at.
	ramStartOffs = offset_end;  /* determined from the input vmlinux file and the system map */
	printf("Ram Disk will start at offset = 0x%lx \n", ramStartOffs);
  
	fseek(ramDisk, 0, SEEK_END);
	ramFileLen = ftell(ramDisk);
	fseek(ramDisk, 0, SEEK_SET);
	printf("%s file size = %ld/0x%lx \n", argv[1], ramFileLen, ramFileLen);

	ramLen = ramFileLen;

	roundR = 4096 - (ramLen % 4096);
	if ( roundR ) {
		printf("Rounding RAM disk file up to a multiple of 4096, adding %ld/0x%lx \n", roundR, roundR);
		ramLen += roundR;
	}

	printf("Rounded RAM disk size is %ld/0x%lx \n", ramLen, ramLen);
	ramPages = ramLen / 4096;
	printf("RAM disk pages to copy = %ld/0x%lx\n", ramPages, ramPages);



  // Copy 64K ELF header
	for (i=0; i<(ElfPages); ++i) {
		get4k( inputVmlinux, inbuf );
		put4k( outputVmlinux, inbuf );
	}

	/* Copy the vmlinux (as full pages). */
	fseek(inputVmlinux, ElfHeaderSize, SEEK_SET);
	for ( i=0; i<roundedKernelPages; ++i ) {
		get4k( inputVmlinux, inbuf );
		put4k( outputVmlinux, inbuf );
	}
  
	/* Insert pad pages (if appropriate) that are needed between */
	/* | the end of the vmlinux and the ram disk. */
	for (i=0; i<padPages; ++i) {
		memset(inbuf, 0, 4096);
		put4k(outputVmlinux, inbuf);
	}

	/* Copy the ram disk (as full pages). */
	for ( i=0; i<ramPages; ++i ) {
		get4k( ramDisk, inbuf );
		put4k( outputVmlinux, inbuf );
	}

	/* Close the input files */
	fclose(ramDisk);
	fclose(inputVmlinux);
	/* And flush the written output file */
	fflush(outputVmlinux);



	/* Fixup the new vmlinux to contain the ram disk starting offset (xRamDisk) and the ram disk size (xRamDiskSize) */
	/* fseek to the hvReleaseData pointer */
	fseek(outputVmlinux, ElfHeaderSize + 0x24, SEEK_SET);
	if (fread(&hvReleaseData, 4, 1, outputVmlinux) != 1) {
		death("Could not read hvReleaseData pointer\n", outputVmlinux, argv[4]);
	}
	hvReleaseData = ntohl(hvReleaseData); /* Convert to native int */
	printf("hvReleaseData is at %08x\n", hvReleaseData);

	/* fseek to the hvReleaseData */
	fseek(outputVmlinux, ElfHeaderSize + hvReleaseData, SEEK_SET);
	if (fread(inbuf, 0x40, 1, outputVmlinux) != 1) {
		death("Could not read hvReleaseData\n", outputVmlinux, argv[4]);
	}
	/* Check hvReleaseData sanity */
	if (memcmp(inbuf, &eyeCatcher, 4) != 0) {
		death("hvReleaseData is invalid\n", outputVmlinux, argv[4]);
	}
	/* Get the naca pointer */
	naca = ntohl(*((u_int32_t*) &inbuf[0x0C])) - KERNELBASE;
	printf("Naca is at offset 0x%lx \n", naca);

	/* fseek to the naca */
	fseek(outputVmlinux, ElfHeaderSize + naca, SEEK_SET);
	if (fread(inbuf, 0x18, 1, outputVmlinux) != 1) {
		death("Could not read naca\n", outputVmlinux, argv[4]);
	}
	xRamDisk = ntohl(*((u_int32_t *) &inbuf[0x0c]));
	xRamDiskSize = ntohl(*((u_int32_t *) &inbuf[0x14]));
	/* Make sure a RAM disk isn't already present */
	if ((xRamDisk != 0) || (xRamDiskSize != 0)) {
		death("RAM disk is already attached to this kernel\n", outputVmlinux, argv[4]);
	}
	/* Fill in the values */
	*((u_int32_t *) &inbuf[0x0c]) = htonl(ramStartOffs);
	*((u_int32_t *) &inbuf[0x14]) = htonl(ramPages);

	/* Write out the new naca */
	fflush(outputVmlinux);
	fseek(outputVmlinux, ElfHeaderSize + naca, SEEK_SET);
	if (fwrite(inbuf, 0x18, 1, outputVmlinux) != 1) {
		death("Could not write naca\n", outputVmlinux, argv[4]);
	}
	printf("Ram Disk of 0x%lx pages is attached to the kernel at offset 0x%08x\n",
	       ramPages, ramStartOffs);

	/* Done */
	fclose(outputVmlinux);
	/* Set permission to executable */
	chmod(argv[4], S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

	return 0;
}


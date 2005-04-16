#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned char buf[8];
    unsigned int i, count, bytes = 0;
    FILE *fd_in, *fd_out;

    if (argc != 4) {
	fprintf(stderr, "\n\tusage: %s <ucode.bin> <array_name> <output_name>\n\n", argv[0]);
	return -1;
    }

    fd_in = fopen(argv[1], "rb");
    if (fd_in == NULL) {
	fprintf(stderr, "firmware file '%s' not found\n", argv[1]);
	return -1;
    }

    fd_out = fopen(argv[3], "w+");
    if (fd_out == NULL) {
	fprintf(stderr, "cannot create output file '%s'\n", argv[3]);
	return -1;
    }

    fprintf(fd_out, "\n#include <asm/types.h>\n\nu8 %s [] = {", argv[2]);

    while ((count = fread(buf, 1, 8, fd_in)) > 0) {
	fprintf(fd_out, "\n\t");
	for (i = 0; i < count; i++, bytes++)
	    fprintf(fd_out, "0x%02x, ", buf[i]);
    }

    fprintf(fd_out, "\n};\n\n");
    
    fclose(fd_in);
    fclose(fd_out);

    return 0;
}

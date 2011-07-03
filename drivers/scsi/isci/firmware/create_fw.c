#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

char blob_name[] = "isci_firmware.bin";
char id[] = "#SCU MAGIC#";
unsigned char version = 1;
unsigned char sub_version = 0;


/*
 * For all defined arrays:
 * elements 0-3 are for SCU0, ports 0-3
 * elements 4-7 are for SCU1, ports 0-3
 *
 * valid configurations for one SCU are:
 *  P0  P1  P2  P3
 * ----------------
 * 0xF,0x0,0x0,0x0 # 1 x4 port
 * 0x3,0x0,0x4,0x8 # Phys 0 and 1 are a x2 port, phy 2 and phy 3 are each x1
 *                 # ports
 * 0x1,0x2,0xC,0x0 # Phys 0 and 1 are each x1 ports, phy 2 and phy 3 are a x2
 *                 # port
 * 0x3,0x0,0xC,0x0 # Phys 0 and 1 are a x2 port, phy 2 and phy 3 are a x2 port
 * 0x1,0x2,0x4,0x8 # Each phy is a x1 port (this is the default configuration)
 *
 * if there is a port/phy on which you do not wish to override the default
 * values, use the value assigned to UNINIT_PARAM (255).
 */
unsigned int phy_mask[] = { 1, 2, 4, 8, 1, 2, 4, 8 };


/* denotes SAS generation. i.e. 3: SAS Gen 3 6G */
unsigned int phy_gen[] = { 3, 3, 3, 3, 3, 3, 3, 3 };

/*
 * if there is a port/phy on which you do not wish to override the default
 * values, use the value "0000000000000000". SAS address of zero's is
 * considered invalid and will not be used.
 */
unsigned long long sas_addr[] = { 0x5FCFFFFFF0000000ULL,
				  0x5FCFFFFFF1000000ULL,
				  0x5FCFFFFFF2000000ULL,
				  0x5FCFFFFFF3000000ULL,
				  0x5FCFFFFFF4000000ULL,
				  0x5FCFFFFFF5000000ULL,
				  0x5FCFFFFFF6000000ULL,
				  0x5FCFFFFFF7000000ULL };

int write_blob(void)
{
	FILE *fd;
	int err;

	fd = fopen(blob_name, "w+");
	if (!fd) {
		perror("Open file for write failed");
		return -EIO;
	}

	/* write id */
	err = fwrite((void *)id, sizeof(char), strlen(id)+1, fd);
	if (err == 0) {
		perror("write id failed");
		return err;
	}

	/* write version */
	err = fwrite((void *)&version, sizeof(version), 1, fd);
	if (err == 0) {
		perror("write version failed");
		return err;
	}

	/* write sub version */
	err = fwrite((void *)&sub_version, sizeof(sub_version), 1, fd);
	if (err == 0) {
		perror("write subversion failed");
		return err;
	}

	/* write phy mask header */
	err = fputc(0x1, fd);
	if (err == EOF) {
		perror("write phy mask header failed");
		return -EIO;
	}

	/* write size */
	err = fputc(8, fd);
	if (err == EOF) {
		perror("write phy mask size failed");
		return -EIO;
	}

	/* write phy masks */
	err = fwrite((void *)phy_mask, 1, sizeof(phy_mask), fd);
	if (err == 0) {
		perror("write phy_mask failed");
		return err;
	}

	/* write phy gen header */
	err = fputc(0x2, fd);
	if (err == EOF) {
		perror("write phy gen header failed");
		return -EIO;
	}

	/* write size */
	err = fputc(8, fd);
	if (err == EOF) {
		perror("write phy gen size failed");
		return -EIO;
	}

	/* write phy_gen */
	err = fwrite((void *)phy_gen,
		     1,
		     sizeof(phy_gen),
		     fd);
	if (err == 0) {
		perror("write phy_gen failed");
		return err;
	}

	/* write phy gen header */
	err = fputc(0x3, fd);
	if (err == EOF) {
		perror("write sas addr header failed");
		return -EIO;
	}

	/* write size */
	err = fputc(8, fd);
	if (err == EOF) {
		perror("write sas addr size failed");
		return -EIO;
	}

	/* write sas_addr */
	err = fwrite((void *)sas_addr,
		     1,
		     sizeof(sas_addr),
		     fd);
	if (err == 0) {
		perror("write sas_addr failed");
		return err;
	}

	/* write end header */
	err = fputc(0xff, fd);
	if (err == EOF) {
		perror("write end header failed");
		return -EIO;
	}

	fclose(fd);

	return 0;
}

int main(void)
{
	int err;

	err = write_blob();
	if (err < 0)
		return err;

	return 0;
}

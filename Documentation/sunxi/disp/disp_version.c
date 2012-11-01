/*
 * Simple example to ease users of /dev/disp into using the new but mandatory
 * versioning handshake ioctl. No license or copyright claims made.
 *
 * Use
 *   gcc -Wall -I../../../include -o disp_version disp_version.c
 * to compile this.
 *
 * Author: Luc Verhaegen <libv@skynet.be>
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <asm/types.h>

#include <video/sunxi_disp_ioctl.h>

int
main(int argc, char *argv[])
{
	int fd = open("/dev/disp", O_RDWR);
	int ret, tmp, width, height;

	if (fd == -1) {
		fprintf(stderr, "Error: Failed to open /dev/disp: %s\n",
			strerror(errno));
		return errno;
	}

	tmp = SUNXI_DISP_VERSION;
	ret = ioctl(fd, DISP_CMD_VERSION, &tmp);
	if (ret == -1) {
		printf("Warning: kernel sunxi disp driver does not support "
		       "versioning.\n");
	} else if (ret < 0) {
		fprintf(stderr, "Error: ioctl(VERSION) failed: %s\n",
			strerror(-ret));
		return ret;
	} else
		printf("sunxi disp kernel module version is %d.%d\n",
		       ret >> 16, ret & 0xFFFF);

	tmp = 0;
	ret = ioctl(fd, DISP_CMD_SCN_GET_WIDTH, &tmp);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(SCN_GET_WIDTH) failed: %s\n",
			strerror(-ret));
		return ret;
	}

	width = ret;

	tmp = 0;
	ret = ioctl(fd, DISP_CMD_SCN_GET_HEIGHT, &tmp);
	if (ret < 0) {
		fprintf(stderr, "Error: ioctl(SCN_GET_HEIGHT) failed: %s\n",
			strerror(-ret));
		return ret;
	}

	height = ret;

	printf("DISP dimensions are %dx%d\n", width, height);

	return 0;
}

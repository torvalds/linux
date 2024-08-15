#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include <linux/usbdevice_fs.h>

/* For building without an updated set of headers */
#ifndef USBDEVFS_DROP_PRIVILEGES
#define USBDEVFS_DROP_PRIVILEGES		_IOW('U', 30, __u32)
#define USBDEVFS_CAP_DROP_PRIVILEGES		0x40
#endif

void drop_privileges(int fd, uint32_t mask)
{
	int res;

	res = ioctl(fd, USBDEVFS_DROP_PRIVILEGES, &mask);
	if (res)
		printf("ERROR: USBDEVFS_DROP_PRIVILEGES returned %d\n", res);
	else
		printf("OK: privileges dropped!\n");
}

void reset_device(int fd)
{
	int res;

	res = ioctl(fd, USBDEVFS_RESET);
	if (!res)
		printf("OK: USBDEVFS_RESET succeeded\n");
	else
		printf("ERROR: reset failed! (%d - %s)\n",
		       -res, strerror(-res));
}

void claim_some_intf(int fd)
{
	int i, res;

	for (i = 0; i < 4; i++) {
		res = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &i);
		if (!res)
			printf("OK: claimed if %d\n", i);
		else
			printf("ERROR claiming if %d (%d - %s)\n",
			       i, -res, strerror(-res));
	}
}

int main(int argc, char *argv[])
{
	uint32_t mask, caps;
	int c, fd;

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("Failed to open file\n");
		goto err_fd;
	}

	/*
	 * check if dropping privileges is supported,
	 * bail on systems where the capability is not present
	 */
	ioctl(fd, USBDEVFS_GET_CAPABILITIES, &caps);
	if (!(caps & USBDEVFS_CAP_DROP_PRIVILEGES)) {
		printf("DROP_PRIVILEGES not supported\n");
		goto err;
	}

	/*
	 * Drop privileges but keep the ability to claim all
	 * free interfaces (i.e., those not used by kernel drivers)
	 */
	drop_privileges(fd, -1U);

	printf("Available options:\n"
		"[0] Exit now\n"
		"[1] Reset device. Should fail if device is in use\n"
		"[2] Claim 4 interfaces. Should succeed where not in use\n"
		"[3] Narrow interface permission mask\n"
		"Which option shall I run?: ");

	while (scanf("%d", &c) == 1) {
		switch (c) {
		case 0:
			goto exit;
		case 1:
			reset_device(fd);
			break;
		case 2:
			claim_some_intf(fd);
			break;
		case 3:
			printf("Insert new mask: ");
			scanf("%x", &mask);
			drop_privileges(fd, mask);
			break;
		default:
			printf("I don't recognize that\n");
		}

		printf("Which test shall I run next?: ");
	}

exit:
	close(fd);
	return 0;

err:
	close(fd);
err_fd:
	return 1;
}

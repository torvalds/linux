#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, const char *argv[]) {
	int fd = open("/dev/watchdog", O_WRONLY);
	if (fd == -1) {
		perror("watchdog");
		exit(1);
	}
	while (1) {
		write(fd, "\0", 1);
		fsync(fd);
		sleep(10);
	}
}

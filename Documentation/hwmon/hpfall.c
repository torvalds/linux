/* Disk protection for HP machines.
 *
 * Copyright 2008 Eric Piel
 * Copyright 2009 Pavel Machek <pavel@suse.cz>
 *
 * GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>

void write_int(char *path, int i)
{
	char buf[1024];
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	sprintf(buf, "%d", i);
	if (write(fd, buf, strlen(buf)) != strlen(buf)) {
		perror("write");
		exit(1);
	}
	close(fd);
}

void set_led(int on)
{
	write_int("/sys/class/leds/hp::hddprotect/brightness", on);
}

void protect(int seconds)
{
	write_int("/sys/block/sda/device/unload_heads", seconds*1000);
}

int on_ac(void)
{
//	/sys/class/power_supply/AC0/online
}

int lid_open(void)
{
//	/proc/acpi/button/lid/LID/state
}

void ignore_me(void)
{
	protect(0);
	set_led(0);

}

int main(int argc, char* argv[])
{
       int fd, ret;

       fd = open("/dev/freefall", O_RDONLY);
       if (fd < 0) {
               perror("open");
               return EXIT_FAILURE;
       }

	signal(SIGALRM, ignore_me);

       for (;;) {
	       unsigned char count;

               ret = read(fd, &count, sizeof(count));
	       alarm(0);
	       if ((ret == -1) && (errno == EINTR)) {
		       /* Alarm expired, time to unpark the heads */
		       continue;
	       }

               if (ret != sizeof(count)) {
                       perror("read");
                       break;
               }

	       protect(21);
	       set_led(1);
	       if (1 || on_ac() || lid_open()) {
		       alarm(2);
	       } else {
		       alarm(20);
	       }
       }

       close(fd);
       return EXIT_SUCCESS;
}

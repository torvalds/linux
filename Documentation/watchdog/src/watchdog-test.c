/*
 * Watchdog Driver Test Program
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>

int fd;

/*
 * This function simply sends an IOCTL to the driver, which in turn ticks
 * the PC Watchdog card to reset its internal timer so it doesn't trigger
 * a computer reset.
 */
static void keep_alive(void)
{
    int dummy;

    ioctl(fd, WDIOC_KEEPALIVE, &dummy);
}

/*
 * The main program.  Run the program with "-d" to disable the card,
 * or "-e" to enable the card.
 */

static void term(int sig)
{
    close(fd);
    fprintf(stderr, "Stopping watchdog ticks...\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    int flags;
    unsigned int ping_rate = 1;

    fd = open("/dev/watchdog", O_WRONLY);

    if (fd == -1) {
	fprintf(stderr, "Watchdog device not enabled.\n");
	fflush(stderr);
	exit(-1);
    }

    if (argc > 1) {
	if (!strncasecmp(argv[1], "-d", 2)) {
	    flags = WDIOS_DISABLECARD;
	    ioctl(fd, WDIOC_SETOPTIONS, &flags);
	    fprintf(stderr, "Watchdog card disabled.\n");
	    fflush(stderr);
	    goto end;
	} else if (!strncasecmp(argv[1], "-e", 2)) {
	    flags = WDIOS_ENABLECARD;
	    ioctl(fd, WDIOC_SETOPTIONS, &flags);
	    fprintf(stderr, "Watchdog card enabled.\n");
	    fflush(stderr);
	    goto end;
	} else if (!strncasecmp(argv[1], "-t", 2) && argv[2]) {
	    flags = atoi(argv[2]);
	    ioctl(fd, WDIOC_SETTIMEOUT, &flags);
	    fprintf(stderr, "Watchdog timeout set to %u seconds.\n", flags);
	    fflush(stderr);
	    goto end;
	} else if (!strncasecmp(argv[1], "-p", 2) && argv[2]) {
	    ping_rate = strtoul(argv[2], NULL, 0);
	    fprintf(stderr, "Watchdog ping rate set to %u seconds.\n", ping_rate);
	    fflush(stderr);
	} else {
	    fprintf(stderr, "-d to disable, -e to enable, -t <n> to set " \
		"the timeout,\n-p <n> to set the ping rate, and \n");
	    fprintf(stderr, "run by itself to tick the card.\n");
	    fflush(stderr);
	    goto end;
	}
    }

    fprintf(stderr, "Watchdog Ticking Away!\n");
    fflush(stderr);

    signal(SIGINT, term);

    while(1) {
	keep_alive();
	sleep(ping_rate);
    }
end:
    close(fd);
    return 0;
}

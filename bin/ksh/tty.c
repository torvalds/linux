/*	$OpenBSD: tty.c,v 1.19 2021/10/24 21:24:21 deraadt Exp $	*/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"
#include "tty.h"

int		tty_fd = -1;	/* dup'd tty file descriptor */
int		tty_devtty;	/* true if tty_fd is from /dev/tty */
struct termios	tty_state;	/* saved tty state */

void
tty_close(void)
{
	if (tty_fd >= 0) {
		close(tty_fd);
		tty_fd = -1;
	}
}

/* Initialize tty_fd.  Used for saving/resetting tty modes upon
 * foreground job completion and for setting up tty process group.
 */
void
tty_init(int init_ttystate)
{
	int	do_close = 1;
	int	tfd;

	tty_close();
	tty_devtty = 1;

	tfd = open("/dev/tty", O_RDWR);
	if (tfd == -1) {
		tty_devtty = 0;
		warningf(false, "No controlling tty (open /dev/tty: %s)",
		    strerror(errno));

		do_close = 0;
		if (isatty(0))
			tfd = 0;
		else if (isatty(2))
			tfd = 2;
		else {
			warningf(false, "Can't find tty file descriptor");
			return;
		}
	}
	if ((tty_fd = fcntl(tfd, F_DUPFD_CLOEXEC, FDBASE)) == -1) {
		warningf(false, "%s: dup of tty fd failed: %s",
		    __func__, strerror(errno));
	} else if (init_ttystate)
		tcgetattr(tty_fd, &tty_state);
	if (do_close)
		close(tfd);
}

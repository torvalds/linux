/*
 *    Implements some necessary HPUX ioctls.
 *
 *    Copyright (C) 1999-2002 Matthew Wilcox <willy with parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Supported ioctls:
 *   TCGETA
 *   TCSETA
 *   TCSETAW
 *   TCSETAF
 *   TCSBRK
 *   TCXONC
 *   TCFLSH
 *   TIOCGWINSZ
 *   TIOCSWINSZ
 *   TIOCGPGRP
 *   TIOCSPGRP
 */

#include <linux/sched.h>
#include <linux/syscalls.h>
#include <asm/errno.h>
#include <asm/ioctl.h>
#include <asm/termios.h>
#include <asm/uaccess.h>

static int hpux_ioctl_t(int fd, unsigned long cmd, unsigned long arg)
{
	int result = -EOPNOTSUPP;
	int nr = _IOC_NR(cmd);
	switch (nr) {
	case 106:
		result = sys_ioctl(fd, TIOCSWINSZ, arg);
		break;
	case 107:
		result = sys_ioctl(fd, TIOCGWINSZ, arg);
		break;
	}
	return result;
}

int hpux_ioctl(int fd, unsigned long cmd, unsigned long arg)
{
	int result = -EOPNOTSUPP;
	int type = _IOC_TYPE(cmd);
	switch (type) {
	case 'T':
		/* Our structures are now compatible with HPUX's */
		result = sys_ioctl(fd, cmd, arg);
		break;
	case 't':
		result = hpux_ioctl_t(fd, cmd, arg);
		break;
	}
	return result;
}

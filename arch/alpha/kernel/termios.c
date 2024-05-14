// SPDX-License-Identifier: GPL-2.0
#include <linux/termios_internal.h>

int user_termio_to_kernel_termios(struct ktermios *termios,
						struct termio __user *termio)
{
	struct termio v;
	bool canon;

	if (copy_from_user(&v, termio, sizeof(struct termio)))
		return -EFAULT;

	termios->c_iflag = (0xffff0000 & termios->c_iflag) | v.c_iflag;
	termios->c_oflag = (0xffff0000 & termios->c_oflag) | v.c_oflag;
	termios->c_cflag = (0xffff0000 & termios->c_cflag) | v.c_cflag;
	termios->c_lflag = (0xffff0000 & termios->c_lflag) | v.c_lflag;
	termios->c_line = (0xffff0000 & termios->c_lflag) | v.c_line;

	canon = v.c_lflag & ICANON;
	termios->c_cc[VINTR]  = v.c_cc[_VINTR];
	termios->c_cc[VQUIT]  = v.c_cc[_VQUIT];
	termios->c_cc[VERASE] = v.c_cc[_VERASE];
	termios->c_cc[VKILL]  = v.c_cc[_VKILL];
	termios->c_cc[VEOL2]  = v.c_cc[_VEOL2];
	termios->c_cc[VSWTC]  = v.c_cc[_VSWTC];
	termios->c_cc[canon ? VEOF : VMIN]  = v.c_cc[_VEOF];
	termios->c_cc[canon ? VEOL : VTIME] = v.c_cc[_VEOL];

	return 0;
}

int kernel_termios_to_user_termio(struct termio __user *termio,
						struct ktermios *termios)
{
	struct termio v;
	bool canon;

	memset(&v, 0, sizeof(struct termio));
	v.c_iflag = termios->c_iflag;
	v.c_oflag = termios->c_oflag;
	v.c_cflag = termios->c_cflag;
	v.c_lflag = termios->c_lflag;
	v.c_line = termios->c_line;

	canon = v.c_lflag & ICANON;
	v.c_cc[_VINTR]  = termios->c_cc[VINTR];
	v.c_cc[_VQUIT]  = termios->c_cc[VQUIT];
	v.c_cc[_VERASE] = termios->c_cc[VERASE];
	v.c_cc[_VKILL]  = termios->c_cc[VKILL];
	v.c_cc[_VEOF]   = termios->c_cc[canon ? VEOF : VMIN];
	v.c_cc[_VEOL]   = termios->c_cc[canon ? VEOL : VTIME];
	v.c_cc[_VEOL2]  = termios->c_cc[VEOL2];
	v.c_cc[_VSWTC]  = termios->c_cc[VSWTC];

	return copy_to_user(termio, &v, sizeof(struct termio));
}

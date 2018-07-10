/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC seek_by_jump(int fd, off_t amount)
{
	if (amount
	 && lseek(fd, amount, SEEK_CUR) == (off_t) -1
	) {
		if (errno == ESPIPE)
			seek_by_read(fd, amount);
		else
			bb_perror_msg_and_die("seek failure");
	}
}

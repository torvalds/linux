/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

/*  If we are reading through a pipe, or from stdin then we can't lseek,
 *  we must read and discard the data to skip over it.
 */
void FAST_FUNC seek_by_read(int fd, off_t amount)
{
	if (amount)
		bb_copyfd_exact_size(fd, -1, amount);
}

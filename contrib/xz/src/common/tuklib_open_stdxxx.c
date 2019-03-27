///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_open_stdxxx.c
/// \brief      Make sure that file descriptors 0, 1, and 2 are open
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tuklib_open_stdxxx.h"

#ifndef TUKLIB_DOSLIKE
#	include <stdlib.h>
#	include <errno.h>
#	include <fcntl.h>
#	include <unistd.h>
#endif


extern void
tuklib_open_stdxxx(int err_status)
{
#ifdef TUKLIB_DOSLIKE
	// Do nothing, just silence warnings.
	(void)err_status;

#else
	for (int i = 0; i <= 2; ++i) {
		// We use fcntl() to check if the file descriptor is open.
		if (fcntl(i, F_GETFD) == -1 && errno == EBADF) {
			// With stdin, we could use /dev/full so that
			// writing to stdin would fail. However, /dev/full
			// is Linux specific, and if the program tries to
			// write to stdin, there's already a problem anyway.
			const int fd = open("/dev/null", O_NOCTTY
					| (i == 0 ? O_WRONLY : O_RDONLY));

			if (fd != i) {
				if (fd != -1)
					(void)close(fd);

				// Something went wrong. Exit with the
				// exit status we were given. Don't try
				// to print an error message, since stderr
				// may very well be non-existent. This
				// error should be extremely rare.
				exit(err_status);
			}
		}
	}
#endif

	return;
}

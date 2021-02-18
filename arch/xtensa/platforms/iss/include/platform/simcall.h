/*
 * include/asm-xtensa/platform-iss/simcall.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Tensilica Inc.
 * Copyright (C) 2017 - 2021 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_PLATFORM_ISS_SIMCALL_H
#define _XTENSA_PLATFORM_ISS_SIMCALL_H

#include <platform/simcall-iss.h>

static inline int simc_exit(int exit_code)
{
	return __simc(SYS_exit, exit_code, 0, 0);
}

static inline int simc_open(const char *file, int flags, int mode)
{
	return __simc(SYS_open, (int) file, flags, mode);
}

static inline int simc_close(int fd)
{
	return __simc(SYS_close, fd, 0, 0);
}

static inline int simc_ioctl(int fd, int request, void *arg)
{
	return __simc(SYS_ioctl, fd, request, (int) arg);
}

static inline int simc_read(int fd, void *buf, size_t count)
{
	return __simc(SYS_read, fd, (int) buf, count);
}

static inline int simc_write(int fd, const void *buf, size_t count)
{
	return __simc(SYS_write, fd, (int) buf, count);
}

static inline int simc_poll(int fd)
{
	long timeval[2] = { 0, 0 };

	return __simc(SYS_select_one, fd, XTISS_SELECT_ONE_READ, (int)&timeval);
}

static inline int simc_lseek(int fd, uint32_t off, int whence)
{
	return __simc(SYS_lseek, fd, off, whence);
}

static inline int simc_argc(void)
{
	return __simc(SYS_iss_argc, 0, 0, 0);
}

static inline int simc_argv_size(void)
{
	return __simc(SYS_iss_argv_size, 0, 0, 0);
}

static inline void simc_argv(void *buf)
{
	__simc(SYS_iss_set_argv, (int)buf, 0, 0);
}

#endif /* _XTENSA_PLATFORM_ISS_SIMCALL_H */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 *
 * The cpustart method is a PMC-Sierra's function to start the secondary CPU.
 * Stock PMON 2000 has the smpfork, semlock and semunlock methods instead.
 */
#ifndef _ASM_PMON_H
#define _ASM_PMON_H

struct callvectors {
	int	(*open) (char*, int, int);
	int	(*close) (int);
	int	(*read) (int, void*, int);
	int	(*write) (int, void*, int);
	off_t	(*lseek) (int, off_t, int);
	int	(*printf) (const char*, ...);
	void	(*cacheflush) (void);
	char*	(*gets) (char*);
	union {
		int	(*smpfork) (unsigned long cp, char *sp);
		int	(*cpustart) (long, void (*)(void), void *, long);
	} _s;
	int	(*semlock) (int sem);
	void	(*semunlock) (int sem);
};

extern struct callvectors *de_vectors;

#define pmon_open(name, flags, mode)	de_vectors->open(name, flage, mode)
#define pmon_close(fd)			de_vectors->close(fd)
#define pmon_read(fd, buf, count)	de_vectors->read(fd, buf, count)
#define pmon_write(fd, buf, count)	de_vectors->write(fd, buf, count)
#define pmon_lseek(fd, off, whence)	de_vectors->lseek(fd, off, whence)
#define pmon_printf(fmt...)		de_vectors->printf(fmt)
#define pmon_cacheflush()		de_vectors->cacheflush()
#define pmon_gets(s)			de_vectors->gets(s)
#define pmon_cpustart(n, f, sp, gp)	de_vectors->_s.cpustart(n, f, sp, gp)
#define pmon_smpfork(cp, sp)		de_vectors->_s.smpfork(cp, sp)
#define pmon_semlock(sem)		de_vectors->semlock(sem)
#define pmon_semunlock(sem)		de_vectors->semunlock(sem)

#endif /* _ASM_PMON_H */

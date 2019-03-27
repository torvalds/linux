/* Machine independent support for SVR4 /proc (process file system) for GDB.
   Copyright 1999, 2000 Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation, 
Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


/*
 * Pretty-print functions for /proc data 
 */

extern void proc_prettyprint_why (unsigned long why, unsigned long what,
				  int verbose);

extern void proc_prettyprint_syscalls (sysset_t *sysset, int verbose);

extern void proc_prettyprint_syscall (int num, int verbose);

extern void proc_prettyprint_flags (unsigned long flags, int verbose);

extern void proc_prettyfprint_signalset (FILE *file, sigset_t *sigset,
					 int verbose);

extern void proc_prettyfprint_faultset (FILE *file, fltset_t *fltset,
					int verbose);

extern void proc_prettyfprint_syscall (FILE *file, int num, int verbose);

extern void proc_prettyfprint_signal (FILE *file, int signo, int verbose);

extern void proc_prettyfprint_flags (FILE *file, unsigned long flags,
				     int verbose);

extern void proc_prettyfprint_why (FILE *file, unsigned long why, 
				   unsigned long what, int verbose);

extern void proc_prettyfprint_fault (FILE *file, int faultno, int verbose);

extern void proc_prettyfprint_syscalls (FILE *file, sysset_t *sysset,
					int verbose);

extern void proc_prettyfprint_status (long, int, int, int);

/*
 * Trace functions for /proc api.
 */

extern  int   write_with_trace (int, void *, size_t, char *, int);
extern  off_t lseek_with_trace (int, off_t,  int,    char *, int);
extern  int   ioctl_with_trace (int, long, void *, char *, int);
extern  pid_t wait_with_trace  (int *, char *, int);
extern  int   open_with_trace  (char *, int, char *, int);
extern  int   close_with_trace (int, char *, int);
extern  void  procfs_note      (char *, char *, int);

#ifdef PROCFS_TRACE
/*
 * Debugging code:
 *
 * These macros allow me to trace the system calls that we make
 * to control the child process.  This is quite handy for comparing
 * with the older version of procfs.
 */

#define write(X,Y,Z)   write_with_trace (X, Y, Z, __FILE__, __LINE__)
#define lseek(X,Y,Z)   lseek_with_trace (X, Y, Z, __FILE__, __LINE__)
#define ioctl(X,Y,Z)   ioctl_with_trace (X, Y, Z, __FILE__, __LINE__)
#define open(X,Y)      open_with_trace  (X, Y,    __FILE__, __LINE__)
#define close(X)       close_with_trace (X,       __FILE__, __LINE__)
#define wait(X)        wait_with_trace  (X,       __FILE__, __LINE__)
#endif
#define PROCFS_NOTE(X) procfs_note      (X,       __FILE__, __LINE__)
#define PROC_PRETTYFPRINT_STATUS(X,Y,Z,T) \
     proc_prettyfprint_status (X, Y, Z, T)

/* Define the type (and more importantly the width) of the control
   word used to write to the /proc/PID/ctl file. */
#if defined (PROC_CTL_WORD_TYPE)
typedef PROC_CTL_WORD_TYPE procfs_ctl_t;
#else
typedef long procfs_ctl_t;
#endif

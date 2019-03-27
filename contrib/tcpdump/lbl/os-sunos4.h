/*
 * Copyright (c) 1989, 1990, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* Prototypes missing in SunOS 4 */
#ifdef FILE
int	_filbuf(FILE *);
int	_flsbuf(u_char, FILE *);
int	fclose(FILE *);
int	fflush(FILE *);
int	fgetc(FILE *);
int	fprintf(FILE *, const char *, ...);
int	fputc(int, FILE *);
int	fputs(const char *, FILE *);
u_int	fread(void *, u_int, u_int, FILE *);
int	fseek(FILE *, long, int);
u_int	fwrite(const void *, u_int, u_int, FILE *);
int	pclose(FILE *);
void	rewind(FILE *);
void	setbuf(FILE *, char *);
int	setlinebuf(FILE *);
int	ungetc(int, FILE *);
int	vfprintf(FILE *, const char *, ...);
int	vprintf(const char *, ...);
#endif

#if __GNUC__ <= 1
int	read(int, char *, u_int);
int	write(int, char *, u_int);
#endif

long	a64l(const char *);
#ifdef __STDC__
struct	sockaddr;
#endif
int	accept(int, struct sockaddr *, int *);
int	bind(int, struct sockaddr *, int);
int	bcmp(const void *, const void *, u_int);
void	bcopy(const void *, void *, u_int);
void	bzero(void *, int);
int	chroot(const char *);
int	close(int);
void	closelog(void);
int	connect(int, struct sockaddr *, int);
char	*crypt(const char *, const char *);
int	daemon(int, int);
int	fchmod(int, int);
int	fchown(int, int, int);
void	endgrent(void);
void	endpwent(void);
void	endservent(void);
#ifdef __STDC__
struct	ether_addr;
#endif
struct	ether_addr *ether_aton(const char *);
int	flock(int, int);
#ifdef __STDC__
struct	stat;
#endif
int	fstat(int, struct stat *);
#ifdef __STDC__
struct statfs;
#endif
int	fstatfs(int, struct statfs *);
int	fsync(int);
#ifdef __STDC__
struct timeb;
#endif
int	ftime(struct timeb *);
int	ftruncate(int, off_t);
int	getdtablesize(void);
long	gethostid(void);
int	gethostname(char *, int);
int	getopt(int, char * const *, const char *);
int	getpagesize(void);
char	*getpass(char *);
int	getpeername(int, struct sockaddr *, int *);
int	getpriority(int, int);
#ifdef __STDC__
struct	rlimit;
#endif
int	getrlimit(int, struct rlimit *);
int	getsockname(int, struct sockaddr *, int *);
int	getsockopt(int, int, int, char *, int *);
#ifdef __STDC__
struct	timeval;
struct	timezone;
#endif
int	gettimeofday(struct timeval *, struct timezone *);
char	*getusershell(void);
char	*getwd(char *);
int	initgroups(const char *, int);
int	ioctl(int, int, caddr_t);
int	iruserok(u_long, int, char *, char *);
int	isatty(int);
int	killpg(int, int);
int	listen(int, int);
#ifdef __STDC__
struct	utmp;
#endif
void	login(struct utmp *);
int	logout(const char *);
off_t	lseek(int, off_t, int);
int	lstat(const char *, struct stat *);
int	mkstemp(char *);
char	*mktemp(char *);
int	munmap(caddr_t, int);
void	openlog(const char *, int, int);
void	perror(const char *);
int	printf(const char *, ...);
int	puts(const char *);
long	random(void);
int	readlink(const char *, char *, int);
#ifdef __STDC__
struct	iovec;
#endif
int	readv(int, struct iovec *, int);
int	recv(int, char *, u_int, int);
int	recvfrom(int, char *, u_int, int, struct sockaddr *, int *);
int	rename(const char *, const char *);
int	rcmd(char **, u_short, char *, char *, char *, int *);
int	rresvport(int *);
int	send(int, char *, u_int, int);
int	sendto(int, char *, u_int, int, struct sockaddr *, int);
int	setenv(const char *, const char *, int);
int	seteuid(int);
int	setpriority(int, int, int);
int	select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int	setpgrp(int, int);
void	setpwent(void);
int	setrlimit(int, struct rlimit *);
void	setservent(int);
int	setsockopt(int, int, int, char *, int);
int	shutdown(int, int);
int	sigblock(int);
void	(*signal (int, void (*) (int))) (int);
int	sigpause(int);
int	sigsetmask(int);
#ifdef __STDC__
struct	sigvec;
#endif
int	sigvec(int, struct sigvec *, struct sigvec*);
int	snprintf(char *, size_t, const char *, ...);
int	socket(int, int, int);
int	socketpair(int, int, int, int *);
int	symlink(const char *, const char *);
void	srandom(int);
int	sscanf(char *, const char *, ...);
int	stat(const char *, struct stat *);
int	statfs(char *, struct statfs *);
char	*strerror(int);
#ifdef __STDC__
struct	tm;
#endif
int	strftime(char *, int, char *, struct tm *);
long	strtol(const char *, char **, int);
void	sync(void);
void	syslog(int, const char *, ...);
int	system(const char *);
long	tell(int);
time_t	time(time_t *);
char	*timezone(int, int);
int	tolower(int);
int	toupper(int);
int	truncate(char *, off_t);
void	unsetenv(const char *);
int	vfork(void);
int	vsprintf(char *, const char *, ...);
int	writev(int, struct iovec *, int);
#ifdef __STDC__
struct	rusage;
#endif
int	utimes(const char *, struct timeval *);
#if __GNUC__ <= 1
int	wait(int *);
pid_t	wait3(int *, int, struct rusage *);
#endif

/* Ugly signal hacking */
#ifdef SIG_ERR
#undef SIG_ERR
#define SIG_ERR		(void (*)(int))-1
#undef SIG_DFL
#define SIG_DFL		(void (*)(int))0
#undef SIG_IGN
#define SIG_IGN		(void (*)(int))1

#ifdef KERNEL
#undef SIG_CATCH
#define SIG_CATCH	(void (*)(int))2
#endif
#undef SIG_HOLD
#define SIG_HOLD	(void (*)(int))3
#endif

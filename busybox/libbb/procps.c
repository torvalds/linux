/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright 1998 by Albert Cahalan; all rights reserved.
 * Copyright (C) 2002 by Vladimir Oleynik <dzo@simtreas.ru>
 * SELinux support: (c) 2007 by Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"


typedef struct id_to_name_map_t {
	uid_t id;
	char name[USERNAME_MAX_SIZE];
} id_to_name_map_t;

typedef struct cache_t {
	id_to_name_map_t *cache;
	int size;
} cache_t;

static cache_t username, groupname;

static void clear_cache(cache_t *cp)
{
	free(cp->cache);
	cp->cache = NULL;
	cp->size = 0;
}
void FAST_FUNC clear_username_cache(void)
{
	clear_cache(&username);
	clear_cache(&groupname);
}

#if 0 /* more generic, but we don't need that yet */
/* Returns -N-1 if not found. */
/* cp->cache[N] is allocated and must be filled in this case */
static int get_cached(cache_t *cp, uid_t id)
{
	int i;
	for (i = 0; i < cp->size; i++)
		if (cp->cache[i].id == id)
			return i;
	i = cp->size++;
	cp->cache = xrealloc_vector(cp->cache, 2, i);
	cp->cache[i++].id = id;
	return -i;
}
#endif

static char* get_cached(cache_t *cp, uid_t id,
			char* FAST_FUNC x2x_utoa(uid_t id))
{
	int i;
	for (i = 0; i < cp->size; i++)
		if (cp->cache[i].id == id)
			return cp->cache[i].name;
	i = cp->size++;
	cp->cache = xrealloc_vector(cp->cache, 2, i);
	cp->cache[i].id = id;
	/* Never fails. Generates numeric string if name isn't found */
	safe_strncpy(cp->cache[i].name, x2x_utoa(id), sizeof(cp->cache[i].name));
	return cp->cache[i].name;
}
const char* FAST_FUNC get_cached_username(uid_t uid)
{
	return get_cached(&username, uid, uid2uname_utoa);
}
const char* FAST_FUNC get_cached_groupname(gid_t gid)
{
	return get_cached(&groupname, gid, gid2group_utoa);
}


#define PROCPS_BUFSIZE 1024

static int read_to_buf(const char *filename, void *buf)
{
	int fd;
	/* open_read_close() would do two reads, checking for EOF.
	 * When you have 10000 /proc/$NUM/stat to read, it isn't desirable */
	ssize_t ret = -1;
	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		ret = read(fd, buf, PROCPS_BUFSIZE-1);
		close(fd);
	}
	((char *)buf)[ret > 0 ? ret : 0] = '\0';
	return ret;
}

static procps_status_t* FAST_FUNC alloc_procps_scan(void)
{
	unsigned n = getpagesize();
	procps_status_t* sp = xzalloc(sizeof(procps_status_t));
	sp->dir = xopendir("/proc");
	while (1) {
		n >>= 1;
		if (!n) break;
		sp->shift_pages_to_bytes++;
	}
	sp->shift_pages_to_kb = sp->shift_pages_to_bytes - 10;
	return sp;
}

void FAST_FUNC free_procps_scan(procps_status_t* sp)
{
	closedir(sp->dir);
#if ENABLE_FEATURE_SHOW_THREADS
	if (sp->task_dir)
		closedir(sp->task_dir);
#endif
	free(sp->argv0);
	free(sp->exe);
	IF_SELINUX(free(sp->context);)
	free(sp);
}

#if ENABLE_FEATURE_TOPMEM || ENABLE_PMAP
static unsigned long fast_strtoul_16(char **endptr)
{
	unsigned char c;
	char *str = *endptr;
	unsigned long n = 0;

	/* Need to stop on both ' ' and '\n' */
	while ((c = *str++) > ' ') {
		c = ((c|0x20) - '0');
		if (c > 9)
			/* c = c + '0' - 'a' + 10: */
			c = c - ('a' - '0' - 10);
		n = n*16 + c;
	}
	*endptr = str; /* We skip trailing space! */
	return n;
}
#endif

#if ENABLE_FEATURE_FAST_TOP || ENABLE_FEATURE_TOPMEM || ENABLE_PMAP
/* We cut a lot of corners here for speed */
static unsigned long fast_strtoul_10(char **endptr)
{
	unsigned char c;
	char *str = *endptr;
	unsigned long n = *str - '0';

	/* Need to stop on both ' ' and '\n' */
	while ((c = *++str) > ' ')
		n = n*10 + (c - '0');

	*endptr = str + 1; /* We skip trailing space! */
	return n;
}

# if ENABLE_FEATURE_FAST_TOP
static long fast_strtol_10(char **endptr)
{
	if (**endptr != '-')
		return fast_strtoul_10(endptr);

	(*endptr)++;
	return - (long)fast_strtoul_10(endptr);
}
# endif

static char *skip_fields(char *str, int count)
{
	do {
		while (*str++ != ' ')
			continue;
		/* we found a space char, str points after it */
	} while (--count);
	return str;
}
#endif

#if ENABLE_FEATURE_TOPMEM || ENABLE_PMAP
int FAST_FUNC procps_read_smaps(pid_t pid, struct smaprec *total,
		void (*cb)(struct smaprec *, void *), void *data)
{
	FILE *file;
	struct smaprec currec;
	char filename[sizeof("/proc/%u/smaps") + sizeof(int)*3];
	char buf[PROCPS_BUFSIZE];
#if !ENABLE_PMAP
	void (*cb)(struct smaprec *, void *) = NULL;
	void *data = NULL;
#endif

	sprintf(filename, "/proc/%u/smaps", (int)pid);

	file = fopen_for_read(filename);
	if (!file)
		return 1;

	memset(&currec, 0, sizeof(currec));
	while (fgets(buf, PROCPS_BUFSIZE, file)) {
		// Each mapping datum has this form:
		// f7d29000-f7d39000 rw-s FILEOFS M:m INODE FILENAME
		// Size:                nnn kB
		// Rss:                 nnn kB
		// .....

		char *tp, *p;

#define SCAN(S, X) \
		if ((tp = is_prefixed_with(buf, S)) != NULL) {       \
			tp = skip_whitespace(tp);                    \
			total->X += currec.X = fast_strtoul_10(&tp); \
			continue;                                    \
		}
		if (cb) {
			SCAN("Pss:"  , smap_pss     );
			SCAN("Swap:" , smap_swap    );
		}
		SCAN("Private_Dirty:", private_dirty);
		SCAN("Private_Clean:", private_clean);
		SCAN("Shared_Dirty:" , shared_dirty );
		SCAN("Shared_Clean:" , shared_clean );
#undef SCAN
		tp = strchr(buf, '-');
		if (tp) {
			// We reached next mapping - the line of this form:
			// f7d29000-f7d39000 rw-s FILEOFS M:m INODE FILENAME

			if (cb) {
				/* If we have a previous record, there's nothing more
				 * for it, call the callback and clear currec
				 */
				if (currec.smap_size)
					cb(&currec, data);
				free(currec.smap_name);
			}
			memset(&currec, 0, sizeof(currec));

			*tp = ' ';
			tp = buf;
			currec.smap_start = fast_strtoul_16(&tp);
			currec.smap_size = (fast_strtoul_16(&tp) - currec.smap_start) >> 10;

			strncpy(currec.smap_mode, tp, sizeof(currec.smap_mode)-1);

			// skipping "rw-s FILEOFS M:m INODE "
			tp = skip_whitespace(skip_fields(tp, 4));
			// filter out /dev/something (something != zero)
			if (!is_prefixed_with(tp, "/dev/") || strcmp(tp, "/dev/zero\n") == 0) {
				if (currec.smap_mode[1] == 'w') {
					currec.mapped_rw = currec.smap_size;
					total->mapped_rw += currec.smap_size;
				} else if (currec.smap_mode[1] == '-') {
					currec.mapped_ro = currec.smap_size;
					total->mapped_ro += currec.smap_size;
				}
			}

			if (strcmp(tp, "[stack]\n") == 0)
				total->stack += currec.smap_size;
			if (cb) {
				p = skip_non_whitespace(tp);
				if (p == tp) {
					currec.smap_name = xstrdup("  [ anon ]");
				} else {
					*p = '\0';
					currec.smap_name = xstrdup(tp);
				}
			}
			total->smap_size += currec.smap_size;
		}
	}
	fclose(file);

	if (cb) {
		if (currec.smap_size)
			cb(&currec, data);
		free(currec.smap_name);
	}

	return 0;
}
#endif

procps_status_t* FAST_FUNC procps_scan(procps_status_t* sp, int flags)
{
	if (!sp)
		sp = alloc_procps_scan();

	for (;;) {
		struct dirent *entry;
		char buf[PROCPS_BUFSIZE];
		long tasknice;
		unsigned pid;
		int n;
		char filename[sizeof("/proc/%u/task/%u/cmdline") + sizeof(int)*3 * 2];
		char *filename_tail;

#if ENABLE_FEATURE_SHOW_THREADS
		if (sp->task_dir) {
			entry = readdir(sp->task_dir);
			if (entry)
				goto got_entry;
			closedir(sp->task_dir);
			sp->task_dir = NULL;
		}
#endif
		entry = readdir(sp->dir);
		if (entry == NULL) {
			free_procps_scan(sp);
			return NULL;
		}
 IF_FEATURE_SHOW_THREADS(got_entry:)
		pid = bb_strtou(entry->d_name, NULL, 10);
		if (errno)
			continue;
#if ENABLE_FEATURE_SHOW_THREADS
		if ((flags & PSSCAN_TASKS) && !sp->task_dir) {
			/* We found another /proc/PID. Do not use it,
			 * there will be /proc/PID/task/PID (same PID!),
			 * so just go ahead and dive into /proc/PID/task. */
			sprintf(filename, "/proc/%u/task", pid);
			/* Note: if opendir fails, we just go to next /proc/XXX */
			sp->task_dir = opendir(filename);
			sp->main_thread_pid = pid;
			continue;
		}
#endif

		/* After this point we can:
		 * "break": stop parsing, return the data
		 * "continue": try next /proc/XXX
		 */

		memset(&sp->vsz, 0, sizeof(*sp) - offsetof(procps_status_t, vsz));

		sp->pid = pid;
		if (!(flags & ~PSSCAN_PID))
			break; /* we needed only pid, we got it */

#if ENABLE_SELINUX
		if (flags & PSSCAN_CONTEXT) {
			if (getpidcon(sp->pid, &sp->context) < 0)
				sp->context = NULL;
		}
#endif

#if ENABLE_FEATURE_SHOW_THREADS
		if (sp->task_dir)
			filename_tail = filename + sprintf(filename, "/proc/%u/task/%u/", sp->main_thread_pid, pid);
		else
#endif
			filename_tail = filename + sprintf(filename, "/proc/%u/", pid);

		if (flags & PSSCAN_UIDGID) {
			struct stat sb;
			if (stat(filename, &sb))
				continue; /* process probably exited */
			/* Effective UID/GID, not real */
			sp->uid = sb.st_uid;
			sp->gid = sb.st_gid;
		}

		/* These are all retrieved from proc/NN/stat in one go: */
		if (flags & (PSSCAN_PPID | PSSCAN_PGID | PSSCAN_SID
			| PSSCAN_COMM | PSSCAN_STATE
			| PSSCAN_VSZ | PSSCAN_RSS
			| PSSCAN_STIME | PSSCAN_UTIME | PSSCAN_START_TIME
			| PSSCAN_TTY | PSSCAN_NICE
			| PSSCAN_CPU)
		) {
			int s_idx;
			char *cp, *comm1;
			int tty;
#if !ENABLE_FEATURE_FAST_TOP
			unsigned long vsz, rss;
#endif
			/* see proc(5) for some details on this */
			strcpy(filename_tail, "stat");
			n = read_to_buf(filename, buf);
			if (n < 0)
				continue; /* process probably exited */
			cp = strrchr(buf, ')'); /* split into "PID (cmd" and "<rest>" */
			/*if (!cp || cp[1] != ' ')
				continue;*/
			cp[0] = '\0';
			BUILD_BUG_ON(sizeof(sp->comm) < 16);
			comm1 = strchr(buf, '(');
			/*if (comm1)*/
				safe_strncpy(sp->comm, comm1 + 1, sizeof(sp->comm));

#if !ENABLE_FEATURE_FAST_TOP
			n = sscanf(cp+2,
				"%c %u "               /* state, ppid */
				"%u %u %d %*s "        /* pgid, sid, tty, tpgid */
				"%*s %*s %*s %*s %*s " /* flags, min_flt, cmin_flt, maj_flt, cmaj_flt */
				"%lu %lu "             /* utime, stime */
				"%*s %*s %*s "         /* cutime, cstime, priority */
				"%ld "                 /* nice */
				"%*s %*s "             /* timeout, it_real_value */
				"%lu "                 /* start_time */
				"%lu "                 /* vsize */
				"%lu "                 /* rss */
# if ENABLE_FEATURE_TOP_SMP_PROCESS
				"%*s %*s %*s %*s %*s %*s " /*rss_rlim, start_code, end_code, start_stack, kstk_esp, kstk_eip */
				"%*s %*s %*s %*s "         /*signal, blocked, sigignore, sigcatch */
				"%*s %*s %*s %*s "         /*wchan, nswap, cnswap, exit_signal */
				"%d"                       /*cpu last seen on*/
# endif
				,
				sp->state, &sp->ppid,
				&sp->pgid, &sp->sid, &tty,
				&sp->utime, &sp->stime,
				&tasknice,
				&sp->start_time,
				&vsz,
				&rss
# if ENABLE_FEATURE_TOP_SMP_PROCESS
				, &sp->last_seen_on_cpu
# endif
				);

			if (n < 11)
				continue; /* bogus data, get next /proc/XXX */
# if ENABLE_FEATURE_TOP_SMP_PROCESS
			if (n == 11)
				sp->last_seen_on_cpu = 0;
# endif

			/* vsz is in bytes and we want kb */
			sp->vsz = vsz >> 10;
			/* vsz is in bytes but rss is in *PAGES*! Can you believe that? */
			sp->rss = rss << sp->shift_pages_to_kb;
			sp->tty_major = (tty >> 8) & 0xfff;
			sp->tty_minor = (tty & 0xff) | ((tty >> 12) & 0xfff00);
#else
/* This costs ~100 bytes more but makes top faster by 20%
 * If you run 10000 processes, this may be important for you */
			sp->state[0] = cp[2];
			cp += 4;
			sp->ppid = fast_strtoul_10(&cp);
			sp->pgid = fast_strtoul_10(&cp);
			sp->sid = fast_strtoul_10(&cp);
			tty = fast_strtoul_10(&cp);
			sp->tty_major = (tty >> 8) & 0xfff;
			sp->tty_minor = (tty & 0xff) | ((tty >> 12) & 0xfff00);
			cp = skip_fields(cp, 6); /* tpgid, flags, min_flt, cmin_flt, maj_flt, cmaj_flt */
			sp->utime = fast_strtoul_10(&cp);
			sp->stime = fast_strtoul_10(&cp);
			cp = skip_fields(cp, 3); /* cutime, cstime, priority */
			tasknice = fast_strtol_10(&cp);
			cp = skip_fields(cp, 2); /* timeout, it_real_value */
			sp->start_time = fast_strtoul_10(&cp);
			/* vsz is in bytes and we want kb */
			sp->vsz = fast_strtoul_10(&cp) >> 10;
			/* vsz is in bytes but rss is in *PAGES*! Can you believe that? */
			sp->rss = fast_strtoul_10(&cp) << sp->shift_pages_to_kb;
# if ENABLE_FEATURE_TOP_SMP_PROCESS
			/* (6): rss_rlim, start_code, end_code, start_stack, kstk_esp, kstk_eip */
			/* (4): signal, blocked, sigignore, sigcatch */
			/* (4): wchan, nswap, cnswap, exit_signal */
			cp = skip_fields(cp, 14);
//FIXME: is it safe to assume this field exists?
			sp->last_seen_on_cpu = fast_strtoul_10(&cp);
# endif
#endif /* FEATURE_FAST_TOP */

#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
			sp->niceness = tasknice;
#endif
			sp->state[1] = ' ';
			sp->state[2] = ' ';
			s_idx = 1;
			if (sp->vsz == 0 && sp->state[0] != 'Z') {
				/* not sure what the purpose of this flag */
				sp->state[1] = 'W';
				s_idx = 2;
			}
			if (tasknice != 0) {
				if (tasknice < 0)
					sp->state[s_idx] = '<';
				else /* > 0 */
					sp->state[s_idx] = 'N';
			}
		}

#if ENABLE_FEATURE_TOPMEM
		if (flags & PSSCAN_SMAPS)
			procps_read_smaps(pid, &sp->smaps, NULL, NULL);
#endif /* TOPMEM */
#if ENABLE_FEATURE_PS_ADDITIONAL_COLUMNS
		if (flags & PSSCAN_RUIDGID) {
			FILE *file;

			strcpy(filename_tail, "status");
			file = fopen_for_read(filename);
			if (file) {
				while (fgets(buf, sizeof(buf), file)) {
					char *tp;
#define SCAN_TWO(str, name, statement) \
	if ((tp = is_prefixed_with(buf, str)) != NULL) { \
		tp = skip_whitespace(tp); \
		sscanf(tp, "%u", &sp->name); \
		statement; \
	}
					SCAN_TWO("Uid:", ruid, continue);
					SCAN_TWO("Gid:", rgid, break);
#undef SCAN_TWO
				}
				fclose(file);
			}
		}
#endif /* PS_ADDITIONAL_COLUMNS */
		if (flags & PSSCAN_EXE) {
			strcpy(filename_tail, "exe");
			free(sp->exe);
			sp->exe = xmalloc_readlink(filename);
		}
		/* Note: if /proc/PID/cmdline is empty,
		 * code below "breaks". Therefore it must be
		 * the last code to parse /proc/PID/xxx data
		 * (we used to have /proc/PID/exe parsing after it
		 * and were getting stale sp->exe).
		 */
#if 0 /* PSSCAN_CMD is not used */
		if (flags & (PSSCAN_CMD|PSSCAN_ARGV0)) {
			free(sp->argv0);
			sp->argv0 = NULL;
			free(sp->cmd);
			sp->cmd = NULL;
			strcpy(filename_tail, "cmdline");
			/* TODO: to get rid of size limits, read into malloc buf,
			 * then realloc it down to real size. */
			n = read_to_buf(filename, buf);
			if (n <= 0)
				break;
			if (flags & PSSCAN_ARGV0)
				sp->argv0 = xstrdup(buf);
			if (flags & PSSCAN_CMD) {
				do {
					n--;
					if ((unsigned char)(buf[n]) < ' ')
						buf[n] = ' ';
				} while (n);
				sp->cmd = xstrdup(buf);
			}
		}
#else
		if (flags & (PSSCAN_ARGV0|PSSCAN_ARGVN)) {
			free(sp->argv0);
			sp->argv0 = NULL;
			strcpy(filename_tail, "cmdline");
			n = read_to_buf(filename, buf);
			if (n <= 0)
				break;
			if (flags & PSSCAN_ARGVN) {
				sp->argv_len = n;
				sp->argv0 = xmemdup(buf, n + 1);
				/* sp->argv0[n] = '\0'; - buf has it */
			} else {
				sp->argv_len = 0;
				sp->argv0 = xstrdup(buf);
			}
		}
#endif
		break;
	} /* for (;;) */

	return sp;
}

void FAST_FUNC read_cmdline(char *buf, int col, unsigned pid, const char *comm)
{
	int sz;
	char filename[sizeof("/proc/%u/cmdline") + sizeof(int)*3];

	sprintf(filename, "/proc/%u/cmdline", pid);
	sz = open_read_close(filename, buf, col - 1);
	if (sz > 0) {
		const char *base;
		int comm_len;

		buf[sz] = '\0';
		while (--sz >= 0 && buf[sz] == '\0')
			continue;
		/* Prevent basename("process foo/bar") = "bar" */
		strchrnul(buf, ' ')[0] = '\0';
		base = bb_basename(buf); /* before we replace argv0's NUL with space */
		while (sz >= 0) {
			if ((unsigned char)(buf[sz]) < ' ')
				buf[sz] = ' ';
			sz--;
		}
		if (base[0] == '-') /* "-sh" (login shell)? */
			base++;

		/* If comm differs from argv0, prepend "{comm} ".
		 * It allows to see thread names set by prctl(PR_SET_NAME).
		 */
		if (!comm)
			return;
		comm_len = strlen(comm);
		/* Why compare up to comm_len, not COMM_LEN-1?
		 * Well, some processes rewrite argv, and use _spaces_ there
		 * while rewriting. (KDE is observed to do it).
		 * I prefer to still treat argv0 "process foo bar"
		 * as 'equal' to comm "process".
		 */
		if (strncmp(base, comm, comm_len) != 0) {
			comm_len += 3;
			if (col > comm_len)
				memmove(buf + comm_len, buf, col - comm_len);
			snprintf(buf, col, "{%s}", comm);
			if (col <= comm_len)
				return;
			buf[comm_len - 1] = ' ';
			buf[col - 1] = '\0';
		}
	} else {
		snprintf(buf, col, "[%s]", comm ? comm : "?");
	}
}

/* from kernel:
	//             pid comm S ppid pgid sid tty_nr tty_pgrp flg
	sprintf(buffer,"%d (%s) %c %d  %d   %d  %d     %d       %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %d 0 %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu %llu\n",
		task->pid,
		tcomm,
		state,
		ppid,
		pgid,
		sid,
		tty_nr,
		tty_pgrp,
		task->flags,
		min_flt,
		cmin_flt,
		maj_flt,
		cmaj_flt,
		cputime_to_clock_t(utime),
		cputime_to_clock_t(stime),
		cputime_to_clock_t(cutime),
		cputime_to_clock_t(cstime),
		priority,
		nice,
		num_threads,
		// 0,
		start_time,
		vsize,
		mm ? get_mm_rss(mm) : 0,
		rsslim,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
the rest is some obsolete cruft
*/

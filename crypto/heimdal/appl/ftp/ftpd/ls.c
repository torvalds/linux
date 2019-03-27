/*
 * Copyright (c) 1999 - 2002 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#ifndef TEST
#include "ftpd_locl.h"

RCSID("$Id$");

#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#define sec_fprintf2 fprintf
#define sec_fflush fflush
static void list_files(FILE *out, const char **files, int n_files, int flags);
static int parse_flags(const char *options);

int
main(int argc, char **argv)
{
    int i = 1;
    int flags;
    if(argc > 1 && argv[1][0] == '-') {
	flags = parse_flags(argv[1]);
	i = 2;
    } else
	flags = parse_flags(NULL);

    list_files(stdout, (const char **)argv + i, argc - i, flags);
    return 0;
}
#endif

struct fileinfo {
    struct stat st;
    int inode;
    int bsize;
    char mode[11];
    int n_link;
    char *user;
    char *group;
    char *size;
    char *major;
    char *minor;
    char *date;
    char *filename;
    char *link;
};

static void
free_fileinfo(struct fileinfo *f)
{
    free(f->user);
    free(f->group);
    free(f->size);
    free(f->major);
    free(f->minor);
    free(f->date);
    free(f->filename);
    free(f->link);
}

#define LS_DIRS		(1 << 0)
#define LS_IGNORE_DOT	(1 << 1)
#define LS_SORT_MODE	(3 << 2)
#define SORT_MODE(f) ((f) & LS_SORT_MODE)
#define LS_SORT_NAME	(1 << 2)
#define LS_SORT_MTIME	(2 << 2)
#define LS_SORT_SIZE	(3 << 2)
#define LS_SORT_REVERSE	(1 << 4)

#define LS_SIZE		(1 << 5)
#define LS_INODE	(1 << 6)
#define LS_TYPE		(1 << 7)
#define LS_DISP_MODE	(3 << 8)
#define DISP_MODE(f) ((f) & LS_DISP_MODE)
#define LS_DISP_LONG	(1 << 8)
#define LS_DISP_COLUMN	(2 << 8)
#define LS_DISP_CROSS	(3 << 8)
#define LS_SHOW_ALL	(1 << 10)
#define LS_RECURSIVE	(1 << 11)
#define LS_EXTRA_BLANK	(1 << 12)
#define LS_SHOW_DIRNAME	(1 << 13)
#define LS_DIR_FLAG	(1 << 14)	/* these files come via list_dir */

#ifndef S_ISTXT
#define S_ISTXT S_ISVTX
#endif

#if !defined(_S_IFMT) && defined(S_IFMT)
#define _S_IFMT S_IFMT
#endif

#ifndef S_ISSOCK
#define S_ISSOCK(mode)  (((mode) & _S_IFMT) == S_IFSOCK)
#endif

#ifndef S_ISLNK
#define S_ISLNK(mode)   (((mode) & _S_IFMT) == S_IFLNK)
#endif

static size_t
block_convert(size_t blocks)
{
#ifdef S_BLKSIZE
    return blocks * S_BLKSIZE / 1024;
#else
    return blocks * 512 / 1024;
#endif
}

static int
make_fileinfo(FILE *out, const char *filename, struct fileinfo *file, int flags)
{
    char buf[128];
    int file_type = 0;
    struct stat *st = &file->st;

    file->inode = st->st_ino;
    file->bsize = block_convert(st->st_blocks);

    if(S_ISDIR(st->st_mode)) {
	file->mode[0] = 'd';
	file_type = '/';
    }
    else if(S_ISCHR(st->st_mode))
	file->mode[0] = 'c';
    else if(S_ISBLK(st->st_mode))
	file->mode[0] = 'b';
    else if(S_ISREG(st->st_mode)) {
	file->mode[0] = '-';
	if(st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
	    file_type = '*';
    }
    else if(S_ISFIFO(st->st_mode)) {
	file->mode[0] = 'p';
	file_type = '|';
    }
    else if(S_ISLNK(st->st_mode)) {
	file->mode[0] = 'l';
	file_type = '@';
    }
    else if(S_ISSOCK(st->st_mode)) {
	file->mode[0] = 's';
	file_type = '=';
    }
#ifdef S_ISWHT
    else if(S_ISWHT(st->st_mode)) {
	file->mode[0] = 'w';
	file_type = '%';
    }
#endif
    else
	file->mode[0] = '?';
    {
	char *x[] = { "---", "--x", "-w-", "-wx",
		      "r--", "r-x", "rw-", "rwx" };
	strcpy(file->mode + 1, x[(st->st_mode & S_IRWXU) >> 6]);
	strcpy(file->mode + 4, x[(st->st_mode & S_IRWXG) >> 3]);
	strcpy(file->mode + 7, x[(st->st_mode & S_IRWXO) >> 0]);
	if((st->st_mode & S_ISUID)) {
	    if((st->st_mode & S_IXUSR))
		file->mode[3] = 's';
	    else
		file->mode[3] = 'S';
	}
	if((st->st_mode & S_ISGID)) {
	    if((st->st_mode & S_IXGRP))
		file->mode[6] = 's';
	    else
		file->mode[6] = 'S';
	}
	if((st->st_mode & S_ISTXT)) {
	    if((st->st_mode & S_IXOTH))
		file->mode[9] = 't';
	    else
		file->mode[9] = 'T';
	}
    }
    file->n_link = st->st_nlink;
    {
	struct passwd *pwd;
	pwd = getpwuid(st->st_uid);
	if(pwd == NULL) {
	    if (asprintf(&file->user, "%u", (unsigned)st->st_uid) == -1)
		file->user = NULL;
	} else
	    file->user = strdup(pwd->pw_name);
	if (file->user == NULL) {
	    syslog(LOG_ERR, "out of memory");
	    return -1;
	}
    }
    {
	struct group *grp;
	grp = getgrgid(st->st_gid);
	if(grp == NULL) {
	    if (asprintf(&file->group, "%u", (unsigned)st->st_gid) == -1)
		file->group = NULL;
	} else
	    file->group = strdup(grp->gr_name);
	if (file->group == NULL) {
	    syslog(LOG_ERR, "out of memory");
	    return -1;
	}
    }

    if(S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) {
#if defined(major) && defined(minor)
	if (asprintf(&file->major, "%u", (unsigned)major(st->st_rdev)) == -1)
	    file->major = NULL;
	if (asprintf(&file->minor, "%u", (unsigned)minor(st->st_rdev)) == -1)
	    file->minor = NULL;
#else
	/* Don't want to use the DDI/DKI crap. */
	if (asprintf(&file->major, "%u", (unsigned)st->st_rdev) == -1)
	    file->major = NULL;
	if (asprintf(&file->minor, "%u", 0) == -1)
	    file->minor = NULL;
#endif
	if (file->major == NULL || file->minor == NULL) {
	    syslog(LOG_ERR, "out of memory");
	    return -1;
	}
    } else {
	if (asprintf(&file->size, "%lu", (unsigned long)st->st_size) == -1)
	    file->size = NULL;
    }

    {
	time_t t = time(NULL);
	time_t mtime = st->st_mtime;
	struct tm *tm = localtime(&mtime);
	if((t - mtime > 6*30*24*60*60) ||
	   (mtime - t > 6*30*24*60*60))
	    strftime(buf, sizeof(buf), "%b %e  %Y", tm);
	else
	    strftime(buf, sizeof(buf), "%b %e %H:%M", tm);
	file->date = strdup(buf);
	if (file->date == NULL) {
	    syslog(LOG_ERR, "out of memory");
	    return -1;
	}
    }
    {
	const char *p = strrchr(filename, '/');
	if(p)
	    p++;
	else
	    p = filename;
	if((flags & LS_TYPE) && file_type != 0) {
	    if (asprintf(&file->filename, "%s%c", p, file_type) == -1)
		file->filename = NULL;
	} else
	    file->filename = strdup(p);
	if (file->filename == NULL) {
	    syslog(LOG_ERR, "out of memory");
	    return -1;
	}
    }
    if(S_ISLNK(st->st_mode)) {
	int n;
	n = readlink((char *)filename, buf, sizeof(buf) - 1);
	if(n >= 0) {
	    buf[n] = '\0';
	    file->link = strdup(buf);
	    if (file->link == NULL) {
		syslog(LOG_ERR, "out of memory");
		return -1;
	    }
	} else
	    sec_fprintf2(out, "readlink(%s): %s", filename, strerror(errno));
    }
    return 0;
}

static void
print_file(FILE *out,
	   int flags,
	   struct fileinfo *f,
	   int max_inode,
	   int max_bsize,
	   int max_n_link,
	   int max_user,
	   int max_group,
	   int max_size,
	   int max_major,
	   int max_minor,
	   int max_date)
{
    if(f->filename == NULL)
	return;

    if(flags & LS_INODE) {
	sec_fprintf2(out, "%*d", max_inode, f->inode);
	sec_fprintf2(out, "  ");
    }
    if(flags & LS_SIZE) {
	sec_fprintf2(out, "%*d", max_bsize, f->bsize);
	sec_fprintf2(out, "  ");
    }
    sec_fprintf2(out, "%s", f->mode);
    sec_fprintf2(out, "  ");
    sec_fprintf2(out, "%*d", max_n_link, f->n_link);
    sec_fprintf2(out, " ");
    sec_fprintf2(out, "%-*s", max_user, f->user);
    sec_fprintf2(out, "  ");
    sec_fprintf2(out, "%-*s", max_group, f->group);
    sec_fprintf2(out, "  ");
    if(f->major != NULL && f->minor != NULL)
	sec_fprintf2(out, "%*s, %*s", max_major, f->major, max_minor, f->minor);
    else
	sec_fprintf2(out, "%*s", max_size, f->size);
    sec_fprintf2(out, " ");
    sec_fprintf2(out, "%*s", max_date, f->date);
    sec_fprintf2(out, " ");
    sec_fprintf2(out, "%s", f->filename);
    if(f->link)
	sec_fprintf2(out, " -> %s", f->link);
    sec_fprintf2(out, "\r\n");
}

static int
compare_filename(struct fileinfo *a, struct fileinfo *b)
{
    if(a->filename == NULL)
	return 1;
    if(b->filename == NULL)
	return -1;
    return strcmp(a->filename, b->filename);
}

static int
compare_mtime(struct fileinfo *a, struct fileinfo *b)
{
    if(a->filename == NULL)
	return 1;
    if(b->filename == NULL)
	return -1;
    return b->st.st_mtime - a->st.st_mtime;
}

static int
compare_size(struct fileinfo *a, struct fileinfo *b)
{
    if(a->filename == NULL)
	return 1;
    if(b->filename == NULL)
	return -1;
    return b->st.st_size - a->st.st_size;
}

static int list_dir(FILE*, const char*, int);

static int
find_log10(int num)
{
    int i = 1;
    while(num > 10) {
	i++;
	num /= 10;
    }
    return i;
}

/*
 * Operate as lstat but fake up entries for AFS mount points so we don't
 * have to fetch them.
 */

#ifdef KRB5
static int do_the_afs_dance = 1;
#endif

static int
lstat_file (const char *file, struct stat *sb)
{
#ifdef KRB5
    if (do_the_afs_dance &&
	k_hasafs()
	&& strcmp(file, ".")
	&& strcmp(file, "..")
	&& strcmp(file, "/"))
    {
	struct ViceIoctl    a_params;
	char               *dir, *last;
	char               *path_bkp;
	static ino_t	   ino_counter = 0, ino_last = 0;
	int		   ret;
	const int	   maxsize = 2048;

	path_bkp = strdup (file);
	if (path_bkp == NULL)
	    return -1;

	a_params.out = malloc (maxsize);
	if (a_params.out == NULL) {
	    free (path_bkp);
	    return -1;
	}

	/* If path contains more than the filename alone - split it */

	last = strrchr (path_bkp, '/');
	if (last != NULL) {
	    if(last[1] == '\0')
		/* if path ended in /, replace with `.' */
		a_params.in = ".";
	    else
		a_params.in = last + 1;
	    while(last > path_bkp && *--last == '/');
	    if(*last != '/' || last != path_bkp) {
		*++last = '\0';
		dir = path_bkp;
	    } else
		/* we got to the start, so this must be the root dir */
		dir = "/";
	} else {
	    /* file is relative to cdir */
	    dir = ".";
	    a_params.in = path_bkp;
	}

	a_params.in_size  = strlen (a_params.in) + 1;
	a_params.out_size = maxsize;

	ret = k_pioctl (dir, VIOC_AFS_STAT_MT_PT, &a_params, 0);
	free (a_params.out);
	if (ret < 0) {
	    free (path_bkp);

	    if (errno != EINVAL)
		return ret;
	    else
		/* if we get EINVAL this is probably not a mountpoint */
		return lstat (file, sb);
	}

	/*
	 * wow this was a mountpoint, lets cook the struct stat
	 * use . as a prototype
	 */

	ret = lstat (dir, sb);
	free (path_bkp);
	if (ret < 0)
	    return ret;

	if (ino_last == sb->st_ino)
	    ino_counter++;
	else {
	    ino_last    = sb->st_ino;
	    ino_counter = 0;
	}
	sb->st_ino += ino_counter;
	sb->st_nlink = 3;

	return 0;
    }
#endif /* KRB5 */
    return lstat (file, sb);
}

#define IS_DOT_DOTDOT(X) ((X)[0] == '.' && ((X)[1] == '\0' || \
				((X)[1] == '.' && (X)[2] == '\0')))

static int
list_files(FILE *out, const char **files, int n_files, int flags)
{
    struct fileinfo *fi;
    int i;
    int *dirs = NULL;
    size_t total_blocks = 0;
    int n_print = 0;
    int ret = 0;

    if(n_files == 0)
	return 0;

    if(n_files > 1)
	flags |= LS_SHOW_DIRNAME;

    fi = calloc(n_files, sizeof(*fi));
    if (fi == NULL) {
	syslog(LOG_ERR, "out of memory");
	return -1;
    }
    for(i = 0; i < n_files; i++) {
	if(lstat_file(files[i], &fi[i].st) < 0) {
	    sec_fprintf2(out, "%s: %s\r\n", files[i], strerror(errno));
	    fi[i].filename = NULL;
	} else {
	    int include_in_list = 1;
	    total_blocks += block_convert(fi[i].st.st_blocks);
	    if(S_ISDIR(fi[i].st.st_mode)) {
		if(dirs == NULL)
		    dirs = calloc(n_files, sizeof(*dirs));
		if(dirs == NULL) {
		    syslog(LOG_ERR, "%s: %m", files[i]);
		    ret = -1;
		    goto out;
		}
		dirs[i] = 1;
		if((flags & LS_DIRS) == 0)
		    include_in_list = 0;
	    }
	    if(include_in_list) {
		ret = make_fileinfo(out, files[i], &fi[i], flags);
		if (ret)
		    goto out;
		n_print++;
	    }
	}
    }
    switch(SORT_MODE(flags)) {
    case LS_SORT_NAME:
	qsort(fi, n_files, sizeof(*fi),
	      (int (*)(const void*, const void*))compare_filename);
	break;
    case LS_SORT_MTIME:
	qsort(fi, n_files, sizeof(*fi),
	      (int (*)(const void*, const void*))compare_mtime);
	break;
    case LS_SORT_SIZE:
	qsort(fi, n_files, sizeof(*fi),
	      (int (*)(const void*, const void*))compare_size);
	break;
    }
    if(DISP_MODE(flags) == LS_DISP_LONG) {
	int max_inode = 0;
	int max_bsize = 0;
	int max_n_link = 0;
	int max_user = 0;
	int max_group = 0;
	int max_size = 0;
	int max_major = 0;
	int max_minor = 0;
	int max_date = 0;
	for(i = 0; i < n_files; i++) {
	    if(fi[i].filename == NULL)
		continue;
	    if(fi[i].inode > max_inode)
		max_inode = fi[i].inode;
	    if(fi[i].bsize > max_bsize)
		max_bsize = fi[i].bsize;
	    if(fi[i].n_link > max_n_link)
		max_n_link = fi[i].n_link;
	    if(strlen(fi[i].user) > max_user)
		max_user = strlen(fi[i].user);
	    if(strlen(fi[i].group) > max_group)
		max_group = strlen(fi[i].group);
	    if(fi[i].major != NULL && strlen(fi[i].major) > max_major)
		max_major = strlen(fi[i].major);
	    if(fi[i].minor != NULL && strlen(fi[i].minor) > max_minor)
		max_minor = strlen(fi[i].minor);
	    if(fi[i].size != NULL && strlen(fi[i].size) > max_size)
		max_size = strlen(fi[i].size);
	    if(strlen(fi[i].date) > max_date)
		max_date = strlen(fi[i].date);
	}
	if(max_size < max_major + max_minor + 2)
	    max_size = max_major + max_minor + 2;
	else if(max_size - max_minor - 2 > max_major)
	    max_major = max_size - max_minor - 2;
	max_inode = find_log10(max_inode);
	max_bsize = find_log10(max_bsize);
	max_n_link = find_log10(max_n_link);

	if(n_print > 0)
	    sec_fprintf2(out, "total %lu\r\n", (unsigned long)total_blocks);
	if(flags & LS_SORT_REVERSE)
	    for(i = n_files - 1; i >= 0; i--)
		print_file(out,
			   flags,
			   &fi[i],
			   max_inode,
			   max_bsize,
			   max_n_link,
			   max_user,
			   max_group,
			   max_size,
			   max_major,
			   max_minor,
			   max_date);
	else
	    for(i = 0; i < n_files; i++)
		print_file(out,
			   flags,
			   &fi[i],
			   max_inode,
			   max_bsize,
			   max_n_link,
			   max_user,
			   max_group,
			   max_size,
			   max_major,
			   max_minor,
			   max_date);
    } else if(DISP_MODE(flags) == LS_DISP_COLUMN ||
	      DISP_MODE(flags) == LS_DISP_CROSS) {
	int max_len = 0;
	int size_len = 0;
	int num_files = n_files;
	int columns;
	int j;
	for(i = 0; i < n_files; i++) {
	    if(fi[i].filename == NULL) {
		num_files--;
		continue;
	    }
	    if(strlen(fi[i].filename) > max_len)
		max_len = strlen(fi[i].filename);
	    if(find_log10(fi[i].bsize) > size_len)
		size_len = find_log10(fi[i].bsize);
	}
	if(num_files == 0)
	    goto next;
	if(flags & LS_SIZE) {
	    columns = 80 / (size_len + 1 + max_len + 1);
	    max_len = 80 / columns - size_len - 1;
	} else {
	    columns = 80 / (max_len + 1); /* get space between columns */
	    max_len = 80 / columns;
	}
	if(flags & LS_SIZE)
	    sec_fprintf2(out, "total %lu\r\n",
			 (unsigned long)total_blocks);
	if(DISP_MODE(flags) == LS_DISP_CROSS) {
	    for(i = 0, j = 0; i < n_files; i++) {
		if(fi[i].filename == NULL)
		    continue;
		if(flags & LS_SIZE)
		    sec_fprintf2(out, "%*u %-*s", size_len, fi[i].bsize,
				 max_len, fi[i].filename);
		else
		    sec_fprintf2(out, "%-*s", max_len, fi[i].filename);
		j++;
		if(j == columns) {
		    sec_fprintf2(out, "\r\n");
		    j = 0;
		}
	    }
	    if(j > 0)
		sec_fprintf2(out, "\r\n");
	} else {
	    int skip = (num_files + columns - 1) / columns;

	    for(i = 0; i < skip; i++) {
		for(j = i; j < n_files;) {
		    while(j < n_files && fi[j].filename == NULL)
			j++;
		    if(flags & LS_SIZE)
			sec_fprintf2(out, "%*u %-*s", size_len, fi[j].bsize,
				     max_len, fi[j].filename);
		    else
			sec_fprintf2(out, "%-*s", max_len, fi[j].filename);
		    j += skip;
		}
		sec_fprintf2(out, "\r\n");
	    }
	}
    } else {
	for(i = 0; i < n_files; i++) {
	    if(fi[i].filename == NULL)
		continue;
	    sec_fprintf2(out, "%s\r\n", fi[i].filename);
	}
    }
 next:
    if(((flags & LS_DIRS) == 0 || (flags & LS_RECURSIVE)) && dirs != NULL) {
	for(i = 0; i < n_files; i++) {
	    if(dirs[i]) {
		const char *p = strrchr(files[i], '/');
		if(p == NULL)
		    p = files[i];
		else
		    p++;
		if(!(flags & LS_DIR_FLAG) || !IS_DOT_DOTDOT(p)) {
		    if((flags & LS_SHOW_DIRNAME)) {
			if ((flags & LS_EXTRA_BLANK))
			    sec_fprintf2(out, "\r\n");
			sec_fprintf2(out, "%s:\r\n", files[i]);
		    }
		    list_dir(out, files[i], flags | LS_DIRS | LS_EXTRA_BLANK);
		}
	    }
	}
    }
 out:
    for(i = 0; i < n_files; i++)
	free_fileinfo(&fi[i]);
    free(fi);
    if(dirs != NULL)
	free(dirs);
    return ret;
}

static void
free_files (char **files, int n)
{
    int i;

    for (i = 0; i < n; ++i)
	free (files[i]);
    free (files);
}

static int
hide_file(const char *filename, int flags)
{
    if(filename[0] != '.')
	return 0;
    if((flags & LS_IGNORE_DOT))
	return 1;
    if(filename[1] == '\0' || (filename[1] == '.' && filename[2] == '\0')) {
	if((flags & LS_SHOW_ALL))
	    return 0;
	else
	    return 1;
    }
    return 0;
}

static int
list_dir(FILE *out, const char *directory, int flags)
{
    DIR *d = opendir(directory);
    struct dirent *ent;
    char **files = NULL;
    int n_files = 0;
    int ret;

    if(d == NULL) {
	syslog(LOG_ERR, "%s: %m", directory);
	return -1;
    }
    while((ent = readdir(d)) != NULL) {
	void *tmp;

	if(hide_file(ent->d_name, flags))
	    continue;
	tmp = realloc(files, (n_files + 1) * sizeof(*files));
	if (tmp == NULL) {
	    syslog(LOG_ERR, "%s: out of memory", directory);
	    free_files (files, n_files);
	    closedir (d);
	    return -1;
	}
	files = tmp;
	ret = asprintf(&files[n_files], "%s/%s", directory, ent->d_name);
	if (ret == -1) {
	    syslog(LOG_ERR, "%s: out of memory", directory);
	    free_files (files, n_files);
	    closedir (d);
	    return -1;
	}
	++n_files;
    }
    closedir(d);
    return list_files(out, (const char**)files, n_files, flags | LS_DIR_FLAG);
}

static int
parse_flags(const char *options)
{
#ifdef TEST
    int flags = LS_SORT_NAME | LS_IGNORE_DOT | LS_DISP_COLUMN;
#else
    int flags = LS_SORT_NAME | LS_IGNORE_DOT | LS_DISP_LONG;
#endif

    const char *p;
    if(options == NULL || *options != '-')
	return flags;
    for(p = options + 1; *p; p++) {
	switch(*p) {
	case '1':
	    flags = (flags & ~LS_DISP_MODE);
	    break;
	case 'a':
	    flags |= LS_SHOW_ALL;
	    /*FALLTHROUGH*/
	case 'A':
	    flags &= ~LS_IGNORE_DOT;
	    break;
	case 'C':
	    flags = (flags & ~LS_DISP_MODE) | LS_DISP_COLUMN;
	    break;
	case 'd':
	    flags |= LS_DIRS;
	    break;
	case 'f':
	    flags = (flags & ~LS_SORT_MODE);
	    break;
	case 'F':
	    flags |= LS_TYPE;
	    break;
	case 'i':
	    flags |= LS_INODE;
	    break;
	case 'l':
	    flags = (flags & ~LS_DISP_MODE) | LS_DISP_LONG;
	    break;
	case 'r':
	    flags |= LS_SORT_REVERSE;
	    break;
	case 'R':
	    flags |= LS_RECURSIVE;
	    break;
	case 's':
	    flags |= LS_SIZE;
	    break;
	case 'S':
	    flags = (flags & ~LS_SORT_MODE) | LS_SORT_SIZE;
	    break;
	case 't':
	    flags = (flags & ~LS_SORT_MODE) | LS_SORT_MTIME;
	    break;
	case 'x':
	    flags = (flags & ~LS_DISP_MODE) | LS_DISP_CROSS;
	    break;
	    /* these are a bunch of unimplemented flags from BSD ls */
	case 'k': /* display sizes in kB */
	case 'c': /* last change time */
	case 'L': /* list symlink target */
	case 'm': /* stream output */
	case 'o': /* BSD file flags */
	case 'p': /* display / after directories */
	case 'q': /* print non-graphic characters */
	case 'u': /* use last access time */
	case 'T': /* display complete time */
	case 'W': /* include whiteouts */
	    break;
	}
    }
    return flags;
}

int
builtin_ls(FILE *out, const char *file)
{
    int flags;
    int ret;

    if(*file == '-') {
	flags = parse_flags(file);
	file = ".";
    } else
	flags = parse_flags("");

    ret = list_files(out, &file, 1, flags);
    sec_fflush(out);
    return ret;
}

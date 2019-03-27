/*-
 * Copyright (c) 1999, 2001, 2002 Robert N M Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * getfacl -- POSIX.1e utility to extract ACLs from files and directories
 * and send the results to stdout
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int more_than_one = 0;

static void
usage(void)
{

	fprintf(stderr, "getfacl [-dhnqv] [file ...]\n");
}

static char *
getuname(uid_t uid)
{
	struct passwd *pw;
	static char uids[10];

	if ((pw = getpwuid(uid)) == NULL) {
		(void)snprintf(uids, sizeof(uids), "%u", uid);
		return (uids);
	} else
		return (pw->pw_name);
}

static char *
getgname(gid_t gid)
{
	struct group *gr;
	static char gids[10];

	if ((gr = getgrgid(gid)) == NULL) {
		(void)snprintf(gids, sizeof(gids), "%u", gid);
		return (gids);
	} else
		return (gr->gr_name);
}

/*
 * return an ACL corresponding to the permissions
 * contained in struct stat
 */
static acl_t
acl_from_stat(const struct stat *sb)
{
	acl_t acl;
	acl_entry_t entry;
	acl_permset_t perms;

	/* create the ACL */
	acl = acl_init(3);
	if (!acl)
		return NULL;

	/* First entry: ACL_USER_OBJ */
	if (acl_create_entry(&acl, &entry) == -1)
		return NULL;
	if (acl_set_tag_type(entry, ACL_USER_OBJ) == -1)
		return NULL;

	if (acl_get_permset(entry, &perms) == -1)
		return NULL;
	if (acl_clear_perms(perms) == -1)
		return NULL;

	/* calculate user mode */
	if (sb->st_mode & S_IRUSR)
		if (acl_add_perm(perms, ACL_READ) == -1)
			return NULL;
	if (sb->st_mode & S_IWUSR)
		if (acl_add_perm(perms, ACL_WRITE) == -1)
			return NULL;
	if (sb->st_mode & S_IXUSR)
		if (acl_add_perm(perms, ACL_EXECUTE) == -1)
			return NULL;
	if (acl_set_permset(entry, perms) == -1)
		return NULL;

	/* Second entry: ACL_GROUP_OBJ */
	if (acl_create_entry(&acl, &entry) == -1)
		return NULL;
	if (acl_set_tag_type(entry, ACL_GROUP_OBJ) == -1)
		return NULL;

	if (acl_get_permset(entry, &perms) == -1)
		return NULL;
	if (acl_clear_perms(perms) == -1)
		return NULL;

	/* calculate group mode */
	if (sb->st_mode & S_IRGRP)
		if (acl_add_perm(perms, ACL_READ) == -1)
			return NULL;
	if (sb->st_mode & S_IWGRP)
		if (acl_add_perm(perms, ACL_WRITE) == -1)
			return NULL;
	if (sb->st_mode & S_IXGRP)
		if (acl_add_perm(perms, ACL_EXECUTE) == -1)
			return NULL;
	if (acl_set_permset(entry, perms) == -1)
		return NULL;

	/* Third entry: ACL_OTHER */
	if (acl_create_entry(&acl, &entry) == -1)
		return NULL;
	if (acl_set_tag_type(entry, ACL_OTHER) == -1)
		return NULL;

	if (acl_get_permset(entry, &perms) == -1)
		return NULL;
	if (acl_clear_perms(perms) == -1)
		return NULL;

	/* calculate other mode */
	if (sb->st_mode & S_IROTH)
		if (acl_add_perm(perms, ACL_READ) == -1)
			return NULL;
	if (sb->st_mode & S_IWOTH)
		if (acl_add_perm(perms, ACL_WRITE) == -1)
			return NULL;
	if (sb->st_mode & S_IXOTH)
		if (acl_add_perm(perms, ACL_EXECUTE) == -1)
			return NULL;
	if (acl_set_permset(entry, perms) == -1)
		return NULL;

	return(acl);
}

static int
print_acl(char *path, acl_type_t type, int hflag, int iflag, int nflag,
    int qflag, int vflag)
{
	struct stat	sb;
	acl_t	acl;
	char	*acl_text;
	int	error, flags = 0, ret;

	if (hflag)
		error = lstat(path, &sb);
	else
		error = stat(path, &sb);
	if (error == -1) {
		warn("%s: stat() failed", path);
		return(-1);
	}

	if (hflag)
		ret = lpathconf(path, _PC_ACL_NFS4);
	else
		ret = pathconf(path, _PC_ACL_NFS4);
	if (ret > 0) {
		if (type == ACL_TYPE_DEFAULT) {
			warnx("%s: there are no default entries in NFSv4 ACLs",
			    path);
			return (-1);
		}
		type = ACL_TYPE_NFS4;
	} else if (ret < 0 && errno != EINVAL) {
		warn("%s: pathconf(..., _PC_ACL_NFS4) failed", path);
		return (-1);
	}

	if (more_than_one)
		printf("\n");
	else
		more_than_one++;

	if (!qflag)
		printf("# file: %s\n# owner: %s\n# group: %s\n", path,
		    getuname(sb.st_uid), getgname(sb.st_gid));

	if (hflag)
		acl = acl_get_link_np(path, type);
	else
		acl = acl_get_file(path, type);
	if (!acl) {
		if (errno != EOPNOTSUPP) {
			warn("%s", path);
			return(-1);
		}
		errno = 0;
		if (type == ACL_TYPE_DEFAULT)
			return(0);
		acl = acl_from_stat(&sb);
		if (!acl) {
			warn("%s: acl_from_stat() failed", path);
			return(-1);
		}
	}

	if (iflag)
		flags |= ACL_TEXT_APPEND_ID;

	if (nflag)
		flags |= ACL_TEXT_NUMERIC_IDS;

	if (vflag)
		flags |= ACL_TEXT_VERBOSE;

	acl_text = acl_to_text_np(acl, 0, flags);
	if (!acl_text) {
		warn("%s: acl_to_text_np() failed", path);
		return(-1);
	}

	printf("%s", acl_text);

	(void)acl_free(acl);
	(void)acl_free(acl_text);

	return(0);
}

static int
print_acl_from_stdin(acl_type_t type, int hflag, int iflag, int nflag,
    int qflag, int vflag)
{
	char	*p, pathname[PATH_MAX];
	int	carried_error = 0;

	while (fgets(pathname, (int)sizeof(pathname), stdin)) {
		if ((p = strchr(pathname, '\n')) != NULL)
			*p = '\0';
		if (print_acl(pathname, type, hflag, iflag, nflag,
		    qflag, vflag) == -1) {
			carried_error = -1;
		}
	}

	return(carried_error);
}

int
main(int argc, char *argv[])
{
	acl_type_t	type = ACL_TYPE_ACCESS;
	int	carried_error = 0;
	int	ch, error, i;
	int	hflag, iflag, qflag, nflag, vflag;

	hflag = 0;
	iflag = 0;
	qflag = 0;
	nflag = 0;
	vflag = 0;
	while ((ch = getopt(argc, argv, "dhinqv")) != -1)
		switch(ch) {
		case 'd':
			type = ACL_TYPE_DEFAULT;
			break;
		case 'h':
			hflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
			return(-1);
		}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		error = print_acl_from_stdin(type, hflag, iflag, nflag,
		    qflag, vflag);
		return(error ? 1 : 0);
	}

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-")) {
			error = print_acl_from_stdin(type, hflag, iflag, nflag,
			    qflag, vflag);
			if (error == -1)
				carried_error = -1;
		} else {
			error = print_acl(argv[i], type, hflag, iflag, nflag,
			    qflag, vflag);
			if (error == -1)
				carried_error = -1;
		}
	}

	return(carried_error ? 1 : 0);
}

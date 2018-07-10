/* vi: set sw=4 ts=4: */
/*
 * update_passwd
 *
 * update_passwd is a common function for passwd and chpasswd applets;
 * it is responsible for updating password file (i.e. /etc/passwd or
 * /etc/shadow) for a given user and password.
 *
 * Moved from loginutils/passwd.c by Alexander Shishkin <virtuoso@slind.org>
 *
 * Modified to be able to add or delete users, groups and users to/from groups
 * by Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

#if ENABLE_SELINUX
static void check_selinux_update_passwd(const char *username)
{
	security_context_t context;
	char *seuser;

	if (getuid() != (uid_t)0 || is_selinux_enabled() == 0)
		return;  /* No need to check */

	if (getprevcon_raw(&context) < 0)
		bb_perror_msg_and_die("getprevcon failed");
	seuser = strtok(context, ":");
	if (!seuser)
		bb_error_msg_and_die("invalid context '%s'", context);
	if (strcmp(seuser, username) != 0) {
		security_class_t tclass;
		access_vector_t av;

		tclass = string_to_security_class("passwd");
		if (tclass == 0)
			goto die;
		av = string_to_av_perm(tclass, "passwd");
		if (av == 0)
			goto die;

		if (selinux_check_passwd_access(av) != 0)
 die:
			bb_error_msg_and_die("SELinux: access denied");
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		freecon(context);
}
#else
# define check_selinux_update_passwd(username) ((void)0)
#endif

/*
 1) add a user: update_passwd(FILE, USER, REMAINING_PWLINE, NULL)
    only if CONFIG_ADDUSER=y and applet_name[0] == 'a' like in adduser

 2) add a group: update_passwd(FILE, GROUP, REMAINING_GRLINE, NULL)
    only if CONFIG_ADDGROUP=y and applet_name[0] == 'a' like in addgroup

 3) add a user to a group: update_passwd(FILE, GROUP, NULL, MEMBER)
    only if CONFIG_FEATURE_ADDUSER_TO_GROUP=y, applet_name[0] == 'a'
    like in addgroup and member != NULL

 4) delete a user: update_passwd(FILE, USER, NULL, NULL)

 5) delete a group: update_passwd(FILE, GROUP, NULL, NULL)

 6) delete a user from a group: update_passwd(FILE, GROUP, NULL, MEMBER)
    only if CONFIG_FEATURE_DEL_USER_FROM_GROUP=y and member != NULL

 7) change user's password: update_passwd(FILE, USER, NEW_PASSWD, NULL)
    only if CONFIG_PASSWD=y and applet_name[0] == 'p' like in passwd
    or if CONFIG_CHPASSWD=y and applet_name[0] == 'c' like in chpasswd

 8) delete a user from all groups: update_passwd(FILE, NULL, NULL, MEMBER)

 This function does not validate the arguments fed to it
 so the calling program should take care of that.

 Returns number of lines changed, or -1 on error.
*/
int FAST_FUNC update_passwd(const char *filename,
		const char *name,
		const char *new_passwd,
		const char *member)
{
#if !(ENABLE_FEATURE_ADDUSER_TO_GROUP || ENABLE_FEATURE_DEL_USER_FROM_GROUP)
#define member NULL
#endif
	struct stat sb;
	struct flock lock;
	FILE *old_fp;
	FILE *new_fp;
	char *fnamesfx;
	char *sfx_char;
	char *name_colon;
	int old_fd;
	int new_fd;
	int i;
	int changed_lines;
	int ret = -1; /* failure */
	/* used as a bool: "are we modifying /etc/shadow?" */
#if ENABLE_FEATURE_SHADOWPASSWDS
	const char *shadow = strstr(filename, "shadow");
#else
# define shadow NULL
#endif

	filename = xmalloc_follow_symlinks(filename);
	if (filename == NULL)
		return ret;

	if (name)
		check_selinux_update_passwd(name);

	/* New passwd file, "/etc/passwd+" for now */
	fnamesfx = xasprintf("%s+", filename);
	sfx_char = &fnamesfx[strlen(fnamesfx)-1];
	name_colon = xasprintf("%s:", name ? name : "");

	if (shadow)
		old_fp = fopen(filename, "r+");
	else
		old_fp = fopen_or_warn(filename, "r+");
	if (!old_fp) {
		if (shadow)
			ret = 0; /* missing shadow is not an error */
		goto free_mem;
	}
	old_fd = fileno(old_fp);

	selinux_preserve_fcontext(old_fd);

	/* Try to create "/etc/passwd+". Wait if it exists. */
	i = 30;
	do {
		// FIXME: on last iteration try w/o O_EXCL but with O_TRUNC?
		new_fd = open(fnamesfx, O_WRONLY|O_CREAT|O_EXCL, 0600);
		if (new_fd >= 0) goto created;
		if (errno != EEXIST) break;
		usleep(100000); /* 0.1 sec */
	} while (--i);
	bb_perror_msg("can't create '%s'", fnamesfx);
	goto close_old_fp;

 created:
	if (fstat(old_fd, &sb) == 0) {
		fchmod(new_fd, sb.st_mode & 0777); /* ignore errors */
		fchown(new_fd, sb.st_uid, sb.st_gid);
	}
	errno = 0;
	new_fp = xfdopen_for_write(new_fd);

	/* Backup file is "/etc/passwd-" */
	*sfx_char = '-';
	/* Delete old backup */
	i = (unlink(fnamesfx) && errno != ENOENT);
	/* Create backup as a hardlink to current */
	if (i || link(filename, fnamesfx))
		bb_perror_msg("warning: can't create backup copy '%s'",
				fnamesfx);
	*sfx_char = '+';

	/* Lock the password file before updating */
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	if (fcntl(old_fd, F_SETLK, &lock) < 0)
		bb_perror_msg("warning: can't lock '%s'", filename);
	lock.l_type = F_UNLCK;

	/* Read current password file, write updated /etc/passwd+ */
	changed_lines = 0;
	while (1) {
		char *cp, *line;

		line = xmalloc_fgetline(old_fp);
		if (!line) /* EOF/error */
			break;

#if ENABLE_FEATURE_ADDUSER_TO_GROUP || ENABLE_FEATURE_DEL_USER_FROM_GROUP
		if (!name && member) {
			/* Delete member from all groups */
			/* line is "GROUP:PASSWD:[member1[,member2]...]" */
			unsigned member_len = strlen(member);
			char *list = strrchr(line, ':');
			while (list) {
				list++;
 next_list_element:
				if (is_prefixed_with(list, member)) {
					char c;
					changed_lines++;
					c = list[member_len];
					if (c == '\0') {
						if (list[-1] == ',')
							list--;
						*list = '\0';
						break;
					}
					if (c == ',') {
						overlapping_strcpy(list, list + member_len + 1);
						goto next_list_element;
					}
					changed_lines--;
				}
				list = strchr(list, ',');
			}
			fprintf(new_fp, "%s\n", line);
			goto next;
		}
#endif

		cp = is_prefixed_with(line, name_colon);
		if (!cp) {
			fprintf(new_fp, "%s\n", line);
			goto next;
		}

		/* We have a match with "name:"... */
		/* cp points past "name:" */

#if ENABLE_FEATURE_ADDUSER_TO_GROUP || ENABLE_FEATURE_DEL_USER_FROM_GROUP
		if (member) {
			/* It's actually /etc/group+, not /etc/passwd+ */
			if (ENABLE_FEATURE_ADDUSER_TO_GROUP
			 && applet_name[0] == 'a'
			) {
				/* Add user to group */
				fprintf(new_fp, "%s%s%s\n", line,
					last_char_is(line, ':') ? "" : ",",
					member);
				changed_lines++;
			} else if (ENABLE_FEATURE_DEL_USER_FROM_GROUP
			/* && applet_name[0] == 'd' */
			) {
				/* Delete user from group */
				char *tmp;
				const char *fmt = "%s";

				/* find the start of the member list: last ':' */
				cp = strrchr(line, ':');
				/* cut it */
				*cp++ = '\0';
				/* write the cut line name:passwd:gid:
				 * or name:!:: */
				fprintf(new_fp, "%s:", line);
				/* parse the tokens of the member list */
				tmp = cp;
				while ((cp = strsep(&tmp, ",")) != NULL) {
					if (strcmp(member, cp) != 0) {
						fprintf(new_fp, fmt, cp);
						fmt = ",%s";
					} else {
						/* found member, skip it */
						changed_lines++;
					}
				}
				fprintf(new_fp, "\n");
			}
		} else
#endif
		if ((ENABLE_PASSWD && applet_name[0] == 'p')
		 || (ENABLE_CHPASSWD && applet_name[0] == 'c')
		) {
			/* Change passwd */
			cp = strchrnul(cp, ':'); /* move past old passwd */

			if (shadow && *cp == ':') {
				/* /etc/shadow's field 3 (passwd change date) needs updating */
				/* move past old change date */
				cp = strchrnul(cp + 1, ':');
				/* "name:" + "new_passwd" + ":" + "change date" + ":rest of line" */
				fprintf(new_fp, "%s%s:%u%s\n", name_colon, new_passwd,
					(unsigned)(time(NULL)) / (24*60*60), cp);
			} else {
				/* "name:" + "new_passwd" + ":rest of line" */
				fprintf(new_fp, "%s%s%s\n", name_colon, new_passwd, cp);
			}
			changed_lines++;
		} /* else delete user or group: skip the line */
 next:
		free(line);
	}

	if (changed_lines == 0) {
#if ENABLE_FEATURE_ADDUSER_TO_GROUP || ENABLE_FEATURE_DEL_USER_FROM_GROUP
		if (member) {
			if (ENABLE_ADDGROUP && applet_name[0] == 'a')
				bb_error_msg("can't find %s in %s", name, filename);
			if (ENABLE_DELGROUP && applet_name[0] == 'd')
				bb_error_msg("can't find %s in %s", member, filename);
		}
#endif
		if ((ENABLE_ADDUSER || ENABLE_ADDGROUP)
		 && applet_name[0] == 'a' && !member
		) {
			/* add user or group */
			fprintf(new_fp, "%s%s\n", name_colon, new_passwd);
			changed_lines++;
		}
	}

	fcntl(old_fd, F_SETLK, &lock);

	/* We do want all of them to execute, thus | instead of || */
	errno = 0;
	if ((ferror(old_fp) | fflush(new_fp) | fsync(new_fd) | fclose(new_fp))
	 || rename(fnamesfx, filename)
	) {
		/* At least one of those failed */
		bb_perror_nomsg();
		goto unlink_new;
	}
	/* Success: ret >= 0 */
	ret = changed_lines;

 unlink_new:
	if (ret < 0)
		unlink(fnamesfx);

 close_old_fp:
	fclose(old_fp);

 free_mem:
	free(fnamesfx);
	free((char *)filename);
	free(name_colon);
	return ret;
}

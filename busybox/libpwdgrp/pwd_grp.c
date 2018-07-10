/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2014 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY!!
 *
 * Rewrite of some parts. Main differences are:
 *
 * 1) the buffer for getpwuid, getgrgid, getpwnam, getgrnam is dynamically
 *    allocated.
 *    If ENABLE_FEATURE_CLEAN_UP is set the buffers are freed at program
 *    exit using the atexit function to make valgrind happy.
 * 2) the passwd/group files:
 *      a) must contain the expected number of fields (as per count of field
 *         delimiters ":") or we will complain with a error message.
 *      b) leading and trailing whitespace in fields is stripped.
 *      c) some fields are not allowed to be empty (e.g. username, uid/gid),
 *         and in this case NULL is returned and errno is set to EINVAL.
 *         This behaviour could be easily changed by modifying PW_DEF, GR_DEF,
 *         SP_DEF strings (uppercase makes a field mandatory).
 *      d) the string representing uid/gid must be convertible by strtoXX
 *         functions, or errno is set to EINVAL.
 *      e) leading and trailing whitespace in group member names is stripped.
 * 3) the internal function for getgrouplist uses dynamically allocated buffer.
 * 4) at the moment only the functions really used by busybox code are
 *    implemented, if you need a particular missing function it should be
 *    easy to write it by using the internal common code.
 */
#include "libbb.h"

struct const_passdb {
	const char *filename;
	char def[7 + 2*ENABLE_USE_BB_SHADOW];
	uint8_t off[7 + 2*ENABLE_USE_BB_SHADOW];
	uint8_t numfields;
	uint8_t size_of;
};
struct passdb {
	const char *filename;
	char def[7 + 2*ENABLE_USE_BB_SHADOW];
	uint8_t off[7 + 2*ENABLE_USE_BB_SHADOW];
	uint8_t numfields;
	uint8_t size_of;
	FILE *fp;
	char *malloced;
};
/* Note: for shadow db, def[] will not contain terminating NUL,
 * but convert_to_struct() logic detects def[] end by "less than SP?",
 * not by "is it NUL?" condition; and off[0] happens to be zero
 * for every db anyway, so there _is_ in fact a terminating NUL there.
 */

/* S = string not empty, s = string maybe empty,
 * I = uid,gid, l = long maybe empty, m = members,
 * r = reserved
 */
#define PW_DEF "SsIIsss"
#define GR_DEF "SsIm"
#define SP_DEF "Ssllllllr"

static const struct const_passdb const_pw_db = {
	_PATH_PASSWD, PW_DEF,
	{
		offsetof(struct passwd, pw_name),       /* 0 S */
		offsetof(struct passwd, pw_passwd),     /* 1 s */
		offsetof(struct passwd, pw_uid),        /* 2 I */
		offsetof(struct passwd, pw_gid),        /* 3 I */
		offsetof(struct passwd, pw_gecos),      /* 4 s */
		offsetof(struct passwd, pw_dir),        /* 5 s */
		offsetof(struct passwd, pw_shell)       /* 6 s */
	},
	sizeof(PW_DEF)-1, sizeof(struct passwd)
};
static const struct const_passdb const_gr_db = {
	_PATH_GROUP, GR_DEF,
	{
		offsetof(struct group, gr_name),        /* 0 S */
		offsetof(struct group, gr_passwd),      /* 1 s */
		offsetof(struct group, gr_gid),         /* 2 I */
		offsetof(struct group, gr_mem)          /* 3 m (char **) */
	},
	sizeof(GR_DEF)-1, sizeof(struct group)
};
#if ENABLE_USE_BB_SHADOW
static const struct const_passdb const_sp_db = {
	_PATH_SHADOW, SP_DEF,
	{
		offsetof(struct spwd, sp_namp),         /* 0 S Login name */
		offsetof(struct spwd, sp_pwdp),         /* 1 s Encrypted password */
		offsetof(struct spwd, sp_lstchg),       /* 2 l */
		offsetof(struct spwd, sp_min),          /* 3 l */
		offsetof(struct spwd, sp_max),          /* 4 l */
		offsetof(struct spwd, sp_warn),         /* 5 l */
		offsetof(struct spwd, sp_inact),        /* 6 l */
		offsetof(struct spwd, sp_expire),       /* 7 l */
		offsetof(struct spwd, sp_flag)          /* 8 r Reserved */
	},
	sizeof(SP_DEF)-1, sizeof(struct spwd)
};
#endif

/* We avoid having big global data. */
struct statics {
	/* We use same buffer (db[0].malloced) for getpwuid and getpwnam.
	 * Manpage says:
	 * "The return value may point to a static area, and may be overwritten
	 * by subsequent calls to getpwent(), getpwnam(), or getpwuid()."
	 */
	struct passdb db[2 + ENABLE_USE_BB_SHADOW];
	char *tokenize_end;
	unsigned string_size;
};

static struct statics *ptr_to_statics;
#define S     (*ptr_to_statics)
#define has_S (ptr_to_statics)

#if ENABLE_FEATURE_CLEAN_UP
static void free_static(void)
{
	free(S.db[0].malloced);
	free(S.db[1].malloced);
# if ENABLE_USE_BB_SHADOW
	free(S.db[2].malloced);
# endif
	free(ptr_to_statics);
}
#endif

static struct statics *get_S(void)
{
	if (!ptr_to_statics) {
		ptr_to_statics = xzalloc(sizeof(S));
		memcpy(&S.db[0], &const_pw_db, sizeof(const_pw_db));
		memcpy(&S.db[1], &const_gr_db, sizeof(const_gr_db));
#if ENABLE_USE_BB_SHADOW
		memcpy(&S.db[2], &const_sp_db, sizeof(const_sp_db));
#endif
#if ENABLE_FEATURE_CLEAN_UP
		atexit(free_static);
#endif
	}
	return ptr_to_statics;
}

/* Internal functions */

/* Divide the passwd/group/shadow record in fields
 * by substituting the given delimiter
 * e.g. ':' or ',' with '\0'.
 * Returns the number of fields found.
 * Strips leading and trailing whitespace in fields.
 */
static int tokenize(char *buffer, int ch)
{
	char *p = buffer;
	char *s = p;
	int num_fields = 0;

	for (;;) {
		if (isblank(*s)) {
			overlapping_strcpy(s, skip_whitespace(s));
		}
		if (*p == ch || *p == '\0') {
			char *end = p;
			while (p != s && isblank(p[-1]))
				p--;
			if (p != end)
				overlapping_strcpy(p, end);
			num_fields++;
			if (*end == '\0') {
				S.tokenize_end = p + 1;
				return num_fields;
			}
			*p = '\0';
			s = p + 1;
		}
		p++;
	}
}

/* Returns !NULL on success and matching line broken up in fields by '\0' in buf.
 * We require the expected number of fields to be found.
 */
static char *parse_common(FILE *fp, struct passdb *db,
		const char *key, int field_pos)
{
	char *buf;

	while ((buf = xmalloc_fgetline(fp)) != NULL) {
		/* Skip empty lines, comment lines */
		if (buf[0] == '\0' || buf[0] == '#')
			goto free_and_next;
		if (tokenize(buf, ':') != db->numfields) {
			/* number of fields is wrong */
			bb_error_msg("%s: bad record", db->filename);
			goto free_and_next;
		}

		if (field_pos == -1) {
			/* no key specified: sequential read, return a record */
			break;
		}
		if (strcmp(key, nth_string(buf, field_pos)) == 0) {
			/* record found */
			break;
		}
 free_and_next:
		free(buf);
	}

	S.string_size = S.tokenize_end - buf;
/*
 * Ugly hack: group db requires additional buffer space
 * for members[] array. If there is only one group, we need space
 * for 3 pointers: alignment padding, group name, NULL.
 * +1 for every additional group.
 */
	if (buf && db->numfields == sizeof(GR_DEF)-1) { /* if we read group file... */
		int cnt = 3;
		char *p = buf;
		while (p < S.tokenize_end)
			if (*p++ == ',')
				cnt++;
		S.string_size += cnt * sizeof(char*);
//bb_error_msg("+%d words = %u key:%s buf:'%s'", cnt, S.string_size, key, buf);
		buf = xrealloc(buf, S.string_size);
	}

	return buf;
}

static char *parse_file(struct passdb *db,
		const char *key, int field_pos)
{
	char *buf = NULL;
	FILE *fp = fopen_for_read(db->filename);

	if (fp) {
		buf = parse_common(fp, db, key, field_pos);
		fclose(fp);
	}
	return buf;
}

/* Convert passwd/group/shadow file record in buffer to a struct */
static void *convert_to_struct(struct passdb *db,
		char *buffer, void *result)
{
	const char *def = db->def;
	const uint8_t *off = db->off;

	/* For consistency, zero out all fields */
	memset(result, 0, db->size_of);

	for (;;) {
		void *member = (char*)result + (*off++);

		if ((*def | 0x20) == 's') { /* s or S */
			*(char **)member = (char*)buffer;
			if (!buffer[0] && (*def == 'S')) {
				errno = EINVAL;
			}
		}
		if (*def == 'I') {
			*(int *)member = bb_strtou(buffer, NULL, 10);
		}
#if ENABLE_USE_BB_SHADOW
		if (*def == 'l') {
			long n = -1;
			if (buffer[0])
				n = bb_strtol(buffer, NULL, 10);
			*(long *)member = n;
		}
#endif
		if (*def == 'm') {
			char **members;
			int i = tokenize(buffer, ',');

			/* Store members[] after buffer's end.
			 * This is safe ONLY because there is a hack
			 * in parse_common() which allocates additional space
			 * at the end of malloced buffer!
			 */
			members = (char **)
				( ((intptr_t)S.tokenize_end + sizeof(members[0]))
				& -(intptr_t)sizeof(members[0])
				);
			((struct group *)result)->gr_mem = members;
			while (--i >= 0) {
				if (buffer[0]) {
					*members++ = buffer;
					// bb_error_msg("member[]='%s'", buffer);
				}
				buffer += strlen(buffer) + 1;
			}
			*members = NULL;
		}
		/* def "r" does nothing */

		def++;
		if ((unsigned char)*def <= (unsigned char)' ')
			break;
		buffer += strlen(buffer) + 1;
	}

	if (errno)
		result = NULL;
	return result;
}

static int massage_data_for_r_func(struct passdb *db,
		char *buffer, size_t buflen,
		void **result,
		char *buf)
{
	void *result_buf = *result;
	*result = NULL;
	if (buf) {
		if (S.string_size > buflen) {
			errno = ERANGE;
		} else {
			memcpy(buffer, buf, S.string_size);
			*result = convert_to_struct(db, buffer, result_buf);
		}
		free(buf);
	}
	/* "The reentrant functions return zero on success.
	 * In case of error, an error number is returned."
	 * NB: not finding the record is also a "success" here:
	 */
	return errno;
}

static void* massage_data_for_non_r_func(struct passdb *db, char *buf)
{
	if (!buf)
		return NULL;

	free(db->malloced);
	/* We enlarge buf and move string data up, freeing space
	 * for struct passwd/group/spwd at the beginning. This way,
	 * entire result of getXXnam is in a single malloced block.
	 * This enables easy creation of xmalloc_getpwnam() API.
	 */
	db->malloced = buf = xrealloc(buf, db->size_of + S.string_size);
	memmove(buf + db->size_of, buf, S.string_size);
	return convert_to_struct(db, buf + db->size_of, buf);
}

/****** getXXnam/id_r */

static int FAST_FUNC getXXnam_r(const char *name, uintptr_t db_and_field_pos,
		char *buffer, size_t buflen,
		void *result)
{
	char *buf;
	struct passdb *db = &get_S()->db[db_and_field_pos >> 2];

	buf = parse_file(db, name, 0 /*db_and_field_pos & 3*/);
	/* "db_and_field_pos & 3" is commented out since so far we don't implement
	 * getXXXid_r() functions which would use that to pass 2 here */

	return massage_data_for_r_func(db, buffer, buflen, result, buf);
}

int FAST_FUNC getpwnam_r(const char *name, struct passwd *struct_buf,
		char *buffer, size_t buflen,
		struct passwd **result)
{
	/* Why the "store buffer address in result" trick?
	 * This way, getXXnam_r has the same ABI signature as getpwnam_r,
	 * hopefully compiler can optimize tail call better in this case.
	 */
	*result = struct_buf;
	return getXXnam_r(name, (0 << 2) + 0, buffer, buflen, result);
}
#if ENABLE_USE_BB_SHADOW
int FAST_FUNC getspnam_r(const char *name, struct spwd *struct_buf, char *buffer, size_t buflen,
		struct spwd **result)
{
	*result = struct_buf;
	return getXXnam_r(name, (2 << 2) + 0, buffer, buflen, result);
}
#endif

#ifdef UNUSED
/****** getXXent_r */

static int FAST_FUNC getXXent_r(uintptr_t db_idx, char *buffer, size_t buflen,
		void *result)
{
	char *buf;
	struct passdb *db = &get_S()->db[db_idx];

	if (!db->fp) {
		db->fp = fopen_for_read(db->filename);
		if (!db->fp) {
			return errno;
		}
		close_on_exec_on(fileno(db->fp));
	}

	buf = parse_common(db->fp, db, /*no search key:*/ NULL, -1);
	if (!buf && !errno)
		errno = ENOENT;
	return massage_data_for_r_func(db, buffer, buflen, result, buf);
}

int FAST_FUNC getpwent_r(struct passwd *struct_buf, char *buffer, size_t buflen,
		struct passwd **result)
{
	*result = struct_buf;
	return getXXent_r(0, buffer, buflen, result);
}
#endif

/****** getXXent */

static void* FAST_FUNC getXXent(uintptr_t db_idx)
{
	char *buf;
	struct passdb *db = &get_S()->db[db_idx];

	if (!db->fp) {
		db->fp = fopen_for_read(db->filename);
		if (!db->fp) {
			return NULL;
		}
		close_on_exec_on(fileno(db->fp));
	}

	buf = parse_common(db->fp, db, /*no search key:*/ NULL, -1);
	return massage_data_for_non_r_func(db, buf);
}

struct passwd* FAST_FUNC getpwent(void)
{
	return getXXent(0);
}

/****** getXXnam/id */

static void* FAST_FUNC getXXnam(const char *name, unsigned db_and_field_pos)
{
	char *buf;
	struct passdb *db = &get_S()->db[db_and_field_pos >> 2];

	buf = parse_file(db, name, db_and_field_pos & 3);
	return massage_data_for_non_r_func(db, buf);
}

struct passwd* FAST_FUNC getpwnam(const char *name)
{
	return getXXnam(name, (0 << 2) + 0);
}
struct group* FAST_FUNC getgrnam(const char *name)
{
	return getXXnam(name, (1 << 2) + 0);
}
struct passwd* FAST_FUNC getpwuid(uid_t id)
{
	return getXXnam(utoa(id), (0 << 2) + 2);
}
struct group* FAST_FUNC getgrgid(gid_t id)
{
	return getXXnam(utoa(id), (1 << 2) + 2);
}

/****** end/setXXend */

void FAST_FUNC endpwent(void)
{
	if (has_S && S.db[0].fp) {
		fclose(S.db[0].fp);
		S.db[0].fp = NULL;
	}
}
void FAST_FUNC setpwent(void)
{
	if (has_S && S.db[0].fp) {
		rewind(S.db[0].fp);
	}
}
void FAST_FUNC endgrent(void)
{
	if (has_S && S.db[1].fp) {
		fclose(S.db[1].fp);
		S.db[1].fp = NULL;
	}
}

/****** initgroups and getgrouplist */

static gid_t* FAST_FUNC getgrouplist_internal(int *ngroups_ptr,
		const char *user, gid_t gid)
{
	FILE *fp;
	gid_t *group_list;
	int ngroups;

	/* We alloc space for 8 gids at a time. */
	group_list = xzalloc(8 * sizeof(group_list[0]));
	group_list[0] = gid;
	ngroups = 1;

	fp = fopen_for_read(_PATH_GROUP);
	if (fp) {
		struct passdb *db = &get_S()->db[1];
		char *buf;
		while ((buf = parse_common(fp, db, NULL, -1)) != NULL) {
			char **m;
			struct group group;
			if (!convert_to_struct(db, buf, &group))
				goto next;
			if (group.gr_gid == gid)
				goto next;
			for (m = group.gr_mem; *m; m++) {
				if (strcmp(*m, user) != 0)
					continue;
				group_list = xrealloc_vector(group_list, /*8=2^3:*/ 3, ngroups);
				group_list[ngroups++] = group.gr_gid;
				goto next;
			}
 next:
			free(buf);
		}
		fclose(fp);
	}
	*ngroups_ptr = ngroups;
	return group_list;
}

int FAST_FUNC initgroups(const char *user, gid_t gid)
{
	int ngroups;
	gid_t *group_list = getgrouplist_internal(&ngroups, user, gid);

	ngroups = setgroups(ngroups, group_list);
	free(group_list);
	return ngroups;
}

int FAST_FUNC getgrouplist(const char *user, gid_t gid, gid_t *groups, int *ngroups)
{
	int ngroups_old = *ngroups;
	gid_t *group_list = getgrouplist_internal(ngroups, user, gid);

	if (*ngroups <= ngroups_old) {
		ngroups_old = *ngroups;
		memcpy(groups, group_list, ngroups_old * sizeof(groups[0]));
	} else {
		ngroups_old = -1;
	}
	free(group_list);
	return ngroups_old;
}

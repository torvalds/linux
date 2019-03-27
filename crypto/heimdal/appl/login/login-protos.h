/* This is a generated file */
#ifndef __login_protos_h__
#define __login_protos_h__

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void
add_env (
	const char */*var*/,
	const char */*value*/);

void
check_shadow (
	const struct passwd */*pw*/,
	const struct spwd */*sp*/);

char *
clean_ttyname (char */*tty*/);

void
copy_env (void);

int
do_osfc2_magic (uid_t /*uid*/);

void
extend_env (char */*str*/);

int
login_access (
	struct passwd */*user*/,
	char */*from*/);

char *
login_conf_get_string (const char */*str*/);

void
login_read_env (const char */*file*/);

char *
make_id (char */*tty*/);

void
prepare_utmp (
	struct utmp */*utmp*/,
	char */*tty*/,
	const char */*username*/,
	const char */*hostname*/);

int
read_limits_conf (
	const char */*file*/,
	const struct passwd */*pwd*/);

int
read_string (
	const char */*prompt*/,
	char */*buf*/,
	size_t /*len*/,
	int /*echo*/);

void
shrink_hostname (
	const char */*hostname*/,
	char */*dst*/,
	size_t /*dst_sz*/);

void
stty_default (void);

void
utmp_login (
	char */*tty*/,
	const char */*username*/,
	const char */*hostname*/);

int
utmpx_login (
	char */*line*/,
	const char */*user*/,
	const char */*host*/);

#ifdef __cplusplus
}
#endif

#endif /* __login_protos_h__ */

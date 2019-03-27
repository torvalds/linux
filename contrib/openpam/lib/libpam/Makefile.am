# $OpenPAM: Makefile.am 938 2017-04-30 21:34:42Z des $

NULL =

AM_CPPFLAGS = -I$(top_srcdir)/include

lib_LTLIBRARIES = libpam.la

noinst_HEADERS = \
	openpam_asprintf.h \
	openpam_constants.h \
	openpam_cred.h \
	openpam_ctype.h \
	openpam_debug.h \
	openpam_dlfunc.h \
	openpam_features.h \
	openpam_impl.h \
	openpam_strlcat.h \
	openpam_strlcmp.h \
	openpam_strlcpy.h \
	openpam_strlset.h \
	openpam_vasprintf.h

libpam_la_SOURCES = \
	openpam_asprintf.c \
	openpam_borrow_cred.c \
	openpam_check_owner_perms.c \
	openpam_configure.c \
	openpam_constants.c \
	openpam_dispatch.c \
	openpam_dynamic.c \
	openpam_features.c \
	openpam_findenv.c \
	openpam_free_data.c \
	openpam_free_envlist.c \
	openpam_get_feature.c \
	openpam_get_option.c \
	openpam_load.c \
	openpam_log.c \
	openpam_nullconv.c \
	openpam_readline.c \
	openpam_readlinev.c \
	openpam_readword.c \
	openpam_restore_cred.c \
	openpam_set_option.c \
	openpam_set_feature.c \
	openpam_static.c \
	openpam_straddch.c \
	openpam_strlcat.c \
	openpam_strlcpy.c \
	openpam_strlset.c \
	openpam_subst.c \
	openpam_vasprintf.c \
	openpam_ttyconv.c \
	pam_acct_mgmt.c \
	pam_authenticate.c \
	pam_chauthtok.c \
	pam_close_session.c \
	pam_end.c \
	pam_error.c \
	pam_get_authtok.c \
	pam_get_data.c \
	pam_get_item.c \
	pam_get_user.c \
	pam_getenv.c \
	pam_getenvlist.c \
	pam_info.c \
	pam_open_session.c \
	pam_prompt.c \
	pam_putenv.c \
	pam_set_data.c \
	pam_set_item.c \
	pam_setcred.c \
	pam_setenv.c \
	pam_start.c \
	pam_strerror.c \
	pam_verror.c \
	pam_vinfo.c \
	pam_vprompt.c \
	$(NULL)

libpam_la_LDFLAGS = -no-undefined -version-info $(LIB_MAJ)
libpam_la_LIBADD = $(DL_LIBS)

EXTRA_DIST = \
	pam_authenticate_secondary.c \
	pam_get_mapped_authtok.c \
	pam_get_mapped_username.c \
	pam_set_mapped_authtok.c \
	pam_set_mapped_username.c \
	\
	pam_sm_acct_mgmt.c \
	pam_sm_authenticate.c \
	pam_sm_authenticate_secondary.c \
	pam_sm_chauthtok.c \
	pam_sm_close_session.c \
	pam_sm_get_mapped_authtok.c \
	pam_sm_get_mapped_username.c \
	pam_sm_open_session.c \
	pam_sm_set_mapped_authtok.c \
	pam_sm_set_mapped_username.c \
	pam_sm_setcred.c

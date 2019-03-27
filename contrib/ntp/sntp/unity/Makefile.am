#AUTOMAKE_OPTIONS = foreign 1.9 subdir-objects
NULL =
BUILT_SOURCES =
CLEANFILES =

AM_CFLAGS = $(NTP_HARD_CFLAGS)
AM_CPPFLAGS = $(NTP_HARD_CPPFLAGS)
AM_LDFLAGS = $(NTP_HARD_LDFLAGS)

noinst_LIBRARIES = libunity.a

libunity_a_CFLAGS =                  	\
	-DUNITY_INCLUDE_CONFIG_H	\
	$(AM_CFLAGS)			\
	$(NULL)

libunity_a_SOURCES =				\
	../libpkgver/colcomp.c			\
	unity.c					\
	ulib_setup.c				\
	ulib_teardown.c				\
	unity.h					\
	unity_config.h				\
	unity_internals.h			\
	unity_fixture.c				\
	unity_fixture.h				\
	unity_fixture_internals.h		\
	unity_fixture_malloc_overrides.h	\
	unity_config.h				\
	$(NULL)

include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf

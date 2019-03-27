EXTRA_DIST = $(TESTS) basic schema generate.res \
	streamline.res rcl_test.json.xz

TESTS = basic.test \
		generate.test \
		schema.test \
		msgpack.test \
		speed.test \
		msgpack.test
TESTS_ENVIRONMENT = $(SH) \
			TEST_DIR=$(top_srcdir)/tests \
			TEST_OUT_DIR=$(top_builddir)/tests \
			TEST_BINARY_DIR=$(top_builddir)/tests

common_test_cflags = -I$(top_srcdir)/include \
					-I$(top_srcdir)/src \
					-I$(top_srcdir)/uthash
common_test_ldadd = $(top_builddir)/src/libucl.la

test_basic_SOURCES = test_basic.c
test_basic_LDADD = $(common_test_ldadd)
test_basic_CFLAGS = $(common_test_cflags)

test_speed_SOURCES = test_speed.c
test_speed_LDADD = $(common_test_ldadd)
test_speed_CFLAGS = $(common_test_cflags)

test_generate_SOURCES = test_generate.c
test_generate_LDADD = $(common_test_ldadd)
test_generate_CFLAGS = $(common_test_cflags)

test_schema_SOURCES = test_schema.c
test_schema_LDADD = $(common_test_ldadd)
test_schema_CFLAGS = $(common_test_cflags)

test_streamline_SOURCES = test_streamline.c
test_streamline_LDADD = $(common_test_ldadd)
test_streamline_CFLAGS = $(common_test_cflags)

test_msgpack_SOURCES = test_msgpack.c
test_msgpack_LDADD = $(common_test_ldadd)
test_msgpack_CFLAGS = $(common_test_cflags)

check_PROGRAMS = test_basic test_speed test_generate test_schema test_streamline \
	test_msgpack
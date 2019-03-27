/* $FreeBSD$ */

/* Define as 1 if you want Dual Sided RMPP Support */
#define DUAL_SIDED_RMPP 1

/* Define as 1 if you want to enable a loopback console */
#define ENABLE_OSM_CONSOLE_LOOPBACK 1

/* Define as 1 if you want to enable a console on a socket connection */
/* #undef ENABLE_OSM_CONSOLE_SOCKET */

/* Define as 1 if you want to enable the event plugin */
/* #undef ENABLE_OSM_DEFAULT_EVENT_PLUGIN */

/* Define as 1 if you want to enable the performance manager */
#define ENABLE_OSM_PERF_MGR 1

/* Define as 1 if you want to enable the performance manager profiling code */
/* #undef ENABLE_OSM_PERF_MGR_PROFILE */

/* Define to 1 if the compiler supports __builtin_expect. */
#define HAVE_BUILTIN_EXPECT 1

/* Define a default node name map file */
#define HAVE_DEFAULT_NODENAME_MAP "/etc/opensm/ib-node-name-map"

/* Define a default OpenSM config file */
#define HAVE_DEFAULT_OPENSM_CONFIG_FILE "/etc/opensm/opensm.conf"

/* Define a Partition config file */
#define HAVE_DEFAULT_PARTITION_CONFIG_FILE "/etc/opensm/partitions.conf"

/* Define a Per Module Logging config file */
#define HAVE_DEFAULT_PER_MOD_LOGGING_FILE "/etc/opensm/per-module-logging.conf"

/* Define a Prefix Routes config file */
#define HAVE_DEFAULT_PREFIX_ROUTES_FILE "/etc/opensm/prefix-routes.conf"

/* Define a QOS policy config file */
#define HAVE_DEFAULT_QOS_POLICY_FILE "/etc/opensm/qos-policy.conf"

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `ibumad' library (-libumad). */
#define HAVE_LIBIBUMAD 1

/* Define to 1 if you have the `pthread' library (-lpthread). */
#define HAVE_LIBPTHREAD 1

/* Define to 1 if you have the `vapi' library (-lvapi). */
/* #undef HAVE_LIBVAPI */

/* Define to 1 if you have the `wrap' library (-lwrap). */
#define HAVE_LIBWRAP 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define OpenSM config directory */
#define OPENSM_CONFIG_DIR "/etc/opensm"

/* define 1 if OpenSM build is in a debug mode */
/* #undef OSM_DEBUG */

/* Define as 1 for vapi vendor */
/* #undef OSM_VENDOR_INTF_MTL */

/* Define as 1 for OpenIB vendor */
#define OSM_VENDOR_INTF_OPENIB 1

/* Define as 1 for sim vendor */
/* #undef OSM_VENDOR_INTF_SIM */

/* Define as 1 for ts vendor */
/* #undef OSM_VENDOR_INTF_TS */

/* Name of package */
#define PACKAGE "opensm"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "linux-rdma@vger.kernel.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "opensm"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "opensm 3.3.20"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "opensm"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "3.3.20"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Define as 1 if you want Vendor RMPP Support */
#define VENDOR_RMPP_SUPPORT 1

/* Version number of package */
#define VERSION "3.3.20"

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* define 1 if OpenSM build is in a debug mode */
/* #undef _DEBUG_ */

/* mark config.h inclusion */
#define _OSM_CONFIG_H_ 1

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to empty if the keyword `volatile' does not work. Warning: valid
   code using `volatile' can become incorrect without. Disable with care. */
/* #undef volatile */

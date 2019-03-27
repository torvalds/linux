/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* apply the noreturn attribute to a function that exits the program */
#define ATTR_NORETURN __attribute__((__noreturn__))

/* apply the weak attribute to a symbol */
#define ATTR_WEAK __attribute__((weak))

/* Directory to chroot to */
#define CHROOT_DIR "/var/unbound"

/* Define this to enable client subnet option. */
/* #undef CLIENT_SUBNET */

/* Do sha512 definitions in config.h */
/* #undef COMPAT_SHA512 */

/* Pathname to the Unbound configuration file */
#define CONFIGFILE "/var/unbound/unbound.conf"

/* Define this if on macOSX10.4-darwin8 and setreuid and setregid do not work
   */
/* #undef DARWIN_BROKEN_SETREUID */

/* Whether daemon is deprecated */
/* #undef DEPRECATED_DAEMON */

/* default dnstap socket path */
/* #undef DNSTAP_SOCKET_PATH */

/* Define if you want to use debug lock checking (slow). */
/* #undef ENABLE_LOCK_CHECKS */

/* Define this if you enabled-allsymbols from libunbound to link binaries to
   it for smaller install size, but the libunbound export table is polluted by
   internal symbols */
/* #undef EXPORT_ALL_SYMBOLS */

/* Define to 1 if you have the `accept4' function. */
#define HAVE_ACCEPT4 1

/* Define to 1 if you have the `arc4random' function. */
#define HAVE_ARC4RANDOM 1

/* Define to 1 if you have the `arc4random_uniform' function. */
#define HAVE_ARC4RANDOM_UNIFORM 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Whether the C compiler accepts the "format" attribute */
#define HAVE_ATTR_FORMAT 1

/* Whether the C compiler accepts the "noreturn" attribute */
#define HAVE_ATTR_NORETURN 1

/* Whether the C compiler accepts the "unused" attribute */
#define HAVE_ATTR_UNUSED 1

/* Whether the C compiler accepts the "weak" attribute */
#define HAVE_ATTR_WEAK 1

/* Define to 1 if you have the `chown' function. */
#define HAVE_CHOWN 1

/* Define to 1 if you have the `chroot' function. */
#define HAVE_CHROOT 1

/* Define to 1 if you have the `CRYPTO_cleanup_all_ex_data' function. */
/* #undef HAVE_CRYPTO_CLEANUP_ALL_EX_DATA */

/* Define to 1 if you have the `ctime_r' function. */
#define HAVE_CTIME_R 1

/* Define to 1 if you have the `daemon' function. */
#define HAVE_DAEMON 1

/* Define to 1 if you have the declaration of `arc4random', and to 0 if you
   don't. */
/* #undef HAVE_DECL_ARC4RANDOM */

/* Define to 1 if you have the declaration of `arc4random_uniform', and to 0
   if you don't. */
/* #undef HAVE_DECL_ARC4RANDOM_UNIFORM */

/* Define to 1 if you have the declaration of `inet_ntop', and to 0 if you
   don't. */
#define HAVE_DECL_INET_NTOP 1

/* Define to 1 if you have the declaration of `inet_pton', and to 0 if you
   don't. */
#define HAVE_DECL_INET_PTON 1

/* Define to 1 if you have the declaration of `NID_ED25519', and to 0 if you
   don't. */
#define HAVE_DECL_NID_ED25519 1

/* Define to 1 if you have the declaration of `NID_ED448', and to 0 if you
   don't. */
#define HAVE_DECL_NID_ED448 1

/* Define to 1 if you have the declaration of `NID_secp384r1', and to 0 if you
   don't. */
#define HAVE_DECL_NID_SECP384R1 1

/* Define to 1 if you have the declaration of `NID_X9_62_prime256v1', and to 0
   if you don't. */
#define HAVE_DECL_NID_X9_62_PRIME256V1 1

/* Define to 1 if you have the declaration of `reallocarray', and to 0 if you
   don't. */
/* #undef HAVE_DECL_REALLOCARRAY */

/* Define to 1 if you have the declaration of `redisConnect', and to 0 if you
   don't. */
/* #undef HAVE_DECL_REDISCONNECT */

/* Define to 1 if you have the declaration of `sk_SSL_COMP_pop_free', and to 0
   if you don't. */
#define HAVE_DECL_SK_SSL_COMP_POP_FREE 1

/* Define to 1 if you have the declaration of
   `SSL_COMP_get_compression_methods', and to 0 if you don't. */
#define HAVE_DECL_SSL_COMP_GET_COMPRESSION_METHODS 1

/* Define to 1 if you have the declaration of `SSL_CTX_set_ecdh_auto', and to
   0 if you don't. */
#define HAVE_DECL_SSL_CTX_SET_ECDH_AUTO 1

/* Define to 1 if you have the declaration of `strlcat', and to 0 if you
   don't. */
/* #undef HAVE_DECL_STRLCAT */

/* Define to 1 if you have the declaration of `strlcpy', and to 0 if you
   don't. */
/* #undef HAVE_DECL_STRLCPY */

/* Define to 1 if you have the declaration of `XML_StopParser', and to 0 if
   you don't. */
#define HAVE_DECL_XML_STOPPARSER 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the `DSA_SIG_set0' function. */
#define HAVE_DSA_SIG_SET0 1

/* Define to 1 if you have the <endian.h> header file. */
/* #undef HAVE_ENDIAN_H */

/* Define to 1 if you have the `endprotoent' function. */
#define HAVE_ENDPROTOENT 1

/* Define to 1 if you have the `endpwent' function. */
#define HAVE_ENDPWENT 1

/* Define to 1 if you have the `endservent' function. */
#define HAVE_ENDSERVENT 1

/* Define to 1 if you have the `ERR_free_strings' function. */
/* #undef HAVE_ERR_FREE_STRINGS */

/* Define to 1 if you have the `ERR_load_crypto_strings' function. */
/* #undef HAVE_ERR_LOAD_CRYPTO_STRINGS */

/* Define to 1 if you have the `event_base_free' function. */
/* #undef HAVE_EVENT_BASE_FREE */

/* Define to 1 if you have the `event_base_get_method' function. */
/* #undef HAVE_EVENT_BASE_GET_METHOD */

/* Define to 1 if you have the `event_base_new' function. */
/* #undef HAVE_EVENT_BASE_NEW */

/* Define to 1 if you have the `event_base_once' function. */
/* #undef HAVE_EVENT_BASE_ONCE */

/* Define to 1 if you have the <event.h> header file. */
/* #undef HAVE_EVENT_H */

/* Define to 1 if you have the `EVP_cleanup' function. */
/* #undef HAVE_EVP_CLEANUP */

/* Define to 1 if you have the `EVP_DigestVerify' function. */
#define HAVE_EVP_DIGESTVERIFY 1

/* Define to 1 if you have the `EVP_dss1' function. */
/* #undef HAVE_EVP_DSS1 */

/* Define to 1 if you have the `EVP_MD_CTX_new' function. */
#define HAVE_EVP_MD_CTX_NEW 1

/* Define to 1 if you have the `EVP_sha1' function. */
#define HAVE_EVP_SHA1 1

/* Define to 1 if you have the `EVP_sha256' function. */
#define HAVE_EVP_SHA256 1

/* Define to 1 if you have the `EVP_sha512' function. */
#define HAVE_EVP_SHA512 1

/* Define to 1 if you have the `ev_default_loop' function. */
/* #undef HAVE_EV_DEFAULT_LOOP */

/* Define to 1 if you have the `ev_loop' function. */
/* #undef HAVE_EV_LOOP */

/* Define to 1 if you have the <expat.h> header file. */
#define HAVE_EXPAT_H 1

/* Define to 1 if you have the `explicit_bzero' function. */
#define HAVE_EXPLICIT_BZERO 1

/* Define to 1 if you have the `fcntl' function. */
#define HAVE_FCNTL 1

/* Define to 1 if you have the `FIPS_mode' function. */
#define HAVE_FIPS_MODE 1

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Define to 1 if fseeko (and presumably ftello) exists and is declared. */
#define HAVE_FSEEKO 1

/* Define to 1 if you have the `fsync' function. */
#define HAVE_FSYNC 1

/* Whether getaddrinfo is available */
#define HAVE_GETADDRINFO 1

/* Define to 1 if you have the `getauxval' function. */
/* #undef HAVE_GETAUXVAL */

/* Define to 1 if you have the `getentropy' function. */
/* #undef HAVE_GETENTROPY */

/* Define to 1 if you have the <getopt.h> header file. */
#define HAVE_GETOPT_H 1

/* Define to 1 if you have the `getpwnam' function. */
#define HAVE_GETPWNAM 1

/* Define to 1 if you have the `getrlimit' function. */
#define HAVE_GETRLIMIT 1

/* Define to 1 if you have the `glob' function. */
#define HAVE_GLOB 1

/* Define to 1 if you have the <glob.h> header file. */
#define HAVE_GLOB_H 1

/* Define to 1 if you have the `gmtime_r' function. */
#define HAVE_GMTIME_R 1

/* Define to 1 if you have the <grp.h> header file. */
#define HAVE_GRP_H 1

/* Define to 1 if you have the <hiredis/hiredis.h> header file. */
/* #undef HAVE_HIREDIS_HIREDIS_H */

/* If you have HMAC_Update */
#define HAVE_HMAC_UPDATE 1

/* Define to 1 if you have the `inet_aton' function. */
#define HAVE_INET_ATON 1

/* Define to 1 if you have the `inet_ntop' function. */
#define HAVE_INET_NTOP 1

/* Define to 1 if you have the `inet_pton' function. */
#define HAVE_INET_PTON 1

/* Define to 1 if you have the `initgroups' function. */
#define HAVE_INITGROUPS 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* if the function 'ioctlsocket' is available */
/* #undef HAVE_IOCTLSOCKET */

/* Define to 1 if you have the <iphlpapi.h> header file. */
/* #undef HAVE_IPHLPAPI_H */

/* Define to 1 if you have the `isblank' function. */
#define HAVE_ISBLANK 1

/* Define to 1 if you have the `kill' function. */
#define HAVE_KILL 1

/* Define to 1 if you have the <libkern/OSByteOrder.h> header file. */
/* #undef HAVE_LIBKERN_OSBYTEORDER_H */

/* Define if we have LibreSSL */
/* #undef HAVE_LIBRESSL */

/* Define to 1 if you have the `localtime_r' function. */
#define HAVE_LOCALTIME_R 1

/* Define to 1 if you have the <login_cap.h> header file. */
#define HAVE_LOGIN_CAP_H 1

/* If have GNU libc compatible malloc */
#define HAVE_MALLOC 1

/* Define to 1 if you have the `memmove' function. */
#define HAVE_MEMMOVE 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* Use libnettle for crypto */
/* #undef HAVE_NETTLE */

/* Define to 1 if you have the <nettle/dsa-compat.h> header file. */
/* #undef HAVE_NETTLE_DSA_COMPAT_H */

/* Define to 1 if you have the <nettle/eddsa.h> header file. */
/* #undef HAVE_NETTLE_EDDSA_H */

/* Use libnss for crypto */
/* #undef HAVE_NSS */

/* Define to 1 if you have the `OpenSSL_add_all_digests' function. */
/* #undef HAVE_OPENSSL_ADD_ALL_DIGESTS */

/* Define to 1 if you have the <openssl/bn.h> header file. */
#define HAVE_OPENSSL_BN_H 1

/* Define to 1 if you have the `OPENSSL_config' function. */
#define HAVE_OPENSSL_CONFIG 1

/* Define to 1 if you have the <openssl/conf.h> header file. */
#define HAVE_OPENSSL_CONF_H 1

/* Define to 1 if you have the <openssl/dh.h> header file. */
#define HAVE_OPENSSL_DH_H 1

/* Define to 1 if you have the <openssl/dsa.h> header file. */
#define HAVE_OPENSSL_DSA_H 1

/* Define to 1 if you have the <openssl/engine.h> header file. */
#define HAVE_OPENSSL_ENGINE_H 1

/* Define to 1 if you have the <openssl/err.h> header file. */
#define HAVE_OPENSSL_ERR_H 1

/* Define to 1 if you have the `OPENSSL_init_crypto' function. */
#define HAVE_OPENSSL_INIT_CRYPTO 1

/* Define to 1 if you have the `OPENSSL_init_ssl' function. */
#define HAVE_OPENSSL_INIT_SSL 1

/* Define to 1 if you have the <openssl/rand.h> header file. */
#define HAVE_OPENSSL_RAND_H 1

/* Define to 1 if you have the <openssl/rsa.h> header file. */
#define HAVE_OPENSSL_RSA_H 1

/* Define to 1 if you have the <openssl/ssl.h> header file. */
#define HAVE_OPENSSL_SSL_H 1

/* Define if you have POSIX threads libraries and header files. */
#define HAVE_PTHREAD 1

/* Have PTHREAD_PRIO_INHERIT. */
#define HAVE_PTHREAD_PRIO_INHERIT 1

/* Define to 1 if the system has the type `pthread_rwlock_t'. */
#define HAVE_PTHREAD_RWLOCK_T 1

/* Define to 1 if the system has the type `pthread_spinlock_t'. */
#define HAVE_PTHREAD_SPINLOCK_T 1

/* Define to 1 if you have the <pwd.h> header file. */
#define HAVE_PWD_H 1

/* Define if you have Python libraries and header files. */
/* #undef HAVE_PYTHON */

/* Define to 1 if you have the `random' function. */
#define HAVE_RANDOM 1

/* Define to 1 if you have the `RAND_cleanup' function. */
/* #undef HAVE_RAND_CLEANUP */

/* Define to 1 if you have the `reallocarray' function. */
#define HAVE_REALLOCARRAY 1

/* Define to 1 if you have the `recvmsg' function. */
#define HAVE_RECVMSG 1

/* Define to 1 if you have the `sendmsg' function. */
#define HAVE_SENDMSG 1

/* Define to 1 if you have the `setregid' function. */
/* #undef HAVE_SETREGID */

/* Define to 1 if you have the `setresgid' function. */
#define HAVE_SETRESGID 1

/* Define to 1 if you have the `setresuid' function. */
#define HAVE_SETRESUID 1

/* Define to 1 if you have the `setreuid' function. */
/* #undef HAVE_SETREUID */

/* Define to 1 if you have the `setrlimit' function. */
#define HAVE_SETRLIMIT 1

/* Define to 1 if you have the `setsid' function. */
#define HAVE_SETSID 1

/* Define to 1 if you have the `setusercontext' function. */
#define HAVE_SETUSERCONTEXT 1

/* Define to 1 if you have the `SHA512_Update' function. */
/* #undef HAVE_SHA512_UPDATE */

/* Define to 1 if you have the `shmget' function. */
#define HAVE_SHMGET 1

/* Define to 1 if you have the `sigprocmask' function. */
#define HAVE_SIGPROCMASK 1

/* Define to 1 if you have the `sleep' function. */
#define HAVE_SLEEP 1

/* Define to 1 if you have the `snprintf' function. */
#define HAVE_SNPRINTF 1

/* Define to 1 if you have the `socketpair' function. */
#define HAVE_SOCKETPAIR 1

/* Using Solaris threads */
/* #undef HAVE_SOLARIS_THREADS */

/* Define to 1 if you have the `srandom' function. */
#define HAVE_SRANDOM 1

/* Define if you have the SSL libraries installed. */
#define HAVE_SSL /**/

/* Define to 1 if you have the `SSL_CTX_set_security_level' function. */
#define HAVE_SSL_CTX_SET_SECURITY_LEVEL 1

/* Define to 1 if you have the `SSL_get0_peername' function. */
#define HAVE_SSL_GET0_PEERNAME 1

/* Define to 1 if you have the `SSL_set1_host' function. */
#define HAVE_SSL_SET1_HOST 1

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stdbool.h> header file. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strftime' function. */
#define HAVE_STRFTIME 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcat' function. */
#define HAVE_STRLCAT 1

/* Define to 1 if you have the `strlcpy' function. */
#define HAVE_STRLCPY 1

/* Define to 1 if you have the `strptime' function. */
#define HAVE_STRPTIME 1

/* Define to 1 if you have the `strsep' function. */
#define HAVE_STRSEP 1

/* Define to 1 if `ipi_spec_dst' is a member of `struct in_pktinfo'. */
/* #undef HAVE_STRUCT_IN_PKTINFO_IPI_SPEC_DST */

/* Define to 1 if `sun_len' is a member of `struct sockaddr_un'. */
#define HAVE_STRUCT_SOCKADDR_UN_SUN_LEN 1

/* Define if you have Swig libraries and header files. */
/* #undef HAVE_SWIG */

/* Define to 1 if you have the <syslog.h> header file. */
#define HAVE_SYSLOG_H 1

/* Define to 1 if systemd should be used */
/* #undef HAVE_SYSTEMD */

/* Define to 1 if you have the <sys/endian.h> header file. */
#define HAVE_SYS_ENDIAN_H 1

/* Define to 1 if you have the <sys/ipc.h> header file. */
#define HAVE_SYS_IPC_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/resource.h> header file. */
#define HAVE_SYS_RESOURCE_H 1

/* Define to 1 if you have the <sys/sha2.h> header file. */
/* #undef HAVE_SYS_SHA2_H */

/* Define to 1 if you have the <sys/shm.h> header file. */
#define HAVE_SYS_SHM_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/sysctl.h> header file. */
/* #undef HAVE_SYS_SYSCTL_H */

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <sys/un.h> header file. */
#define HAVE_SYS_UN_H 1

/* Define to 1 if you have the <sys/wait.h> header file. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the `tzset' function. */
#define HAVE_TZSET 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `usleep' function. */
#define HAVE_USLEEP 1

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the <windows.h> header file. */
/* #undef HAVE_WINDOWS_H */

/* Using Windows threads */
/* #undef HAVE_WINDOWS_THREADS */

/* Define to 1 if you have the <winsock2.h> header file. */
/* #undef HAVE_WINSOCK2_H */

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* Define to 1 if you have the `writev' function. */
#define HAVE_WRITEV 1

/* Define to 1 if you have the <ws2tcpip.h> header file. */
/* #undef HAVE_WS2TCPIP_H */

/* Define to 1 if you have the `_beginthreadex' function. */
/* #undef HAVE__BEGINTHREADEX */

/* if lex has yylex_destroy */
#define LEX_HAS_YYLEX_DESTROY 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Define to the maximum message length to pass to syslog. */
#define MAXSYSLOGMSGLEN 10240

/* Define if memcmp() does not compare unsigned bytes */
/* #undef MEMCMP_IS_BROKEN */

/* Define if mkdir has one argument. */
/* #undef MKDIR_HAS_ONE_ARG */

/* Define if the network stack does not fully support nonblocking io (causes
   lower performance). */
/* #undef NONBLOCKING_IS_BROKEN */

/* Put -D_ALL_SOURCE define in config.h */
/* #undef OMITTED__D_ALL_SOURCE */

/* Put -D_BSD_SOURCE define in config.h */
/* #undef OMITTED__D_BSD_SOURCE */

/* Put -D_DEFAULT_SOURCE define in config.h */
/* #undef OMITTED__D_DEFAULT_SOURCE */

/* Put -D_GNU_SOURCE define in config.h */
/* #undef OMITTED__D_GNU_SOURCE */

/* Put -D_LARGEFILE_SOURCE=1 define in config.h */
/* #undef OMITTED__D_LARGEFILE_SOURCE_1 */

/* Put -D_POSIX_C_SOURCE=200112 define in config.h */
/* #undef OMITTED__D_POSIX_C_SOURCE_200112 */

/* Put -D_XOPEN_SOURCE=600 define in config.h */
/* #undef OMITTED__D_XOPEN_SOURCE_600 */

/* Put -D_XOPEN_SOURCE_EXTENDED=1 define in config.h */
/* #undef OMITTED__D_XOPEN_SOURCE_EXTENDED_1 */

/* Put -D__EXTENSIONS__ define in config.h */
/* #undef OMITTED__D__EXTENSIONS__ */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "unbound-bugs@nlnetlabs.nl"

/* Define to the full name of this package. */
#define PACKAGE_NAME "unbound"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "unbound 1.8.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "unbound"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.8.1"

/* default pidfile location */
#define PIDFILE "/var/unbound/unbound.pid"

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* if REUSEPORT is enabled by default */
#define REUSEPORT_DEFAULT 0

/* default rootkey location */
#define ROOT_ANCHOR_FILE "/var/unbound/root.key"

/* default rootcert location */
#define ROOT_CERT_FILE "/var/unbound/icannbundle.pem"

/* version number for resource files */
#define RSRC_PACKAGE_VERSION 1,8,1,0

/* Directory to chdir to */
#define RUN_DIR "/var/unbound"

/* Shared data */
#define SHARE_DIR "/var/unbound"

/* The size of `time_t', as computed by sizeof. */
#define SIZEOF_TIME_T 8

/* define if (v)snprintf does not return length needed, (but length used) */
/* #undef SNPRINTF_RET_BROKEN */

/* Define to 1 if libsodium supports sodium_set_misuse_handler */
/* #undef SODIUM_MISUSE_HANDLER */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* use default strptime. */
#define STRPTIME_WORKS 1

/* Use win32 resources and API */
/* #undef UB_ON_WINDOWS */

/* default username */
#define UB_USERNAME "unbound"

/* use to enable lightweight alloc assertions, for debug use */
/* #undef UNBOUND_ALLOC_LITE */

/* use malloc not regions, for debug use */
/* #undef UNBOUND_ALLOC_NONREGIONAL */

/* use statistics for allocs and frees, for debug use */
/* #undef UNBOUND_ALLOC_STATS */

/* define this to enable debug checks. */
/* #undef UNBOUND_DEBUG */

/* Define to 1 to use cachedb support */
/* #undef USE_CACHEDB */

/* Define to 1 to enable dnscrypt support */
/* #undef USE_DNSCRYPT */

/* Define to 1 to enable dnscrypt with xchacha20 support */
/* #undef USE_DNSCRYPT_XCHACHA20 */

/* Define to 1 to enable dnstap support */
/* #undef USE_DNSTAP */

/* Define this to enable DSA support. */
#define USE_DSA 1

/* Define this to enable ECDSA support. */
#define USE_ECDSA 1

/* Define this to enable an EVP workaround for older openssl */
/* #undef USE_ECDSA_EVP_WORKAROUND */

/* Define this to enable ED25519 support. */
#define USE_ED25519 1

/* Define this to enable ED448 support. */
#define USE_ED448 1

/* Define this to enable GOST support. */
/* #undef USE_GOST */

/* Define to 1 to use ipsecmod support. */
/* #undef USE_IPSECMOD */

/* Define if you want to use internal select based events */
#define USE_MINI_EVENT 1

/* Define this to enable client TCP Fast Open. */
/* #undef USE_MSG_FASTOPEN */

/* Define this to enable client TCP Fast Open. */
/* #undef USE_OSX_MSG_FASTOPEN */

/* Define this to use hiredis client. */
/* #undef USE_REDIS */

/* Define this to enable SHA1 support. */
#define USE_SHA1 1

/* Define this to enable SHA256 and SHA512 support. */
#define USE_SHA2 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Define this to enable server TCP Fast Open. */
/* #undef USE_TCP_FASTOPEN */

/* Whether the windows socket API is used */
/* #undef USE_WINSOCK */

/* the version of the windows API enabled */
#define WINVER 0x0502

/* Define if you want Python module. */
/* #undef WITH_PYTHONMODULE */

/* Define if you want PyUnbound. */
/* #undef WITH_PYUNBOUND */

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define to 1 to make fseeko visible on some hosts (e.g. glibc 2.2). */
/* #undef _LARGEFILE_SOURCE */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Enable for compile on Minix */
/* #undef _NETBSD_SOURCE */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef gid_t */

/* in_addr_t */
/* #undef in_addr_t */

/* in_port_t */
/* #undef in_port_t */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to `short' if <sys/types.h> does not define. */
/* #undef int16_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef int32_t */

/* Define to `long long' if <sys/types.h> does not define. */
/* #undef int64_t */

/* Define to `signed char' if <sys/types.h> does not define. */
/* #undef int8_t */

/* Define if replacement function should be used. */
/* #undef malloc */

/* Define to `long int' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to 'int' if not defined */
/* #undef rlim_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to 'int' if not defined */
/* #undef socklen_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef ssize_t */

/* Define to 'unsigned char if not defined */
/* #undef u_char */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef uid_t */

/* Define to `unsigned short' if <sys/types.h> does not define. */
/* #undef uint16_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef uint32_t */

/* Define to `unsigned long long' if <sys/types.h> does not define. */
/* #undef uint64_t */

/* Define to `unsigned char' if <sys/types.h> does not define. */
/* #undef uint8_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */

#if defined(OMITTED__D_GNU_SOURCE) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif 

#if defined(OMITTED__D_BSD_SOURCE) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif 

#if defined(OMITTED__D_DEFAULT_SOURCE) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif 

#if defined(OMITTED__D__EXTENSIONS__) && !defined(__EXTENSIONS__)
#define __EXTENSIONS__ 1
#endif 

#if defined(OMITTED__D_POSIX_C_SOURCE_200112) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112
#endif 

#if defined(OMITTED__D_XOPEN_SOURCE_600) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif 

#if defined(OMITTED__D_XOPEN_SOURCE_EXTENDED_1) && !defined(_XOPEN_SOURCE_EXTENDED)
#define _XOPEN_SOURCE_EXTENDED 1
#endif 

#if defined(OMITTED__D_ALL_SOURCE) && !defined(_ALL_SOURCE)
#define _ALL_SOURCE 1
#endif 

#if defined(OMITTED__D_LARGEFILE_SOURCE_1) && !defined(_LARGEFILE_SOURCE)
#define _LARGEFILE_SOURCE 1
#endif 




#ifndef UNBOUND_DEBUG
#  define NDEBUG
#endif

/** Use small-ldns codebase */
#define USE_SLDNS 1
#ifdef HAVE_SSL
#  define LDNS_BUILD_CONFIG_HAVE_SSL 1
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <errno.h>

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#ifndef USE_WINSOCK
#define ARG_LL "%ll"
#else
#define ARG_LL "%I64"
#endif

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif


 
#ifdef HAVE_ATTR_FORMAT
#  define ATTR_FORMAT(archetype, string_index, first_to_check) \
    __attribute__ ((format (archetype, string_index, first_to_check)))
#else /* !HAVE_ATTR_FORMAT */
#  define ATTR_FORMAT(archetype, string_index, first_to_check) /* empty */
#endif /* !HAVE_ATTR_FORMAT */


#if defined(DOXYGEN)
#  define ATTR_UNUSED(x)  x
#elif defined(__cplusplus)
#  define ATTR_UNUSED(x)
#elif defined(HAVE_ATTR_UNUSED)
#  define ATTR_UNUSED(x)  x __attribute__((unused))
#else /* !HAVE_ATTR_UNUSED */
#  define ATTR_UNUSED(x)  x
#endif /* !HAVE_ATTR_UNUSED */


#ifndef HAVE_FSEEKO
#define fseeko fseek
#define ftello ftell
#endif /* HAVE_FSEEKO */


#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#if !defined(HAVE_SNPRINTF) || defined(SNPRINTF_RET_BROKEN)
#define snprintf snprintf_unbound
#define vsnprintf vsnprintf_unbound
#include <stdarg.h>
int snprintf (char *str, size_t count, const char *fmt, ...);
int vsnprintf (char *str, size_t count, const char *fmt, va_list arg);
#endif /* HAVE_SNPRINTF or SNPRINTF_RET_BROKEN */

#ifndef HAVE_INET_PTON
#define inet_pton inet_pton_unbound
int inet_pton(int af, const char* src, void* dst);
#endif /* HAVE_INET_PTON */


#ifndef HAVE_INET_NTOP
#define inet_ntop inet_ntop_unbound
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif


#ifndef HAVE_INET_ATON
#define inet_aton inet_aton_unbound
int inet_aton(const char *cp, struct in_addr *addr);
#endif


#ifndef HAVE_MEMMOVE
#define memmove memmove_unbound
void *memmove(void *dest, const void *src, size_t n);
#endif


#ifndef HAVE_STRLCAT
#define strlcat strlcat_unbound
size_t strlcat(char *dst, const char *src, size_t siz);
#endif


#ifndef HAVE_STRLCPY
#define strlcpy strlcpy_unbound
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif


#ifndef HAVE_GMTIME_R
#define gmtime_r gmtime_r_unbound
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#endif


#ifndef HAVE_REALLOCARRAY
#define reallocarray reallocarrayunbound
void* reallocarray(void *ptr, size_t nmemb, size_t size);
#endif


#if !defined(HAVE_SLEEP) || defined(HAVE_WINDOWS_H)
#define sleep(x) Sleep((x)*1000) /* on win32 */
#endif /* HAVE_SLEEP */


#ifndef HAVE_USLEEP
#define usleep(x) Sleep((x)/1000 + 1) /* on win32 */
#endif /* HAVE_USLEEP */


#ifndef HAVE_RANDOM
#define random rand /* on win32, for tests only (bad random) */
#endif /* HAVE_RANDOM */


#ifndef HAVE_SRANDOM
#define srandom(x) srand(x) /* on win32, for tests only (bad random) */
#endif /* HAVE_SRANDOM */


/* detect if we need to cast to unsigned int for FD_SET to avoid warnings */
#ifdef HAVE_WINSOCK2_H
#define FD_SET_T (u_int)
#else
#define FD_SET_T 
#endif


#ifndef IPV6_MIN_MTU
#define IPV6_MIN_MTU 1280
#endif /* IPV6_MIN_MTU */


#ifdef MEMCMP_IS_BROKEN
#include "compat/memcmp.h"
#define memcmp memcmp_unbound
int memcmp(const void *x, const void *y, size_t n);
#endif



#ifndef HAVE_CTIME_R
#define ctime_r unbound_ctime_r
char *ctime_r(const time_t *timep, char *buf);
#endif

#ifndef HAVE_STRSEP
#define strsep unbound_strsep
char *strsep(char **stringp, const char *delim);
#endif

#ifndef HAVE_ISBLANK
#define isblank unbound_isblank
int isblank(int c);
#endif

#ifndef HAVE_EXPLICIT_BZERO
#define explicit_bzero unbound_explicit_bzero
void explicit_bzero(void* buf, size_t len);
#endif

#if defined(HAVE_INET_NTOP) && !HAVE_DECL_INET_NTOP
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif

#if defined(HAVE_INET_PTON) && !HAVE_DECL_INET_PTON
int inet_pton(int af, const char* src, void* dst);
#endif

#if !defined(HAVE_STRPTIME) || !defined(STRPTIME_WORKS)
#define strptime unbound_strptime
struct tm;
char *strptime(const char *s, const char *format, struct tm *tm);
#endif

#ifdef HAVE_LIBRESSL
#  if !HAVE_DECL_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#  endif
#  if !HAVE_DECL_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
#  endif
#  if !HAVE_DECL_ARC4RANDOM && defined(HAVE_ARC4RANDOM)
uint32_t arc4random(void);
#  endif
#  if !HAVE_DECL_ARC4RANDOM_UNIFORM && defined(HAVE_ARC4RANDOM_UNIFORM)
uint32_t arc4random_uniform(uint32_t upper_bound);
#  endif
#  if !HAVE_DECL_REALLOCARRAY
void *reallocarray(void *ptr, size_t nmemb, size_t size);
#  endif
#endif /* HAVE_LIBRESSL */
#ifndef HAVE_ARC4RANDOM
int getentropy(void* buf, size_t len);
uint32_t arc4random(void);
void arc4random_buf(void* buf, size_t n);
void _ARC4_LOCK(void);
void _ARC4_UNLOCK(void);
void _ARC4_LOCK_DESTROY(void);
#endif
#ifndef HAVE_ARC4RANDOM_UNIFORM
uint32_t arc4random_uniform(uint32_t upper_bound);
#endif
#ifdef COMPAT_SHA512
#ifndef SHA512_DIGEST_LENGTH
#define SHA512_BLOCK_LENGTH		128
#define SHA512_DIGEST_LENGTH		64
#define SHA512_DIGEST_STRING_LENGTH	(SHA512_DIGEST_LENGTH * 2 + 1)
typedef struct _SHA512_CTX {
	uint64_t	state[8];
	uint64_t	bitcount[2];
	uint8_t	buffer[SHA512_BLOCK_LENGTH];
} SHA512_CTX;
#endif /* SHA512_DIGEST_LENGTH */
void SHA512_Init(SHA512_CTX*);
void SHA512_Update(SHA512_CTX*, void*, size_t);
void SHA512_Final(uint8_t[SHA512_DIGEST_LENGTH], SHA512_CTX*);
unsigned char *SHA512(void* data, unsigned int data_len, unsigned char *digest);
#endif /* COMPAT_SHA512 */



#if defined(HAVE_EVENT_H) && !defined(HAVE_EVENT_BASE_ONCE) && !(defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)) && (defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS))
   /* using version of libevent that is not threadsafe. */
#  define LIBEVENT_SIGNAL_PROBLEM 1
#endif

#ifndef CHECKED_INET6
#  define CHECKED_INET6
#  ifdef AF_INET6
#    define INET6
#  else
#    define AF_INET6        28
#  endif
#endif /* CHECKED_INET6 */

#ifndef HAVE_GETADDRINFO
struct sockaddr_storage;
#include "compat/fake-rfc2553.h"
#endif

#ifdef UNBOUND_ALLOC_STATS
#  define malloc(s) unbound_stat_malloc_log(s, __FILE__, __LINE__, __func__)
#  define calloc(n,s) unbound_stat_calloc_log(n, s, __FILE__, __LINE__, __func__)
#  define free(p) unbound_stat_free_log(p, __FILE__, __LINE__, __func__)
#  define realloc(p,s) unbound_stat_realloc_log(p, s, __FILE__, __LINE__, __func__)
void *unbound_stat_malloc(size_t size);
void *unbound_stat_calloc(size_t nmemb, size_t size);
void unbound_stat_free(void *ptr);
void *unbound_stat_realloc(void *ptr, size_t size);
void *unbound_stat_malloc_log(size_t size, const char* file, int line,
	const char* func);
void *unbound_stat_calloc_log(size_t nmemb, size_t size, const char* file,
	int line, const char* func);
void unbound_stat_free_log(void *ptr, const char* file, int line,
	const char* func);
void *unbound_stat_realloc_log(void *ptr, size_t size, const char* file,
	int line, const char* func);
#elif defined(UNBOUND_ALLOC_LITE)
#  include "util/alloc.h"
#endif /* UNBOUND_ALLOC_LITE and UNBOUND_ALLOC_STATS */

/** default port for DNS traffic. */
#define UNBOUND_DNS_PORT 53
/** default port for DNS over TLS traffic. */
#define UNBOUND_DNS_OVER_TLS_PORT 853
/** default port for unbound control traffic, registered port with IANA,
    ub-dns-control  8953/tcp    unbound dns nameserver control */
#define UNBOUND_CONTROL_PORT 8953
/** the version of unbound-control that this software implements */
#define UNBOUND_CONTROL_VERSION 1



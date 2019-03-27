dnl $Id: Makefile.m4,v 1.75 2013-08-27 19:02:10 ca Exp $
define(`confREQUIRE_LIBUNIX')
include(confBUILDTOOLSDIR`/M4/switch.m4')

define(`confREQUIRE_LIBSM', `true')
define(`confREQUIRE_SM_OS_H', `true')
PREPENDDEF(`confENVDEF', `confMAPDEF')
bldPRODUCT_START(`library', `libsm')
define(`bldSOURCES', ` assert.c debug.c errstring.c exc.c heap.c match.c rpool.c strdup.c strerror.c strl.c clrerr.c fclose.c feof.c ferror.c fflush.c fget.c fpos.c findfp.c flags.c fopen.c fprintf.c fpurge.c fput.c fread.c fscanf.c fseek.c fvwrite.c fwalk.c fwrite.c get.c makebuf.c put.c refill.c rewind.c setvbuf.c smstdio.c snprintf.c sscanf.c stdio.c strio.c ungetc.c vasprintf.c vfprintf.c vfscanf.c vprintf.c vsnprintf.c wbuf.c wsetup.c string.c stringf.c xtrap.c strto.c test.c strcasecmp.c strrevcmp.c signal.c clock.c config.c shm.c sem.c mbdb.c strexit.c cf.c ldap.c niprop.c mpeix.c memstat.c util.c inet6_ntop.c ')
bldPRODUCT_END
dnl msg.c
dnl syslogio.c

define(`confCHECK_LIBS',`libsm.a')dnl
include(confBUILDTOOLSDIR`/M4/'bldM4_TYPE_DIR`/check.m4')
smcheck(`t-event', `compile-run')
smcheck(`t-exc', `compile-run')
smcheck(`t-rpool', `compile-run')
smcheck(`t-string', `compile-run')
smcheck(`t-smstdio', `compile-run')
smcheck(`t-fget', `compile-run')
smcheck(`t-match', `compile-run')
smcheck(`t-strio', `compile-run')
smcheck(`t-heap', `compile-run')
smcheck(`t-fopen', `compile-run')
smcheck(`t-strl', `compile-run')
smcheck(`t-strrevcmp', `compile-run')
smcheck(`t-types', `compile-run')
smcheck(`t-path', `compile-run')
smcheck(`t-float', `compile-run')
smcheck(`t-scanf', `compile-run')
smcheck(`t-shm', `compile-run')
smcheck(`t-sem', `compile-run')
smcheck(`t-inet6_ntop', `compile-run')
dnl smcheck(`t-msg', `compile-run')
smcheck(`t-cf')
smcheck(`b-strcmp')
dnl SM_CONF_STRL cannot be turned off
dnl smcheck(`b-strl')
smcheck(`t-memstat')

smcheck(`t-qic', `compile-run')
divert(bldTARGETS_SECTION)
divert(0)

bldFINISH

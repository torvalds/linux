dnl ######################################################################
dnl OpenSSL support
AC_DEFUN([LIBEVENT_OPENSSL], [
AC_REQUIRE([NTP_PKG_CONFIG])dnl

case "$enable_openssl" in
 yes)
    have_openssl=no
    case "$PKG_CONFIG" in
     '')
	;;
     *)
	OPENSSL_LIBS=`$PKG_CONFIG --libs openssl 2>/dev/null`
	case "$OPENSSL_LIBS" in
	 '') ;;
	 *) OPENSSL_LIBS="$OPENSSL_LIBS $EV_LIB_GDI $EV_LIB_WS32 $OPENSSL_LIBADD"
	    have_openssl=yes
	    ;;
	esac
	OPENSSL_INCS=`$PKG_CONFIG --cflags openssl 2>/dev/null`
	;;
    esac
    case "$have_openssl" in
     yes) ;;
     *)
	save_LIBS="$LIBS"
	LIBS=""
	OPENSSL_LIBS=""
	AC_SEARCH_LIBS([SSL_new], [ssl],
	    [have_openssl=yes
	    OPENSSL_LIBS="$LIBS -lcrypto $EV_LIB_GDI $EV_LIB_WS32 $OPENSSL_LIBADD"],
	    [have_openssl=no],
	    [-lcrypto $EV_LIB_GDI $EV_LIB_WS32 $OPENSSL_LIBADD])
	LIBS="$save_LIBS"
	;;
    esac
    AC_SUBST(OPENSSL_INCS)
    AC_SUBST(OPENSSL_LIBS)
    case "$have_openssl" in
     yes)  AC_DEFINE(HAVE_OPENSSL, 1, [Define if the system has openssl]) ;;
    esac
    ;;
esac

# check if we have and should use openssl
AM_CONDITIONAL(OPENSSL, [test "$enable_openssl" != "no" && test "$have_openssl" = "yes"])
])

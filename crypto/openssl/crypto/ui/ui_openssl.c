/*
 * Copyright 2001-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include <openssl/e_os2.h>
#include <openssl/err.h>
#include <openssl/ui.h>

#ifndef OPENSSL_NO_UI_CONSOLE
/*
 * need for #define _POSIX_C_SOURCE arises whenever you pass -ansi to gcc
 * [maybe others?], because it masks interfaces not discussed in standard,
 * sigaction and fileno included. -pedantic would be more appropriate for the
 * intended purposes, but we can't prevent users from adding -ansi.
 */
# if defined(OPENSSL_SYS_VXWORKS)
#  include <sys/types.h>
# endif

# if !defined(_POSIX_C_SOURCE) && defined(OPENSSL_SYS_VMS)
#  ifndef _POSIX_C_SOURCE
#   define _POSIX_C_SOURCE 2
#  endif
# endif
# include <signal.h>
# include <stdio.h>
# include <string.h>
# include <errno.h>

# if !defined(OPENSSL_SYS_MSDOS) && !defined(OPENSSL_SYS_VMS)
#  ifdef OPENSSL_UNISTD
#   include OPENSSL_UNISTD
#  else
#   include <unistd.h>
#  endif
/*
 * If unistd.h defines _POSIX_VERSION, we conclude that we are on a POSIX
 * system and have sigaction and termios.
 */
#  if defined(_POSIX_VERSION) && _POSIX_VERSION>=199309L

#   define SIGACTION
#   if !defined(TERMIOS) && !defined(TERMIO) && !defined(SGTTY)
#    define TERMIOS
#   endif

#  endif
# endif

# include "ui_locl.h"
# include "internal/cryptlib.h"

# ifdef OPENSSL_SYS_VMS          /* prototypes for sys$whatever */
#  include <starlet.h>
#  ifdef __DECC
#   pragma message disable DOLLARID
#  endif
# endif

# ifdef WIN_CONSOLE_BUG
#  include <windows.h>
#  ifndef OPENSSL_SYS_WINCE
#   include <wincon.h>
#  endif
# endif

/*
 * There are 6 types of terminal interface supported, TERMIO, TERMIOS, VMS,
 * MSDOS, WIN32 Console and SGTTY.
 *
 * If someone defines one of the macros TERMIO, TERMIOS or SGTTY, it will
 * remain respected.  Otherwise, we default to TERMIOS except for a few
 * systems that require something different.
 *
 * Note: we do not use SGTTY unless it's defined by the configuration.  We
 * may eventually opt to remove it's use entirely.
 */

# if !defined(TERMIOS) && !defined(TERMIO) && !defined(SGTTY)

#  if defined(_LIBC)
#   undef  TERMIOS
#   define TERMIO
#   undef  SGTTY
/*
 * We know that VMS, MSDOS, VXWORKS, use entirely other mechanisms.
 */
#  elif !defined(OPENSSL_SYS_VMS) \
	&& !defined(OPENSSL_SYS_MSDOS) \
	&& !defined(OPENSSL_SYS_VXWORKS)
#   define TERMIOS
#   undef  TERMIO
#   undef  SGTTY
#  endif

# endif

# if defined(OPENSSL_SYS_VXWORKS)
#  undef TERMIOS
#  undef TERMIO
#  undef SGTTY
# endif

# ifdef TERMIOS
#  include <termios.h>
#  define TTY_STRUCT             struct termios
#  define TTY_FLAGS              c_lflag
#  define TTY_get(tty,data)      tcgetattr(tty,data)
#  define TTY_set(tty,data)      tcsetattr(tty,TCSANOW,data)
# endif

# ifdef TERMIO
#  include <termio.h>
#  define TTY_STRUCT             struct termio
#  define TTY_FLAGS              c_lflag
#  define TTY_get(tty,data)      ioctl(tty,TCGETA,data)
#  define TTY_set(tty,data)      ioctl(tty,TCSETA,data)
# endif

# ifdef SGTTY
#  include <sgtty.h>
#  define TTY_STRUCT             struct sgttyb
#  define TTY_FLAGS              sg_flags
#  define TTY_get(tty,data)      ioctl(tty,TIOCGETP,data)
#  define TTY_set(tty,data)      ioctl(tty,TIOCSETP,data)
# endif

# if !defined(_LIBC) && !defined(OPENSSL_SYS_MSDOS) && !defined(OPENSSL_SYS_VMS)
#  include <sys/ioctl.h>
# endif

# ifdef OPENSSL_SYS_MSDOS
#  include <conio.h>
# endif

# ifdef OPENSSL_SYS_VMS
#  include <ssdef.h>
#  include <iodef.h>
#  include <ttdef.h>
#  include <descrip.h>
struct IOSB {
    short iosb$w_value;
    short iosb$w_count;
    long iosb$l_info;
};
# endif

# ifndef NX509_SIG
#  define NX509_SIG 32
# endif

/* Define globals.  They are protected by a lock */
# ifdef SIGACTION
static struct sigaction savsig[NX509_SIG];
# else
static void (*savsig[NX509_SIG]) (int);
# endif

# ifdef OPENSSL_SYS_VMS
static struct IOSB iosb;
static $DESCRIPTOR(terminal, "TT");
static long tty_orig[3], tty_new[3]; /* XXX Is there any guarantee that this
                                      * will always suffice for the actual
                                      * structures? */
static long status;
static unsigned short channel = 0;
# elif defined(_WIN32) && !defined(_WIN32_WCE)
static DWORD tty_orig, tty_new;
# else
#  if !defined(OPENSSL_SYS_MSDOS) || defined(__DJGPP__)
static TTY_STRUCT tty_orig, tty_new;
#  endif
# endif
static FILE *tty_in, *tty_out;
static int is_a_tty;

/* Declare static functions */
# if !defined(OPENSSL_SYS_WINCE)
static int read_till_nl(FILE *);
static void recsig(int);
static void pushsig(void);
static void popsig(void);
# endif
# if defined(OPENSSL_SYS_MSDOS) && !defined(_WIN32)
static int noecho_fgets(char *buf, int size, FILE *tty);
# endif
static int read_string_inner(UI *ui, UI_STRING *uis, int echo, int strip_nl);

static int read_string(UI *ui, UI_STRING *uis);
static int write_string(UI *ui, UI_STRING *uis);

static int open_console(UI *ui);
static int echo_console(UI *ui);
static int noecho_console(UI *ui);
static int close_console(UI *ui);

/*
 * The following function makes sure that info and error strings are printed
 * before any prompt.
 */
static int write_string(UI *ui, UI_STRING *uis)
{
    switch (UI_get_string_type(uis)) {
    case UIT_ERROR:
    case UIT_INFO:
        fputs(UI_get0_output_string(uis), tty_out);
        fflush(tty_out);
        break;
    case UIT_NONE:
    case UIT_PROMPT:
    case UIT_VERIFY:
    case UIT_BOOLEAN:
        break;
    }
    return 1;
}

static int read_string(UI *ui, UI_STRING *uis)
{
    int ok = 0;

    switch (UI_get_string_type(uis)) {
    case UIT_BOOLEAN:
        fputs(UI_get0_output_string(uis), tty_out);
        fputs(UI_get0_action_string(uis), tty_out);
        fflush(tty_out);
        return read_string_inner(ui, uis,
                                 UI_get_input_flags(uis) & UI_INPUT_FLAG_ECHO,
                                 0);
    case UIT_PROMPT:
        fputs(UI_get0_output_string(uis), tty_out);
        fflush(tty_out);
        return read_string_inner(ui, uis,
                                 UI_get_input_flags(uis) & UI_INPUT_FLAG_ECHO,
                                 1);
    case UIT_VERIFY:
        fprintf(tty_out, "Verifying - %s", UI_get0_output_string(uis));
        fflush(tty_out);
        if ((ok = read_string_inner(ui, uis,
                                    UI_get_input_flags(uis) &
                                    UI_INPUT_FLAG_ECHO, 1)) <= 0)
            return ok;
        if (strcmp(UI_get0_result_string(uis), UI_get0_test_string(uis)) != 0) {
            fprintf(tty_out, "Verify failure\n");
            fflush(tty_out);
            return 0;
        }
        break;
    case UIT_NONE:
    case UIT_INFO:
    case UIT_ERROR:
        break;
    }
    return 1;
}

# if !defined(OPENSSL_SYS_WINCE)
/* Internal functions to read a string without echoing */
static int read_till_nl(FILE *in)
{
#  define SIZE 4
    char buf[SIZE + 1];

    do {
        if (!fgets(buf, SIZE, in))
            return 0;
    } while (strchr(buf, '\n') == NULL);
    return 1;
}

static volatile sig_atomic_t intr_signal;
# endif

static int read_string_inner(UI *ui, UI_STRING *uis, int echo, int strip_nl)
{
    static int ps;
    int ok;
    char result[BUFSIZ];
    int maxsize = BUFSIZ - 1;
# if !defined(OPENSSL_SYS_WINCE)
    char *p = NULL;
    int echo_eol = !echo;

    intr_signal = 0;
    ok = 0;
    ps = 0;

    pushsig();
    ps = 1;

    if (!echo && !noecho_console(ui))
        goto error;
    ps = 2;

    result[0] = '\0';
#  if defined(_WIN32)
    if (is_a_tty) {
        DWORD numread;
#   if defined(CP_UTF8)
        if (GetEnvironmentVariableW(L"OPENSSL_WIN32_UTF8", NULL, 0) != 0) {
            WCHAR wresult[BUFSIZ];

            if (ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE),
                         wresult, maxsize, &numread, NULL)) {
                if (numread >= 2 &&
                    wresult[numread-2] == L'\r' &&
                    wresult[numread-1] == L'\n') {
                    wresult[numread-2] = L'\n';
                    numread--;
                }
                wresult[numread] = '\0';
                if (WideCharToMultiByte(CP_UTF8, 0, wresult, -1,
                                        result, sizeof(result), NULL, 0) > 0)
                    p = result;

                OPENSSL_cleanse(wresult, sizeof(wresult));
            }
        } else
#   endif
        if (ReadConsoleA(GetStdHandle(STD_INPUT_HANDLE),
                         result, maxsize, &numread, NULL)) {
            if (numread >= 2 &&
                result[numread-2] == '\r' && result[numread-1] == '\n') {
                result[numread-2] = '\n';
                numread--;
            }
            result[numread] = '\0';
            p = result;
        }
    } else
#  elif defined(OPENSSL_SYS_MSDOS)
    if (!echo) {
        noecho_fgets(result, maxsize, tty_in);
        p = result;             /* FIXME: noecho_fgets doesn't return errors */
    } else
#  endif
    p = fgets(result, maxsize, tty_in);
    if (p == NULL)
        goto error;
    if (feof(tty_in))
        goto error;
    if (ferror(tty_in))
        goto error;
    if ((p = (char *)strchr(result, '\n')) != NULL) {
        if (strip_nl)
            *p = '\0';
    } else if (!read_till_nl(tty_in))
        goto error;
    if (UI_set_result(ui, uis, result) >= 0)
        ok = 1;

 error:
    if (intr_signal == SIGINT)
        ok = -1;
    if (echo_eol)
        fprintf(tty_out, "\n");
    if (ps >= 2 && !echo && !echo_console(ui))
        ok = 0;

    if (ps >= 1)
        popsig();
# else
    ok = 1;
# endif

    OPENSSL_cleanse(result, BUFSIZ);
    return ok;
}

/* Internal functions to open, handle and close a channel to the console.  */
static int open_console(UI *ui)
{
    CRYPTO_THREAD_write_lock(ui->lock);
    is_a_tty = 1;

# if defined(OPENSSL_SYS_VXWORKS)
    tty_in = stdin;
    tty_out = stderr;
# elif defined(_WIN32) && !defined(_WIN32_WCE)
    if ((tty_out = fopen("conout$", "w")) == NULL)
        tty_out = stderr;

    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &tty_orig)) {
        tty_in = stdin;
    } else {
        is_a_tty = 0;
        if ((tty_in = fopen("conin$", "r")) == NULL)
            tty_in = stdin;
    }
# else
#  ifdef OPENSSL_SYS_MSDOS
#   define DEV_TTY "con"
#  else
#   define DEV_TTY "/dev/tty"
#  endif
    if ((tty_in = fopen(DEV_TTY, "r")) == NULL)
        tty_in = stdin;
    if ((tty_out = fopen(DEV_TTY, "w")) == NULL)
        tty_out = stderr;
# endif

# if defined(TTY_get) && !defined(OPENSSL_SYS_VMS)
    if (TTY_get(fileno(tty_in), &tty_orig) == -1) {
#  ifdef ENOTTY
        if (errno == ENOTTY)
            is_a_tty = 0;
        else
#  endif
#  ifdef EINVAL
            /*
             * Ariel Glenn reports that solaris can return EINVAL instead.
             * This should be ok
             */
        if (errno == EINVAL)
            is_a_tty = 0;
        else
#  endif
#  ifdef ENXIO
            /*
             * Solaris can return ENXIO.
             * This should be ok
             */
        if (errno == ENXIO)
            is_a_tty = 0;
        else
#  endif
#  ifdef EIO
            /*
             * Linux can return EIO.
             * This should be ok
             */
        if (errno == EIO)
            is_a_tty = 0;
        else
#  endif
#  ifdef ENODEV
            /*
             * MacOS X returns ENODEV (Operation not supported by device),
             * which seems appropriate.
             */
        if (errno == ENODEV)
            is_a_tty = 0;
        else
#  endif
            {
                char tmp_num[10];
                BIO_snprintf(tmp_num, sizeof(tmp_num) - 1, "%d", errno);
                UIerr(UI_F_OPEN_CONSOLE, UI_R_UNKNOWN_TTYGET_ERRNO_VALUE);
                ERR_add_error_data(2, "errno=", tmp_num);

                return 0;
            }
    }
# endif
# ifdef OPENSSL_SYS_VMS
    status = sys$assign(&terminal, &channel, 0, 0);

    /* if there isn't a TT device, something is very wrong */
    if (status != SS$_NORMAL) {
        char tmp_num[12];

        BIO_snprintf(tmp_num, sizeof(tmp_num) - 1, "%%X%08X", status);
        UIerr(UI_F_OPEN_CONSOLE, UI_R_SYSASSIGN_ERROR);
        ERR_add_error_data(2, "status=", tmp_num);
        return 0;
    }

    status = sys$qiow(0, channel, IO$_SENSEMODE, &iosb, 0, 0, tty_orig, 12,
                      0, 0, 0, 0);

    /* If IO$_SENSEMODE doesn't work, this is not a terminal device */
    if ((status != SS$_NORMAL) || (iosb.iosb$w_value != SS$_NORMAL))
        is_a_tty = 0;
# endif
    return 1;
}

static int noecho_console(UI *ui)
{
# ifdef TTY_FLAGS
    memcpy(&(tty_new), &(tty_orig), sizeof(tty_orig));
    tty_new.TTY_FLAGS &= ~ECHO;
# endif

# if defined(TTY_set) && !defined(OPENSSL_SYS_VMS)
    if (is_a_tty && (TTY_set(fileno(tty_in), &tty_new) == -1))
        return 0;
# endif
# ifdef OPENSSL_SYS_VMS
    if (is_a_tty) {
        tty_new[0] = tty_orig[0];
        tty_new[1] = tty_orig[1] | TT$M_NOECHO;
        tty_new[2] = tty_orig[2];
        status = sys$qiow(0, channel, IO$_SETMODE, &iosb, 0, 0, tty_new, 12,
                          0, 0, 0, 0);
        if ((status != SS$_NORMAL) || (iosb.iosb$w_value != SS$_NORMAL)) {
            char tmp_num[2][12];

            BIO_snprintf(tmp_num[0], sizeof(tmp_num[0]) - 1, "%%X%08X",
                         status);
            BIO_snprintf(tmp_num[1], sizeof(tmp_num[1]) - 1, "%%X%08X",
                         iosb.iosb$w_value);
            UIerr(UI_F_NOECHO_CONSOLE, UI_R_SYSQIOW_ERROR);
            ERR_add_error_data(5, "status=", tmp_num[0],
                               ",", "iosb.iosb$w_value=", tmp_num[1]);
            return 0;
        }
    }
# endif
# if defined(_WIN32) && !defined(_WIN32_WCE)
    if (is_a_tty) {
        tty_new = tty_orig;
        tty_new &= ~ENABLE_ECHO_INPUT;
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), tty_new);
    }
# endif
    return 1;
}

static int echo_console(UI *ui)
{
# if defined(TTY_set) && !defined(OPENSSL_SYS_VMS)
    memcpy(&(tty_new), &(tty_orig), sizeof(tty_orig));
    if (is_a_tty && (TTY_set(fileno(tty_in), &tty_new) == -1))
        return 0;
# endif
# ifdef OPENSSL_SYS_VMS
    if (is_a_tty) {
        tty_new[0] = tty_orig[0];
        tty_new[1] = tty_orig[1];
        tty_new[2] = tty_orig[2];
        status = sys$qiow(0, channel, IO$_SETMODE, &iosb, 0, 0, tty_new, 12,
                          0, 0, 0, 0);
        if ((status != SS$_NORMAL) || (iosb.iosb$w_value != SS$_NORMAL)) {
            char tmp_num[2][12];

            BIO_snprintf(tmp_num[0], sizeof(tmp_num[0]) - 1, "%%X%08X",
                         status);
            BIO_snprintf(tmp_num[1], sizeof(tmp_num[1]) - 1, "%%X%08X",
                         iosb.iosb$w_value);
            UIerr(UI_F_ECHO_CONSOLE, UI_R_SYSQIOW_ERROR);
            ERR_add_error_data(5, "status=", tmp_num[0],
                               ",", "iosb.iosb$w_value=", tmp_num[1]);
            return 0;
        }
    }
# endif
# if defined(_WIN32) && !defined(_WIN32_WCE)
    if (is_a_tty) {
        tty_new = tty_orig;
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), tty_new);
    }
# endif
    return 1;
}

static int close_console(UI *ui)
{
    if (tty_in != stdin)
        fclose(tty_in);
    if (tty_out != stderr)
        fclose(tty_out);
# ifdef OPENSSL_SYS_VMS
    status = sys$dassgn(channel);
    if (status != SS$_NORMAL) {
        char tmp_num[12];

        BIO_snprintf(tmp_num, sizeof(tmp_num) - 1, "%%X%08X", status);
        UIerr(UI_F_CLOSE_CONSOLE, UI_R_SYSDASSGN_ERROR);
        ERR_add_error_data(2, "status=", tmp_num);
        return 0;
    }
# endif
    CRYPTO_THREAD_unlock(ui->lock);

    return 1;
}

# if !defined(OPENSSL_SYS_WINCE)
/* Internal functions to handle signals and act on them */
static void pushsig(void)
{
#  ifndef OPENSSL_SYS_WIN32
    int i;
#  endif
#  ifdef SIGACTION
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = recsig;
#  endif

#  ifdef OPENSSL_SYS_WIN32
    savsig[SIGABRT] = signal(SIGABRT, recsig);
    savsig[SIGFPE] = signal(SIGFPE, recsig);
    savsig[SIGILL] = signal(SIGILL, recsig);
    savsig[SIGINT] = signal(SIGINT, recsig);
    savsig[SIGSEGV] = signal(SIGSEGV, recsig);
    savsig[SIGTERM] = signal(SIGTERM, recsig);
#  else
    for (i = 1; i < NX509_SIG; i++) {
#   ifdef SIGUSR1
        if (i == SIGUSR1)
            continue;
#   endif
#   ifdef SIGUSR2
        if (i == SIGUSR2)
            continue;
#   endif
#   ifdef SIGKILL
        if (i == SIGKILL)       /* We can't make any action on that. */
            continue;
#   endif
#   ifdef SIGACTION
        sigaction(i, &sa, &savsig[i]);
#   else
        savsig[i] = signal(i, recsig);
#   endif
    }
#  endif

#  ifdef SIGWINCH
    signal(SIGWINCH, SIG_DFL);
#  endif
}

static void popsig(void)
{
#  ifdef OPENSSL_SYS_WIN32
    signal(SIGABRT, savsig[SIGABRT]);
    signal(SIGFPE, savsig[SIGFPE]);
    signal(SIGILL, savsig[SIGILL]);
    signal(SIGINT, savsig[SIGINT]);
    signal(SIGSEGV, savsig[SIGSEGV]);
    signal(SIGTERM, savsig[SIGTERM]);
#  else
    int i;
    for (i = 1; i < NX509_SIG; i++) {
#   ifdef SIGUSR1
        if (i == SIGUSR1)
            continue;
#   endif
#   ifdef SIGUSR2
        if (i == SIGUSR2)
            continue;
#   endif
#   ifdef SIGACTION
        sigaction(i, &savsig[i], NULL);
#   else
        signal(i, savsig[i]);
#   endif
    }
#  endif
}

static void recsig(int i)
{
    intr_signal = i;
}
# endif

/* Internal functions specific for Windows */
# if defined(OPENSSL_SYS_MSDOS) && !defined(_WIN32)
static int noecho_fgets(char *buf, int size, FILE *tty)
{
    int i;
    char *p;

    p = buf;
    for (;;) {
        if (size == 0) {
            *p = '\0';
            break;
        }
        size--;
#  if defined(_WIN32)
        i = _getch();
#  else
        i = getch();
#  endif
        if (i == '\r')
            i = '\n';
        *(p++) = i;
        if (i == '\n') {
            *p = '\0';
            break;
        }
    }
#  ifdef WIN_CONSOLE_BUG
    /*
     * Win95 has several evil console bugs: one of these is that the last
     * character read using getch() is passed to the next read: this is
     * usually a CR so this can be trouble. No STDIO fix seems to work but
     * flushing the console appears to do the trick.
     */
    {
        HANDLE inh;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        FlushConsoleInputBuffer(inh);
    }
#  endif
    return strlen(buf);
}
# endif

static UI_METHOD ui_openssl = {
    "OpenSSL default user interface",
    open_console,
    write_string,
    NULL,                       /* No flusher is needed for command lines */
    read_string,
    close_console,
    NULL
};

/* The method with all the built-in console thingies */
UI_METHOD *UI_OpenSSL(void)
{
    return &ui_openssl;
}

static const UI_METHOD *default_UI_meth = &ui_openssl;

#else

static const UI_METHOD *default_UI_meth = NULL;

#endif

void UI_set_default_method(const UI_METHOD *meth)
{
    default_UI_meth = meth;
}

const UI_METHOD *UI_get_default_method(void)
{
    return default_UI_meth;
}

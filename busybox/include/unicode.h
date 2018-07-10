/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#ifndef UNICODE_H
#define UNICODE_H 1

#if ENABLE_UNICODE_USING_LOCALE
# include <wchar.h>
# include <wctype.h>
#endif

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

enum {
	UNICODE_UNKNOWN = 0,
	UNICODE_OFF = 1,
	UNICODE_ON = 2,
};

#define unicode_bidi_isrtl(wc) 0
#define unicode_bidi_is_neutral_wchar(wc) (wc <= 126 && !isalpha(wc))

#if !ENABLE_UNICODE_SUPPORT

# define unicode_strlen(string)   strlen(string)
# define unicode_strwidth(string) strlen(string)
# define unicode_status UNICODE_OFF
# define init_unicode() ((void)0)
# define reinit_unicode(LANG) ((void)0)

#else

# if CONFIG_LAST_SUPPORTED_WCHAR < 126 || CONFIG_LAST_SUPPORTED_WCHAR >= 0x30000
#  undef CONFIG_LAST_SUPPORTED_WCHAR
#  define CONFIG_LAST_SUPPORTED_WCHAR 0x2ffff
# endif

# if CONFIG_LAST_SUPPORTED_WCHAR < 0x300
#  undef ENABLE_UNICODE_COMBINING_WCHARS
#  define ENABLE_UNICODE_COMBINING_WCHARS 0
# endif

# if CONFIG_LAST_SUPPORTED_WCHAR < 0x1100
#  undef ENABLE_UNICODE_WIDE_WCHARS
#  define ENABLE_UNICODE_WIDE_WCHARS 0
# endif

# if CONFIG_LAST_SUPPORTED_WCHAR < 0x590
#  undef  ENABLE_UNICODE_BIDI_SUPPORT
#  define ENABLE_UNICODE_BIDI_SUPPORT 0
# endif

/* Number of unicode chars. Falls back to strlen() on invalid unicode */
size_t FAST_FUNC unicode_strlen(const char *string);
/* Width on terminal */
size_t FAST_FUNC unicode_strwidth(const char *string);
enum {
	UNI_FLAG_PAD = (1 << 0),
};
//UNUSED: unsigned FAST_FUNC unicode_padding_to_width(unsigned width, const char *src);
//UNUSED: char* FAST_FUNC unicode_conv_to_printable2(uni_stat_t *stats, const char *src, unsigned width, int flags);
char* FAST_FUNC unicode_conv_to_printable(uni_stat_t *stats, const char *src);
//UNUSED: char* FAST_FUNC unicode_conv_to_printable_maxwidth(uni_stat_t *stats, const char *src, unsigned maxwidth);
char* FAST_FUNC unicode_conv_to_printable_fixedwidth(/*uni_stat_t *stats,*/ const char *src, unsigned width);

# if ENABLE_UNICODE_USING_LOCALE

extern uint8_t unicode_status;
void init_unicode(void) FAST_FUNC;
void reinit_unicode(const char *LANG) FAST_FUNC;

# else

/* Homegrown Unicode support. It knows only C and Unicode locales. */

#  if !ENABLE_FEATURE_CHECK_UNICODE_IN_ENV
#   define unicode_status UNICODE_ON
#   define init_unicode() ((void)0)
#   define reinit_unicode(LANG) ((void)0)
#  else
extern uint8_t unicode_status;
void init_unicode(void) FAST_FUNC;
void reinit_unicode(const char *LANG) FAST_FUNC;
#  endif

#  undef MB_CUR_MAX
#  define MB_CUR_MAX 6

/* Prevent name collisions */
#  define wint_t    bb_wint_t
#  define mbstate_t bb_mbstate_t
#  define mbstowcs  bb_mbstowcs
#  define wcstombs  bb_wcstombs
#  define wcrtomb   bb_wcrtomb
#  define iswspace  bb_iswspace
#  define iswalnum  bb_iswalnum
#  define iswpunct  bb_iswpunct
#  define wcwidth   bb_wcwidth

typedef int32_t wint_t;
typedef struct {
	char bogus;
} mbstate_t;

size_t mbstowcs(wchar_t *dest, const char *src, size_t n) FAST_FUNC;
size_t wcstombs(char *dest, const wchar_t *src, size_t n) FAST_FUNC;
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) FAST_FUNC;
int iswspace(wint_t wc) FAST_FUNC;
int iswalnum(wint_t wc) FAST_FUNC;
int iswpunct(wint_t wc) FAST_FUNC;
int wcwidth(unsigned ucs) FAST_FUNC;
#  if ENABLE_UNICODE_BIDI_SUPPORT
#   undef unicode_bidi_isrtl
int unicode_bidi_isrtl(wint_t wc) FAST_FUNC;
#   if ENABLE_UNICODE_NEUTRAL_TABLE
#    undef unicode_bidi_is_neutral_wchar
int unicode_bidi_is_neutral_wchar(wint_t wc) FAST_FUNC;
#   endif
#  endif


# endif /* !UNICODE_USING_LOCALE */

#endif /* UNICODE_SUPPORT */

POP_SAVED_FUNCTION_VISIBILITY

#endif

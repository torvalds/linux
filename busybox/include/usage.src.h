/* vi: set sw=8 ts=8: */
/*
 * This file suffers from chronically incorrect tabification
 * of messages. Before editing this file:
 * 1. Switch you editor to 8-space tab mode.
 * 2. Do not use \t in messages, use real tab character.
 * 3. Start each source line with message as follows:
 *    |<7 spaces>"text with tabs"....
 * or
 *    |<5 spaces>"\ntext with tabs"....
 */
#ifndef BB_USAGE_H
#define BB_USAGE_H 1

#define NOUSAGE_STR "\b"

#if !ENABLE_USE_BB_CRYPT || ENABLE_USE_BB_CRYPT_SHA
# define CRYPT_METHODS_HELP_STR "des,md5,sha256/512" \
	" (default "CONFIG_FEATURE_DEFAULT_PASSWD_ALGO")"
#else
# define CRYPT_METHODS_HELP_STR "des,md5" \
	" (default "CONFIG_FEATURE_DEFAULT_PASSWD_ALGO")"
#endif

INSERT

#define busybox_notes_usage \
       "Hello world!\n"

#endif

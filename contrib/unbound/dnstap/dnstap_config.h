#ifndef UNBOUND_DNSTAP_CONFIG_H
#define UNBOUND_DNSTAP_CONFIG_H

/*
 * Process this file (dnstap_config.h.in) with AC_CONFIG_FILES to generate
 * dnstap_config.h.
 *
 * This file exists so that USE_DNSTAP can be used without including config.h.
 */

#if 0 /* ENABLE_DNSTAP */
# ifndef USE_DNSTAP
#  define USE_DNSTAP 1
# endif
#endif

#endif /* UNBOUND_DNSTAP_CONFIG_H */

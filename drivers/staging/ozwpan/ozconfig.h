/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * ---------------------------------------------------------------------------*/
#ifndef _OZCONFIG_H
#define _OZCONFIG_H

/* #define WANT_TRACE */
#ifdef WANT_TRACE
#define WANT_VERBOSE_TRACE
#endif /* #ifdef WANT_TRACE */
/* #define WANT_URB_PARANOIA */

/* #define WANT_PRE_2_6_39 */

/* These defines determine what verbose trace is displayed. */
#ifdef WANT_VERBOSE_TRACE
/* #define WANT_TRACE_STREAM */
/* #define WANT_TRACE_URB */
/* #define WANT_TRACE_CTRL_DETAIL */
#define WANT_TRACE_HUB
/* #define WANT_TRACE_RX_FRAMES */
/* #define WANT_TRACE_TX_FRAMES */
#endif /* WANT_VERBOSE_TRACE */

#endif /* _OZCONFIG_H */

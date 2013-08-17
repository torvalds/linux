/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include "ozconfig.h"
#include "oztrace.h"

#ifdef WANT_VERBOSE_TRACE
unsigned long trace_flags =
	0
#ifdef WANT_TRACE_STREAM
	| OZ_TRACE_STREAM
#endif /* WANT_TRACE_STREAM */
#ifdef WANT_TRACE_URB
	| OZ_TRACE_URB
#endif /* WANT_TRACE_URB */

#ifdef WANT_TRACE_CTRL_DETAIL
	| OZ_TRACE_CTRL_DETAIL
#endif /* WANT_TRACE_CTRL_DETAIL */

#ifdef WANT_TRACE_HUB
	| OZ_TRACE_HUB
#endif /* WANT_TRACE_HUB */

#ifdef WANT_TRACE_RX_FRAMES
	| OZ_TRACE_RX_FRAMES
#endif /* WANT_TRACE_RX_FRAMES */

#ifdef WANT_TRACE_TX_FRAMES
	| OZ_TRACE_TX_FRAMES
#endif /* WANT_TRACE_TX_FRAMES */
	;
#endif /* WANT_VERBOSE_TRACE */


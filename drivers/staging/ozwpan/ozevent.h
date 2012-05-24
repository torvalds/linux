/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZEVENT_H
#define _OZEVENT_H
#include "ozconfig.h"
#include "ozeventdef.h"

#ifdef WANT_EVENT_TRACE
extern unsigned long g_evt_mask;
void oz_event_init(void);
void oz_event_term(void);
void oz_event_log2(u8 evt, u8 ctx1, u16 ctx2, void *ctx3, unsigned ctx4);
#define oz_event_log(__evt, __ctx1, __ctx2, __ctx3, __ctx4) \
	do { \
		if ((1<<(__evt)) & g_evt_mask) \
			oz_event_log2(__evt, __ctx1, __ctx2, __ctx3, __ctx4); \
	} while (0)
int oz_events_copy(struct oz_evtlist __user *lst);
void oz_events_clear(void);
#else
#define oz_event_init()
#define oz_event_term()
#define oz_event_log(__evt, __ctx1, __ctx2, __ctx3, __ctx4)
#define oz_events_copy(__lst)
#define oz_events_clear()
#endif /* WANT_EVENT_TRACE */

#endif /* _OZEVENT_H */

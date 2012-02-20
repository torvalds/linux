/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include "ozconfig.h"
#ifdef WANT_EVENT_TRACE
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include "oztrace.h"
#include "ozevent.h"
/*------------------------------------------------------------------------------
 */
unsigned long g_evt_mask = 0xffffffff;
/*------------------------------------------------------------------------------
 */
#define OZ_MAX_EVTS	2048	/* Must be power of 2 */
DEFINE_SPINLOCK(g_eventlock);
static int g_evt_in;
static int g_evt_out;
static int g_missed_events;
static struct oz_event g_events[OZ_MAX_EVTS];
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_event_init(void)
{
	oz_trace("Event tracing initialized\n");
	g_evt_in = g_evt_out = 0;
	g_missed_events = 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_event_term(void)
{
	oz_trace("Event tracing terminated\n");
}
/*------------------------------------------------------------------------------
 * Context: any
 */
void oz_event_log2(u8 evt, u8 ctx1, u16 ctx2, void *ctx3, unsigned ctx4)
{
	unsigned long irqstate;
	int ix;
	spin_lock_irqsave(&g_eventlock, irqstate);
	ix = (g_evt_in + 1) & (OZ_MAX_EVTS - 1);
	if (ix != g_evt_out) {
		struct oz_event *e = &g_events[g_evt_in];
		e->jiffies = jiffies;
		e->evt = evt;
		e->ctx1 = ctx1;
		e->ctx2 = ctx2;
		e->ctx3 = ctx3;
		e->ctx4 = ctx4;
		g_evt_in = ix;
	} else {
		g_missed_events++;
	}
	spin_unlock_irqrestore(&g_eventlock, irqstate);
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_events_copy(struct oz_evtlist __user *lst)
{
	int first;
	int ix;
	struct hdr {
		int count;
		int missed;
	} hdr;
	ix = g_evt_out;
	hdr.count = g_evt_in - ix;
	if (hdr.count < 0)
		hdr.count += OZ_MAX_EVTS;
	if (hdr.count > OZ_EVT_LIST_SZ)
		hdr.count = OZ_EVT_LIST_SZ;
	hdr.missed = g_missed_events;
	g_missed_events = 0;
	if (copy_to_user((void __user *)lst, &hdr, sizeof(hdr)))
		return -EFAULT;
	first = OZ_MAX_EVTS - ix;
	if (first > hdr.count)
		first = hdr.count;
	if (first) {
		int sz = first*sizeof(struct oz_event);
		void __user *p = (void __user *)lst->evts;
		if (copy_to_user(p, &g_events[ix], sz))
			return -EFAULT;
		if (hdr.count > first) {
			p = (void __user *)&lst->evts[first];
			sz = (hdr.count-first)*sizeof(struct oz_event);
			if (copy_to_user(p, g_events, sz))
				return -EFAULT;
		}
	}
	ix += hdr.count;
	if (ix >= OZ_MAX_EVTS)
		ix -= OZ_MAX_EVTS;
	g_evt_out = ix;
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_events_clear(void)
{
	unsigned long irqstate;
	spin_lock_irqsave(&g_eventlock, irqstate);
	g_evt_in = g_evt_out = 0;
	g_missed_events = 0;
	spin_unlock_irqrestore(&g_eventlock, irqstate);
}
#endif /* WANT_EVENT_TRACE */


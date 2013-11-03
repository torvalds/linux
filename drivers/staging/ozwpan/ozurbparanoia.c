/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include <linux/usb.h>
#include "ozdbg.h"

#ifdef WANT_URB_PARANOIA

#include "ozurbparanoia.h"

#define OZ_MAX_URBS	1000
struct urb *g_urb_memory[OZ_MAX_URBS];
int g_nb_urbs;
DEFINE_SPINLOCK(g_urb_mem_lock);

void oz_remember_urb(struct urb *urb)
{
	unsigned long irq_state;

	spin_lock_irqsave(&g_urb_mem_lock, irq_state);
	if (g_nb_urbs < OZ_MAX_URBS) {
		g_urb_memory[g_nb_urbs++] = urb;
		oz_dbg(ON, "urb up = %d %p\n", g_nb_urbs, urb);
	} else {
		oz_dbg(ON, "ERROR urb buffer full\n");
	}
	spin_unlock_irqrestore(&g_urb_mem_lock, irq_state);
}

/*
 */
int oz_forget_urb(struct urb *urb)
{
	unsigned long irq_state;
	int i;
	int rc = -1;

	spin_lock_irqsave(&g_urb_mem_lock, irq_state);
	for (i = 0; i < g_nb_urbs; i++) {
		if (g_urb_memory[i] == urb) {
			rc = 0;
			if (--g_nb_urbs > i)
				memcpy(&g_urb_memory[i], &g_urb_memory[i+1],
					(g_nb_urbs - i) * sizeof(struct urb *));
			oz_dbg(ON, "urb down = %d %p\n", g_nb_urbs, urb);
		}
	}
	spin_unlock_irqrestore(&g_urb_mem_lock, irq_state);
	return rc;
}
#endif /* #ifdef WANT_URB_PARANOIA */


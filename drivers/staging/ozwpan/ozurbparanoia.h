#ifndef _OZURBPARANOIA_H
#define _OZURBPARANOIA_H
/* -----------------------------------------------------------------------------
 * Released under the GNU General Public License Version 2 (GPLv2).
 * Copyright (c) 2011 Ozmo Inc
 * -----------------------------------------------------------------------------
 */

#ifdef WANT_URB_PARANOIA
void oz_remember_urb(struct urb *urb);
int oz_forget_urb(struct urb *urb);
#else
#define oz_remember_urb(__x)
#define oz_forget_urb(__x)	0
#endif /* WANT_URB_PARANOIA */


#endif /* _OZURBPARANOIA_H */


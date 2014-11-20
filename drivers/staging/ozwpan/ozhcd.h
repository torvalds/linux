/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * ---------------------------------------------------------------------------*/
#ifndef _OZHCD_H
#define _OZHCD_H

int oz_hcd_init(void);
void oz_hcd_term(void);
struct oz_port *oz_hcd_pd_arrived(void *ctx);
void oz_hcd_pd_departed(struct oz_port *hport);
void oz_hcd_pd_reset(void *hpd, void *hport);

#endif /* _OZHCD_H */


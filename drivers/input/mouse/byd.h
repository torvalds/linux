/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BYD_H
#define _BYD_H

int byd_detect(struct psmouse *psmouse, bool set_properties);
int byd_init(struct psmouse *psmouse);

#endif /* _BYD_H */

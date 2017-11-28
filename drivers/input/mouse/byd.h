/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BYD_H
#define _BYD_H

#ifdef CONFIG_MOUSE_PS2_BYD
int byd_detect(struct psmouse *psmouse, bool set_properties);
int byd_init(struct psmouse *psmouse);
#else
static inline int byd_detect(struct psmouse *psmouse, bool set_properties)
{
	return -ENOSYS;
}
static inline int byd_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}
#endif /* CONFIG_MOUSE_PS2_BYD */

#endif /* _BYD_H */

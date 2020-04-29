/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _SUN4I_DOTCLOCK_H_
#define _SUN4I_DOTCLOCK_H_

struct sun4i_tcon;

int sun4i_dclk_create(struct device *dev, struct sun4i_tcon *tcon);
int sun4i_dclk_free(struct sun4i_tcon *tcon);

#endif /* _SUN4I_DOTCLOCK_H_ */

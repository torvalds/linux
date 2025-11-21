/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    gpio functions.
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@kernel.org>

 */

#ifndef IVTV_GPIO_H
#define IVTV_GPIO_H

/* GPIO stuff */
int ivtv_gpio_init(struct ivtv *itv);
void ivtv_reset_ir_gpio(struct ivtv *itv);
int ivtv_reset_tuner_gpio(void *dev, int component, int cmd, int value);

#endif

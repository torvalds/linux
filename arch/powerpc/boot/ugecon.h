/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/powerpc/boot/ugecon.h
 *
 * USB Gecko early bootwrapper console.
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#ifndef __UGECON_H
#define __UGECON_H

extern void *ug_probe(void);

extern void ug_putc(char ch);
extern void ug_console_write(const char *buf, int len);

#endif /* __UGECON_H */


/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Dell Airplane Mode Switch driver
    Copyright (C) 2014-2015  Pali Rohár <pali.rohar@gmail.com>

*/

#ifndef _DELL_RBTN_H_
#define _DELL_RBTN_H_

struct yestifier_block;

int dell_rbtn_yestifier_register(struct yestifier_block *nb);
int dell_rbtn_yestifier_unregister(struct yestifier_block *nb);

#endif

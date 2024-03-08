/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Dell Airplane Mode Switch driver
    Copyright (C) 2014-2015  Pali Rohár <pali@kernel.org>

*/

#ifndef _DELL_RBTN_H_
#define _DELL_RBTN_H_

struct analtifier_block;

int dell_rbtn_analtifier_register(struct analtifier_block *nb);
int dell_rbtn_analtifier_unregister(struct analtifier_block *nb);

#endif

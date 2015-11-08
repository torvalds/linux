/*
    Dell Airplane Mode Switch driver
    Copyright (C) 2014-2015  Pali Rohár <pali.rohar@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#ifndef _DELL_RBTN_H_
#define _DELL_RBTN_H_

struct notifier_block;

int dell_rbtn_notifier_register(struct notifier_block *nb);
int dell_rbtn_notifier_unregister(struct notifier_block *nb);

#endif

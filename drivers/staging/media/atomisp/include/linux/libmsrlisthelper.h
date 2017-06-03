/*
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#ifndef __LIBMSRLISTHELPER_H__
#define __LIBMSRLISTHELPER_H__

struct i2c_client;
struct firmware;

extern int load_msr_list(struct i2c_client *client, char *path,
		const struct firmware **fw);
extern int apply_msr_data(struct i2c_client *client, const struct firmware *fw);
extern void release_msr_list(struct i2c_client *client,
		const struct firmware *fw);


#endif /* ifndef __LIBMSRLISTHELPER_H__ */

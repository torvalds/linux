/*
 * Copyright: Matias Bjorling <mb@bjorling.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef GENNVM_H_
#define GENNVM_H_

#include <linux/module.h>
#include <linux/vmalloc.h>

#include <linux/lightnvm.h>

struct gen_dev {
	struct nvm_dev *dev;

	int nr_luns;
	struct nvm_lun *luns;
	struct list_head area_list;

	struct mutex lock;
	struct list_head targets;
};

struct gen_area {
	struct list_head list;
	sector_t begin;
	sector_t end;	/* end is excluded */
};

#define gen_for_each_lun(bm, lun, i) \
		for ((i) = 0, lun = &(bm)->luns[0]; \
			(i) < (bm)->nr_luns; (i)++, lun = &(bm)->luns[(i)])

#endif /* GENNVM_H_ */

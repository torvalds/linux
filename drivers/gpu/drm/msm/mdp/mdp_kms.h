/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MDP_KMS_H__
#define __MDP_KMS_H__

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "msm_drv.h"
#include "mdp_common.xml.h"

struct mdp_format {
	struct msm_format base;
	enum mdp_bpc bpc_r, bpc_g, bpc_b;
	enum mdp_bpc_alpha bpc_a;
	uint8_t unpack[4];
	bool alpha_enable, unpack_tight;
	uint8_t cpp, unpack_count;
};
#define to_mdp_format(x) container_of(x, struct mdp_format, base)


uint32_t mdp_get_formats(uint32_t *formats, uint32_t max_formats);
const struct msm_format *mdp_get_format(struct msm_kms *kms, uint32_t format);

#endif /* __MDP_KMS_H__ */

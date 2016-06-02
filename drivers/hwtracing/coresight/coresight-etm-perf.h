/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
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

#ifndef _CORESIGHT_ETM_PERF_H
#define _CORESIGHT_ETM_PERF_H

struct coresight_device;

#ifdef CONFIG_CORESIGHT
int etm_perf_symlink(struct coresight_device *csdev, bool link);

#else
static inline int etm_perf_symlink(struct coresight_device *csdev, bool link)
{ return -EINVAL; }

#endif /* CONFIG_CORESIGHT */

#endif

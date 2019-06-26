/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 */

#ifndef _CEDRUS_HW_H_
#define _CEDRUS_HW_H_

#define CEDRUS_CLOCK_RATE_DEFAULT	320000000

int cedrus_engine_enable(struct cedrus_dev *dev, enum cedrus_codec codec);
void cedrus_engine_disable(struct cedrus_dev *dev);

void cedrus_dst_format_set(struct cedrus_dev *dev,
			   struct v4l2_pix_format *fmt);

int cedrus_hw_probe(struct cedrus_dev *dev);
void cedrus_hw_remove(struct cedrus_dev *dev);

#endif

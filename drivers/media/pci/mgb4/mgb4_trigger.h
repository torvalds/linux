/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

struct iio_dev *mgb4_trigger_create(struct mgb4_dev *mgbdev);
void mgb4_trigger_free(struct iio_dev *indio_dev);

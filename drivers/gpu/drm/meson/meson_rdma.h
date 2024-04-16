/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __MESON_RDMA_H
#define __MESON_RDMA_H

#include "meson_drv.h"

int meson_rdma_init(struct meson_drm *priv);
void meson_rdma_free(struct meson_drm *priv);
void meson_rdma_setup(struct meson_drm *priv);
void meson_rdma_reset(struct meson_drm *priv);
void meson_rdma_stop(struct meson_drm *priv);

void meson_rdma_writel_sync(struct meson_drm *priv, uint32_t val, uint32_t reg);
void meson_rdma_flush(struct meson_drm *priv);

#endif /* __MESON_RDMA_H */

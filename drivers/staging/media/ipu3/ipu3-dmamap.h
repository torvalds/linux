/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */
/* Copyright 2018 Google LLC. */

#ifndef __IPU3_DMAMAP_H
#define __IPU3_DMAMAP_H

struct imgu_device;
struct scatterlist;

void *imgu_dmamap_alloc(struct imgu_device *imgu, struct imgu_css_map *map,
			size_t len);
void imgu_dmamap_free(struct imgu_device *imgu, struct imgu_css_map *map);

int imgu_dmamap_map_sg(struct imgu_device *imgu, struct scatterlist *sglist,
		       int nents, struct imgu_css_map *map);
void imgu_dmamap_unmap(struct imgu_device *imgu, struct imgu_css_map *map);

int imgu_dmamap_init(struct imgu_device *imgu);
void imgu_dmamap_exit(struct imgu_device *imgu);

#endif

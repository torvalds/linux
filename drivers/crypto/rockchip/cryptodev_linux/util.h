/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef UTILS_H
#define UTILS_H
int cryptodev_sg_copy(struct scatterlist *sg_from, struct scatterlist *sg_to, int len);
struct scatterlist *cryptodev_sg_advance(struct scatterlist *sg, int consumed);
#endif


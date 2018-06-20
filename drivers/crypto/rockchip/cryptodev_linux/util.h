/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef UTILS_H
#define UTILS_H
int sg_copy(struct scatterlist *sg_from, struct scatterlist *sg_to, int len);
struct scatterlist *sg_advance(struct scatterlist *sg, int consumed);
#endif


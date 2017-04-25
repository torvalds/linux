/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_MEM_H
#define DELTA_MEM_H

int hw_alloc(struct delta_ctx *ctx, u32 size, const char *name,
	     struct delta_buf *buf);
void hw_free(struct delta_ctx *ctx, struct delta_buf *buf);

#endif /* DELTA_MEM_H */

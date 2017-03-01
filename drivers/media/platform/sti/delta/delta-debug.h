/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Hugues Fruchet <hugues.fruchet@st.com>
 *          Fabrice Lecoultre <fabrice.lecoultre@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_DEBUG_H
#define DELTA_DEBUG_H

char *delta_streaminfo_str(struct delta_streaminfo *s, char *str,
			   unsigned int len);
char *delta_frameinfo_str(struct delta_frameinfo *f, char *str,
			  unsigned int len);
void delta_trace_summary(struct delta_ctx *ctx);

#endif /* DELTA_DEBUG_H */

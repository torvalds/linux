/*
 * Copyright (C) STMicroelectronics SA 2013
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_MJPEG_H
#define DELTA_MJPEG_H

#include "delta.h"

struct mjpeg_component {
	unsigned int id;/* 1=Y, 2=Cb, 3=Cr, 4=L, 5=Q */
	unsigned int h_sampling_factor;
	unsigned int v_sampling_factor;
	unsigned int quant_table_index;
};

#define MJPEG_MAX_COMPONENTS 5

struct mjpeg_header {
	unsigned int length;
	unsigned int sample_precision;
	unsigned int frame_width;
	unsigned int frame_height;
	unsigned int nb_of_components;
	struct mjpeg_component components[MJPEG_MAX_COMPONENTS];
};

int delta_mjpeg_read_header(struct delta_ctx *pctx,
			    unsigned char *data, unsigned int size,
			    struct mjpeg_header *header,
			    unsigned int *data_offset);

#endif /* DELTA_MJPEG_H */

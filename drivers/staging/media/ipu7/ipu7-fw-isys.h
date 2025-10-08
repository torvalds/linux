/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_FW_ISYS_H
#define IPU7_FW_ISYS_H

#include <linux/types.h>

#include "abi/ipu7_fw_isys_abi.h"

struct device;
struct ipu7_insys_buffset;
struct ipu7_insys_stream_cfg;
struct ipu7_isys;

/* From here on type defines not coming from the ISYSAPI interface */

int ipu7_fw_isys_init(struct ipu7_isys *isys);
void ipu7_fw_isys_release(struct ipu7_isys *isys);
int ipu7_fw_isys_open(struct ipu7_isys *isys);
int ipu7_fw_isys_close(struct ipu7_isys *isys);

void ipu7_fw_isys_dump_stream_cfg(struct device *dev,
				  struct ipu7_insys_stream_cfg *cfg);
void ipu7_fw_isys_dump_frame_buff_set(struct device *dev,
				      struct ipu7_insys_buffset *buf,
				      unsigned int outputs);
int ipu7_fw_isys_simple_cmd(struct ipu7_isys *isys,
			    const unsigned int stream_handle, u16 send_type);
int ipu7_fw_isys_complex_cmd(struct ipu7_isys *isys,
			     const unsigned int stream_handle,
			     void *cpu_mapped_buf,
			     dma_addr_t dma_mapped_buf,
			     size_t size, u16 send_type);
struct ipu7_insys_resp *ipu7_fw_isys_get_resp(struct ipu7_isys *isys);
void ipu7_fw_isys_put_resp(struct ipu7_isys *isys);
#endif

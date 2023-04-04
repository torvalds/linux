/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef HAB_PIPE_H
#define HAB_PIPE_H

struct hab_shared_buf {
	uint32_t rd_count; /* volatile cannot be used here */
	uint32_t wr_count; /* volatile cannot be used here */
	uint32_t size;
	unsigned char data[]; /* volatile cannot be used here */
};

/* debug only */
struct dbg_item {
	uint32_t rd_cnt;
	uint32_t wr_cnt;
	void *va; /* local for read or write */
	uint32_t index; /* local */
	uint32_t sz; /* size in */
	uint32_t ret; /* actual bytes read */
};

#define DBG_ITEM_SIZE 20

struct dbg_items {
	struct dbg_item it[DBG_ITEM_SIZE];
	int idx;
};

struct hab_pipe_endpoint {
	struct {
		uint32_t wr_count;
		uint32_t index;
		struct hab_shared_buf *legacy_sh_buf;
	} tx_info;
	struct {
		uint32_t index;
		struct hab_shared_buf *legacy_sh_buf;
	} rx_info;
};

struct hab_pipe {
	struct hab_pipe_endpoint top;
	struct hab_pipe_endpoint bottom;

	/* Legacy debugging metadata, replaced by dbg_itms from qvm_channel */
	struct hab_shared_buf *legacy_buf_a; /* top TX, bottom RX */
	struct hab_shared_buf *legacy_buf_b; /* top RX, bottom TX */
	size_t legacy_total_size;

	unsigned char buf_base[];
};

size_t hab_pipe_calc_required_bytes(const uint32_t shared_buf_size);

struct hab_pipe_endpoint *hab_pipe_init(struct hab_pipe *pipe,
		struct hab_shared_buf **tx_buf_p,
		struct hab_shared_buf **rx_buf_p,
		struct dbg_items **itms,
		const uint32_t shared_buf_size, int top);

uint32_t hab_pipe_write(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf,
		const uint32_t buf_size,
		unsigned char *p, uint32_t num_bytes);

void hab_pipe_write_commit(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf);

uint32_t hab_pipe_read(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf,
		const uint32_t buf_size,
		unsigned char *p, uint32_t size, uint32_t clear);

/* debug only */
void hab_pipe_rxinfo(struct hab_pipe_endpoint *ep,
		struct hab_shared_buf *sh_buf,
		uint32_t *rd_cnt,
		uint32_t *wr_cnt, uint32_t *idx);

#endif /* HAB_PIPE_H */

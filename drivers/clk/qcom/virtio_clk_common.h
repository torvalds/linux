/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __VIRTIO_CLK_COMMON__
#define __VIRTIO_CLK_COMMON__

#include <linux/types.h>

/*
 *
 */
struct virtio_clk_init_data {
	const char		*name;
	const char		* const *parent_names;
	u8			num_parents;
};

/*
 * struct clk_virtio_desc - virtio clock descriptor
 * clk_names: the pointer of clock name pointer
 * num_clks: number of clocks
 * reset_names: the pointer of reset name pointer
 * num_resets: number of resets
 */
struct clk_virtio_desc {
	const struct virtio_clk_init_data *clks;
	size_t num_clks;
	const char * const *reset_names;
	size_t num_resets;
};

extern const struct clk_virtio_desc clk_virtio_sm8150_gcc;
extern const struct clk_virtio_desc clk_virtio_sm8150_scc;
extern const struct clk_virtio_desc clk_virtio_sm6150_gcc;
extern const struct clk_virtio_desc clk_virtio_sm6150_scc;
extern const struct clk_virtio_desc clk_virtio_sa8195p_gcc;
extern const struct clk_virtio_desc clk_virtio_direwolf_gcc;
extern const struct clk_virtio_desc clk_virtio_lemans_gcc;
extern const struct clk_virtio_desc clk_virtio_monaco_gcc;
#endif

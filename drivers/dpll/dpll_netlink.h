/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (c) 2023 Meta Platforms, Inc. and affiliates
 *  Copyright (c) 2023 Intel and affiliates
 */

int dpll_device_create_ntf(struct dpll_device *dpll);

int dpll_device_delete_ntf(struct dpll_device *dpll);

int dpll_pin_create_ntf(struct dpll_pin *pin, u64 src_clock_id);

int dpll_pin_delete_ntf(struct dpll_pin *pin, u64 src_clock_id);

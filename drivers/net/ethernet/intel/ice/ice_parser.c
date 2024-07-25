// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024 Intel Corporation */

#include "ice_common.h"

/**
 * ice_parser_create - create a parser instance
 * @hw: pointer to the hardware structure
 *
 * Return: a pointer to the allocated parser instance or ERR_PTR
 * in case of error.
 */
struct ice_parser *ice_parser_create(struct ice_hw *hw)
{
	struct ice_parser *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->hw = hw;
	return p;
}

/**
 * ice_parser_destroy - destroy a parser instance
 * @psr: pointer to a parser instance
 */
void ice_parser_destroy(struct ice_parser *psr)
{
	kfree(psr);
}

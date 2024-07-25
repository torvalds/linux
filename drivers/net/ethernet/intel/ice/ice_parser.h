/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024 Intel Corporation */

#ifndef _ICE_PARSER_H_
#define _ICE_PARSER_H_

struct ice_parser {
	struct ice_hw *hw; /* pointer to the hardware structure */
};

struct ice_parser *ice_parser_create(struct ice_hw *hw);
void ice_parser_destroy(struct ice_parser *psr);
#endif /* _ICE_PARSER_H_ */

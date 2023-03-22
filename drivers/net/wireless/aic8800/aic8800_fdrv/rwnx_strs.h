/**
 ****************************************************************************************
 *
 * @file rwnx_strs.h
 *
 * @brief Miscellaneous debug strings
 *
 * Copyright (C) RivieraWaves 2014-2019
 *
 ****************************************************************************************
 */

#ifndef _RWNX_STRS_H_
#define _RWNX_STRS_H_

#ifdef CONFIG_RWNX_FHOST

#define RWNX_ID2STR(tag) "Cmd"

#else
#include "lmac_msg.h"

#define RWNX_ID2STR(tag) (((MSG_T(tag) < ARRAY_SIZE(rwnx_id2str)) &&        \
                           (rwnx_id2str[MSG_T(tag)]) &&          \
                           ((rwnx_id2str[MSG_T(tag)])[MSG_I(tag)])) ?   \
                          (rwnx_id2str[MSG_T(tag)])[MSG_I(tag)] : "unknown")

extern const char *const *rwnx_id2str[TASK_LAST_EMB + 1];
#endif /* CONFIG_RWNX_FHOST */

#endif /* _RWNX_STRS_H_ */

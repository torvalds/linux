/**
 ****************************************************************************************
 *
 * @file ecrnx_strs.h
 *
 * @brief Miscellaneous debug strings
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef _ECRNX_STRS_H_
#define _ECRNX_STRS_H_

#ifdef CONFIG_ECRNX_FHOST

#define ECRNX_ID2STR(tag) "Cmd"

#else
#include "lmac_msg.h"

#define ECRNX_ID2STR(tag) (((MSG_T(tag) < ARRAY_SIZE(ecrnx_id2str)) &&        \
                           (ecrnx_id2str[MSG_T(tag)]) &&          \
                           ((ecrnx_id2str[MSG_T(tag)])[MSG_I(tag)])) ?   \
                          (ecrnx_id2str[MSG_T(tag)])[MSG_I(tag)] : "unknown")

extern const char *const *ecrnx_id2str[TASK_LAST_EMB + 1];
#endif /* CONFIG_ECRNX_FHOST */

#endif /* _ECRNX_STRS_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2021  ASPEED Technology Inc.
 */

#ifndef AST_VIDEO_DEBUG_H_
#define AST_VIDEO_DEBUG_H_

#include <linux/string.h>
#include <linux/types.h>
#include <linux/fcntl.h>

//#define RVAS_VIDEO_DEBUG
//#define VIDEO_ENGINE_DEBUG
//#define HARDWARE_ENGINE_DEBUG

#ifdef RVAS_VIDEO_DEBUG
#define VIDEO_DBG(fmt, args...) ({ dev_printk(KERNEL_INFO, pAstRVAS->pdev, "%s() " fmt, __func__, ## args); })
#else
#define VIDEO_DBG(fmt, args...) do; while (0)
#endif // RVAS_VIDEO_DEBUG

#ifdef VIDEO_ENGINE_DEBUG
#define VIDEO_ENG_DBG(fmt, args...) ({ dev_printk(KERNEL_INFO, pAstRVAS->pdev, "%s() " fmt, __func__, ## args); })
#else
#define VIDEO_ENG_DBG(fmt, args...) do; while (0)
#endif // RVAS_VIDEO_DEBUG

#endif // AST_VIDEO_DEBUG_H_

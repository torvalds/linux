/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __QUEUE_H_INCLUDED__
#define __QUEUE_H_INCLUDED__

/*
 * This file is included on every cell {SP,ISP,host} and is system agnostic
 *
 * System and cell specific interfaces and inline code are included
 * conditionally through Makefile path settings.
 *
 *  - system and cell agnostic interfaces, constants and identifiers
 *	- public:  cell specific interfaces
 *	- private: cell specific inline implementations
 *	- global:  inter cell constants and identifiers
 *	- local:   cell specific constants and identifiers
 *
 */

#include "queue_local.h"

#ifndef __INLINE_QUEUE__
#define STORAGE_CLASS_QUEUE_H extern
#define STORAGE_CLASS_QUEUE_C
/* #include "queue_public.h" */
#include "ia_css_queue.h"
#else  /* __INLINE_QUEUE__ */
#define STORAGE_CLASS_QUEUE_H static inline
#define STORAGE_CLASS_QUEUE_C static inline
#include "queue_private.h"
#endif /* __INLINE_QUEUE__ */

#endif /* __QUEUE_H_INCLUDED__ */

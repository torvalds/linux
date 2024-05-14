// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PS3 hvcall exports for modules.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 */

#define LV1_CALL(name, in, out, num)                          \
  extern s64 _lv1_##name(LV1_##in##_IN_##out##_OUT_ARG_DECL); \
  EXPORT_SYMBOL(_lv1_##name);

#include <asm/lv1call.h>

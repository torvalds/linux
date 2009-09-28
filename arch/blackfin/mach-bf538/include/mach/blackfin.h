/*
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#define BF538_FAMILY

#include "bf538.h"
#include "defBF539.h"
#include "anomaly.h"


#if !defined(__ASSEMBLY__)
#include "cdefBF538.h"

#if defined(CONFIG_BF539)
#include "cdefBF539.h"
#endif
#endif

#endif

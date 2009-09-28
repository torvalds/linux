/*
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _MACH_BLACKFIN_H_
#define _MACH_BLACKFIN_H_

#include "bf527.h"
#include "defBF522.h"
#include "anomaly.h"

#if defined(CONFIG_BF527) || defined(CONFIG_BF526)
#include "defBF527.h"
#endif

#if defined(CONFIG_BF525) || defined(CONFIG_BF524)
#include "defBF525.h"
#endif

#if !defined(__ASSEMBLY__)
#include "cdefBF522.h"

#if defined(CONFIG_BF527) || defined(CONFIG_BF526)
#include "cdefBF527.h"
#endif

#if defined(CONFIG_BF525) || defined(CONFIG_BF524)
#include "cdefBF525.h"
#endif
#endif

#endif

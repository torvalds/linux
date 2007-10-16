/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __CHOOSE_MODE_H__
#define __CHOOSE_MODE_H__

#include "uml-config.h"

#define CHOOSE_MODE(tt, skas) (skas)

#define CHOOSE_MODE_PROC(tt, skas, args...) \
	CHOOSE_MODE(tt(args), skas(args))

#ifndef __CHOOSE_MODE
#define __CHOOSE_MODE(tt, skas) CHOOSE_MODE(tt, skas)
#endif

#endif

/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __CHOOSE_MODE_H__
#define __CHOOSE_MODE_H__

#include "uml-config.h"

#if defined(UML_CONFIG_MODE_TT) && defined(UML_CONFIG_MODE_SKAS)
#define CHOOSE_MODE(tt, skas) (mode_tt ? (tt) : (skas))

extern int mode_tt;
static inline void *__choose_mode(void *tt, void *skas) {
	return mode_tt ? tt : skas;
}

#define __CHOOSE_MODE(tt, skas) (*( (typeof(tt) *) __choose_mode(&(tt), &(skas))))

#elif defined(UML_CONFIG_MODE_SKAS)
#define CHOOSE_MODE(tt, skas) (skas)

#elif defined(UML_CONFIG_MODE_TT)
#define CHOOSE_MODE(tt, skas) (tt)
#endif

#define CHOOSE_MODE_PROC(tt, skas, args...) \
	CHOOSE_MODE(tt(args), skas(args))

#ifndef __CHOOSE_MODE
#define __CHOOSE_MODE(tt, skas) CHOOSE_MODE(tt, skas)
#endif

#endif

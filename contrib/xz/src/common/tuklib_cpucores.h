///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_cpucores.h
/// \brief      Get the number of CPU cores online
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_CPUCORES_H
#define TUKLIB_CPUCORES_H

#include "tuklib_common.h"
TUKLIB_DECLS_BEGIN

#define tuklib_cpucores TUKLIB_SYMBOL(tuklib_cpucores)
extern uint32_t tuklib_cpucores(void);

TUKLIB_DECLS_END
#endif

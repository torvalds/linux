///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_physmem.h
/// \brief      Get the amount of physical memory
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_PHYSMEM_H
#define TUKLIB_PHYSMEM_H

#include "tuklib_common.h"
TUKLIB_DECLS_BEGIN

#define tuklib_physmem TUKLIB_SYMBOL(tuklib_physmem)
extern uint64_t tuklib_physmem(void);
///<
/// \brief      Get the amount of physical memory in bytes
///
/// \return     Amount of physical memory in bytes. On error, zero is
///             returned.

TUKLIB_DECLS_END
#endif

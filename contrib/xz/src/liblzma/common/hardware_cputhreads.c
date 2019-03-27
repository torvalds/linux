///////////////////////////////////////////////////////////////////////////////
//
/// \file       hardware_cputhreads.c
/// \brief      Get the number of CPU threads or cores
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"

#include "tuklib_cpucores.h"


extern LZMA_API(uint32_t)
lzma_cputhreads(void)
{
	return tuklib_cpucores();
}

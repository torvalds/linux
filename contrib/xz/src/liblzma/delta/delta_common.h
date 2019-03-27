///////////////////////////////////////////////////////////////////////////////
//
/// \file       delta_common.h
/// \brief      Common stuff for Delta encoder and decoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_DELTA_COMMON_H
#define LZMA_DELTA_COMMON_H

#include "common.h"

extern uint64_t lzma_delta_coder_memusage(const void *options);

#endif

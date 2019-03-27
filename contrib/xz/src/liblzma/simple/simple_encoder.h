///////////////////////////////////////////////////////////////////////////////
//
/// \file       simple_encoder.c
/// \brief      Properties encoder for simple filters
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_SIMPLE_ENCODER_H
#define LZMA_SIMPLE_ENCODER_H

#include "simple_coder.h"


extern lzma_ret lzma_simple_props_size(uint32_t *size, const void *options);

extern lzma_ret lzma_simple_props_encode(const void *options, uint8_t *out);

#endif

///////////////////////////////////////////////////////////////////////////////
//
/// \file       alone_decoder.h
/// \brief      Decoder for LZMA_Alone files
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_ALONE_DECODER_H
#define LZMA_ALONE_DECODER_H

#include "common.h"


extern lzma_ret lzma_alone_decoder_init(
		lzma_next_coder *next, const lzma_allocator *allocator,
		uint64_t memlimit, bool picky);

#endif

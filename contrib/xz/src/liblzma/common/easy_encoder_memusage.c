///////////////////////////////////////////////////////////////////////////////
//
/// \file       easy_encoder_memusage.c
/// \brief      Easy .xz Stream encoder memory usage calculation
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "easy_preset.h"


extern LZMA_API(uint64_t)
lzma_easy_encoder_memusage(uint32_t preset)
{
	lzma_options_easy opt_easy;
	if (lzma_easy_preset(&opt_easy, preset))
		return UINT32_MAX;

	return lzma_raw_encoder_memusage(opt_easy.filters);
}

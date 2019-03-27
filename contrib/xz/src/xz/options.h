///////////////////////////////////////////////////////////////////////////////
//
/// \file       options.h
/// \brief      Parser for filter-specific options
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/// \brief      Parser for Delta options
///
/// \return     Pointer to allocated options structure.
///             Doesn't return on error.
extern lzma_options_delta *options_delta(const char *str);


/// \brief      Parser for BCJ options
///
/// \return     Pointer to allocated options structure.
///             Doesn't return on error.
extern lzma_options_bcj *options_bcj(const char *str);


/// \brief      Parser for LZMA options
///
/// \return     Pointer to allocated options structure.
///             Doesn't return on error.
extern lzma_options_lzma *options_lzma(const char *str);

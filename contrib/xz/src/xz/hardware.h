///////////////////////////////////////////////////////////////////////////////
//
/// \file       hardware.h
/// \brief      Detection of available hardware resources
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/// Initialize some hardware-specific variables, which are needed by other
/// hardware_* functions.
extern void hardware_init(void);


/// Set the maximum number of worker threads.
extern void hardware_threads_set(uint32_t threadlimit);

/// Get the maximum number of worker threads.
extern uint32_t hardware_threads_get(void);


/// Set the memory usage limit. There are separate limits for compression
/// and decompression (the latter includes also --list), one or both can
/// be set with a single call to this function. Zero indicates resetting
/// the limit back to the defaults. The limit can also be set as a percentage
/// of installed RAM; the percentage must be in the range [1, 100].
extern void hardware_memlimit_set(uint64_t new_memlimit,
		bool set_compress, bool set_decompress, bool is_percentage);

/// Get the current memory usage limit for compression or decompression.
extern uint64_t hardware_memlimit_get(enum operation_mode mode);

/// Display the amount of RAM and memory usage limits and exit.
extern void hardware_memlimit_show(void) lzma_attribute((__noreturn__));

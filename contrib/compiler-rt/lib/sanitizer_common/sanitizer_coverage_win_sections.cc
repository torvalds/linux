//===-- sanitizer_coverage_win_sections.cc --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines delimiters for Sanitizer Coverage's section. It contains
// Windows specific tricks to coax the linker into giving us the start and stop
// addresses of a section, as ELF linkers can do, to get the size of certain
// arrays. According to https://msdn.microsoft.com/en-us/library/7977wcck.aspx
// sections with the same name before "$" are sorted alphabetically by the
// string that comes after "$" and merged into one section. We take advantage
// of this by putting data we want the size of into the middle (M) of a section,
// by using the letter "M" after "$". We get the start of this data (ie:
// __start_section_name) by making the start variable come at the start of the
// section (using the letter A after "$"). We do the same to get the end of the
// data by using the letter "Z" after "$" to make the end variable come after
// the data. Note that because of our technique the address of the start
// variable is actually the address of data that comes before our middle
// section. We also need to prevent the linker from adding any padding. Each
// technique we use for this is explained in the comments below.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS
#include <stdint.h>
extern "C" {
// The Guard array and counter array should both be merged into the .data
// section to reduce the number of PE sections However, because PCTable is
// constant it should be merged with the .rdata section.
#pragma section(".SCOV$GA", read, write)  // NOLINT
// Use align(1) to avoid adding any padding that will mess up clients trying to
// determine the start and end of the array.
__declspec(allocate(".SCOV$GA")) __declspec(align(1)) uint64_t
    __start___sancov_guards = 0;
#pragma section(".SCOV$GZ", read, write)  // NOLINT
__declspec(allocate(".SCOV$GZ")) __declspec(align(1)) uint64_t
    __stop___sancov_guards = 0;

#pragma section(".SCOV$CA", read, write)  // NOLINT
__declspec(allocate(".SCOV$CA")) __declspec(align(1)) uint64_t
    __start___sancov_cntrs = 0;
#pragma section(".SCOV$CZ", read, write)  // NOLINT
__declspec(allocate(".SCOV$CZ")) __declspec(align(1)) uint64_t
    __stop___sancov_cntrs = 0;

#pragma comment(linker, "/MERGE:.SCOV=.data")

// Use uint64_t so there won't be any issues if the linker tries to word align
// the pc array.
#pragma section(".SCOVP$A", read)  // NOLINT
__declspec(allocate(".SCOVP$A")) __declspec(align(1)) uint64_t
    __start___sancov_pcs = 0;
#pragma section(".SCOVP$Z", read)  // NOLINT
__declspec(allocate(".SCOVP$Z")) __declspec(align(1)) uint64_t
    __stop___sancov_pcs = 0;

#pragma comment(linker, "/MERGE:.SCOVP=.rdata")
}
#endif  // SANITIZER_WINDOWS

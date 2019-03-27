///////////////////////////////////////////////////////////////////////////////
//
/// \file       suffix.h
/// \brief      Checks filename suffix and creates the destination filename
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/// \brief      Get the name of the destination file
///
/// Depending on the global variable opt_mode, this tries to find a matching
/// counterpart for src_name. If the name can be constructed, it is allocated
/// and returned (caller must free it). On error, a message is printed and
/// NULL is returned.
extern char *suffix_get_dest_name(const char *src_name);


/// \brief      Set a custom filename suffix
///
/// This function calls xstrdup() for the given suffix, thus the caller
/// doesn't need to keep the memory allocated. There can be only one custom
/// suffix, thus if this is called multiple times, the old suffixes are freed
/// and forgotten.
extern void suffix_set(const char *suffix);

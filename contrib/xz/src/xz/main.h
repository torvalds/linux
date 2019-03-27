///////////////////////////////////////////////////////////////////////////////
//
/// \file       main.h
/// \brief      Miscellaneous declarations
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/// Possible exit status values. These are the same as used by gzip and bzip2.
enum exit_status_type {
	E_SUCCESS  = 0,
	E_ERROR    = 1,
	E_WARNING  = 2,
};


/// Sets the exit status after a warning or error has occurred. If new_status
/// is E_WARNING and the old exit status was already E_ERROR, the exit
/// status is not changed.
extern void set_exit_status(enum exit_status_type new_status);


/// Use E_SUCCESS instead of E_WARNING if something worth a warning occurs
/// but nothing worth an error has occurred. This is called when --no-warn
/// is specified.
extern void set_exit_no_warn(void);

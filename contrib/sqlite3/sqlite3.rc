/*
** 2012 September 2
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file contains code and resources that are specific to Windows.
*/

#if !defined(_WIN32_WCE)
#include "winresrc.h"
#else
#include "windows.h"
#endif /* !defined(_WIN32_WCE) */

#if !defined(VS_FF_NONE)
#  define VS_FF_NONE            0x00000000L
#endif /* !defined(VS_FF_NONE) */

#include "sqlite3.h"
#include "sqlite3rc.h"

/*
 * English (U.S.) resources
 */

#if defined(_WIN32)
LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US
#pragma code_page(1252)
#endif /* defined(_WIN32) */

/*
 * Icon
 */

#if !defined(RC_VERONLY)
#define IDI_SQLITE 101

IDI_SQLITE ICON "..\\art\\sqlite370.ico"
#endif /* !defined(RC_VERONLY) */

/*
 * Version
 */

VS_VERSION_INFO VERSIONINFO
  FILEVERSION SQLITE_RESOURCE_VERSION
  PRODUCTVERSION SQLITE_RESOURCE_VERSION
  FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
#if defined(_DEBUG)
  FILEFLAGS VS_FF_DEBUG
#else
  FILEFLAGS VS_FF_NONE
#endif /* defined(_DEBUG) */
  FILEOS VOS__WINDOWS32
  FILETYPE VFT_DLL
  FILESUBTYPE VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "SQLite Development Team"
      VALUE "FileDescription", "SQLite is a software library that implements a self-contained, serverless, zero-configuration, transactional SQL database engine."
      VALUE "FileVersion", SQLITE_VERSION
      VALUE "InternalName", "sqlite3"
      VALUE "LegalCopyright", "http://www.sqlite.org/copyright.html"
      VALUE "ProductName", "SQLite"
      VALUE "ProductVersion", SQLITE_VERSION
      VALUE "SourceId", SQLITE_SOURCE_ID
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 0x4b0
  END
END

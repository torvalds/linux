/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 2016 - 2017, Steve Holme, <steve_holme@hotmail.com>.
 * Copyright (C) 2017, Expat development team
 *
 * All rights reserved.
 * Licensed under the MIT license:
 *
 * Permission to  use, copy,  modify, and distribute  this software  for any
 * purpose with  or without fee is  hereby granted, provided that  the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
 * EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
 * MERCHANTABILITY, FITNESS FOR A  PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR  OTHERWISE, ARISING FROM, OUT OF OR  IN CONNECTION WITH
 * THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice,  the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings  in this  Software without  prior written  authorization of  the
 * copyright holder.
 *
 ***************************************************************************/

#if defined(_WIN32)

#include <windows.h>
#include <tchar.h>


HMODULE _Expat_LoadLibrary(LPCTSTR filename);


#if !defined(LOAD_WITH_ALTERED_SEARCH_PATH)
#define LOAD_WITH_ALTERED_SEARCH_PATH  0x00000008
#endif

#if !defined(LOAD_LIBRARY_SEARCH_SYSTEM32)
#define LOAD_LIBRARY_SEARCH_SYSTEM32   0x00000800
#endif

/* We use our own typedef here since some headers might lack these */
typedef HMODULE (APIENTRY *LOADLIBRARYEX_FN)(LPCTSTR, HANDLE, DWORD);

/* See function definitions in winbase.h */
#ifdef UNICODE
#  ifdef _WIN32_WCE
#    define LOADLIBARYEX  L"LoadLibraryExW"
#  else
#    define LOADLIBARYEX  "LoadLibraryExW"
#  endif
#else
#  define LOADLIBARYEX    "LoadLibraryExA"
#endif


/*
 * _Expat_LoadLibrary()
 *
 * This is used to dynamically load DLLs using the most secure method available
 * for the version of Windows that we are running on.
 *
 * Parameters:
 *
 * filename  [in] - The filename or full path of the DLL to load. If only the
 *                  filename is passed then the DLL will be loaded from the
 *                  Windows system directory.
 *
 * Returns the handle of the module on success; otherwise NULL.
 */
HMODULE _Expat_LoadLibrary(LPCTSTR filename)
{
  HMODULE hModule = NULL;
  LOADLIBRARYEX_FN pLoadLibraryEx = NULL;

  /* Get a handle to kernel32 so we can access it's functions at runtime */
  HMODULE hKernel32 = GetModuleHandle(TEXT("kernel32"));
  if(!hKernel32)
    return NULL;  /* LCOV_EXCL_LINE */

  /* Attempt to find LoadLibraryEx() which is only available on Windows 2000
     and above */
  pLoadLibraryEx = (LOADLIBRARYEX_FN) GetProcAddress(hKernel32, LOADLIBARYEX);

  /* Detect if there's already a path in the filename and load the library if
     there is. Note: Both back slashes and forward slashes have been supported
     since the earlier days of DOS at an API level although they are not
     supported by command prompt */
  if(_tcspbrk(filename, TEXT("\\/"))) {
    /** !checksrc! disable BANNEDFUNC 1 **/
    hModule = pLoadLibraryEx ?
      pLoadLibraryEx(filename, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) :
      LoadLibrary(filename);
  }
  /* Detect if KB2533623 is installed, as LOAD_LIBARY_SEARCH_SYSTEM32 is only
     supported on Windows Vista, Windows Server 2008, Windows 7 and Windows
     Server 2008 R2 with this patch or natively on Windows 8 and above */
  else if(pLoadLibraryEx && GetProcAddress(hKernel32, "AddDllDirectory")) {
    /* Load the DLL from the Windows system directory */
    hModule = pLoadLibraryEx(filename, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  else {
    /* Attempt to get the Windows system path */
    UINT systemdirlen = GetSystemDirectory(NULL, 0);
    if(systemdirlen) {
      /* Allocate space for the full DLL path (Room for the null terminator
         is included in systemdirlen) */
      size_t filenamelen = _tcslen(filename);
      TCHAR *path = malloc(sizeof(TCHAR) * (systemdirlen + 1 + filenamelen));
      if(path && GetSystemDirectory(path, systemdirlen)) {
        /* Calculate the full DLL path */
        _tcscpy(path + _tcslen(path), TEXT("\\"));
        _tcscpy(path + _tcslen(path), filename);

        /* Load the DLL from the Windows system directory */
        /** !checksrc! disable BANNEDFUNC 1 **/
        hModule = pLoadLibraryEx ?
          pLoadLibraryEx(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) :
          LoadLibrary(path);

      }
      free(path);
    }
  }

  return hModule;
}

#else /* defined(_WIN32) */

/* ISO C requires a translation unit to contain at least one declaration
   [-Wempty-translation-unit] */
typedef int _TRANSLATION_UNIT_LOAD_LIBRARY_C_NOT_EMTPY;

#endif /* defined(_WIN32) */

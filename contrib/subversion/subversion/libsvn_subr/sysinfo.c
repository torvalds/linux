/*
 * sysinfo.c :  information about the running system
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_lib.h>
#include <apr_pools.h>
#include <apr_file_info.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_version.h>
#include <apu_version.h>

#include "svn_pools.h"
#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "private/svn_sqlite.h"
#include "private/svn_subr_private.h"
#include "private/svn_utf_private.h"

#include "sysinfo.h"
#include "svn_private_config.h"

#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#ifdef SVN_HAVE_MACOS_PLIST
#include <CoreFoundation/CoreFoundation.h>
#include <AvailabilityMacros.h>
# ifndef MAC_OS_X_VERSION_10_6
#  define MAC_OS_X_VERSION_10_6  1060
# endif
#endif

#ifdef SVN_HAVE_MACHO_ITERATE
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif

#if HAVE_UNAME
static const char *canonical_host_from_uname(apr_pool_t *pool);
# ifndef SVN_HAVE_MACOS_PLIST
static const char *release_name_from_uname(apr_pool_t *pool);
# endif
#endif

#ifdef WIN32
static const char *win32_canonical_host(apr_pool_t *pool);
static const char *win32_release_name(apr_pool_t *pool);
static const apr_array_header_t *win32_shared_libs(apr_pool_t *pool);
#endif /* WIN32 */

#ifdef SVN_HAVE_MACOS_PLIST
static const char *macos_release_name(apr_pool_t *pool);
#endif

#ifdef SVN_HAVE_MACHO_ITERATE
static const apr_array_header_t *macos_shared_libs(apr_pool_t *pool);
#endif


#if __linux__
static const char *linux_release_name(apr_pool_t *pool);
#endif

const char *
svn_sysinfo__canonical_host(apr_pool_t *pool)
{
#ifdef WIN32
  return win32_canonical_host(pool);
#elif HAVE_UNAME
  return canonical_host_from_uname(pool);
#else
  return "unknown-unknown-unknown";
#endif
}


const char *
svn_sysinfo__release_name(apr_pool_t *pool)
{
#ifdef WIN32
  return win32_release_name(pool);
#elif defined(SVN_HAVE_MACOS_PLIST)
  return macos_release_name(pool);
#elif __linux__
  return linux_release_name(pool);
#elif HAVE_UNAME
  return release_name_from_uname(pool);
#else
  return NULL;
#endif
}

const apr_array_header_t *
svn_sysinfo__linked_libs(apr_pool_t *pool)
{
  svn_version_ext_linked_lib_t *lib;
  apr_array_header_t *array = apr_array_make(pool, 7, sizeof(*lib));
  int lz4_version = svn_lz4__runtime_version();

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "APR";
  lib->compiled_version = APR_VERSION_STRING;
  lib->runtime_version = apr_pstrdup(pool, apr_version_string());

/* Don't list APR-Util if it isn't linked in, which it may not be if
 * we're using APR 2.x+ which combined APR-Util into APR. */
#ifdef APU_VERSION_STRING
  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "APR-Util";
  lib->compiled_version = APU_VERSION_STRING;
  lib->runtime_version = apr_pstrdup(pool, apu_version_string());
#endif

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "Expat";
  lib->compiled_version = apr_pstrdup(pool, svn_xml__compiled_version());
  lib->runtime_version = apr_pstrdup(pool, svn_xml__runtime_version());

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "SQLite";
  lib->compiled_version = apr_pstrdup(pool, svn_sqlite__compiled_version());
#ifdef SVN_SQLITE_INLINE
  lib->runtime_version = NULL;
#else
  lib->runtime_version = apr_pstrdup(pool, svn_sqlite__runtime_version());
#endif

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "Utf8proc";
  lib->compiled_version = apr_pstrdup(pool, svn_utf__utf8proc_compiled_version());
  lib->runtime_version = apr_pstrdup(pool, svn_utf__utf8proc_runtime_version());

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "ZLib";
  lib->compiled_version = apr_pstrdup(pool, svn_zlib__compiled_version());
  lib->runtime_version = apr_pstrdup(pool, svn_zlib__runtime_version());

  lib = &APR_ARRAY_PUSH(array, svn_version_ext_linked_lib_t);
  lib->name = "LZ4";
  lib->compiled_version = apr_pstrdup(pool, svn_lz4__compiled_version());

  lib->runtime_version = apr_psprintf(pool, "%d.%d.%d",
                                      lz4_version / 100 / 100,
                                      (lz4_version / 100) % 100,
                                      lz4_version % 100);

  return array;
}

const apr_array_header_t *
svn_sysinfo__loaded_libs(apr_pool_t *pool)
{
#ifdef WIN32
  return win32_shared_libs(pool);
#elif defined(SVN_HAVE_MACHO_ITERATE)
  return macos_shared_libs(pool);
#else
  return NULL;
#endif
}


#if HAVE_UNAME
static const char*
canonical_host_from_uname(apr_pool_t *pool)
{
  const char *machine = "unknown";
  const char *vendor = "unknown";
  const char *sysname = "unknown";
  const char *sysver = "";
  struct utsname info;

  if (0 <= uname(&info))
    {
      svn_error_t *err;
      const char *tmp;

      err = svn_utf_cstring_to_utf8(&tmp, info.machine, pool);
      if (err)
        svn_error_clear(err);
      else
        machine = tmp;

      err = svn_utf_cstring_to_utf8(&tmp, info.sysname, pool);
      if (err)
        svn_error_clear(err);
      else
        {
          char *lwr = apr_pstrdup(pool, tmp);
          char *it = lwr;
          while (*it)
            {
              if (svn_ctype_isupper(*it))
                *it = apr_tolower(*it);
              ++it;
            }
          sysname = lwr;
        }

      if (0 == strcmp(sysname, "darwin"))
        vendor = "apple";
      if (0 == strcmp(sysname, "linux"))
        sysver = "-gnu";
      else
        {
          err = svn_utf_cstring_to_utf8(&tmp, info.release, pool);
          if (err)
            svn_error_clear(err);
          else
            {
              apr_size_t n = strspn(tmp, ".0123456789");
              if (n > 0)
                {
                  char *ver = apr_pstrdup(pool, tmp);
                  ver[n] = 0;
                  sysver = ver;
                }
              else
                sysver = tmp;
            }
        }
    }

  return apr_psprintf(pool, "%s-%s-%s%s", machine, vendor, sysname, sysver);
}

# ifndef SVN_HAVE_MACOS_PLIST
/* Generate a release name from the uname(3) info, effectively
   returning "`uname -s` `uname -r`". */
static const char *
release_name_from_uname(apr_pool_t *pool)
{
  struct utsname info;
  if (0 <= uname(&info))
    {
      svn_error_t *err;
      const char *sysname;
      const char *sysver;

      err = svn_utf_cstring_to_utf8(&sysname, info.sysname, pool);
      if (err)
        {
          sysname = NULL;
          svn_error_clear(err);
        }


      err = svn_utf_cstring_to_utf8(&sysver, info.release, pool);
      if (err)
        {
          sysver = NULL;
          svn_error_clear(err);
        }

      if (sysname || sysver)
        {
          return apr_psprintf(pool, "%s%s%s",
                              (sysname ? sysname : ""),
                              (sysver ? (sysname ? " " : "") : ""),
                              (sysver ? sysver : ""));
        }
    }
  return NULL;
}
# endif  /* !SVN_HAVE_MACOS_PLIST */
#endif  /* HAVE_UNAME */


#if __linux__
/* Split a stringbuf into a key/value pair.
   Return the key, leaving the stripped value in the stringbuf. */
static const char *
stringbuf_split_key(svn_stringbuf_t *buffer, char delim)
{
  char *key;
  char *end;

  end = strchr(buffer->data, delim);
  if (!end)
    return NULL;

  svn_stringbuf_strip_whitespace(buffer);

  /* Now we split the currently allocated buffer in two parts:
      - a const char * HEAD
      - the remaining stringbuf_t. */

  /* Create HEAD as '\0' terminated const char * */
  key = buffer->data;
  end = strchr(key, delim);
  *end = '\0';

  /* And update the TAIL to be a smaller, but still valid stringbuf */
  buffer->data = end + 1;
  buffer->len -= 1 + end - key;
  buffer->blocksize -= 1 + end - key;

  svn_stringbuf_strip_whitespace(buffer);

  return key;
}

/* Parse `/usr/bin/lsb_rlease --all` */
static const char *
lsb_release(apr_pool_t *pool)
{
  static const char *const args[3] =
    {
      "/usr/bin/lsb_release",
      "--all",
      NULL
    };

  const char *distributor = NULL;
  const char *description = NULL;
  const char *release = NULL;
  const char *codename = NULL;

  apr_proc_t lsbproc;
  svn_stream_t *lsbinfo;
  svn_error_t *err;

  /* Run /usr/bin/lsb_release --all < /dev/null 2>/dev/null */
  {
    apr_file_t *stdin_handle;
    apr_file_t *stdout_handle;

    err = svn_io_file_open(&stdin_handle, SVN_NULL_DEVICE_NAME,
                           APR_READ, APR_OS_DEFAULT, pool);
    if (!err)
      err = svn_io_file_open(&stdout_handle, SVN_NULL_DEVICE_NAME,
                             APR_WRITE, APR_OS_DEFAULT, pool);
    if (!err)
      err = svn_io_start_cmd3(&lsbproc, NULL, args[0], args, NULL, FALSE,
                              FALSE, stdin_handle,
                              TRUE, NULL,
                              FALSE, stdout_handle,
                              pool);
    if (err)
      {
        svn_error_clear(err);
        return NULL;
      }
  }

  /* Parse the output and try to populate the  */
  lsbinfo = svn_stream_from_aprfile2(lsbproc.out, TRUE, pool);
  if (lsbinfo)
    {
      for (;;)
        {
          svn_boolean_t eof = FALSE;
          svn_stringbuf_t *line;
          const char *key;

          err = svn_stream_readline(lsbinfo, &line, "\n", &eof, pool);
          if (err || eof)
            break;

          key = stringbuf_split_key(line, ':');
          if (!key)
            continue;

          if (0 == svn_cstring_casecmp(key, "Distributor ID"))
            distributor = line->data;
          else if (0 == svn_cstring_casecmp(key, "Description"))
            description = line->data;
          else if (0 == svn_cstring_casecmp(key, "Release"))
            release = line->data;
          else if (0 == svn_cstring_casecmp(key, "Codename"))
            codename = line->data;
        }
      err = svn_error_compose_create(err,
                                     svn_stream_close(lsbinfo));
      if (err)
        {
          svn_error_clear(err);
          apr_proc_kill(&lsbproc, SIGKILL);
          return NULL;
        }
    }

  /* Reap the child process */
  err = svn_io_wait_for_cmd(&lsbproc, "", NULL, NULL, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

  if (description)
    return apr_psprintf(pool, "%s%s%s%s", description,
                        (codename ? " (" : ""),
                        (codename ? codename : ""),
                        (codename ? ")" : ""));
  if (distributor)
    return apr_psprintf(pool, "%s%s%s%s%s%s", distributor,
                        (release ? " " : ""),
                        (release ? release : ""),
                        (codename ? " (" : ""),
                        (codename ? codename : ""),
                        (codename ? ")" : ""));

  return NULL;
}

/* Read /etc/os-release, as documented here:
 * http://www.freedesktop.org/software/systemd/man/os-release.html
 */
static const char *
systemd_release(apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stream_t *stream;

  /* Open the file. */
  err = svn_stream_open_readonly(&stream, "/etc/os-release", pool, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      err = svn_stream_open_readonly(&stream, "/usr/lib/os-release", pool,
                                     pool);
    }
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

  /* Look for the PRETTY_NAME line. */
  while (TRUE)
    {
      svn_stringbuf_t *line;
      svn_boolean_t eof;

      err = svn_stream_readline(stream, &line, "\n", &eof, pool);
      if (err)
        {
          svn_error_clear(err);
          return NULL;
        }

      if (!strncmp(line->data, "PRETTY_NAME=", 12))
        {
          svn_stringbuf_t *release_name;

          /* The value may or may not be enclosed by double quotes.  We don't
           * attempt to strip them. */
          release_name = svn_stringbuf_create(line->data + 12, pool);
          svn_error_clear(svn_stream_close(stream));
          svn_stringbuf_strip_whitespace(release_name);
          return release_name->data;
        }

      if (eof)
        break;
    }

  /* The file did not contain a PRETTY_NAME line. */
  svn_error_clear(svn_stream_close(stream));
  return NULL;
}

/* Read the whole contents of a file. */
static svn_stringbuf_t *
read_file_contents(const char *filename, apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stringbuf_t *buffer;

  err = svn_stringbuf_from_file2(&buffer, filename, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

  return buffer;
}

/* Strip everything but the first line from a stringbuf. */
static void
stringbuf_first_line_only(svn_stringbuf_t *buffer)
{
  char *eol = strchr(buffer->data, '\n');
  if (eol)
    {
      *eol = '\0';
      buffer->len = 1 + eol - buffer->data;
    }
  svn_stringbuf_strip_whitespace(buffer);
}

/* Look at /etc/redhat_release to detect RHEL/Fedora/CentOS. */
static const char *
redhat_release(apr_pool_t *pool)
{
  svn_stringbuf_t *buffer = read_file_contents("/etc/redhat-release", pool);
  if (buffer)
    {
      stringbuf_first_line_only(buffer);
      return buffer->data;
    }
  return NULL;
}

/* Look at /etc/SuSE-release to detect non-LSB SuSE. */
static const char *
suse_release(apr_pool_t *pool)
{
  const char *release = NULL;
  const char *codename = NULL;

  svn_stringbuf_t *buffer = read_file_contents("/etc/SuSE-release", pool);
  svn_stringbuf_t *line;
  svn_stream_t *stream;
  svn_boolean_t eof;
  svn_error_t *err;
  if (!buffer)
      return NULL;

  stream = svn_stream_from_stringbuf(buffer, pool);
  err = svn_stream_readline(stream, &line, "\n", &eof, pool);
  if (err || eof)
    {
      svn_error_clear(err);
      return NULL;
    }

  svn_stringbuf_strip_whitespace(line);
  release = line->data;

  for (;;)
    {
      const char *key;

      err = svn_stream_readline(stream, &line, "\n", &eof, pool);
      if (err || eof)
        {
          svn_error_clear(err);
          break;
        }

      key = stringbuf_split_key(line, '=');
      if (!key)
        continue;

      if (0 == strncmp(key, "CODENAME", 8))
        codename = line->data;
    }

  return apr_psprintf(pool, "%s%s%s%s",
                      release,
                      (codename ? " (" : ""),
                      (codename ? codename : ""),
                      (codename ? ")" : ""));
}

/* Look at /etc/debian_version to detect non-LSB Debian. */
static const char *
debian_release(apr_pool_t *pool)
{
  svn_stringbuf_t *buffer = read_file_contents("/etc/debian_version", pool);
  if (!buffer)
      return NULL;

  stringbuf_first_line_only(buffer);
  return apr_pstrcat(pool, "Debian ", buffer->data, SVN_VA_NULL);
}

/* Try to find the Linux distribution name, or return info from uname. */
static const char *
linux_release_name(apr_pool_t *pool)
{
  const char *uname_release = release_name_from_uname(pool);

  /* Try anything that has /usr/bin/lsb_release.
     Covers, for example, Debian, Ubuntu and SuSE.  */
  const char *release_name = lsb_release(pool);

  /* Try the systemd way (covers Arch). */
  if (!release_name)
    release_name = systemd_release(pool);

  /* Try RHEL/Fedora/CentOS */
  if (!release_name)
    release_name = redhat_release(pool);

  /* Try Non-LSB SuSE */
  if (!release_name)
    release_name = suse_release(pool);

  /* Try non-LSB Debian */
  if (!release_name)
    release_name = debian_release(pool);

  if (!release_name)
    return uname_release;

  if (!uname_release)
    return release_name;

  return apr_psprintf(pool, "%s [%s]", release_name, uname_release);
}
#endif /* __linux__ */


#ifdef WIN32
typedef DWORD (WINAPI *FNGETNATIVESYSTEMINFO)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *FNENUMPROCESSMODULES) (HANDLE, HMODULE*, DWORD, LPDWORD);

svn_boolean_t
svn_sysinfo___fill_windows_version(OSVERSIONINFOEXW *version_info)
{
  memset(version_info, 0, sizeof(*version_info));

  version_info->dwOSVersionInfoSize = sizeof(*version_info);

  /* Kill warnings with the Windows 8 and later platform SDK */
#if _MSC_VER > 1600 && NTDDI_VERSION >= _0x06020000
  /* Windows 8 deprecated the API to retrieve the Windows version to avoid
     backwards compatibility problems... It might return a constant version
     in future Windows versions... But let's kill the warning.

     We can implementation this using a different function later. */
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

  /* Prototype supports OSVERSIONINFO */
  return GetVersionExW((LPVOID)version_info);
#if _MSC_VER > 1600 && NTDDI_VERSION >= _0x06020000
#pragma warning(pop)
#pragma warning(disable: 4996)
#endif
}

/* Get system info, and try to tell the difference between the native
   system type and the runtime environment of the current process.
   Populate results in SYSINFO and LOCAL_SYSINFO (optional). */
static BOOL
system_info(SYSTEM_INFO *sysinfo,
            SYSTEM_INFO *local_sysinfo)
{
  FNGETNATIVESYSTEMINFO GetNativeSystemInfo_ = (FNGETNATIVESYSTEMINFO)
    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetNativeSystemInfo");

  memset(sysinfo, 0, sizeof *sysinfo);
  if (local_sysinfo)
    {
      memset(local_sysinfo, 0, sizeof *local_sysinfo);
      GetSystemInfo(local_sysinfo);
      if (GetNativeSystemInfo_)
        GetNativeSystemInfo_(sysinfo);
      else
        memcpy(sysinfo, local_sysinfo, sizeof *sysinfo);
    }
  else
    GetSystemInfo(sysinfo);

  return TRUE;
}

/* Map the proccessor type from SYSINFO to a string. */
static const char *
processor_name(SYSTEM_INFO *sysinfo)
{
  switch (sysinfo->wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:         return "x86_64";
    case PROCESSOR_ARCHITECTURE_IA64:          return "ia64";
    case PROCESSOR_ARCHITECTURE_INTEL:         return "x86";
    case PROCESSOR_ARCHITECTURE_MIPS:          return "mips";
    case PROCESSOR_ARCHITECTURE_ALPHA:         return "alpha32";
    case PROCESSOR_ARCHITECTURE_PPC:           return "powerpc";
    case PROCESSOR_ARCHITECTURE_SHX:           return "shx";
    case PROCESSOR_ARCHITECTURE_ARM:           return "arm";
    case PROCESSOR_ARCHITECTURE_ALPHA64:       return "alpha";
    case PROCESSOR_ARCHITECTURE_MSIL:          return "msil";
    case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64: return "x86_wow64";
    default: return "unknown";
    }
}

/* Return the Windows-specific canonical host name. */
static const char *
win32_canonical_host(apr_pool_t *pool)
{
  SYSTEM_INFO sysinfo;
  SYSTEM_INFO local_sysinfo;
  OSVERSIONINFOEXW osinfo;

  if (system_info(&sysinfo, &local_sysinfo)
      && svn_sysinfo___fill_windows_version(&osinfo))
    {
      const char *arch = processor_name(&local_sysinfo);
      const char *machine = processor_name(&sysinfo);
      const char *vendor = "microsoft";
      const char *sysname = "windows";
      const char *sysver = apr_psprintf(pool, "%u.%u.%u",
                                        (unsigned int)osinfo.dwMajorVersion,
                                        (unsigned int)osinfo.dwMinorVersion,
                                        (unsigned int)osinfo.dwBuildNumber);

      if (sysinfo.wProcessorArchitecture
          == local_sysinfo.wProcessorArchitecture)
        return apr_psprintf(pool, "%s-%s-%s%s",
                            machine, vendor, sysname, sysver);
      return apr_psprintf(pool, "%s/%s-%s-%s%s",
                          arch, machine, vendor, sysname, sysver);
    }

  return "unknown-microsoft-windows";
}

/* Convert a Unicode string to UTF-8. */
static char *
wcs_to_utf8(const wchar_t *wcs, apr_pool_t *pool)
{
  const int bufsize = WideCharToMultiByte(CP_UTF8, 0, wcs, -1,
                                          NULL, 0, NULL, NULL);
  if (bufsize > 0)
    {
      char *const utf8 = apr_palloc(pool, bufsize + 1);
      WideCharToMultiByte(CP_UTF8, 0, wcs, -1, utf8, bufsize, NULL, NULL);
      return utf8;
    }
  return NULL;
}

/* Query the value called NAME of the registry key HKEY. */
static char *
registry_value(HKEY hkey, wchar_t *name, apr_pool_t *pool)
{
  DWORD size;
  wchar_t *value;

  if (RegQueryValueExW(hkey, name, NULL, NULL, NULL, &size))
    return NULL;

  value = apr_palloc(pool, size + sizeof *value);
  if (RegQueryValueExW(hkey, name, NULL, NULL, (void*)value, &size))
    return NULL;
  value[size / sizeof *value] = 0;
  return wcs_to_utf8(value, pool);
}

/* Try to glean the Windows release name and associated info from the
   registry. Failing that, construct a release name from the version
   info. */
static const char *
win32_release_name(apr_pool_t *pool)
{
  SYSTEM_INFO sysinfo;
  OSVERSIONINFOEXW osinfo;
  HKEY hkcv;

  if (!system_info(&sysinfo, NULL)
      || !svn_sysinfo___fill_windows_version(&osinfo))
    return NULL;

  if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     0, KEY_QUERY_VALUE, &hkcv))
    {
      const char *release = registry_value(hkcv, L"ProductName", pool);
      const char *spack = registry_value(hkcv, L"CSDVersion", pool);
      const char *curver = registry_value(hkcv, L"CurrentVersion", pool);
      const char *curtype = registry_value(hkcv, L"CurrentType", pool);
      const char *install = registry_value(hkcv, L"InstallationType", pool);
      const char *curbuild = registry_value(hkcv, L"CurrentBuildNumber", pool);

      if (!spack && *osinfo.szCSDVersion)
        spack = wcs_to_utf8(osinfo.szCSDVersion, pool);

      if (!curbuild)
        curbuild = registry_value(hkcv, L"CurrentBuild", pool);

      if (release || spack || curver || curtype || curbuild)
        {
          const char *bootinfo = "";
          if (curver || install || curtype)
            {
              bootinfo = apr_psprintf(pool, "[%s%s%s%s%s]",
                                      (curver ? curver : ""),
                                      (install ? (curver ? " " : "") : ""),
                                      (install ? install : ""),
                                      (curtype
                                       ? (curver||install ? " " : "")
                                       : ""),
                                      (curtype ? curtype : ""));
            }

          return apr_psprintf(pool, "%s%s%s%s%s%s%s",
                              (release ? release : ""),
                              (spack ? (release ? ", " : "") : ""),
                              (spack ? spack : ""),
                              (curbuild
                               ? (release||spack ? ", build " : "build ")
                               : ""),
                              (curbuild ? curbuild : ""),
                              (bootinfo
                               ? (release||spack||curbuild ? " " : "")
                               : ""),
                              (bootinfo ? bootinfo : ""));
        }
    }

  if (*osinfo.szCSDVersion)
    {
      const char *servicepack = wcs_to_utf8(osinfo.szCSDVersion, pool);

      if (servicepack)
        return apr_psprintf(pool, "Windows NT %u.%u, %s, build %u",
                            (unsigned int)osinfo.dwMajorVersion,
                            (unsigned int)osinfo.dwMinorVersion,
                            servicepack,
                            (unsigned int)osinfo.dwBuildNumber);

      /* Assume wServicePackMajor > 0 if szCSDVersion is not empty */
      if (osinfo.wServicePackMinor)
        return apr_psprintf(pool, "Windows NT %u.%u SP%u.%u, build %u",
                            (unsigned int)osinfo.dwMajorVersion,
                            (unsigned int)osinfo.dwMinorVersion,
                            (unsigned int)osinfo.wServicePackMajor,
                            (unsigned int)osinfo.wServicePackMinor,
                            (unsigned int)osinfo.dwBuildNumber);

      return apr_psprintf(pool, "Windows NT %u.%u SP%u, build %u",
                          (unsigned int)osinfo.dwMajorVersion,
                          (unsigned int)osinfo.dwMinorVersion,
                          (unsigned int)osinfo.wServicePackMajor,
                          (unsigned int)osinfo.dwBuildNumber);
    }

  return apr_psprintf(pool, "Windows NT %u.%u, build %u",
                      (unsigned int)osinfo.dwMajorVersion,
                      (unsigned int)osinfo.dwMinorVersion,
                      (unsigned int)osinfo.dwBuildNumber);
}


/* Get a list of handles of shared libs loaded by the current
   process. Returns a NULL-terminated array alocated from POOL. */
static HMODULE *
enum_loaded_modules(apr_pool_t *pool)
{
  HMODULE psapi_dll = 0;
  HANDLE current = GetCurrentProcess();
  HMODULE dummy[1];
  HMODULE *handles;
  DWORD size;
  FNENUMPROCESSMODULES EnumProcessModules_;

  psapi_dll = GetModuleHandleW(L"psapi.dll");

  if (!psapi_dll)
    {
      /* Load and never unload, just like static linking */
      psapi_dll = LoadLibraryW(L"psapi.dll");
    }

  if (!psapi_dll)
      return NULL;

  EnumProcessModules_ = (FNENUMPROCESSMODULES)
                              GetProcAddress(psapi_dll, "EnumProcessModules");

  /* Before Windows XP psapi was an optional module */
  if (! EnumProcessModules_)
    return NULL;

  if (!EnumProcessModules_(current, dummy, sizeof(dummy), &size))
    return NULL;

  handles = apr_palloc(pool, size + sizeof *handles);
  if (! EnumProcessModules_(current, handles, size, &size))
    return NULL;
  handles[size / sizeof *handles] = NULL;
  return handles;
}

/* Find the version number, if any, embedded in FILENAME. */
static const char *
file_version_number(const wchar_t *filename, apr_pool_t *pool)
{
  VS_FIXEDFILEINFO info;
  unsigned int major, minor, micro, nano;
  void *data;
  DWORD data_size = GetFileVersionInfoSizeW(filename, NULL);
  void *vinfo;
  UINT vinfo_size;

  if (!data_size)
    return NULL;

  data = apr_palloc(pool, data_size);
  if (!GetFileVersionInfoW(filename, 0, data_size, data))
    return NULL;

  if (!VerQueryValueW(data, L"\\", &vinfo, &vinfo_size))
    return NULL;

  if (vinfo_size != sizeof info)
    return NULL;

  memcpy(&info, vinfo, sizeof info);
  major = (info.dwFileVersionMS >> 16) & 0xFFFF;
  minor = info.dwFileVersionMS & 0xFFFF;
  micro = (info.dwFileVersionLS >> 16) & 0xFFFF;
  nano = info.dwFileVersionLS & 0xFFFF;

  if (!nano)
    {
      if (!micro)
        return apr_psprintf(pool, "%u.%u", major, minor);
      else
        return apr_psprintf(pool, "%u.%u.%u", major, minor, micro);
    }
  return apr_psprintf(pool, "%u.%u.%u.%u", major, minor, micro, nano);
}

/* List the shared libraries loaded by the current process. */
static const apr_array_header_t *
win32_shared_libs(apr_pool_t *pool)
{
  apr_array_header_t *array = NULL;
  wchar_t buffer[MAX_PATH + 1];
  HMODULE *handles = enum_loaded_modules(pool);
  HMODULE *module;

  for (module = handles; module && *module; ++module)
    {
      const char *filename;
      const char *version;
      if (GetModuleFileNameW(*module, buffer, MAX_PATH))
        {
          buffer[MAX_PATH] = 0;

          version = file_version_number(buffer, pool);
          filename = wcs_to_utf8(buffer, pool);
          if (filename)
            {
              svn_version_ext_loaded_lib_t *lib;

              if (!array)
                {
                  array = apr_array_make(pool, 32, sizeof(*lib));
                }
              lib = &APR_ARRAY_PUSH(array, svn_version_ext_loaded_lib_t);
              lib->name = svn_dirent_local_style(filename, pool);
              lib->version = version;
            }
        }
    }

  return array;
}
#endif /* WIN32 */


#ifdef SVN_HAVE_MACOS_PLIST
/* implements svn_write_fn_t to copy the data into a CFMutableDataRef that's
 * in the baton. */
static svn_error_t *
write_to_cfmutabledata(void *baton, const char *data, apr_size_t *len)
{
  CFMutableDataRef *resource = (CFMutableDataRef *) baton;

  CFDataAppendBytes(*resource, (UInt8 *)data, *len);

  return SVN_NO_ERROR;
}

/* Load the SystemVersion.plist or ServerVersion.plist file into a
   property list. Set SERVER to TRUE if the file read was
   ServerVersion.plist. */
static CFDictionaryRef
system_version_plist(svn_boolean_t *server, apr_pool_t *pool)
{
  static const char server_version[] =
    "/System/Library/CoreServices/ServerVersion.plist";
  static const char system_version[] =
    "/System/Library/CoreServices/SystemVersion.plist";
  svn_stream_t *read_stream, *write_stream;
  svn_error_t *err;
  CFPropertyListRef plist = NULL;
  CFMutableDataRef resource = CFDataCreateMutable(kCFAllocatorDefault, 0);

  /* failed getting the CFMutableDataRef, shouldn't happen */
  if (!resource)
    return NULL;

  /* Try to open the plist files to get the data */
  err = svn_stream_open_readonly(&read_stream, server_version, pool, pool);
  if (err)
    {
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        {
          svn_error_clear(err);
          CFRelease(resource);
          return NULL;
        }
      else
        {
          svn_error_clear(err);
          err = svn_stream_open_readonly(&read_stream, system_version,
                                         pool, pool);
          if (err)
            {
              svn_error_clear(err);
              CFRelease(resource);
              return NULL;
            }

          *server = FALSE;
        }
    }
  else
    {
      *server = TRUE;
    }

  /* copy the data onto the CFMutableDataRef to allow us to provide it to
   * the CoreFoundation functions that parse proprerty lists */
  write_stream = svn_stream_create(&resource, pool);
  svn_stream_set_write(write_stream, write_to_cfmutabledata);
  err = svn_stream_copy3(read_stream, write_stream, NULL, NULL, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_6
  /* This function is only available from Mac OS 10.6 onward. */
  plist = CFPropertyListCreateWithData(kCFAllocatorDefault, resource,
                                       kCFPropertyListImmutable,
                                       NULL, NULL);
#else  /* Mac OS 10.5 or earlier */
  /* This function obsolete and deprecated since Mac OS 10.10. */
  plist = CFPropertyListCreateFromXMLData(kCFAllocatorDefault, resource,
                                          kCFPropertyListImmutable,
                                          NULL);
#endif /* MAC_OS_X_VERSION_10_6 */

  if (resource)
    CFRelease(resource);

  if (!plist)
    return NULL;

  if (CFDictionaryGetTypeID() != CFGetTypeID(plist))
    {
      /* Oops ... this really should be a dict. */
      CFRelease(plist);
      return NULL;
    }

  return plist;
}

/* Return the value for KEY from PLIST, or NULL if not available. */
static const char *
value_from_dict(CFDictionaryRef plist, CFStringRef key, apr_pool_t *pool)
{
  CFStringRef valref;
  CFIndex bufsize;
  const void *valptr;
  const char *value;

  if (!CFDictionaryGetValueIfPresent(plist, key, &valptr))
    return NULL;

  valref = valptr;
  if (CFStringGetTypeID() != CFGetTypeID(valref))
    return NULL;

  value = CFStringGetCStringPtr(valref, kCFStringEncodingUTF8);
  if (value)
    return apr_pstrdup(pool, value);

  bufsize =  5 * CFStringGetLength(valref) + 1;
  value = apr_palloc(pool, bufsize);
  if (!CFStringGetCString(valref, (char*)value, bufsize,
                          kCFStringEncodingUTF8))
    value = NULL;

  return value;
}

/* Return the commercial name of the OS, given the version number in
   a format that matches the regular expression /^10\.\d+(\..*)?$/ */
static const char *
release_name_from_version(const char *osver)
{
  char *end = NULL;
  unsigned long num = strtoul(osver, &end, 10);

  if (!end || *end != '.' || num != 10)
    return NULL;

  osver = end + 1;
  end = NULL;
  num = strtoul(osver, &end, 10);
  if (!end || (*end && *end != '.'))
    return NULL;

  /* See http://en.wikipedia.org/wiki/History_of_OS_X#Release_timeline */
  switch(num)
    {
    case  0: return "Cheetah";
    case  1: return "Puma";
    case  2: return "Jaguar";
    case  3: return "Panther";
    case  4: return "Tiger";
    case  5: return "Leopard";
    case  6: return "Snow Leopard";
    case  7: return "Lion";
    case  8: return "Mountain Lion";
    case  9: return "Mavericks";
    case 10: return "Yosemite";
    case 11: return "El Capitan";
    case 12: return "Sierra";
    case 13: return "High Sierra";
    }

  return NULL;
}

/* Construct the release name from information stored in the Mac OS X
   "SystemVersion.plist" file (or ServerVersion.plist, for Mac Os
   Server. */
static const char *
macos_release_name(apr_pool_t *pool)
{
  svn_boolean_t server;
  CFDictionaryRef plist = system_version_plist(&server, pool);

  if (plist)
    {
      const char *osname = value_from_dict(plist, CFSTR("ProductName"), pool);
      const char *osver = value_from_dict(plist,
                                          CFSTR("ProductUserVisibleVersion"),
                                          pool);
      const char *build = value_from_dict(plist,
                                          CFSTR("ProductBuildVersion"),
                                          pool);
      const char *release;

      if (!osver)
        osver = value_from_dict(plist, CFSTR("ProductVersion"), pool);
      release = release_name_from_version(osver);

      CFRelease(plist);
      return apr_psprintf(pool, "%s%s%s%s%s%s%s%s",
                          (osname ? osname : ""),
                          (osver ? (osname ? " " : "") : ""),
                          (osver ? osver : ""),
                          (release ? (osname||osver ? " " : "") : ""),
                          (release ? release : ""),
                          (build
                           ? (osname||osver||release ? ", " : "")
                           : ""),
                          (build
                           ? (server ? "server build " : "build ")
                           : ""),
                          (build ? build : ""));
    }

  return NULL;
}
#endif  /* SVN_HAVE_MACOS_PLIST */

#ifdef SVN_HAVE_MACHO_ITERATE
/* List the shared libraries loaded by the current process.
   Ignore frameworks and system libraries, they're just clutter. */
static const apr_array_header_t *
macos_shared_libs(apr_pool_t *pool)
{
  static const char slb_prefix[] = "/usr/lib/system/";
  static const char fwk_prefix[] = "/System/Library/Frameworks/";
  static const char pfk_prefix[] = "/System/Library/PrivateFrameworks/";

  const size_t slb_prefix_len = strlen(slb_prefix);
  const size_t fwk_prefix_len = strlen(fwk_prefix);
  const size_t pfk_prefix_len = strlen(pfk_prefix);

  apr_array_header_t *result = NULL;
  apr_array_header_t *dylibs = NULL;

  uint32_t i;
  for (i = 0;; ++i)
    {
      const struct mach_header *header = _dyld_get_image_header(i);
      const char *filename = _dyld_get_image_name(i);
      const char *version;
      char *truename;
      svn_version_ext_loaded_lib_t *lib;

      if (!(header && filename))
        break;

      switch (header->cputype)
        {
        case CPU_TYPE_I386:      version = _("Intel"); break;
        case CPU_TYPE_X86_64:    version = _("Intel 64-bit"); break;
        case CPU_TYPE_POWERPC:   version = _("PowerPC"); break;
        case CPU_TYPE_POWERPC64: version = _("PowerPC 64-bit"); break;
        default:
          version = NULL;
        }

      if (0 == apr_filepath_merge(&truename, "", filename,
                                  APR_FILEPATH_NATIVE
                                  | APR_FILEPATH_TRUENAME,
                                  pool))
        filename = truename;
      else
        filename = apr_pstrdup(pool, filename);

      if (0 == strncmp(filename, slb_prefix, slb_prefix_len)
          || 0 == strncmp(filename, fwk_prefix, fwk_prefix_len)
          || 0 == strncmp(filename, pfk_prefix, pfk_prefix_len))
        {
          /* Ignore frameworks and system libraries. */
          continue;
        }

      if (header->filetype == MH_EXECUTE)
        {
          /* Make sure the program filename is first in the list */
          if (!result)
            {
              result = apr_array_make(pool, 32, sizeof(*lib));
            }
          lib = &APR_ARRAY_PUSH(result, svn_version_ext_loaded_lib_t);
        }
      else
        {
          if (!dylibs)
            {
              dylibs = apr_array_make(pool, 32, sizeof(*lib));
            }
          lib = &APR_ARRAY_PUSH(dylibs, svn_version_ext_loaded_lib_t);
        }

      lib->name = filename;
      lib->version = version;
    }

  /* Gather results into one array. */
  if (dylibs)
    {
      if (result)
        apr_array_cat(result, dylibs);
      else
        result = dylibs;
    }

  return result;
}
#endif  /* SVN_HAVE_MACHO_ITERATE */

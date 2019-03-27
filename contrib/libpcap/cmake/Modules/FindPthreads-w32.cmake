# ==============================================================================
# This is a heavily modified version of FindPthreads.cmake for the pcap project.
# It's meant to find Pthreads-w32, an implementation of the
# Threads component of the POSIX 1003.1c 1995 Standard (or later)
# for Microsoft's WIndows.
#
# Apart from this notice, this module "enjoys" the following modifications:
#
# - changed its name to FindPthreads-w32.cmake to not conflict with FindThreads.cmake
#
# - users may be able to use the environment variable PTHREADS_ROOT to point
#   cmake to the *root* of their Pthreads-w32 installation.
#   Alternatively, PTHREADS_ROOT may also be set from cmake command line or GUI
#   (-DPTHREADS_ROOT=/path/to/Pthreads-w32)
#   Two other variables that can be defined in a similar fashion are
#   PTHREAD_INCLUDE_PATH and PTHREAD_LIBRARY_PATH.
#
# - added some additional status/error messages
#
# - changed formating (uppercase to lowercare + indentation)
#
# - removed some stuff
#
# - when searching for Pthreads-win32 libraries, the directory structure of the
#   pre-build binaries folder found in the pthreads-win32 CVS code repository is
#   considered (e.i /Pre-built.2/lib/x64 /Pre-built.2/lib/x86)
#
# Send suggestion, patches, gifts and praises to pcap's developers.
# ==============================================================================
#
# Find the Pthreads library
# This module searches for the Pthreads-win32 library (including the
# pthreads-win32 port).
#
# This module defines these variables:
#
#  PTHREADS_FOUND       - True if the Pthreads library was found
#  PTHREADS_LIBRARY     - The location of the Pthreads library
#  PTHREADS_INCLUDE_DIR - The include directory of the Pthreads library
#  PTHREADS_DEFINITIONS - Preprocessor definitions to define (HAVE_PTHREAD_H is a fairly common one)
#
# This module responds to the PTHREADS_EXCEPTION_SCHEME
# variable on Win32 to allow the user to control the
# library linked against. The Pthreads-win32 port
# provides the ability to link against a version of the
# library with exception handling.
# IT IS NOT RECOMMENDED THAT YOU CHANGE PTHREADS_EXCEPTION_SCHEME
# TO ANYTHING OTHER THAN "C" because most POSIX thread implementations
# do not support stack unwinding.
#
#  PTHREADS_EXCEPTION_SCHEME
#       C  = no exceptions (default)
#           (NOTE: This is the default scheme on most POSIX thread
#           implementations and what you should probably be using)
#       CE = C++ Exception Handling
#       SE = Structure Exception Handling (MSVC only)
#

#
# Define a default exception scheme to link against
# and validate user choice.
#
#
if(NOT DEFINED PTHREADS_EXCEPTION_SCHEME)
  # Assign default if needed
  set(PTHREADS_EXCEPTION_SCHEME "C")
else(NOT DEFINED PTHREADS_EXCEPTION_SCHEME)
  # Validate
  if(NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "C" AND
    NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "CE" AND
    NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "SE")

    message(FATAL_ERROR "See documentation for FindPthreads.cmake, only C, CE, and SE modes are allowed")

  endif(NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "C" AND
    NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "CE" AND
    NOT PTHREADS_EXCEPTION_SCHEME STREQUAL "SE")

  if(NOT MSVC AND PTHREADS_EXCEPTION_SCHEME STREQUAL "SE")
    message(FATAL_ERROR "Structured Exception Handling is only allowed for MSVC")
  endif(NOT MSVC AND PTHREADS_EXCEPTION_SCHEME STREQUAL "SE")

endif(NOT DEFINED PTHREADS_EXCEPTION_SCHEME)

if(PTHREADS_ROOT)
  set(PTHREADS_ROOT PATHS ${PTHREADS_ROOT} NO_DEFAULT_PATH)
else()
  set(PTHREADS_ROOT $ENV{PTHREADS_ROOT})
endif(PTHREADS_ROOT)

#
# Find the header file
#
find_path(PTHREADS_INCLUDE_DIR
  NAMES pthread.h
  HINTS
  $ENV{PTHREAD_INCLUDE_PATH}
  ${PTHREADS_ROOT}/include
)

if(PTHREADS_INCLUDE_DIR)
  message(STATUS "Found pthread.h: ${PTHREADS_INCLUDE_DIR}")
# else()
# message(FATAL_ERROR "Could not find pthread.h. See README.Win32 for more information.")
endif(PTHREADS_INCLUDE_DIR)

#
# Find the library
#
set(names)
if(MSVC)
  set(names
      pthreadV${PTHREADS_EXCEPTION_SCHEME}2
      libpthread
  )
elseif(MINGW)
  set(names
      pthreadG${PTHREADS_EXCEPTION_SCHEME}2
      pthread
  )
endif(MSVC)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(SUBDIR "/x86")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(SUBDIR "/x64")
endif()

find_library(PTHREADS_LIBRARY NAMES ${names}
  DOC "The Portable Threads Library"
  HINTS
  ${CMAKE_SOURCE_DIR}/lib
  $ENV{PTHREAD_LIBRARY_PATH}
  ${PTHREADS_ROOT}
  C:/MinGW/lib/
  PATH_SUFFIXES lib/${SUBDIR}
)

if(PTHREADS_LIBRARY)
message(STATUS "Found PTHREADS library: ${PTHREADS_LIBRARY} (PTHREADS Exception Scheme: ${PTHREADS_EXCEPTION_SCHEME})")
# else()
# message(FATAL_ERROR "Could not find PTHREADS LIBRARY. See README.Win32 for more information.")
endif(PTHREADS_LIBRARY)

if(PTHREADS_INCLUDE_DIR AND PTHREADS_LIBRARY)
  set(PTHREADS_DEFINITIONS -DHAVE_PTHREAD_H)
  set(PTHREADS_INCLUDE_DIRS ${PTHREADS_INCLUDE_DIR})
  set(PTHREADS_LIBRARIES ${PTHREADS_LIBRARY})
  set(PTHREADS_FOUND TRUE)
endif(PTHREADS_INCLUDE_DIR AND PTHREADS_LIBRARY)

mark_as_advanced(PTHREADS_INCLUDE_DIR PTHREADS_LIBRARY)

# CMake support for fseeko
#
# Based on FindLFS.cmake by
# Copyright (C) 2016 Julian Andres Klode <jak@debian.org>.
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# This defines the following variables
#
# FSEEKO_DEFINITIONS - List of definitions to pass to add_definitions()
# FSEEKO_COMPILE_OPTIONS - List of definitions to pass to add_compile_options()
# FSEEKO_LIBRARIES - List of libraries and linker flags
# FSEEKO_FOUND - If there is Large files support
#

include(CheckCSourceCompiles)
include(FindPackageHandleStandardArgs)
include(CMakePushCheckState)

# Check for the availability of fseeko()
# The cases handled are:
#
#  * Native fseeko()
#  * Preprocessor flag -D_LARGEFILE_SOURCE
#
function(_fseeko_check)
    set(_fseeko_cppflags)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_QUIET 1)
    set(CMAKE_REQUIRED_DEFINITIONS ${LFS_DEFINITIONS})
    message(STATUS "Looking for native fseeko support")
    check_symbol_exists(fseeko stdio.h fseeko_native)
    cmake_pop_check_state()
    if (fseeko_native)
        message(STATUS "Looking for native fseeko support - found")
        set(FSEEKO_FOUND TRUE)
    else()
        message(STATUS "Looking for native fseeko support - not found")
    endif()

    if (NOT FSEEKO_FOUND)
        # See if it's available with _LARGEFILE_SOURCE.
        cmake_push_check_state()
        set(CMAKE_REQUIRED_QUIET 1)
        set(CMAKE_REQUIRED_DEFINITIONS ${LFS_DEFINITIONS} "-D_LARGEFILE_SOURCE")
        check_symbol_exists(fseeko stdio.h fseeko_need_largefile_source)
        cmake_pop_check_state()
        if (fseeko_need_largefile_source)
            message(STATUS "Looking for fseeko support with _LARGEFILE_SOURCE - found")
            set(FSEEKO_FOUND TRUE)
            set(_fseeko_cppflags "-D_LARGEFILE_SOURCE")
        else()
            message(STATUS "Looking for fseeko support with _LARGEFILE_SOURCE - not found")
        endif()
    endif()

    set(FSEEKO_DEFINITIONS ${_fseeko_cppflags} CACHE STRING "Extra definitions for fseeko support")
    set(FSEEKO_COMPILE_OPTIONS "" CACHE STRING "Extra compiler options for fseeko support")
    set(FSEEKO_LIBRARIES "" CACHE STRING "Extra definitions for fseeko support")
    set(FSEEKO_FOUND ${FSEEKO_FOUND} CACHE INTERNAL "Found fseeko")
endfunction()

if (NOT FSEEKO_FOUND)
    _fseeko_check()
endif()

find_package_handle_standard_args(FSEEKO "Could not find fseeko. Set FSEEKO_DEFINITIONS, FSEEKO_COMPILE_OPTIONS, FSEEKO_LIBRARIES." FSEEKO_FOUND)

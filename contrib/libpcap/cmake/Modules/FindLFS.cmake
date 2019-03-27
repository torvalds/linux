# CMake support for large files
#
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
# LFS_DEFINITIONS - List of definitions to pass to add_definitions()
# LFS_COMPILE_OPTIONS - List of definitions to pass to add_compile_options()
# LFS_LIBRARIES - List of libraries and linker flags
# LFS_FOUND - If there is Large files support
#

include(CheckCSourceCompiles)
include(FindPackageHandleStandardArgs)
include(CMakePushCheckState)

# Test program to check for LFS. Requires that off_t has at least 8 byte large
set(_lfs_test_source
    "
    #include <sys/types.h>
    typedef char my_static_assert[sizeof(off_t) >= 8 ? 1 : -1];
    int main(void) { return 0; }
    "
)

# Check if the given options are needed
#
# This appends to the variables _lfs_cppflags, _lfs_cflags, and _lfs_ldflags,
# it also sets LFS_FOUND to 1 if it works.
function(_lfs_check_compiler_option var options definitions libraries)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_QUIET 1)
    set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS} ${options})
    set(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS} ${definitions})
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_DEFINITIONS} ${libraries})

    message(STATUS "Looking for LFS support using ${options} ${definitions} ${libraries}")
    check_c_source_compiles("${_lfs_test_source}" ${var})
    cmake_pop_check_state()

    if(${var})
        message(STATUS "Looking for LFS support using ${options} ${definitions} ${libraries} - found")
        set(_lfs_cppflags ${_lfs_cppflags} ${definitions} PARENT_SCOPE)
        set(_lfs_cflags ${_lfs_cflags} ${options} PARENT_SCOPE)
        set(_lfs_ldflags ${_lfs_ldflags} ${libraries} PARENT_SCOPE)
        set(LFS_FOUND TRUE PARENT_SCOPE)
    else()
        message(STATUS "Looking for LFS support using ${options} ${definitions} ${libraries} - not found")
    endif()
endfunction()

# Check for the availability of LFS.
# The cases handled are:
#
#  * Native LFS
#  * Output of getconf LFS_CFLAGS; getconf LFS_LIBS; getconf LFS_LDFLAGS
#  * Preprocessor flag -D_FILE_OFFSET_BITS=64
#  * Preprocessor flag -D_LARGE_FILES
#
function(_lfs_check)
    set(_lfs_cflags)
    set(_lfs_cppflags)
    set(_lfs_ldflags)
    set(_lfs_libs)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_QUIET 1)
    message(STATUS "Looking for native LFS support")
    check_c_source_compiles("${_lfs_test_source}" lfs_native)
    cmake_pop_check_state()
    if (lfs_native)
        message(STATUS "Looking for native LFS support - found")
        set(LFS_FOUND TRUE)
    else()
        message(STATUS "Looking for native LFS support - not found")
    endif()

    if (NOT LFS_FOUND)
        # Check using getconf. If getconf fails, don't worry, the check in
        # _lfs_check_compiler_option will fail as well.
        execute_process(COMMAND getconf LFS_CFLAGS
                        OUTPUT_VARIABLE _lfs_cflags_raw
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        execute_process(COMMAND getconf LFS_LIBS
                        OUTPUT_VARIABLE _lfs_libs_tmp
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        execute_process(COMMAND getconf LFS_LDFLAGS
                        OUTPUT_VARIABLE _lfs_ldflags_tmp
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)

        separate_arguments(_lfs_cflags_raw)
        separate_arguments(_lfs_ldflags_tmp)
        separate_arguments(_lfs_libs_tmp)

        # Move -D flags to the place they are supposed to be
        foreach(flag ${_lfs_cflags_raw})
            if (flag MATCHES "-D.*")
                list(APPEND _lfs_cppflags_tmp ${flag})
            else()
                list(APPEND _lfs_cflags_tmp ${flag})
            endif()
        endforeach()

        # Check if the flags we received (if any) produce working LFS support
        _lfs_check_compiler_option(lfs_getconf_works
                                   "${_lfs_cflags_tmp}"
                                   "${_lfs_cppflags_tmp}"
                                   "${_lfs_libs_tmp};${_lfs_ldflags_tmp}")
    endif()

    if(NOT LFS_FOUND)  # IRIX stuff
        _lfs_check_compiler_option(lfs_need_n32 "-n32" "" "")
    endif()
    if(NOT LFS_FOUND)  # Linux and friends
        _lfs_check_compiler_option(lfs_need_file_offset_bits "" "-D_FILE_OFFSET_BITS=64" "")
    endif()
    if(NOT LFS_FOUND)  # AIX
        _lfs_check_compiler_option(lfs_need_large_files "" "-D_LARGE_FILES=1" "")
    endif()

    set(LFS_DEFINITIONS ${_lfs_cppflags} CACHE STRING "Extra definitions for large file support")
    set(LFS_COMPILE_OPTIONS ${_lfs_cflags} CACHE STRING "Extra definitions for large file support")
    set(LFS_LIBRARIES ${_lfs_libs} ${_lfs_ldflags} CACHE STRING "Extra definitions for large file support")
    set(LFS_FOUND ${LFS_FOUND} CACHE INTERNAL "Found LFS")
endfunction()

if (NOT LFS_FOUND)
    _lfs_check()
endif()

find_package_handle_standard_args(LFS "Could not find LFS. Set LFS_DEFINITIONS, LFS_COMPILE_OPTIONS, LFS_LIBRARIES." LFS_FOUND)

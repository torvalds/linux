# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Read README.cmake before using this.

PROJECT(APR C)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

OPTION(APR_INSTALL_PRIVATE_H  "Install selected private .h files (for httpd)"  OFF)
OPTION(APR_HAVE_IPV6        "IPv6 support"                 ON)
OPTION(INSTALL_PDB          "Install .pdb files (if generated)"  ON)
OPTION(APR_BUILD_TESTAPR    "Build the test suite"         OFF)
OPTION(TEST_STATIC_LIBS     "Test programs use APR static libraries instead of shared libraries?" OFF)
SET(MIN_WINDOWS_VER             "Vista" 
    CACHE STRING "Minimum Windows version")

# create 1-or-0 representation of feature tests for apr.h

SET(apr_have_ipv6_10 0)

IF(APR_HAVE_IPV6)
  SET(apr_have_ipv6_10 1)
ENDIF()

IF("${MIN_WINDOWS_VER}" STREQUAL "")
  SET(win32_winnt_str "0x0600")
ELSEIF(${MIN_WINDOWS_VER} STREQUAL "Vista")
  SET(win32_winnt_str "0x0600")
ELSEIF(${MIN_WINDOWS_VER} STREQUAL "Windows7")
  SET(win32_winnt_str "0x0601")
ELSE()
  SET(win32_winnt_str ${MIN_WINDOWS_VER})
ENDIF()

CONFIGURE_FILE(include/apr.hwc
               ${PROJECT_BINARY_DIR}/apr.h)

ADD_EXECUTABLE(gen_test_char tools/gen_test_char.c)
GET_TARGET_PROPERTY(GEN_TEST_CHAR_EXE gen_test_char LOCATION)
ADD_CUSTOM_COMMAND(
  COMMENT "Generating character tables, apr_escape_test_char.h, for current locale"
  DEPENDS gen_test_char
  COMMAND ${GEN_TEST_CHAR_EXE} > ${PROJECT_BINARY_DIR}/apr_escape_test_char.h
  OUTPUT ${PROJECT_BINARY_DIR}/apr_escape_test_char.h
)
ADD_CUSTOM_TARGET(
  test_char_header ALL
  DEPENDS ${PROJECT_BINARY_DIR}/apr_escape_test_char.h
)

# Generated .h files are stored in PROJECT_BINARY_DIR, not the
# source tree.
#
# BROKEN: not searching PROJECT_BINARY_DIR first, so you have to
#         manually delete apr.h in PROJECT_SOURCE_DIR/include if
#         you've generated apr.h before using a different build

SET(APR_INCLUDE_DIRECTORIES
  ${PROJECT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/arch/win32
  ${CMAKE_CURRENT_SOURCE_DIR}/include/arch/unix
  ${CMAKE_CURRENT_SOURCE_DIR}/include/private
)

SET(APR_SYSTEM_LIBS
  ws2_32
  mswsock
  rpcrt4
)

INCLUDE_DIRECTORIES(${APR_INCLUDE_DIRECTORIES})

SET(APR_PUBLIC_HEADERS_STATIC
  include/apr_allocator.h
  include/apr_atomic.h
  include/apr_dso.h
  include/apr_env.h
  include/apr_errno.h
  include/apr_escape.h
  include/apr_file_info.h
  include/apr_file_io.h
  include/apr_fnmatch.h
  include/apr_general.h
  include/apr_getopt.h
  include/apr_global_mutex.h
  include/apr_hash.h
  include/apr_inherit.h
  include/apr_lib.h
  include/apr_mmap.h
  include/apr_network_io.h
  include/apr_poll.h
  include/apr_pools.h
  include/apr_portable.h
  include/apr_proc_mutex.h
  include/apr_random.h
  include/apr_ring.h
  include/apr_shm.h
  include/apr_signal.h
  include/apr_skiplist.h
  include/apr_strings.h
  include/apr_support.h
  include/apr_tables.h
  include/apr_thread_cond.h
  include/apr_thread_mutex.h
  include/apr_thread_proc.h
  include/apr_thread_rwlock.h
  include/apr_time.h
  include/apr_user.h
  include/apr_version.h
  include/apr_want.h
)
SET(APR_PUBLIC_HEADERS_GENERATED
  ${PROJECT_BINARY_DIR}/apr.h
)

SET(APR_SOURCES
  atomic/win32/apr_atomic.c
  dso/win32/dso.c
  encoding/apr_escape.c
  file_io/unix/copy.c
  file_io/unix/fileacc.c
  file_io/unix/filepath_util.c
  file_io/unix/fullrw.c
  file_io/unix/mktemp.c
  file_io/unix/tempdir.c
  file_io/win32/buffer.c
  file_io/win32/dir.c
  file_io/win32/filedup.c
  file_io/win32/filepath.c
  file_io/win32/filestat.c
  file_io/win32/filesys.c
  file_io/win32/flock.c
  file_io/win32/open.c
  file_io/win32/pipe.c
  file_io/win32/readwrite.c
  file_io/win32/seek.c
  locks/win32/proc_mutex.c
  locks/win32/thread_cond.c
  locks/win32/thread_mutex.c
  locks/win32/thread_rwlock.c
  memory/unix/apr_pools.c
  misc/unix/errorcodes.c
  misc/unix/getopt.c
  misc/unix/otherchild.c
  misc/unix/version.c
  misc/win32/charset.c
  misc/win32/env.c
  misc/win32/internal.c
  misc/win32/misc.c
  misc/win32/rand.c
  misc/win32/start.c
  misc/win32/utf8.c
  mmap/unix/common.c
  mmap/win32/mmap.c
  network_io/unix/inet_ntop.c
  network_io/unix/inet_pton.c
  network_io/unix/multicast.c
  network_io/unix/sockaddr.c
  network_io/unix/socket_util.c
  network_io/win32/sendrecv.c
  network_io/win32/sockets.c
  network_io/win32/sockopt.c
  passwd/apr_getpass.c
  poll/unix/poll.c
  poll/unix/pollcb.c
  poll/unix/pollset.c
  poll/unix/select.c
  random/unix/apr_random.c
  random/unix/sha2.c
  random/unix/sha2_glue.c
  shmem/win32/shm.c
  strings/apr_cpystrn.c
  strings/apr_fnmatch.c
  strings/apr_snprintf.c
  strings/apr_strings.c
  strings/apr_strnatcmp.c
  strings/apr_strtok.c
  tables/apr_hash.c
  tables/apr_skiplist.c
  tables/apr_tables.c
  threadproc/win32/proc.c
  threadproc/win32/signals.c
  threadproc/win32/thread.c
  threadproc/win32/threadpriv.c
  time/win32/time.c
  time/win32/timestr.c
  user/win32/groupinfo.c
  user/win32/userinfo.c
)

SET(APR_TEST_SOURCES
  test/abts.c
  test/testargs.c
  test/testatomic.c
  test/testcond.c
  test/testdir.c
  test/testdso.c
  test/testdup.c
  test/testenv.c
  test/testescape.c
  test/testfile.c
  test/testfilecopy.c
  test/testfileinfo.c
  test/testflock.c
  test/testfmt.c
  test/testfnmatch.c
  test/testglobalmutex.c
  test/testhash.c
  test/testipsub.c
  test/testlfs.c
  test/testlock.c
  test/testmmap.c
  test/testnames.c
  test/testoc.c
  test/testpath.c
  test/testpipe.c
  test/testpoll.c
  test/testpools.c
  test/testproc.c
  test/testprocmutex.c
  test/testrand.c
  test/testshm.c
  test/testskiplist.c
  test/testsleep.c
  test/testsock.c
  test/testsockets.c
  test/testsockopt.c
  test/teststr.c
  test/teststrnatcmp.c
  test/testtable.c
  test/testtemp.c
  test/testthread.c
  test/testtime.c
  test/testud.c
  test/testuser.c
  test/testutil.c
  test/testvsn.c
)

SET(install_targets)
SET(install_bin_pdb)

# libapr-1 is shared, apr-1 is static
ADD_LIBRARY(libapr-1 SHARED ${APR_SOURCES} ${APR_PUBLIC_HEADERS_GENERATED} libapr.rc)
SET(install_targets ${install_targets} libapr-1)
SET(install_bin_pdb ${install_bin_pdb} ${PROJECT_BINARY_DIR}/libapr-1.pdb)
TARGET_LINK_LIBRARIES(libapr-1 ${APR_SYSTEM_LIBS})
SET_TARGET_PROPERTIES(libapr-1 PROPERTIES COMPILE_DEFINITIONS "APR_DECLARE_EXPORT;WINNT")
ADD_DEPENDENCIES(libapr-1 test_char_header)

ADD_LIBRARY(apr-1 STATIC ${APR_SOURCES} ${APR_PUBLIC_HEADERS_GENERATED})
SET(install_targets ${install_targets} apr-1)
TARGET_LINK_LIBRARIES(apr-1 ${APR_SYSTEM_LIBS})
SET_TARGET_PROPERTIES(apr-1 PROPERTIES COMPILE_DEFINITIONS "APR_DECLARE_STATIC;WINNT")
ADD_DEPENDENCIES(apr-1 test_char_header)

# libaprapp-1 and aprapp-1 are static
ADD_LIBRARY(libaprapp-1 STATIC misc/win32/apr_app.c misc/win32/internal.c ${APR_PUBLIC_HEADERS_GENERATED})
SET(install_targets ${install_targets} libaprapp-1)
SET_TARGET_PROPERTIES(libaprapp-1 PROPERTIES COMPILE_DEFINITIONS "APR_APP;WINNT")

ADD_LIBRARY(aprapp-1 STATIC misc/win32/apr_app.c misc/win32/internal.c ${APR_PUBLIC_HEADERS_GENERATED})
SET(install_targets ${install_targets} aprapp-1)
SET_TARGET_PROPERTIES(aprapp-1 PROPERTIES COMPILE_DEFINITIONS "APR_DECLARE_STATIC;APR_APP;WINNT")

IF(APR_BUILD_TESTAPR)
  ENABLE_TESTING()
  # Create a "check" target that displays test program output to the console.
  ADD_CUSTOM_TARGET(check COMMAND ${CMAKE_CTEST_COMMAND} --verbose)

  # copy data files to build directory so that we can run programs from there
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E make_directory 
                  ${PROJECT_BINARY_DIR}/data)
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                  ${PROJECT_SOURCE_DIR}/test/data/file_datafile.txt
                  ${PROJECT_BINARY_DIR}/data/file_datafile.txt)
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  ${PROJECT_SOURCE_DIR}/test/data/mmap_datafile.txt
                  ${PROJECT_BINARY_DIR}/data/mmap_datafile.txt)

  IF(TEST_STATIC_LIBS)
    SET(whichapr    apr-1)
    SET(whichaprapp aprapp-1)
    SET(apiflag     -DAPR_DECLARE_STATIC)
  ELSE()
    SET(whichapr    libapr-1)
    SET(whichaprapp libaprapp-1)
    SET(apiflag)
  ENDIF()

  ADD_EXECUTABLE(testapp test/testapp.c)
  TARGET_LINK_LIBRARIES(testapp ${whichapr} ${whichaprapp} ${APR_SYSTEM_LIBS})
  SET_TARGET_PROPERTIES(testapp PROPERTIES LINK_FLAGS /entry:wmainCRTStartup)
  IF(apiflag)
    SET_TARGET_PROPERTIES(testapp PROPERTIES COMPILE_FLAGS ${apiflag})
  ENDIF()
  ADD_TEST(NAME testapp COMMAND testapp)

  ADD_EXECUTABLE(testall ${APR_TEST_SOURCES})
  TARGET_LINK_LIBRARIES(testall ${whichapr} ${APR_SYSTEM_LIBS})
  IF(apiflag)
    SET_TARGET_PROPERTIES(testall PROPERTIES COMPILE_FLAGS ${apiflag})
  ENDIF()
  ADD_TEST(NAME testall COMMAND testall)

  ADD_LIBRARY(mod_test MODULE test/mod_test.c)
  TARGET_LINK_LIBRARIES(mod_test ${whichapr} ${APR_SYSTEM_LIBS})
  SET_PROPERTY(TARGET mod_test APPEND PROPERTY LINK_FLAGS /export:print_hello)
  # nasty work-around for difficulties adding more than one additional flag
  # (they get joined in a bad way behind the scenes)
  GET_PROPERTY(link_flags TARGET mod_test PROPERTY LINK_FLAGS)
  SET(link_flags "${link_flags} /export:count_reps")
  SET_TARGET_PROPERTIES(mod_test PROPERTIES LINK_FLAGS ${link_flags})
  IF(apiflag)
    SET_TARGET_PROPERTIES(mod_test PROPERTIES COMPILE_FLAGS ${apiflag})
  ENDIF()

  # Build all the single-source executable files with no special build
  # requirements.
  SET(single_source_programs
    test/echod.c
    test/sendfile.c
    test/sockperf.c
    test/testlockperf.c
    test/testmutexscope.c
    test/globalmutexchild.c
    test/occhild.c
    test/proc_child.c
    test/readchild.c
    test/sockchild.c
    test/testshmproducer.c
    test/testshmconsumer.c
    test/tryread.c
    test/internal/testucs.c
  )

  FOREACH(sourcefile ${single_source_programs})
    STRING(REGEX REPLACE ".*/([^\\]+)\\.c" "\\1" proggie ${sourcefile})
    ADD_EXECUTABLE(${proggie} ${sourcefile})
    TARGET_LINK_LIBRARIES(${proggie} ${whichapr} ${APR_SYSTEM_LIBS})
    IF(apiflag)
      SET_TARGET_PROPERTIES(${proggie} PROPERTIES COMPILE_FLAGS ${apiflag})
    ENDIF()
  ENDFOREACH()

  # Add tests for programs that run by themselves with no arguments.
  SET(simple_tests
    testmutexscope
    testucs
  )

  FOREACH(simple ${simple_tests})
    ADD_TEST(NAME ${simple} COMMAND ${simple})
  ENDFOREACH()

  # testlockperf takes forever on Windows with default counter limit
  ADD_TEST(NAME testlockperf COMMAND testlockperf -c 50000)

  # sendfile runs multiple times with different parameters.
  FOREACH(sendfile_mode blocking nonblocking timeout)
    ADD_TEST(NAME sendfile-${sendfile_mode} COMMAND sendfile client ${sendfile_mode} startserver)
  ENDFOREACH()

  # No test is added for echod+sockperf.  Those will have to be run manually.

ENDIF (APR_BUILD_TESTAPR)

# Installation

INSTALL(TARGETS ${install_targets}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
       )

IF(INSTALL_PDB)
  INSTALL(FILES ${install_bin_pdb}
          DESTINATION bin
          CONFIGURATIONS RelWithDebInfo Debug)
ENDIF()

INSTALL(FILES ${APR_PUBLIC_HEADERS_STATIC} ${APR_PUBLIC_HEADERS_GENERATED} DESTINATION include)
IF(APR_INSTALL_PRIVATE_H)
  # Kludges for unexpected dependencies of httpd 2.x, not installed by default
  SET(APR_PRIVATE_H_FOR_HTTPD
    include/arch/win32/apr_arch_file_io.h
    include/arch/win32/apr_arch_misc.h
    include/arch/win32/apr_arch_utf8.h
    include/arch/win32/apr_private.h
    )
  INSTALL(FILES ${APR_PRIVATE_H_FOR_HTTPD} DESTINATION include/arch/win32)
  INSTALL(FILES include/arch/apr_private_common.h DESTINATION include/arch)
ENDIF()

STRING(TOUPPER "${CMAKE_BUILD_TYPE}" buildtype)
MESSAGE(STATUS "")
MESSAGE(STATUS "")
MESSAGE(STATUS "APR configuration summary:")
MESSAGE(STATUS "")

MESSAGE(STATUS "  Build type ...................... : ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "  Install .pdb (if available)...... : ${INSTALL_PDB}")
MESSAGE(STATUS "  Install prefix .................. : ${CMAKE_INSTALL_PREFIX}")
MESSAGE(STATUS "  C compiler ...................... : ${CMAKE_C_COMPILER}")
MESSAGE(STATUS "  IPv6 ............................ : ${APR_HAVE_IPV6}")
MESSAGE(STATUS "  Minimum Windows version ......... : ${MIN_WINDOWS_VER}")
MESSAGE(STATUS "  Build test suite ................ : ${APR_BUILD_TESTAPR}")
IF(TEST_STATIC_LIBS)
MESSAGE(STATUS "    (testing static libraries)")
ELSE()
MESSAGE(STATUS "    (testing dynamic libraries)")
ENDIF()
MESSAGE(STATUS "  Install private .h for httpd .... : ${APR_INSTALL_PRIVATE_H}")

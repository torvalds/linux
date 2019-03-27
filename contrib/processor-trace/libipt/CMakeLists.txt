# Copyright (c) 2013-2018, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#  * Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#  * Neither the name of Intel Corporation nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

include_directories(
  internal/include
)

set(LIBIPT_SECTION_FILES
  src/pt_section.c
  src/pt_section_file.c
)

set(LIBIPT_FILES
  src/pt_error.c
  src/pt_packet_decoder.c
  src/pt_query_decoder.c
  src/pt_encoder.c
  src/pt_sync.c
  src/pt_version.c
  src/pt_last_ip.c
  src/pt_tnt_cache.c
  src/pt_ild.c
  src/pt_image.c
  src/pt_image_section_cache.c
  src/pt_retstack.c
  src/pt_insn_decoder.c
  src/pt_time.c
  src/pt_asid.c
  src/pt_event_queue.c
  src/pt_packet.c
  src/pt_decoder_function.c
  src/pt_config.c
  src/pt_insn.c
  src/pt_block_decoder.c
  src/pt_block_cache.c
  src/pt_msec_cache.c
)

if (CMAKE_HOST_UNIX)
  include_directories(
    internal/include/posix
  )

  set(LIBIPT_FILES ${LIBIPT_FILES} src/posix/init.c)
  set(LIBIPT_SECTION_FILES ${LIBIPT_SECTION_FILES} src/posix/pt_section_posix.c)
endif (CMAKE_HOST_UNIX)

if (CMAKE_HOST_WIN32)
  add_definitions(
    # export libipt symbols
    #
    /Dpt_export=__declspec\(dllexport\)
  )

  include_directories(
    internal/include/windows
  )

  set(LIBIPT_FILES ${LIBIPT_FILES} src/windows/init.c)
  set(LIBIPT_SECTION_FILES ${LIBIPT_SECTION_FILES} src/windows/pt_section_windows.c)
endif (CMAKE_HOST_WIN32)

set(LIBIPT_FILES ${LIBIPT_FILES} ${LIBIPT_SECTION_FILES})

add_library(libipt SHARED
  ${LIBIPT_FILES}
)

# put the version into the intel-pt header
#
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/intel-pt.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/intel-pt.h
)

set_target_properties(libipt PROPERTIES
  PREFIX ""
  PUBLIC_HEADER ${CMAKE_CURRENT_BINARY_DIR}/include/intel-pt.h
  VERSION   ${PT_VERSION}
  SOVERSION ${PT_VERSION_MAJOR}
)

install(TARGETS libipt
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)


function(add_ptunit_std_test name)
    add_ptunit_c_test(${name} src/pt_${name}.c ${ARGN})
endfunction(add_ptunit_std_test)


add_ptunit_std_test(last_ip)
add_ptunit_std_test(tnt_cache)
add_ptunit_std_test(retstack)
add_ptunit_std_test(ild)
add_ptunit_std_test(cpu)
add_ptunit_std_test(time)
add_ptunit_std_test(asid)
add_ptunit_std_test(event_queue)
add_ptunit_std_test(image src/pt_asid.c)
add_ptunit_std_test(sync src/pt_packet.c)
add_ptunit_std_test(config)
add_ptunit_std_test(image_section_cache)
add_ptunit_std_test(block_cache)
add_ptunit_std_test(msec_cache)

add_ptunit_c_test(mapped_section src/pt_asid.c)
add_ptunit_c_test(query
  src/pt_encoder.c
  src/pt_last_ip.c
  src/pt_packet_decoder.c
  src/pt_sync.c
  src/pt_tnt_cache.c
  src/pt_time.c
  src/pt_event_queue.c
  src/pt_query_decoder.c
  src/pt_packet.c
  src/pt_decoder_function.c
  src/pt_packet_decoder.c
  src/pt_config.c
  src/pt_time.c
  src/pt_block_cache.c
)
add_ptunit_c_test(section ${LIBIPT_SECTION_FILES})
add_ptunit_c_test(section-file
  test/src/ptunit-section.c
  src/pt_section.c
  src/pt_section_file.c
)
add_ptunit_c_test(packet
  src/pt_encoder.c
  src/pt_packet_decoder.c
  src/pt_sync.c
  src/pt_packet.c
  src/pt_decoder_function.c
  src/pt_config.c
)
add_ptunit_c_test(fetch
  src/pt_decoder_function.c
  src/pt_encoder.c
  src/pt_config.c
)

add_ptunit_cpp_test(cpp)
add_ptunit_libraries(cpp libipt)

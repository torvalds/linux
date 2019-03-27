add_subdirectory(MachO)
add_subdirectory(YAML)

if (MSVC)
  add_definitions(-wd4062) # Suppress 'warning C4062: Enumerator has no associated handler in a switch statement.'
endif()

add_lld_library(lldReaderWriter
  FileArchive.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLD_INCLUDE_DIR}/lld/ReaderWriter

  LINK_COMPONENTS
    Object
    Support

  LINK_LIBS
    lldCore
  )

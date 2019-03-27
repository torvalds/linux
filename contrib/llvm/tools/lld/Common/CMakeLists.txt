if(NOT LLD_BUILT_STANDALONE)
  set(tablegen_deps intrinsics_gen)
endif()

add_lld_library(lldCommon
  Args.cpp
  ErrorHandler.cpp
  Memory.cpp
  Reproduce.cpp
  Strings.cpp
  TargetOptionsCommandFlags.cpp
  Threads.cpp
  Timer.cpp
  Version.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLD_INCLUDE_DIR}/lld/Common

  LINK_COMPONENTS
  Codegen
  Core
  Demangle
  MC
  Option
  Support
  Target

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  DEPENDS
  ${tablegen_deps}
  )

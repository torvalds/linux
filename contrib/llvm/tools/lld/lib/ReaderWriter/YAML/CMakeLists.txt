add_lld_library(lldYAML
  ReaderWriterYAML.cpp

  LINK_COMPONENTS
    Support

  LINK_LIBS
    lldCore
  )

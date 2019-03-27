#
# Try to find the Riverbed TurboCap library.
#

# Try to find the header
find_path(TC_INCLUDE_DIR TcApi.h)

# Try to find the library
find_library(TC_LIBRARY TcApi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TC
  DEFAULT_MSG
  TC_INCLUDE_DIR
  TC_LIBRARY
)

mark_as_advanced(
  TC_INCLUDE_DIR
  TC_LIBRARY
)

set(TC_INCLUDE_DIRS ${TC_INCLUDE_DIR})
set(TC_LIBRARIES ${TC_LIBRARY})

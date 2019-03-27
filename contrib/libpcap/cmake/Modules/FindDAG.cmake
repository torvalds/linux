#
# Try to find the Endace DAG library.
#

# Try to find the header
find_path(DAG_INCLUDE_DIR dagapi.h)

#
# Try to find the libraries
#
# We assume that if we have libdag we have libdagconf, as they're
# installed at the same time from the same package.
#
find_library(DAG_LIBRARY dag)
find_library(DAGCONF_LIBRARY dagconf)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DAG
  DEFAULT_MSG
  DAG_INCLUDE_DIR
  DAG_LIBRARY
  DAGCONF_LIBRARY
)

mark_as_advanced(
  DAG_INCLUDE_DIR
  DAG_LIBRARY
  DAGCONF_LIBRARY
)

set(DAG_INCLUDE_DIRS ${DAG_INCLUDE_DIR})
set(DAG_LIBRARIES ${DAG_LIBRARY} ${DAGCONF_LIBRARY})

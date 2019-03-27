Experimental cmake-based build support for APR on Microsoft Windows

Status
------

This build support is currently intended only for Microsoft Windows.
Only Windows NT-based systems can be targeted.  (The traditional 
Windows build support for APR can target Windows 9x as well.)

This build support is experimental.  Specifically,

* It does not support all features of APR.
* Some components may not be built correctly and/or in a manner
  compatible with the previous Windows build support.
* Build interfaces, such as the mechanisms which are used to enable
  optional functionality or specify prerequisites, may change from
  release to release as feedback is received from users and bugs and
  limitations are resolved.

Important: Refer to the "Known Bugs and Limitations" section for further
           information.

           It is beyond the scope of this document to document or explain
           how to utilize the various cmake features, such as different
           build backends or provisions for finding support libraries.

           Please refer to the cmake documentation for additional information
           that applies to building any project with cmake.

Prerequisites
-------------

The following tools must be in PATH:

* cmake, version 2.8 or later
* If using a command-line compiler: compiler and linker and related tools
  (Refer to the cmake documentation for more information.)

How to build
------------

1. cd to a clean directory for building (i.e., don't build in your
   source tree)

2. Some cmake backends may want your compile tools in PATH.  (Hint: "Visual
   Studio Command Prompt")

3. cmake -G "some backend, like 'NMake Makefiles'"
     -DCMAKE_INSTALL_PREFIX=d:/path/to/aprinst
     -DAPR-specific-flags
     d:/path/to/aprsource

   Alternately, use cmake-gui and update settings in the GUI.

   APR feature flags:

       APR_INSTALL_PRIVATE_H  Install extra .h files which are required when
                              building httpd and Subversion but which aren't
                              intended for use by applications.
                              Default: OFF
       APR_HAVE_IPV6          Enable IPv6 support
                              Default: ON
       APR_BUILD_TESTAPR      Build APR test suite
                              Default: OFF
       TEST_STATIC_LIBS       Build the test suite to test the APR static
                              library instead of the APR dynamic library.
                              Default: OFF
                              In order to build the test suite against both
                              static and dynamic libraries, separate builds
                              will be required, one with TEST_STATIC_LIBS
                              set to ON.
       MIN_WINDOWS_VER        Minimum Windows version supported by this build
                              (This controls the setting of _WIN32_WINNT.)
                              "Vista" or "Windows7" or a numeric value like
                              "0x0601"
                              Default: "Vista"
                              For desktop/server equivalence or other values,
                              refer to
                              http://msdn.microsoft.com/en-us/library/windows/
                              desktop/aa383745(v=vs.85).aspx
       INSTALL_PDB            Install .pdb files if generated.
                              Default: ON

   CMAKE_C_FLAGS_RELEASE, _DEBUG, _RELWITHDEBINFO, _MINSIZEREL

   CMAKE_BUILD_TYPE

       For NMake Makefiles the choices are at least DEBUG, RELEASE,
       RELWITHDEBINFO, and MINSIZEREL
       Other backends make have other selections.

4. build using chosen backend (e.g., "nmake install")

Known Bugs and Limitations
--------------------------

* If include/apr.h or other generated files have been created in the source
  directory by another build system, they will be used unexpectedly and
  cause the build to fail.
* Options should be provided for remaining features:
  + APR_POOL_DEBUG
* APR-CHANGES.txt, APR-LICENSE.txt, and APR-NOTICE.txt are not installed,
  though perhaps that is a job for a higher-level script.

Generally:

* Many APR features have not been tested with this build.
* Developers need to examine the existing Windows build in great detail and see
  what is missing from the cmake-based build, whether a feature or some build
  nuance.
* Any feedback you can provide on your experiences with this build will be
  helpful.

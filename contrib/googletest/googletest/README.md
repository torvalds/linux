### Generic Build Instructions

#### Setup

To build Google Test and your tests that use it, you need to tell your build
system where to find its headers and source files. The exact way to do it
depends on which build system you use, and is usually straightforward.

#### Build

Suppose you put Google Test in directory `${GTEST_DIR}`. To build it, create a
library build target (or a project as called by Visual Studio and Xcode) to
compile

    ${GTEST_DIR}/src/gtest-all.cc

with `${GTEST_DIR}/include` in the system header search path and `${GTEST_DIR}`
in the normal header search path. Assuming a Linux-like system and gcc,
something like the following will do:

    g++ -isystem ${GTEST_DIR}/include -I${GTEST_DIR} \
        -pthread -c ${GTEST_DIR}/src/gtest-all.cc
    ar -rv libgtest.a gtest-all.o

(We need `-pthread` as Google Test uses threads.)

Next, you should compile your test source file with `${GTEST_DIR}/include` in
the system header search path, and link it with gtest and any other necessary
libraries:

    g++ -isystem ${GTEST_DIR}/include -pthread path/to/your_test.cc libgtest.a \
        -o your_test

As an example, the make/ directory contains a Makefile that you can use to build
Google Test on systems where GNU make is available (e.g. Linux, Mac OS X, and
Cygwin). It doesn't try to build Google Test's own tests. Instead, it just
builds the Google Test library and a sample test. You can use it as a starting
point for your own build script.

If the default settings are correct for your environment, the following commands
should succeed:

    cd ${GTEST_DIR}/make
    make
    ./sample1_unittest

If you see errors, try to tweak the contents of `make/Makefile` to make them go
away. There are instructions in `make/Makefile` on how to do it.

### Using CMake

Google Test comes with a CMake build script (
[CMakeLists.txt](https://github.com/google/googletest/blob/master/CMakeLists.txt))
that can be used on a wide range of platforms ("C" stands for cross-platform.).
If you don't have CMake installed already, you can download it for free from
<http://www.cmake.org/>.

CMake works by generating native makefiles or build projects that can be used in
the compiler environment of your choice. You can either build Google Test as a
standalone project or it can be incorporated into an existing CMake build for
another project.

#### Standalone CMake Project

When building Google Test as a standalone project, the typical workflow starts
with:

    mkdir mybuild       # Create a directory to hold the build output.
    cd mybuild
    cmake ${GTEST_DIR}  # Generate native build scripts.

If you want to build Google Test's samples, you should replace the last command
with

    cmake -Dgtest_build_samples=ON ${GTEST_DIR}

If you are on a \*nix system, you should now see a Makefile in the current
directory. Just type 'make' to build gtest.

If you use Windows and have Visual Studio installed, a `gtest.sln` file and
several `.vcproj` files will be created. You can then build them using Visual
Studio.

On Mac OS X with Xcode installed, a `.xcodeproj` file will be generated.

#### Incorporating Into An Existing CMake Project

If you want to use gtest in a project which already uses CMake, then a more
robust and flexible approach is to build gtest as part of that project directly.
This is done by making the GoogleTest source code available to the main build
and adding it using CMake's `add_subdirectory()` command. This has the
significant advantage that the same compiler and linker settings are used
between gtest and the rest of your project, so issues associated with using
incompatible libraries (eg debug/release), etc. are avoided. This is
particularly useful on Windows. Making GoogleTest's source code available to the
main build can be done a few different ways:

*   Download the GoogleTest source code manually and place it at a known
    location. This is the least flexible approach and can make it more difficult
    to use with continuous integration systems, etc.
*   Embed the GoogleTest source code as a direct copy in the main project's
    source tree. This is often the simplest approach, but is also the hardest to
    keep up to date. Some organizations may not permit this method.
*   Add GoogleTest as a git submodule or equivalent. This may not always be
    possible or appropriate. Git submodules, for example, have their own set of
    advantages and drawbacks.
*   Use CMake to download GoogleTest as part of the build's configure step. This
    is just a little more complex, but doesn't have the limitations of the other
    methods.

The last of the above methods is implemented with a small piece of CMake code in
a separate file (e.g. `CMakeLists.txt.in`) which is copied to the build area and
then invoked as a sub-build _during the CMake stage_. That directory is then
pulled into the main build with `add_subdirectory()`. For example:

New file `CMakeLists.txt.in`:

    cmake_minimum_required(VERSION 2.8.2)

    project(googletest-download NONE)

    include(ExternalProject)
    ExternalProject_Add(googletest
      GIT_REPOSITORY    https://github.com/google/googletest.git
      GIT_TAG           master
      SOURCE_DIR        "${CMAKE_BINARY_DIR}/googletest-src"
      BINARY_DIR        "${CMAKE_BINARY_DIR}/googletest-build"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND     ""
      INSTALL_COMMAND   ""
      TEST_COMMAND      ""
    )

Existing build's `CMakeLists.txt`:

    # Download and unpack googletest at configure time
    configure_file(CMakeLists.txt.in googletest-download/CMakeLists.txt)
    execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
    if(result)
      message(FATAL_ERROR "CMake step for googletest failed: ${result}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-download )
    if(result)
      message(FATAL_ERROR "Build step for googletest failed: ${result}")
    endif()

    # Prevent overriding the parent project's compiler/linker
    # settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Add googletest directly to our build. This defines
    # the gtest and gtest_main targets.
    add_subdirectory(${CMAKE_BINARY_DIR}/googletest-src
                     ${CMAKE_BINARY_DIR}/googletest-build
                     EXCLUDE_FROM_ALL)

    # The gtest/gtest_main targets carry header search path
    # dependencies automatically when using CMake 2.8.11 or
    # later. Otherwise we have to add them here ourselves.
    if (CMAKE_VERSION VERSION_LESS 2.8.11)
      include_directories("${gtest_SOURCE_DIR}/include")
    endif()

    # Now simply link against gtest or gtest_main as needed. Eg
    add_executable(example example.cpp)
    target_link_libraries(example gtest_main)
    add_test(NAME example_test COMMAND example)

Note that this approach requires CMake 2.8.2 or later due to its use of the
`ExternalProject_Add()` command. The above technique is discussed in more detail
in [this separate article](http://crascit.com/2015/07/25/cmake-gtest/) which
also contains a link to a fully generalized implementation of the technique.

##### Visual Studio Dynamic vs Static Runtimes

By default, new Visual Studio projects link the C runtimes dynamically but
Google Test links them statically. This will generate an error that looks
something like the following: gtest.lib(gtest-all.obj) : error LNK2038: mismatch
detected for 'RuntimeLibrary': value 'MTd_StaticDebug' doesn't match value
'MDd_DynamicDebug' in main.obj

Google Test already has a CMake option for this: `gtest_force_shared_crt`

Enabling this option will make gtest link the runtimes dynamically too, and
match the project in which it is included.

### Legacy Build Scripts

Before settling on CMake, we have been providing hand-maintained build
projects/scripts for Visual Studio, Xcode, and Autotools. While we continue to
provide them for convenience, they are not actively maintained any more. We
highly recommend that you follow the instructions in the above sections to
integrate Google Test with your existing build system.

If you still need to use the legacy build scripts, here's how:

The msvc\ folder contains two solutions with Visual C++ projects. Open the
`gtest.sln` or `gtest-md.sln` file using Visual Studio, and you are ready to
build Google Test the same way you build any Visual Studio project. Files that
have names ending with -md use DLL versions of Microsoft runtime libraries (the
/MD or the /MDd compiler option). Files without that suffix use static versions
of the runtime libraries (the /MT or the /MTd option). Please note that one must
use the same option to compile both gtest and the test code. If you use Visual
Studio 2005 or above, we recommend the -md version as /MD is the default for new
projects in these versions of Visual Studio.

On Mac OS X, open the `gtest.xcodeproj` in the `xcode/` folder using Xcode.
Build the "gtest" target. The universal binary framework will end up in your
selected build directory (selected in the Xcode "Preferences..." -> "Building"
pane and defaults to xcode/build). Alternatively, at the command line, enter:

    xcodebuild

This will build the "Release" configuration of gtest.framework in your default
build location. See the "xcodebuild" man page for more information about
building different configurations and building in different locations.

If you wish to use the Google Test Xcode project with Xcode 4.x and above, you
need to either:

*   update the SDK configuration options in xcode/Config/General.xconfig.
    Comment options `SDKROOT`, `MACOS_DEPLOYMENT_TARGET`, and `GCC_VERSION`. If
    you choose this route you lose the ability to target earlier versions of
    MacOS X.
*   Install an SDK for an earlier version. This doesn't appear to be supported
    by Apple, but has been reported to work
    (http://stackoverflow.com/questions/5378518).

### Tweaking Google Test

Google Test can be used in diverse environments. The default configuration may
not work (or may not work well) out of the box in some environments. However,
you can easily tweak Google Test by defining control macros on the compiler
command line. Generally, these macros are named like `GTEST_XYZ` and you define
them to either 1 or 0 to enable or disable a certain feature.

We list the most frequently used macros below. For a complete list, see file
[include/gtest/internal/gtest-port.h](https://github.com/google/googletest/blob/master/include/gtest/internal/gtest-port.h).

### Choosing a TR1 Tuple Library

Some Google Test features require the C++ Technical Report 1 (TR1) tuple
library, which is not yet available with all compilers. The good news is that
Google Test implements a subset of TR1 tuple that's enough for its own need, and
will automatically use this when the compiler doesn't provide TR1 tuple.

Usually you don't need to care about which tuple library Google Test uses.
However, if your project already uses TR1 tuple, you need to tell Google Test to
use the same TR1 tuple library the rest of your project uses, or the two tuple
implementations will clash. To do that, add

    -DGTEST_USE_OWN_TR1_TUPLE=0

to the compiler flags while compiling Google Test and your tests. If you want to
force Google Test to use its own tuple library, just add

    -DGTEST_USE_OWN_TR1_TUPLE=1

to the compiler flags instead.

If you don't want Google Test to use tuple at all, add

    -DGTEST_HAS_TR1_TUPLE=0

and all features using tuple will be disabled.

### Multi-threaded Tests

Google Test is thread-safe where the pthread library is available. After
`#include "gtest/gtest.h"`, you can check the `GTEST_IS_THREADSAFE` macro to see
whether this is the case (yes if the macro is `#defined` to 1, no if it's
undefined.).

If Google Test doesn't correctly detect whether pthread is available in your
environment, you can force it with

    -DGTEST_HAS_PTHREAD=1

or

    -DGTEST_HAS_PTHREAD=0

When Google Test uses pthread, you may need to add flags to your compiler and/or
linker to select the pthread library, or you'll get link errors. If you use the
CMake script or the deprecated Autotools script, this is taken care of for you.
If you use your own build script, you'll need to read your compiler and linker's
manual to figure out what flags to add.

### As a Shared Library (DLL)

Google Test is compact, so most users can build and link it as a static library
for the simplicity. You can choose to use Google Test as a shared library (known
as a DLL on Windows) if you prefer.

To compile *gtest* as a shared library, add

    -DGTEST_CREATE_SHARED_LIBRARY=1

to the compiler flags. You'll also need to tell the linker to produce a shared
library instead - consult your linker's manual for how to do it.

To compile your *tests* that use the gtest shared library, add

    -DGTEST_LINKED_AS_SHARED_LIBRARY=1

to the compiler flags.

Note: while the above steps aren't technically necessary today when using some
compilers (e.g. GCC), they may become necessary in the future, if we decide to
improve the speed of loading the library (see
<http://gcc.gnu.org/wiki/Visibility> for details). Therefore you are recommended
to always add the above flags when using Google Test as a shared library.
Otherwise a future release of Google Test may break your build script.

### Avoiding Macro Name Clashes

In C++, macros don't obey namespaces. Therefore two libraries that both define a
macro of the same name will clash if you `#include` both definitions. In case a
Google Test macro clashes with another library, you can force Google Test to
rename its macro to avoid the conflict.

Specifically, if both Google Test and some other code define macro FOO, you can
add

    -DGTEST_DONT_DEFINE_FOO=1

to the compiler flags to tell Google Test to change the macro's name from `FOO`
to `GTEST_FOO`. Currently `FOO` can be `FAIL`, `SUCCEED`, or `TEST`. For
example, with `-DGTEST_DONT_DEFINE_TEST=1`, you'll need to write

    GTEST_TEST(SomeTest, DoesThis) { ... }

instead of

    TEST(SomeTest, DoesThis) { ... }

in order to define a test.

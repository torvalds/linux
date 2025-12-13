.. SPDX-License-Identifier: GPL-2.0

===================================
Using AutoFDO with the Linux kernel
===================================

This enables AutoFDO build support for the kernel when using
the Clang compiler. AutoFDO (Auto-Feedback-Directed Optimization)
is a type of profile-guided optimization (PGO) used to enhance the
performance of binary executables. It gathers information about the
frequency of execution of various code paths within a binary using
hardware sampling. This data is then used to guide the compiler's
optimization decisions, resulting in a more efficient binary. AutoFDO
is a powerful optimization technique, and data indicates that it can
significantly improve kernel performance. It's especially beneficial
for workloads affected by front-end stalls.

For AutoFDO builds, unlike non-FDO builds, the user must supply a
profile. Acquiring an AutoFDO profile can be done in several ways.
AutoFDO profiles are created by converting hardware sampling using
the "perf" tool. It is crucial that the workload used to create these
perf files is representative; they must exhibit runtime
characteristics similar to the workloads that are intended to be
optimized. Failure to do so will result in the compiler optimizing
for the wrong objective.

The AutoFDO profile often encapsulates the program's behavior. If the
performance-critical codes are architecture-independent, the profile
can be applied across platforms to achieve performance gains. For
instance, using the profile generated on Intel architecture to build
a kernel for AMD architecture can also yield performance improvements.

There are two methods for acquiring a representative profile:
(1) Sample real workloads using a production environment.
(2) Generate the profile using a representative load test.
When enabling the AutoFDO build configuration without providing an
AutoFDO profile, the compiler only modifies the dwarf information in
the kernel without impacting runtime performance. It's advisable to
use a kernel binary built with the same AutoFDO configuration to
collect the perf profile. While it's possible to use a kernel built
with different options, it may result in inferior performance.

One can collect profiles using AutoFDO build for the previous kernel.
AutoFDO employs relative line numbers to match the profiles, offering
some tolerance for source changes. This mode is commonly used in a
production environment for profile collection.

In a profile collection based on a load test, the AutoFDO collection
process consists of the following steps:

#. Initial build: The kernel is built with AutoFDO options
   without a profile.

#. Profiling: The above kernel is then run with a representative
   workload to gather execution frequency data. This data is
   collected using hardware sampling, via perf. AutoFDO is most
   effective on platforms supporting advanced PMU features like
   LBR on Intel machines.

#. AutoFDO profile generation: Perf output file is converted to
   the AutoFDO profile via offline tools.

The support requires a Clang compiler LLVM 17 or later.

Preparation
===========

Configure the kernel with::

   CONFIG_AUTOFDO_CLANG=y

Customization
=============

The default CONFIG_AUTOFDO_CLANG setting covers kernel space objects for
AutoFDO builds. One can, however, enable or disable AutoFDO build for
individual files and directories by adding a line similar to the following
to the respective kernel Makefile:

- For enabling a single file (e.g. foo.o) ::

   AUTOFDO_PROFILE_foo.o := y

- For enabling all files in one directory ::

   AUTOFDO_PROFILE := y

- For disabling one file ::

   AUTOFDO_PROFILE_foo.o := n

- For disabling all files in one directory ::

   AUTOFDO_PROFILE := n

Workflow
========

Here is an example workflow for AutoFDO kernel:

1)  Build the kernel on the host machine with LLVM enabled,
    for example, ::

      $ make menuconfig LLVM=1

    Turn on AutoFDO build config::

      CONFIG_AUTOFDO_CLANG=y

    With a configuration that with LLVM enabled, use the following command::

      $ scripts/config -e AUTOFDO_CLANG

    After getting the config, build with ::

      $ make LLVM=1

2) Install the kernel on the test machine.

3) Run the load tests. The '-c' option in perf specifies the sample
   event period. We suggest using a suitable prime number, like 500009,
   for this purpose.

   - For Intel platforms::

      $ perf record -e BR_INST_RETIRED.NEAR_TAKEN:k -a -N -b -c <count> -o <perf_file> -- <loadtest>

   - For AMD platforms:

     The supported systems are: Zen3 with BRS, or Zen4 with amd_lbr_v2. To check,

     For Zen3::

      $ cat /proc/cpuinfo | grep " brs"

     For Zen4::

      $ cat /proc/cpuinfo | grep amd_lbr_v2

     The following command generated the perf data file::

      $ perf record --pfm-events RETIRED_TAKEN_BRANCH_INSTRUCTIONS:k -a -N -b -c <count> -o <perf_file> -- <loadtest>

4) (Optional) Download the raw perf file to the host machine.

5) To generate an AutoFDO profile, two offline tools are available:
   create_llvm_prof and llvm_profgen. The create_llvm_prof tool is part
   of the AutoFDO project and can be found on GitHub
   (https://github.com/google/autofdo), version v0.30.1 or later.
   The llvm_profgen tool is included in the LLVM compiler itself. It's
   important to note that the version of llvm_profgen doesn't need to match
   the version of Clang. It needs to be the LLVM 19 release of Clang
   or later, or just from the LLVM trunk. ::

      $ llvm-profgen --kernel --binary=<vmlinux> --perfdata=<perf_file> -o <profile_file>

   or ::

      $ create_llvm_prof --binary=<vmlinux> --profile=<perf_file> --format=extbinary --out=<profile_file>

   Note that multiple AutoFDO profile files can be merged into one via::

      $ llvm-profdata merge -o <profile_file> <profile_1> <profile_2> ... <profile_n>

6) Rebuild the kernel using the AutoFDO profile file with the same config as step 1,
   (Note CONFIG_AUTOFDO_CLANG needs to be enabled)::

      $ make LLVM=1 CLANG_AUTOFDO_PROFILE=<profile_file>

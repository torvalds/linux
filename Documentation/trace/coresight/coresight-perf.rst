.. SPDX-License-Identifier: GPL-2.0

================
CoreSight - Perf
================

    :Author:   Carsten Haitzler <carsten.haitzler@arm.com>
    :Date:     June 29th, 2022

Perf is able to locally access CoreSight trace data and store it to the
output perf data files. This data can then be later decoded to give the
instructions that were traced for debugging or profiling purposes. You
can log such data with a perf record command like::

   perf record -e cs_etm//u testbinary

This would run some test binary (testbinary) until it exits and record
a perf.data trace file. That file would have AUX sections if CoreSight
is working correctly. You can dump the content of this file as
readable text with a command like::

   perf report --stdio --dump -i perf.data

You should find some sections of this file have AUX data blocks like::

   0x1e78 [0x30]: PERF_RECORD_AUXTRACE size: 0x11dd0  offset: 0  ref: 0x1b614fc1061b0ad1  idx: 0  tid: 531230  cpu: -1

   . ... CoreSight ETM Trace data: size 73168 bytes
           Idx:0; ID:10;   I_ASYNC : Alignment Synchronisation.
             Idx:12; ID:10;  I_TRACE_INFO : Trace Info.; INFO=0x0 { CC.0 }
             Idx:17; ID:10;  I_ADDR_L_64IS0 : Address, Long, 64 bit, IS0.; Addr=0x0000000000000000;
             Idx:26; ID:10;  I_TRACE_ON : Trace On.
             Idx:27; ID:10;  I_ADDR_CTXT_L_64IS0 : Address & Context, Long, 64 bit, IS0.; Addr=0x0000FFFFB6069140; Ctxt: AArch64,EL0, NS;
             Idx:38; ID:10;  I_ATOM_F6 : Atom format 6.; EEEEEEEEEEEEEEEEEEEEEEEE
             Idx:39; ID:10;  I_ATOM_F6 : Atom format 6.; EEEEEEEEEEEEEEEEEEEEEEEE
             Idx:40; ID:10;  I_ATOM_F6 : Atom format 6.; EEEEEEEEEEEEEEEEEEEEEEEE
             Idx:41; ID:10;  I_ATOM_F6 : Atom format 6.; EEEEEEEEEEEN
             ...

If you see these above, then your system is tracing CoreSight data
correctly.

To compile perf with CoreSight support in the tools/perf directory do::

    make CORESIGHT=1

This requires OpenCSD to build. You may install distribution packages
for the support such as libopencsd and libopencsd-dev or download it
and build yourself. Upstream OpenCSD is located at:

  https://github.com/Linaro/OpenCSD

For complete information on building perf with CoreSight support and
more extensive usage look at:

  https://github.com/Linaro/OpenCSD/blob/master/HOWTO.md


Kernel CoreSight Support
------------------------

You will also want CoreSight support enabled in your kernel config.
Ensure it is enabled with::

   CONFIG_CORESIGHT=y

There are various other CoreSight options you probably also want
enabled like::

   CONFIG_CORESIGHT_LINKS_AND_SINKS=y
   CONFIG_CORESIGHT_LINK_AND_SINK_TMC=y
   CONFIG_CORESIGHT_CATU=y
   CONFIG_CORESIGHT_SINK_TPIU=y
   CONFIG_CORESIGHT_SINK_ETBV10=y
   CONFIG_CORESIGHT_SOURCE_ETM4X=y
   CONFIG_CORESIGHT_CTI=y
   CONFIG_CORESIGHT_CTI_INTEGRATION_REGS=y

Please refer to the kernel configuration help for more information.

Perf test - Verify kernel and userspace perf CoreSight work
-----------------------------------------------------------

When you run perf test, it will do a lot of self tests. Some of those
tests will cover CoreSight (only if enabled and on ARM64). You
generally would run perf test from the tools/perf directory in the
kernel tree. Some tests will check some internal perf support like:

   Check Arm CoreSight trace data recording and synthesized samples
   Check Arm SPE trace data recording and synthesized samples

Some others will actually use perf record and some test binaries that
are in tests/shell/coresight and will collect traces to ensure a
minimum level of functionality is met. The scripts that launch these
tests are in the same directory. These will all look like:

   CoreSight / ASM Pure Loop
   CoreSight / Memcpy 16k 10 Threads
   CoreSight / Thread Loop 10 Threads - Check TID
   etc.

These perf record tests will not run if the tool binaries do not exist
in tests/shell/coresight/\*/ and will be skipped. If you do not have
CoreSight support in hardware then either do not build perf with
CoreSight support or remove these binaries in order to not have these
tests fail and have them skip instead.

These tests will log historical results in the current working
directory (e.g. tools/perf) and will be named stats-\*.csv like:

   stats-asm_pure_loop-out.csv
   stats-memcpy_thread-16k_10.csv
   ...

These statistic files log some aspects of the AUX data sections in
the perf data output counting some numbers of certain encodings (a
good way to know that it's working in a very simple way). One problem
with CoreSight is that given a large enough amount of data needing to
be logged, some of it can be lost due to the processor not waking up
in time to read out all the data from buffers etc.. You will notice
that the amount of data collected can vary a lot per run of perf test.
If you wish to see how this changes over time, simply run perf test
multiple times and all these csv files will have more and more data
appended to it that you can later examine, graph and otherwise use to
figure out if things have become worse or better.

This means sometimes these tests fail as they don't capture all the
data needed. This is about tracking quality and amount of data
produced over time and to see when changes to the Linux kernel improve
quality of traces.

Be aware that some of these tests take quite a while to run, specifically
in processing the perf data file and dumping contents to then examine what
is inside.

You can change where these csv logs are stored by setting the
PERF_TEST_CORESIGHT_STATDIR environment variable before running perf
test like::

   export PERF_TEST_CORESIGHT_STATDIR=/var/tmp
   perf test

They will also store resulting perf output data in the current
directory for later inspection like::

   perf-asm_pure_loop-out.data
   perf-memcpy_thread-16k_10.data
   ...

You can alter where the perf data files are stored by setting the
PERF_TEST_CORESIGHT_DATADIR environment variable such as::

   PERF_TEST_CORESIGHT_DATADIR=/var/tmp
   perf test

You may wish to set these above environment variables if you wish to
keep the output of tests outside of the current working directory for
longer term storage and examination.

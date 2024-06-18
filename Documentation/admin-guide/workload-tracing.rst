.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)

======================================================
Discovering Linux kernel subsystems used by a workload
======================================================

:Authors: - Shuah Khan <skhan@linuxfoundation.org>
          - Shefali Sharma <sshefali021@gmail.com>
:maintained-by: Shuah Khan <skhan@linuxfoundation.org>

Key Points
==========

 * Understanding system resources necessary to build and run a workload
   is important.
 * Linux tracing and strace can be used to discover the system resources
   in use by a workload. The completeness of the system usage information
   depends on the completeness of coverage of a workload.
 * Performance and security of the operating system can be analyzed with
   the help of tools such as:
   `perf <https://man7.org/linux/man-pages/man1/perf.1.html>`_,
   `stress-ng <https://www.mankier.com/1/stress-ng>`_,
   `paxtest <https://github.com/opntr/paxtest-freebsd>`_.
 * Once we discover and understand the workload needs, we can focus on them
   to avoid regressions and use it to evaluate safety considerations.

Methodology
===========

`strace <https://man7.org/linux/man-pages/man1/strace.1.html>`_ is a
diagnostic, instructional, and debugging tool and can be used to discover
the system resources in use by a workload. Once we discover and understand
the workload needs, we can focus on them to avoid regressions and use it
to evaluate safety considerations. We use strace tool to trace workloads.

This method of tracing using strace tells us the system calls invoked by
the workload and doesn't include all the system calls that can be invoked
by it. In addition, this tracing method tells us just the code paths within
these system calls that are invoked. As an example, if a workload opens a
file and reads from it successfully, then the success path is the one that
is traced. Any error paths in that system call will not be traced. If there
is a workload that provides full coverage of a workload then the method
outlined here will trace and find all possible code paths. The completeness
of the system usage information depends on the completeness of coverage of a
workload.

The goal is tracing a workload on a system running a default kernel without
requiring custom kernel installs.

How do we gather fine-grained system information?
=================================================

strace tool can be used to trace system calls made by a process and signals
it receives. System calls are the fundamental interface between an
application and the operating system kernel. They enable a program to
request services from the kernel. For instance, the open() system call in
Linux is used to provide access to a file in the file system. strace enables
us to track all the system calls made by an application. It lists all the
system calls made by a process and their resulting output.

You can generate profiling data combining strace and perf record tools to
record the events and information associated with a process. This provides
insight into the process. "perf annotate" tool generates the statistics of
each instruction of the program. This document goes over the details of how
to gather fine-grained information on a workload's usage of system resources.

We used strace to trace the perf, stress-ng, paxtest workloads to illustrate
our methodology to discover resources used by a workload. This process can
be applied to trace other workloads.

Getting the system ready for tracing
====================================

Before we can get started we will show you how to get your system ready.
We assume that you have a Linux distribution running on a physical system
or a virtual machine. Most distributions will include strace command. Let’s
install other tools that aren’t usually included to build Linux kernel.
Please note that the following works on Debian based distributions. You
might have to find equivalent packages on other Linux distributions.

Install tools to build Linux kernel and tools in kernel repository.
scripts/ver_linux is a good way to check if your system already has
the necessary tools::

  sudo apt-get build-essentials flex bison yacc
  sudo apt install libelf-dev systemtap-sdt-dev libaudit-dev libslang2-dev libperl-dev libdw-dev

cscope is a good tool to browse kernel sources. Let's install it now::

  sudo apt-get install cscope

Install stress-ng and paxtest::

  apt-get install stress-ng
  apt-get install paxtest

Workload overview
=================

As mentioned earlier, we used strace to trace perf bench, stress-ng and
paxtest workloads to show how to analyze a workload and identify Linux
subsystems used by these workloads. Let's start with an overview of these
three workloads to get a better understanding of what they do and how to
use them.

perf bench (all) workload
-------------------------

The perf bench command contains multiple multi-threaded microkernel
benchmarks for executing different subsystems in the Linux kernel and
system calls. This allows us to easily measure the impact of changes,
which can help mitigate performance regressions. It also acts as a common
benchmarking framework, enabling developers to easily create test cases,
integrate transparently, and use performance-rich tooling subsystems.

Stress-ng netdev stressor workload
----------------------------------

stress-ng is used for performing stress testing on the kernel. It allows
you to exercise various physical subsystems of the computer, as well as
interfaces of the OS kernel, using "stressor-s". They are available for
CPU, CPU cache, devices, I/O, interrupts, file system, memory, network,
operating system, pipelines, schedulers, and virtual machines. Please refer
to the `stress-ng man-page <https://www.mankier.com/1/stress-ng>`_ to
find the description of all the available stressor-s. The netdev stressor
starts specified number (N) of workers that exercise various netdevice
ioctl commands across all the available network devices.

paxtest kiddie workload
-----------------------

paxtest is a program that tests buffer overflows in the kernel. It tests
kernel enforcements over memory usage. Generally, execution in some memory
segments makes buffer overflows possible. It runs a set of programs that
attempt to subvert memory usage. It is used as a regression test suite for
PaX, but might be useful to test other memory protection patches for the
kernel. We used paxtest kiddie mode which looks for simple vulnerabilities.

What is strace and how do we use it?
====================================

As mentioned earlier, strace which is a useful diagnostic, instructional,
and debugging tool and can be used to discover the system resources in use
by a workload. It can be used:

 * To see how a process interacts with the kernel.
 * To see why a process is failing or hanging.
 * For reverse engineering a process.
 * To find the files on which a program depends.
 * For analyzing the performance of an application.
 * For troubleshooting various problems related to the operating system.

In addition, strace can generate run-time statistics on times, calls, and
errors for each system call and report a summary when program exits,
suppressing the regular output. This attempts to show system time (CPU time
spent running in the kernel) independent of wall clock time. We plan to use
these features to get information on workload system usage.

strace command supports basic, verbose, and stats modes. strace command when
run in verbose mode gives more detailed information about the system calls
invoked by a process.

Running strace -c generates a report of the percentage of time spent in each
system call, the total time in seconds, the microseconds per call, the total
number of calls, the count of each system call that has failed with an error
and the type of system call made.

 * Usage: strace <command we want to trace>
 * Verbose mode usage: strace -v <command>
 * Gather statistics: strace -c <command>

We used the “-c” option to gather fine-grained run-time statistics in use
by three workloads we have chose for this analysis.

 * perf
 * stress-ng
 * paxtest

What is cscope and how do we use it?
====================================

Now let’s look at `cscope <https://cscope.sourceforge.net/>`_, a command
line tool for browsing C, C++ or Java code-bases. We can use it to find
all the references to a symbol, global definitions, functions called by a
function, functions calling a function, text strings, regular expression
patterns, files including a file.

We can use cscope to find which system call belongs to which subsystem.
This way we can find the kernel subsystems used by a process when it is
executed.

Let’s checkout the latest Linux repository and build cscope database::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git linux
  cd linux
  cscope -R -p10  # builds cscope.out database before starting browse session
  cscope -d -p10  # starts browse session on cscope.out database

Note: Run "cscope -R -p10" to build the database and c"scope -d -p10" to
enter into the browsing session. cscope by default cscope.out database.
To get out of this mode press ctrl+d. -p option is used to specify the
number of file path components to display. -p10 is optimal for browsing
kernel sources.

What is perf and how do we use it?
==================================

Perf is an analysis tool based on Linux 2.6+ systems, which abstracts the
CPU hardware difference in performance measurement in Linux, and provides
a simple command line interface. Perf is based on the perf_events interface
exported by the kernel. It is very useful for profiling the system and
finding performance bottlenecks in an application.

If you haven't already checked out the Linux mainline repository, you can do
so and then build kernel and perf tool::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git linux
  cd linux
  make -j3 all
  cd tools/perf
  make

Note: The perf command can be built without building the kernel in the
repository and can be run on older kernels. However matching the kernel
and perf revisions gives more accurate information on the subsystem usage.

We used "perf stat" and "perf bench" options. For a detailed information on
the perf tool, run "perf -h".

perf stat
---------
The perf stat command generates a report of various hardware and software
events. It does so with the help of hardware counter registers found in
modern CPUs that keep the count of these activities. "perf stat cal" shows
stats for cal command.

Perf bench
----------
The perf bench command contains multiple multi-threaded microkernel
benchmarks for executing different subsystems in the Linux kernel and
system calls. This allows us to easily measure the impact of changes,
which can help mitigate performance regressions. It also acts as a common
benchmarking framework, enabling developers to easily create test cases,
integrate transparently, and use performance-rich tooling.

"perf bench all" command runs the following benchmarks:

 * sched/messaging
 * sched/pipe
 * syscall/basic
 * mem/memcpy
 * mem/memset

What is stress-ng and how do we use it?
=======================================

As mentioned earlier, stress-ng is used for performing stress testing on
the kernel. It allows you to exercise various physical subsystems of the
computer, as well as interfaces of the OS kernel, using stressor-s. They
are available for CPU, CPU cache, devices, I/O, interrupts, file system,
memory, network, operating system, pipelines, schedulers, and virtual
machines.

The netdev stressor starts N workers that exercise various netdevice ioctl
commands across all the available network devices. The following ioctls are
exercised:

 * SIOCGIFCONF, SIOCGIFINDEX, SIOCGIFNAME, SIOCGIFFLAGS
 * SIOCGIFADDR, SIOCGIFNETMASK, SIOCGIFMETRIC, SIOCGIFMTU
 * SIOCGIFHWADDR, SIOCGIFMAP, SIOCGIFTXQLEN

The following command runs the stressor::

  stress-ng --netdev 1 -t 60 --metrics command.

We can use the perf record command to record the events and information
associated with a process. This command records the profiling data in the
perf.data file in the same directory.

Using the following commands you can record the events associated with the
netdev stressor, view the generated report perf.data and annotate the to
view the statistics of each instruction of the program::

  perf record stress-ng --netdev 1 -t 60 --metrics command.
  perf report
  perf annotate

What is paxtest and how do we use it?
=====================================

paxtest is a program that tests buffer overflows in the kernel. It tests
kernel enforcements over memory usage. Generally, execution in some memory
segments makes buffer overflows possible. It runs a set of programs that
attempt to subvert memory usage. It is used as a regression test suite for
PaX, and will be useful to test other memory protection patches for the
kernel.

paxtest provides kiddie and blackhat modes. The paxtest kiddie mode runs
in normal mode, whereas the blackhat mode tries to get around the protection
of the kernel testing for vulnerabilities. We focus on the kiddie mode here
and combine "paxtest kiddie" run with "perf record" to collect CPU stack
traces for the paxtest kiddie run to see which function is calling other
functions in the performance profile. Then the "dwarf" (DWARF's Call Frame
Information) mode can be used to unwind the stack.

The following command can be used to view resulting report in call-graph
format::

  perf record --call-graph dwarf paxtest kiddie
  perf report --stdio

Tracing workloads
=================

Now that we understand the workloads, let's start tracing them.

Tracing perf bench all workload
-------------------------------

Run the following command to trace perf bench all workload::

 strace -c perf bench all

**System Calls made by the workload**

The below table shows the system calls invoked by the workload, number of
times each system call is invoked, and the corresponding Linux subsystem.

+-------------------+-----------+-----------------+-------------------------+
| System Call       | # calls   | Linux Subsystem | System Call (API)       |
+===================+===========+=================+=========================+
| getppid           | 10000001  | Process Mgmt    | sys_getpid()            |
+-------------------+-----------+-----------------+-------------------------+
| clone             | 1077      | Process Mgmt.   | sys_clone()             |
+-------------------+-----------+-----------------+-------------------------+
| prctl             | 23        | Process Mgmt.   | sys_prctl()             |
+-------------------+-----------+-----------------+-------------------------+
| prlimit64         | 7         | Process Mgmt.   | sys_prlimit64()         |
+-------------------+-----------+-----------------+-------------------------+
| getpid            | 10        | Process Mgmt.   | sys_getpid()            |
+-------------------+-----------+-----------------+-------------------------+
| uname             | 3         | Process Mgmt.   | sys_uname()             |
+-------------------+-----------+-----------------+-------------------------+
| sysinfo           | 1         | Process Mgmt.   | sys_sysinfo()           |
+-------------------+-----------+-----------------+-------------------------+
| getuid            | 1         | Process Mgmt.   | sys_getuid()            |
+-------------------+-----------+-----------------+-------------------------+
| getgid            | 1         | Process Mgmt.   | sys_getgid()            |
+-------------------+-----------+-----------------+-------------------------+
| geteuid           | 1         | Process Mgmt.   | sys_geteuid()           |
+-------------------+-----------+-----------------+-------------------------+
| getegid           | 1         | Process Mgmt.   | sys_getegid             |
+-------------------+-----------+-----------------+-------------------------+
| close             | 49951     | Filesystem      | sys_close()             |
+-------------------+-----------+-----------------+-------------------------+
| pipe              | 604       | Filesystem      | sys_pipe()              |
+-------------------+-----------+-----------------+-------------------------+
| openat            | 48560     | Filesystem      | sys_opennat()           |
+-------------------+-----------+-----------------+-------------------------+
| fstat             | 8338      | Filesystem      | sys_fstat()             |
+-------------------+-----------+-----------------+-------------------------+
| stat              | 1573      | Filesystem      | sys_stat()              |
+-------------------+-----------+-----------------+-------------------------+
| pread64           | 9646      | Filesystem      | sys_pread64()           |
+-------------------+-----------+-----------------+-------------------------+
| getdents64        | 1873      | Filesystem      | sys_getdents64()        |
+-------------------+-----------+-----------------+-------------------------+
| access            | 3         | Filesystem      | sys_access()            |
+-------------------+-----------+-----------------+-------------------------+
| lstat             | 1880      | Filesystem      | sys_lstat()             |
+-------------------+-----------+-----------------+-------------------------+
| lseek             | 6         | Filesystem      | sys_lseek()             |
+-------------------+-----------+-----------------+-------------------------+
| ioctl             | 3         | Filesystem      | sys_ioctl()             |
+-------------------+-----------+-----------------+-------------------------+
| dup2              | 1         | Filesystem      | sys_dup2()              |
+-------------------+-----------+-----------------+-------------------------+
| execve            | 2         | Filesystem      | sys_execve()            |
+-------------------+-----------+-----------------+-------------------------+
| fcntl             | 8779      | Filesystem      | sys_fcntl()             |
+-------------------+-----------+-----------------+-------------------------+
| statfs            | 1         | Filesystem      | sys_statfs()            |
+-------------------+-----------+-----------------+-------------------------+
| epoll_create      | 2         | Filesystem      | sys_epoll_create()      |
+-------------------+-----------+-----------------+-------------------------+
| epoll_ctl         | 64        | Filesystem      | sys_epoll_ctl()         |
+-------------------+-----------+-----------------+-------------------------+
| newfstatat        | 8318      | Filesystem      | sys_newfstatat()        |
+-------------------+-----------+-----------------+-------------------------+
| eventfd2          | 192       | Filesystem      | sys_eventfd2()          |
+-------------------+-----------+-----------------+-------------------------+
| mmap              | 243       | Memory Mgmt.    | sys_mmap()              |
+-------------------+-----------+-----------------+-------------------------+
| mprotect          | 32        | Memory Mgmt.    | sys_mprotect()          |
+-------------------+-----------+-----------------+-------------------------+
| brk               | 21        | Memory Mgmt.    | sys_brk()               |
+-------------------+-----------+-----------------+-------------------------+
| munmap            | 128       | Memory Mgmt.    | sys_munmap()            |
+-------------------+-----------+-----------------+-------------------------+
| set_mempolicy     | 156       | Memory Mgmt.    | sys_set_mempolicy()     |
+-------------------+-----------+-----------------+-------------------------+
| set_tid_address   | 1         | Process Mgmt.   | sys_set_tid_address()   |
+-------------------+-----------+-----------------+-------------------------+
| set_robust_list   | 1         | Futex           | sys_set_robust_list()   |
+-------------------+-----------+-----------------+-------------------------+
| futex             | 341       | Futex           | sys_futex()             |
+-------------------+-----------+-----------------+-------------------------+
| sched_getaffinity | 79        | Scheduler       | sys_sched_getaffinity() |
+-------------------+-----------+-----------------+-------------------------+
| sched_setaffinity | 223       | Scheduler       | sys_sched_setaffinity() |
+-------------------+-----------+-----------------+-------------------------+
| socketpair        | 202       | Network         | sys_socketpair()        |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigprocmask    | 21        | Signal          | sys_rt_sigprocmask()    |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigaction      | 36        | Signal          | sys_rt_sigaction()      |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigreturn      | 2         | Signal          | sys_rt_sigreturn()      |
+-------------------+-----------+-----------------+-------------------------+
| wait4             | 889       | Time            | sys_wait4()             |
+-------------------+-----------+-----------------+-------------------------+
| clock_nanosleep   | 37        | Time            | sys_clock_nanosleep()   |
+-------------------+-----------+-----------------+-------------------------+
| capget            | 4         | Capability      | sys_capget()            |
+-------------------+-----------+-----------------+-------------------------+

Tracing stress-ng netdev stressor workload
------------------------------------------

Run the following command to trace stress-ng netdev stressor workload::

  strace -c  stress-ng --netdev 1 -t 60 --metrics

**System Calls made by the workload**

The below table shows the system calls invoked by the workload, number of
times each system call is invoked, and the corresponding Linux subsystem.

+-------------------+-----------+-----------------+-------------------------+
| System Call       | # calls   | Linux Subsystem | System Call (API)       |
+===================+===========+=================+=========================+
| openat            | 74        | Filesystem      | sys_openat()            |
+-------------------+-----------+-----------------+-------------------------+
| close             | 75        | Filesystem      | sys_close()             |
+-------------------+-----------+-----------------+-------------------------+
| read              | 58        | Filesystem      | sys_read()              |
+-------------------+-----------+-----------------+-------------------------+
| fstat             | 20        | Filesystem      | sys_fstat()             |
+-------------------+-----------+-----------------+-------------------------+
| flock             | 10        | Filesystem      | sys_flock()             |
+-------------------+-----------+-----------------+-------------------------+
| write             | 7         | Filesystem      | sys_write()             |
+-------------------+-----------+-----------------+-------------------------+
| getdents64        | 8         | Filesystem      | sys_getdents64()        |
+-------------------+-----------+-----------------+-------------------------+
| pread64           | 8         | Filesystem      | sys_pread64()           |
+-------------------+-----------+-----------------+-------------------------+
| lseek             | 1         | Filesystem      | sys_lseek()             |
+-------------------+-----------+-----------------+-------------------------+
| access            | 2         | Filesystem      | sys_access()            |
+-------------------+-----------+-----------------+-------------------------+
| getcwd            | 1         | Filesystem      | sys_getcwd()            |
+-------------------+-----------+-----------------+-------------------------+
| execve            | 1         | Filesystem      | sys_execve()            |
+-------------------+-----------+-----------------+-------------------------+
| mmap              | 61        | Memory Mgmt.    | sys_mmap()              |
+-------------------+-----------+-----------------+-------------------------+
| munmap            | 3         | Memory Mgmt.    | sys_munmap()            |
+-------------------+-----------+-----------------+-------------------------+
| mprotect          | 20        | Memory Mgmt.    | sys_mprotect()          |
+-------------------+-----------+-----------------+-------------------------+
| mlock             | 2         | Memory Mgmt.    | sys_mlock()             |
+-------------------+-----------+-----------------+-------------------------+
| brk               | 3         | Memory Mgmt.    | sys_brk()               |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigaction      | 21        | Signal          | sys_rt_sigaction()      |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigprocmask    | 1         | Signal          | sys_rt_sigprocmask()    |
+-------------------+-----------+-----------------+-------------------------+
| sigaltstack       | 1         | Signal          | sys_sigaltstack()       |
+-------------------+-----------+-----------------+-------------------------+
| rt_sigreturn      | 1         | Signal          | sys_rt_sigreturn()      |
+-------------------+-----------+-----------------+-------------------------+
| getpid            | 8         | Process Mgmt.   | sys_getpid()            |
+-------------------+-----------+-----------------+-------------------------+
| prlimit64         | 5         | Process Mgmt.   | sys_prlimit64()         |
+-------------------+-----------+-----------------+-------------------------+
| arch_prctl        | 2         | Process Mgmt.   | sys_arch_prctl()        |
+-------------------+-----------+-----------------+-------------------------+
| sysinfo           | 2         | Process Mgmt.   | sys_sysinfo()           |
+-------------------+-----------+-----------------+-------------------------+
| getuid            | 2         | Process Mgmt.   | sys_getuid()            |
+-------------------+-----------+-----------------+-------------------------+
| uname             | 1         | Process Mgmt.   | sys_uname()             |
+-------------------+-----------+-----------------+-------------------------+
| setpgid           | 1         | Process Mgmt.   | sys_setpgid()           |
+-------------------+-----------+-----------------+-------------------------+
| getrusage         | 1         | Process Mgmt.   | sys_getrusage()         |
+-------------------+-----------+-----------------+-------------------------+
| geteuid           | 1         | Process Mgmt.   | sys_geteuid()           |
+-------------------+-----------+-----------------+-------------------------+
| getppid           | 1         | Process Mgmt.   | sys_getppid()           |
+-------------------+-----------+-----------------+-------------------------+
| sendto            | 3         | Network         | sys_sendto()            |
+-------------------+-----------+-----------------+-------------------------+
| connect           | 1         | Network         | sys_connect()           |
+-------------------+-----------+-----------------+-------------------------+
| socket            | 1         | Network         | sys_socket()            |
+-------------------+-----------+-----------------+-------------------------+
| clone             | 1         | Process Mgmt.   | sys_clone()             |
+-------------------+-----------+-----------------+-------------------------+
| set_tid_address   | 1         | Process Mgmt.   | sys_set_tid_address()   |
+-------------------+-----------+-----------------+-------------------------+
| wait4             | 2         | Time            | sys_wait4()             |
+-------------------+-----------+-----------------+-------------------------+
| alarm             | 1         | Time            | sys_alarm()             |
+-------------------+-----------+-----------------+-------------------------+
| set_robust_list   | 1         | Futex           | sys_set_robust_list()   |
+-------------------+-----------+-----------------+-------------------------+

Tracing paxtest kiddie workload
-------------------------------

Run the following command to trace paxtest kiddie workload::

 strace -c paxtest kiddie

**System Calls made by the workload**

The below table shows the system calls invoked by the workload, number of
times each system call is invoked, and the corresponding Linux subsystem.

+-------------------+-----------+-----------------+----------------------+
| System Call       | # calls   | Linux Subsystem | System Call (API)    |
+===================+===========+=================+======================+
| read              | 3         | Filesystem      | sys_read()           |
+-------------------+-----------+-----------------+----------------------+
| write             | 11        | Filesystem      | sys_write()          |
+-------------------+-----------+-----------------+----------------------+
| close             | 41        | Filesystem      | sys_close()          |
+-------------------+-----------+-----------------+----------------------+
| stat              | 24        | Filesystem      | sys_stat()           |
+-------------------+-----------+-----------------+----------------------+
| fstat             | 2         | Filesystem      | sys_fstat()          |
+-------------------+-----------+-----------------+----------------------+
| pread64           | 6         | Filesystem      | sys_pread64()        |
+-------------------+-----------+-----------------+----------------------+
| access            | 1         | Filesystem      | sys_access()         |
+-------------------+-----------+-----------------+----------------------+
| pipe              | 1         | Filesystem      | sys_pipe()           |
+-------------------+-----------+-----------------+----------------------+
| dup2              | 24        | Filesystem      | sys_dup2()           |
+-------------------+-----------+-----------------+----------------------+
| execve            | 1         | Filesystem      | sys_execve()         |
+-------------------+-----------+-----------------+----------------------+
| fcntl             | 26        | Filesystem      | sys_fcntl()          |
+-------------------+-----------+-----------------+----------------------+
| openat            | 14        | Filesystem      | sys_openat()         |
+-------------------+-----------+-----------------+----------------------+
| rt_sigaction      | 7         | Signal          | sys_rt_sigaction()   |
+-------------------+-----------+-----------------+----------------------+
| rt_sigreturn      | 38        | Signal          | sys_rt_sigreturn()   |
+-------------------+-----------+-----------------+----------------------+
| clone             | 38        | Process Mgmt.   | sys_clone()          |
+-------------------+-----------+-----------------+----------------------+
| wait4             | 44        | Time            | sys_wait4()          |
+-------------------+-----------+-----------------+----------------------+
| mmap              | 7         | Memory Mgmt.    | sys_mmap()           |
+-------------------+-----------+-----------------+----------------------+
| mprotect          | 3         | Memory Mgmt.    | sys_mprotect()       |
+-------------------+-----------+-----------------+----------------------+
| munmap            | 1         | Memory Mgmt.    | sys_munmap()         |
+-------------------+-----------+-----------------+----------------------+
| brk               | 3         | Memory Mgmt.    | sys_brk()            |
+-------------------+-----------+-----------------+----------------------+
| getpid            | 1         | Process Mgmt.   | sys_getpid()         |
+-------------------+-----------+-----------------+----------------------+
| getuid            | 1         | Process Mgmt.   | sys_getuid()         |
+-------------------+-----------+-----------------+----------------------+
| getgid            | 1         | Process Mgmt.   | sys_getgid()         |
+-------------------+-----------+-----------------+----------------------+
| geteuid           | 2         | Process Mgmt.   | sys_geteuid()        |
+-------------------+-----------+-----------------+----------------------+
| getegid           | 1         | Process Mgmt.   | sys_getegid()        |
+-------------------+-----------+-----------------+----------------------+
| getppid           | 1         | Process Mgmt.   | sys_getppid()        |
+-------------------+-----------+-----------------+----------------------+
| arch_prctl        | 2         | Process Mgmt.   | sys_arch_prctl()     |
+-------------------+-----------+-----------------+----------------------+

Conclusion
==========

This document is intended to be used as a guide on how to gather fine-grained
information on the resources in use by workloads using strace.

References
==========

 * `Discovery Linux Kernel Subsystems used by OpenAPS <https://elisa.tech/blog/2022/02/02/discovery-linux-kernel-subsystems-used-by-openaps>`_
 * `ELISA-White-Papers-Discovering Linux kernel subsystems used by a workload <https://github.com/elisa-tech/ELISA-White-Papers/blob/master/Processes/Discovering_Linux_kernel_subsystems_used_by_a_workload.md>`_
 * `strace <https://man7.org/linux/man-pages/man1/strace.1.html>`_
 * `perf <https://man7.org/linux/man-pages/man1/perf.1.html>`_
 * `paxtest README <https://github.com/opntr/paxtest-freebsd/blob/hardenedbsd/0.9.14-hbsd/README>`_
 * `stress-ng <https://www.mankier.com/1/stress-ng>`_
 * `Monitoring and managing system status and performance <https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/monitoring_and_managing_system_status_and_performance/index>`_

.. SPDX-License-Identifier: GPL-2.0

==================================
Eprobe - Event-based Probe Tracing
==================================

:Author: Steven Rostedt <rostedt@goodmis.org>

- Written for v6.17

Overview
========

Eprobes are dynamic events that are placed on existing events to either
dereference a field that is a pointer, or simply to limit what fields are
recorded in the trace event.

Eprobes depend on kprobe events so to enable this feature, build your kernel
with CONFIG_EPROBE_EVENTS=y.

Eprobes are created via the /sys/kernel/tracing/dynamic_events file.

Synopsis of eprobe_events
-------------------------
::

  e[:[EGRP/][EEVENT]] GRP.EVENT [FETCHARGS]	: Set a probe
  -:[EGRP/][EEVENT]				: Clear a probe

 EGRP		: Group name of the new event. If omitted, use "eprobes" for it.
 EEVENT		: Event name. If omitted, the event name is generated and will
		  be the same event name as the event it attached to.
 GRP		: Group name of the event to attach to.
 EVENT		: Event name of the event to attach to.

 FETCHARGS	: Arguments. Each probe can have up to 128 args.
  $FIELD	: Fetch the value of the event field called FIELD.
  @ADDR		: Fetch memory at ADDR (ADDR should be in kernel)
  @SYM[+|-offs]	: Fetch memory at SYM +|- offs (SYM should be a data symbol)
  $comm		: Fetch current task comm.
  +|-[u]OFFS(FETCHARG) : Fetch memory at FETCHARG +|- OFFS address.(\*3)(\*4)
  \IMM		: Store an immediate value to the argument.
  NAME=FETCHARG : Set NAME as the argument name of FETCHARG.
  FETCHARG:TYPE : Set TYPE as the type of FETCHARG. Currently, basic types
		  (u8/u16/u32/u64/s8/s16/s32/s64), hexadecimal types
		  (x8/x16/x32/x64), VFS layer common type(%pd/%pD), "char",
                  "string", "ustring", "symbol", "symstr" and "bitfield" are
                  supported.

Types
-----
The FETCHARGS above is very similar to the kprobe events as described in
Documentation/trace/kprobetrace.rst.

The difference between eprobes and kprobes FETCHARGS is that eprobes has a
$FIELD command that returns the content of the event field of the event
that is attached. Eprobes do not have access to registers, stacks and function
arguments that kprobes has.

If a field argument is a pointer, it may be dereferenced just like a memory
address using the FETCHARGS syntax.


Attaching to dynamic events
---------------------------

Eprobes may attach to dynamic events as well as to normal events. It may
attach to a kprobe event, a synthetic event or a fprobe event. This is useful
if the type of a field needs to be changed. See Example 2 below.

Usage examples
==============

Example 1
---------

The basic usage of eprobes is to limit the data that is being recorded into
the tracing buffer. For example, a common event to trace is the sched_switch
trace event. That has a format of::

	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:char prev_comm[16];	offset:8;	size:16;	signed:0;
	field:pid_t prev_pid;	offset:24;	size:4;	signed:1;
	field:int prev_prio;	offset:28;	size:4;	signed:1;
	field:long prev_state;	offset:32;	size:8;	signed:1;
	field:char next_comm[16];	offset:40;	size:16;	signed:0;
	field:pid_t next_pid;	offset:56;	size:4;	signed:1;
	field:int next_prio;	offset:60;	size:4;	signed:1;

The first four fields are common to all events and can not be limited. But the
rest of the event has 60 bytes of information. It records the names of the
previous and next tasks being scheduled out and in, as well as their pids and
priorities. It also records the state of the previous task. If only the pids
of the tasks are of interest, why waste the ring buffer with all the other
fields?

An eprobe can limit what gets recorded. Note, it does not help in performance,
as all the fields are recorded in a temporary buffer to process the eprobe.
::

 # echo 'e:sched/switch sched.sched_switch prev=$prev_pid:u32 next=$next_pid:u32' >> /sys/kernel/tracing/dynamic_events
 # echo 1 > /sys/kernel/tracing/events/sched/switch/enable
 # cat /sys/kernel/tracing/trace

 # tracer: nop
 #
 # entries-in-buffer/entries-written: 2721/2721   #P:8
 #
 #                                _-----=> irqs-off/BH-disabled
 #                               / _----=> need-resched
 #                              | / _---=> hardirq/softirq
 #                              || / _--=> preempt-depth
 #                              ||| / _-=> migrate-disable
 #                              |||| /     delay
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
     sshd-session-1082    [004] d..4.  5041.239906: switch: (sched.sched_switch) prev=1082 next=0
             bash-1085    [001] d..4.  5041.240198: switch: (sched.sched_switch) prev=1085 next=141
    kworker/u34:5-141     [001] d..4.  5041.240259: switch: (sched.sched_switch) prev=141 next=1085
           <idle>-0       [004] d..4.  5041.240354: switch: (sched.sched_switch) prev=0 next=1082
             bash-1085    [001] d..4.  5041.240385: switch: (sched.sched_switch) prev=1085 next=141
    kworker/u34:5-141     [001] d..4.  5041.240410: switch: (sched.sched_switch) prev=141 next=1085
             bash-1085    [001] d..4.  5041.240478: switch: (sched.sched_switch) prev=1085 next=0
     sshd-session-1082    [004] d..4.  5041.240526: switch: (sched.sched_switch) prev=1082 next=0
           <idle>-0       [001] d..4.  5041.247524: switch: (sched.sched_switch) prev=0 next=90
           <idle>-0       [002] d..4.  5041.247545: switch: (sched.sched_switch) prev=0 next=16
      kworker/1:1-90      [001] d..4.  5041.247580: switch: (sched.sched_switch) prev=90 next=0
        rcu_sched-16      [002] d..4.  5041.247591: switch: (sched.sched_switch) prev=16 next=0
           <idle>-0       [002] d..4.  5041.257536: switch: (sched.sched_switch) prev=0 next=16
        rcu_sched-16      [002] d..4.  5041.257573: switch: (sched.sched_switch) prev=16 next=0

Note, without adding the "u32" after the prev_pid and next_pid, the values
would default showing in hexadecimal.

Example 2
---------

If a specific system call is to be recorded but the syscalls events are not
enabled, the raw_syscalls can still be used (syscalls are system call
events are not normal events, but are created from the raw_syscalls events
within the kernel). In order to trace the openat system call, one can create
an event probe on top of the raw_syscalls event:
::

 # cd /sys/kernel/tracing
 # cat events/raw_syscalls/sys_enter/format
 name: sys_enter
 ID: 395
 format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:long id;	offset:8;	size:8;	signed:1;
	field:unsigned long args[6];	offset:16;	size:48;	signed:0;

 print fmt: "NR %ld (%lx, %lx, %lx, %lx, %lx, %lx)", REC->id, REC->args[0], REC->args[1], REC->args[2], REC->args[3], REC->args[4], REC->args[5]

From the source code, the sys_openat() has:
::

 int sys_openat(int dirfd, const char *path, int flags, mode_t mode)
 {
	return my_syscall4(__NR_openat, dirfd, path, flags, mode);
 }

The path is the second parameter, and that is what is wanted.
::

 # echo 'e:openat raw_syscalls.sys_enter nr=$id filename=+8($args):ustring' >> dynamic_events

This is being run on x86_64 where the word size is 8 bytes and the openat
system call __NR_openat is set at 257.
::

 # echo 'nr == 257' > events/eprobes/openat/filter

Now enable the event and look at the trace.
::

 # echo 1 > events/eprobes/openat/enable
 # cat trace

 # tracer: nop
 #
 # entries-in-buffer/entries-written: 4/4   #P:8
 #
 #                                _-----=> irqs-off/BH-disabled
 #                               / _----=> need-resched
 #                              | / _---=> hardirq/softirq
 #                              || / _--=> preempt-depth
 #                              ||| / _-=> migrate-disable
 #                              |||| /     delay
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
              cat-1298    [003] ...2.  2060.875970: openat: (raw_syscalls.sys_enter) nr=0x101 filename=(fault)
              cat-1298    [003] ...2.  2060.876197: openat: (raw_syscalls.sys_enter) nr=0x101 filename=(fault)
              cat-1298    [003] ...2.  2060.879126: openat: (raw_syscalls.sys_enter) nr=0x101 filename=(fault)
              cat-1298    [003] ...2.  2060.879639: openat: (raw_syscalls.sys_enter) nr=0x101 filename=(fault)

The filename shows "(fault)". This is likely because the filename has not been
pulled into memory yet and currently trace events cannot fault in memory that
is not present. When an eprobe tries to read memory that has not been faulted
in yet, it will show the "(fault)" text.

To get around this, as the kernel will likely pull in this filename and make
it present, attaching it to a synthetic event that can pass the address of the
filename from the entry of the event to the end of the event, this can be used
to show the filename when the system call returns.

Remove the old eprobe::

 # echo 1 > events/eprobes/openat/enable
 # echo '-:openat' >> dynamic_events

This time make an eprobe where the address of the filename is saved::

 # echo 'e:openat_start raw_syscalls.sys_enter nr=$id filename=+8($args):x64' >> dynamic_events

Create a synthetic event that passes the address of the filename to the
end of the event::

 # echo 's:filename u64 file' >> dynamic_events
 # echo 'hist:keys=common_pid:f=filename if nr == 257' > events/eprobes/openat_start/trigger
 # echo 'hist:keys=common_pid:file=$f:onmatch(eprobes.openat_start).trace(filename,$file) if id == 257' > events/raw_syscalls/sys_exit/trigger

Now that the address of the filename has been passed to the end of the
system call, create another eprobe to attach to the exit event to show the
string::

 # echo 'e:openat synthetic.filename filename=+0($file):ustring' >> dynamic_events
 # echo 1 > events/eprobes/openat/enable
 # cat trace

 # tracer: nop
 #
 # entries-in-buffer/entries-written: 4/4   #P:8
 #
 #                                _-----=> irqs-off/BH-disabled
 #                               / _----=> need-resched
 #                              | / _---=> hardirq/softirq
 #                              || / _--=> preempt-depth
 #                              ||| / _-=> migrate-disable
 #                              |||| /     delay
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
              cat-1331    [001] ...5.  2944.787977: openat: (synthetic.filename) filename="/etc/ld.so.cache"
              cat-1331    [001] ...5.  2944.788480: openat: (synthetic.filename) filename="/lib/x86_64-linux-gnu/libc.so.6"
              cat-1331    [001] ...5.  2944.793426: openat: (synthetic.filename) filename="/usr/lib/locale/locale-archive"
              cat-1331    [001] ...5.  2944.831362: openat: (synthetic.filename) filename="trace"

Example 3
---------

If syscall trace events are available, the above would not need the first
eprobe, but it would still need the last one::

 # echo 's:filename u64 file' >> dynamic_events
 # echo 'hist:keys=common_pid:f=filename' > events/syscalls/sys_enter_openat/trigger
 # echo 'hist:keys=common_pid:file=$f:onmatch(syscalls.sys_enter_openat).trace(filename,$file)' > events/syscalls/sys_exit_openat/trigger
 # echo 'e:openat synthetic.filename filename=+0($file):ustring' >> dynamic_events
 # echo 1 > events/eprobes/openat/enable

And this would produce the same result as Example 2.

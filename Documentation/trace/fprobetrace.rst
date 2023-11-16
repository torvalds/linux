.. SPDX-License-Identifier: GPL-2.0

==========================
Fprobe-based Event Tracing
==========================

.. Author: Masami Hiramatsu <mhiramat@kernel.org>

Overview
--------

Fprobe event is similar to the kprobe event, but limited to probe on
the function entry and exit only. It is good enough for many use cases
which only traces some specific functions.

This document also covers tracepoint probe events (tprobe) since this
is also works only on the tracepoint entry. User can trace a part of
tracepoint argument, or the tracepoint without trace-event, which is
not exposed on tracefs.

As same as other dynamic events, fprobe events and tracepoint probe
events are defined via `dynamic_events` interface file on tracefs.

Synopsis of fprobe-events
-------------------------
::

  f[:[GRP1/][EVENT1]] SYM [FETCHARGS]                       : Probe on function entry
  f[MAXACTIVE][:[GRP1/][EVENT1]] SYM%return [FETCHARGS]     : Probe on function exit
  t[:[GRP2/][EVENT2]] TRACEPOINT [FETCHARGS]                : Probe on tracepoint

 GRP1           : Group name for fprobe. If omitted, use "fprobes" for it.
 GRP2           : Group name for tprobe. If omitted, use "tracepoints" for it.
 EVENT1         : Event name for fprobe. If omitted, the event name is
                  "SYM__entry" or "SYM__exit".
 EVENT2         : Event name for tprobe. If omitted, the event name is
                  the same as "TRACEPOINT", but if the "TRACEPOINT" starts
                  with a digit character, "_TRACEPOINT" is used.
 MAXACTIVE      : Maximum number of instances of the specified function that
                  can be probed simultaneously, or 0 for the default value
                  as defined in Documentation/trace/fprobe.rst

 FETCHARGS      : Arguments. Each probe can have up to 128 args.
  ARG           : Fetch "ARG" function argument using BTF (only for function
                  entry or tracepoint.) (\*1)
  @ADDR         : Fetch memory at ADDR (ADDR should be in kernel)
  @SYM[+|-offs] : Fetch memory at SYM +|- offs (SYM should be a data symbol)
  $stackN       : Fetch Nth entry of stack (N >= 0)
  $stack        : Fetch stack address.
  $argN         : Fetch the Nth function argument. (N >= 1) (\*2)
  $retval       : Fetch return value.(\*3)
  $comm         : Fetch current task comm.
  +|-[u]OFFS(FETCHARG) : Fetch memory at FETCHARG +|- OFFS address.(\*4)(\*5)
  \IMM          : Store an immediate value to the argument.
  NAME=FETCHARG : Set NAME as the argument name of FETCHARG.
  FETCHARG:TYPE : Set TYPE as the type of FETCHARG. Currently, basic types
                  (u8/u16/u32/u64/s8/s16/s32/s64), hexadecimal types
                  (x8/x16/x32/x64), "char", "string", "ustring", "symbol", "symstr"
                  and bitfield are supported.

  (\*1) This is available only when BTF is enabled.
  (\*2) only for the probe on function entry (offs == 0). Note, this argument access
        is best effort, because depending on the argument type, it may be passed on
        the stack. But this only support the arguments via registers.
  (\*3) only for return probe. Note that this is also best effort. Depending on the
        return value type, it might be passed via a pair of registers. But this only
        accesses one register.
  (\*4) this is useful for fetching a field of data structures.
  (\*5) "u" means user-space dereference.

For the details of TYPE, see :ref:`kprobetrace documentation <kprobetrace_types>`.

BTF arguments
-------------
BTF (BPF Type Format) argument allows user to trace function and tracepoint
parameters by its name instead of ``$argN``. This feature is available if the
kernel is configured with CONFIG_BPF_SYSCALL and CONFIG_DEBUG_INFO_BTF.
If user only specify the BTF argument, the event's argument name is also
automatically set by the given name. ::

 # echo 'f:myprobe vfs_read count pos' >> dynamic_events
 # cat dynamic_events
 f:fprobes/myprobe vfs_read count=count pos=pos

It also chooses the fetch type from BTF information. For example, in the above
example, the ``count`` is unsigned long, and the ``pos`` is a pointer. Thus,
both are converted to 64bit unsigned long, but only ``pos`` has "%Lx"
print-format as below ::

 # cat events/fprobes/myprobe/format
 name: myprobe
 ID: 1313
 format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:unsigned long __probe_ip;	offset:8;	size:8;	signed:0;
	field:u64 count;	offset:16;	size:8;	signed:0;
	field:u64 pos;	offset:24;	size:8;	signed:0;

 print fmt: "(%lx) count=%Lu pos=0x%Lx", REC->__probe_ip, REC->count, REC->pos

If user unsures the name of arguments, ``$arg*`` will be helpful. The ``$arg*``
is expanded to all function arguments of the function or the tracepoint. ::

 # echo 'f:myprobe vfs_read $arg*' >> dynamic_events
 # cat dynamic_events
 f:fprobes/myprobe vfs_read file=file buf=buf count=count pos=pos

BTF also affects the ``$retval``. If user doesn't set any type, the retval
type is automatically picked from the BTF. If the function returns ``void``,
``$retval`` is rejected.

You can access the data fields of a data structure using allow operator ``->``
(for pointer type) and dot operator ``.`` (for data structure type.)::

# echo 't sched_switch preempt prev_pid=prev->pid next_pid=next->pid' >> dynamic_events

The field access operators, ``->`` and ``.`` can be combined for accessing deeper
members and other structure members pointed by the member. e.g. ``foo->bar.baz->qux``
If there is non-name union member, you can directly access it as the C code does.
For example::

 struct {
	union {
	int a;
	int b;
	};
 } *foo;

To access ``a`` and ``b``, use ``foo->a`` and ``foo->b`` in this case.

This data field access is available for the return value via ``$retval``,
e.g. ``$retval->name``.

For these BTF arguments and fields, ``:string`` and ``:ustring`` change the
behavior. If these are used for BTF argument or field, it checks whether
the BTF type of the argument or the data field is ``char *`` or ``char []``,
or not.  If not, it rejects applying the string types. Also, with the BTF
support, you don't need a memory dereference operator (``+0(PTR)``) for
accessing the string pointed by a ``PTR``. It automatically adds the memory
dereference operator according to the BTF type. e.g. ::

# echo 't sched_switch prev->comm:string' >> dynamic_events
# echo 'f getname_flags%return $retval->name:string' >> dynamic_events

The ``prev->comm`` is an embedded char array in the data structure, and
``$retval->name`` is a char pointer in the data structure. But in both
cases, you can use ``:string`` type to get the string.


Usage examples
--------------
Here is an example to add fprobe events on ``vfs_read()`` function entry
and exit, with BTF arguments.
::

  # echo 'f vfs_read $arg*' >> dynamic_events
  # echo 'f vfs_read%return $retval' >> dynamic_events
  # cat dynamic_events
 f:fprobes/vfs_read__entry vfs_read file=file buf=buf count=count pos=pos
 f:fprobes/vfs_read__exit vfs_read%return arg1=$retval
  # echo 1 > events/fprobes/enable
  # head -n 20 trace | tail
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
               sh-70      [000] ...1.   335.883195: vfs_read__entry: (vfs_read+0x4/0x340) file=0xffff888005cf9a80 buf=0x7ffef36c6879 count=1 pos=0xffffc900005aff08
               sh-70      [000] .....   335.883208: vfs_read__exit: (ksys_read+0x75/0x100 <- vfs_read) arg1=1
               sh-70      [000] ...1.   335.883220: vfs_read__entry: (vfs_read+0x4/0x340) file=0xffff888005cf9a80 buf=0x7ffef36c6879 count=1 pos=0xffffc900005aff08
               sh-70      [000] .....   335.883224: vfs_read__exit: (ksys_read+0x75/0x100 <- vfs_read) arg1=1
               sh-70      [000] ...1.   335.883232: vfs_read__entry: (vfs_read+0x4/0x340) file=0xffff888005cf9a80 buf=0x7ffef36c687a count=1 pos=0xffffc900005aff08
               sh-70      [000] .....   335.883237: vfs_read__exit: (ksys_read+0x75/0x100 <- vfs_read) arg1=1
               sh-70      [000] ...1.   336.050329: vfs_read__entry: (vfs_read+0x4/0x340) file=0xffff888005cf9a80 buf=0x7ffef36c6879 count=1 pos=0xffffc900005aff08
               sh-70      [000] .....   336.050343: vfs_read__exit: (ksys_read+0x75/0x100 <- vfs_read) arg1=1

You can see all function arguments and return values are recorded as signed int.

Also, here is an example of tracepoint events on ``sched_switch`` tracepoint.
To compare the result, this also enables the ``sched_switch`` traceevent too.
::

  # echo 't sched_switch $arg*' >> dynamic_events
  # echo 1 > events/sched/sched_switch/enable
  # echo 1 > events/tracepoints/sched_switch/enable
  # echo > trace
  # head -n 20 trace | tail
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
               sh-70      [000] d..2.  3912.083993: sched_switch: prev_comm=sh prev_pid=70 prev_prio=120 prev_state=S ==> next_comm=swapper/0 next_pid=0 next_prio=120
               sh-70      [000] d..3.  3912.083995: sched_switch: (__probestub_sched_switch+0x4/0x10) preempt=0 prev=0xffff88800664e100 next=0xffffffff828229c0 prev_state=1
           <idle>-0       [000] d..2.  3912.084183: sched_switch: prev_comm=swapper/0 prev_pid=0 prev_prio=120 prev_state=R ==> next_comm=rcu_preempt next_pid=16 next_prio=120
           <idle>-0       [000] d..3.  3912.084184: sched_switch: (__probestub_sched_switch+0x4/0x10) preempt=0 prev=0xffffffff828229c0 next=0xffff888004208000 prev_state=0
      rcu_preempt-16      [000] d..2.  3912.084196: sched_switch: prev_comm=rcu_preempt prev_pid=16 prev_prio=120 prev_state=I ==> next_comm=swapper/0 next_pid=0 next_prio=120
      rcu_preempt-16      [000] d..3.  3912.084196: sched_switch: (__probestub_sched_switch+0x4/0x10) preempt=0 prev=0xffff888004208000 next=0xffffffff828229c0 prev_state=1026
           <idle>-0       [000] d..2.  3912.085191: sched_switch: prev_comm=swapper/0 prev_pid=0 prev_prio=120 prev_state=R ==> next_comm=rcu_preempt next_pid=16 next_prio=120
           <idle>-0       [000] d..3.  3912.085191: sched_switch: (__probestub_sched_switch+0x4/0x10) preempt=0 prev=0xffffffff828229c0 next=0xffff888004208000 prev_state=0

As you can see, the ``sched_switch`` trace-event shows *cooked* parameters, on
the other hand, the ``sched_switch`` tracepoint probe event shows *raw*
parameters. This means you can access any field values in the task
structure pointed by the ``prev`` and ``next`` arguments.

For example, usually ``task_struct::start_time`` is not traced, but with this
traceprobe event, you can trace that field as below.
::

  # echo 't sched_switch comm=next->comm:string next->start_time' > dynamic_events
  # head -n 20 trace | tail
 #           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION
 #              | |         |   |||||     |         |
               sh-70      [000] d..3.  5606.686577: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="rcu_preempt" usage=1 start_time=245000000
      rcu_preempt-16      [000] d..3.  5606.686602: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="sh" usage=1 start_time=1596095526
               sh-70      [000] d..3.  5606.686637: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="swapper/0" usage=2 start_time=0
           <idle>-0       [000] d..3.  5606.687190: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="rcu_preempt" usage=1 start_time=245000000
      rcu_preempt-16      [000] d..3.  5606.687202: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="swapper/0" usage=2 start_time=0
           <idle>-0       [000] d..3.  5606.690317: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="kworker/0:1" usage=1 start_time=137000000
      kworker/0:1-14      [000] d..3.  5606.690339: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="swapper/0" usage=2 start_time=0
           <idle>-0       [000] d..3.  5606.692368: sched_switch: (__probestub_sched_switch+0x4/0x10) comm="kworker/0:1" usage=1 start_time=137000000

==================
S390 Debug Feature
==================

files:
      - arch/s390/kernel/debug.c
      - arch/s390/include/asm/debug.h

Description:
------------
The goal of this feature is to provide a kernel debug logging API
where log records can be stored efficiently in memory, where each component
(e.g. device drivers) can have one separate debug log.
One purpose of this is to inspect the debug logs after a production system crash
in order to analyze the reason for the crash.

If the system still runs but only a subcomponent which uses dbf fails,
it is possible to look at the debug logs on a live system via the Linux
debugfs filesystem.

The debug feature may also very useful for kernel and driver development.

Design:
-------
Kernel components (e.g. device drivers) can register themselves at the debug
feature with the function call debug_register(). This function initializes a
debug log for the caller. For each debug log exists a number of debug areas
where exactly one is active at one time.  Each debug area consists of contiguous
pages in memory. In the debug areas there are stored debug entries (log records)
which are written by event- and exception-calls.

An event-call writes the specified debug entry to the active debug
area and updates the log pointer for the active area. If the end
of the active debug area is reached, a wrap around is done (ring buffer)
and the next debug entry will be written at the beginning of the active
debug area.

An exception-call writes the specified debug entry to the log and
switches to the next debug area. This is done in order to be sure
that the records which describe the origin of the exception are not
overwritten when a wrap around for the current area occurs.

The debug areas themselves are also ordered in form of a ring buffer.
When an exception is thrown in the last debug area, the following debug
entries are then written again in the very first area.

There are three versions for the event- and exception-calls: One for
logging raw data, one for text and one for numbers.

Each debug entry contains the following data:

- Timestamp
- Cpu-Number of calling task
- Level of debug entry (0...6)
- Return Address to caller
- Flag, if entry is an exception or not

The debug logs can be inspected in a live system through entries in
the debugfs-filesystem. Under the toplevel directory "s390dbf" there is
a directory for each registered component, which is named like the
corresponding component. The debugfs normally should be mounted to
/sys/kernel/debug therefore the debug feature can be accessed under
/sys/kernel/debug/s390dbf.

The content of the directories are files which represent different views
to the debug log. Each component can decide which views should be
used through registering them with the function debug_register_view().
Predefined views for hex/ascii, sprintf and raw binary data are provided.
It is also possible to define other views. The content of
a view can be inspected simply by reading the corresponding debugfs file.

All debug logs have an actual debug level (range from 0 to 6).
The default level is 3. Event and Exception functions have a 'level'
parameter. Only debug entries with a level that is lower or equal
than the actual level are written to the log. This means, when
writing events, high priority log entries should have a low level
value whereas low priority entries should have a high one.
The actual debug level can be changed with the help of the debugfs-filesystem
through writing a number string "x" to the 'level' debugfs file which is
provided for every debug log. Debugging can be switched off completely
by using "-" on the 'level' debugfs file.

Example::

	> echo "-" > /sys/kernel/debug/s390dbf/dasd/level

It is also possible to deactivate the debug feature globally for every
debug log. You can change the behavior using  2 sysctl parameters in
/proc/sys/s390dbf:

There are currently 2 possible triggers, which stop the debug feature
globally. The first possibility is to use the "debug_active" sysctl. If
set to 1 the debug feature is running. If "debug_active" is set to 0 the
debug feature is turned off.

The second trigger which stops the debug feature is a kernel oops.
That prevents the debug feature from overwriting debug information that
happened before the oops. After an oops you can reactivate the debug feature
by piping 1 to /proc/sys/s390dbf/debug_active. Nevertheless, its not
suggested to use an oopsed kernel in a production environment.

If you want to disallow the deactivation of the debug feature, you can use
the "debug_stoppable" sysctl. If you set "debug_stoppable" to 0 the debug
feature cannot be stopped. If the debug feature is already stopped, it
will stay deactivated.

Kernel Interfaces:
------------------

.. kernel-doc:: arch/s390/include/asm/debug.h

Predefined views:
-----------------

::

  debug_info_t *debug_info;
  ...
  debug_info = debug_register ("test", 0, 4, 4 ));
  debug_register_view(debug_info, &debug_test_view);
  for(i = 0; i < 10; i ++) debug_int_event(debug_info, 1, i);

  > cat /sys/kernel/debug/s390dbf/test/myview
  00 00964419734:611402 1 - 00 88042ca   This error...........
  00 00964419734:611405 1 - 00 88042ca   That error...........
  00 00964419734:611408 1 - 00 88042ca   Problem..............
  00 00964419734:611411 1 - 00 88042ca   Something went wrong.
  00 00964419734:611414 1 - 00 88042ca   Everything ok........
  00 00964419734:611417 1 - 00 88042ca   data: 00000005
  00 00964419734:611419 1 - 00 88042ca   data: 00000006
  00 00964419734:611422 1 - 00 88042ca   data: 00000007
  00 00964419734:611425 1 - 00 88042ca   data: 00000008
  00 00964419734:611428 1 - 00 88042ca   data: 00000009

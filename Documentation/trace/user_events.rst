=========================================
user_events: User-based Event Tracing
=========================================

:Author: Beau Belgrave

Overview
--------
User based trace events allow user processes to create events and trace data
that can be viewed via existing tools, such as ftrace and perf.
To enable this feature, build your kernel with CONFIG_USER_EVENTS=y.

Programs can view status of the events via
/sys/kernel/tracing/user_events_status and can both register and write
data out via /sys/kernel/tracing/user_events_data.

Typically programs will register a set of events that they wish to expose to
tools that can read trace_events (such as ftrace and perf). The registration
process tells the kernel which address and bit to reflect if any tool has
enabled the event and data should be written. The registration will give back
a write index which describes the data when a write() or writev() is called
on the /sys/kernel/tracing/user_events_data file.

The structures referenced in this document are contained within the
/include/uapi/linux/user_events.h file in the source tree.

**NOTE:** *Both user_events_status and user_events_data are under the tracefs
filesystem and may be mounted at different paths than above.*

Registering
-----------
Registering within a user process is done via ioctl() out to the
/sys/kernel/tracing/user_events_data file. The command to issue is
DIAG_IOCSREG.

This command takes a packed struct user_reg as an argument::

  struct user_reg {
        /* Input: Size of the user_reg structure being used */
        __u32 size;

        /* Input: Bit in enable address to use */
        __u8 enable_bit;

        /* Input: Enable size in bytes at address */
        __u8 enable_size;

        /* Input: Flags for future use, set to 0 */
        __u16 flags;

        /* Input: Address to update when enabled */
        __u64 enable_addr;

        /* Input: Pointer to string with event name, description and flags */
        __u64 name_args;

        /* Output: Index of the event to use when writing data */
        __u32 write_index;
  } __attribute__((__packed__));

The struct user_reg requires all the above inputs to be set appropriately.

+ size: This must be set to sizeof(struct user_reg).

+ enable_bit: The bit to reflect the event status at the address specified by
  enable_addr.

+ enable_size: The size of the value specified by enable_addr.
  This must be 4 (32-bit) or 8 (64-bit). 64-bit values are only allowed to be
  used on 64-bit kernels, however, 32-bit can be used on all kernels.

+ flags: The flags to use, if any. For the initial version this must be 0.
  Callers should first attempt to use flags and retry without flags to ensure
  support for lower versions of the kernel. If a flag is not supported -EINVAL
  is returned.

+ enable_addr: The address of the value to use to reflect event status. This
  must be naturally aligned and write accessible within the user program.

+ name_args: The name and arguments to describe the event, see command format
  for details.

Upon successful registration the following is set.

+ write_index: The index to use for this file descriptor that represents this
  event when writing out data. The index is unique to this instance of the file
  descriptor that was used for the registration. See writing data for details.

User based events show up under tracefs like any other event under the
subsystem named "user_events". This means tools that wish to attach to the
events need to use /sys/kernel/tracing/events/user_events/[name]/enable
or perf record -e user_events:[name] when attaching/recording.

**NOTE:** The event subsystem name by default is "user_events". Callers should
not assume it will always be "user_events". Operators reserve the right in the
future to change the subsystem name per-process to accomodate event isolation.

Command Format
^^^^^^^^^^^^^^
The command string format is as follows::

  name[:FLAG1[,FLAG2...]] [Field1[;Field2...]]

Supported Flags
^^^^^^^^^^^^^^^
None yet

Field Format
^^^^^^^^^^^^
::

  type name [size]

Basic types are supported (__data_loc, u32, u64, int, char, char[20], etc).
User programs are encouraged to use clearly sized types like u32.

**NOTE:** *Long is not supported since size can vary between user and kernel.*

The size is only valid for types that start with a struct prefix.
This allows user programs to describe custom structs out to tools, if required.

For example, a struct in C that looks like this::

  struct mytype {
    char data[20];
  };

Would be represented by the following field::

  struct mytype myname 20

Deleting
--------
Deleting an event from within a user process is done via ioctl() out to the
/sys/kernel/tracing/user_events_data file. The command to issue is
DIAG_IOCSDEL.

This command only requires a single string specifying the event to delete by
its name. Delete will only succeed if there are no references left to the
event (in both user and kernel space). User programs should use a separate file
to request deletes than the one used for registration due to this.

**NOTE:** By default events will auto-delete when there are no references left
to the event. Flags in the future may change this logic.

Unregistering
-------------
If after registering an event it is no longer wanted to be updated then it can
be disabled via ioctl() out to the /sys/kernel/tracing/user_events_data file.
The command to issue is DIAG_IOCSUNREG. This is different than deleting, where
deleting actually removes the event from the system. Unregistering simply tells
the kernel your process is no longer interested in updates to the event.

This command takes a packed struct user_unreg as an argument::

  struct user_unreg {
        /* Input: Size of the user_unreg structure being used */
        __u32 size;

        /* Input: Bit to unregister */
        __u8 disable_bit;

        /* Input: Reserved, set to 0 */
        __u8 __reserved;

        /* Input: Reserved, set to 0 */
        __u16 __reserved2;

        /* Input: Address to unregister */
        __u64 disable_addr;
  } __attribute__((__packed__));

The struct user_unreg requires all the above inputs to be set appropriately.

+ size: This must be set to sizeof(struct user_unreg).

+ disable_bit: This must be set to the bit to disable (same bit that was
  previously registered via enable_bit).

+ disable_addr: This must be set to the address to disable (same address that was
  previously registered via enable_addr).

**NOTE:** Events are automatically unregistered when execve() is invoked. During
fork() the registered events will be retained and must be unregistered manually
in each process if wanted.

Status
------
When tools attach/record user based events the status of the event is updated
in realtime. This allows user programs to only incur the cost of the write() or
writev() calls when something is actively attached to the event.

The kernel will update the specified bit that was registered for the event as
tools attach/detach from the event. User programs simply check if the bit is set
to see if something is attached or not.

Administrators can easily check the status of all registered events by reading
the user_events_status file directly via a terminal. The output is as follows::

  Name [# Comments]
  ...

  Active: ActiveCount
  Busy: BusyCount

For example, on a system that has a single event the output looks like this::

  test

  Active: 1
  Busy: 0

If a user enables the user event via ftrace, the output would change to this::

  test # Used by ftrace

  Active: 1
  Busy: 1

Writing Data
------------
After registering an event the same fd that was used to register can be used
to write an entry for that event. The write_index returned must be at the start
of the data, then the remaining data is treated as the payload of the event.

For example, if write_index returned was 1 and I wanted to write out an int
payload of the event. Then the data would have to be 8 bytes (2 ints) in size,
with the first 4 bytes being equal to 1 and the last 4 bytes being equal to the
value I want as the payload.

In memory this would look like this::

  int index;
  int payload;

User programs might have well known structs that they wish to use to emit out
as payloads. In those cases writev() can be used, with the first vector being
the index and the following vector(s) being the actual event payload.

For example, if I have a struct like this::

  struct payload {
        int src;
        int dst;
        int flags;
  } __attribute__((__packed__));

It's advised for user programs to do the following::

  struct iovec io[2];
  struct payload e;

  io[0].iov_base = &write_index;
  io[0].iov_len = sizeof(write_index);
  io[1].iov_base = &e;
  io[1].iov_len = sizeof(e);

  writev(fd, (const struct iovec*)io, 2);

**NOTE:** *The write_index is not emitted out into the trace being recorded.*

Example Code
------------
See sample code in samples/user_events.

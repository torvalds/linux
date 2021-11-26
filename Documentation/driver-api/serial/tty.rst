=================
The Lockronomicon
=================

Your guide to the ancient and twisted locking policies of the tty layer and
the warped logic behind them. Beware all ye who read on.


Line Discipline
---------------

Line disciplines are registered with tty_register_ldisc() passing the
discipline number and the ldisc structure. At the point of registration the
discipline must be ready to use and it is possible it will get used before
the call returns success. If the call returns an error then it won't get
called. Do not re-use ldisc numbers as they are part of the userspace ABI
and writing over an existing ldisc will cause demons to eat your computer.
After the return the ldisc data has been copied so you may free your own
copy of the structure. You must not re-register over the top of the line
discipline even with the same data or your computer again will be eaten by
demons.

In order to remove a line discipline call tty_unregister_ldisc().
In ancient times this always worked. In modern times the function will
return -EBUSY if the ldisc is currently in use. Since the ldisc referencing
code manages the module counts this should not usually be a concern.

Heed this warning: the reference count field of the registered copies of the
tty_ldisc structure in the ldisc table counts the number of lines using this
discipline. The reference count of the tty_ldisc structure within a tty
counts the number of active users of the ldisc at this instant. In effect it
counts the number of threads of execution within an ldisc method (plus those
about to enter and exit although this detail matters not).

Line Discipline Methods
-----------------------

TTY side interfaces
^^^^^^^^^^^^^^^^^^^

======================= =======================================================
open()			Called when the line discipline is attached to
			the terminal. No other call into the line
			discipline for this tty will occur until it
			completes successfully. Should initialize any
			state needed by the ldisc, and set receive_room
			in the tty_struct to the maximum amount of data
			the line discipline is willing to accept from the
			driver with a single call to receive_buf().
			Returning an error will prevent the ldisc from
			being attached. Can sleep.

close()			This is called on a terminal when the line
			discipline is being unplugged. At the point of
			execution no further users will enter the
			ldisc code for this tty. Can sleep.

hangup()		Called when the tty line is hung up.
			The line discipline should cease I/O to the tty.
			No further calls into the ldisc code will occur.
			Can sleep.

read()			(optional) A process requests reading data from
			the line. Multiple read calls may occur in parallel
			and the ldisc must deal with serialization issues.
			If not defined, the process will receive an EIO
			error. May sleep.

write()			(optional) A process requests writing data to the
			line. Multiple write calls are serialized by the
			tty layer for the ldisc. If not defined, the
			process will receive an EIO error. May sleep.

flush_buffer()		(optional) May be called at any point between
			open and close, and instructs the line discipline
			to empty its input buffer.

set_termios()		(optional) Called on termios structure changes.
			The caller passes the old termios data and the
			current data is in the tty. Called under the
			termios semaphore so allowed to sleep. Serialized
			against itself only.

poll()			(optional) Check the status for the poll/select
			calls. Multiple poll calls may occur in parallel.
			May sleep.

ioctl()			(optional) Called when an ioctl is handed to the
			tty layer that might be for the ldisc. Multiple
			ioctl calls may occur in parallel. May sleep.

compat_ioctl()		(optional) Called when a 32 bit ioctl is handed
			to the tty layer that might be for the ldisc.
			Multiple ioctl calls may occur in parallel.
			May sleep.
======================= =======================================================

Driver Side Interfaces
^^^^^^^^^^^^^^^^^^^^^^

======================= =======================================================
receive_buf()		(optional) Called by the low-level driver to hand
			a buffer of received bytes to the ldisc for
			processing. The number of bytes is guaranteed not
			to exceed the current value of tty->receive_room.
			All bytes must be processed.

receive_buf2()		(optional) Called by the low-level driver to hand
			a buffer of received bytes to the ldisc for
			processing. Returns the number of bytes processed.

			If both receive_buf() and receive_buf2() are
			defined, receive_buf2() should be preferred.

write_wakeup()		May be called at any point between open and close.
			The TTY_DO_WRITE_WAKEUP flag indicates if a call
			is needed but always races versus calls. Thus the
			ldisc must be careful about setting order and to
			handle unexpected calls. Must not sleep.

			The driver is forbidden from calling this directly
			from the ->write call from the ldisc as the ldisc
			is permitted to call the driver write method from
			this function. In such a situation defer it.

dcd_change()		Report to the tty line the current DCD pin status
			changes and the relative timestamp. The timestamp
			cannot be NULL.
======================= =======================================================


Driver Access
^^^^^^^^^^^^^

Line discipline methods can call the methods of the underlying hardware driver.
These are documented as a part of struct tty_operations.

Flags
^^^^^

Line discipline methods have access to tty->flags field containing the
following interesting flags:

======================= =======================================================
TTY_THROTTLED		Driver input is throttled. The ldisc should call
			tty->driver->unthrottle() in order to resume
			reception when it is ready to process more data.

TTY_DO_WRITE_WAKEUP	If set, causes the driver to call the ldisc's
			write_wakeup() method in order to resume
			transmission when it can accept more data
			to transmit.

TTY_IO_ERROR		If set, causes all subsequent userspace read/write
			calls on the tty to fail, returning -EIO.

TTY_OTHER_CLOSED	Device is a pty and the other side has closed.

TTY_NO_WRITE_SPLIT	Prevent driver from splitting up writes into
			smaller chunks.
======================= =======================================================


Locking
^^^^^^^

Callers to the line discipline functions from the tty layer are required to
take line discipline locks. The same is true of calls from the driver side
but not yet enforced.

Three calls are now provided::

	ldisc = tty_ldisc_ref(tty);

takes a handle to the line discipline in the tty and returns it. If no ldisc
is currently attached or the ldisc is being closed and re-opened at this
point then NULL is returned. While this handle is held the ldisc will not
change or go away::

	tty_ldisc_deref(ldisc)

Returns the ldisc reference and allows the ldisc to be closed. Returning the
reference takes away your right to call the ldisc functions until you take
a new reference::

	ldisc = tty_ldisc_ref_wait(tty);

Performs the same function as tty_ldisc_ref except that it will wait for an
ldisc change to complete and then return a reference to the new ldisc.

While these functions are slightly slower than the old code they should have
minimal impact as most receive logic uses the flip buffers and they only
need to take a reference when they push bits up through the driver.

A caution: The ldisc->open(), ldisc->close() and driver->set_ldisc
functions are called with the ldisc unavailable. Thus tty_ldisc_ref will
fail in this situation if used within these functions. Ldisc and driver
code calling its own functions must be careful in this case.

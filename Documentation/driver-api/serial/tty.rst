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

.. kernel-doc:: include/linux/tty_ldisc.h
   :identifiers: tty_ldisc_ops

Driver Access
^^^^^^^^^^^^^

Line discipline methods can call the methods of the underlying hardware driver.
These are documented as a part of struct tty_operations.

Flags
^^^^^

Line discipline methods have access to :c:member:`tty_struct.flags` field. See
:doc:`tty_struct`.

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

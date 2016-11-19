============================================
The object-lifetime debugging infrastructure
============================================

:Author: Thomas Gleixner

Introduction
============

debugobjects is a generic infrastructure to track the life time of
kernel objects and validate the operations on those.

debugobjects is useful to check for the following error patterns:

-  Activation of uninitialized objects

-  Initialization of active objects

-  Usage of freed/destroyed objects

debugobjects is not changing the data structure of the real object so it
can be compiled in with a minimal runtime impact and enabled on demand
with a kernel command line option.

Howto use debugobjects
======================

A kernel subsystem needs to provide a data structure which describes the
object type and add calls into the debug code at appropriate places. The
data structure to describe the object type needs at minimum the name of
the object type. Optional functions can and should be provided to fixup
detected problems so the kernel can continue to work and the debug
information can be retrieved from a live system instead of hard core
debugging with serial consoles and stack trace transcripts from the
monitor.

The debug calls provided by debugobjects are:

-  debug_object_init

-  debug_object_init_on_stack

-  debug_object_activate

-  debug_object_deactivate

-  debug_object_destroy

-  debug_object_free

-  debug_object_assert_init

Each of these functions takes the address of the real object and a
pointer to the object type specific debug description structure.

Each detected error is reported in the statistics and a limited number
of errors are printk'ed including a full stack trace.

The statistics are available via /sys/kernel/debug/debug_objects/stats.
They provide information about the number of warnings and the number of
successful fixups along with information about the usage of the internal
tracking objects and the state of the internal tracking objects pool.

Debug functions
===============

.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_init

This function is called whenever the initialization function of a real
object is called.

When the real object is already tracked by debugobjects it is checked,
whether the object can be initialized. Initializing is not allowed for
active and destroyed objects. When debugobjects detects an error, then
it calls the fixup_init function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real initialization of the object happens. E.g. it
can deactivate an active object in order to prevent damage to the
subsystem.

When the real object is not yet tracked by debugobjects, debugobjects
allocates a tracker object for the real object and sets the tracker
object state to ODEBUG_STATE_INIT. It verifies that the object is not
on the callers stack. If it is on the callers stack then a limited
number of warnings including a full stack trace is printk'ed. The
calling code must use debug_object_init_on_stack() and remove the
object before leaving the function which allocated it. See next section.

.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_init_on_stack

This function is called whenever the initialization function of a real
object which resides on the stack is called.

When the real object is already tracked by debugobjects it is checked,
whether the object can be initialized. Initializing is not allowed for
active and destroyed objects. When debugobjects detects an error, then
it calls the fixup_init function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real initialization of the object happens. E.g. it
can deactivate an active object in order to prevent damage to the
subsystem.

When the real object is not yet tracked by debugobjects debugobjects
allocates a tracker object for the real object and sets the tracker
object state to ODEBUG_STATE_INIT. It verifies that the object is on
the callers stack.

An object which is on the stack must be removed from the tracker by
calling debug_object_free() before the function which allocates the
object returns. Otherwise we keep track of stale objects.

.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_activate

This function is called whenever the activation function of a real
object is called.

When the real object is already tracked by debugobjects it is checked,
whether the object can be activated. Activating is not allowed for
active and destroyed objects. When debugobjects detects an error, then
it calls the fixup_activate function of the object type description
structure if provided by the caller. The fixup function can correct the
problem before the real activation of the object happens. E.g. it can
deactivate an active object in order to prevent damage to the subsystem.

When the real object is not yet tracked by debugobjects then the
fixup_activate function is called if available. This is necessary to
allow the legitimate activation of statically allocated and initialized
objects. The fixup function checks whether the object is valid and calls
the debug_objects_init() function to initialize the tracking of this
object.

When the activation is legitimate, then the state of the associated
tracker object is set to ODEBUG_STATE_ACTIVE.


.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_deactivate

This function is called whenever the deactivation function of a real
object is called.

When the real object is tracked by debugobjects it is checked, whether
the object can be deactivated. Deactivating is not allowed for untracked
or destroyed objects.

When the deactivation is legitimate, then the state of the associated
tracker object is set to ODEBUG_STATE_INACTIVE.

.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_destroy

This function is called to mark an object destroyed. This is useful to
prevent the usage of invalid objects, which are still available in
memory: either statically allocated objects or objects which are freed
later.

When the real object is tracked by debugobjects it is checked, whether
the object can be destroyed. Destruction is not allowed for active and
destroyed objects. When debugobjects detects an error, then it calls the
fixup_destroy function of the object type description structure if
provided by the caller. The fixup function can correct the problem
before the real destruction of the object happens. E.g. it can
deactivate an active object in order to prevent damage to the subsystem.

When the destruction is legitimate, then the state of the associated
tracker object is set to ODEBUG_STATE_DESTROYED.

.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_free

This function is called before an object is freed.

When the real object is tracked by debugobjects it is checked, whether
the object can be freed. Free is not allowed for active objects. When
debugobjects detects an error, then it calls the fixup_free function of
the object type description structure if provided by the caller. The
fixup function can correct the problem before the real free of the
object happens. E.g. it can deactivate an active object in order to
prevent damage to the subsystem.

Note that debug_object_free removes the object from the tracker. Later
usage of the object is detected by the other debug checks.


.. kernel-doc:: lib/debugobjects.c
   :functions: debug_object_assert_init

This function is called to assert that an object has been initialized.

When the real object is not tracked by debugobjects, it calls
fixup_assert_init of the object type description structure provided by
the caller, with the hardcoded object state ODEBUG_NOT_AVAILABLE. The
fixup function can correct the problem by calling debug_object_init
and other specific initializing functions.

When the real object is already tracked by debugobjects it is ignored.

Fixup functions
===============

Debug object type description structure
---------------------------------------

.. kernel-doc:: include/linux/debugobjects.h
   :internal:

fixup_init
-----------

This function is called from the debug code whenever a problem in
debug_object_init is detected. The function takes the address of the
object and the state which is currently recorded in the tracker.

Called from debug_object_init when the object state is:

-  ODEBUG_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note, that the function needs to call the debug_object_init() function
again, after the damage has been repaired in order to keep the state
consistent.

fixup_activate
---------------

This function is called from the debug code whenever a problem in
debug_object_activate is detected.

Called from debug_object_activate when the object state is:

-  ODEBUG_STATE_NOTAVAILABLE

-  ODEBUG_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note that the function needs to call the debug_object_activate()
function again after the damage has been repaired in order to keep the
state consistent.

The activation of statically initialized objects is a special case. When
debug_object_activate() has no tracked object for this object address
then fixup_activate() is called with object state
ODEBUG_STATE_NOTAVAILABLE. The fixup function needs to check whether
this is a legitimate case of a statically initialized object or not. In
case it is it calls debug_object_init() and debug_object_activate()
to make the object known to the tracker and marked active. In this case
the function should return false because this is not a real fixup.

fixup_destroy
--------------

This function is called from the debug code whenever a problem in
debug_object_destroy is detected.

Called from debug_object_destroy when the object state is:

-  ODEBUG_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

fixup_free
-----------

This function is called from the debug code whenever a problem in
debug_object_free is detected. Further it can be called from the debug
checks in kfree/vfree, when an active object is detected from the
debug_check_no_obj_freed() sanity checks.

Called from debug_object_free() or debug_check_no_obj_freed() when
the object state is:

-  ODEBUG_STATE_ACTIVE

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

fixup_assert_init
-------------------

This function is called from the debug code whenever a problem in
debug_object_assert_init is detected.

Called from debug_object_assert_init() with a hardcoded state
ODEBUG_STATE_NOTAVAILABLE when the object is not found in the debug
bucket.

The function returns true when the fixup was successful, otherwise
false. The return value is used to update the statistics.

Note, this function should make sure debug_object_init() is called
before returning.

The handling of statically initialized objects is a special case. The
fixup function should check if this is a legitimate case of a statically
initialized object or not. In this case only debug_object_init()
should be called to make the object known to the tracker. Then the
function should return false because this is not a real fixup.

Known Bugs And Assumptions
==========================

None (knock on wood).

==================================
Assignment 1 - Kprobe based tracer
==================================

-  Deadline: :command:`Wednesday, April 7th 2021, 23:00`

Assignment's Objectives
=======================

*  gaining knowledge related to the instrumentation of functions in the Linux kernel (``kretprobes`` mechanism)
*  gaining knowledge regarding the ``/proc`` file system from the Linux kernel
*  get familiar with data structures specific to the Linux kernel (``hash table`` and ``list``)

Statement
=========

Build a kernel operations surveillant.

With this surveillant, we aim to intercept:

* ``kmalloc`` and ``kfree`` calls
* ``schedule`` calls
* ``up`` and ``down_interruptible`` calls
* ``mutex_lock`` and ``mutex_unlock`` calls

The surveillant will hold, at the process level, the number of calls for each of the above functions.
For the ``kmalloc`` and ``kfree`` calls the total quantity of allocated and deallocated memory will be
shown.

The surveillant will be implemented as a kernel module with the name ``tracer.ko``.

Implementation details
----------------------

The interception will be done by recording a sample (``kretprobe``) for each of the above functions. The
surveillant will retain a list/hashtable with the monitored processes and will account for
the above information for these processes.

For the control of the list/hashtable with the monitored processes, a char device called ``/dev/tracer``
will be used, with major `10` and minor `42`. It will expose an ``ioctl`` interface with two arguments:

* the first argument is the request to the monitoring subsystem:

    * ``TRACER_ADD_PROCESS``
    * ``TRACER_REMOVE_PROCESS``

* the second argument is the PID of the process for which the monitoring request will be executed

In order to create a char device with major `10` you will need to use the `miscdevice <https://elixir.bootlin.com/linux/latest/source/include/linux/miscdevice.h>`__ interface in the kernel.
Definitions of related macros can be found in the `tracer.h header <http://elf.cs.pub.ro/so2/res/teme/tracer.h>`__.

Since the ``kmalloc`` function is inline for instrumenting the allocated amount of memory, the ``__kmalloc``
function will be inspected as follows:

* a ``kretprobe`` will be used, which will retain the amount of memory allocated and the address of the allocated memory area.
* the ``.entry_handler`` and ``.handler`` fields in the ``kretprobe`` structure will be used to retain information about the amount of memory allocated and the address from which the allocated memory starts.

.. code-block:: C

    static struct kretprobe kmalloc_probe = {
       .entry_handler = kmalloc_probe_entry_handler, /* entry handler */
       .handler = kmalloc_probe_handler, /* return probe handler */
       .maxactive = 32,
    };

Since the ``kfree`` function only receives the address of the memory area to be freed, in order to determine
the total amount of memory freed, we will need to determine its size based on the address of the area.
This is possible because there is an address-size association made when inspecting the ``__kmalloc`` function.

For the rest of the instrumentation functions it is enough to use a ``kretprobe``.

.. code-block:: C

    static struct kretprobe up_probe = {
       .entry_handler = up_probe_handler,
       .maxactive = 32,
    };

The virtual machine kernel has the ``CONFIG_DEBUG_LOCK_ALLOC`` option enabled where the ``mutex_lock`` symbol
is a macro that expands to ``mutex_lock_nested``. Thus, in order to obtain information about the ``mutex_lock``
function you will have to instrument the ``mutex_lock_nested`` function.

Processes that have been added to the list/hashtable and that end their execution will be removed
from the list/hashtable. Also, a process will be removed from the dispatch list/hashtable following
the ``TRACER_REMOVE_PROCESS`` operation.

The information retained by the surveillant will be displayed via the procfs file system, in the ``/proc/tracer`` file.
For each monitored process an entry is created in the ``/proc/tracer`` file having as first field the process PID.
The entry will be read-only, and a read operation on it will display the retained results. An example of
displaying the contents of the entry is:

.. code-block:: console

    $cat /proc/tracer
    PID   kmalloc kfree kmalloc_mem kfree_mem  sched   up     down  lock   unlock
    42    12      12    2048        2048        124    2      2     9      9
    1099  0       0     0           0           1984   0      0     0      0
    1244  0       0     0           0           1221   100   1023   1023   1002
    1337  123     99    125952      101376      193821 992   81921  7421   6392

Testing
=======

In order to simplify the assignment evaluation process, but also to reduce the mistakes of the submitted assignments,
the assignment evaluation will be done automatically with the help of a
`test script <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/1-tracer/checker/_checker>`__ called `_checker`.
The test script assumes that the kernel module is called `tracer`.

Tips
----

Create the skeleton by running the command below:

.. code-block:: console

    $ LABS=assignments/1-tracer make skels

To increase your chances of getting the highest grade, read and follow the Linux kernel
coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

Also, use the following static analysis tools to verify the code:

- checkpatch.pl

.. code-block:: console

   $ linux/scripts/checkpatch.pl --no-tree --terse -f /path/to/your/tracer.c

- sparse

.. code-block:: console

   $ sudo apt-get install sparse
   $ cd linux
   $ make C=2 /path/to/your/tracer.c

- cppcheck

.. code-block:: console

   $ sudo apt-get install cppcheck
   $ cppcheck /path/to/your/tracer.c

Penalties
---------

Information about assigments penalties can be found on the
`General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__. In addition, the following
elements will be taken into account:

* *-2*: missing of proper disposal of resources (``kretprobes``, entries in ``/proc``)
* *-2*: data synchronization issues for data used by multiple executing instances (e.g. the list/hashtable)

In exceptional cases (the assigment passes the tests but it is not complying with the requirements)
and if the assigment does not pass all the tests, the grade may decrease more than mentioned above.

Submitting the assigment
------------------------

The assignment archive will be submitted to vmchecker, according to the rules on the
`rules page <https://ocw.cs.pub.ro/courses/so2/reguli-notare#reguli_de_trimitere_a_temelor>`__.

From the vmchecker interface choose the `Kprobe Based Tracer` option for this assigment.

Resources
=========

* `Documentation/kprobes.txt <https://www.kernel.org/doc/Documentation/kprobes.txt>`__ - description of the ``kprobes`` subsystem from Linux kernel sources.
* `samples/kprobes/ <https://elixir.bootlin.com/linux/latest/source/samples/kprobes>`__ - some examples of using ``kprobes`` from Linux kernel sources.

We recommend that you use gitlab to store your homework. Follow the directions in
`README <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/README.md>`__
and on the dedicated `git wiki page <https://ocw.cs.pub.ro/courses/so2/teme/folosire-gitlab>`__.

Questions
=========

For questions about the topic, you can consult the mailing `list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
or send an e-mail (you must be `registered <http://cursuri.cs.pub.ro/cgi-bin/mailman/listinfo/so2>`__).

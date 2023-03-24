=========================
Assignment 0 - Kernel API
=========================

-  Deadline: :command:`Sunday, 26 March 2023, 23:00`

Assignment's Objectives
=======================

*  getting familiar with the qemu setup
*  loading/unloading kernel modules
*  getting familiar with the list API implemented in the kernel
*  have fun :)

Statement
=========

Write a kernel module called `list` (the resulting file must be called `list.ko`) which stores data (strings)
in an internal list.

It is mandatory to use `the list API <https://github.com/torvalds/linux/blob/master/include/linux/list.h>`__
implemented in the kernel.
For details you can take a look at `the laboratory 2 <https://linux-kernel-labs.github.io/refs/heads/master/so2/lab2-kernel-api.html>`__.

The module exports a directory named :command:`list` to procfs. The directory contains two files:

-   :command:`management`: with write-only access; is the interface for transmitting commands to the kernel module
-   :command:`preview`: with read-only access; is the interface through which the internal contents of the kernel list can be viewed.

`The code skeleton <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/0-list/list.c>`__ implements the two procfs files.
You will need to create a list and implement support for `adding` and `reading` data. Follow the TODOs in the code for details.

To interact with the kernel list, you must write commands (using the `echo` command) in the `/proc/list/management` file:

- `addf name`: adds the `name` element to the top of the list
- `adde name`: adds the `name` element to the end of the list
- `delf name`: deletes the first appearance of the `name` item from the list
- `dela name`: deletes all occurrences of the `name` element in the list

Viewing the contents of the list is done by viewing the contents of the `/proc/list/preview` file (use the` cat` command).
The format contains one element on each line.

Testing
=======

In order to simplify the assignment evaluation process, but also to reduce the mistakes of the submitted assignments,
the assignment evaluation will be done automatically with the help of a
`test script <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/0-list/checker/_checker>`__ called `_checker`.
The test script assumes that the kernel module is called `list.ko`.

QuickStart
==========

It is mandatory to start the implementation of the assignment from the code skeleton found in the `list.c <https://gitlab.cs.pub.ro/so2/0-list/-/blob/master/src/list.c>`__ file.
You should follow the instructions in the `README.md file <https://gitlab.cs.pub.ro/so2/0-list/-/blob/master/README.md>`__ of the `assignment's repo <https://gitlab.cs.pub.ro/so2/0-list>`__.

Tips
----

To increase your chances of getting the highest grade, read and follow the Linux kernel
coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

Also, use the following static analysis tools to verify the code:

- checkpatch.pl

.. code-block:: console

   $ linux/scripts/checkpatch.pl --no-tree --terse -f /path/to/your/list.c

- sparse

.. code-block:: console

   $ sudo apt-get install sparse
   $ cd linux
   $ make C=2 /path/to/your/list.c

- cppcheck

.. code-block:: console

   $ sudo apt-get install cppcheck
   $ cppcheck /path/to/your/list.c

Penalties
---------
Information about assigments penalties can be found on the
`General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__.

In exceptional cases (the assigment passes the tests by not complying with the requirements)
and if the assigment does not pass all the tests, the grade will may decrease more than mentioned above.

Submitting the assigment
------------------------

The assignment will be graded automatically using the `vmchecker-next <https://github.com/systems-cs-pub-ro/vmchecker-next/wiki/Student-Handbook>`__ infrastructure.
The submission will be made on moodle on the `course's page <https://curs.upb.ro/2022/course/view.php?id=5121>`__ to the related assignment.
You will find the submission details in the `README.md file <https://gitlab.cs.pub.ro/so2/0-list/-/blob/master/README.md>`__ of the `repo <https://gitlab.cs.pub.ro/so2/0-list/-/blob/master>`__.

Resources
=========

We recommend that you use gitlab to store your homework. Follow the directions in
`README.md file <https://gitlab.cs.pub.ro/so2/0-list/-/blob/master/README.md>`__.

Questions
=========

For questions about the topic, you can consult the mailing `list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
or you can write a question on the dedicated Teams channel.
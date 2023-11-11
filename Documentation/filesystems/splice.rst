================
splice and pipes
================

splice API
==========

splice is a method for moving blocks of data around inside the kernel,
without continually transferring them between the kernel and user space.

.. kernel-doc:: fs/splice.c

pipes API
=========

Pipe interfaces are all for in-kernel (builtin image) use. They are not
exported for use by modules.

.. kernel-doc:: include/linux/pipe_fs_i.h
   :internal:

.. kernel-doc:: fs/pipe.c

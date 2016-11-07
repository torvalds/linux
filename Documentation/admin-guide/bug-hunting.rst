Bug hunting
+++++++++++

Last updated: 28 October 2016

Fixing the bug
==============

Nobody is going to tell you how to fix bugs. Seriously. You need to work it
out. But below are some hints on how to use the tools.

objdump
-------

To debug a kernel, use objdump and look for the hex offset from the crash
output to find the valid line of code/assembler. Without debug symbols, you
will see the assembler code for the routine shown, but if your kernel has
debug symbols the C code will also be available. (Debug symbols can be enabled
in the kernel hacking menu of the menu configuration.) For example::

    $ objdump -r -S -l --disassemble net/dccp/ipv4.o

.. note::

   You need to be at the top level of the kernel tree for this to pick up
   your C files.

If you don't have access to the code you can also debug on some crash dumps
e.g. crash dump output as shown by Dave Miller::

     EIP is at 	+0x14/0x4c0
      ...
     Code: 44 24 04 e8 6f 05 00 00 e9 e8 fe ff ff 8d 76 00 8d bc 27 00 00
     00 00 55 57  56 53 81 ec bc 00 00 00 8b ac 24 d0 00 00 00 8b 5d 08
     <8b> 83 3c 01 00 00 89 44  24 14 8b 45 28 85 c0 89 44 24 18 0f 85

     Put the bytes into a "foo.s" file like this:

            .text
            .globl foo
     foo:
            .byte  .... /* bytes from Code: part of OOPS dump */

     Compile it with "gcc -c -o foo.o foo.s" then look at the output of
     "objdump --disassemble foo.o".

     Output:

     ip_queue_xmit:
         push       %ebp
         push       %edi
         push       %esi
         push       %ebx
         sub        $0xbc, %esp
         mov        0xd0(%esp), %ebp        ! %ebp = arg0 (skb)
         mov        0x8(%ebp), %ebx         ! %ebx = skb->sk
         mov        0x13c(%ebx), %eax       ! %eax = inet_sk(sk)->opt

gdb
---

In addition, you can use GDB to figure out the exact file and line
number of the OOPS from the ``vmlinux`` file.

The usage of gdb requires a kernel compiled with ``CONFIG_DEBUG_INFO``.
This can be set by running::

  $ ./scripts/config -d COMPILE_TEST -e DEBUG_KERNEL -e DEBUG_INFO

On a kernel compiled with ``CONFIG_DEBUG_INFO``, you can simply copy the
EIP value from the OOPS::

 EIP:    0060:[<c021e50e>]    Not tainted VLI

And use GDB to translate that to human-readable form::

  $ gdb vmlinux
  (gdb) l *0xc021e50e

If you don't have ``CONFIG_DEBUG_INFO`` enabled, you use the function
offset from the OOPS::

 EIP is at vt_ioctl+0xda8/0x1482

And recompile the kernel with ``CONFIG_DEBUG_INFO`` enabled::

  $ make vmlinux
  $ gdb vmlinux
  (gdb) l *vt_ioctl+0xda8
  0x1888 is in vt_ioctl (drivers/tty/vt/vt_ioctl.c:293).
  288	{
  289		struct vc_data *vc = NULL;
  290		int ret = 0;
  291
  292		console_lock();
  293		if (VT_BUSY(vc_num))
  294			ret = -EBUSY;
  295		else if (vc_num)
  296			vc = vc_deallocate(vc_num);
  297		console_unlock();

or, if you want to be more verbose::

  (gdb) p vt_ioctl
  $1 = {int (struct tty_struct *, unsigned int, unsigned long)} 0xae0 <vt_ioctl>
  (gdb) l *0xae0+0xda8

You could, instead, use the object file::

  $ make drivers/tty/
  $ gdb drivers/tty/vt/vt_ioctl.o
  (gdb) l *vt_ioctl+0xda8

If you have a call trace, such as::

     Call Trace:
      [<ffffffff8802c8e9>] :jbd:log_wait_commit+0xa3/0xf5
      [<ffffffff810482d9>] autoremove_wake_function+0x0/0x2e
      [<ffffffff8802770b>] :jbd:journal_stop+0x1be/0x1ee
      ...

this shows the problem likely in the :jbd: module. You can load that module
in gdb and list the relevant code::

  $ gdb fs/jbd/jbd.ko
  (gdb) l *log_wait_commit+0xa3

Another very useful option of the Kernel Hacking section in menuconfig is
Debug memory allocations. This will help you see whether data has been
initialised and not set before use etc. To see the values that get assigned
with this look at ``mm/slab.c`` and search for ``POISON_INUSE``. When using
this an Oops will often show the poisoned data instead of zero which is the
default.

Once you have worked out a fix please submit it upstream. After all open
source is about sharing what you do and don't you want to be recognised for
your genius?

Please do read
ref:`Documentation/process/submitting-patches.rst <submittingpatches>` though
to help your code get accepted.

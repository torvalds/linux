=====================================
Customizing the Virtual Machine Setup
=====================================

Connect to the Virtual Machine via SSH
--------------------------------------

The default Yocto image for the QEMU virtual machine
(``core-image-minimal-qemu``) provides the minimal functionality to run the
kernel and kernel modules. For extra features, such as an SSH connection,
a more complete image is required, such as ``core-image-sato-dev-qemu``.

To use the new image, update the ``YOCTO_IMAGE`` variable in
``tools/labs/qemu/Makefile``:

.. code-block:: shell

   YOCTO_IMAGE = core-image-sato-qemu$(ARCH).ext4

When you start the virtual machine the first time using ``make boot`` with the
new image configuration, it will download the image and then boot the virtual
machine. The image is larger (around 400MB) than the minimal image so expect
some time for the download.

You then enter the virtual machine via ``minicom``, determine the IP address of
the ``eth0`` interface an then you can connect to the virtual machine via SSH:

.. code-block:: shell

   $ minicom -D serial.pts
   Poky (Yocto Project Reference Distro) 2.3 qemux86 /dev/hvc0

   qemux86 login: root
   root@qemux86:~# ip a s
   1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue qlen 1000
       link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
       inet 127.0.0.1/8 scope host lo
          valid_lft forever preferred_lft forever
       inet6 ::1/128 scope host 
          valid_lft forever preferred_lft forever
   2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast qlen 1000
       link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff
       inet 172.213.0.18/24 brd 172.213.0.255 scope global eth0
          valid_lft forever preferred_lft forever
       inet6 fe80::5054:ff:fe12:3456/64 scope link 
          valid_lft forever preferred_lft forever
   3: sit0@NONE: <NOARP> mtu 1480 qdisc noop qlen 1000
       link/sit 0.0.0.0 brd 0.0.0.0

   $ ssh -l root 172.213.0.18
   The authenticity of host '172.213.0.18 (172.213.0.18)' can't be established.
   RSA key fingerprint is SHA256:JUWUcD7LdvURNcamoPePMhqEjFFtUNLAqO+TtzUiv5k.
   Are you sure you want to continue connecting (yes/no)? yes
   Warning: Permanently added '172.213.0.18' (RSA) to the list of known hosts.
   root@qemux86:~# uname -a
   Linux qemux86 4.19.0+ #3 SMP Sat Apr 4 22:45:18 EEST 2020 i686 GNU/Linux

Connecting a Debugger to the Virtual Machine Kernel
---------------------------------------------------

You can use GDB to connect to the running virtual machine kernel and inspect
the state of the kernel. You run ``make gdb`` in ``tools/labs/``:

.. code-block:: shell

   .../linux/tools/labs$ make gdb
   ln -fs /home/tavi/src/linux/vmlinux vmlinux
   gdb -ex "target remote localhost:1234" vmlinux
   GNU gdb (Ubuntu 7.11.1-0ubuntu1~16.04) 7.11.1
   Copyright (C) 2016 Free Software Foundation, Inc.
   License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
   This is free software: you are free to change and redistribute it.
   There is NO WARRANTY, to the extent permitted by law.  Type "show copying"
   and "show warranty" for details.
   This GDB was configured as "x86_64-linux-gnu".
   Type "show configuration" for configuration details.
   For bug reporting instructions, please see:
   <http://www.gnu.org/software/gdb/bugs/>.
   Find the GDB manual and other documentation resources online at:
   <http://www.gnu.org/software/gdb/documentation/>.
   For help, type "help".
   Type "apropos word" to search for commands related to "word"...
   Reading symbols from vmlinux...done.
   Remote debugging using localhost:1234
   0xc13cf2f2 in native_safe_halt () at ./arch/x86/include/asm/irqflags.h:53
   53asm volatile("sti; hlt": : :"memory");
   (gdb) bt
   #0  0xc13cf2f2 in native_safe_halt () at ./arch/x86/include/asm/irqflags.h:53
   #1  arch_safe_halt () at ./arch/x86/include/asm/irqflags.h:95
   #2  default_idle () at arch/x86/kernel/process.c:341
   #3  0xc101f136 in arch_cpu_idle () at arch/x86/kernel/process.c:332
   #4  0xc106a6dd in cpuidle_idle_call () at kernel/sched/idle.c:156
   #5  do_idle () at kernel/sched/idle.c:245
   #6  0xc106a8c5 in cpu_startup_entry (state=<optimized out>)
   at kernel/sched/idle.c:350
   #7  0xc13cb14a in rest_init () at init/main.c:415
   #8  0xc1507a7a in start_kernel () at init/main.c:679
   #9  0xc10001da in startup_32_smp () at arch/x86/kernel/head_32.S:368
   #10 0x00000000 in ?? ()
   (gdb)

Rebuild the Kernel Image
------------------------

The kernel image is built the first time the VM is started. To rebuild the
kernel remove the kernel image file defined by the ``ZIMAGE`` variable in
``tools/labs/qemu/Makefile``:

.. code-block:: shell

   ZIMAGE = $(KDIR)/arch/$(ARCH)/boot/$(b)zImage

Typically the full path of the kernel is ``arch/x86/boot/bzImage``.

Once removed the kernel image is rebuild by using:

.. code-block:: shell

   ~/src/linux/tools/labs$ make zImage

or simply starting the virtual machine

.. code-block:: shell

   ~/src/linux/tools/labs$ make boot

Using Docker containers
-----------------------

If your setup doesn't allow the installation of the packages required for the
laboratory setup, you can build and run a container that has all the setup
already prepared for the virtual machine environment.

In order to run the containerized setup, you need to install the following
packages:

* ``docker``
* ``docker-compose``

In order to run the container infrastructure run the following command in the
``tools/labs/`` directory:

.. code-block:: shell

    sergiu@local:~/src/linux/tools/labs$ make docker-kernel
    ...
    ubuntu@so2:~$

The first time you run the command above, it will take a long time, because you
will have to build the container environment and install the required
applications.

Every time you run the ``make docker-kernel`` command, another shell will
connect to the container. This will allow you to work with multiple tabs.

All the commands that you would use in the regular environment can be used in
the containerized environment.

The linux repository is mounted in the ``/linux`` directory. All changes
you will make here will also be seen on your local instance.

In order to stop the container use the following command:

.. code-block:: shell

    make stop-docker-kernel

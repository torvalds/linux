.. _vm_link:

=====================
Virtual Machine Setup
=====================

Exercises are designed to run on a qemu based virtual machine. In
order to run the virtual machine you will need following packages:

* build-essential
* qemu-system-x86
* qemu-system-arm
* kvm
* python3

The virtual machine setup uses prebuild Yocto images that it downloads
from downloads.yocyoproject.org and a kernel image that it builds
itself. The following images are supported:

* core-image-minimal-qemu
* core-image-minimal-dev-qemu
* core-image-sato-dev-qemu
* core-image-sato-qemu
* core-image-sato-sdk-qemu

and can be selected from tools/labs/qemu/Makefile.


Starting the VM
---------------

The virtual machine scripts are available in tools/labs/qemeu and you
can can start the virtual machine by using the **boot** make target in
tools/labs:

.. code-block:: shell

   ~/src/linux/tools/labs$ make boot
   ARCH=x86 qemu/qemu.sh -kernel zImage.x86 -device virtio-serial \
		-chardev pty,id=virtiocon0 -device virtconsole,chardev=virtiocon0 \
		-net nic,model=virtio,vlan=0 -net tap,ifname=tap0,vlan=0,script=no,downscript=no\
		-drive file=rootfs.img,if=virtio,format=raw --append "root=/dev/vda console=hvc0" \
		--display none -s
   char device redirected to /dev/pts/19 (label virtiocon0)


.. note:: To show the qemu console use "QEMU_DISPLAY=sdl make
          boot". This will show the VGA output and will also give
          access to the standard keyboard.

.. _vm_interaction_link:

Connecting to the VM
--------------------

Once the machine is booted you can connect to it on the serial port. A
link named *serial.pts* is created to the right emulated serial port
and you can use **minicom**, **picocom** to connect to the virtual
machine from the host:

.. code-block:: shell

   $ minicom -D serial.pts

   Poky (Yocto Project Reference Distro) 2.3 qemux86 /dev/hvc0

   qemux86 login: root
   root@qemux86:~#

Networking is also setup and you can use ssh to connect to the virtual
machine after finding out the allocated IP address:

.. code-block:: shell

   $ minicom -D serial.pts

   Poky (Yocto Project Reference Distro) 2.3 qemux86 /dev/hvc0

   qemux86 login: root
   root@qemux86:~# ifconfig
   eth0      Link encap:Ethernet  HWaddr 52:54:00:12:34:56
             inet addr:172.20.0.6  Bcast:172.20.0.255  Mask:255.255.255.0
	     UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1
	     RX packets:41 errors:0 dropped:0 overruns:0 frame:0
	     TX packets:6 errors:0 dropped:0 overruns:0 carrier:0
	     collisions:0 txqueuelen:1000
	     RX bytes:7578 (7.4 KiB)  TX bytes:1296 (1.2 KiB)

   lo        Link encap:Local Loopback
	     inet addr:127.0.0.1  Mask:255.0.0.0
	     inet6 addr: ::1%134535719/128 Scope:Host
	     UP LOOPBACK RUNNING  MTU:65536  Metric:1
	     RX packets:0 errors:0 dropped:0 overruns:0 frame:0
	     TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
	     collisions:0 txqueuelen:1000
	     RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

   $ ssh root@172.20.0.6
   The authenticity of host '172.20.0.6 (172.20.0.6)' can't be established.
   RSA key fingerprint is SHA256:CW1opJUHi4LDt1lnKjBVv12kXZ4s+8rreMBm5Jsdm00.
   Are you sure you want to continue connecting (yes/no)? yes
   Warning: Permanently added '172.20.0.6' (RSA) to the list of known hosts.
   root@qemux86:~#

.. attention:: The Yocto core-image-minimal-qemu does not include an
               SSH server, so you will not able to connect via ssh if
               you are using this image.


Connecting a debugger to the VM kernel
--------------------------------------

You can connect gdb to the running VM kernel and inspect the state of
the kernel by running the *gdb* target from tools/labs:

.. code-block :: shell

   $ make gdb
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

Rebuild the kernel image
------------------------

The kernel image is built the first time the VM is started. To rebuild
the kernel remove the **zImage** file and run the zImage target (or
start the VM again).

.. add info about how to update the image

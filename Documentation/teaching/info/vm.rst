.. _vm_link:

=====================
Virtual Machine Setup
=====================

Practice work is designed to run on a QEMU based virtual machine. Kernel code
is developed and built on the host machine and then deployed and run on the
virtual machine.

In order to run and use the virtual machine the following packages are required
on a Debian/Ubuntu system:

* ``flex``
* ``bison``
* ``build-essential``
* ``gcc-multilib``
* ``libncurses5-dev``
* ``qemu-system-x86``
* ``qemu-system-arm``
* ``python3``
* ``minicom``

The ``kvm`` package is not strictly required, but will make the virtual machine
faster by using KVM support (with the ``-enable-kvm`` option to QEMU). If ``kvm``
is absent, the virtual machine will still run (albeit slower) using emulation.

The virtual machine setup uses prebuild Yocto images that it downloads and a
kernel image that it builds itself. The following images are supported:

* ``core-image-minimal-qemu``
* ``core-image-minimal-dev-qemu``
* ``core-image-sato-dev-qemu``
* ``core-image-sato-qemu``
* ``core-image-sato-sdk-qemu``

By default, ``core-image-minimal-qemu`` it used. This setting can be changed by
updating the ``YOCTO_IMAGE`` variable in ``tools/labs/qemu/Makefile``.

Starting the Virtual Machine
----------------------------

You start the virtual machine in the ``tools/labs/`` folder by running ``make
boot``:

.. code-block:: shell

   .../linux/tools/labs$ make boot

The first run of the ``make boot`` command will compile the kernel image and it
will take longer. Subsequent runs will only start the QEMU virtual machine,
with verbose output provided:

.. code-block:: shell

   .../linux/tools/labs$ make boot
   mkdir /tmp/tmp.7rWv63E9Wf
   sudo mount -t ext4 -o loop core-image-minimal-qemux86.ext4 /tmp/tmp.7rWv63E9Wf
   sudo make -C /home/razvan/school/so2/linux.git modules_install INSTALL_MOD_PATH=/tmp/tmp.7rWv63E9Wf
   make: Entering directory '/home/razvan/school/so2/linux.git'
     INSTALL crypto/crypto_engine.ko
     INSTALL drivers/crypto/virtio/virtio_crypto.ko
     INSTALL drivers/net/netconsole.ko
     DEPMOD  4.19.0+
   make: Leaving directory '/home/razvan/school/so2/linux.git'
   sudo umount /tmp/tmp.7rWv63E9Wf
   rmdir /tmp/tmp.7rWv63E9Wf
   sleep 1 && touch .modinst
   qemu/create_net.sh tap0

   dnsmasq: failed to create listening socket for 172.213.0.1: Address already in use
   qemu/create_net.sh tap1

   dnsmasq: failed to create listening socket for 127.0.0.1: Address already in use
   /home/razvan/school/so2/linux.git/tools/labs/templates/assignments/6-e100/nttcp -v -i &
   nttcp-l: nttcp, version 1.47
   nttcp-l: running in inetd mode on port 5037 - ignoring options beside -v and -p
   bind: Address already in use
   nttcp-l: service-socket: bind:: Address already in use, errno=98
   ARCH=x86 qemu/qemu.sh -kernel /home/razvan/school/so2/linux.git/arch/x86/boot/bzImage -device virtio-serial -chardev pty,id=virtiocon0 -device virtconsole,chardev=virtiocon0 -serial pipe:pipe1 -serial pipe:pipe2 -netdev tap,id=tap0,ifname=tap0,script=no,downscript=no -net nic,netdev=tap0,model=virtio -netdev tap,id=tap1,ifname=tap1,script=no,downscript=no -net nic,netdev=tap1,model=i82559er -drive file=core-image-minimal-qemux86.ext4,if=virtio,format=raw -drive file=disk1.img,if=virtio,format=raw -drive file=disk2.img,if=virtio,format=raw --append "root=/dev/vda loglevel=15 console=hvc0" --display none -s
   qemu-system-i386: -chardev pty,id=virtiocon0: char device redirected to /dev/pts/68 (label virtiocon0)

.. note:: To show the QEMU console use

.. code-block:: shell

   .../linux/tools/labs$ QEMU_DISPLAY=gtk make boot

          This will show the VGA output and will also give
          access to the standard keyboard.

.. note:: The virtual machine setup scripts and configuration files are located
          in ``tools/labs/qemu/``.

.. _vm_interaction_link:

Connecting to the Virtual Machine
---------------------------------

Once the virtual machine is started you can connect to it on the serial port. A
symbolic link named ``serial.pts`` is created to the emulated serial port
device:

.. code-block:: shell

   .../linux/tools/labs$ ls -l serial.pts
   lrwxrwxrwx 1 razvan razvan 11 Apr  1 08:03 serial.pts -> /dev/pts/68

On the host you use the ``minicom`` command to connect to the virtual machine
via the ``serial.pts`` link:

.. code-block:: shell

   .../linux/tools/labs$ minicom -D serial.pts
   [...]
   Poky (Yocto Project Reference Distro) 2.3 qemux86 /dev/hvc0

   qemux86 login: root
   root@qemux86:~#

.. note:: When you connect to the virtual machine, simply enter ``root`` at the
          login prompt and you will get a root console, no password required.

.. note:: You exit ``minicom`` by pressing ``Ctrl+a`` and then ``x``. You will
          get a confirmation prompt and then you will exit ``minicom``.

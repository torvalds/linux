=====================
Linux Kernel Teaching
=====================

This is a collection of lectures and labs Linux kernel topics. The
lectures focus on theoretical and Linux kernel exploration.


The labs focus on device drivers topics and they resemble "howto"
style documentation. Each topic has two parts:

* a walk-through the topic which contains an overview, the main
  abstractions, simple examples and pointers to APIs

* a hands-on part which contains a few exercises that should be
  resolved by the student; to focus on the topic at hand, the student
  is presented with a starting coding skeleton and with in-depth tips
  on how to solve the exercises

This content is based on the `Operatings Systems 2
<http://ocw.cs.pub.ro/courses/so2>`_ course from the Computer Science
and Engineering Department, the Faculty of Automatic Control and
Computers, University POLITEHNICA of Bucharest.

You can get the latest version at http://github.com/linux-kernel-labs.

To get started build the documentation from the sources after
installing docker-compose on you host:

.. code-block:: c

   cd tools/labs && make docker-docs

then point your browser at **Documentation/output/labs/index.html**.

Alternatively, you can build directly on the host (see
tools/labs/docs/Dockerfile for dependencies):

.. code-block:: c

   cd tools/labs && make docs

.. toctree::

   so2/index.rst

.. toctree::
   :caption: Lectures

   lectures/intro.rst
   lectures/syscalls.rst
   lectures/processes.rst
   lectures/interrupts.rst
   lectures/smp.rst
   lectures/address-space.rst
   lectures/memory-management.rst
   lectures/debugging.rst

.. toctree::
   :caption: Labs

   labs/infrastructure.rst
   labs/introduction.rst
   labs/kernel_modules.rst
   labs/kernel_api.rst
   labs/device_drivers.rst
   labs/interrupts.rst
   labs/deferred_work.rst
   labs/block_device_drivers.rst
   labs/filesystems_part1.rst
   labs/filesystems_part2.rst
   labs/networking.rst
   labs/memory_mapping.rst
   labs/device_model.rst

.. toctree::
   :caption: Useful info

   info/vm.rst
   info/extra-vm.rst
   info/contributing.rst


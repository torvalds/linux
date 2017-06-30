Linux device driver labs
========================

This is a collection of "howtos" for various device driver topics. For
each topic there are two parts:

* a walk-through the topic which contains an overview, the main
  abstractions, simple examples and pointers to APIs

* a hands-on part which contains a few exercises that should be
  resolved by the student; to focus on the topic at hand, the student
  is presented with a starting coding skeleton and with in-depth tips
  on how to solve the exercises

They are based on the labs of the `Operatings Systems 2
<http://ocw.cs.pub.ro/courses/so2>`_ course, Computer Science
department, Faculty of Automatic Control and Computers, Politehnica
Univesity of Bucharest.

You can get the latest version at http://github.com/linux-kernel-labs.

To get started build the documentation from the sources:

.. code-block:: c
   
   cd tools/labs && make docs

then point your browser at **Documentation/output/labs/index.html**.

.. toctree::

   vm.rst
   exercises.rst
   kernel_modules.rst
   kernel_api.rst
   device_drivers.rst
   interrupts.rst
   deferred_work.rst

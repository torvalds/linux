=================
Memory Management
=================

Linux memory management subsystem is responsible, as the name implies,
for managing the memory in the system. This includes implemnetation of
virtual memory and demand paging, memory allocation both for kernel
internal structures and user space programms, mapping of files into
processes address space and many other cool things.

Linux memory management is a complex system with many configurable
settings. Most of these settings are available via ``/proc``
filesystem and can be quired and adjusted using ``sysctl``. These APIs
are described in Documentation/sysctl/vm.txt and in `man 5 proc`_.

.. _man 5 proc: http://man7.org/linux/man-pages/man5/proc.5.html

Linux memory management has its own jargon and if you are not yet
familiar with it, consider reading
:ref:`Documentation/admin-guide/mm/concepts.rst <mm_concepts>`.

Here we document in detail how to interact with various mechanisms in
the Linux memory management.

.. toctree::
   :maxdepth: 1

   concepts
   hugetlbpage
   idle_page_tracking
   ksm
   numa_memory_policy
   pagemap
   soft-dirty
   transhuge
   userfaultfd

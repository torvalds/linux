ABI stable symbols
==================

Documents the interfaces that the developer has defined to be stable.

Userspace programs are free to use these interfaces with no
restrictions, and backward compatibility for them will be guaranteed
for at least 2 years.

Most interfaces (like syscalls) are expected to never change and always
be available.

.. kernel-abi:: $srctree/Documentation/ABI/stable
   :rst:

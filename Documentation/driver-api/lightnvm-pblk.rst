pblk: Physical Block Device Target
==================================

pblk implements a fully associative, host-based FTL that exposes a traditional
block I/O interface. Its primary responsibilities are:

  - Map logical addresses onto physical addresses (4KB granularity) in a
    logical-to-physical (L2P) table.
  - Maintain the integrity and consistency of the L2P table as well as its
    recovery from normal tear down and power outage.
  - Deal with controller- and media-specific constrains.
  - Handle I/O errors.
  - Implement garbage collection.
  - Maintain consistency across the I/O stack during synchronization points.

For more information please refer to:

  http://lightnvm.io

which maintains updated FAQs, manual pages, technical documentation, tools,
contacts, etc.

Description

  DRBD is a shared-nothing, synchronously replicated block device. It
  is designed to serve as a building block for high availability
  clusters and in this context, is a "drop-in" replacement for shared
  storage. Simplistically, you could see it as a network RAID 1.

  Please visit http://www.drbd.org to find out more.

The here included files are intended to help understand the implementation

DRBD-8.3-data-packets.svg, DRBD-data-packets.svg  
  relates some functions, and write packets.

conn-states-8.dot, disk-states-8.dot, node-states-8.dot
  The sub graphs of DRBD's state transitions

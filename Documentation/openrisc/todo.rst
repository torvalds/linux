====
TODO
====

The OpenRISC Linux port is fully functional and has been tracking upstream
since 2.6.35.  There are, however, remaining items to be completed within
the coming months.  Here's a list of known-to-be-less-than-stellar items
that are due for investigation shortly, i.e. our TODO list:

-  Implement the rest of the DMA API... dma_map_sg, etc.

-  Finish the renaming cleanup... there are references to or32 in the code
   which was an older name for the architecture.  The name we've settled on is
   or1k and this change is slowly trickling through the stack.  For the time
   being, or32 is equivalent to or1k.

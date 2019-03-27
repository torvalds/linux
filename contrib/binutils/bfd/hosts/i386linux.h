/* Linux writes the task structure at the end of the core file.  Currently it
   is 2912 bytes.  It is possible that this should be a pickier check, but
   we should probably not be too picky (the size of the task structure might
   vary, and if it's not the length we expect it to be, it doesn't affect
   our ability to process the core file).  So allow 0-4096 extra bytes at
   the end.  */

#define TRAD_CORE_EXTRA_SIZE_ALLOWED 4096

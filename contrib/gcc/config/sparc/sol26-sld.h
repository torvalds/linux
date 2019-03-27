/* Up through Solaris 2.6, the system linker does not work with DWARF2
   since it does not have working support for relocations to unaligned
   data.  */

#undef DWARF2_DEBUGGING_INFO

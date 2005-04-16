SECTIONS {
	. = SIZEOF_HEADERS;
	.rodata : { *(.rodata) } :ro
	. = 0xa0000;
	.data : { *(.data) } :dat
	/DISCARD/ : { *(*) }
}
PHDRS {
  ro PT_LOAD FILEHDR PHDRS;
  dat PT_LOAD;
}

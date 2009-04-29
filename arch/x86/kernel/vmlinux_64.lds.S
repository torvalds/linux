	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		__bss_start = .;		/* BSS */
		*(.bss.page_aligned)
		*(.bss)
		__bss_stop = .;
	}

	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.brk_reservation)	/* areas brk users have reserved */
		__brk_limit = .;
	}

	_end = . ;

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.exitcall.exit)
		*(.eh_frame)
		*(.discard)
	}

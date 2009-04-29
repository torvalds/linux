	PERCPU(PAGE_SIZE)

	. = ALIGN(PAGE_SIZE);
	/* freed after init ends here */

	/* BSS */
	.bss : AT(ADDR(.bss) - LOAD_OFFSET) {
		__init_end = .;
		__bss_start = .;
		*(.bss.page_aligned)
		*(.bss)
		. = ALIGN(4);
		__bss_stop = .;
	}

	.brk : AT(ADDR(.brk) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		__brk_base = .;
		. += 64 * 1024;		/* 64k alignment slop space */
		*(.brk_reservation)	/* areas brk users have reserved */
		__brk_limit = .;
	}

	.end : AT(ADDR(.end) - LOAD_OFFSET) {
		_end = . ;
	}

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.exitcall.exit)
		*(.discard)
	}

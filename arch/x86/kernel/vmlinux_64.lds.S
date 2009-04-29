#ifdef CONFIG_SMP
	/*
	 * percpu offsets are zero-based on SMP.  PERCPU_VADDR() changes the
	 * output PHDR, so the next output section - __data_nosave - should
	 * start another section data.init2.  Also, pda should be at the head of
	 * percpu area.  Preallocate it and define the percpu offset symbol
	 * so that it can be accessed as a percpu variable.
	 */
	. = ALIGN(PAGE_SIZE);
	PERCPU_VADDR(0, :percpu)
#else
	PERCPU(PAGE_SIZE)
#endif

	. = ALIGN(PAGE_SIZE);
	__init_end = .;

	.data_nosave : AT(ADDR(.data_nosave) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		__nosave_begin = .;
		*(.data.nosave)
		. = ALIGN(PAGE_SIZE);
		__nosave_end = .;
	} :data.init2
	/* use another section data.init2, see PERCPU_VADDR() above */

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

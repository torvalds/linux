	/* init_task */
	. = ALIGN(THREAD_SIZE);
	.data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
		*(.data.init_task)
	}

	. = ALIGN(PAGE_SIZE);
	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		/* might get freed after init */
		__smp_locks = .;
		*(.smp_locks)
		__smp_locks_end = .;
	}
	/* will be freed after init
	 * Following ALIGN() is required to make sure no other data falls on the
	 * same page where __smp_alt_end is pointing as that page might be freed
	 * after boot. Always make sure that ALIGN() directive is present after
	 * the section which contains __smp_alt_end.
	 */
	. = ALIGN(PAGE_SIZE);

	/* Init code and data - will be freed after init */
	. = ALIGN(PAGE_SIZE);
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		__init_begin = .;
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}

	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
	}

	. = ALIGN(16);
	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}
	.initcall.init : AT(ADDR(.initcall.init) - LOAD_OFFSET) {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

	.con_initcall.init : AT(ADDR(.con_initcall.init) - LOAD_OFFSET) {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	.x86_cpu_dev.init : AT(ADDR(.x86_cpu_dev.init) - LOAD_OFFSET) {
		__x86_cpu_dev_start = .;
		*(.x86_cpu_dev.init)
		__x86_cpu_dev_end = .;
	}

	SECURITY_INIT

	. = ALIGN(4);
	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
	}

	. = ALIGN(4);
	.parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
		__parainstructions = .;
		*(.parainstructions)
		__parainstructions_end = .;
	}

	/*
	 * .exit.text is discard at runtime, not link time, to deal with
	 *  references from .altinstructions and .eh_frame
	 */
	.exit.text : AT(ADDR(.exit.text) - LOAD_OFFSET) {
		EXIT_TEXT
	}

	.exit.data : AT(ADDR(.exit.data) - LOAD_OFFSET) {
		EXIT_DATA
	}

#if defined(CONFIG_BLK_DEV_INITRD)
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif

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

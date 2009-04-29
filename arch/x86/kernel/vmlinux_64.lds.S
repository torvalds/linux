	/* init_task */
	.data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
		. = ALIGN(THREAD_SIZE);
		*(.data.init_task)
	} :data.init

	.smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
		/* might get freed after init */
		. = ALIGN(PAGE_SIZE);
		__smp_alt_begin = .;
		__smp_locks = .;
		*(.smp_locks)
		__smp_locks_end = .;
		. = ALIGN(PAGE_SIZE);
		__smp_alt_end = .;
	}

	/* Init code and data */
	. = ALIGN(PAGE_SIZE);
	__init_begin = .;	/* paired with __init_end */
	.init.text : AT(ADDR(.init.text) - LOAD_OFFSET) {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}

	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		__initdata_begin = .;
		INIT_DATA
		__initdata_end = .;
	}

	.init.setup : AT(ADDR(.init.setup) - LOAD_OFFSET) {
		. = ALIGN(16);
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

	. = ALIGN(8);
	.parainstructions : AT(ADDR(.parainstructions) - LOAD_OFFSET) {
		__parainstructions = .;
		*(.parainstructions)
		__parainstructions_end = .;
	}

	.altinstructions : AT(ADDR(.altinstructions) - LOAD_OFFSET) {
		. = ALIGN(8);
		__alt_instructions = .;
		*(.altinstructions)
		__alt_instructions_end = .;
	}

	.altinstr_replacement : AT(ADDR(.altinstr_replacement) - LOAD_OFFSET) {
		*(.altinstr_replacement)
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

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : AT(ADDR(.init.ramfs) - LOAD_OFFSET) {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif

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

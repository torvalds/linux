	/* Align data segment to page size boundary */
	. = ALIGN(PAGE_SIZE);
	/* Data */
	.data : AT(ADDR(.data) - LOAD_OFFSET) {
		DATA_DATA
		CONSTRUCTORS
		/* End of data section */
		_edata = .;
	} :data


	.data.cacheline_aligned :
		AT(ADDR(.data.cacheline_aligned) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
		*(.data.cacheline_aligned)
	}

	. = ALIGN(CONFIG_X86_INTERNODE_CACHE_BYTES);
	.data.read_mostly : AT(ADDR(.data.read_mostly) - LOAD_OFFSET) {
		*(.data.read_mostly)
	}

#define VSYSCALL_ADDR (-10*1024*1024)
#define VSYSCALL_PHYS_ADDR ((LOADADDR(.data.read_mostly) + \
                            SIZEOF(.data.read_mostly) + 4095) & ~(4095))
#define VSYSCALL_VIRT_ADDR ((ADDR(.data.read_mostly) + \
                            SIZEOF(.data.read_mostly) + 4095) & ~(4095))

#define VLOAD_OFFSET (VSYSCALL_ADDR - VSYSCALL_PHYS_ADDR)
#define VLOAD(x) (ADDR(x) - VLOAD_OFFSET)

#define VVIRT_OFFSET (VSYSCALL_ADDR - VSYSCALL_VIRT_ADDR)
#define VVIRT(x) (ADDR(x) - VVIRT_OFFSET)

	. = VSYSCALL_ADDR;
	.vsyscall_0 : AT(VSYSCALL_PHYS_ADDR) {
		*(.vsyscall_0)
	} :user

	__vsyscall_0 = VSYSCALL_VIRT_ADDR;

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.vsyscall_fn : AT(VLOAD(.vsyscall_fn)) {
		*(.vsyscall_fn)
	}

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.vsyscall_gtod_data : AT(VLOAD(.vsyscall_gtod_data)) {
		*(.vsyscall_gtod_data)
	}

	vsyscall_gtod_data = VVIRT(.vsyscall_gtod_data);
	.vsyscall_clock : AT(VLOAD(.vsyscall_clock)) {
		*(.vsyscall_clock)
	}
	vsyscall_clock = VVIRT(.vsyscall_clock);


	.vsyscall_1 ADDR(.vsyscall_0) + 1024: AT(VLOAD(.vsyscall_1)) {
		*(.vsyscall_1)
	}
	.vsyscall_2 ADDR(.vsyscall_0) + 2048: AT(VLOAD(.vsyscall_2)) {
		*(.vsyscall_2)
	}

	.vgetcpu_mode : AT(VLOAD(.vgetcpu_mode)) {
		*(.vgetcpu_mode)
	}
	vgetcpu_mode = VVIRT(.vgetcpu_mode);

	. = ALIGN(CONFIG_X86_L1_CACHE_BYTES);
	.jiffies : AT(VLOAD(.jiffies)) {
		*(.jiffies)
	}
	jiffies = VVIRT(.jiffies);

	.vsyscall_3 ADDR(.vsyscall_0) + 3072: AT(VLOAD(.vsyscall_3)) {
		*(.vsyscall_3)
	}

	. = VSYSCALL_VIRT_ADDR + PAGE_SIZE;

#undef VSYSCALL_ADDR
#undef VSYSCALL_PHYS_ADDR
#undef VSYSCALL_VIRT_ADDR
#undef VLOAD_OFFSET
#undef VLOAD
#undef VVIRT_OFFSET
#undef VVIRT

	/* init_task */
	.data.init_task : AT(ADDR(.data.init_task) - LOAD_OFFSET) {
		. = ALIGN(THREAD_SIZE);
		*(.data.init_task)
	} :data.init

	.data.page_aligned : AT(ADDR(.data.page_aligned) - LOAD_OFFSET) {
		. = ALIGN(PAGE_SIZE);
		*(.data.page_aligned)
	}

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

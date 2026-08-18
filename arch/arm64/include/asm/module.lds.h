SECTIONS {
	.plt 0 : { BYTE(0) }
	.init.plt 0 : { BYTE(0) }
	.text.ftrace_trampoline 0 : { BYTE(0) }
	.init.text.ftrace_trampoline 0 : { BYTE(0) }

#ifdef CONFIG_UNWIND_TABLES
	/*
	 * Currently, we only use unwind info at module load time, so we can
	 * put it into the .init allocation.
	 */
	.init.eh_frame 0 : { *(.eh_frame) }
#endif
}

#ifdef CONFIG_ARM64_MODULE_PLTS
SECTIONS {
	.plt 0 (NOLOAD) : { BYTE(0) }
	.init.plt 0 (NOLOAD) : { BYTE(0) }
	.text.ftrace_trampoline 0 (NOLOAD) : { BYTE(0) }
}
#endif

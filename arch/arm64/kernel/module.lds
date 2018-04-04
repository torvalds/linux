SECTIONS {
	.plt (NOLOAD) : { BYTE(0) }
	.init.plt (NOLOAD) : { BYTE(0) }
	.text.ftrace_trampoline (NOLOAD) : { BYTE(0) }
}

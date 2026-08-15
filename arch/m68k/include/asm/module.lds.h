SECTIONS {
	.m68k_fixup 0 : {
		__start_fixup = .;
		*(.m68k_fixup)
		__stop_fixup = .;
	}
}

SECTIONS {
	.m68k_fixup : {
		__start_fixup = .;
		*(.m68k_fixup)
		__stop_fixup = .;
	}
}

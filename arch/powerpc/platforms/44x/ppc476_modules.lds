SECTIONS
{
	.text : ALIGN(4096)
	{
		*(.text .text.* .fixup)
	}
	.init.text : ALIGN(4096)
	{
		*(.init.text .init.text.*)
	}
	.exit.text : ALIGN(4096)
	{
		*(.exit.text .exit.text.*)
	}
}

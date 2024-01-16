/* Force alignment of .toc section.  */
SECTIONS
{
	.toc 0 : ALIGN(256)
	{
		*(.got .toc)
	}
}

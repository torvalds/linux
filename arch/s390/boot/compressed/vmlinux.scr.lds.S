/* SPDX-License-Identifier: GPL-2.0 */
SECTIONS
{
  .rodata.compressed : {
#ifndef CONFIG_KERNEL_UNCOMPRESSED
	input_len = .;
	LONG(input_data_end - input_data) input_data = .;
#endif
	*(.data)
#ifndef CONFIG_KERNEL_UNCOMPRESSED
	output_len = . - 4;
	input_data_end = .;
#endif
	}
}

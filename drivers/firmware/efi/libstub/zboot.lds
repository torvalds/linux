/* SPDX-License-Identifier: GPL-2.0 */

ENTRY(__efistub_efi_zboot_header);

PROVIDE(zboot_code_size = ABSOLUTE(0));

SECTIONS
{
	.head : ALIGN(4096) {
		*(.head)
	}

	.text : {
		*(.text* .init.text*)
	}

	.rodata : ALIGN(8) {
		__efistub__gzdata_start = .;
		*(.gzdata)
		__efistub_payload_size = . - 4;
		__efistub__gzdata_end = .;
		*(.rodata* .init.rodata* .srodata*)

		. = ALIGN(4);
		__efistub_code_size = .;
		LONG(zboot_code_size);

		_etext = ALIGN(4096);
		. = _etext;
	}

#ifdef CONFIG_EFI_SBAT
        .sbat : ALIGN(4096) {
		_sbat = .;
		*(.sbat)
		_esbat = ALIGN(4096);
		. = _esbat;
	}
#endif

	.data : ALIGN(4096) {
		_data = .;
		*(.data* .init.data*)
		_edata = ALIGN(512);
		. = _edata;
	}

	.bss : {
		*(.bss* .init.bss*)
		_end = ALIGN(512);
		. = _end;
	}

	/DISCARD/ : {
		*(.discard .discard.*)
		*(.modinfo .init.modinfo)
	}
}

PROVIDE(__efistub__gzdata_size =
		ABSOLUTE(__efistub__gzdata_end - __efistub__gzdata_start));

PROVIDE(__data_rawsize = ABSOLUTE(_edata - _etext));
PROVIDE(__data_size = ABSOLUTE(_end - _etext));
PROVIDE(__sbat_size = ABSOLUTE(_esbat - _sbat));

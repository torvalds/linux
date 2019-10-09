/* SPDX-License-Identifier: GPL-2.0-only */

SECTIONS {
	__mcount_loc : {
		*(__patchable_function_entries)
	}
}

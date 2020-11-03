/* SPDX-License-Identifier: GPL-2.0-or-later */

struct hd44780_common {
	void *hd44780;
};

struct hd44780_common *hd44780_common_alloc(void);

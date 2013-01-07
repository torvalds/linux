/*
 * Copyright (C) 2010 Nouveau Project
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <core/os.h>
#include <core/enum.h>

const struct nouveau_enum *
nouveau_enum_find(const struct nouveau_enum *en, u32 value)
{
	while (en->name) {
		if (en->value == value)
			return en;
		en++;
	}

	return NULL;
}

void
nouveau_enum_print(const struct nouveau_enum *en, u32 value)
{
	en = nouveau_enum_find(en, value);
	if (en)
		printk("%s", en->name);
	else
		printk("(unknown enum 0x%08x)", value);
}

void
nouveau_bitfield_print(const struct nouveau_bitfield *bf, u32 value)
{
	while (bf->name) {
		if (value & bf->mask) {
			printk(" %s", bf->name);
			value &= ~bf->mask;
		}

		bf++;
	}

	if (value)
		printk(" (unknown bits 0x%08x)", value);
}
